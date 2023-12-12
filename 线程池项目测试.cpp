#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include "threadpool.h"

/*
* ��Щ������ϣ���ܹ���ȡ�߳�ִ�����񷵻�ֵ��
* �����������̼߳��㣩
  1 + .... + 30000�ĺ�
  thread1  1 + ... + 10000
  thread2  10001 + ... 20000
  ...

  main thread  ��ÿ���̷߳���������䣬���ȴ����Ƿ��ؽ�������պϲ����
*/
using Llong = long long;

class Mytask : public Task
{
public:
	Mytask(Llong begin, Llong end)
		:begin_(begin),
		 end_(end)
	{}
	//����һ  �������run�����ķ���ֵ�����Ա�ʾ���������
	// java Python ��Object�������������͵Ļ���
	// c++17 any����
	Any run()  //run�������վ����̳߳ط�����߳���ȥִ��
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
	//����ThreadPool��������ô���̳߳�����߳�ȫ������
	{
		ThreadPool pool;
		//�û��Լ������߳�ģʽ
		pool.setMode(PoolMode::CHACHED_MODE);
		//�����̳߳�
		pool.start(4);
		//����� �����������Result����
		Result res1 = pool.subMitTask(std::make_shared<Mytask>(0, 1000000000));
		Result res2 = pool.subMitTask(std::make_shared<Mytask>(1000000001, 2000000000));
		Result res3 = pool.subMitTask(std::make_shared<Mytask>(2000000001, 3000000000));
		/*pool.subMitTask(std::make_shared<Mytask>(2000000001, 3000000000));

		pool.subMitTask(std::make_shared<Mytask>(2000000001, 3000000000));
		pool.subMitTask(std::make_shared<Mytask>(2000000001, 3000000000));*/
		//����ûִ�����ʱ������������ȴ��̴߳��������շ���ֵ����������ִ��������Ҫ�ȴ��ҵ���get������
		//����task��ִ���꣬task����û�ˣ�������task�����Result����Ҳû�ˡ�

		//Master - slave�߳�ģ��
		//Master�߳������ֽ�����Ȼ�������slave�̷߳�������
		//�ȴ�����slave�߳�ִ�������񣬷��ؽ��
		//master�̺߳ϲ��������������
		Llong sum1 = res1.get().cast_<Llong>(); //get����һ��Any���ͣ����ת�ɾ�������͡�
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