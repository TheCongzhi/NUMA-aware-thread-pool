/**
 * @file thread_pool.hpp
 * @brief A cross-platform thread pool based on congzhi::Thread.
 * @author Congzhi
 * @date 2025-06-11
 * @license MIT License
 * 
 * This header defines the ThreadPool interface for NormalThreadPool and NumaThreadPool.
 * 
 */

#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#if defined(__APPLE__) || defined(__linux__)

#include "pthread_wrapper.hpp"
// #include <pthread.h> // could be explicit

// linux specific includes for NUMA support
#ifdef __linux__
#include "numa_wrapper.hpp"
#endif

#include <iostream>
#include <vector>
#include <queue>
// #include <deque>
#include <unordered_map>
#include <functional>
#include <memory>
#include <utility>
#include <future>
#include <type_traits>
#include <atomic>
#include <stdexcept>
#include <chrono>
// #include <random>

namespace congzhi {
    
/**
 * @brief Enumeration of thread pool shutdown policies.
 */
enum class ShutdownPolicy {
    WaitForAll,      // Wait for all tasks to complete
    WaitForTimeout,  // Wait for a specified timeout, then discard remaining tasks
    Immediate        // Immediately stop all tasks and exit
};

/**
 * @brief Abstract base class for a thread pool. Thus the class is not intended to be instantiated directly.
 * The class only provides a common interface for thread pool implementations.
 */
class ThreadPool {
public:
    /**
     * @brief Interface for printing thread pool information.
     */
    virtual void Info() const = 0;

    /**
     * @brief Interface for starting the thread pool.
     */
    virtual void Start() = 0;
    
    /**
     * @brief Interface for stopping the thread pool with a specified shutdown policy.
     * @param policy The shutdown policy to use when stopping the thread pool.
     * @param timeout The maximum timeout for ShutdownPolicy::WaitForTimeout.
     */
    virtual void Stop(ShutdownPolicy policy = ShutdownPolicy::WaitForAll, 
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) = 0;
    
    /**
     * @brief Enqueues a task to be executed by the thread pool.
     * @param task A callable object representing the task to be executed.
     */
    virtual void Enqueue(std::function<void()> task) = 0;
    
    /**
     * @brief Submits a task to the thread pool and returns a future for the result.
     * @tparam F The type of the callable object.
     * @tparam Args The types of the arguments to be passed to the callable object.
     * @param f The callable object to be executed.
     * @param args The arguments to be passed to the callable object.
     * @return A std::future that will hold the result of the callable object.
     * @note Calling std::future::get() blocks until the asynchronous result is ready.
     */
    template<typename F, typename... Args>
    auto Submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {

        using RetType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<RetType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        auto fut = task->get_future();
        Enqueue([task]() {(*task)();});
        return fut;
    }
    virtual bool IsRunning() const = 0;
    virtual size_t WorkerCount() const = 0;
    virtual size_t TaskCount() const = 0;
    virtual ~ThreadPool() = default;
};

/**
 * @brief A normal thread pool implementation based on congzhi::Thread.
 * 
 * This thread pool manages a number of worker threads that can execute tasks concurrently.
 */
class NormalThreadPool : public ThreadPool {
private:

    /**
     * @brief Worker structure representing a thread in the pool.
     * 
     * Each worker has its own thread, state flags, and idle time tracking.
     */
    struct Worker {
        congzhi::Thread thread; /// The thread object representing the worker.
        std::atomic<bool> is_valid{false}; /// Indicates if the worker is currently valid.
        std::atomic<bool> should_exit{false}; /// Indicates if the worker should exit.
        std::atomic<bool> is_idle{false}; /// Indicates if the worker is currently idle.
        std::chrono::steady_clock::time_point idle_time_start; /// The time when the worker became idle.
    };
    mutable congzhi::Mutex worker_mutex_; /// Mutex to protect access to the worker vector. Applied M&M rule.

    const size_t min_threads_{congzhi::Thread::HardwareConcurrency()}; /// Minimum number of threads in the pool, set to the number of hardware threads available.
    const size_t max_threads_{congzhi::Thread::HardwareConcurrency() * 2}; /// Maximum number of threads in the pool, set to twice the number of hardware threads available.
    constexpr size_t expand_factor_{2}; /// Factor by which the thread pool expands when more threads are needed.
    constexpr size_t expand_threshold_{5}; /// Threshold for expanding the thread pool, set to 5 tasks.
    constexpr std::chrono::milliseconds min_check_interval_{100}; /// Minimum interval for monitoring the thread pool.
    constexpr std::chrono::milliseconds max_check_interval_{5000}; /// Maximum interval for monitoring the thread pool.
    constexpr std::chrono::seconds idle_threshold_{10}; /// Threshold for considering a thread idle, set to 10 seconds.
    
