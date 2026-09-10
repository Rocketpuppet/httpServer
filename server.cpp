#include <cstdio>
#include "server.hh"
#include "epollReactor.hh"
#include <cstring>
#include <csignal>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <memory>

namespace {
    std::mutex logMutex;
}

void logLine(const std::string& line){
    std::lock_guard<std::mutex> lock(logMutex);
    fprintf(stderr, "%s\n", line.c_str());
}

std::string buildResponse(int statusCode, const std::string& statusText, const std::string& body, const std::string& contentType){
    std::string response = "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;
    return response;
}

std::string processHttpRequest(const httpRequest& req, const httpParser& parser){
    const std::string& buf = parser.buffer;

    size_t first_space = buf.find(' ', 0);
    size_t second_space = (first_space == std::string::npos) ? std::string::npos : buf.find(' ', first_space + 1);

    if(first_space == std::string::npos || second_space == std::string::npos || second_space >= (size_t)req.requestHeaderEnd){
        return buildResponse(400, "Bad Request", "Malformed request line\n");
    }

    std::string method = buf.substr(0, first_space);
    std::string endpoint = buf.substr(first_space + 1, second_space - first_space - 1);

    if(method == "GET"){
        if(endpoint == "/"){
            return buildResponse(200, "OK", "Hello, World!\n");
        }
        return buildResponse(404, "Not Found", "Not Found\n");
    }

    if(method == "POST"){
        size_t bodyStart = (size_t)req.headerEnd + 4;
        std::string body = (bodyStart < buf.size()) ? buf.substr(bodyStart) : "";
        return buildResponse(200, "OK", body);
    }

    return buildResponse(501, "Not Implemented", "Unsupported method: " + method + "\n");
}

static int acceptConnection(int serverSocket){
    int clientFd = accept(serverSocket, nullptr, nullptr);
    if(clientFd == -1){
        logLine("accept() failed");
        return -1;
    }
    return clientFd;
}

int main(){

    signal(SIGPIPE, SIG_IGN); // a client closing its end mid-write must not take the whole process down

    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(serverSocket == -1){
        logLine("socket() failed");
        return 1;
    }

    int reuse = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    if(bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == -1){
        logLine("bind() failed");
        close(serverSocket);
        return 1;
    }

    if(listen(serverSocket, 128) == -1){
        logLine("listen() failed");
        close(serverSocket);
        return 1;
    }

    unsigned int numReactors = std::thread::hardware_concurrency();
    if(numReactors == 0) numReactors = 4;

    std::vector<std::unique_ptr<EpollReactor>> reactors;
    reactors.reserve(numReactors);
    for(unsigned int i = 0; i < numReactors; i++){
        reactors.push_back(std::make_unique<EpollReactor>());
    }

    logLine("Listening on port 8080 with " + std::to_string(numReactors) + " epoll reactor threads");

    // accept() itself still blocks — that's fine, there's nothing else for this
    // thread to do while no connection is pending. The blocking I/O this
    // replaces was per-connection recv()/send() inside the request handler,
    // which is now entirely non-blocking and multiplexed via epoll below.
    size_t nextReactor = 0;
    while(true){
        int clientFd = acceptConnection(serverSocket);
        if(clientFd == -1){
            continue;
        }
        reactors[nextReactor]->enqueue(clientFd);
        nextReactor = (nextReactor + 1) % reactors.size();
    }
}
