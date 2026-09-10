#include "epollReactor.hh"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <fcntl.h>

namespace {
    void setNonBlocking(int fd){
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

EpollReactor::EpollReactor(){
    epollFd = epoll_create1(0);
    notifyFd = eventfd(0, EFD_NONBLOCK);

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = notifyFd;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, notifyFd, &ev);

    thread = std::thread(&EpollReactor::run, this);
}

EpollReactor::~EpollReactor(){
    stopping = true;
    uint64_t one = 1;
    ssize_t ignored = write(notifyFd, &one, sizeof(one)); // wake epoll_wait() so it notices stopping
    (void)ignored;
    if(thread.joinable()) thread.join();
    close(epollFd);
    close(notifyFd);
}

void EpollReactor::enqueue(int clientFd){
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingConnections.push(clientFd);
    }
    uint64_t one = 1;
    ssize_t ignored = write(notifyFd, &one, sizeof(one));
    (void)ignored;
}

void EpollReactor::drainNewConnections(){
    uint64_t val;
    ssize_t ignored = read(notifyFd, &val, sizeof(val)); // clears the eventfd readiness
    (void)ignored;

    std::queue<int> newFds;
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        std::swap(newFds, pendingConnections);
    }

    while(!newFds.empty()){
        int fd = newFds.front();
        newFds.pop();

        setNonBlocking(fd);
        connections.emplace(fd, ConnectionState{});

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = fd;
        epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev);
    }
}

void EpollReactor::closeConnection(int fd){
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    connections.erase(fd);
}

// Attempts to send whatever of conn.response hasn't gone out yet, without
// blocking. If the socket's send buffer fills up mid-write, switches this
// fd's epoll interest to EPOLLOUT so we get called again once there's room,
// instead of blocking the whole reactor thread on send().
void EpollReactor::flushResponse(int fd, ConnectionState& conn){
    while(conn.responseSent < conn.response.size()){
        ssize_t sent = send(fd, conn.response.data() + conn.responseSent,
                             conn.response.size() - conn.responseSent, 0);
        if(sent == -1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                epoll_event ev{};
                ev.events = EPOLLOUT | EPOLLRDHUP;
                ev.data.fd = fd;
                epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &ev);
                return;
            }
            closeConnection(fd);
            return;
        }
        conn.responseSent += (size_t)sent;
    }

    closeConnection(fd); // full response sent; server always replies with Connection: close
}

void EpollReactor::handleReadable(int fd){
    auto it = connections.find(fd);
    if(it == connections.end()) return;
    ConnectionState& conn = it->second;

    while(conn.parser.state != parseState::complete && conn.parser.state != parseState::error){
        int ret = conn.parser.feed(fd, conn.req);
        if(ret == -3){
            return; // no data available right now; wait for the next EPOLLIN
        }
        if(ret <= 0){
            closeConnection(fd); // 0 = peer closed, -1 = real error, -2 = body too large
            return;
        }
    }

    conn.response = (conn.parser.state == parseState::error)
        ? buildResponse(400, "Bad Request", "Malformed request\n")
        : processHttpRequest(conn.req, conn.parser);
    conn.responseSent = 0;

    flushResponse(fd, conn); // optimistic immediate write; falls back to EPOLLOUT if it would block
}

void EpollReactor::handleWritable(int fd){
    auto it = connections.find(fd);
    if(it == connections.end()) return;
    flushResponse(fd, it->second);
}

void EpollReactor::run(){
    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while(!stopping.load()){
        int n = epoll_wait(epollFd, events, MAX_EVENTS, -1);
        if(n == -1){
            if(errno == EINTR) continue;
            break;
        }

        for(int i = 0; i < n; i++){
            int fd = events[i].data.fd;
            uint32_t flags = events[i].events;

            if(fd == notifyFd){
                drainNewConnections();
                continue;
            }

            if(flags & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)){
                closeConnection(fd);
                continue;
            }
            if(flags & EPOLLIN){
                handleReadable(fd);
                continue; // fd may already be closed/erased by handleReadable — don't touch it again
            }
            if(flags & EPOLLOUT){
                handleWritable(fd);
            }
        }
    }
}
