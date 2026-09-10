CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread

server: server.cpp server.hh threadPool.cpp threadPool.hh
	$(CXX) $(CXXFLAGS) server.cpp threadPool.cpp -o server

clean:
	rm -f server

.PHONY: clean
