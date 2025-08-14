// Different destructor actions of congzhi::Thread class

#include <iostream>
#include "../pthread_wrapper.hpp"

int main()
{
    congzhi::Mutex mutex;

    // Create a thread with default destructor action (JOIN)
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread1([]()
                                { std::cout << "Thread 1 is running." << std::endl; });
        thread1.Join();                                                 // Explicitly join the thread
        std::cout << "The thread's state is: "<< thread1.GetThreadState() // Get the state after joining, should be TERMINATED
                  << "\nFinishes its job? " << (thread1.IsCompleteExecution() ? "Yes" : "No") // Is thread finishes its job after Join()? Should be Yes
                  << std::endl; 
    }

    // Create a thread with JOIN action
    // Main thread will wait for thread2 to finish before exiting
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread2([]()
                                { std::cout << "Thread 2 is running." << std::endl; }, congzhi::DtorAction::JOIN);
        congzhi::this_thread::SleepFor(std::chrono::milliseconds(100));
        std::cout << "The thread's state is: "<< thread2.GetThreadState() // Should be joinable, bacause the dtor han't been called.
                  << "\nFinishes its job? " << (thread2.IsCompleteExecution() ? "Yes" : "No") // Is thread finishes its job after 100ms sleep? Should be Yes
                  << std::endl; 
    }

    // Create a thread with DETACH action
    // Main thread will detach thread3 when this stack frame returns, detached threads would cleaned up automatically when it finishes, main thread won't wait for it
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread3([]()
                                { std::cout << "Thread 3 is running." << std::endl; }, congzhi::DtorAction::DETACH);
        congzhi::this_thread::SleepFor(std::chrono::milliseconds(100));
        std::cout << "The thread's state is: "<< thread3.GetThreadState() // Should be joinable, bacause the dtor han't been called.
                  << "\nFinishes its job? " << (thread3.IsCompleteExecution() ? "Yes" : "No") // Is thread finishes its job after 100ms sleep? Should be Yes
                  << std::endl;     
    }

    // Create a thread with CANCEL action
    // Beware!! Caller should set cancel point or the action would block forever!!
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread4([]()
                                {            
            while (true) {
                std::cout << "Thread 4 is running." << std::endl;
                congzhi::this_thread::SleepFor(std::chrono::milliseconds(10));
            } }, congzhi::DtorAction::CANCEL);
        congzhi::this_thread::SleepFor(std::chrono::milliseconds(100)); // Ensure thread4 has started and running
        thread4.Cancel();
        std::cout << "The thread's state is: "<< thread4.GetThreadState() // Get the state after cancellation, should be TERMINATED
                  << "\nFinishes its job? " << (thread4.IsCompleteExecution() ? "Yes" : "No") // Is thread finishes its job after cancellation? Should be No
                  << std::endl; 
    }

    // Create a thread with TERMINATE action
    // In this way, the thread must handle SIGTERM signal to exit gracefully
    // If there's no signal handler registered, the default action will terminate the entire process
    {
        congzhi::LockGuard<congzhi::Mutex> lock(mutex);
        congzhi::Thread thread5([]() {
            // Register signal handler for SIGTERM
            struct sigaction sa;
            sa.sa_handler = [](int) {
            std::cout << "Thread 5 exiting gracefully." << std::endl;
            pthread_exit(nullptr);
            };
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(SIGTERM, &sa, nullptr);

            // The thread's main loop
            while (true) {
                std::cout << "Thread 5 is running." << std::endl;
                congzhi::this_thread::SleepFor(std::chrono::milliseconds(10));
        } }, congzhi::DtorAction::TERMINATE);

        congzhi::this_thread::SleepFor(std::chrono::milliseconds(100)); // Ensure thread5 has started and running
        try
        {
            thread5.Terminate(); // Terminate the thread
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error terminating thread: " << e.what() << std::endl;
        }
        std::cout << "The thread's state is: "<< thread5.GetThreadState() // Get the state after termination, should be TERMINATED
                  << "\nFinishes its job? " << (thread5.IsCompleteExecution() ? "Yes" : "No") // Is thread finishes its job after cancellation? Should be No
                  << std::endl; 
    }
    std::cout << "All threads have been cleaned up." << std::endl;
    return 0;
}