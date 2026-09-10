#include "threadPool.hh"
#include "server.hh"

ThreadPool::ThreadPool(size_t numThreads){
    if(numThreads == 0) numThreads = 1;
    workers.reserve(numThreads);
    for(size_t i = 0; i < numThreads; i++){
        workers.emplace_back(&ThreadPool::workerLoop, this);
    }
}
//Function for closing. worker.joinable and worker.join can stop the code
ThreadPool::~ThreadPool(){
    stopping = true;
    queueCv.notify_all();
    for(std::thread& worker : workers){
        if(worker.joinable()) worker.join();
    }
}


// Threads share global vars which means every thread can read from the list of current connecting items.  
void ThreadPool::enqueue(int clientFd){
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        connectionQueue.push(clientFd);
    }
    queueCv.notify_one();
}


void ThreadPool::workerLoop(){
    while(true){
        int clientFd;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait(lock, [this]{ return stopping.load() || !connectionQueue.empty(); });

            if(connectionQueue.empty()){
                if(stopping.load()) return;
                continue;
            }

            clientFd = connectionQueue.front();
            connectionQueue.pop();
        }

        handleConnection(clientFd);
    }
}
