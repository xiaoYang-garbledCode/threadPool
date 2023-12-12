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
������������� template
����һ������ָ��������������ͣ� ��������    ����ָ��   ����������		
	Any  => Base*     ====>  Derive : public Base
							    data
*/

//Any���Ϳ��Խ���������������
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete; //��Ϊ��Ա����base�ǲ�������ֵ��������ģ���������Ҳ����
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	// ������캯��������any���ͽ�����������������
	template<typename T>
	Any(T data): base_(std::make_unique<Derive<T>>(data))
	{}

	// ��������ܰ�Any������洢��data������ȡ����
	template<typename T>
	T cast_()
	{
		// ��ô��base_�ҵ�����ָ���Derive���󣬴���ȡ��data��Ա����
		//������ָ��תΪ������ָ��  ��������ǿת ֻ��dynamic_cast ֧��RTTI
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());//����ָ���get�����������ָ����洢����ָ��
		if (pd == nullptr)
		{
			//throw "type is incompatible";
			std::cout << "type is incompatible" << std::endl;
		}
		return pd->data_;
	}
private:
	// ��������
	class Base
	{
	public:
		// ����ָ��ָ���������������ڶ��ϴ����ģ���ô���������������޷����ã�   ������ʲôʱ����������������Ҫ����Ϊ�麯��
		virtual ~Base() = default; //ʹ��Ĭ������
	};
	//����������
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) :data_(data)
		{}
		T data_; //������������������
	};
private:
	//����һ�������ָ��
	std::unique_ptr<Base> base_;
};

/*
	�ź���  ===�� mutex + ��������   �ź���������һ���߳�ִ��wait������һ���߳�ִ��post������Դ��ʼֵ��
									 mutex��˵������wait��post��ֻ����ͬһ���߳�����
	�̵߳�ͬ����
		�̻߳��⣺mutex  atomic =====�������������߼����������ڴ潻����Ϊԭ�Ӳ���  
		�߳�ͨ�ţ���������  + �ź���
*/
//ʵ��һ���ź���
class Semaphore
{
public:
	Semaphore(int limit =0) : resLimit_(limit)
	{}
	~Semaphore() = default;
	//��ȡһ���ź�����Դ
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
			//û����Դ����ǰ�߳�״̬��Ϊwait
		cond_.wait(lock, [&]()->bool { return resLimit_ > 0; });
		resLimit_--;
	}
	//����һ���ź�����Դ
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:
	int resLimit_; //�Ѿ����̰߳�ȫ���� 
	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task; //ǰ������
// ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ���
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	// ����һ��setVal������ ��ȡ����ִ����ķ���ֵ��������ִ����ķ���ֵ��Task::run���У���ν����ķ���ֵ�浽any_��
	void setVal(Any any);
	
	//������� get������ �û��������������ȡtask�ķ���ֵ��
	Any get();
private:
	Any any_; //�洢����ķ���ֵ
	Semaphore sem_; // �߳�ͨ���ź���
	std::shared_ptr<Task> task_; // ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_; //����ֵ�Ƿ���Ч
};


//����������
class Task //�û������Զ��������������ͣ���task�̳У�ʵ���Զ���������
{
public:
//�麯�����ܺ�ģ��һ��ʹ�ã� �麯���ڱ����ʱ��Ҫ�����麯��������ģ�����͵ĺ�����û��ʵ������virtual T run(){}
	virtual Any run() = 0;
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
private:
	Result* result_;  //result������������� ���� Task
};

//�̳߳�֧�ֵ�ģʽ
enum class PoolMode
{
	FIXED_MODE,  //�̶��������߳�
	CHACHED_MODE, //�߳������Ķ�̬����
};



//�߳�����
class Thread
{
public:
	//�̺߳�����������
	using threadFunc = std::function<void(int threadid)>;
	Thread(threadFunc func); //�̹߳��캯��
	~Thread();
	void start();
	
	//��ȡ�߳�id
	int getThreadId() const;
private:
	threadFunc threadFunc_; //���յ���threadpool��start���������̵߳�ʱ��󶨵��̺߳���
	static int generatedId_;//���һ���ֲ���Χ�ڵ�id (��̬�ĳ�Ա������Ҫ��������г�ʼ��)
	int threadId_; //�����߳�id
};

/*
example:

class Mytask : public Task
{
	void run() { // �̴߳���...};
};

ThreadPool pool;
pool.start();
//make_shared���ĺô��ǿ��Խ�������ڴ�����ü������ڴ汣����һ��
pool.submitTask(std::make_shared<Mytask>()); 
*/


//�̳߳�����
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	//�����̳߳�  hardware_concurrency���ص��ǵ�ǰϵͳcpu�ĺ�����
	void start(size_t initTreadSize= std::thread::hardware_concurrency()); 

	void setMode(PoolMode poolMode);

	void setMaxTaskQueThreashold(size_t max_size); //����������е�������ֵ

	void setMaxThreadThreashold(size_t max_size); //�����̳߳�cachedģʽ�µ��߳���ֵ


	Result subMitTask(std::shared_ptr<Task> taskPtr);  //�û��ύtask

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
private:
	//�����̺߳���
	void threadFunc(int threadid);
	//���pool������״̬
	bool checkRunningState() const;
private:
	//std::vector<Thread*> threadsVec_; //�߳��б� Thread������new����������vector������ʱ�����ĳ�Ա��ָ�룬������ڴ�й©
	//std::vector <std::unique_ptr<Thread>> threadsVec_;  
	std::unordered_map<int, std::unique_ptr<Thread>> threadsMap_; // �߳��б�
	size_t maxThreadThreshold_; //�߳�vec������������ֵ
	size_t initTreadSize_; //��ʼ�߳�����
	std::atomic_size_t curThreadSize_; //��¼��ǰ�̳߳����̵߳�������  vector.size()Ҳ�Ǽ�¼�߳������ģ������������̰߳�ȫ��
	std::atomic_size_t idleThreadSize_;//��¼�����̵߳�����
	//������ʹ��task�Ļ�����ָ�루����Ҫ���û�Ҫ����һ���������ڳ��Ķ��󣩣���ֹ�û�����task��ʱ����
	//Ϊʲôʹ��shared_ptr ����ʹ��unique_ptr,��Ϊ��auto task = taskQue_.front();��ȡ�����ʱ���������Ҫ������һ��ָ����ָ��task
	
	std::queue<std::shared_ptr<Task>> taskQue_; //������� 
	std::atomic_uint curTaskSize_; //������� ��Ӧ����Ҫ�����̰߳�ȫ��
	size_t maxTaskQueThreshold_; //������е�������������ֵ
	std::mutex taskQueMtx_; //��֤������е��̰߳�ȫ��

	std::condition_variable notEmpty_;  //���գ�˵����������
	std::condition_variable notFull_;	//������˵���������� 
	std::condition_variable exitCond_;  //�ȴ��߳���Դȫ������
	PoolMode poolMode_; //��ǰ�̵߳Ĺ���ģʽ
	std::atomic_bool isPoolRunning_;// ��ʾ��ǰ�̳߳ص�����״̬


};

#endif
