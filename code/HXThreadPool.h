#pragma once
#ifndef _HX_THREAD_POOL_H_
#define _HX_THREAD_POOL_H_
#include <thread>				// C++线程
#include <functional>			// 可调用对象封装
#include <atomic>				// 原子变量
#include <mutex>				// 互斥锁
#include <condition_variable>	// 条件变量
#include <cstdarg>				// C 的可变参数
#include <chrono>				// 时间日期库
#include <map>
#include <queue>

#define SINGLE_NUME 2				// 单次增加的线程数量
#define CACHE_STR_ARR_SIZE_MAX 1024 // 缓冲字符串最大长度

using namespace std;

namespace HX {
	namespace tools {
		// 单例-懒汉-输出类
		class HXprint {
			HXprint();
		private:
			HXprint(const HXprint&) = delete;
			HXprint& operator =(const HXprint&) = delete;

			// 自带输出正常
			static void printInfo(const char* str, ...);

			// 自带输出异常
			static void printError(const char* str, ...);
		public:
			static HXprint* getHXprint();
			
			// 设置info输出函数
			void setPrintInfoFun(void (*fun)(const char* str, ...));

			// 设置error输出函数
			void setPrintErrorFun(void (*fun)(const char* str, ...));
			
			void (*ptr_pInfo)(const char* str, ...);
			void (*ptr_pError)(const char* str, ...);
		};
	}

	// 线程池
	class ThreadPool {
	public:
		// 为什么需要核心数 - 1?, 因为 还有主线程 (-1); 还有一个管理者线程 (-1), 但是因为有时间间隔, 所以可以忽略...
		/**
		 * @brief 创建线程池并初始化, [-1] 代表使用 (当前CPU的核心数 - 1)
		 * @param t_min 最小线程数
		 * @param t_max 最大线程数
		 * @param taskMaxSize 最大任务数
		 * @param opTime 管理者线程 检查 消费者线程 的时间间隔, 单位 ms
		 */
		ThreadPool(int t_min, int t_max = -1, int taskMaxSize = -1, int opTime = 2000);

		/**
		 * @brief 给线程池添加任务
		 * @param func 子线程需要执行的任务可调用对象
		 * @param ... 可调用对象分别对应的参数, 若没有可以为空 (只写参数func)
		 * @return 添加成功 1, 出错 0
		 */
		template<typename Function, typename... Args>
		bool addTask(Function&& func, Args&&... args);

		template<typename Function>
		bool addTask(Function&& func);

		/**
		 * @brief 设置单次增减线程数量
		 * @param add 单次增加的线程数量
		 * @param sub 单次减小的线程数量
		 * @return 无
		 */
		inline void setSinglNumer(int add = SINGLE_NUME, int sub = SINGLE_NUME);

		/**
		 * @brief 设置管理者线程判断函数
		 * @param addPtr 单次增加的线程的判断函数
		 * @param subPtr 单次减小的线程的判断函数
		 * @return 无
		 */
		void setSinglFunPtr(bool (*addPtr)(int, int, int, int, int, int, int) = ifAddThread ,
							bool (*subPtr)(int, int, int, int, int, int, int) = ifSubThread );

		/**
		 * @brief 获取当前线程池忙碌的线程数
		 * @param 无
		 * @return 忙碌的线程数
		 */
		int getPoolBusySize();

		/**
		 * @brief 获取当前线程池存活的线程数
		 * @param 无
		 * @return 存活的线程数
		 */
		int getPoolLiveSize();

		/**
		 * @brief 获取当前线程池的任务队列的长度
		 * @param 无
		 * @return 任务队列的长度
		 */
		int getTaskSize();

		// 销毁线程池
		void freeThreadPool();
		~ThreadPool();
	private:
		// 工作的线程(消费者线程)任务函数
		static void consumerThreadFun(ThreadPool& pool);

		// 管理者线程 任务函数
		static void OpThreadFun(ThreadPool& pool);

		// 任务的个数>存活的线程个数 && 存活的线程数<最大线程数
		/**
		 * @brief 自带的增加线程数量 判断函数
		 * @param now_taskSize 当前任务数
		 * @param now_busy 当前繁忙的线程
		 * @param now_idle 当前闲置的线程
		 * @param now_live 当前存活的线程
		 * @param t_min    线程池的最小线程数
		 * @param t_max	   线程池的最大线程数
		 * @param taskMaxSize 任务队列最大长度
		 * @return 0 不进行增加 / 1 进行增加
		 */
		static bool ifAddThread(int now_taskSize, int now_busy, int now_idle, int now_live, int t_min, int t_max, int taskMaxSize);