    std::vector<std::unique_ptr<Worker>> workers_; /// Vector of worker threads in the pool.
    std::atomic<size_t> valid_thread_count_{0}; /// Number of currently valid worker threads in the pool.
    std::queue<std::function<void()>> tasks_; /// Queue of tasks to be executed by the worker threads.
    std::atomic<bool> running_{false}; /// Indicates if the thread pool is currently running.
    mutable congzhi::Mutex global_mutex_; /// Mutex to protect access to the global task queue. Applied M&M rule.
    congzhi::ConditionVariable global_cond_; /// Condition variable for synchronizing access to the global task queue.

    std::unique_ptr<congzhi::Thread> monitor_thread_; /// Thread for monitoring the pool and managing thread expansion/shrinking.
    std::atomic<bool> monitoring_{false}; /// Indicates if the monitoring thread is currently running.
    mutable congzhi::Mutex monitor_mutex_; /// Mutex to protect access to the monitoring state.
    congzhi::ConditionVariable monitor_cond_; /// Condition variable for the monitoring thread to wait on.
    std::atomic<std::chrono::milliseconds> monitor_interval_{5000}; /// Interval for the monitoring thread to check the state of the pool.

    /**
     * @brief Worker loop function that runs in each worker thread.
     * 
     * This function continuously checks for tasks to execute and manages the worker's state.
     * 
     * @param worker_index The index of the worker in the workers_ vector.
     */
    void WorkerLoop(size_t worker_index) {
        {
            congzhi::LockGuard<congzhi::Mutex> lock(worker_mutex_);
            if (worker_index >= workers_.size() || !workers_[worker_index]) {
                return;
            }
        }

        auto& worker = workers_[worker_index];
        worker->is_valid = true;
        valid_thread_count_.fetch_add(1);

        while (!worker->should_exit && running_) {
            std::function<void()> task;
            worker->is_idle = true;
            worker->idle_time_start = std::chrono::steady_clock::now();
            {
                congzhi::LockGuard<congzhi::Mutex> lock(global_mutex_);
                global_cond_.Wait(global_mutex_, [this, &worker]() {
                    return !tasks_.empty() || worker->should_exit || !running_;
                });
            }
            worker->is_idle = false;
            {
                congzhi::LockGuard<congzhi::Mutex> lock(global_mutex_);
                if (!tasks_.empty()) {
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
            }
            if (task) {
                task();
            }
        }
        worker->is_valid = false;
        worker->is_idle = false;
        valid_thread_count_.fetch_sub(1);
    }

    /**
     * @brief Adjusts the check interval for the monitoring thread based on the current task load.
     */
    void AdjustCheckInterval() {
        size_t task_count = TaskCount();
        size_t thread_count = valid_thread_count_.load();

        if (task_count == 0) {
            monitor_interval_.store(max_check_interval_);
        } else if (task_count > thread_count * expand_threshold_) {
            monitor_interval_.store(min_check_interval_);
        } else {
            double load_factor = static_cast<double>(task_count) / thread_count;
            monitor_interval_.store(std::chrono::milliseconds(static_cast<int>(min_check_interval_.count() + 
                (max_check_interval_.count() - min_check_interval_.count()) * (1 - load_factor)))
            );
        }
    }

    /**
     * @brief Expands the number of worker threads in the pool if needed.
     * 
     * This function checks the current number of valid threads and creates new threads based on the expand factor.
     * If the current valid thread count is less than the maximum allowed threads, it will create new worker threads
     * until the target count is reached or the maximum thread limit is hit.
     */
    void ExpandThreads() {
        auto current_thread = valid_thread_count_.load();
        if (current_thread >= max_threads_) {
            return;
        }
        auto threads_target_count = (((current_thread * expand_factor_) < max_threads_) ? (current_thread * expand_factor_) : (max_threads_));
        auto threads_need_count = threads_target_count - current_thread;
        if (threads_need_count <= 0) {
            return;
        }
        
        congzhi::LockGuard<congzhi::Mutex> lock(worker_mutex_);
        auto create_thread_count = 0ul;
        
        for (auto i = 0; i < workers_.size() && create_thread_count < threads_need_count; ++i) {
            if (!workers_[i]->is_valid) {
                workers_[i]->should_exit = false;
                workers_[i]->thread.Start([this, index = i]() {
                    WorkerLoop(index);
                });
                ++create_thread_count;
            }
        }

        // Create new worker threads if needed
        while (create_thread_count < threads_need_count && workers_.size() < max_threads_) {
            auto new_worker = std::make_unique<Worker>();
            new_worker->should_exit = false;
            new_worker->thread.Start([this, index = workers_.size()]() {
                WorkerLoop(index);
            });
            workers_.push_back(std::move(new_worker));
            ++create_thread_count;
        }
    }

    /**
     * @brief Shrinks the number of worker threads in the pool if they are idle for too long.
     * 
     * This function checks each worker thread and marks it for exit if it has been idle
     * for longer than the specified idle threshold.
     */
    void ShrinkThreads() {
        auto current_thread = valid_thread_count_.load();
        if (current_thread <= min_threads_) {
            return;
        }
        auto threads_need_remove = current_thread - min_threads_;
        if (threads_need_remove <= 0) {
            return;
        }
        auto now = std::chrono::steady_clock::now();
        size_t removed_threads_count = 0;

        congzhi::LockGuard<congzhi::Mutex> lock(worker_mutex_);
        for (auto i = 0; i <workers_.size() && removed_threads_count < threads_need_remove; ++i) {
            auto& worker = workers_[i];
            if (worker->is_valid && worker->is_idle) {
                auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(now - worker->idle_time_start);
            
                if (idle_time >= idle_threshold_) {
                    worker->should_exit = true; // Mark the thread for exit.
                    ++removed_threads_count;
                }
            }
        }
        global_cond_.NotifyAll(); 
    }

    /**
     * @brief Monitoring loop that runs in a separate thread to manage thread expansion and shrinking.
     * 
     * This function periodically checks the state of the worker threads and calls ExpandThreads or ShrinkThreads
     * as needed based on the current task queue size and worker idle states.
     */
    void MonitorLoop() {
        while(monitoring_) {
            if (running_) {
                ShrinkThreads();

                size_t task_count = TaskCount();
                size_t thread_count = valid_thread_count_.load();
                if (task_count > thread_count * expand_threshold_) {
                    ExpandThreads();
                }
            } else {
                break;
            }
            AdjustCheckInterval();
            {
                congzhi::LockGuard<congzhi::Mutex> lock(monitor_mutex_);
                monitor_cond_.WaitFor(monitor_mutex_, monitor_interval_,
                    [this]() {
                        return !monitoring_ || !running_ || 
                               (TaskCount() > valid_thread_count_.load() * expand_threshold_);
                    }
                );
            }
        }
    }
public:
    /**
     * @brief Constructs a NormalThreadPool with a minimum number of threads.
     * 
     * Initializes the worker vector with the minimum number of threads.
     */
    NormalThreadPool() { workers_.reserve(min_threads_); }
    
    /**
     * @brief Destructor for NormalThreadPool.
     * 
     * Stops the thread pool if it is running and cleans up resources.
     */
    virtual ~NormalThreadPool() override { 
        if (running_) {
            Stop();
        } 
    }
    
    /**
     * @brief Copy constructor is deleted to prevent copying of the thread pool.
     */
    NormalThreadPool(const NormalThreadPool&) = delete;

    /**
     * @brief Copy assignment operator is deleted to prevent copying of the thread pool.
     */
    NormalThreadPool& operator=(const NormalThreadPool&) = delete; 

    /**
     * @brief Move constructor is deleted to prevent moving of the thread pool.
     */
    NormalThreadPool(NormalThreadPool&&) = delete;

    /**
     * @brief Move assignment operator is deleted to prevent moving of the thread pool.
     */
    NormalThreadPool& operator=(NormalThreadPool&&) = delete;

    /**
     * @brief Prints information about the thread pool.
     * 
     * This function outputs the current state of the thread pool, including the number of worker threads,
     * the number of tasks in the queue, and whether the pool is running.
     */
    virtual void Info() const override {
        std::cout << "=== NormalThreadPool Info ===" << "\n";
        std::cout << "Min threads: \t" << min_threads_ << "\n";
        std::cout << "Max threads: \t" << max_threads_ << "\n";
        std::cout << "Expand factor: \t" << expand_factor_ << "\n";
        std::cout << "Expand threshold: \t" << expand_threshold_ << "\n";
        std::cout << "Idle threshold (seconds): \t" << idle_threshold_.count() << "\n";
        std::cout << "Current check interval (seconds): \t" << monitor_interval_.count() << "\n";
        std::cout << "Current valid threads: \t" << valid_thread_count_.load() << "\n";
        std::cout << "Current tasks in queue: \t" << tasks_.size() << "\n";
        std::cout << "Is running: \t" << (running_ ? "Yes" : "No") << "\n";
        std::cout << "=============================" << "\n\n";
    }

    /**
     * @brief Starts the thread pool by creating the initial worker threads.
     * This function initializes the worker threads and starts the monitoring thread.
     * @throws std::runtime_error if the thread pool is already running.
     * @note This function must be called before any tasks can be enqueued.
     */
    virtual void Start() override {
        if (running_) {
            throw std::runtime_error("Thread pool is already running");
        }
        running_ = true;

        congzhi::LockGuard<congzhi::Mutex> lock(worker_mutex_);
        for(auto i = 0; i < min_threads_; ++i) {
            auto new_worker = std::make_unique<Worker>();
            new_worker->thread.Start([this, index = workers_.size()]() {
                WorkerLoop(index);
            });
            workers_.push_back(std::move(new_worker));
        }

        monitoring_ = true;
        monitor_thread_ = std::make_unique<congzhi::Thread>();
        monitor_thread_->Start([this]() {
            MonitorLoop();
        });
    }

    /**
     * @brief Stops the thread pool and cleans up resources.
     * 
     * This function stops the monitoring thread, marks all worker threads for exit,
     * and waits for them to finish before clearing the worker vector and task queue.
     * 
     * @param policy The shutdown policy to use when stopping the thread pool.
     * @param timeout The maximum timeout for ShutdownPolicy::WaitForTimeout.
     * @throws std::runtime_error if the thread pool is not running.
     */
    virtual void Stop(ShutdownPolicy policy = ShutdownPolicy::WaitForAll,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) override {
        if (!running_) {
            throw std::runtime_error("Thread pool is not running");
        }

        monitoring_ = false;
        monitor_cond_.NotifyOne(); // Stop the monitoring thread

        if (monitor_thread_ && monitor_thread_->Joinable()) {
            monitor_thread_->Join();
            monitor_thread_.reset();
        }

        running_ = false;
        global_cond_.NotifyAll(); // Notify all threads to wake up and exit.

        switch (policy) {
            case ShutdownPolicy::WaitForAll: {
                // Wait for all tasks to complete
                while (TaskCount() > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                break;
            }
            case ShutdownPolicy::WaitForTimeout: {
                auto start_time = std::chrono::steady_clock::now();
                while (TaskCount() > 0) {
                    auto elapsed = std::chrono::steady_clock::now() - start_time;
                    if (elapsed >= timeout) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                break;
            }
            case ShutdownPolicy::Immediate:
                break;
        }

        congzhi::LockGuard<congzhi::Mutex> lock(worker_mutex_);
        for (auto& worker : workers_) {
            if (worker && worker->thread.Joinable()) {
                worker->thread.Join(); // Wait for all worker threads to finish.
                worker->should_exit = true; // Mark the thread for exit.
            }
        }
        workers_.clear();
        valid_thread_count_ = 0;
        {
            congzhi::LockGuard<congzhi::Mutex> lock(global_mutex_);
            tasks_ = std::queue<std::function<void()>>(); // Clear the task queue.
        }
    }

    /**
     * @brief Enqueues a task to be executed by the thread pool.
     * 
     * This function adds a task to the task queue and notifies one worker thread to wake up and process the task.
     * If the number of tasks exceeds the current valid thread count multiplied by the expand factor,
     * it will call ExpandThreads to create more worker threads if needed.
     * 
     * @param task A callable object representing the task to be executed.
     * @throws std::runtime_error if the thread pool is not running.
     */
    virtual void Enqueue(std::function<void()> task) override {
        if (!running_) {
            throw std::runtime_error("Thread pool is not running");
        }
        // Enqueue the task queue.
        {
            congzhi::LockGuard<congzhi::Mutex> lock(global_mutex_);
            tasks_.emplace(std::move(task));
        }

        global_cond_.NotifyOne(); // Notify one worker thread to wake up and process the task.
        
        auto task_count = tasks_.size();
        auto current_thread = valid_thread_count_.load();
        if (task_count > current_thread * expand_threshold_) {
            monitor_cond_.NotifyOne(); // Notify the monitoring thread to check if it needs to expand threads.
        }
    }

    /**
     * @brief Checks if the thread pool is currently running.
     * 
     * @return true if the thread pool is running, false otherwise.
     */
    virtual bool IsRunning() const override {
        return running_.load();
    }

    /**
     * @brief Returns the number of valid worker threads in the pool.
     * 
     * @return The number of valid worker threads.
     */
    virtual size_t WorkerCount() const override {
        return valid_thread_count_.load();
    }

    /**
     * @brief Returns the number of tasks currently in the task queue.
     * 
     * @return The number of tasks in the queue.
     */

    virtual size_t TaskCount() const override {
        congzhi::LockGuard<congzhi::Mutex> lock(global_mutex_);
        return tasks_.size();
    }
};

// NUMA awareness functions (Linux only)
#ifdef __linux__

/**
 * @brief A NUMA-aware thread pool implementation based on congzhi::Thread.
 * 
 * This thread pool manages worker threads that are bound to specific NUMA nodes,
 * allowing for efficient task execution on systems with non-uniform memory access.
 */
class NumaThreadPool : public ThreadPool {
private:
    struct Worker {
        congzhi::Thread thread; /// The thread object representing the worker.
        std::atomic<bool> is_valid{false}; /// Indicates if the worker is currently valid.
        std::atomic<bool> should_exit{false}; /// Indicates if the worker should exit.
        std::atomic<bool> is_idle{false}; /// Indicates if the worker is currently idle.
        std::chrono::steady_clock::time_point idle_time_start; /// The time when the worker became idle.
        int numa_node{-1}; // NUMA node index for this worker.
    };
    struct NumaNodeData {
        std::queue<std::function<void()>> tasks;
        congzhi::Mutex tasks_mutex;
        congzhi::ConditionVariable tasks_cond;
        std::atomic<size_t> thread_count {0}; // Number of threads bound to this NUMA node
    };

    const int numa_node_count_{congzhi::numa::NumaNodeCount()};
    const size_t min_threads_per_node_{congzhi::Thread::HardwareConcurrency() / numa_node_count_};
    const size_t max_threads_per_node_{(congzhi::Thread::HardwareConcurrency() * 2) / numa_node_count_};
    constexpr size_t expand_factor_{2};
    constexpr size_t expand_threshold_{5}; // Number of tasks per thread to trigger expansion
    constexpr std::chrono::milliseconds min_check_interval_{100};
    constexpr std::chrono::milliseconds max_check_interval_{5000};
    constexpr std::chrono::seconds idle_threshold_{10}; // For threads pool expansion and contraction.
    
    std::vector<NumaNodeData> node_data_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<size_t> valid_thread_count_{0}; // Total valid threads across all NUMA nodes
    std::atomic<bool> running_{false};
    mutable congzhi::Mutex global_mutex_;

    // Monitoring thread for threads expansion/shrinking
    std::unique_ptr<congzhi::Thread> monitor_thread_;
    std::atomic<bool> monitoring_{false};
    mutable congzhi::Mutex monitor_mutex_;
    congzhi::ConditionVariable monitor_cond_;
    std::atomic<std::chrono::milliseconds> monitor_interval_{5000}; // Monitoring interval

    /**
     * @brief Worker loop function that runs in each worker thread.
     * 
     * This function continuously checks for tasks to execute and manages the worker's state.
     * 
     * @param worker_index The index of the worker in the workers_ vector.
     */
    void WorkerLoop(size_t worker_index){
        {
            congzhi::LockGuard<congzhi::Mutex> lock(global_mutex_);
            if (worker_index >= workers_.size() || !workers_[worker_index]) {
                return;
            }
        }
        Worker& worker = *workers_[worker_index];
        auto node_num = worker.numa_node;

        if (node_num < 0 || node_num >= numa_node_count_) {
            return;
        }

        auto& node_data = node_data_[node_num];
        try {
            congzhi::numa::BindThreadToNumaNode(*worker.thread.NativeHandle(), node_num);
        } catch (...) {
            worker.is_valid = false;
            return;
        }

        worker.is_valid = true;
        valid_thread_count_.fetch_add(1);

        while(!worker.should_exit && running_) {
            std::function<void()> task;
            worker.is_idle = true;
            worker.idle_time_start = std::chrono::steady_clock::now();
            
            {
                congzhi::LockGuard<congzhi::Mutex> lock(node_data.tasks_mutex);
                node_data.tasks_cond.Wait(node_data.tasks_mutex, [&]() {
                    return !node_data.tasks.empty() || worker.should_exit || !running_;
                })
            }

            if (worker.should_exit || !running_) {
                break;
            }
            worker.is_idle = false;
            {
                congzhi::LockGuard<congzhi::Mutex> lock(node_data.tasks_mutex);
                if (!node_data.tasks.empty()) {
                    task = std::move(node_data.tasks.front());
                    node_data.tasks.pop();
                }
            }
            if (task) {
                task();
            }
        }
        worker.is_valid = false;
        worker.is_idle = false;
        valid_thread_count_.fetch_sub(1);
    }

    /**
     * @brief Adjusts the check interval for the monitoring thread based on the current task load.
     */
    void AdjustCheckInterval() {
        size_t task_count = TaskCount();
        size_t thread_count = valid_thread_count_.load();

        if (task_count == 0) {
            monitor_interval_.store(max_check_interval_);
        } else if (task_count > thread_count * expand_threshold_) {
            monitor_interval_.store(min_check_interval_);
        } else {
            double load_factor = static_cast<double>(task_count) / thread_count;
            monitor_interval_.store(std::chrono::milliseconds(static_cast<int>(min_check_interval_.count() + 
                (max_check_interval_.count() - min_check_interval_.count()) * (1 - load_factor)))
            );
        }
    }

    /**
     * @brief Expands the number of worker threads on a specific NUMA node if needed.
     * 
     * This function checks the current number of threads on the specified NUMA node and creates new threads
     * based on the expand factor. If the current thread count is less than the maximum allowed threads,
     * it will create new worker threads until the target count is reached or the maximum thread limit is hit.
     * 
     * @param node_num The NUMA node index to expand threads on.
     */    
    void ExpandThreadsOnNode(int node_num) {
        if (node_num < 0 || node_num >= numa_node_count_) {
            return;
        }
        auto& node_data = node_data_[node_num];
        if (node_data.thread_count.load() >= max_threads_per_node_) {
            return;
        }

        auto threads_target_count = (
            ((node_data.thread_count.load() * expand_factor_) < max_threads_per_node_) 
            ? (node_data.thread_count.load() * expand_factor_) : (max_threads_per_node_)
        );

        auto threads_need_count = threads_target_count - node_data.thread_count.load();
        if (threads_need_count <= 0) {
            return;
        }

        for (auto i = 0; i < threads_need_count; ++i) {
            auto new_worker = std::make_unique<Worker>();
            new_worker->numa_node = node_num;
            new_worker->should_exit = false;

            size_t index = workers_.size();
            new_worker->thread.Start([this, index]() {
                WorkerLoop(index);
            });

            congzhi::LockGuard<congzhi::Mutex> lock(global_mutex_);
            workers_.push_back(std::move(new_worker));
            node_data.thread_count.fetch_add(1);
        }
    }

    /**
     * @brief Shrinks the number of worker threads on a specific NUMA node if they are idle for too long.
     * 
     * This function checks each worker thread on the specified NUMA node and marks it for exit if it has been idle
     * for longer than the specified idle threshold.
     * 
     * @param node_num The NUMA node index to shrink threads on.
     */
    void ShrinkThreadsOnNode(int node_num) {
        if (node_num < 0 || node_num >= numa_node_count_) {
            return;
        }

        auto& node_data = node_data_[node_num];

        if (node_data.thread_count.load() <= min_threads_per_node_) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        size_t thread_need_remove = node_data.thread_count.load() - min_threads_per_node_;
        if (thread_need_remove <= 0) {
            return;
        }
        size_t thread_removed = 0;
        congzhi::LockGuard<congzhi::Mutex> lock(global_mutex_);
        for (auto& worker_ptr : workers_) {
            if (thread_removed >= thread_need_remove) {
                break;
            }
            if (worker_ptr && worker_ptr->is_valid && worker_ptr->is_idle && worker_ptr->numa_node == node_num) {
                auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(
                    now - worker_ptr->idle_time_start
                );
                if (idle_time > idle_threshold_) {
                    worker_ptr->should_exit = true;
                    thread_removed++;
                }
            }
        }
        node_data.tasks_cond.NotifyAll();
    }
    
    /**
     * @brief Monitoring loop that runs in a separate thread to manage thread expansion and shrinking.
     *
     * This function periodically checks the state of the worker threads on each NUMA node and calls ExpandThreadsOnNode or ShrinkThreadsOnNode
     * as needed based on the current task queue size and worker idle states.
     */
    void MonitorLoop() {
        while (monitoring_) {
            if (running_) {
                for (int node_num = 0; node_num < numa_node_count_; ++node_num) {
                    auto& node_data = node_data_[node_num];
                    congzhi::LockGuard<congzhi::Mutex> lock(node_data.tasks_mutex);
                    if (node_data.tasks.size() > node_data.thread_count.load() * expand_threshold_) {
                        ExpandThreadsOnNode(node_num);
                    } else {
                        ShrinkThreadsOnNode(node_num);
                    }
                }
            } else {
                break;
            }
            AdjustCheckInterval();
            {
                congzhi::LockGuard<congzhi::Mutex> lock(monitor_mutex_);
                monitor_cond_.WaitFor(monitor_mutex_, monitor_interval_,
                    [this]() {
                        return !monitoring_ || !running_ || 
                               (TaskCount() > valid_thread_count_.load() * expand_threshold_);
                    }
                );
            }
        }
    }
public:
    /**
     * @brief Constructs a NumaThreadPool with NUMA awareness.
     * 
     * Initializes the node data for each NUMA node and checks if NUMA is supported on the system.
     * 
     * @throws std::runtime_error if NUMA is not supported on the system.
     */
    NumaThreadPool() {
        if (!congzhi::numa::IsNumaSupported()) {
            throw std::runtime_error("NUMA is not supported on this system.");
        }
        node_data_.resize(numa_node_count_);
    }

    /**
     * @brief Destructor for NumaThreadPool.
     * 
     * Stops the thread pool if it is running and cleans up resources.
     */
    virtual ~NumaThreadPool() override {
        if(running_) {
            Stop();
        }
    }

    /// @brief Copy constructor is deleted to prevent copying of the thread pool.  
    NumaThreadPool(const NumaThreadPool&) = delete;
    /// @brief Copy assignment operator is deleted to prevent copying of the thread pool.
    NumaThreadPool& operator=(const NumaThreadPool&) = delete;
    /// @brief Move constructor is deleted to prevent moving of the thread pool.
    NumaThreadPool(NumaThreadPool&&) = delete;
    /// @brief Move assignment operator is deleted to prevent moving of the thread pool.
    NumaThreadPool& operator=(NumaThreadPool&&) = delete;

    /**
     * @brief Prints information about the NUMA-aware thread pool.
     * 
     * This function outputs the current state of the thread pool, including the number of worker threads,
     * the number of tasks in the queue, and whether the pool is running.
     */
    virtual void Info() const override {
        std::cout << "=== NumaThreadPool Info ===" << std::endl;
        std::cout << "NUMA node count: \t" << numa_node_count_ << std::endl;
        std::cout << "Min threads per node: \t" << min_threads_per_node_ << std::endl;
        std::cout << "Max threads per node: \t" << max_threads_per_node_ << std::endl;
        std::cout << "Expand factor: \t" << expand_factor_ << std::endl;
        std::cout << "Expand threshold: \t" << expand_threshold_ << std::endl;
        std::cout << "Current check interval (milliseconds): \t" << monitor_interval_.count() << std::endl;
        std::cout << "Idle threshold (seconds): \t" << idle_threshold_.count() << std::endl;
        std::cout << "Current valid threads: \t" << valid_thread_count_.load() << std::endl;
        
        for (int node_num = 0; node_num < numa_node_count_; ++node_num) {
            auto& node_data = node_data_[node_num];
            congzhi::LockGuard<congzhi::Mutex> lock(node_data.tasks_mutex);
            std::cout << "  Node " << node_num 
                      << ": Threads = " << node_data.thread_count.load() 
                      << ", Tasks in queue = " << node_data.tasks.size() 
                      << std::endl;
            std::cout << "  Total memory on node " << node_num 
                      << ": " << congzhi::numa::GetNodeMemorySize(node_num) << " bytes" << std::endl;
            std::cout << "  Total memory free on node " << node_num
                      << ": " << congzhi::numa::GetNodeMemoryFreeSize(node_num) << " bytes" << std::endl;
            std::cout << "  Total CPUs on node " << node_num
                      << ": " << congzhi::numa::GetNodeCpuCount(node_num) << std::endl;
        }
        std::cout << "Is running: \t" << (running_ ? "Yes" : "No") << std::endl;
        std::cout << "===========================" << std::endl;
    }

    /**
     * @brief Starts the NUMA-aware thread pool by creating the initial worker threads.
     */
    virtual void Start() override {
        if (running_) {
            throw std::runtime_error("Thread pool is already running");
        }
        running_ = true;

        // Init minimum threads for each NUMA node
        for (int node_num = 0; node_num < numa_node_count_; ++node_num) {
            for (size_t i = 0; i < min_threads_per_node_; ++i) {
                auto new_worker = std::make_unique<Worker>();
                new_worker->numa_node = node_num;
                new_worker->should_exit = false;

                size_t index = workers_.size();
                new_worker->thread.Start([this, index]() {
                    WorkerLoop(index);
                });
                // Bind the worker thread to the NUMA node
                congzhi::LockGuard<congzhi::Mutex> lock(global_mutex_);
                workers_.push_back(std::move(new_worker));
                auto& node_data = node_data_[node_num];
                node_data.thread_count.fetch_add(1);
            }
        }
        monitoring_ = true;
        monitor_thread_ = std::make_unique<congzhi::Thread>();
        monitor_thread_->Start([this]() {
            MonitorLoop();
        });
    }

    /**
     * @brief Stops the NUMA-aware thread pool and cleans up resources.
     * 
     * This function stops the monitoring thread, marks all worker threads for exit,
     * and waits for them to finish before clearing the worker vector and task queues for each NUMA node.
     * 
     * @param policy The shutdown policy to use when stopping the thread pool.
     * @param timeout The maximum timeout for ShutdownPolicy::WaitForTimeout.
     * @throws std::runtime_error if the thread pool is not running.
     */
    virtual void Stop(ShutdownPolicy policy = ShutdownPolicy::WaitForAll,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) override {
        if (!running_) {
            throw std::runtime_error("Thread pool is not running");
        }
        
        monitoring_ = false;
        monitor_cond_.NotifyOne(); // Stop the monitoring thread

        if (monitor_thread_ && monitor_thread_->Joinable()) {
            monitor_thread_->Join();
            monitor_thread_.reset();
        }

        running_ = false;
        for(auto& node_data : node_data_) {
            node_data.tasks_cond.NotifyAll();
        }

        switch (policy) {
            case ShutdownPolicy::WaitForAll: {
                // Wait for all tasks to complete
                while (TaskCount() > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                break;
            }
            case ShutdownPolicy::WaitForTimeout: {
                auto start_time = std::chrono::steady_clock::now();
                while (TaskCount() > 0) {
                    auto elapsed = std::chrono::steady_clock::now() - start_time;
                    if (elapsed >= timeout) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                break;
            }
            case ShutdownPolicy::Immediate:
                break;
        }
        // Join all worker threads and clear the worker vector.
        congzhi::LockGuard<congzhi::Mutex> lock(global_mutex_);
        for (auto& worker_ptr : workers_) {
            if (worker_ptr && worker_ptr->is_valid && worker_ptr->thread.Joinable()) {
                worker_ptr->should_exit = true; // Mark the thread for exit.
                worker_ptr->thread.Join();
            }
        }

        workers_.clear();
        valid_thread_count_ = 0;

        for (auto& node_data : node_data_) {
            congzhi::LockGuard<congzhi::Mutex> lock(node_data.tasks_mutex);
            while (!node_data.tasks.empty()) {
                node_data.tasks.pop();
            }
        }
    }

    /**
     * @brief Enqueues a task to be executed by the NUMA-aware thread pool.
     */
    virtual void Enqueue(std::function<void()> task) override {
        if (!running_) {
            throw std::runtime_error("Thread pool is not running");
        }

        int node_num = congzhi::numa::GetNodeCurrentThreadIsOn();
        if (node_num < 0 || node_num >= numa_node_count_) {
            node_num = 0; // Fallback to node 0 if current thread's node is invalid.
        }

        EnqueueToNumaNode(node_num, std::move(task));
    }

    /**
     * @brief Enqueues a task to a specific NUMA node.
     * 
     * This function adds a task to the task queue of the specified NUMA node and notifies one worker thread
     * on that node to wake up and process the task.
     * 
     * @param node_num The NUMA node index to enqueue the task to.
     * @param task A callable object representing the task to be executed.
     * @throws std::out_of_range if the node_num is invalid.
     * @throws std::runtime_error if the thread pool is not running.
     */
    virtual void EnqueueToNumaNode(int node_num, std::function<void()> task) {
        if (node_num < 0 || node_num >= numa_node_count_) {
            throw std::out_of_range("Invalid NUMA node index");
        }
        if (!running_) {
            throw std::runtime_error("Thread pool is not running");
        }
        auto& node_data = node_data_[node_num];
        {
            congzhi::LockGuard<congzhi::Mutex> lock(node_data.tasks_mutex);
            node_data.tasks.emplace(std::move(task));
        }
        node_data.tasks_cond.NotifyOne(); // Notify one worker thread to wake up and process the task.
    }
    // Check if the NUMA-aware thread pool is running.    
    virtual bool IsRunning() const override {
        return running_.load();
    }

    // Get the number of worker threads in the NUMA-aware thread pool.    
    virtual size_t WorkerCount() const override {
        return valid_thread_count_.load();
    }

    // Get the number of worker threads on a specific NUMA node.
    size_t WorkerCountOnNumaNode(int node_num) const {
        if (node_num < 0 || node_num >= numa_node_count_) {
            throw std::out_of_range("Invalid NUMA node index");
        }
        congzhi::LockGuard<congzhi::Mutex> lock(node_data_[node_num].tasks_mutex);
        auto& node_data = node_data_[node_num];
        return node_data.thread_count.load();
    }

    // Get the number of tasks in the NUMA-aware thread pool.
    virtual size_t TaskCount() const override {
        size_t total_tasks = 0;
        for (const auto& node_data : node_data_) {
            congzhi::LockGuard<congzhi::Mutex> lock(node_data.tasks_mutex);
            total_tasks += node_data.tasks.size();
        }
        return total_tasks;
    }
};
#endif


} // namespace congzhi

#else
#error "This thread pool is only supported on Linux and macOS platforms."
#endif // __APPLE__ || __linux__
#endif // THREAD_POOL_HPP
