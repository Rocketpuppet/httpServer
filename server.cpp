#include <cstdio>
#include "server.hh"
#include "threadPool.hh"
#include <cstring>
#include <csignal>
#include <mutex>
#include <thread>
#include <string>

namespace {
    std::mutex logMutex;
}

static void logLine(const std::string& line){
    std::lock_guard<std::mutex> lock(logMutex);
    fprintf(stderr, "%s\n", line.c_str());
}

static std::string buildResponse(int statusCode, const std::string& statusText, const std::string& body, const std::string& contentType = "text/plain"){
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

// Runs on a worker thread. Owns clientFd exclusively for its whole lifetime —
// nothing else touches this fd, so the parser/request state needs no locking.
void handleConnection(int clientFd){
    httpRequest req;
    httpParser parser;

    while(parser.state != parseState::complete && parser.state != parseState::error){
        int ret = parser.feed(clientFd, req);
        if(ret <= 0){
            close(clientFd);
            return;
        }
    }

    std::string response = (parser.state == parseState::error)
        ? buildResponse(400, "Bad Request", "Malformed request\n")
        : processHttpRequest(req, parser);

    size_t totalSent = 0;
    while(totalSent < response.size()){
        ssize_t sent = send(clientFd, response.data() + totalSent, response.size() - totalSent, 0);
        if(sent <= 0){
            logLine("Error writing to socket (fd " + std::to_string(clientFd) + ")");
            break;
        }
        totalSent += (size_t)sent;
    }

    close(clientFd);
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

    unsigned int numThreads = std::thread::hardware_concurrency();
    if(numThreads == 0) numThreads = 4;
    ThreadPool pool(numThreads); //Create 4 new threads to check for connections on

    logLine("Listening on port 8080 with " + std::to_string(numThreads) + " worker threads");

    while(true){
        int clientFd = acceptConnection(serverSocket);
        if(clientFd == -1){
            continue;
        }
        pool.enqueue(clientFd);
    }
}
