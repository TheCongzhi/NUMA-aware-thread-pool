/**
 * @file numa_namespace.hpp
 * @brief A utility NUMA namespace for NUMA operations.
 * @author Congzhi
 * @date 2025-08-05
 * @license MIT License
 */

#ifndef NUMA_NAMESPACE_HPP
#define NUMA_NAMESPACE_HPP
#if defined(__linux__)

#include <pthread.h>
#include <sched.h>  // For sched_getcpu
#include <numa.h>   // NUMA support library
#include <numaif.h> // linux syscall interface
#include <unistd.h> // For sysconf
#include <errno.h>

#include <stdexcept>
#include <iostream>
#include <string>

/**
 * @brief A pure utility namespace for NUMA operations.
 */
namespace congzhi::numa {

/**
 * @brief Checks whether the system supports NUMA.
 * @return true if NUMA is available, false otherwise.
 */
bool IsNumaSupported() {
    return numa_available() == 0;
}

/**
 * @brief Returns the number of NUMA nodes available on the system.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @return Number of NUMA nodes.
 */
int NumaNodeCount() {
    //return numa_max_node() + 1;
    return numa_num_configured_nodes();
}

/**
 * @brief Checks if a specific NUMA node is online.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param node The NUMA node index to check.
 * @return true if the node is online, false otherwise.
 */
bool IsNumaNodeOnline(const int node) {
    if (node < 0 || node >= NumaNodeCount()) {
        return false;
    }
    struct bitmask* node_mask = numa_get_mems_allowed();
    return numa_bitmask_isbitset(node_mask, node);
}

/**
 * @brief Checks if a specific NUMA node is available.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param node The NUMA node index to check.
 * @return true if the node is available, false otherwise.
 */
bool IsNumaNodeAvailable(const int node) {
    if (!IsNumaNodeOnline(node)) {
        return false;
    }

    const long long total_memory = numa_node_size64(node, nullptr);
    if (total_memory <= 0) {
        return false;
    }
    return true;
}

/**
 * @brief Gets the NUMA node mask as a bitmask
 * @pre Must be called only if IsNumaSupported() returns true.
 * @return std::vector<bool> representing the NUMA node mask (true for available nodes)
 * @warning std::vector<bool> is a specialized container that uses proxy objects 
 *          instead of actual bool references. This may lead to unexpected behavior
 *          when taking references or pointers to elements.
 */
std::vector<bool> GetNumaNodeMask() {
    const int max_node = numa_max_node();
    std::vector<bool> mask(max_node + 1, false);
    
    for (int node = 0; node <= max_node; ++node) {
        mask[node] = IsNumaNodeAvailable(node);
    }
    return mask;
}

/**
 * @brief Prints the NUMA node mask in a human-readable format, use this with GetNumaNodeMask().
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param mask The NUMA node mask to print
 */
void PrintNumaNodeMask(const std::vector<bool>& mask) {
    std::cout << "NUMA Node Mask: [";
    for (size_t i = 0; i < mask.size(); ++i) {
        std::cout << (mask[i] ? "■" : "□");
        if (i < mask.size() - 1) std::cout << " ";
    }
    std::cout << "]\n";
    std::cout << "Availability: ";
    for (size_t i = 0; i < mask.size(); ++i) {
        if (mask[i]) {
            std::cout << "Node " << i << ", ";
        }
    }
    std::cout << "\b\b are available\n"; // Remove trailing comma

}

/**
 * @brief Gets the total number of CPUs available on the system.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @return Returns the total number of configured CPUs.
 */
int GetTotalCpuCount() {
    return numa_num_configured_cpus();
}

/**
 * @brief Gets the CPU count of a specific NUMA node.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param node The NUMA node index.
 * @return Returns the number of CPUs on the specified NUMA node.
 * @throws std::runtime_error if node is unavailable or CPU mask allocation or retrieval fails.
 */
int GetCpuCountOnNode(const int node) {
    if (!IsNumaNodeAvailable(node)) {
        throw std::runtime_error("NUMA node " + std::to_string(node) + " is not available");
    }

    struct bitmask* cpumask = numa_allocate_cpumask();
    if (!cpumask) {
        throw std::runtime_error("Failed to allocate CPU mask");
    }

    if (numa_node_to_cpus(node, cpumask) != 0) {
        numa_free_cpumask(cpumask);
        throw std::runtime_error("Failed to get CPUs for NUMA node");
    }

    const int cpu_count = numa_bitmask_weight(cpumask);

    numa_free_cpumask(cpumask);
    return cpu_count;
}

/**
 * @brief Gets the NUMA node of a specific CPU.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param cpu The CPU index.
 * @return The NUMA node index of the specified CPU.
 * @throws std::out_of_range if the CPU index is invalid.
 * @throws std::runtime_error if node retrieval fails.  
 */
int GetNodeOfCpu(const int cpu) {
    if (cpu < 0 || cpu >= GetTotalCpuCount()) {
        throw std::out_of_range("Invalid CPU index");
    }
    const int node = numa_node_of_cpu(cpu);
    if (node < 0) {
        int err = errno;
        throw std::runtime_error("Failed to get NUMA node of CPU: " + std::string(strerror(err)));
    }
    return node;
}

/**
 * @brief Binds a specific thread to a given NUMA node.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param thread The pthread_t handle of the thread to bind.
 * @param node The NUMA node index to bind the thread to.
 * @throws std::runtime_error if node is unavailable or affinity setting fails.
 */
void BindThreadToNumaNode(const pthread_t thread, const int node) {
    if (!IsNumaNodeAvailable(node)) {
        throw std::runtime_error("NUMA node" + std::to_string(node) + " is not available");
    }

    // Get the CPU mask associated with the specified NUMA node
    struct bitmask* cpumask = numa_allocate_cpumask();
    if (numa_node_to_cpus(node, cpumask) != 0) {
        numa_free_cpumask(cpumask);
        throw std::runtime_error("Failed to get CPUs for NUMA node" + std::to_string(node));
    }

    // Set the CPU affinity for the thread to the CPUs in the node
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i < cpumask->size; ++i) {
        if (numa_bitmask_isbitset(cpumask, i)) {
            CPU_SET(i, &cpuset);
        }
    }
    numa_free_cpumask(cpumask);
    
