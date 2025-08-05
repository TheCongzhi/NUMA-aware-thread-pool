#ifndef NUMA_WRAPPER_HPP
#define NUMA_WRAPPER_HPP
#if defined(__linux__)

#include <pthread.h>
#include <sched.h>  // For sched_getcpu
#include <numa.h>   // NUMA support library
#include <numaif.h> // linux syscall interface
#include <stdexcept>
#include <iostream>


namespace congzhi {
class Numa {
private:
    // Preventing instantiation
    Numa() = default;
    // Preventing deletion
    ~Numa() = default;
    
    // No instance would be created, thus the copying and moveing is meaningless
    Numa(const Numa&) = delete;
    Numa& operator=(const Numa&) = delete;
    Numa(Numa&&) = delete;
    Numa& operator=(Numa&&) = delete;
public:
    // Check NUMA system support
    static bool IsNumaSupported() {
        return numa_available() == 0;
    }
    
    // Initialize NUMA system
    static void InitializeNuma() {
        if (!IsNumaSupported()) {
            throw std::runtime_error("NUMA is not supported on this system");
        }
        if (numa_available() < 0) {
            throw std::runtime_error("Failed to initialize NUMA.");
        }
    }

    // Get the number of NUMA nodes available
    static int NumaNodeCount() {
        return numa_max_node() + 1;
    }

    // Set the CPU affinity, binding current thread to a specific NUMA node.
    static void BindThreadToNumaNode(pthread_t thread,int node) {
        if (node < 0 || node >= NumaNodeCount()) {
            throw std::out_of_range("Invalid NUMA node index");
        }
        struct bitmask* nodemask = numa_allocate_nodemask();
        if (numa_node_to_cpus(node, nodemask) != 0) {
            numa_free_nodemask(nodemask);
            throw std::runtime_error("Failed to get CPUs for NUMA node");
        }
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int i = 0; i < nodemask->size; ++i) {
            if (numa_bitmask_isbitset(nodemask, i)) {
                CPU_SET(i, &cpuset);
            }
        }
        int res = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
        numa_free_nodemask(nodemask);
        
        if (res != 0) {
            throw std::runtime_error("Failed to set thread affinity: " + std::string(strerror(result)));
        }
    }

    // Get the current NUMA node of the calling thread.
    static int GetNodeCurrentThreadIsOn() {
        int cpu = sched_getcpu();
        if (cpu < 0) {
            throw std::runtime_error("Failed to get current CPU: " + std::string(strerror(errno)));
        }
        int node = numa_node_of_cpu(cpu);
        if (node < 0) {
            throw std::runtime_error("Failed to get NUMA node of CPU: " + std::string(strerror(errno)));
        }
        return node;
    }

    // Allocate memory on a specific NUMA node.
    static void* AllocateMemoryOnNode(size_t size, int node) {
        if (node < 0 || node >= NumaNodeCount()) {
            throw std::out_of_range("Invalid NUMA node index");
        }
        if (size <= 0) {
            throw std::invalid_argument("Size must be greater than zero");
        }
        void* ptr = numa_alloc_onnode(size, node);
        if (!ptr) {
            throw std::runtime_error("Failed to allocate memory on NUMA node");
        }
        return ptr;
    }

    // Allocate memory on interleaved NUMA nodes
    static void* AllocateMemoryInterleaved(size_t size) {
        if (size <= 0) {
            throw std::invalid_argument("Size must be greater than zero");
        }
        void* ptr = numa_alloc_interleaved(size);
        if (!ptr) {
            throw std::runtime_error("Failed to allocate interleaved memory on NUMA nodes");
        }
        return ptr;
    }

    // Free memory allocated on a NUMA system
    static void FreeMemory(void* ptr, size_t size) {
        if (ptr) {
            numa_free(ptr, size);
        }
    }
};
} // namespace congzhi
#endif // defined(__linux__)
#endif // NUMA_WRAPPER_HPP