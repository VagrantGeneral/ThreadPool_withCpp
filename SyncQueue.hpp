#pragma once
//******************************************************************************************************//
// <ͬ������>������������>�̶�ʽ�̳߳�
// 
// ͬ���������̳߳�����ṹ�е��м�㣬��֤��������еĹ������ݵ��̰߳�ȫ��
// Ϊ�ϲ�����ṩ�������Ľӿڣ�Ϊ�²�����ṩ��ȡ����Ľӿڣ�ͬʱ�����������ޣ����������������⣻
// �ڶ���Ϊ��ʱ��ֻ��������Put��ӣ��ڶ���Ϊ��ʱ��ֻ��������Take��ȡ�����ղ���ʱ���������������߱��ֻ���ͬ��ģ�ͣ�
//******************************************************************************************************// 
#include <list>
#include <deque>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <condition_variable>

template<class Task>
class SyncQueue {
private:
	std::list<Task> m_queue;			//��list�洢����
	std::mutex m_mutex;
	std::condition_variable m_notEmpty;	//��Ӧ������
	std::condition_variable m_notFull;	//��Ӧ������
	std::condition_variable m_waitStop;	//��Եȴ�ֹͣ����������
	size_t m_maxSize;		//��������
	size_t m_waitTime = 10;		//�ȴ�ʱ��10ms
	bool m_Stop;		//false->����,true->ֹͣ	

private:
	//template<typename F>
	//void Add(F&& f) {
	//	//F&& f	�����ͱ�δ����
	//	std::unique_lock<std::mutex> locker(m_mutex);
	//	while (!m_Stop && IsFull()) {
	//		m_notFull.wait(locker);	//<1>���� <2>�������������ĵȴ����� 
	//	}
	//	if (m_Stop) {
	//		//ֹͣ
	//		return;
	//	}
	//	m_queue.push_back(std::forward<F>(f));
	//	m_notEmpty.notify_all();	//<3>�����������ĵȴ��������뻥�����ĵȴ����� <4>����
	//}

	template<typename F>
	int Add(F&& f) {
		//F&& f	�����ͱ�δ����
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsFull()) {
			if (std::cv_status::timeout == m_notFull.wait_for(locker, std::chrono::milliseconds(m_waitTime))) {
				//����Ҫ��ֹ���������ʧ�ܵ����
				return -1;
			}
		}
		if (m_Stop) {
			//ֹͣ
			return -2;
		}
		m_queue.push_back(std::forward<F>(f));
		m_notEmpty.notify_all();	//<3>�����������ĵȴ��������뻥�����ĵȴ����� <4>����
		return 0;
	}	


	bool IsFull() const {
		return m_queue.size() >= m_maxSize;
	}


	bool IsEmpty() const {
		return m_queue.empty();
	}

public:
	SyncQueue(size_t maxsize) {
		m_maxSize = maxsize;
		m_Stop = false;
	}

	//ǿ��ֹͣ����ͬ������
	void Stop() {
		{
			std::unique_lock<std::mutex> locker(m_mutex);
			m_Stop = true;
		}
		//�������еȴ������е��̣߳�ͨ��m_Stop��������ֹͣ��
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

	//�ȴ�ֹͣ����ͬ������
	void WaitStop() {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!IsEmpty()) {
			//m_waitStop.wait(locker);//û���κζ���������
			m_waitStop.wait_for(locker, std::chrono::milliseconds(10));		//�ӳٺ����ֹͣ������
		}
		m_Stop = true;
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

	//������
	int Put(const Task& task) {
		return Add(task);
	}


	int Put(Task&& task) {
		return Add(std::forward<Task>(task));
	}

	//������
	void Take(Task& task) {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsEmpty()) {
			m_notEmpty.wait(locker); 
		}
		if (m_Stop) {
			//�Ƿ�ֹͣ
			return;
		}
		//����
		task = m_queue.front();
		m_queue.pop_front();
		m_notFull.notify_all();
	}


	void Take(std::list<Task>& tasklist) {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsEmpty()) {
			m_notEmpty.wait(locker);
		}
		if (m_Stop) {
			return;
		}
		//����
		tasklist = std::move(m_queue);
		m_notFull.notify_all();
	}

	//����ӿ�
	bool Empty() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue.empty();
	}


	bool Full() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue.size() >= m_maxSize;
	}


	size_t Size() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue.size();
	}


	size_t Count() const {
		return m_queue.size();
	}


	~SyncQueue() {
		//Stop();
	}
};


