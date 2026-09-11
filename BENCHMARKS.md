# Benchmarks

Numbers gathered against the epoll-based server, plus a head-to-head comparison
against the earlier bounded-thread-pool design it replaced (commit `da6b53b`).

## Test environment

- WSL2 Ubuntu, 12 logical cores, 7.7GB RAM
- Client (`wrk`) and server run on the same machine over loopback — these are
  dev-box numbers, not isolated-hardware numbers, and the client process
  itself consumes real CPU generating load
- Built with optimizations on: `g++ -std=c++17 -Wall -Wextra -pthread -O2`
  (the build had no optimization flags before this; unoptimized numbers
  would not be representative)
- Load generated with `wrk` (and `wrk` Lua scripts for POST bodies)

## Current (epoll) server

| Concurrency | Req/s | p50 | p99 | Errors |
|---|---|---|---|---|
| 10 | 24.3k | 123μs | 384μs | 0 |
| 50 | 85.3k | 232μs | 2.63ms | 0 |
| 200 | 96.6k | 426μs | 5.19ms | 0 |
| 1000 | 100.3k | 677μs | 147ms | 0 |
| 5000 | 103.7k | — | avg 17ms, max 1.7s | 0 |

**Throughput ceiling is ~100-105k req/s**, reached around c=200 and flat all
the way out to c=5000. This is a genuine system ceiling and not a
client-side artifact by running two `wrk` processes in parallel — combined
throughput was still ~106k req/s, not roughly double.

**Root cause of the ceiling** (`top` sampled during a c=1000 run): 49% system
time + 24% softirq vs. only 7% userspace. The bottleneck is kernel-side TCP
connection setup/teardown, not the request-handling code. The server has no
HTTP keep-alive, so *every single request* pays a full handshake + 4-way
close. TIME_WAIT sockets hit 13k+ within 3 seconds at c=1000
(`tcp_tw_reuse=2` on loopback is why this doesn't turn into ephemeral-port
exhaustion errors).

**Server resource footprint stayed flat** — 13 OS threads (1 main + 12
reactors), ~30 file descriptors — across the entire range from c=10 to
c=5000. This is the result of implmeneting epoll alongside multi-threading: connection count
and thread count are decoupled.

**POST body size changes which ceiling you hit:**

| Body size | Req/s | Throughput | p50 | p99 |
|---|---|---|---|---|
| 100 bytes | 96.1k | 17.0MB/s | 453μs | 5.27ms |
| 100KB | 23.7k | 2.2GB/s | 6.42ms | 32.87ms |

Small bodies are connection-churn-bound (same ceiling as GET). Large bodies
shift to a bandwidth/copy-bound regime instead.

## Head-to-head vs. the pre-epoll thread-pool design

The thread-pool version ([commit da6b53b](https://github.com/Rocketpuppet/httpServer/commit/da6b53b))
used a fixed pool of 12 worker threads, each doing blocking I/O for exactly
one connection at a time.

**50 idle/stalled connections held open, then one fast `GET /`:**

| | Time to respond |
|---|---|
| Thread-pool version | 4.1 seconds |
| Epoll version | <10ms |

With only 12 workers, 50 stalled clients exhaust the entire pool; any new
request queues behind them until a worker frees up.
The epoll version has no such ceiling on simultaneous connections, since one
reactor thread can multiplex many idle/slow connections without blocking on
any single one.

**Under clean, non-adversarial concurrent load, throughput was statistically
the same for both architectures:**

| Concurrency | Thread-pool req/s | Epoll req/s |
|---|---|---|
| 50 | 89.0k | 85.3k |
| 200 | 101.3k | 96.6k |
| 1000 | 92.0k | 100.3k |

From this data it can be conculded: **epoll's win here is
connection scalability and isolation under adverse conditions, not raw
throughput.** Neither architecture has HTTP keep-alive, so both pay the same
kernel-level TCP setup/teardown cost per request — that shared bottleneck is
what caps throughput in both cases.

## Reproducing these numbers

```bash
sudo apt-get install wrk apache2-utils   # or: wsl.exe -u root -e apt-get install wrk apache2-utils
make
./server &

# baseline
wrk -t2 -c10 -d10s --latency http://127.0.0.1:8080/

# throughput sweep
for c in 50 200 500 1000; do
  wrk -t8 -c$c -d10s --latency http://127.0.0.1:8080/
done

# POST (needs a Lua script to set method/body, e.g.):
#   wrk.method = "POST"
#   wrk.body   = string.rep("x", 100)
wrk -t8 -c200 -d10s --latency -s post.lua http://127.0.0.1:8080/echo
```

To reproduce the idle-connection isolation test, hold connections open with a
partial request (e.g. send `GET ` and nothing else) from a small script while
issuing a concurrent request against `/` and timing it.
