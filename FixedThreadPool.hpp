#pragma once
//******************************************************************************************************//
// <固定式线程池>
// 
// 一个完整的线程池包括三层：同步服务层（生产者），排队层（同步队列），异步服务层（消费者）；
// ThreadPool中三个变量：线程组，同步队列，原子变量；
// 固定式线程池有固定大小，不会随着任务的增多而增多，任务的减小而减小；
//******************************************************************************************************//
#include "SyncQueue.hpp"
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <future>

class FixedThreadPool {
public:
	using Task = std::function<void(void)>;	//别名声明；（任务队列中存放线程函数）

private:
	SyncQueue<Task> m_queue;	//同步队列
	std::list<std::unique_ptr<std::thread>> m_threadgroup;	//线程组：存放“由unique_ptr管理的线程”
	std::atomic_bool m_running;	//标识当前线程池运行与否：true运行，false停止；
	std::once_flag m_flag;		//标识运行一次

private:
	//启动线程组
	void Start(int numthread) {
		m_running = true;
		for (int i = 0; i < numthread; i++) {
			//构建固定数量的线程，并指定任务（从任务队列获取到的任务）
			m_threadgroup.push_back(std::make_unique<std::thread>(&FixedThreadPool::RunInThread, this));
		}
	}

	//运行线程（获取任务）
	void RunInThread() {
		while (m_running) {
			Task task;
			m_queue.Take(task);		//从任务队列中取出任务
			if (task && m_running) {
				task();
			}
		}
	}

	//停止线程组
	void StopThreadGroup() {
		m_queue.WaitStop();
		m_running = false;
		for (auto& tha : m_threadgroup) {
			if (tha && tha->joinable()) {
				tha->join();
			}
		}
		m_threadgroup.clear();
	}

public:
	//构造————————————（消息队列大小，线程组大小）
	FixedThreadPool(size_t qusize,int numthread = 8) 
		: m_queue(qusize), m_running(false) {
		Start(numthread);
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
		if (m_queue.Put(task) != 0) {
			std::clog << "add task failed" << std::endl;
			task();
		}
	}

	void AddTask(Task&& task) {
		if (m_queue.Put(std::forward<Task>(task)) != 0) {
			std::clog << "add task failed" << std::endl;
			task();
		}
	}
	

	//线程组停止运行（只运行一次）
	void Stop() {
		//判断标志是否被运行过；若没被运行，就运行可调用对象；
		std::call_once(m_flag, [this]() { StopThreadGroup(); });
	}


	~FixedThreadPool() {
		Stop();
	}
};