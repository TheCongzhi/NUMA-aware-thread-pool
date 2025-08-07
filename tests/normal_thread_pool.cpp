#include "../thread_pool.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <cassert>

// Enqueue tests
void test_task(int id) {
    std::cout << "Task " << id << " is running (thread ID: " 
              << std::this_thread::get_id() << ")\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Task " << id << " completed\n";
}

// Submit tests
int sum_task(int a, int b) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return a + b;
}

int main() {
    try {
        // 1. Initialize the thread pool
        std::cout << "=== NormalThreadPool Test ===\n";
        congzhi::NormalThreadPool pool;
        pool.Info();

        // 2. Start the thread pool
        std::cout << "=== Starting Thread Pool ===\n" << std::endl;
        pool.Start();
        assert(pool.IsRunning() == true);
        std::cout << "Thread pool started. Initial worker count: " << pool.WorkerCount() << "\n\n";

        // 3. Test Enqueue (no return value tasks)
        std::cout << "=== Testing Enqueue ===" << std::endl;
        for (int i = 0; i < 5; ++i) {
            pool.Enqueue([i]() { test_task(i); });
        }
        
        while (pool.TaskCount() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        std::cout << "All Enqueue tasks completed\n\n";

        // 4. Test Submit (tasks with return values)
        std::cout << "=== Testing Submit ===" << std::endl;
        std::vector<std::future<int>> futures;
        for (int i = 0; i < 3; ++i) {
            int a = i * 10;
            int b = i * 20;
            futures.emplace_back(pool.Submit(sum_task, a, b));
            std::cout << "Submitted sum task: " << a << " + " << b << " = ?\n";
        }

        for (size_t i = 0; i < futures.size(); ++i) {
            int result = futures[i].get();
            int expected = (i * 10) + (i * 20);
            assert(result == expected);
            std::cout << "Sum task " << i << " result: " << result << " (expected: " << expected << ")\n";
        }
        std::cout << "All Submit tasks completed\n\n";

        // 5. Test auto-scaling
        std::cout << "=== Testing auto-scaling ===" << std::endl;
        std::cout << "Current worker count: " << pool.WorkerCount() << std::endl;
        for (int i = 0; i < 20; ++i) {
            pool.Enqueue([]() { std::this_thread::sleep_for(std::chrono::milliseconds(200)); });
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "Worker count after expansion: " << pool.WorkerCount() << std::endl;

        while (pool.TaskCount() > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::this_thread::sleep_for(std::chrono::seconds(12)); // 超过idle_threshold_(10秒)
        std::cout << "Worker count after shrinking: " << pool.WorkerCount() << std::endl;

        // 6. Stop the thread pool
        pool.Stop();
        assert(pool.IsRunning() == false);
        std::cout << "\nThread pool stopped. Worker count: " << pool.WorkerCount() << std::endl;

        std::cout << "\nAll tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}