//******************************************************************************************************//
// <ͬ������>������������>����ʽ�̳߳�
//	
// �������ʽ�̳߳��еĿ���ʱ�����⡣
// ��ԭ��ͬ�������е�Take�����޸ġ���m_notEmpty.wait��ʹ���޸�Ϊm_notEmpty.wait_for�Կ��Ƶȴ�ʱ�䣻
//******************************************************************************************************//
template<class Task>
class SyncQueueToCache {
private:
	std::list<Task> m_queue;			//��list�洢����
	std::mutex m_mutex;
	std::condition_variable m_notEmpty;	//��Ӧ������
	std::condition_variable m_notFull;	//��Ӧ������
	std::condition_variable m_waitStop;	//��Եȴ�ֹͣ����������
	size_t m_maxSize;	//��������
	size_t m_waitTime = 10;		//�ȴ�ʱ��10ms
	bool m_Stop;		//false->����,true->ֹͣ	

private:
	//template<typename F>
	//void Add(F&& f) {
	//	//F&& f	�����ͱ�δ����
	//	std::unique_lock<std::mutex> locker(m_mutex);
	//	while (!m_Stop && IsFull()) {
	//		m_notFull.wait(locker);	//<1>���� <2>�������������ĵȴ����� 
	//	}
	//	if (m_Stop) {
	//		//ֹͣ
	//		return;
	//	}
	//	m_queue.push_back(std::forward<F>(f));
	//	m_notEmpty.notify_all();	//<3>�����������ĵȴ��������뻥�����ĵȴ����� <4>����
	//}

	template<typename F>
	int Add(F&& f) {
		//F&& f	�����ͱ�δ����
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsFull()) {
			if (std::cv_status::timeout == m_notFull.wait_for(locker, std::chrono::milliseconds(m_waitTime))) {
				//����Ҫ��ֹ���������ʧ�ܵ����
				return -1;
			}
		}
		if (m_Stop) {
			//ֹͣ
			return -2;
		}
		m_queue.push_back(std::forward<F>(f));
		m_notEmpty.notify_all();	//<3>�����������ĵȴ��������뻥�����ĵȴ����� <4>����
		return 0;
	}


	bool IsFull() const {
		return m_queue.size() >= m_maxSize;
	}


	bool IsEmpty() const {
		return m_queue.empty();
	}

public:
	SyncQueueToCache(size_t maxsize) {
		m_maxSize = maxsize;
		m_Stop = false;
	}

	//ǿ��ֹͣ����ͬ������
	void Stop() {
		{
			std::unique_lock<std::mutex> locker(m_mutex);
			m_Stop = true;
		}
		//�������еȴ������е��̣߳�ͨ��m_Stop��������ֹͣ��
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

	//�ȴ�ֹͣ����ͬ������
	void WaitStop() {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!IsEmpty()) {
			//m_waitStop.wait(locker);//û���κζ���������
			m_waitStop.wait_for(locker, std::chrono::milliseconds(10));		//�ӳٺ����ֹͣ������
		}
		m_Stop = true;
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

	//������
	int Put(const Task& task) {
		return Add(task);
	}


	int Put(Task&& task) {
		return Add(std::forward<Task>(task));
	}

	//������
	int Take(Task& task) {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsEmpty()) {
			//m_notEmpty.wait(locker);
			if ( std::cv_status::timeout == m_notEmpty.wait_for(locker, std::chrono::seconds(1)) ) {
				//��Ϊ��ʱ���
				return -1
			}
		}
		if (m_Stop) {
			//�Ƿ�ֹͣ
			return -2
		}
		//����
		task = m_queue.front();
		m_queue.pop_front();
		m_notFull.notify_all();
		return 0;
	}

	//
	int Take(std::list<Task>& tasklist) {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsEmpty()) {
			//m_notEmpty.wait(locker);
			if (std::cv_status::timeout == m_notEmpty.wait_for(locker, std::chrono::seconds(1))) {
				//��Ϊ��ʱ���
				return -1
			}
		}
		if (m_Stop) {
			return -2
		}
		//����
		tasklist = std::move(m_queue);
		m_notFull.notify_all();
		return 0;
	}

	//����ӿ�
	//�ж�Ϊ��
	bool Empty() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue.empty();
	}

	//�ж�Ϊ��
	bool Full() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue.size() >= m_maxSize;
	}

	//��ȡ��С
	size_t Size() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue.size();
	}


	size_t Count() const {
		return m_queue.size();
	}

	//����
	~SyncQueueToCache() {
		//Stop();
	}
};