		// 忙的线程*2 < 存活的线程数 && 存活的线程>最小线程数
		/**
		 * @brief 自带的减少线程数量 判断函数
		 * @param now_taskSize 当前任务数
		 * @param now_busy 当前繁忙的线程
		 * @param now_idle 当前闲置的线程
		 * @param now_live 当前存活的线程
		 * @param t_min    线程池的最小线程数
		 * @param t_max	   线程池的最大线程数
		 * @param taskMaxSize 任务队列最大长度
		 * @return 0 不进行减少 / 1 进行减少
		 */
		static bool ifSubThread(int now_taskSize, int now_busy, int now_idle, int now_live, int t_min, int t_max, int taskMaxSize);

		// --- 任务队列 组 ---
		queue<std::function<void()>> TP_threadTaskQueue; // 任务队列
		int taskMaxSize;			// 任务数量
		int opTime;					// 间隔检查时间
		thread* TP_op;				// 管理者线程

		mutex TP_mutex_all;						// 整个线程池的互斥锁

		condition_variable taskQueueFull;		// 条件变量: 任务队列满了
		condition_variable taskQueueEmpty;		// 条件变量: 任务队列空了
		condition_variable freeConsumer_cond;	// 条件变量: 需要释放的线程

		bool (*ifAddFunPtr)(int, int, int, int, int, int, int); // 函数指针指向是否增加线程的判断函数
		bool (*ifSubFunPtr)(int, int, int, int, int, int, int); // 函数指针指向是否减少线程的判断函数

		// --- 线程池 信息 ---
		int t_min;					// 最小线程数
		int t_max;					// 最大线程数
		int singleAdd;				// 单次添加的线程数
		int singleSub;				// 单次销毁的线程数

		atomic_int TP_busy;         // 繁忙线程数 (正在执行任务)
		atomic_int TP_idle;         // 空闲线程数 (已挂起)
		atomic_int TP_live;         // 存活线程数 == 空闲线程数 + 繁忙线程数
		atomic_int del_t_num;		// 目前需要删除的线程数

		// --- 线程池 社畜 ---
		map<thread::id, thread *> TP_consumer;	// 消费者红黑树 (添加/查找/删除 O(logN) 时间复杂度)
		queue<thread*> free_consumer;			// 需要释放的线程

		// --- 线程池 开关 ---
		atomic_bool TP_free;					// 线程池是否释放
	};
}

template<typename Function, typename... Args>
bool HX::ThreadPool::addTask(Function&& func, Args&&... args)
{
	auto print = HX::tools::HXprint::getHXprint();

	unique_lock<mutex> lock(this->TP_mutex_all);
	do
	{
		if (this->TP_free)
			break;

		while (this->TP_threadTaskQueue.size() == this->taskMaxSize)
		{
			// 任务队列已满, 等待添加
			this->taskQueueFull.wait(lock);
		}

		if (this->TP_threadTaskQueue.size() < this->taskMaxSize)
		{
			this->TP_threadTaskQueue.push(std::bind(func, std::forward<Args>(args)...));
			this->taskQueueEmpty.notify_one();
		}

		lock.unlock();

		print->ptr_pInfo("添加任务成功!");
		return 1;
	} while (0);

	lock.unlock();
	print->ptr_pError("添加任务出错: 线程池在添加时已经在销毁!");
	return 0;
}

template<typename Function>
bool HX::ThreadPool::addTask(Function&& func)
{
	auto print = HX::tools::HXprint::getHXprint();

	unique_lock<mutex> lock(this->TP_mutex_all);
	do
	{
		if (this->TP_free)
			break;

		while (this->TP_threadTaskQueue.size() == this->taskMaxSize)
		{
			// 任务队列已满, 等待添加
			this->taskQueueFull.wait(lock);
		}

		if (this->TP_threadTaskQueue.size() < this->taskMaxSize)
		{
			this->TP_threadTaskQueue.push(func);
			this->taskQueueEmpty.notify_one();
		}
		lock.unlock();

		print->ptr_pInfo("添加任务成功!");
		return 1;
	} while (0);

	lock.unlock();
	print->ptr_pError("添加任务出错: 线程池在添加时已经在销毁!");
	return 0;
}

#endif // _HX_THREAD_POOL_H_