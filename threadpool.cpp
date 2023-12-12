#include "threadpool.h"

const int MAX_TASKQUE_THRESHOLD = INT32_MAX;
const int MAX_THREAD_THRESHOLD = 200;
const int THREAD_IDLE_TIMEOUT = 60; //��λ��
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
	// �ȴ��̳߳������е��̷߳���   ������߳�������״̬ ��1.û����������notEmpty.wait() 2.����ִ��������

	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool { return threadsMap_.size() == 0; });
}

void ThreadPool::start(size_t initTreadSize) //�����̳߳�
{
	//�����̳߳ص�����״̬
	isPoolRunning_ = true;
	//��¼��ʼ�̸߳���
	initTreadSize_ = initTreadSize;
	curThreadSize_ = initTreadSize;
	//�����̶߳���
	for (int i = 0; i < initTreadSize; ++i)
	{
		// ����thread�����ʱ�򣬰��̺߳�������thread  auto:unique_ptr<Thread>
		auto threadPtr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = threadPtr->getThreadId();
		//threadsMap_.insert({ threadId,std::move(threadPtr) });
		threadsMap_.emplace(threadId, std::move(threadPtr));
	}

	//���������߳�
	for (int i = 0; i < initTreadSize; ++i)
	{
		//�̵߳�������Ҫȥִ��һ���̺߳���   �̺߳���Ӧ����ȥ����������Ƿ�������Ȼ����һ������ִ��
		//�̺߳�����Ҫ���ʵ�������У�����������threadpool���У����н��̺߳���д��Thread���оͲ���ȥ������Щ����
		//����һ����ȥ����һ��ȫ�ֺ��������ǻ����޷�����threadpool���е�˽�ж���
		//����������Ҫ��thread�е��̺߳���ȥִ��threadpool����õ��̺߳���
		threadsMap_[i]->start();
		idleThreadSize_++; //��¼��ʼ�����̵߳�����
	}
}

//���pool������״̬
bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

void ThreadPool::setMode(PoolMode poolMode)
{
	if (checkRunningState())  //����̳߳��Ѿ������ˣ���ô������������
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

void ThreadPool::setMaxTaskQueThreashold(size_t max_size) //����������е�������ֵ
{
	if (checkRunningState())//����̳߳��Ѿ������ˣ���ô������������
		return;
	maxTaskQueThreshold_ = max_size;
}

//���̳߳��ύ����    ������
Result ThreadPool::subMitTask(std::shared_ptr<Task> taskPtr)//�û��ύtask
{
	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	//cachedģʽ������ȽϽ����������ڳ�����С��������� ��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��߳�
	//�����������ͷ���������wait״̬���ȴ�notify---�������״̬ ---������----����״̬
	// �û��ύ��������ܳ���һ�룬�����ж������ύʧ�ܣ�����, �������ֵΪfalse��˵������һ�����������ύʧ��
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool { return threadsMap_.size() < maxTaskQueThreshold_; }))
	{
		std::cerr << "task queue is full, submit task fail." << std::endl;
		// return taskPtr->getResult(); // Task  Result �߳�ִ����task��task����ͱ�������
		return Result(taskPtr, false); // ResultҪ�ӳ�taskPtr���������ڣ�����͵ò�������ֵ�ˡ�false��ʾ��Ч�ķ���ֵ
	}
	//notFull_.wait(lock, [&]()->bool { return curTaskSize_ < maxTaskQueThreshold_; });

	//���в�������ʼ����
	taskQue_.emplace(taskPtr);
	curTaskSize_++;
	notEmpty_.notify_all(); //֪ͨ�������߳�

	if (poolMode_ == PoolMode::CHACHED_MODE
		&& idleThreadSize_ < curTaskSize_   //������������ڿ��е��߳�����
		&& threadsMap_.size() < maxThreadThreshold_) // ��ǰ�����̵߳�������С���߳�������ֵ
	{
		std::cout << ">>> create new thread..." << std::endl;
		//�������̶߳���
		auto threadPtr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadid = threadPtr->getThreadId();
		threadsMap_.emplace(threadid, std::move(threadPtr));
		threadsMap_[threadid]->start(); //�����߳�
		idleThreadSize_++;
		curThreadSize_++;
	}

	//���������Result����
	//return taskPtr->getResult();
	return Result(taskPtr);
}

