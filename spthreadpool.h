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
const int TASK_MAX_THRESHOLD = 2; //INT32_MAX; //最大任务数量
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
		// 创建线程来执行线程函数
		std::thread t(func_, threadId_);
		t.detach();
	}

	//获取线程id
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
		// 等待线程池里所有的线程返回  线程池有两种状态：阻塞 & 正在执行任务中
		std::unique_lock<std::mutex> lck(taskQueMutx);
		notEmpty.notify_all(); //唤醒所有在notEmpty上阻塞的线程
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

	// 给线程池提交任务
	// 使用可变参模板编程，让submitTask可以接收任意任务函数和任意数量的参数
	// pool.submitTask(sum1, 10, 20);  右值引用 + 引用折叠原理
	// 返回值future<>
	template<typename Func, typename ...Args>
	auto submitTask(Func&& func, Args&& ... args) -> std::future<decltype(func(args...))>
	{
		//打包任务，放入任务队列里    decltype 自动推导该函数的返回值并不执行该函数
		using RType = decltype(func(args...));
		//如果不用智能指针应该写成：
		// task = new std::packaged_task<RType()>(std::forward<Func>(func), std::forward<Args>(args)...))
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> result = task->get_future();

		// 拿到锁
		std::unique_lock<std::mutex> lck(taskQueMutx);
		// 判断任务队列是否满了，满则等待，若等待时间超过1s，则提示任务提交失败
		if (!notFull_.wait_for(lck, std::chrono::seconds(1), [&]()->bool { return taskQue_.size() 
						< (size_t)taskSizeThreadhold_; }))
		{
			// 表示等待1s仍然没有提交成功
			std::cerr << "task queue is full, submit task fail" << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();
			return task->get_future();
		}
		else
		{
			// 提交任务
			taskQue_.emplace([task]() {(*task)(); });
			curTaskSize_++;
			//通知empty
			notEmpty.notify_all();

			// 需要根据任务数量和空闲线程的数量判断是否需要创建新的线程出来
			if (poolMode_ == PoolMode::MODE_CACHED
				&& curTaskSize_ > idleThreadSize_
				&& curThreadSize_ < threadSizeThreshHold_)
			{
				std::cout << " >>> 创建新线程..." << std::endl;
				auto p = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,
					std::placeholders::_1));
				int threadId = p->getId();
				//threads_.emplace_back(std::move(p));
				threads_.emplace(threadId, std::move(p));
				// 启动线程
				threads_[threadId]->start();

				//修改对应变量
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
		// 创建线程对象
		for (int i = 0; i < initThreadSize_; i++)
		{
			// threads是new出来的，所以要用智能指针来控制它的生命周期
			//等价写法 std::unique_ptr<Thread> ptr(new Thread(std::bind(&ThreadPool::threadFunc, this)));
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,
				std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			idleThreadSize_++;
		}

		//启动所有线程
		for (int i = 0; i < initThreadSize_; i++)
		{
			threads_[i]->start(); //需要去执行一个线程函数
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
		// 所有任务必须执行完，线程池才可以回收所有线程资源
		for (;;)
		{
			Task ta;
			{
				// 拿到锁
				std::unique_lock<std::mutex> lck(taskQueMutx);
				std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务" << std::endl;

				// 锁加双重(isThreadPoolRunning_)判断   解决第三种情况下，线程比线程池先拿到锁
				while (taskQue_.size() == 0)
				{
					// 线程池要结束，回收线程资源
					if (!isThreadPoolRunning_)
					{
						threads_.erase(threadId);
						std::cout << "threadId:" << std::this_thread::get_id() << "exit！！" << std::endl;
						exitCond_.notify_all();
						return; // 线程函数结束，线程结束
					}
					if (poolMode_ == PoolMode::MODE_CACHED)
					{
						// cached模式下，有可能已经创建了很多线程，但是空闲时间超过60s，
						// 应该把多余的线程结束并回收(超过initThreadSize的线程要回收)
						// 当前时间 - 上一次线程执行的时间 > 60s
						while (std::cv_status::timeout
							== notEmpty.wait_for(lck, std::chrono::seconds(1))) // 每一秒钟返回一次
						{
							// 条件变量超时返回，判断该线程的空闲时间。
							auto dur_time = std::chrono::duration_cast<std::chrono::seconds>
								(std::chrono::high_resolution_clock().now() - last_time);
							if (dur_time.count() > THREAD_MAX_IDLE_TIME
								&& curTaskSize_ > idleThreadSize_)
							{
								// 按线程id回收线程
								threads_.erase(threadId);
								std::cout << ">>>回收多余线程..." << std::endl;
								idleThreadSize_--;
								curThreadSize_--;
								return;
							}
						}
					}
					else // FIX模式下
					{
						// 当前任务为空，等待notEmpty通知 
						notEmpty.wait(lck);
					}
				}
				std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功" << std::endl;

				// 取任务
				ta = taskQue_.front();
				taskQue_.pop();
				//更新参数
				curThreadSize_--; // 任务数量-1
				idleThreadSize_--;  // 空闲线程数量-1
				curTaskSize_--; // 任务数量-1
				// 依然有剩余任务，继续通知其他线程执行任务
				if (taskQue_.size() > 0)
				{
					notEmpty.notify_all();
				}

				//取出一个任务后，通知可以继续提交任务
				notFull_.notify_all();
			} //此时就应该把锁释放了

			// 执行任务
			if (ta != nullptr) {// 执行任务

				ta();
			}
			idleThreadSize_++; // 该线程执行结束
			auto last_time = std::chrono::high_resolution_clock().now; // 任务执行完之后更新线程时间
		}
	}
private:

	int initThreadSize_;  // 初始的线程数量
	int threadSizeThreshHold_; // 线程数量上限阈值
	int taskSizeThreadhold_; // 任务队列数量上限

	std::atomic_bool isThreadPoolRunning_; //线程池是否运行

	std::atomic_int curTaskSize_;
	std::atomic_int idleThreadSize_; // 空闲线程的数量
	std::atomic_int curThreadSize_; // 记录当前线程池里线程的总数量 

	// 锁与信号量
	std::condition_variable notFull_;
	std::condition_variable notEmpty;
	std::condition_variable exitCond_;
	std::mutex taskQueMutx;

	//Task任务 =》 函数对象
	using Task = std::function<void()>;
	//std::vector<std::unique_ptr<Thread>> threads_;
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;
	std::queue<Task> taskQue_;
	PoolMode poolMode_;
};