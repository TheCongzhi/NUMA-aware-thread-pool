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
#include <unordered_map>
#include <functional>
#include <memory>
#include <utility>
#include <future>
#include <type_traits>
#include <atomic>
#include <stdexcept>
#include <chrono>

namespace congzhi {


// congzhi::ThreadPool, interface for different thread pool implementations.


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
     * @brief Interface for stopping the thread pool.
     */
    virtual void Stop() = 0;
    
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
    const size_t expand_factor_{2}; /// Factor by which the thread pool expands when more threads are needed.
    const std::chrono::seconds idle_threshold_{10}; /// Threshold for considering a thread idle, set to 10 seconds.
    
    std::vector<std::unique_ptr<Worker>> workers_; /// Vector of worker threads in the pool.
    std::atomic<size_t> valid_thread_count_{0}; /// Number of currently valid worker threads in the pool.
    std::queue<std::function<void()>> tasks_; /// Queue of tasks to be executed by the worker threads.
    std::atomic<bool> running_{false}; /// Indicates if the thread pool is currently running.
    mutable congzhi::Mutex global_mutex_; /// Mutex to protect access to the task queue and other shared resources. Applied M&M rule.
    congzhi::ConditionVariable cond_var_; /// Condition variable for synchronizing access to the task queue.

    
    std::unique_ptr<congzhi::Thread> monitor_thread_; /// Thread for monitoring the pool and managing thread expansion/shrinking.
    std::atomic<bool> monitoring_{false}; /// Indicates if the monitoring thread is currently running.

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
                cond_var_.Wait(global_mutex_, [this, &worker]() {
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
     * @brief Expands the number of worker threads in the pool if needed.
     * 
     * This function checks the current number of valid threads and creates new threads based on the expand factor.
     * If the current valid thread count is less than the maximum allowed threads, it will create new worker threads
     * until the target count is reached or the maximum thread limit is hit.
     */
    void ExpandThreads() {
        auto current_valid = valid_thread_count_.load();
        if (current_valid >= max_threads_) {
            return;
        }
        auto threads_target_count = (((current_valid * expand_factor_) < max_threads_) ? (current_valid * expand_factor_) : (max_threads_));
        auto threads_need_count = threads_target_count - current_valid;
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
        auto current_valid = valid_thread_count_.load();
        if (current_valid <= min_threads_) {
            return;
        }
        auto threads_need_remove = current_valid - min_threads_;
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
        cond_var_.NotifyAll(); 
    }

    /**
     * @brief Monitoring loop that runs in a separate thread to manage thread expansion and shrinking.
     * 
     * This function periodically checks the state of the worker threads and calls ExpandThreads or ShrinkThreads
     * as needed based on the current task queue size and worker idle states.
     */
    void MonitorLoop() {
        while(monitoring_) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            if (running_) {
                ShrinkThreads();
            } else {
                break;
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
        std::cout << "Min threads: " << min_threads_ << "\n";
        std::cout << "Max threads: " << max_threads_ << "\n";
        std::cout << "Expand factor: " << expand_factor_ << "\n";
        std::cout << "Idle threshold (seconds): " << idle_threshold_.count() << "\n";
        std::cout << "Current valid threads: " << valid_thread_count_.load() << "\n";
        {
            congzhi::LockGuard<congzhi::Mutex> lock(global_mutex_);
            std::cout << "Current tasks in queue: " << tasks_.size() << "\n";
        }
        std::cout << "Is running: " << (running_ ? "Yes" : "No") << "\n";
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
     * @throws std::runtime_error if the thread pool is not running.
     */
    virtual void Stop() override {
        if (!running_) {
            throw std::runtime_error("Thread pool is not running");
        }
        monitoring_ = false;
        if (monitor_thread_ && monitor_thread_->Joinable()) {
            monitor_thread_->Join();
            monitor_thread_.reset();
        }
        running_ = false;
        cond_var_.NotifyAll(); // Notify all threads to wake up and exit.

        congzhi::LockGuard<congzhi::Mutex> lock(worker_mutex_);
        for (auto& worker : workers_) {
            if (worker && worker->thread.Joinable()) {
                worker->thread.Join(); // Wait for all worker threads to finish.
                worker->should_exit = true; // Mark the thread for exit.
            }
        }
        workers_.clear();
        valid_thread_count_ = 0;
        tasks_ = std::queue<std::function<void()>>(); // Clear remaining tasks
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
        cond_var_.NotifyOne(); // Notify one worker thread to wake up and process the task.
        auto task_count = tasks_.size();
        auto current_valid = valid_thread_count_.load();
        if (task_count > current_valid * expand_factor_) {
            ExpandThreads();
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

// NUMA-aware thread pool class - extends ThreadPoolBase for NUMA support.
class NumaThreadPool : public ThreadPool {
private:
    struct Worker {
        congzhi::Thread thread;
        std::atomic<bool> is_valid{false};
        std::atomic<bool> should_exit{false};
        std::atomic<bool> is_idle{false};
        std::chrono::steady_clock::time_point idle_time_start;
        int numa_node{-1}; // NUMA node index for this worker.
    };
    struct NumaNodeData {
        std::queue<std::function<void()>> tasks;
        congzhi::Mutex queue_mutex;
        congzhi::ConditionVariable cond_var;
        std::atomic<size_t> thread_count {0}; // Number of threads bound to this NUMA node
    };

    const int numa_node_count_{congzhi::Numa::NumaNodeCount()};
    const size_t min_threads_per_node_{4};
    const size_t max_threads_per_node_{8};
    const size_t expand_factor_{2};
    const std::chrono::seconds idle_threshold_{10}; // For threads pool expansion and contraction.
    
    std::vector<NumaNodeData> node_data_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<size_t> valid_thread_count_{0}; // Total valid threads across all NUMA nodes
    std::atomic<bool> running_{false};
    mutable congzhi::Mutex global_mutex_;

    // Monitoring thread for threads expansion/shrinking
    std::unique_ptr<congzhi::Thread> monitor_thread_;
    std::atomic<bool> monitoring_{false};

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
            congzhi::Numa::BindThreadToNumaNode(*worker.thread.NativeHandle(), node_num);
        } catch (...) {
            worker.is_valid = false;
            return;
        }

        worker.is_valid = true;
        valid_thread_count_.fetch_add(1);

        while(!worker.should_exit && running_) {                std::function<void()> task;
            worker.is_idle = true;
            worker.idle_time_start = std::chrono::steady_clock::now();
            {
                congzhi::LockGuard<congzhi::Mutex> lock(node_data.queue_mutex);
                node_data.cond_var.Wait(node_data.queue_mutex, [&]() {
                    return !node_data.tasks.empty() || worker.should_exit || !running_;
                })
            }
            if (worker.should_exit || !running_) {
                break;
            }
            worker.is_idle = false;
            {
                congzhi::LockGuard<congzhi::Mutex> lock(node_data.queue_mutex);
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
        node_data.cond_var.NotifyAll();
    }
    // Monitor thread run loop
    void MonitorLoop() {
        while (monitoring_) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            if (running_) {
                for (int node_num = 0; node_num < numa_node_count_; ++node_num) {
                    auto& node_data = node_data_[node_num];
                    congzhi::LockGuard<congzhi::Mutex> lock(node_data.queue_mutex);
                    if (node_data.tasks.size() > node_data.thread_count.load() * expand_factor_) {
                        ExpandThreadsOnNode(node_num);
                    } else {
                        ShrinkThreadsOnNode(node_num);
                    }
                }
            }
        }
    }
public:
    NumaThreadPool() {
        if (!congzhi::Numa::IsNumaSupported()) {
            throw std::runtime_error("NUMA is not supported on this system.");
        }
        node_data_.resize(numa_node_count_);
    }
    ~NumaThreadPool() override {
        if(running_) {
            Stop();
        }
    }
    NumaThreadPool(const NumaThreadPool&) = delete;
    NumaThreadPool& operator=(const NumaThreadPool&) = delete;
    NumaThreadPool(NumaThreadPool&&) = delete;
    NumaThreadPool& operator=(NumaThreadPool&&) = delete;


    // Print NUMA-aware thread pool information
    virtual void Info() const override {
        std::cout << "=== NumaThreadPool Info ===" << std::endl;
        std::cout << "NUMA node count: " << numa_node_count_ << std::endl;
        std::cout << "Min threads per node: " << min_threads_per_node_ << std::endl;
        std::cout << "Max threads per node: " << max_threads_per_node_ << std::endl;
        std::cout << "Expand factor: " << expand_factor_ << std::endl;
        std::cout << "Idle threshold (seconds): " << idle_threshold_.count() << std::endl;
        std::cout << "Current valid threads: " << valid_thread_count_.load() << std::endl;
        for (int node_num = 0; node_num < numa_node_count_; ++node_num) {
            auto& node_data = node_data_[node_num];
            congzhi::LockGuard<congzhi::Mutex> lock(node_data.queue_mutex);
            std::cout << "  Node " << node_num 
                      << ": Threads = " << node_data.thread_count.load() 
                      << ", Tasks in queue = " << node_data.tasks.size() 
                      << std::endl;
        }
        std::cout << "Is running: " << (running_ ? "Yes" : "No") << std::endl;
        std::cout << "===========================" << std::endl;
    }

    // Start the NUMA-aware thread pool
    void Start() override {
        if (running_) {
            throw std::runtime_error("Thread pool is already running");
        }
        running_ = true;
        // Init minimum threads for each NUMA node
        for (int node_num; node_num < numa_node_count_; ++node_num) {
            for (size_t i = 0; i < min_threads_per_node_; ++i) {
                auto new_worker = std::make_unique<Worker>();
                new_worker->numa_node = node_num;
                new_worker->should_exit = false;

                size_t index = workers_.size();
                new_worker->thread.Start([this, index]() {
                    WorkerLoop(index);
                });
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

    // Stop the NUMA-aware thread pool
    void Stop() override {
        if (!running_) {
            throw std::runtime_error("Thread pool is not running");
        }
        
        monitoring_ = false;
        if (monitor_thread_ && monitor_thread_->Joinable()) {
            monitor_thread_->Join();
            monitor_thread_.reset();
        }

        running_ = false;
        for(auto& node_data : node_data_) {
            node_data.cond_var.NotifyAll();
        }

        congzhi::LockGuard<congzhi::Mutex> lock(global_mutex_);
        for (auto& worker_ptr : workers_) {
            if (worker_ptr && worker_ptr->is_valid && worker_ptr->thread.Joinable()) {
                worker_ptr->thread.Join();
            }
        }

        workers_.clear();
        valid_thread_count_ = 0;

        for (auto& node_data : node_data_) {
            congzhi::LockGuard<congzhi::Mutex> lock(node_data.queue_mutex);
            while (!node_data.tasks.empty()) {
                node_data.tasks.pop();
            }
        }
    }

    // Enqueue a task to the NUMA node associated with the current thread.
    void Enqueue(std::function<void()> task) override {
        if (!running_) {
            throw std::runtime_error("Thread pool is not running");
        }
        int node_num = congzhi::Numa::GetNodeCurrentThreadIsOn();
        if (node_num < 0 || node_num >= numa_node_count_) {
            node_num = 0; // Fallback to node 0 if current thread's node is invalid.
        }
        auto& node_data = node_data_[node_num];
        {
            congzhi::LockGuard<congzhi::Mutex> lock(node_data.queue_mutex);
            node_data.tasks.emplace(std::move(task));
        }
        node_data.cond_var.NotifyOne();
    }

    // Enqueue a task to a specific NUMA node 
    void EnqueueToNumaNode(int node_num, std::function<void()> task) {
        if (node_num < 0 || node_num >= numa_node_count_) {
            throw std::out_of_range("Invalid NUMA node index");
        }
        if (!running_) {
            throw std::runtime_error("Thread pool is not running");
        }
        auto& node_data = node_data_[node_num];
        {
            congzhi::LockGuard<congzhi::Mutex> lock(node_data.queue_mutex);
            node_data.tasks.emplace(std::move(task));
        }
        node_data.cond_var.NotifyOne(); // Notify one worker thread to wake up and process the task.
    }
    // Check if the NUMA-aware thread pool is running.    
    bool IsRunning() const override {
        return running_.load();
    }

    // Get the number of worker threads in the NUMA-aware thread pool.    
    size_t WorkerCount() const override {
        return valid_thread_count_.load();
    }

    // Get the number of worker threads on a specific NUMA node.
    size_t WorkerCountOnNumaNode(int node_num) const {
        if (node_num < 0 || node_num >= numa_node_count_) {
            throw std::out_of_range("Invalid NUMA node index");
        }
        congzhi::LockGuard<congzhi::Mutex> lock(node_data_[node_num].queue_mutex);
        auto& node_data = node_data_[node_num];
        return node_data.thread_count.load();
    }

    // Get the number of tasks in the NUMA-aware thread pool.
    size_t TaskCount() const override {
        size_t total_tasks = 0;
        for (const auto& node_data : node_data_) {
            congzhi::LockGuard<congzhi::Mutex> lock(node_data.queue_mutex);
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
