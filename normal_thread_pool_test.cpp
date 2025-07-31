#include "thread_pool.hpp"
#include <iostream>
#include <chrono>
#include <mutex>

// 全局互斥锁，确保cout输出线程安全
std::mutex cout_mutex;

// 测试任务：打印任务ID和当前线程ID
void print_task(int task_id) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << "Task " << task_id << " running on thread " 
              << pthread_self() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 模拟任务耗时
}

// 测试函数：通过基类指针操作线程池
void test_thread_pool(congzhi::ThreadPool* pool) {
    std::cout << "\n=== 启动线程池 ===" << std::endl;
    pool->Start();
    std::cout << "初始有效线程数: " << pool->WorkerCount() << std::endl;

    std::cout << "\n=== 提交20个任务 ===" << std::endl;
    for (int i = 0; i < 20; ++i) {
        pool->Enqueue([i]() { print_task(i); });
    }

    // 等待任务队列清空
    while (pool->TaskCount() > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "\n=== 所有任务执行完毕 ===" << std::endl;
    std::cout << "当前有效线程数: " << pool->WorkerCount() << std::endl;

    // 等待15秒，观察自动缩容（空闲阈值为10秒）
    std::cout << "\n=== 等待15秒，测试自动缩容 ===" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(15));
    std::cout << "缩容后有效线程数: " << pool->WorkerCount() << std::endl;

    std::cout << "\n=== 停止线程池 ===" << std::endl;
    pool->Stop();
    std::cout << "线程池已停止" << std::endl;
}

int main() {
    // 通过基类指针创建NormalThreadPool实例
    congzhi::ThreadPool* pool = new congzhi::NormalThreadPool();

    try {
        test_thread_pool(pool); // 传入基类指针，测试线程池功能
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cerr << "测试出错: " << e.what() << std::endl;
    }

    delete pool; // 自动调用派生类析构函数，释放资源
    return 0;
}