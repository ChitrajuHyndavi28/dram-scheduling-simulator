"""Runs every scheduler on every workload, sweeps T / banks / queue size, writes results/*.csv and results/*.png.
slowdown(p)  = time(p, shared) / time(p, alone)   (same scheduler, same banks/queue)
unfairness   = max slowdown / min slowdown over the programs in the mix
"""
import csv, os, subprocess, sys
from functools import lru_cache

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, "dramsim")
TR = os.path.join(HERE, "traces")
RES = os.path.join(HERE, "results")
os.makedirs(RES, exist_ok=True)


@lru_cache(maxsize=None)
def run(trace, policy, banks=4, queue=32, T=500):
    if policy != "fair":
        T = 0  # T is irrelevant: share the cache entry
    out = subprocess.run([SIM, os.path.join(TR, trace + ".csv"), policy, str(banks), str(queue), str(T)],
                         capture_output=True, text=True, check=True).stdout.strip().splitlines()
    tot = dict(kv.split("=") for kv in out[0].split())
    per = {}
    for line in out[1:]:
        d = dict(kv.split("=") for kv in line.split())
        per[int(d["pid"])] = {k: float(v) for k, v in d.items()}
    return {k: float(v) for k, v in tot.items()}, per


def mix_metrics(mix, nprog, policy, banks=4, queue=32, T=500):
    tot, per = run(mix, policy, banks, queue, T)
    sd = []
    for pid in range(nprog):
        _, alone = run(f"{mix}__alone{pid}", policy, banks, queue, T)
        sd.append(per[pid]["time"] / alone[pid]["time"])
    return dict(hit_rate=tot["hit_rate"], avg_wait=tot["avg_wait"], throughput=tot["throughput"],
                slowdowns=sd, max_slowdown=max(sd), unfairness=max(sd) / min(sd))


MIXES = {"mix_stream_random": 2, "mix_stream_linkedlist": 2, "mix_two_stream": 2,
         "mix_matmul_linkedlist": 2, "mix4": 4}
POL = [("fifo", "FIFO", 500), ("frfcfs", "FR-FCFS", 500), ("fair", "Fair (T=500)", 500)]


def write_csv(name, header, rows):
    with open(os.path.join(RES, name), "w", newline="") as f:
        w = csv.writer(f); w.writerow(header); w.writerows(rows)


def main():
    # 1. single programs
    rows = []
    for prog in ["stream", "mixed", "matmul", "random", "linkedlist"]:
        for p, label, _ in POL[:2]:
            tot, _ = run(f"single_{prog}", p)
            rows.append([prog, label, tot["hit_rate"], tot["avg_wait"], tot["avg_lat"], tot["throughput"]])
    write_csv("single_programs.csv", ["program", "policy", "hit_rate", "avg_wait", "avg_latency", "throughput"], rows)

    # 2. mixes x policies
    rows = []
    for mix, n in MIXES.items():
        for p, label, T in POL:
            m = mix_metrics(mix, n, p, T=T)
            rows.append([mix, label, m["hit_rate"], m["avg_wait"], m["throughput"],
                         ";".join(f"{s:.3f}" for s in m["slowdowns"]), m["max_slowdown"], m["unfairness"]])
    write_csv("mixes.csv", ["mix", "policy", "hit_rate", "avg_wait", "throughput", "slowdowns", "max_slowdown", "unfairness"], rows)

    # 3. T sweep (stream+random, 4-program)
    rows = []
    for mix, n in [("mix_stream_random", 2), ("mix4", 4)]:
        for T in [50, 100, 200, 500, 1000, 2000, 5000, 20000]:
            m = mix_metrics(mix, n, "fair", T=T)
            rows.append([mix, T, m["hit_rate"], m["throughput"], m["max_slowdown"], m["unfairness"]])
        for p in ("fifo", "frfcfs"):
            m = mix_metrics(mix, n, p)
            rows.append([mix, p, m["hit_rate"], m["throughput"], m["max_slowdown"], m["unfairness"]])
    write_csv("T_sweep.csv", ["mix", "T", "hit_rate", "throughput", "max_slowdown", "unfairness"], rows)

    # 4. bank / queue sweeps (stream+random)
    rows = []
    for b in [1, 2, 4, 8, 16]:
        for q in [8, 16, 32, 64]:
            for p, label, T in POL:
                m = mix_metrics("mix_stream_random", 2, p, banks=b, queue=q, T=T)
                rows.append([b, q, label, m["hit_rate"], m["throughput"], m["max_slowdown"], m["unfairness"]])
    write_csv("sweeps.csv", ["banks", "queue", "policy", "hit_rate", "throughput", "max_slowdown", "unfairness"], rows)
    print("done")


if __name__ == "__main__":
    main()
