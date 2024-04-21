#pragma once
//******************************************************************************************************//
// <����ʽ�̳߳�>
//
// ��̶�ʽ��ͬ���ǻ���ʽ�̳߳�û�й̶����������߳����Ƕ�̬�����ġ�
// �����������ύʱ������̳߳����п��е��̣߳���ʹ�ÿ��̣߳�����̳߳���û�п����̣߳���ᴴ�����߳�ִ�У�
// ���̳߳��� ������ʱ�� ʱ�����̻߳ᱻ���պ����١�
//******************************************************************************************************//
#include "SyncQueue.hpp"
#include <functional>
#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <future>
#include <map>

class CachedThreadPool {
public:
	using Task = std::function<void(void)>;

private:
	SyncQueueToCache<Task> m_queue;	//ͬ������
	std::map<std::thread::id, std::unique_ptr<std::thread>> m_threadgroup;	//�߳��飺��� �߳�ID �Լ� �߳�
	std::mutex m_mutex;			//�������
	std::condition_variable m_threadExit;	//
	std::atomic_int m_maxIdleTime{5};	//����ʱ��5��
	std::atomic_int m_maxThreadSize{16}; //std::thread::hardware_concurrency() + 1;		//����
	std::atomic_int m_coreThreadSize{2};											//����
	std::atomic<int> m_curThreadSize{0};	//�߳�����
	std::atomic<int> m_idleThreadSize{0};	//�����߳���
	std::atomic_bool m_running{false};		//�Ƿ�����
	std::once_flag m_flag;

private:
	//�����߳���
	void Start(int numthreads) {
		//std::map<std::thread::id, std::unique_ptr<std::thread>> m_threadgroup;
		m_running = true;
		m_curThreadSize = numthreads;
		for (int i = 0; i < numthreads; i++) {
			auto tha = std::make_unique<std::thread>(&CachedThreadPool::RunInThread, this);	//�����߳�
			std::thread::id tid = tha->get_id();	//auto tid = tha->get_id(); ��ȡ�߳�id��

			m_threadgroup.emplace(tid, std::move(tha));		//���
			++m_idleThreadSize;
		}
	}

	//�����߳�
	void RunInThread() {
		auto tid = std::this_thread::get_id();			//��ȡ�̺߳�
		auto startTime = std::chrono::high_resolution_clock::now();		//��ȡ�߳���ʼʱ��
		while (m_running) {
			Task task;

			//�ж϶����Ƿ�Ϊ�գ���Ϊ��Ҫ�ʵ������߳�����
			if (m_queue.Size() == 0) {
				auto now = std::chrono::high_resolution_clock::now();	//��ȡ�̵߳�ǰʱ��
				auto intervalTime = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();	//ʱ���
				std::unique_lock<std::mutex> locker(m_mutex);
 				//�߳̿���ʱ�䳤�������㹻ʱ������������
				std::lock_guard<std::mutex> locker(m_mutex);
				if ((intervalTime >= m_maxIdleTime) && (m_curThreadSize > m_coreThreadSize)) {
					m_threadgroup.find(tid)->second->detach();	//�ͷ��߳���Դ
					m_threadgroup.erase(tid);
					--m_curThreadSize;		//�߳���--
					--m_idleThreadSize;		//�����߳�--
					//std::clog << "idle Thread size:" << m_idleThreadSize << std::endl;
					//std::clog << "cur Thread size:" << m_curThreadSize << std::endl;
					m_threadExit.notify_one();
					return;
				}
			}
			//���в�Ϊ��
			//ȡ������	//task && m_running
			if (!m_queue.Take(task)) {
				--m_idleThreadSize;		//�����߳�--
				task();
				++m_idleThreadSize;		//�����߳�++
				startTime = std::chrono::high_resolution_clock::now();	//�����趨��ʼʱ��
			}
		}
	}

	//ֹͣ
	void StopThreadGroup() {
		//m_queue.WaitStop();
		//m_running = false;
		//for (auto& tha : m_threadgroup) {
		//	//tha.first; id
		//	if (tha.second && tha.second->joinable()) {
		//		//tha.second->join();	
		//		tha.second->detach();	//���Ǳ�ڵ���������
		//		--m_curThreadSize;
		//	}
		//}
		//m_threadgroup.clear();

		m_queue.WaitStop();
		m_coreThreadSize = 0;
		m_maxIdleTime = 1;
		std::unique_lock<std::mutex> locker(m_mutex);
		while (!m_threadgroup.empty()) {
			m_threadExit.wait_for(locker, std::chrono::milliseconds(1));
		}
		m_running = false;
	}

	//������߳�
	void AddnewThread() {
		std::lock_guard<std::mutex> locker(m_mutex);
		if (m_idleThreadSize <= 0 && m_curThreadSize < m_maxThreadSize) {
			auto tha = std::make_unique<std::thread>(&CachedThreadPool::RunInThread, this);	//�����߳�
			std::thread::id tid = tha->get_id();	//auto tid = tha->get_id(); ��ȡ�߳�id��
			m_threadgroup.emplace(tid, std::move(tha));		//���
			++m_idleThreadSize;
			++m_curThreadSize;

			//std::cout << "add new thread..." << std::endl;
			//std::cout << "idle thread size :" << m_idleThreadSize << std::endl;		//�����߳���
			//std::cout << "cur  thread size :" << m_curThreadSize  << std::endl;		//���߳���
		}
	}


public:
	CachedThreadPool(size_t qusize = 200, int numthread = 2) : m_queue(qusize) {
		Start(numthread);
	}

	//�߳���ֹͣ���У�ֻ����һ�Σ�
	void Stop() {
		//�жϱ�־�Ƿ����й�����û�����У������пɵ��ö���
		std::call_once(m_flag, [this]() { StopThreadGroup(); });
	}

	//�������
	template<typename Func, typename ... Args>
	auto AddTask_T(Func&& func, Args&& ... args) {
		using RetType = decltype(func(args...));

		//std::packaged_task<RetType()> task(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		auto task = std::make_shared<std::packaged_task<RetType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
		);

		std::future<RetType> result = task->get_future();	//��ȡ����ֵ

		if (m_queue.Put([task]() {(*task)(); }) != 0) {
			//std::clog << "add task failed" << std::endl;
			(*task)();
		}

		//if (m_idleThreadSize <= 0 && m_curThreadSize < m_maxThreadSize) {
		//	auto tha = std::make_unique<std::thread>(&CachedThreadPool::RunInThread, this);	//�����߳�
		//	std::thread::id tid = tha->get_id();	//auto tid = tha->get_id(); ��ȡ�߳�id��
		//	m_threadgroup.emplace(tid, std::move(tha));		//���
		//	++m_idleThreadSize;
		//	++m_curThreadSize;
		//}
		AddnewThread();
		return result;
	}

	//�������
	void AddTask(const Task& task) {
		if (m_queue.Put(task) != 0) {
			std::clog << "add task failed" << std::endl;
			task();
		}
		AddnewThread();
	}

	void AddTask(Task&& task) {
		if (m_queue.Put(std::forward<Task>(task)) != 0) {
			std::clog << "add task failed" << std::endl;
			task();
		}
		AddnewThread();
	}

	~CachedThreadPool() {
		Stop();
	}


};