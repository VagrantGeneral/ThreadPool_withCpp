#pragma once
//******************************************************************************************************//
// <同步队列>——————>固定式线程池
// 
// 同步队列是线程池三层结构中的中间层，保证任务队列中的共享数据的线程安全；
// 为上层服务提供添加任务的接口；为下层服务提供获取任务的接口；同时限制任务上限，避免任务过多的问题；
// 在队列为空时，只能生产者Put添加；在队列为满时，只能消费者Take获取；不空不满时，生产者与消费者保持基本同步模型；
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
	std::list<Task> m_queue;			//以list存储任务
	std::mutex m_mutex;
	std::condition_variable m_notEmpty;	//对应消费者
	std::condition_variable m_notFull;	//对应生产者
	std::condition_variable m_waitStop;	//针对等待停止的条件变量
	size_t m_maxSize;		//任务上限
	size_t m_waitTime = 10;		//等待时间10ms
	bool m_Stop;		//false->运行,true->停止	

private:
	//template<typename F>
	//void Add(F&& f) {
	//	//F&& f	引用型别未定义
	//	std::unique_lock<std::mutex> locker(m_mutex);
	//	while (!m_Stop && IsFull()) {
	//		m_notFull.wait(locker);	//<1>弃锁 <2>进入条件变量的等待队列 
	//	}
	//	if (m_Stop) {
	//		//停止
	//		return;
	//	}
	//	m_queue.push_back(std::forward<F>(f));
	//	m_notEmpty.notify_all();	//<3>从条件变量的等待队列移入互斥锁的等待队列 <4>获锁
	//}

	template<typename F>
	int Add(F&& f) {
		//F&& f	引用型别未定义
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsFull()) {
			if (std::cv_status::timeout == m_notFull.wait_for(locker, std::chrono::milliseconds(m_waitTime))) {
				//我们要防止添加任务多次失败的情况
				return -1;
			}
		}
		if (m_Stop) {
			//停止
			return -2;
		}
		m_queue.push_back(std::forward<F>(f));
		m_notEmpty.notify_all();	//<3>从条件变量的等待队列移入互斥锁的等待队列 <4>获锁
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

	//强制停止——同步队列
	void Stop() {
		{
			std::unique_lock<std::mutex> locker(m_mutex);
			m_Stop = true;
		}
		//唤醒所有等待队列中的线程（通过m_Stop在运行中停止）
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

	//等待停止——同步队列
	void WaitStop() {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!IsEmpty()) {
			//m_waitStop.wait(locker);//没有任何东西唤醒它
			m_waitStop.wait_for(locker, std::chrono::milliseconds(10));		//延迟后面的停止操作。
		}
		m_Stop = true;
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

	//生产者
	int Put(const Task& task) {
		return Add(task);
	}


	int Put(Task&& task) {
		return Add(std::forward<Task>(task));
	}

	//消费者
	void Take(Task& task) {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsEmpty()) {
			m_notEmpty.wait(locker); 
		}
		if (m_Stop) {
			//是否停止
			return;
		}
		//钓鱼
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
		//钓鱼
		tasklist = std::move(m_queue);
		m_notFull.notify_all();
	}

	//对外接口
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
// <同步队列>——————>缓存式线程池
//	
// 解决缓存式线程池中的空闲时间问题。
// 对原先同步队列中的Take进行修改。将m_notEmpty.wait的使用修改为m_notEmpty.wait_for以控制等待时间；
//******************************************************************************************************//
template<class Task>
class SyncQueueToCache {
private:
	std::list<Task> m_queue;			//以list存储任务
	std::mutex m_mutex;
	std::condition_variable m_notEmpty;	//对应消费者
	std::condition_variable m_notFull;	//对应生产者
	std::condition_variable m_waitStop;	//针对等待停止的条件变量
	size_t m_maxSize;	//任务上限
	size_t m_waitTime = 10;		//等待时间10ms
	bool m_Stop;		//false->运行,true->停止	

private:
	//template<typename F>
	//void Add(F&& f) {
	//	//F&& f	引用型别未定义
	//	std::unique_lock<std::mutex> locker(m_mutex);
	//	while (!m_Stop && IsFull()) {
	//		m_notFull.wait(locker);	//<1>弃锁 <2>进入条件变量的等待队列 
	//	}
	//	if (m_Stop) {
	//		//停止
	//		return;
	//	}
	//	m_queue.push_back(std::forward<F>(f));
	//	m_notEmpty.notify_all();	//<3>从条件变量的等待队列移入互斥锁的等待队列 <4>获锁
	//}

	template<typename F>
	int Add(F&& f) {
		//F&& f	引用型别未定义
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsFull()) {
			if (std::cv_status::timeout == m_notFull.wait_for(locker, std::chrono::milliseconds(m_waitTime))) {
				//我们要防止添加任务多次失败的情况
				return -1;
			}
		}
		if (m_Stop) {
			//停止
			return -2;
		}
		m_queue.push_back(std::forward<F>(f));
		m_notEmpty.notify_all();	//<3>从条件变量的等待队列移入互斥锁的等待队列 <4>获锁
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

