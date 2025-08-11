// examine congzhi::Mutex

#include <iostream>
#include <thread>
#include "../pthread_wrapper.hpp"

void LockForSeconds(congzhi::Mutex* mutex, int seconds) {
    mutex->Lock();
    std::cout << "Thread " << std::this_thread::get_id() << " acquired lock for " << seconds << " seconds." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    mutex->Unlock();
    std::cout << "Lock released after " << seconds << " seconds by thread" << std::this_thread::get_id() << std::endl;
}

void TryLocking(congzhi::Mutex* mutex) {
    while (!mutex->TryLock()) {
        std::cout << "Thread " << std::this_thread::get_id() << " failed to acquire lock, trying again..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "Thread " << std::this_thread::get_id() << " acquired the lock!" << std::endl;
    mutex->Unlock();
}

int main() {
    congzhi::Mutex mutex;
    std::cout << "Starting threads to demonstrate mutex locking..." << std::endl;
    std::thread t1(LockForSeconds, &mutex, 3);
    std::thread t2(TryLocking, &mutex);

    t1.join();
    t2.join();
    std::cout << "Both threads completed." << std::endl;

    return 0;
}