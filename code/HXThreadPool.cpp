#include "HXThreadPool.h"

void HX::tools::HXprint::printInfo(const char* str, ...)
{
	va_list args;
	va_start(args, str);

	// 构建输出字符串
	char buffer[CACHE_STR_ARR_SIZE_MAX];
	vsnprintf(buffer, sizeof(buffer), str, args);

	printf("[INFO]: %s\n", buffer);
	va_end(args);
}

HX::tools::HXprint::HXprint()
{
	this->ptr_pInfo = printInfo;
	this->ptr_pError = printError;
}

void HX::tools::HXprint::printError(const char* str, ...)
{
	va_list args;
	va_start(args, str);

	// 构建输出字符串
	char buffer[CACHE_STR_ARR_SIZE_MAX];
	vsnprintf(buffer, sizeof(buffer), str, args);

	printf("[ERROR]: %s\n", buffer);
	va_end(args);
}

HX::tools::HXprint* HX::tools::HXprint::getHXprint()
{
	static HXprint hxPrint;
	return &hxPrint;
}

void HX::tools::HXprint::setPrintInfoFun(void(*fun)(const char* str, ...))
{
	this->ptr_pInfo = fun;
}

void HX::tools::HXprint::setPrintErrorFun(void(*fun)(const char* str, ...))
{
	this->ptr_pError = fun;
}


/////////////////////////////////////////////////////////////////////////////
// 正文																		/
/////////////////////////////////////////////////////////////////////////////

HX::ThreadPool::ThreadPool(int t_min, int t_max, int taskMaxSize, int opTime) : TP_mutex_all(), taskQueueFull(), taskQueueEmpty(), TP_threadTaskQueue(), TP_consumer(), free_consumer(), freeConsumer_cond()
{
	auto print = HX::tools::HXprint::getHXprint();

	do 
	{
		this->TP_free = false;
		this->setSinglNumer();
		this->setSinglFunPtr();

		this->t_min = t_min;
		this->t_max = (t_max == -1) ? thread::hardware_concurrency() - 1: t_max;

		this->taskMaxSize = (taskMaxSize == -1) ? thread::hardware_concurrency() - 1 : taskMaxSize;
		this->opTime = opTime;

		this->TP_busy = 0;
		this->del_t_num = 0;
		this->TP_idle = t_min;
		this->TP_live = t_min;

		print->ptr_pInfo("初始化成员变量成功!");

		// 启动子线程
		for (int i = 0; i < this->TP_live; ++i)
		{
			auto p = new thread(consumerThreadFun, std::ref(*this));
			this->TP_consumer.insert(make_pair(p->get_id(), p));
		}

		print->ptr_pInfo("已经启动 %d 个消费者线程!", t_min);

		// 启动管理者线程
		this->TP_op = new thread(OpThreadFun, std::ref(*this));

		print->ptr_pInfo("已经启动管理者线程");

		return;
	} while (0);
	
	print->ptr_pError("初始化线程池出错!");
}

void HX::ThreadPool::consumerThreadFun(ThreadPool& pool)
{
	auto print = HX::tools::HXprint::getHXprint();
	print->ptr_pInfo("子线程: %ld, 启动!", this_thread::get_id());

	unique_lock<mutex> lock(pool.TP_mutex_all);
	lock.unlock();

	while (!pool.TP_free)
	{
		lock.lock();
		// 判断是否有任务
		while (pool.TP_threadTaskQueue.empty() && !pool.TP_free && !pool.del_t_num.load())
		{
			// 没有任务则挂起
			print->ptr_pInfo("uid = %ld, 没有任务睡大觉!", this_thread::get_id());
			pool.taskQueueEmpty.wait(lock);
		}

		// 销毁线程 (自杀)
		if (pool.del_t_num > 0 || pool.TP_free)
		{
			--pool.del_t_num;
			auto p = pool.TP_consumer.find(this_thread::get_id());
			pool.free_consumer.push(p->second);
			pool.TP_consumer.erase(p);
			lock.unlock();
			--pool.TP_idle;
			--pool.TP_live;
			print->ptr_pInfo("线程: %ld 已退出!", this_thread::get_id());
			pool.freeConsumer_cond.notify_one();
			return;
		}

		// 接任务
		auto task = pool.TP_threadTaskQueue.front();
		pool.TP_threadTaskQueue.pop();
		lock.unlock();

		++pool.TP_busy;
		--pool.TP_idle;
		print->ptr_pInfo("线程: %ld, 开工!", this_thread::get_id());

		// 做任务
		task();

		// 做完了
		--pool.TP_busy;
		++pool.TP_idle;
		lock.lock();
		pool.taskQueueFull.notify_one(); // 通知任务添加函数, 如果在排队可以放一个人过来了
		lock.unlock();
	}

	print->ptr_pError("线程创建时, 线程池已在销毁!");
	
	lock.lock();
	auto p = pool.TP_consumer.find(this_thread::get_id());
	pool.free_consumer.push(p->second);
	pool.TP_consumer.erase(p);
	lock.unlock();
	pool.freeConsumer_cond.notify_one();
	
	--pool.del_t_num;
	--pool.TP_idle;
	--pool.TP_live;
}

