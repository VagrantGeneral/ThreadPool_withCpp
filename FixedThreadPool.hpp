#pragma once
//******************************************************************************************************//
// <�̶�ʽ�̳߳�>
// 
// һ���������̳߳ذ������㣺ͬ������㣨�����ߣ����ŶӲ㣨ͬ�����У����첽����㣨�����ߣ���
// ThreadPool�������������߳��飬ͬ�����У�ԭ�ӱ�����
// �̶�ʽ�̳߳��й̶���С�����������������������࣬����ļ�С����С��
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
	using Task = std::function<void(void)>;	//��������������������д���̺߳�����

private:
	SyncQueue<Task> m_queue;	//ͬ������
	std::list<std::unique_ptr<std::thread>> m_threadgroup;	//�߳��飺��š���unique_ptr������̡߳�
	std::atomic_bool m_running;	//��ʶ��ǰ�̳߳��������true���У�falseֹͣ��
	std::once_flag m_flag;		//��ʶ����һ��

private:
	//�����߳���
	void Start(int numthread) {
		m_running = true;
		for (int i = 0; i < numthread; i++) {
			//�����̶��������̣߳���ָ�����񣨴�������л�ȡ��������
			m_threadgroup.push_back(std::make_unique<std::thread>(&FixedThreadPool::RunInThread, this));
		}
	}

	//�����̣߳���ȡ����
	void RunInThread() {
		while (m_running) {
			Task task;
			m_queue.Take(task);		//�����������ȡ������
			if (task && m_running) {
				task();
			}
		}
	}

	//ֹͣ�߳���
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
	//���졪��������������������������Ϣ���д�С���߳����С��
	FixedThreadPool(size_t qusize,int numthread = 8) 
		: m_queue(qusize), m_running(false) {
		Start(numthread);
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
	

	//�߳���ֹͣ���У�ֻ����һ�Σ�
	void Stop() {
		//�жϱ�־�Ƿ����й�����û�����У������пɵ��ö���
		std::call_once(m_flag, [this]() { StopThreadGroup(); });
	}


	~FixedThreadPool() {
		Stop();
	}
};