	//强制停止——同步队列
	void Stop() {
		{
			std::unique_lock<std::mutex> locker(m_mutex);
			m_Stop = true;
		}
		//唤醒所有等待队列中的线程（通过m_Stop在运行中停止）
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

	//等待停止——同步队列
	void WaitStop() {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!IsEmpty()) {
			//m_waitStop.wait(locker);//没有任何东西唤醒它
			m_waitStop.wait_for(locker, std::chrono::milliseconds(10));		//延迟后面的停止操作。
		}
		m_Stop = true;
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

	//生产者
	int Put(const Task& task) {
		return Add(task);
	}


	int Put(Task&& task) {
		return Add(std::forward<Task>(task));
	}

	//消费者
	int Take(Task& task) {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsEmpty()) {
			//m_notEmpty.wait(locker);
			if ( std::cv_status::timeout == m_notEmpty.wait_for(locker, std::chrono::seconds(1)) ) {
				//因为超时因而
				return -1
			}
		}
		if (m_Stop) {
			//是否停止
			return -2
		}
		//钓鱼
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
				//因为超时因而
				return -1
			}
		}
		if (m_Stop) {
			return -2
		}
		//钓鱼
		tasklist = std::move(m_queue);
		m_notFull.notify_all();
		return 0;
	}

	//对外接口
	//判断为空
	bool Empty() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue.empty();
	}

	//判断为满
	bool Full() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue.size() >= m_maxSize;
	}

	//获取大小
	size_t Size() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue.size();
	}


	size_t Count() const {
		return m_queue.size();
	}

	//析构
	~SyncQueueToCache() {
		//Stop();
	}
};


//******************************************************************************************************//
// <同步队列>——————>工作窃取线程池
//	
// 工作窃取线程池的同步队列使用“vector为引， list为体”实现一个线程对应一个任务队列的同步队列。 
//******************************************************************************************************//
template<class Task>
class SyncQueueToWork {
private:
	std::vector<std::list<Task>> m_queue;	//桶
	size_t m_backetSize;	//桶的大小————>vector的大小————>threadnum
	size_t m_maxSize;		//任务数量————>list的大小
	std::mutex m_mutex;
	std::condition_variable m_notEmpty;	//对应消费者
	std::condition_variable m_notFull;	//对应生产者
	std::condition_variable m_waitStop;	//针对等待停止的条件变量
	size_t m_waitTime = 10;	//等待时间10ms
	bool m_Stop;			//false->运行,true->停止	

private:
	template<typename F>
	int Add(F&& f, int index) {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsFull(index)) {
			if (std::cv_status::timeout == m_notFull.wait_for(locker, std::chrono::milliseconds(m_waitTime))) {
				//我们要防止添加任务多次失败的情况
				return -1;
			}
		}
		if (m_Stop) {
			//停止
			return -2;
		}
		m_queue[index].push_back(std::forward<F>(f));
		m_notEmpty.notify_all();	//<3>从条件变量的等待队列移入互斥锁的等待队列 <4>获锁
		return 0;
	}

	//为满
	bool IsFull(int index) {
		return m_queue[index].size() >= m_maxSize;
	}

	//为空
	bool IsEmpty(int index) {
		return m_queue[index].empty();
	}

	//获取任务总数
	size_t TotalTaskCount() const {
		size_t sum = 0;
		for (auto& tlist : m_queue) {
			sum += tlist.size();
		}
		return sum;
	}

public:
	//构造create
	SyncQueueToWork(size_t maxsize, size_t bucketsize)
		: m_maxSize(maxsize), m_Stop(false) {
		//设置vector大小
		m_queue.resize(bucketsize);
		
	}

	//强制停止——同步队列
	void Stop() {
		{
			std::unique_lock<std::mutex> locker(m_mutex);
			m_Stop = true;
		}
		//唤醒所有等待队列中的线程（通过m_Stop在运行中停止）
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

	//等待停止——同步队列
	void WaitStop() {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (TotalTaskCount() != 0) {
			//m_waitStop.wait(locker);//没有任何东西唤醒它
			m_waitStop.wait_for(locker, std::chrono::milliseconds(10));		//延迟后面的停止操作。
		}
		m_Stop = true;
		m_notEmpty.notify_all();
		m_notFull.notify_all();
	}

	//生产者————置任务
	int Put(Task&& task, int index) {
		return Add(std::forward<Task>(task), index);
	}

	//
	int Put(const Task& task, int index) {
		return Add(task, index);
	}

	//消费者————取任务
	int Task(Task& task, int index) {
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_Stop && IsEmpty(index)) {
			//m_notEmpty.wait(locker);
			if (std::cv_status::timeout == m_notEmpty.wait_for(locker, std::chrono::seconds(1))) {
				//因为超时因而
				return -1
			}
		}
		if (m_Stop) {
			//是否停止
			return -2
		}
		//钓鱼
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
		//		//因为超时因而
		//		return -1
		//	}
		//}
		if (!waitret) {
			return -1
		}

		if (m_Stop) {
			return -2
		}
		//钓鱼
		tasklist = std::move(m_queue);
		m_notFull.notify_all();
		return 0;
	}

	//是否为空
	bool Empty() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return TotalTaskCount() == 0;
	}

	//桶是否为空
	bool BucketEmpty(int index) const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue[index].empty();
	}

	//是否为满
	bool Full() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return TotalTaskCount() >= (m_maxSize * m_backetSize);
	}

	//桶是否为满
	bool BucketFull(int index) const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue[index].size() >= m_maxSize;
	}

	//获取总大小
	size_t Size() const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return TotalTaskCount();
	}


	size_t Count() const {
		return TotalTaskCount();
	}

	//获取桶大小
	size_t BucketSize(int index) const {
		std::unique_lock<std::mutex> locker(m_mutex);
		return m_queue[index].size();
	}





	//析构
	~SyncQueueToWork() {
		//
	}

};


//******************************************************************************************************//
// <同步队列>——————>计划线程池
//	
// 计划线程池的同步队列使用 
//******************************************************************************************************//
template<class Task>
class SyncQueueToScheduled {

};