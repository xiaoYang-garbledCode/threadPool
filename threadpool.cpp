#include "threadpool.h"

const int MAX_TASKQUE_THRESHOLD = INT32_MAX;
const int MAX_THREAD_THRESHOLD = 200;
const int THREAD_IDLE_TIMEOUT = 60; //单位秒
ThreadPool::ThreadPool()
	:initTreadSize_(0)
	,curTaskSize_(0)
	,maxTaskQueThreshold_(MAX_TASKQUE_THRESHOLD)
	,poolMode_(PoolMode::FIXED_MODE)
	,isPoolRunning_(false)
	,idleThreadSize_(0)
	,maxThreadThreshold_(MAX_THREAD_THRESHOLD)
	,curThreadSize_(0)
{ 
}
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	// 等待线程池里所有的线程返回   里面的线程有两种状态 ：1.没有任务阻塞notEmpty.wait() 2.正在执行任务中

	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool { return threadsMap_.size() == 0; });
}

void ThreadPool::start(size_t initTreadSize) //开启线程池
{
	//设置线程池的运行状态
	isPoolRunning_ = true;
	//记录初始线程个数
	initTreadSize_ = initTreadSize;
	curThreadSize_ = initTreadSize;
	//创建线程对象
	for (int i = 0; i < initTreadSize; ++i)
	{
		// 创建thread对象的时候，把线程函数给到thread  auto:unique_ptr<Thread>
		auto threadPtr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = threadPtr->getThreadId();
		//threadsMap_.insert({ threadId,std::move(threadPtr) });
		threadsMap_.emplace(threadId, std::move(threadPtr));
	}

	//启动所有线程
	for (int i = 0; i < initTreadSize; ++i)
	{
		//线程的启动需要去执行一个线程函数   线程函数应该是去看任务队列是否有任务，然后抢一个任务执行
		//线程函数需要访问的任务队列，队列锁都在threadpool当中，所有将线程函数写在Thread当中就不好去访问这些变量
		//方法一可以去定义一个全局函数，但是还是无法访问threadpool当中的私有对象
		//所以现在需要让thread中的线程函数去执行threadpool定义好的线程函数
		threadsMap_[i]->start();
		idleThreadSize_++; //记录初始空闲线程的数量
	}
}

//检查pool的运行状态
bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

void ThreadPool::setMode(PoolMode poolMode)
{
	if (checkRunningState())  //如果线程池已经启动了，那么将不允许设置
		return;
	poolMode_ = poolMode;
}

void ThreadPool::setMaxThreadThreashold(size_t max_size)
{
	if (checkRunningState())
		return;
	if(poolMode_==PoolMode::CHACHED_MODE)
		maxThreadThreshold_ = max_size;
}

void ThreadPool::setMaxTaskQueThreashold(size_t max_size) //设置任务队列的上限阈值
{
	if (checkRunningState())//如果线程池已经启动了，那么将不允许设置
		return;
	maxTaskQueThreshold_ = max_size;
}

//给线程池提交任务    生产者
Result ThreadPool::subMitTask(std::shared_ptr<Task> taskPtr)//用户提交task
{
	//获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//cached模式任务处理比较紧急，适用于场景：小而快的任务 需要根据任务数量和空闲线程的数量来判断是否需要创建新的线程
	//队列已满，释放锁，进入wait状态，等待notify---变成阻塞状态 ---抢到锁----就绪状态
	// 用户提交任务最长不能超过一秒，否则判断任务提交失败，返回, 如果返回值为false，说明超过一秒钟了任务提交失败
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool { return threadsMap_.size() < maxTaskQueThreshold_; }))
	{
		std::cerr << "task queue is full, submit task fail." << std::endl;
		// return taskPtr->getResult(); // Task  Result 线程执行完task，task对象就被析构了
		return Result(taskPtr, false); // Result要延长taskPtr的生命周期，否则就得不到返回值了。false表示无效的返回值
	}
	//notFull_.wait(lock, [&]()->bool { return curTaskSize_ < maxTaskQueThreshold_; });

	//队列不满，开始生产
	taskQue_.emplace(taskPtr);
	curTaskSize_++;
	notEmpty_.notify_all(); //通知消费者线程

	if (poolMode_ == PoolMode::CHACHED_MODE
		&& idleThreadSize_ < curTaskSize_   //任务的数量大于空闲的线程数量
		&& threadsMap_.size() < maxThreadThreshold_) // 当前所有线程的数量是小于线程数量阈值
	{
		std::cout << ">>> create new thread..." << std::endl;
		//创建新线程对象
		auto threadPtr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadid = threadPtr->getThreadId();
		threadsMap_.emplace(threadid, std::move(threadPtr));
		threadsMap_[threadid]->start(); //启动线程
		idleThreadSize_++;
		curThreadSize_++;
	}

	//返回任务的Result对象
	//return taskPtr->getResult();
	return Result(taskPtr);
}

