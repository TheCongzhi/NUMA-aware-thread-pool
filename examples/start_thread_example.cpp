// About the Start() method:

#include <iostream>
#include <thread>
#include "../pthread_wrapper.hpp"

int main () {

// Starting threads with immediate execution, both implementations support it
std::thread std_t1([]() {std::cout << "Thread 1 is running." << std::endl;});
congzhi::Thread congzhi_t1([]() {std::cout << "Congzhi Thread 1 is running." << std::endl;});
std_t1.join();
congzhi_t1.Join();

// Move construction, no delayed start, both implementations support it
std::thread std_t2;
std_t2 = std::thread([]() {std::cout << "Thread 2 is running." << std::endl;});
congzhi::Thread congzhi_t2;
congzhi_t2 = congzhi::Thread([]() {std::cout << "Congzhi Thread 2 is running." << std::endl;});
std_t2.join();
congzhi_t2.Join();

// Delayed start, no move construction needed, std::thread does not support it
congzhi::Thread congzhi_t3;
congzhi_t3.Start([]() {std::cout << "Congzhi Thread 3 is running." << std::endl;});
congzhi_t3.Join();
}