#include <iostream>
#include "../pthread_wrapper.hpp"
int main() {
    congzhi::Thread::Yield();
    std::cout << "Thread yield executed successfully." << std::endl;
    return 0;
}