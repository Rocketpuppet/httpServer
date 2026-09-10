CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -O2

server: server.cpp server.hh epollReactor.cpp epollReactor.hh
	$(CXX) $(CXXFLAGS) server.cpp epollReactor.cpp -o server

clean:
	rm -f server

.PHONY: clean
