#include <iostream>
#include "../pthread_wrapper.hpp"
#include "../numa_namespace.hpp"


void MonitorMemory(const int numa_node) {
    for (;;) {
        auto mem_info = congzhi::numa::GetNodeMemoryInfo(numa_node);
        std::cout << "\nLocal memory info of node " << numa_node << ":" 
                << "\n\tTotal memory:\t" << mem_info.total_ / (1024 * 1024) << "MB"
                << "\n\tUsed  memory: \t" << mem_info.used_ / (1024 * 1024) << "MB"
                << "\n\tFree  memory: \t" << mem_info.free_ / (1024 * 1024) << "MB"
                << std::endl;
        
        congzhi::this_thread::SleepFor(1000);
    }    
}

int main () {
    const int numa_node = 0;
    try {
        auto bound_task = std::bind(MonitorMemory, 0);
        congzhi::Thread t(
            bound_task, 
            congzhi::DtorAction::CANCEL
        );
        
        // Allocate memory
        const size_t size = static_cast<size_t>(512 * 1024 * 1024);
        std::cout << "Allocating " << size / (1024 *1024) << " MB\n";
        void* memory = congzhi::numa::AllocateMemoryOnNode(size, 0);
        std::cout << "Memory allocated at " << memory << "\n";
        congzhi::this_thread::SleepFor(2000);

        // Access memory
        char* char_mem = static_cast<char*>(memory);
        for(size_t i = 0; i < size; i += 4096) {
            char_mem[i] = i;
        }
        std::cout << "Memory initialized\n";
        congzhi::this_thread::SleepFor(2000);

        // Free memory
        congzhi::numa::FreeMemory(memory, size);
        std::cout << "Memory Freed.\n";
        congzhi::this_thread::SleepFor(2000);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}
