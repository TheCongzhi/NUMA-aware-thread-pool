#include <iostream>
#include <thread>
#include "pthread_wrapper.hpp"

int main() {

    std::thread t1([]() { std::cout << "t1 says hello!\n"; } );
    congzhi::Thread t2([]() { std::cout << "t2 says hello!\n"; } );

    t1.join();
    t2.Join();

    std::thread t3;
    t3 = std::thread([]() { std::cout << "t3 says hello!\n"; }); // Move assignment

    congzhi::Thread t4;
    t4.Start([]() { std::cout << "t4 says hello!\n"; });
    
    t3.join();
    t4.Join();
    return 0;
}