//�����̺߳���   �̳߳ص������̴߳���������������߳�
void ThreadPool::threadFunc(int threadid)  //�̺߳���ִ�з��أ���Ӧ���߳�Ҳ�ͽ���
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	for(;;)
	{
		//�ڻ�ȡ������֮�󣬾�Ӧ���ͷ�������е�����Ȼ����ȥִ������
		std::shared_ptr<Task> task;
		{
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid: " << std::this_thread::get_id() << "���Ի�ȡ����..." << std::endl;

			// cachedģʽ�£��п����Ѿ������˺ܶ��̣߳����ǿ���ʱ�䳬��60s��Ӧ�ðѶ�����߳̽������ա�
			//���ճ���initThreadSize�������߳�
			//��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s ʱ�����߳�
		
				// ÿһ���ӷ���һ�Σ� ��Ϊ��Ҫ�ж��̵߳Ŀ���ʱ�䣬����60s����Ҫ����
				// ���� ��������ǳ�ʱ���أ������������ִ�еķ���
				while (taskQue_.size() == 0)
				{
					//�̳߳�Ҫ�����������߳���Դ
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
							== notEmpty_.wait_for(lock, std::chrono::seconds(1)))//�����ַ���ֵ: no_timeout��timeout
						{
							//����������ʱ���أ�����
							auto now_time = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now_time - lastTime);
							if (dur.count() >= THREAD_IDLE_TIMEOUT
								&& threadsMap_.size() > initTreadSize_) //����60s���̳߳���������Ǵ���initThreadSize_��
							{
								//���յ�ǰ�߳�
								//��¼�߳���������ر�����ֵ���޸�   
								//���̶߳����threadsVec������ɾ�������������������Ҫ�洢�߳�id��threadid => thread���� => ɾ��
								//std::this_thread::getid() ��c++thread��������id���������Լ�����Thread��id��generatedId
								threadsMap_.erase(threadid);
								curThreadSize_--;
								idleThreadSize_--;
								std::cout << "threadid" << std::this_thread::get_id() << "exit!" << std::endl;
								return;
							}
						}
					}
					//������в��գ�ȡһ��������ִ��
					else
					{
						notEmpty_.wait(lock);
					}

					//�̳߳�Ҫ�����ˣ���Ҫ�����߳���Դ
					//if (!isPoolRunning_)
					//{
					//	threadsMap_.erase(threadid);
					//	//curThreadSize_--;
					//	//idleThreadSize_--;  �̳߳ؿ�����ˣ��Ͳ���Ҫ�ټ���ά��������������
					//	std::cout << "threadid: " << std::this_thread::get_id() << "exit" << std::endl;
					//	exitCond_.notify_all();
					//	return;
					//}
				}

			idleThreadSize_--; //ȡ�������ˣ������߳�����--
			std::cout << "tid: " << std::this_thread::get_id() << "��ȡ����ɹ�" << std::endl;
			//ȡ��һ������ִ��
			task = taskQue_.front();
			taskQue_.pop();
			curTaskSize_--;

			//�����Ȼ��ʣ�����񣬼���֪ͨ�����߳���ȡ����
			if (curTaskSize_ > 0)
			{
				notEmpty_.notify_all();
			}
			//ȡ��һ������Ҫ����֪ͨ
			notFull_.notify_all();
		}
		//ִ������  ����ָ��
		if (task != nullptr)
		{
			//task->run(); //ִ�����񣬰�����ķ���ֵͨ��setVal�������� Result�е�any_
			task->exec();
			//task->getResult()->setVal(task->run());
		}

		idleThreadSize_++;//�̴߳�����������
		lastTime = std::chrono::high_resolution_clock().now(); //�����߳�ִ���������ʱ��
	}
	// �������߳���Ҫʵ�ֵ��̺߳�������Ϊ������У�������е��̰߳�ȫ������threadpool���˽�г�Ա�����У�����
	// �մ������߳���Ҫִ�е��̺߳���Ӧ�ö�����threadpool���У����������Щ������
	/*std::cout << "begin threadFunc pid is: "  << std::this_thread::get_id() << std::endl;
	std::cout << "end thread pid is: " << std::this_thread::get_id() << std::endl;*/

	//����ִ�е��̣߳������̳߳�Ҫ������������whileѭ��
	threadsMap_.erase(threadid);
	std::cout << "threadid: " << std::this_thread::get_id() << "exit!" << std::endl;
	exitCond_.notify_all();
}

///////  �̷߳���ʵ��
int Thread::generatedId_ = 0; //(��̬�ĳ�Ա������Ҫ��������г�ʼ��)

Thread::Thread(threadFunc func)//�̹߳��캯��
	: threadFunc_(func)
	, threadId_(generatedId_++)
{}

Thread::~Thread() {}

void Thread::start()
{
	//����һ���߳���ִ���̺߳���
	//��c++11��˵�� ���̶߳�����̺߳��������˸��������̶߳���������������̺߳�����Ҫ�ڣ����԰��̶߳�������Ϊ�����̡߳�
	std::thread t(threadFunc_, threadId_); //�����������߳�
	t.detach(); //�ػ��߳�
}


//��ȡ�߳�id
int Thread::getThreadId() const
{
	return threadId_;
}

/////// Result������ʵ��
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:task_(task),
	isValid_(isValid)
{
	task_->setResult(this);
}


//������� get������ �û��������������ȡtask�ķ���ֵ��
Any Result::get() // �û����õ�
{
	if (!isValid_)
	{
		return ""; //����ֵ����Ч��
	}
	//return any_; //�����⣬ƥ�����ֵ�Ŀ�������
	sem_.wait(); //�ȴ��̺߳���ִ�����ȡ����ֵ���û����õ�ʱ������û��ִ���꣬���ｫ�����û��̡߳�
	return std::move(any_);
}
// ����һ��setVal������ ��ȡ����ִ����ķ���ֵ��������ִ����ķ���ֵ��Task::run���У���ν����ķ���ֵ��any_��
void Result::setVal(Any any) // threadFunc���õ�
{
	// �洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post(); //�Ѿ���ȡ����ķ���ֵ�ˣ������ź�����Դ����������ظ��û���
}


////////Task������ʵ��
void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run()); //���﷢����̬
	}
}

void Task::setResult(Result* res)
{
	this->result_ = res;
}

Task::Task()
	:result_(nullptr)
{}