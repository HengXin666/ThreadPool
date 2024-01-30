#include "HXThreadPool.h"

int main()
{
	// 创建线程池
	HX::ThreadPool pool(2);

    // 可以设置线程池的最小线程数和最大线程数 (可选)
    pool.setSinglNumer(2, 2);

    // 添加任务
	pool.addTask([]() { printf("hello world!\n"); });

    // 等待3秒 不等待线程池 或者在添加任务的同时删除线程池 可能会产生内存泄漏 (不过已经修复了)
    std::this_thread::sleep_for(std::chrono::seconds(3));

    return 0;
}