//定义线程函数   线程池的所有线程从任务队列中消费线程
void ThreadPool::threadFunc(int threadid)  //线程函数执行返回，相应的线程也就结束
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	for(;;)
	{
		//在获取到任务之后，就应该释放任务队列的锁，然后再去执行任务
		std::shared_ptr<Task> task;
		{
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务..." << std::endl;

			// cached模式下，有可能已经创建了很多线程，但是空闲时间超过60s，应该把多余的线程结束回收。
			//回收超过initThreadSize数量的线程
			//当前时间 - 上一次线程执行的时间 > 60s 时回收线程
		
				// 每一秒钟返回一次， 因为需要判断线程的空闲时间，超过60s的需要回收
				// 问题 如何区别是超时返回，还是有任务待执行的返回
				while (taskQue_.size() == 0)
				{
					//线程池要结束，回收线程资源
					if (!isPoolRunning_)
					{
						threadsMap_.erase(threadid);
						std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
						exitCond_.notify_all();
						return;
					}
					if (poolMode_ == PoolMode::CHACHED_MODE)
					{
						if (std::cv_status::timeout
							== notEmpty_.wait_for(lock, std::chrono::seconds(1)))//有两种返回值: no_timeout和timeout
						{
							//条件变量超时返回，计算
							auto now_time = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now_time - lastTime);
							if (dur.count() >= THREAD_IDLE_TIMEOUT
								&& threadsMap_.size() > initTreadSize_) //超过60s且线程池里的数量是大于initThreadSize_的
							{
								//回收当前线程
								//记录线程数量的相关变量的值的修改   
								//把线程对象从threadsVec容器中删除（所以这个容器中需要存储线程id）threadid => thread对象 => 删除
								//std::this_thread::getid() 是c++thread库生产的id，但我们自己定义Thread的id是generatedId
								threadsMap_.erase(threadid);
								curThreadSize_--;
								idleThreadSize_--;
								std::cout << "threadid" << std::this_thread::get_id() << "exit!" << std::endl;
								return;
							}
						}
					}
					//任务队列不空，取一个任务来执行
					else
					{
						notEmpty_.wait(lock);
					}

					//线程池要结束了，需要回收线程资源
					//if (!isPoolRunning_)
					//{
					//	threadsMap_.erase(threadid);
					//	//curThreadSize_--;
					//	//idleThreadSize_--;  线程池快结束了，就不需要再继续维护这两个变量了
					//	std::cout << "threadid: " << std::this_thread::get_id() << "exit" << std::endl;
					//	exitCond_.notify_all();
					//	return;
					//}
				}

			idleThreadSize_--; //取到任务了，空闲线程数量--
			std::cout << "tid: " << std::this_thread::get_id() << "获取任务成功" << std::endl;
			//取出一个任务执行
			task = taskQue_.front();
			taskQue_.pop();
			curTaskSize_--;

			//如果依然有剩余任务，继续通知其他线程来取任务
			if (curTaskSize_ > 0)
			{
				notEmpty_.notify_all();
			}
			//取完一个任务要进行通知
			notFull_.notify_all();
		}
		//执行任务  基类指针
		if (task != nullptr)
		{
			//task->run(); //执行任务，把任务的返回值通过setVal方法给到 Result中的any_
			task->exec();
			//task->getResult()->setVal(task->run());
		}

		idleThreadSize_++;//线程处理完任务了
		lastTime = std::chrono::high_resolution_clock().now(); //更新线程执行完任务的时间
	}
	// 启动的线程需要实现的线程函数，因为任务队列，任务队列的线程安全锁都在threadpool类的私有成员变量中，所以
	// 刚创建的线程需要执行的线程函数应该定义下threadpool当中，方便访问这些变量。
	/*std::cout << "begin threadFunc pid is: "  << std::this_thread::get_id() << std::endl;
	std::cout << "end thread pid is: " << std::this_thread::get_id() << std::endl;*/

	//正在执行的线程，发现线程池要结束，就跳出while循环
	threadsMap_.erase(threadid);
	std::cout << "threadid: " << std::this_thread::get_id() << "exit!" << std::endl;
	exitCond_.notify_all();
}

///////  线程方法实现
int Thread::generatedId_ = 0; //(静态的成员变量需要在类外进行初始化)

Thread::Thread(threadFunc func)//线程构造函数
	: threadFunc_(func)
	, threadId_(generatedId_++)
{}

Thread::~Thread() {}

void Thread::start()
{
	//创建一个线程来执行线程函数
	//对c++11来说， 有线程对象和线程函数，出了该作用域线程对象就析构，但是线程函数需要在，所以把线程对象设置为分离线程。
	std::thread t(threadFunc_, threadId_); //真正的启动线程
	t.detach(); //守护线程
}


//获取线程id
int Thread::getThreadId() const
{
	return threadId_;
}

/////// Result方法的实现
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:task_(task),
	isValid_(isValid)
{
	task_->setResult(this);
}


//问题二： get方法， 用户调用这个方法获取task的返回值。
Any Result::get() // 用户调用的
{
	if (!isValid_)
	{
		return ""; //返回值是无效的
	}
	//return any_; //有问题，匹配的左值的拷贝构造
	sem_.wait(); //等待线程函数执行完获取返回值，用户调用的时候任务没有执行完，这里将阻塞用户线程。
	return std::move(any_);
}
// 问题一：setVal方法， 获取任务执行完的返回值。（任务执行完的返回值在Task::run的中，如何将它的返回值存any_）
void Result::setVal(Any any) // threadFunc调用的
{
	// 存储task的返回值
	this->any_ = std::move(any);
	sem_.post(); //已经获取任务的返回值了，增加信号量资源，将结果返回给用户。
}


////////Task方法的实现
void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run()); //这里发生多态
	}
}

void Task::setResult(Result* res)
{
	this->result_ = res;
}

Task::Task()
	:result_(nullptr)
{}