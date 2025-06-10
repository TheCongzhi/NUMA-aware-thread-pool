#include "pthread_wrapper.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <exception>

using namespace congzhi;

// ---------------------------
// 1. Thread basic testing
// ---------------------------
void test_thread_basic() {
    std::cout << "Testing Thread Basic Functionality..." << std::endl;
    Thread t;
    std::cout << "Thread State: " << t.GetThreadState() << std::endl;
    // Lambda
    t.CreateThread([]() {
        std::cout << "Hello from lambda!" << std::endl;
    });
    std::cout << "Thread State: " << t.GetThreadState() << std::endl;
    // Wait for the thread to finish
    t.Join();
    std::cout << "Thread State: " << t.GetThreadState() << std::endl;
    std::cout << "Done testing thread!" << std::endl;
}

// ---------------------------
// 2. Mutex basic testing
// ---------------------------
void test_mutex_basic() {
    std::cout << "\n\nTesting Mutex Basic Functionality..." << std::endl;
    
    // mutex test
    Mutex m;
    {
        LockGuard<Mutex> lock(m);
        std::cout << "Mutex has been locked!" << std::endl;
    } // Unlock automatically when going out of scope.
    std::cout << "The mutex has been unlocked!" << std::endl;

    // try_lock test
    if (m.TryLock()) {
        std::cout << "Successfully acquired the mutex using try lock!" << std::endl;
        m.Unlock();
    } else {
        std::cout << "Try lock failed..." << std::endl;
    }
    std::cout << "Done testing mutex!" << std::endl;
}

// ---------------------------------------
// 3. Condition Variable basic testing
// ---------------------------------------
void test_condition_variable_basic() {
    std::cout << "\n\nTesting condition variable..." << std::endl;
    
    Mutex m;
    ConditionVariable cv;
    bool ready = false;

    Thread waiter([&]() {
        LockGuard<Mutex> lock(m);
        while (!ready) {
            cv.Wait(m);
        }
        std::cout << "Waiting for main thread notifying..." << std::endl;
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    {
        LockGuard<Mutex> lock(m);
        ready = true;
        std::cout << "Notifying the condition variable..." << std::endl;
        cv.NotifyOne();
    }

    waiter.Join();
    std::cout << "Done testing condition variable!" << std::endl;
}

// ---------------------------
// Run all tests
// ---------------------------
int main() {
    try {
        test_thread_basic();
        test_mutex_basic();
        test_condition_variable_basic();
        std::cout << "\n\nAll tests passed successfully!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Failed to run tests: " << e.what() << std::endl;
        return 1;
    }
}