#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <atomic>

// Bounded worker-thread pool. main() only accepts connections and hands the
// fd off here; a fixed number of threads pull fds off the queue and run the
// full request/response lifecycle for that connection. Bounded (vs.
// thread-per-connection) so a burst of connections can't spawn unbounded
// OS threads.
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void enqueue(int clientFd);

private:
    void workerLoop();

    std::vector<std::thread> workers;
    std::queue<int> connectionQueue;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::atomic<bool> stopping{false};
};
