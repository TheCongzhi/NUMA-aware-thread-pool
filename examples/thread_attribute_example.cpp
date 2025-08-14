#include <iostream>
#include "../pthread_wrapper.hpp"

int main() {
    congzhi::ThreadAttribute attr;

    attr.SetStackSize(4 * 1024 * 1024);
    attr.SetScope(congzhi::Scope::System);
    attr.SetDetachState(congzhi::DetachState::Joinable);
    attr.SetSchedulingPolicy(congzhi::SchedulingPolicy::Default, 0);
    std::cout << "Thread attributes set successfully." << std::endl;

    congzhi::Thread t1(
        [&](){
            congzhi::this_thread::SleepFor(std::chrono::seconds(1));
            std::cout << "Thread is running with custom attributes." << std::endl;
            std::cout << "Thread ID: " << pthread_self() << std::endl;
            std::cout << "Thread stack size:" << attr.GetStackSize() << std::endl;
            std::cout << "Thread scope: " << (attr.GetScope() == congzhi::Scope::System ? "System" : "Process") << std::endl;
            std::cout << "Thread detach state: " << (attr.GetDetachState() == congzhi::DetachState::Detached ? "Detached" : "Joinable") << std::endl;
            std::cout << "Thread scheduling policy: " << (attr.GetSchedulingPolicy() == congzhi::SchedulingPolicy::Default ? "Default" : "Custom") << std::endl;
        },
        congzhi::DtorAction::DETACH,
        attr
    );
    congzhi::this_thread::SleepFor(std::chrono::seconds(2));
    try {
        t1.Join(); // This will throw since the thread is detached
    } catch (const std::logic_error& e) {
        std::cerr << "Logic error: " << e.what() << std::endl;
    }
    return 0;
}