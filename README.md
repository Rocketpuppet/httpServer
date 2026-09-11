#Pietro's HTTP web server

A multi-threaded http-server built in C++ utilizing epoll to improve request throughput. 

#Why this project exists

This server was created to foster a deeper understanding of socket-programming and implement modern-day C++ design patterns. The original goal was to create a http-server with ngix levels of efficiency. 

#Project architecture

The server is built by utilizing epoll() improved time complexity. (Runs in O(1) instead of O(n) like select() and poll()). Using epoll + a modified writing implementation allows each thread to watch over a large number of fds, drastically increasing request throughput. 

#Results 

To check the benchmarks of the http server read the attached benchmark.md file in the git repository.

#Recreating the results for yourself run the following commands

Requirements:
A Linux terminal (WSL, etc)
A C++ 17 complier (g++ or clang++)

This program does not include any third party libraries. 

To run the server run the following: 
sudo apt-get install wrk apache2-utils   # or: wsl.exe -u root -e apt-get install wrk apache2-utils
make
./server &

Once the server is running, run the following command to stress test it:
wrk -t8 -c200 -d10s --latency -s post.lua http://127.0.0.1:8080/echo


