#include <iostream>
#include "../pthread_wrapper.hpp"
#include "../numa_namespace.hpp"


int main () {
    if (!congzhi::numa::IsNumaSupported()) {
        std::cout << "NUMA is not supported on this machine." << std::endl;
    }
    std::cout << "The number of NUMA nodes available on the system: " << congzhi::numa::NumaNodeCount() << std::endl
              << "And the number of total CPU count available on the system: " << congzhi::numa::GetTotalCpuCount() << std::endl;
    
    auto node_mask = congzhi::numa::GetNumaNodeMask();
    congzhi::numa::PrintNumaNodeMask(node_mask);
    
    std::cout << "\nI can check the info of NUMA node for you.\n"
                  "Which node would you like to check? (node num starts at 0)\n"
                  "Node: ";
    int num;
    std::cin >> num;
    try {
        // The node available is the online node that total memory is non-zero.
        std::cout << "\nNode " << num << " online?   \t" << (congzhi::numa::IsNumaNodeOnline(num) ? "Online" : "Not Online")
                << "\nNode " << num << " available? \t" << (congzhi::numa::IsNumaNodeAvailable(num) ? "Available" : "Not Available")
                << "\nCPU num on node "   << num << ":\t" << congzhi::numa::GetCpuCountOnNode(num);
        auto mem_info = congzhi::numa::GetNodeMemoryInfo(num);
        std::cout << "\nLocal memory info of node " << num << ":" 
                << "\n\tTotal memory:\t" << mem_info.total_ / (1024 * 1024) << "MB"
                << "\n\tUsed  memory: \t" << mem_info.used_ / (1024 * 1024) << "MB"
                << "\n\tFree  memory: \t" << mem_info.free_ / (1024 * 1024) << "MB"
                << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

}