    const int res = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (res != 0) {
        throw std::runtime_error("Failed to set thread affinity: " + std::string(strerror(res)));
    }
}

/**
 * @brief Binds the current thread to a specific NUMA node.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param node The NUMA node index to bind the current thread to.
 * @throws std::runtime_error if node is unavailable or CPU affinity setting fails.
 */
void BindThreadToNumaNode(const int node) {
    BindThreadToNumaNode(pthread_self(), node);
}

/**
 * @brief Binds a specific thread to a given CPU.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param thread The pthread_t handle of the thread to bind.
 * @param cpu The CPU index to bind the thread to.
 * @throws std::out_of_range if the CPU index is invalid.
 * @throws std::runtime_error if CPU affinity setting fails.
 */
void BindThreadToCpu(const pthread_t thread, const int cpu) {
    if (cpu < 0 || cpu >= GetTotalCpuCount()) {
        throw std::out_of_range("Invalid CPU index");
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    const int res = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (res != 0) {
        throw std::runtime_error("Failed to set thread affinity: " + std::string(strerror(res)));
    }
}

/**
 * @brief Binds the current thread to a specific CPU.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param cpu The CPU index to bind the current thread to.
 * @throws std::out_of_range if the CPU index is invalid.
 * @throws std::runtime_error if CPU affinity setting fails.
 */
void BindThreadToCpu(const int cpu) {
    BindThreadToCpu(pthread_self(), cpu);
}

/**
 * @brief Gets the NUMA node of the CPU where the current thread is running.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @return The NUMA node index.
 * @throws std::runtime_error if CPU or node detection fails.
 */
int GetNodeCurrentThreadIsOn() {
    const int cpu = sched_getcpu();
    if (cpu < 0) {
        int err = errno;
        throw std::runtime_error("Failed to get current CPU: " + std::string(strerror(err)));
    }
    const int node = numa_node_of_cpu(cpu);
    if (node < 0) {
        int err = errno;
        throw std::runtime_error("Failed to get NUMA node of CPU: " + std::string(strerror(err)));
    }
    return node;
}

/**
 * @brief Round up a size to the next multiple of page size on underlying system.
 * @param size The memory size in bytes.
 * @return The rounded-up size in bytes.
 * @throws std::invalid_argument if the size value is negitive or zero.
 * @throw std::runtime_error if the page size connot be obtained. 
 */
size_t RoundUpToPageSize(const size_t size) {
    if ((size & (size_t(1) << (sizeof(size_t) * 8 -1)) !=0) || (size == 0)) {
        throw std::invalid_argument("Size cannot be negative or zero.");
    }
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        int err = errno;
        throw std::runtime_error("Failed to get page size: " + std::string(strerror(err)));
    }
    const size_t page_size_u = static_cast<size_t>(page_size);
    return (size + page_size_u -1) & ~(page_size_u -1);
}