//******************************************************************************************************//
// <ͬ������>������������>������ȡ�̳߳�
//	
// ������ȡ�̳߳ص�ͬ������ʹ�á�vectorΪ���� listΪ�塱ʵ��һ���̶߳�Ӧһ��������е�ͬ�����С� 
//******************************************************************************************************//
template<class Task>
class SyncQueueToWork {
private:
	std::vector<std::list<Task>> m_queue;	//Ͱ
	size_t m_backetSize;	//Ͱ�Ĵ�С��������>vector�Ĵ�С��������>threadnum
	size_t m_maxSize;		//����������������>list�Ĵ�С
	std::mutex m_mutex;
	std::condition_variable m_notEmpty;	//��Ӧ������
	std::condition_variable m_notFull;	//��Ӧ������
	std::condition_variable m_waitStop;	//��Եȴ�ֹͣ����������
	size_t m_waitTime = 10;	//�ȴ�ʱ��10ms
	bool m_Stop;			//false->����,true->ֹͣ	

private:
	template<typename F>
	int Add(F&& f, int index) {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsFull(index)) {
			if (std::cv_status::timeout == m_notFull.wait_for(locker, std::chrono::milliseconds(m_waitTime))) {
				//����Ҫ��ֹ���������ʧ�ܵ����
				return -1;
			}
		}
		if (m_Stop) {
			//ֹͣ
			return -2;
		}
		m_queue[index].push_back(std::forward<F>(f));
		m_notEmpty.notify_all();	//<3>�����������ĵȴ��������뻥�����ĵȴ����� <4>����
		return 0;
	}

	//Ϊ��
	bool IsFull(int index) {
		return m_queue[index].size() >= m_maxSize;
	}

	//Ϊ��
	bool IsEmpty(int index) {
		return m_queue[index].empty();
	}

	//��ȡ��������
	size_t TotalTaskCount() const {
		size_t sum = 0;
		for (auto& tlist : m_queue) {
			sum += tlist.size();
		}
		return sum;
	}

public:
	//����create
	SyncQueueToWork(size_t maxsize, size_t bucketsize)
		: m_maxSize(maxsize), m_Stop(false) {
		//����vector��С
		m_queue.resize(bucketsize);
		
	}

	//ǿ��ֹͣ����ͬ������
	void Stop() {
		{
			std::unique_lock<std::mutex> locker(m_mutex);
			m_Stop = true;
		}
		//�������еȴ������е��̣߳�ͨ��m_Stop��������ֹͣ��
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

	//�ȴ�ֹͣ����ͬ������
	void WaitStop() {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (TotalTaskCount() != 0) {
			//m_waitStop.wait(locker);//û���κζ���������
			m_waitStop.wait_for(locker, std::chrono::milliseconds(10));		//�ӳٺ����ֹͣ������
		}
		m_Stop = true;
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

	//�����ߡ�������������
	int Put(Task&& task, int index) {
		return Add(std::forward<Task>(task), index);
	}

	//
	int Put(const Task& task, int index) {
		return Add(task, index);
	}

	//�����ߡ�������ȡ����
	int Task(Task& task, int index) {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsEmpty(index)) {
			//m_notEmpty.wait(locker);
			if (std::cv_status::timeout == m_notEmpty.wait_for(locker, std::chrono::seconds(1))) {
				//��Ϊ��ʱ���
				return -1
			}
		}
		if (m_Stop) {
			//�Ƿ�ֹͣ
			return -2
		}
		//����
		task = m_queue[index].front();
		m_queue[index].pop_front();
		m_notFull.notify_all();
		return 0;
	}

	
	int Take(std::list<Task>& tasklist, const int index) {
		std::unique_lock<std::mutex> locker(m_mutex);
		bool waitret = m_notEmpty.wait_for(locker, std::chrono::seconds(m_waitTime), [this, index]()->bool {return m_needStop || !IsEmpty(index); });
		//while (!m_Stop && IsEmpty()) {
		//	//m_notEmpty.wait(locker);
		//	if (std::cv_status::timeout == m_notEmpty.wait_for(locker, std::chrono::seconds(1))) {
		//		//��Ϊ��ʱ���
		//		return -1
		//	}
		//}
		if (!waitret) {
			return -1
		}

		if (m_Stop) {
			return -2
		}
		//����
		tasklist = std::move(m_queue);
		m_notFull.notify_all();
		return 0;
	}

	//�Ƿ�Ϊ��
	bool Empty() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return TotalTaskCount() == 0;
	}

	//Ͱ�Ƿ�Ϊ��
	bool BucketEmpty(int index) const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue[index].empty();
	}

	//�Ƿ�Ϊ��
	bool Full() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return TotalTaskCount() >= (m_maxSize * m_backetSize);
	}

	//Ͱ�Ƿ�Ϊ��
	bool BucketFull(int index) const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue[index].size() >= m_maxSize;
	}

	//��ȡ�ܴ�С
	size_t Size() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return TotalTaskCount();
	}


	size_t Count() const {
		return TotalTaskCount();
	}

	//��ȡͰ��С
	size_t BucketSize(int index) const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue[index].size();
	}





	//����
	~SyncQueueToWork() {
		//
	}

};


//******************************************************************************************************//
// <ͬ������>������������>�ƻ��̳߳�
//	
// �ƻ��̳߳ص�ͬ������ʹ�� 
//******************************************************************************************************//
template<class Task>
class SyncQueueToScheduled {

};