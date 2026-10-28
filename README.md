# DRAM scheduling simulator

Balancing Row-Buffer Locality and Fairness in Multi-Program DRAM Scheduling.
One memory channel, N banks, one row buffer per bank, a request queue, and three policies:
**FIFO**, **FR-FCFS**, and **FAIR** (FR-FCFS plus a waiting-time limit T).

## Build and test
```
g++ -O2 -std=c++17 -o dramsim dramsim.cpp      # on macOS with a broken SDK add: -isysroot <older MacOSX*.sdk>
./dramsim --selftest                            # 7 hand-checked tests, 20 checks, must print SELFTEST PASS
```

## Request file format (shared by the simulator and the trace tools)
CSV, one request per line:

| column | meaning |
|---|---|
| `program_id` | integer id of the program issuing the request |
| `address` | byte address (decimal or 0x hex) |
| `R` or `W` | read or write |
| `arrival_cycle` | cycle at which the program would issue the request |

Optional header lines `#win <program_id> <W>` set how many requests that program may have outstanding
(default 8; a linked list uses 1 because each pointer depends on the previous one).
The simulator is closed-loop: program p issues request k a "think time" after request k-1, where
think time = `arrival_k - arrival_{k-1}`, and never exceeds its window.

## Model
- Address mapping: `bank = (addr / 8192) % banks`, `row = addr / 8192 / banks`.
- DDR3-1600 11-11-11: row hit 11 cycles, idle bank 22 (tRCD + tCL), row conflict 33 (tRP + tRCD + tCL).
- The channel starts at most one request every 4 cycles. A bank serves one request at a time.
- FIFO: oldest request only. FR-FCFS: among requests whose bank is free, row hits first, then oldest.
  FAIR: requests waiting at least T cycles first, then row hits, then oldest.
- Not modeled: refresh, tRAS/tFAW, data-bus turnaround, multiple ranks or channels.

## Experiments
```
g++ -O2 -std=c++17 -o record_traces record_traces.cpp
for p in stream random chunks matmul linkedlist; do ./record_traces $p recorded/$p.rec 20000; done
python3 gen_traces.py          # traces/*.csv: single programs and 2-/4-program mixes built from recorded/*.rec
python3 run_experiments.py     # results/*.csv: single programs, mixes, T sweep, banks x queue sweep
python3 make_plots.py          # results/fig_*.png   (needs matplotlib, numpy)
```
Metrics: row-buffer hit rate, average wait, throughput, per-program slowdown
(`time shared / time alone`, same policy), unfairness (`max slowdown / min slowdown`).

## Recorded traces
`record_traces.cpp` holds small real programs: array sum (16 MB), random access (64 MB), chunked scan
(8-64 sequential lines, then a jump), naive 192x192 matrix multiply, and a 16 MB linked list in random order.
Every load/store goes through a tracer that records the real address (offset in one 8 KB-aligned arena),
filters it through a 256 KB 8-way LRU write-allocate cache, and writes only the misses as DRAM requests.
Timing: 2 CPU cycles per access, 4 CPU cycles per memory-clock cycle. Writebacks are not modeled.

## Known limitations
One set of recorded traces (the programs use a fixed pseudo-random seed). A program that finishes early is
not slowed afterwards, so equal-length runs would be a better slowdown measure. No SPEC traces.
In the streaming + random mix the fairness-aware policy reduces unfairness by slowing the streamer;
the random program's own slowdown does not improve.
