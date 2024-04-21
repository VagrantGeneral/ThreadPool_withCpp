#pragma once
//******************************************************************************************************//
// <工作窃取线程池>
//
// 工作窃取线程池采用 “工作窃取算法” ，当某个线程执行完自己队列中的任务时，会从其他线程的队列中“窃取”任务执行。
// 工作窃取线程池可以设定多个工作线程，其都有一个自己的任务队列。每个工作线程优先执行自己队列的任务。当为空后，从
// 其他队列获取任务。
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

class WorKStealingPool {
public:
	using Task = std::function<void(void)>;

private:
	SyncQueueToWork<Task> m_queue;	//任务队列
	size_t m_numThreads;	//线程数
	std::vector<std::unique_ptr<std::thread>> m_threadgroup;	//线程组
	std::atomic_bool m_running;
	std::once_flag m_flag;

private:
	//启动线程组
	void Start(int numthreads) {
		m_running = true;
		for (int i = 0; i < numthreads; i++) {
			m_threadgroup.push_back(std::make_unique<std::thread>(&WorKStealingPool::RunInThread, this, i));
		}
	}

	//运行线程
	void RunInThread(const int index) {
		while (m_running) {
			std::list<Task> tasklist;
			if (m_queue.Take(tasklist, index) == 0) {
				for (auto& task : tasklist) {
					task();
				}
			}
			else {
				int i = threadIndex();
				if (i != index && m_queue.Take(tasklist, i) == 0) { 
					std::clog << "偷取任务成功..." << std::endl;
					for (auto& task : tasklist) {
						task();
					}
				}
			}
		}
	}

	//停止线程组
	void StopThreadGroup() {
		m_queue.Stop();
		m_running = false;
		for (auto& tha : m_threadgroup) {
			if (tha && tha->joinable()) {
				tha->join();
			}
		}
		m_threadgroup.clear();
	}

	//计算桶号
	int threadIndex() const {
		static int num = 0;
		return ++num % m_numThreads;
	}

public:
	//构造create
	WorKStealingPool(const int qusize = 100,const int numthreads = 8)
		: m_numThreads(numthreads), m_queue(m_numThreads, qusize), m_running(false) {
		Start(m_numThreads);
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
		return result;
	}

	//添加任务
	void AddTask(const Task& task) {
		if (m_queue.Put(task, threadIndex()) != 0) {
			std::clog << "add task failed" << std::endl;
			task();
		}
	}

	//
	void AddTask(Task&& task) {
		if (m_queue.Put(std::forward<Task>(task), threadIndex()) != 0) {
			std::clog << "add task failed" << std::endl;
			task();
		}
	}

	//线程组停止运行（只运行一次）
	void Stop() {
		//判断标志是否被运行过；若没被运行，就运行可调用对象；
		std::call_once(m_flag, [this]() { StopThreadGroup(); });
	}

	//析构
	~WorKStealingPool() {
		Stop();
	}
};

