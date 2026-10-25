# Pietro's HTTP Web Server

A multi-threaded HTTP server built in C++, using epoll to improve request throughput.

## Why This Project Exists

This server was built to develop a deeper understanding of socket programming and modern C++ design patterns. The original goal was an HTTP server with nginx-level efficiency.

## Project Architecture

The server uses `epoll()` for connection readiness notification instead of `select()`/`poll()` — `epoll_wait()` only returns the fds that are actually ready, in O(1), rather than scanning the whole fd set in O(n) on every call. Combined with non-blocking sockets, this lets a single thread watch over a large number of connections at once instead of needing one thread per connection, which is what drives the throughput improvement documented below.

## Results

See [BENCHMARKS.md](BENCHMARKS.md) for full results and methodology, including a head-to-head comparison against the earlier thread-per-connection design.

## Building and Running

**Requirements:**
- A Linux terminal (WSL2, etc.) — this server uses `epoll`/`eventfd`, which are Linux-specific and won't build on macOS or native Windows
- A C++17 compiler (`g++` or `clang++`)

No third-party libraries required.

```bash
make
./server &
```

The server listens on port 8080.

## Recreating the Benchmarks Yourself

Install `wrk` and Apache Bench:

```bash
sudo apt-get install wrk apache2-utils
# or, from Windows: wsl.exe -u root -e apt-get install wrk apache2-utils
```

Then stress test it:

```bash
# GET benchmark
wrk -t8 -c200 -d10s --latency http://127.0.0.1:8080/

# POST benchmark (uses post.lua, included in this repo)
wrk -t8 -c200 -d10s --latency -s post.lua http://127.0.0.1:8080/echo
```
