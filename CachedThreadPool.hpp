#pragma once
//******************************************************************************************************//
// <缓存式线程池>
//
// 与固定式不同的是缓存式线程池没有固定数量，其线程数是动态调整的。
// 当有新任务提交时，如果线程池中有空闲的线程，则使用空线程；如果线程池中没有空闲线程，则会创建新线程执行；
// 当线程超过 最大空闲时间 时，此线程会被回收和销毁。
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
	SyncQueueToCache<Task> m_queue;	//同步队列
	std::map<std::thread::id, std::unique_ptr<std::thread>> m_threadgroup;	//线程组：存放 线程ID 以及 线程
	std::mutex m_mutex;			//总体管理
	std::condition_variable m_threadExit;	//
	std::atomic_int m_maxIdleTime{5};	//空闲时间5秒
	std::atomic_int m_maxThreadSize{16}; //std::thread::hardware_concurrency() + 1;		//上限
	std::atomic_int m_coreThreadSize{2};											//下限
	std::atomic<int> m_curThreadSize{0};	//线程总数
	std::atomic<int> m_idleThreadSize{0};	//空闲线程数
	std::atomic_bool m_running{false};		//是否运行
	std::once_flag m_flag;

private:
	//启动线程组
	void Start(int numthreads) {
		//std::map<std::thread::id, std::unique_ptr<std::thread>> m_threadgroup;
		m_running = true;
		m_curThreadSize = numthreads;
		for (int i = 0; i < numthreads; i++) {
			auto tha = std::make_unique<std::thread>(&CachedThreadPool::RunInThread, this);	//构建线程
			std::thread::id tid = tha->get_id();	//auto tid = tha->get_id(); 获取线程id。

			m_threadgroup.emplace(tid, std::move(tha));		//添加
			++m_idleThreadSize;
		}
	}

	//运行线程
	void RunInThread() {
		auto tid = std::this_thread::get_id();			//获取线程号
		auto startTime = std::chrono::high_resolution_clock::now();		//获取线程起始时间
		while (m_running) {
			Task task;

			//判断队列是否为空，若为空要适当减少线程数。
			if (m_queue.Size() == 0) {
				auto now = std::chrono::high_resolution_clock::now();	//获取线程当前时间
				auto intervalTime = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();	//时间差
				std::unique_lock<std::mutex> locker(m_mutex);
 				//线程空闲时间长且数量足够时，减少数量。
				std::lock_guard<std::mutex> locker(m_mutex);
				if ((intervalTime >= m_maxIdleTime) && (m_curThreadSize > m_coreThreadSize)) {
					m_threadgroup.find(tid)->second->detach();	//释放线程资源
					m_threadgroup.erase(tid);
					--m_curThreadSize;		//线程数--
					--m_idleThreadSize;		//空闲线程--
					//std::clog << "idle Thread size:" << m_idleThreadSize << std::endl;
					//std::clog << "cur Thread size:" << m_curThreadSize << std::endl;
					m_threadExit.notify_one();
					return;
				}
			}
			//队列不为空
			//取得任务	//task && m_running
			if (!m_queue.Take(task)) {
				--m_idleThreadSize;		//空闲线程--
				task();
				++m_idleThreadSize;		//空闲线程++
				startTime = std::chrono::high_resolution_clock::now();	//重新设定起始时间
			}
		}
	}

	//停止
	void StopThreadGroup() {
		//m_queue.WaitStop();
		//m_running = false;
		//for (auto& tha : m_threadgroup) {
		//	//tha.first; id
		//	if (tha.second && tha.second->joinable()) {
		//		//tha.second->join();	
		//		tha.second->detach();	//解决潜在的死锁问题
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

	//添加新线程
	void AddnewThread() {
		std::lock_guard<std::mutex> locker(m_mutex);
		if (m_idleThreadSize <= 0 && m_curThreadSize < m_maxThreadSize) {
			auto tha = std::make_unique<std::thread>(&CachedThreadPool::RunInThread, this);	//构建线程
			std::thread::id tid = tha->get_id();	//auto tid = tha->get_id(); 获取线程id。
			m_threadgroup.emplace(tid, std::move(tha));		//添加
			++m_idleThreadSize;
			++m_curThreadSize;

			//std::cout << "add new thread..." << std::endl;
			//std::cout << "idle thread size :" << m_idleThreadSize << std::endl;		//空闲线程数
			//std::cout << "cur  thread size :" << m_curThreadSize  << std::endl;		//总线程数
		}
	}


public:
	CachedThreadPool(size_t qusize = 200, int numthread = 2) : m_queue(qusize) {
		Start(numthread);
	}

	//线程组停止运行（只运行一次）
	void Stop() {
		//判断标志是否被运行过；若没被运行，就运行可调用对象；
		std::call_once(m_flag, [this]() { StopThreadGroup(); });
	}

	//添加任务
	template<typename Func, typename ... Args>
	auto AddTask_T(Func&& func, Args&& ... args) {
		using RetType = decltype(func(args...));

		//std::packaged_task<RetType()> task(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		auto task = std::make_shared<std::packaged_task<RetType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
		);

		std::future<RetType> result = task->get_future();	//获取返回值

		if (m_queue.Put([task]() {(*task)(); }) != 0) {
			//std::clog << "add task failed" << std::endl;
			(*task)();
		}

		//if (m_idleThreadSize <= 0 && m_curThreadSize < m_maxThreadSize) {
		//	auto tha = std::make_unique<std::thread>(&CachedThreadPool::RunInThread, this);	//构建线程
		//	std::thread::id tid = tha->get_id();	//auto tid = tha->get_id(); 获取线程id。
		//	m_threadgroup.emplace(tid, std::move(tha));		//添加
		//	++m_idleThreadSize;
		//	++m_curThreadSize;
		//}
		AddnewThread();
		return result;
	}

	//添加任务
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