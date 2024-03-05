#include"threadpool.h"

int sum1(int a, int b)
{
	std::this_thread::sleep_for(std::chrono::seconds(2));
	return a + b;
}

int sum2(int a, int b, int c)
{
	std::this_thread::sleep_for(std::chrono::seconds(2));
	return a + b + c;
}


int main()
{
	ThreadPool pool;
	pool.start(2);
	//pool.setMode(PoolMode::MODE_CACHED);
	std::future<int> res = pool.submitTask(sum1, 10, 20);
	std::future<int> res1 = pool.submitTask(sum2, 10, 20, 30);
	std::future<int> res2 = pool.submitTask([](int s, int e)->int {
		int sum = 0;
		for (int i = s; i < e; i++)
			sum += i; 
		return sum;
		}, 1, 100);
	std::future<int> res3 = pool.submitTask(sum2, 10, 20, 30);
	std::cout << res.get() << std::endl;
	std::cout << res1.get() << std::endl;
	std::cout << res2.get() << std::endl;
	std::cout << res3.get() << std::endl;
}