#pragma once
#include<thread>
#include<functional>
#include<mutex>
#include<queue>
#include<vector>
#include<memory>
#include<condition_variable>
#include<iostream>
#include<unordered_map>
#include<future>

const int THREAD_MAX_IDLE_TIME = 10;
const int TASK_MAX_THRESHOLD = 2; //INT32_MAX; //�����������
const int THREAD_MAX_THRESHOLD = 6;
enum class  PoolMode
{
	MODE_FIXED,
	MODE_CACHED,
};
class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func)
		:func_(func)
		, threadId_(generatedId_++)
	{}
	~Thread() = default;
	void start()
	{
		// �����߳���ִ���̺߳���
		std::thread t(func_, threadId_);
		t.detach();
	}

	//��ȡ�߳�id
	int getId() const
	{
		return threadId_;
	}
private:
	static int generatedId_;
	int threadId_;
	ThreadFunc func_;
};

int Thread::generatedId_ = 0;

class ThreadPool
{
public:
	ThreadPool()
		:initThreadSize_(0)
		, threadSizeThreshHold_(THREAD_MAX_THRESHOLD)
		, taskSizeThreadhold_(TASK_MAX_THRESHOLD)
		, poolMode_(PoolMode::MODE_FIXED)
		, idleThreadSize_(0)
		, curTaskSize_(0)
		, isThreadPoolRunning_(false)
	{}
	~ThreadPool()
	{
		isThreadPoolRunning_ = false;
		// �ȴ��̳߳������е��̷߳���  �̳߳�������״̬������ & ����ִ��������
		std::unique_lock<std::mutex> lck(taskQueMutx);
		notEmpty.notify_all(); //����������notEmpty���������߳�
		exitCond_.wait(lck, [&]()->bool { return threads_.size() == 0; });
	}

	void setMode(PoolMode mode)
	{
		if (checkThreadPoolRunning())
		{
			poolMode_ = mode;
		}
		return;
	}

	void setTaskQueMaxThreshold(int threshold)
	{
		if (checkThreadPoolRunning())
		{
			taskSizeThreadhold_ = threshold;
		}
	}

	void setThreadSizeThreshold(int threhold)
	{
		if (checkThreadPoolRunning())
		{
			if (poolMode_ == PoolMode::MODE_CACHED)
			{
				threadSizeThreshHold_ = threhold;
			}
		}
	}

