#pragma once

#include <thread>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <atomic>
#include <cstdint>
#include "server.hh"

// Per-connection state that persists across epoll wakeups. A blocking
// handler could keep the request and response as local variables because
// one function call = one connection's whole lifetime; here a connection's
// lifetime is spread across many separate onReadable/onWritable callbacks,
// so that state has to live somewhere between calls. This is that somewhere.
struct ConnectionState {
    httpParser parser;
    httpRequest req;
    std::string response;
    size_t responseSent = 0;
};

// One epoll instance running on its own OS thread, capable of multiplexing
// many non-blocking connections at once — unlike ThreadPool, where one
// thread could only ever be doing blocking I/O for one connection at a
// time. main() round-robins accepted fds across a small, fixed number of
// these reactors (typically hardware_concurrency()) instead of needing one
// thread per connection.
//
// `connections` is only ever touched by this reactor's own thread inside
// run(), so it needs no locking. The one piece of state shared with other
// threads is the pending-connections queue, guarded by pendingMutex; a
// notifyFd (eventfd) wakes epoll_wait() up when main() enqueues a new fd,
// since epoll_wait() would otherwise block forever with nothing registered
// yet ready.
class EpollReactor {
public:
    EpollReactor();
    ~EpollReactor();

    EpollReactor(const EpollReactor&) = delete;
    EpollReactor& operator=(const EpollReactor&) = delete;

    // Hand off a freshly accept()ed fd to this reactor. Safe to call from
    // any thread (this is the only cross-thread entry point).
    void enqueue(int clientFd);

private:
    void run();
    void drainNewConnections();
    void handleReadable(int fd);
    void handleWritable(int fd);
    void flushResponse(int fd, ConnectionState& conn);
    void closeConnection(int fd);

    int epollFd;
    int notifyFd;
    std::thread thread;
    std::atomic<bool> stopping{false};

    std::mutex pendingMutex;
    std::queue<int> pendingConnections;

    std::unordered_map<int, ConnectionState> connections;
};