/**
 * @brief Allocates memory on a specific NUMA node.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param size The size of memory to allocate in bytes.
 * @param node The NUMA node index.
 * @return Pointer to the allocated memory.
 * @throws std::runtime_error if node is unavailable or allocation fails.
 * @throws std::invalid_argument if the size value is negitive or zero.
 */
void* AllocateMemoryOnNode(const size_t size, const int node) {
    if (!IsNumaNodeAvailable(node)) {
        throw std::runtime_error("NUMA node" + std::to_string(node) + " is not available");
    }
    const size_t rounded_up_size = RoundUpToPageSize(size);
    void* ptr = numa_alloc_onnode(rounded_up_size, node);
    if (!ptr) {
        throw std::runtime_error("Failed to allocate memory on NUMA node " + std::to_string(node));
    }
    return ptr;
}

/**
 * @brief Allocates memory interleaved across all NUMA nodes.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param size The size of memory to allocate in bytes.
 * @return Pointer to the allocated memory.
 * @throws std::invalid_argument if the size value is negitive or zero.
 * @throws std::runtime_error if allocation fails.
 */
void* AllocateMemoryInterleaved(const size_t size) {
    const size_t rounded_up_size = RoundUpToPageSize(size);
    void* ptr = numa_alloc_interleaved(rounded_up_size);
    if (!ptr) {
        throw std::runtime_error("Failed to allocate interleaved memory on NUMA nodes.");
    }
    return ptr;
}

/**
 * @brief Frees memory previously allocated on a NUMA node.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param ptr Pointer to the memory block.
 * @param size Size of the memory block in bytes.
 */
void FreeMemory(void* const ptr, const size_t size) {
    if (ptr) {
        numa_free(ptr, size);
    }
}

/**
 * @brief Migrates memory to a specific NUMA node.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param ptr Pointer to the memory block to migrate.
 * @param size Size of the memory block in bytes.
 * @param node The NUMA node index to migrate the memory to.
 * @throws std::runtime_error if node is unvailable or migration fails.
 * @throws std::invalid_argument if ptr is null or size is zero.
 */
void MigrateMemoryToNode(void* ptr, size_t size, const int node) {
    if (!IsNumaNodeAvailable(node)) {
        throw std::runtime_error("NUMA node " + std::to_string(node) + " is not available");
    }

    if (ptr == nullptr) {
        throw std::invalid_argument("Pointer cannot be null");
    }

    size_t rounded_up_size = RoundUpToPageSize(size);
    
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        int err = errno;
        throw std::runtime_error("Failed to get page size: " + std::string(strerror(err)));
    }
    uintptr_t ptr_addr = reinterpret_cast<uintptr_t>(ptr);
    if (ptr_addr % page_size != 0) {
        throw std::invalid_argument("Pointer is not page-aligned (required for migration)");
    }
    if (size % page_size != 0) {
        throw std::invalid_argument("Size is not a multiple of page size (required for migration)");
    }

    // Migrate memory to the specified NUMA node
    numa_tonode_memory(ptr, rounded_up_size, node);
    
}

struct NodeMemory { // struct to hold memory info of a NUMA node
    long long total_;  // total memory of the node (bytes)
    long long free_;   // free memory of the node (bytes)
    long long used_;   // used memory of the node (bytes)
};

/**
 * @brief Gets the memory information of a specific NUMA node.
 * @pre Must be called only if IsNumaSupported() returns true.
 * @param node The NUMA node index.
 * @return A NodeMemoryInfo struct containing total, free, and used memory sizes in bytes.
 * @throws std::out_of_range if the node index is invalid.
 * @throws std::runtime_error if memory info retrieval fails.
 */
NodeMemory GetNodeMemoryInfo(const int node) {
    if (!IsNumaNodeAvailable(node)) {
        throw std::runtime_error("NUMA node " + std::to_string(node) + " is not available");
    }

    // Get total and free memory size of the NUMA node
    long long free = 0;
    long long total = numa_node_size64(node, &free);
    if (total < 0 || free < 0) {
        throw std::runtime_error("Failed to get memory info for node " + std::to_string(node));
    }
    return {total, free, total - free};
}

} // namespace congzhi::numa
#else
#error "numa_namespace only on Linux platforms."
#endif // defined(__linux__)
#endif // NUMA_NAMESPACE_HPP
