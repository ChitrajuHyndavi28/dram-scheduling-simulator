#!/bin/bash
# Live demo for the proposal presentation. Run from the dram_sim folder:  bash demo.sh
cd "$(dirname "$0")"
build() {  # $1 = source, $2 = output; falls back to an older macOS SDK if the default linker fails
  g++ -O2 -std=c++17 -o "$2" "$1" 2>/dev/null || g++ -O2 -std=c++17 -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.sdk -o "$2" "$1" || exit 1
}
hr() { echo; echo "=============== $1 ==============="; }

hr "1. Build the simulator and the trace recorder"
build dramsim.cpp dramsim && build record_traces.cpp record_traces && echo "built dramsim, record_traces"

hr "2. Hand-checked tests (7 tests, 20 checks) - includes the slide-8 example: FIFO 143 cycles vs FR-FCFS 99"
./dramsim --selftest

hr "3. Record a REAL program's memory accesses (matrix multiply -> cache model -> DRAM misses)"
./record_traces matmul /tmp/demo_matmul.rec 5
echo "(first 5 DRAM requests: arena_offset, R/W, arrival_cycle)"; cat /tmp/demo_matmul.rec

hr "4. Single program: matrix multiply, FIFO vs FR-FCFS (4 banks, 32-entry queue)"
for p in fifo frfcfs; do echo -n "$p:    "; ./dramsim traces/single_matmul.csv $p 4 32 | head -1; done

hr "5. Two programs share the channel: streaming (pid 0) + random access (pid 1)"
for p in fifo frfcfs; do echo "--- $p"; ./dramsim traces/mix_stream_random.csv $p 4 32; done
echo "--- fair (waiting-time limit T = 500 cycles)"; ./dramsim traces/mix_stream_random.csv fair 4 32 500
echo "(compare each program's 'time=' to its alone run to get slowdown)"
for i in 0 1; do echo -n "alone pid $i (frfcfs): "; ./dramsim traces/mix_stream_random__alone$i.csv frfcfs 4 32 | sed -n 2p; done

hr "6. Summary tables from the full experiment run"
python3 - <<'PY'
import csv
print("Streaming + random mix:   policy / slowdowns (stream;random) / unfairness (max/min)")
for r in csv.DictReader(open("results/mixes.csv")):
    if r["mix"] == "mix_stream_random": print(f"  {r['policy']:14} {r['slowdowns']:14} {float(r['unfairness']):.2f}")
print("Waiting-time limit T sweep (same mix): T / unfairness")
for r in csv.DictReader(open("results/T_sweep.csv")):
    if r["mix"] == "mix_stream_random": print(f"  {r['T']:>7} {float(r['unfairness']):.2f}")
PY
echo; echo "Graphs are in results/*.png  (open results)"
