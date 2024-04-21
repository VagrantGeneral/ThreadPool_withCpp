#pragma once
//******************************************************************************************************//
// <������ȡ�̳߳�>
//
// ������ȡ�̳߳ز��� ��������ȡ�㷨�� ����ĳ���߳�ִ�����Լ������е�����ʱ����������̵߳Ķ����С���ȡ������ִ�С�
// ������ȡ�̳߳ؿ����趨��������̣߳��䶼��һ���Լ���������С�ÿ�������߳�����ִ���Լ����е����񡣵�Ϊ�պ󣬴�
// �������л�ȡ����
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
	SyncQueueToWork<Task> m_queue;	//�������
	size_t m_numThreads;	//�߳���
	std::vector<std::unique_ptr<std::thread>> m_threadgroup;	//�߳���
	std::atomic_bool m_running;
	std::once_flag m_flag;

private:
	//�����߳���
	void Start(int numthreads) {
		m_running = true;
		for (int i = 0; i < numthreads; i++) {
			m_threadgroup.push_back(std::make_unique<std::thread>(&WorKStealingPool::RunInThread, this, i));
		}
	}

	//�����߳�
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
					std::clog << "͵ȡ����ɹ�..." << std::endl;
					for (auto& task : tasklist) {
						task();
					}
				}
			}
		}
	}

	//ֹͣ�߳���
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

	//����Ͱ��
	int threadIndex() const {
		static int num = 0;
		return ++num % m_numThreads;
	}

public:
	//����create
	WorKStealingPool(const int qusize = 100,const int numthreads = 8)
		: m_numThreads(numthreads), m_queue(m_numThreads, qusize), m_running(false) {
		Start(m_numThreads);
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
		return result;
	}

	//�������
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

	//�߳���ֹͣ���У�ֻ����һ�Σ�
	void Stop() {
		//�жϱ�־�Ƿ����й�����û�����У������пɵ��ö���
		std::call_once(m_flag, [this]() { StopThreadGroup(); });
	}

	//����
	~WorKStealingPool() {
		Stop();
	}
};

