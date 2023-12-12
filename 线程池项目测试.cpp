#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include "threadpool.h"

/*
* 有些场景是希望能够获取线程执行任务返回值的
* 举例：（多线程计算）
  1 + .... + 30000的和
  thread1  1 + ... + 10000
  thread2  10001 + ... 20000
  ...

  main thread  给每个线程分配计算区间，并等待它们返回结果，最终合并结果
*/
using Llong = long long;

class Mytask : public Task
{
public:
	Mytask(Llong begin, Llong end)
		:begin_(begin),
		 end_(end)
	{}
	//问题一  如何设置run函数的返回值，可以表示任意的类型
	// java Python 的Object是所有其他类型的基类
	// c++17 any类型
	Any run()  //run方法最终就在线程池分配的线程中去执行
	{
		Llong sum = 0;
		std::cout << "tid: " << std::this_thread::get_id() << " doing task" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(1));
		for (Llong i = begin_; i < end_; ++i)
		{
			sum += i;
		}
		return sum;
	}
private:
	Llong begin_;
	Llong end_;
};
int main()
{
	{
		ThreadPool pool; 
		pool.setMode(PoolMode::CHACHED_MODE);
		pool.start(4);
		Result res1 = pool.subMitTask(std::make_shared<Mytask>(1, 1000000));
		Result res2 = pool.subMitTask(std::make_shared<Mytask>(1, 1000000));
		Result res3 = pool.subMitTask(std::make_shared<Mytask>(1, 1000000));
		Result res4 = pool.subMitTask(std::make_shared<Mytask>(1, 1000000));
		Result res5 = pool.subMitTask(std::make_shared<Mytask>(1, 1000000));
		//Llong sum1 = res1.get().cast_<Llong>();
		//std::cout << sum1 << std::endl;	
		//std::cout << "main thread sleep for 10s ........" << std::endl;
		//std::this_thread::sleep_for(std::chrono::seconds(5));
	}
	std::cout << "main over" << std::endl;
	getchar();
	return 0;
}
#if 0
	//问题ThreadPool析构后，怎么把线程池里的线程全部回收
	{
		ThreadPool pool;
		//用户自己设置线程模式
		pool.setMode(PoolMode::CHACHED_MODE);
		//开启线程池
		pool.start(4);
		//问题二 如何设计这里的Result机制
		Result res1 = pool.subMitTask(std::make_shared<Mytask>(0, 1000000000));
		Result res2 = pool.subMitTask(std::make_shared<Mytask>(1000000001, 2000000000));
		Result res3 = pool.subMitTask(std::make_shared<Mytask>(2000000001, 3000000000));
		/*pool.subMitTask(std::make_shared<Mytask>(2000000001, 3000000000));

		pool.subMitTask(std::make_shared<Mytask>(2000000001, 3000000000));
		pool.subMitTask(std::make_shared<Mytask>(2000000001, 3000000000));*/
		//任务还没执行完的时候，阻塞在这里等待线程处理完后接收返回值。或者任务执行完了需要等待我调用get方法。
		//随着task被执行完，task对象没了，依赖于task对象的Result对象也没了。

		//Master - slave线程模型
		//Master线程用来分解任务，然后给各个slave线程分配任务
		//等待各个slave线程执行完任务，返回结果
		//master线程合并各个任务结果输出
		Llong sum1 = res1.get().cast_<Llong>(); //get返回一个Any类型，如何转成具体的类型。
		Llong sum2 = res2.get().cast_<Llong>();
		Llong sum3 = res3.get().cast_<Llong>();
		std::cout << "thread sum: " << sum1 + sum2 + sum3 << std::endl;
	}
	
	/*Llong sum = 0;
	for (Llong i = 0; i < 3000000000; i++)
	{
		sum += i;
	}
	std::cout << "control sum: " << sum << std::endl;*/
	//pool.subMitTask(task);
	//pool.subMitTask(task);
	//pool.subMitTask(task);
	/*pool.subMitTask(std::make_shared<Mytask>());
	pool.subMitTask(std::make_shared<Mytask>());
	pool.subMitTask(std::make_shared<Mytask>());
	pool.subMitTask(std::make_shared<Mytask>());
	pool.subMitTask(std::make_shared<Mytask>());*/
	//std::this_thread::sleep_for(std::chrono::seconds());
	getchar();
	return 0;
#endif