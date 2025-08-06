/**
 * @file numa_wrapper.hpp
 * @brief A static utility NUMA operations wrapper for Linux.
 * @author Congzhi
 * @date 2025-08-05
 * @license MIT License
 */

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

/**
 * @brief A static utility class for NUMA (Non-Uniform Memory Access) operations.
 *
 * The congzhi::Numa class provides convenient static methods for querying NUMA topology,
 * binding threads to specific NUMA nodes, and allocating memory either on specific nodes
 * or interleaved across multiple nodes.
 *
 * This class is non-instantiable by design.
 */
class Numa {
private:
    /// @brief Private constructor to prevent instantiation.
    Numa() = default;
    
    /// @brief Private destructor to prevent deletion.
    ~Numa() = default;

    /// @brief Deleted copy constructor.
    Numa(const Numa&) = delete;

    /// @brief Deleted copy assignment operator.
    Numa& operator=(const Numa&) = delete;

    /// @brief Deleted move constructor.
    Numa(Numa&&) = delete;

    /// @brief Deleted move assignment operator.
    Numa& operator=(Numa&&) = delete;

public:
    /**
     * @brief Checks whether the system supports NUMA.
     * @return true if NUMA is available, false otherwise.
     */
    static bool IsNumaSupported() {
        return numa_available() == 0;
    }

    /**
     * @brief Returns the number of NUMA nodes available on the system.
     * @return Number of NUMA nodes.
     */
    static int NumaNodeCount() {
        return numa_max_node() + 1;
    }

    /**
     * @brief Binds a specific thread to a given NUMA node.
     * @param thread The pthread_t handle of the thread to bind.
     * @param node The NUMA node index to bind the thread to.
     * @throws std::out_of_range if the node index is invalid.
     * @throws std::runtime_error if CPU affinity setting fails.
     */
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
        int cpu = -1;
        while ((cpu = numa_bitmask_next(nodemask, cpu)) >= 0) {
            CPU_SET(cpu, &cpuset);
        }
        // for (int i = 0; i < nodemask->size; ++i) {
        //     if (numa_bitmask_isbitset(nodemask, i)) {
        //         CPU_SET(i, &cpuset);
        //     }
        // }
        int res = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
        numa_free_nodemask(nodemask);
        
        if (res != 0) {
            throw std::runtime_error("Failed to set thread affinity: " + std::string(strerror(res)));
        }
    }

    /**
     * @brief Gets the NUMA node of the CPU where the current thread is running.
     * @return The NUMA node index.
     * @throws std::runtime_error if CPU or node detection fails.
     */
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

    /**
     * @brief Allocates memory on a specific NUMA node.
     * @param size The size of memory to allocate in bytes.
     * @param node The NUMA node index.
     * @return Pointer to the allocated memory.
     * @throws std::out_of_range if the node index is invalid.
     * @throws std::invalid_argument if size is zero.
     * @throws std::runtime_error if allocation fails.
     */
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

    /**
     * @brief Allocates memory interleaved across all NUMA nodes.
     * @param size The size of memory to allocate in bytes.
     * @return Pointer to the allocated memory.
     * @throws std::invalid_argument if size is zero.
     * @throws std::runtime_error if allocation fails.
     */
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

    /**
     * @brief Frees memory previously allocated on a NUMA node.
     * @param ptr Pointer to the memory block.
     * @param size Size of the memory block in bytes.
     */
    static void FreeMemory(void* ptr, size_t size) {
        if (ptr) {
            numa_free(ptr, size);
        }
    }
};
} // namespace congzhi
#endif // defined(__linux__)
#endif // NUMA_WRAPPER_HPP