void HX::ThreadPool::OpThreadFun(ThreadPool& pool)
{
	auto print = HX::tools::HXprint::getHXprint();
	print->ptr_pInfo("管理者线程: %ld, 启动!", this_thread::get_id());

	while (!pool.TP_free || pool.TP_live > 0)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(pool.opTime));   // 每间隔{opTime}毫秒检查一下线程
		//print->ptr_pInfo("检查消费者线程中...");

		// 安全的获取数据
		unique_lock<mutex> lock(pool.TP_mutex_all);
		int queueLen = pool.TP_threadTaskQueue.size();
		lock.unlock();
		int now_busy = pool.TP_busy;
		int now_idle = pool.TP_idle;
		int now_live = pool.TP_live;

		print->ptr_pInfo("当前任务队列剩有 %d 个任务, 繁忙线程: %d, 存活线程: %d", queueLen, now_busy, now_live);

		// 添加线程
		if (!pool.TP_free && pool.ifAddFunPtr(queueLen, now_busy, now_idle, now_live, pool.t_min, pool.t_max, pool.taskMaxSize))
		{
			// 增加
			for (int i = 0; i < pool.singleAdd; ++i)
			{
				lock.lock();
				auto p = new thread(consumerThreadFun, std::ref(pool));
				pool.TP_consumer.insert(make_pair(p->get_id(), p));
				lock.unlock();

				++pool.TP_idle;
				++pool.TP_live;
			}
			print->ptr_pInfo("线程过少, 添加线程!");
			continue;
		}

		// 销毁线程
		if (pool.TP_free || pool.ifSubFunPtr(queueLen, now_busy, now_idle, now_live, pool.t_min, pool.t_max, pool.taskMaxSize))
		{
			// 减少
			if (pool.TP_free)
				pool.del_t_num = now_live;
			else
				pool.del_t_num = pool.singleSub;

			if (pool.TP_free)
				print->ptr_pInfo("正在关闭线程池...");
			else
				print->ptr_pInfo("线程过多, 减少线程!");

			// 让消费者线程自杀
			int n = pool.del_t_num;
			for (int i = 0; i < n; ++i)
			{
				lock.lock();
				pool.taskQueueEmpty.notify_one();
				pool.freeConsumer_cond.wait(lock);
				auto p = pool.free_consumer.front();
				pool.free_consumer.pop();
				p->join();
				delete p;
				lock.unlock();
			}
			continue;
		}
	}

	while (!pool.free_consumer.empty())
	{
		print->ptr_pInfo("发现内存泄漏!");
		auto p = pool.free_consumer.front();
		pool.free_consumer.pop();
		p->join();
		delete p;
	}
}

void HX::ThreadPool::freeThreadPool()
{
	this->TP_free = true;

	this->TP_op->join();
	delete this->TP_op;
	HX::tools::HXprint::getHXprint()->ptr_pInfo("已释放线程池!");
}

inline void HX::ThreadPool::setSinglNumer(int add, int sub)
{
	this->singleAdd = add;
	this->singleSub = sub;
}

// 任务的个数>存活的线程个数 && 存活的线程数<最大线程数
bool HX::ThreadPool::ifAddThread(int now_taskSize, int now_busy, int now_idle, int now_live, int t_min, int t_max, int taskMaxSize)
{
	return now_taskSize > now_live && now_live < t_max;
}

// 忙的线程*2 < 存活的线程数 && 存活的线程>最小线程数
bool HX::ThreadPool::ifSubThread(int now_taskSize, int now_busy, int now_idle, int now_live, int t_min, int t_max, int taskMaxSize)
{
	return (now_busy << 1) < now_live && now_live > t_min;
}

HX::ThreadPool::~ThreadPool()
{
	if (!this->TP_free)
	{
		this->freeThreadPool();
	}
}

void HX::ThreadPool::setSinglFunPtr(bool(*addPtr)(int, int, int, int, int, int, int), bool(*subPtr)(int, int, int, int, int, int, int))
{
	this->ifAddFunPtr = addPtr;
	this->ifSubFunPtr = subPtr;
}

int HX::ThreadPool::getPoolBusySize()
{
	return this->TP_busy.load();
}

int HX::ThreadPool::getPoolLiveSize()
{
	return this->TP_live.load();
}

int HX::ThreadPool::getTaskSize()
{
	return this->TP_threadTaskQueue.size();
}