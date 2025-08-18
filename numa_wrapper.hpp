/**
 * @file numa_wrapper.hpp
 * @brief A RAII-style classes for NUMA operations.
 * @author Congzhi
 * @date 2025-08-15
 * @license MIT License
 */

#ifndef NUMA_WRAPPER_HPP
#define NUMA_WRAPPER_HPP
#if defined(__linux__)

#include "numa_namespace.hpp"
namespace congzhi {

} // namespace congzhi
#else
#error "numa_namespace only on Linux platforms."
#endif // defined(__linux__)
#endif // NUMA_WRAPPER_HPP