#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <iostream>


/*
任意的其他类型 template
能让一个类型指向其他任意的类型： 基类类型    可以指向   派生类类型		
	Any  => Base*     ====>  Derive : public Base
							    data
*/

//Any类型可以接收任意数据类型
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete; //因为成员变量base是不允许左值拷贝构造的，因此这个类也不行
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// 这个构造函数可以让any类型接收任意其他的数据
	template<typename T>
	Any(T data): base_(std::make_unique<Derive<T>>(data))
	{}

	// 这个方法能把Any对象里存储的data数据提取出来
	template<typename T>
	T cast_()
	{
		// 怎么从base_找到它所指向的Derive对象，从它取出data成员变量
		//将基类指针转为派生类指针  四种类型强转 只有dynamic_cast 支持RTTI
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());//智能指针的get方法获得智能指针里存储的裸指针
		if (pd == nullptr)
		{
			//throw "type is incompatible";
			std::cout << "type is incompatible" << std::endl;
		}
		return pd->data_;
	}
private:
	// 基类类型
	class Base
	{
	public:
		// 基类指针指向的派生类如果是在堆上创建的，那么它的析构函数就无法调用，   ！！！什么时候基类的析构函数需要设置为虚函数
		virtual ~Base() = default; //使用默认析构
	};
	//派生类类型
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) :data_(data)
		{}
		T data_; //保存了任意其他类型
	};
private:
	//定义一个基类的指针
	std::unique_ptr<Base> base_;
};

/*
	信号量  ===》 mutex + 条件变量   信号量可以在一个线程执行wait，在另一个线程执行post（有资源初始值）
									 mutex来说，它的wait和post都只能是同一个线程来做
	线程的同步：
		线程互斥：mutex  atomic =====》》》》给总线加锁，让与内存交换成为原子操作  
		线程通信：条件变量  + 信号量
*/
//实现一个信号量
class Semaphore
{
public:
	Semaphore(int limit =0) : resLimit_(limit)
	{}
	~Semaphore() = default;
	//获取一个信号量资源
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
			//没有资源，当前线程状态变为wait
		cond_.wait(lock, [&]()->bool { return resLimit_ > 0; });
		resLimit_--;
	}
	//增加一个信号量资源
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:
	int resLimit_; //已经是线程安全的了 
	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task; //前置声明
// 实现接收提交到线程池的task任务执行完成后的返回值结果
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	// 问题一：setVal方法， 获取任务执行完的返回值。（任务执行完的返回值在Task::run的中，如何将它的返回值存到any_）
	void setVal(Any any);
	
	//问题二： get方法， 用户调用这个方法获取task的返回值。
	Any get();
private:
	Any any_; //存储任务的返回值
	Semaphore sem_; // 线程通信信号量
	std::shared_ptr<Task> task_; // 指向对应获取返回值的任务对象
	std::atomic_bool isValid_; //返回值是否有效
};


//任务抽象基类
class Task //用户可以自定义任意任务类型，从task继承，实现自定义任务处理
{
public:
//虚函数不能和模板一起使用， 虚函数在编译的时候要生产虚函数表，但是模板类型的函数还没有实例化。virtual T run(){}
	virtual Any run() = 0;
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
private:
	Result* result_;  //result对象的生命周期 长于 Task
};

//线程池支持的模式
enum class PoolMode
{
	FIXED_MODE,  //固定数量的线程
	CHACHED_MODE, //线程数量的动态增长
};



//线程类型
class Thread
{
public:
	//线程函数对象类型
	using threadFunc = std::function<void(int threadid)>;
	Thread(threadFunc func); //线程构造函数
	~Thread();
	void start();
	
	//获取线程id
	int getThreadId() const;
private:
	threadFunc threadFunc_; //接收的是threadpool中start方法创建线程的时候绑定的线程函数
	static int generatedId_;//设计一个局部范围内的id (静态的成员变量需要在类外进行初始化)
	int threadId_; //保存线程id
};

/*
example:

class Mytask : public Task
{
	void run() { // 线程代码...};
};

ThreadPool pool;
pool.start();
//make_shared它的好处是可以将对象的内存和引用计数的内存保存在一块
pool.submitTask(std::make_shared<Mytask>()); 
*/


//线程池类型
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	//开启线程池  hardware_concurrency返回的是当前系统cpu的核心数
	void start(size_t initTreadSize= std::thread::hardware_concurrency()); 

	void setMode(PoolMode poolMode);

	void setMaxTaskQueThreashold(size_t max_size); //设置任务队列的上限阈值

	void setMaxThreadThreashold(size_t max_size); //设置线程池cached模式下的线程阈值


	Result subMitTask(std::shared_ptr<Task> taskPtr);  //用户提交task

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
private:
	//定义线程函数
	void threadFunc(int threadid);
	//检查pool的运行状态
	bool checkRunningState() const;
private:
	//std::vector<Thread*> threadsVec_; //线程列表 Thread是我们new出来，但是vector析构的时候它的成员是指针，会造成内存泄漏
	//std::vector <std::unique_ptr<Thread>> threadsVec_;  
	std::unordered_map<int, std::unique_ptr<Thread>> threadsMap_; // 线程列表
	size_t maxThreadThreshold_; //线程vec数量的上限阈值
	size_t initTreadSize_; //初始线程数量
	std::atomic_size_t curThreadSize_; //记录当前线程池里线程的总数量  vector.size()也是记录线程总数的，但是它不是线程安全的
	std::atomic_size_t idleThreadSize_;//记录空闲线程的数量
	//不可以使用task的基类裸指针（否则要求用户要传入一个生命周期长的对象），防止用户传入task临时对象。
	//为什么使用shared_ptr 而不使用unique_ptr,因为【auto task = taskQue_.front();】取任务的时候等于是需要再利用一个指针来指向task
	
	std::queue<std::shared_ptr<Task>> taskQue_; //任务队列 
	std::atomic_uint curTaskSize_; //任务队列 （应该需要考虑线程安全）
	size_t maxTaskQueThreshold_; //任务队列的数量的上限阈值
	std::mutex taskQueMtx_; //保证任务队列的线程安全锁

	std::condition_variable notEmpty_;  //不空，说明可以消费
	std::condition_variable notFull_;	//不满，说明可以生产 
	std::condition_variable exitCond_;  //等待线程资源全部回收
	PoolMode poolMode_; //当前线程的工作模式
	std::atomic_bool isPoolRunning_;// 表示当前线程池的启动状态


};

#endif
