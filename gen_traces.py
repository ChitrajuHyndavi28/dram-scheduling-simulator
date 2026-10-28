"""Builds simulator traces from the address recordings in recorded/*.rec (made by record_traces.cpp
from real running programs).  Output: traces/<name>.csv with lines  program_id,address,R|W,arrival_cycle
and '#win pid W' header lines.  Multi-program mixes put each program in its own 1 GB address region.
Run  ./record_traces <prog> recorded/<prog>.rec  first (see README).
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REC = os.path.join(HERE, "recorded")
OUT = os.path.join(HERE, "traces")
REGION = 1 << 30

# program name -> recorded file, outstanding-request window (pointer chasing = 1)
PROGS = {"stream": "stream", "mixed": "chunks", "matmul": "matmul", "random": "random", "linkedlist": "linkedlist"}
WINDOW = {"stream": 8, "mixed": 8, "matmul": 8, "random": 8, "linkedlist": 1}


def load(prog):
    rows = []
    for line in open(os.path.join(REC, PROGS[prog] + ".rec")):
        off, rw, t = line.strip().split(",")
        rows.append((int(off), rw, int(t)))
    return rows


def write(name, progs):
    """progs = list of program names; program i becomes pid i."""
    rows, wins = [], []
    for pid, p in enumerate(progs):
        wins.append(f"#win {pid} {WINDOW[p]}")
        rows += [(t, pid, pid * REGION + off, rw) for off, rw, t in load(p)]
    rows.sort()
    with open(os.path.join(OUT, name + ".csv"), "w") as f:
        f.write("\n".join(wins) + "\n")
        for t, pid, a, rw in rows:
            f.write(f"{pid},{a},{rw},{t}\n")


MIXES = {
    "mix_stream_random": ["stream", "random"],
    "mix_stream_linkedlist": ["stream", "linkedlist"],
    "mix_two_stream": ["stream", "stream"],
    "mix_matmul_linkedlist": ["matmul", "linkedlist"],
    "mix4": ["stream", "random", "matmul", "linkedlist"],
}

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for p in PROGS:
        write("single_" + p, [p])
    for name, progs in MIXES.items():
        write(name, progs)
        for pid, p in enumerate(progs):          # alone-baselines for slowdown: same pid, same addresses
            rows = [(t, pid, pid * REGION + off, rw) for off, rw, t in load(p)]
            with open(os.path.join(OUT, f"{name}__alone{pid}.csv"), "w") as f:
                f.write(f"#win {pid} {WINDOW[p]}\n")
                for t, _, a, rw in sorted(rows):
                    f.write(f"{pid},{a},{rw},{t}\n")
    print("traces written to", OUT)