	// ���̳߳��ύ����
	// ʹ�ÿɱ��ģ���̣���submitTask���Խ������������������������Ĳ���
	// pool.submitTask(sum1, 10, 20);  ��ֵ���� + �����۵�ԭ��
	// ����ֵfuture<>
	template<typename Func, typename ...Args>
	auto submitTask(Func&& func, Args&& ... args) -> std::future<decltype(func(args...))>
	{
		//������񣬷������������    decltype �Զ��Ƶ��ú����ķ���ֵ����ִ�иú���
		using RType = decltype(func(args...));
		//�����������ָ��Ӧ��д�ɣ�
		// task = new std::packaged_task<RType()>(std::forward<Func>(func), std::forward<Args>(args)...))
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		// �õ���
		std::unique_lock<std::mutex> lck(taskQueMutx);
		// �ж���������Ƿ����ˣ�����ȴ������ȴ�ʱ�䳬��1s������ʾ�����ύʧ��
		if (!notFull_.wait_for(lck, std::chrono::seconds(1), [&]()->bool { return taskQue_.size() 
						< (size_t)taskSizeThreadhold_; }))
		{
			// ��ʾ�ȴ�1s��Ȼû���ύ�ɹ�
			std::cerr << "task queue is full, submit task fail" << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();
			return task->get_future();
		}
		else
		{
			// �ύ����
			taskQue_.emplace([task]() {(*task)(); });
			curTaskSize_++;
			//֪ͨempty
			notEmpty.notify_all();

			// ��Ҫ�������������Ϳ����̵߳������ж��Ƿ���Ҫ�����µ��̳߳���
			if (poolMode_ == PoolMode::MODE_CACHED
				&& curTaskSize_ > idleThreadSize_
				&& curThreadSize_ < threadSizeThreshHold_)
			{
				std::cout << " >>> �������߳�..." << std::endl;
				auto p = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,
					std::placeholders::_1));
				int threadId = p->getId();
				//threads_.emplace_back(std::move(p));
				threads_.emplace(threadId, std::move(p));
				// �����߳�
				threads_[threadId]->start();

				//�޸Ķ�Ӧ����
				idleThreadSize_++;
				curThreadSize_++;
			}
			// std::future<RType> result = task->get_future();
			return result;
		}

	}

	void start(int initThreadSize = std::thread::hardware_concurrency())
	{
		initThreadSize_ = initThreadSize;
		isThreadPoolRunning_ = true;
		// �����̶߳���
		for (int i = 0; i < initThreadSize_; i++)
		{
			// threads��new�����ģ�����Ҫ������ָ��������������������
			//�ȼ�д�� std::unique_ptr<Thread> ptr(new Thread(std::bind(&ThreadPool::threadFunc, this)));
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,
				std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			idleThreadSize_++;
		}

		//���������߳�
		for (int i = 0; i < initThreadSize_; i++)
		{
			threads_[i]->start(); //��Ҫȥִ��һ���̺߳���
		}
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	bool checkThreadPoolRunning() const
	{
		return isThreadPoolRunning_;
	}
	void threadFunc(int threadId)
	{
		auto last_time = std::chrono::high_resolution_clock().now();
		// �����������ִ���꣬�̳߳زſ��Ի��������߳���Դ
		for (;;)
		{
			Task ta;
			{
				// �õ���
				std::unique_lock<std::mutex> lck(taskQueMutx);
				std::cout << "tid:" << std::this_thread::get_id() << "���Ի�ȡ����" << std::endl;

				// ����˫��(isThreadPoolRunning_)�ж�   �������������£��̱߳��̳߳����õ���
				while (taskQue_.size() == 0)
				{
					// �̳߳�Ҫ�����������߳���Դ
					if (!isThreadPoolRunning_)
					{
						threads_.erase(threadId);
						std::cout << "threadId:" << std::this_thread::get_id() << "exit����" << std::endl;
						exitCond_.notify_all();
						return; // �̺߳����������߳̽���
					}
					if (poolMode_ == PoolMode::MODE_CACHED)
					{
						// cachedģʽ�£��п����Ѿ������˺ܶ��̣߳����ǿ���ʱ�䳬��60s��
						// Ӧ�ðѶ�����߳̽���������(����initThreadSize���߳�Ҫ����)
						// ��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s
						while (std::cv_status::timeout
							== notEmpty.wait_for(lck, std::chrono::seconds(1))) // ÿһ���ӷ���һ��
						{
							// ����������ʱ���أ��жϸ��̵߳Ŀ���ʱ�䡣
							auto dur_time = std::chrono::duration_cast<std::chrono::seconds>
								(std::chrono::high_resolution_clock().now() - last_time);
							if (dur_time.count() > THREAD_MAX_IDLE_TIME
								&& curTaskSize_ > idleThreadSize_)
							{
								// ���߳�id�����߳�
								threads_.erase(threadId);
								std::cout << ">>>���ն����߳�..." << std::endl;
								idleThreadSize_--;
								curThreadSize_--;
								return;
							}
						}
					}
					else // FIXģʽ��
					{
						// ��ǰ����Ϊ�գ��ȴ�notEmpty֪ͨ 
						notEmpty.wait(lck);
					}
				}
				std::cout << "tid:" << std::this_thread::get_id() << "��ȡ����ɹ�" << std::endl;

				// ȡ����
				ta = taskQue_.front();
				taskQue_.pop();
				//���²���
				curThreadSize_--; // ��������-1
				idleThreadSize_--;  // �����߳�����-1
				curTaskSize_--; // ��������-1
				// ��Ȼ��ʣ�����񣬼���֪ͨ�����߳�ִ������
				if (taskQue_.size() > 0)
				{
					notEmpty.notify_all();
				}

				//ȡ��һ�������֪ͨ���Լ����ύ����
				notFull_.notify_all();
			} //��ʱ��Ӧ�ð����ͷ���

			// ִ������
			if (ta != nullptr) {// ִ������

				ta();
			}
			idleThreadSize_++; // ���߳�ִ�н���
			auto last_time = std::chrono::high_resolution_clock().now; // ����ִ����֮������߳�ʱ��
		}
	}
private:

	int initThreadSize_;  // ��ʼ���߳�����
	int threadSizeThreshHold_; // �߳�����������ֵ
	int taskSizeThreadhold_; // ���������������

	std::atomic_bool isThreadPoolRunning_; //�̳߳��Ƿ�����

	std::atomic_int curTaskSize_;
	std::atomic_int idleThreadSize_; // �����̵߳�����
	std::atomic_int curThreadSize_; // ��¼��ǰ�̳߳����̵߳������� 

	// �����ź���
	std::condition_variable notFull_;
	std::condition_variable notEmpty;
	std::condition_variable exitCond_;
	std::mutex taskQueMutx;

	//Task���� =�� ��������
	using Task = std::function<void()>;
	//std::vector<std::unique_ptr<Thread>> threads_;
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	std::queue<Task> taskQue_;
	PoolMode poolMode_;
};