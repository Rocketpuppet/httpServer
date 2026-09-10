#pragma once
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <string>

enum class parseState{ // Acts like a DFA, if there is an malformed request we can put the parser into error state.
    readingRequestLine,        // Otherwise use complete state.
    readingHeaders,
    readingBody,
    complete,
    error
}; // enum class is bascially just a group of states.

struct httpRequest{
    public:
    int requestHeaderEnd = 0;
    int headerEnd = 0;
    int bodyEnd = 0;
};

class httpParser{
    public:
    std::string buffer;
    parseState state = parseState::readingRequestLine;
    size_t parsedUpTo = 0;
    size_t bodyBytesNeeded = 0;

    // Returns bytes read (>0), 0 if the peer closed the connection, -1 on a
    // real socket error (state is set to error), -2 if Content-Length exceeds
    // the max allowed body size, or -3 if targetFd is non-blocking and has no
    // data available right now — not an error, the caller should wait for the
    // next epoll readiness notification and call feed() again then.
    int feed(int targetFd, httpRequest& req){

        char tempBuffer[8192];
        int ret = recv(targetFd, tempBuffer, sizeof(tempBuffer), 0); // recv() returns the number of bytes recevied.
        if(ret==-1){
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                return -3;
            }
            this->state = parseState::error;
            return -1;
        }
        if(ret==0){
            return 0; // Connection was closed by client
        }

        this->buffer.append(tempBuffer, ret); // Append number of bytes to the buffer.

        // how far back we need to look to catch a \r\n that got split across two recv() calls
        size_t scanStart = (this->parsedUpTo >= 3) ? (this->parsedUpTo - 3) : 0;
        size_t bufEnd = this->buffer.size();

        for(size_t pos = scanStart; pos + 1 < bufEnd; pos++){

            if(this->buffer[pos] != '\r' || this->buffer[pos + 1] != '\n'){ // Started with most likely case.
                continue;
            }

            if(this->state == parseState::readingRequestLine){
                req.requestHeaderEnd = (int)pos;
                this->state = parseState::readingHeaders;
            }
            // Not an else-if: a request with zero headers means the \r\n that ends
            // the request line is *also* the \r\n that starts the blank line, so
            // the just-set readingHeaders state must be checked in this same pass.
            if(this->state == parseState::readingHeaders){
                // blank line = \r\n\r\n = end of headers
                if(pos + 3 < bufEnd && this->buffer[pos + 2] == '\r' && this->buffer[pos + 3] == '\n'){
                    req.headerEnd = (int)pos;

                    long contentLength = getContentLength(this->buffer, req.headerEnd);
                    this->bodyBytesNeeded = (contentLength > 0) ? (size_t)contentLength : 0;

                    if(bodyBytesNeeded>5E+09){
                        return -2;
                    }


                    size_t bodyStart = pos + 4;
                    size_t bodyBytesSoFar = (bufEnd > bodyStart) ? bufEnd - bodyStart : 0;

                    if(this->bodyBytesNeeded == 0 || bodyBytesSoFar >= this->bodyBytesNeeded){
                        req.bodyEnd = (int)(bodyStart + this->bodyBytesNeeded);
                        this->state = parseState::complete;
                    } else {
                        this->state = parseState::readingBody;
                    }
                }
            }
        // readingBody is intentionally NOT handled here — see below.
        }

        // Body completion is a byte-count check, not a CRLF scan — a body can contain
        // anything, including bytes that look like \r\n. Run this once per feed() call.
        if(this->state == parseState::readingBody){
            size_t bodyStart = req.headerEnd + 4;
            size_t bodyBytesSoFar = (bufEnd > bodyStart) ? bufEnd - bodyStart : 0;
            if(bodyBytesSoFar >= this->bodyBytesNeeded){
                req.bodyEnd = (int)(bodyStart + this->bodyBytesNeeded);
                this->state = parseState::complete;
            }
        }

        this->parsedUpTo = this->parsedUpTo + ret;

        return ret;
    };


    void reset(){
        buffer.clear();
        state = parseState::readingRequestLine;
        parsedUpTo = 0;
        bodyBytesNeeded = 0;
    }

    static long getContentLength(const std::string& buffer, size_t headerEnd) {
        std::string headers = buffer.substr(0, headerEnd);

        // case-insensitive search for the header name
        std::string target = "content-length";
        std::string lowerHeaders = headers;
        for (auto& c : lowerHeaders) c = (char)std::tolower((unsigned char)c);

        size_t pos = lowerHeaders.find(target);
        if (pos == std::string::npos) {
            return -1; // no Content-Length header, e.g. most GET requests
        }

        pos = headers.find(':', pos);
        if (pos == std::string::npos) return -1;
        pos++; // move past the colon

        while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) {
            pos++;
        }

        size_t lineEnd = headers.find("\r\n", pos);
        if (lineEnd == std::string::npos) lineEnd = headers.size();

        std::string valueStr = headers.substr(pos, lineEnd - pos);

        try {
            return std::stol(valueStr);
        } catch (...) {
            return -1; // malformed value
        }
    }
};

std::string processHttpRequest(const httpRequest& req, const httpParser& parser);
std::string buildResponse(int statusCode, const std::string& statusText, const std::string& body, const std::string& contentType = "text/plain");
void logLine(const std::string& line);

int main();
