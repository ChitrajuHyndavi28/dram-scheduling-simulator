"""Graphs from results/*.csv -> results/fig_*.png"""
import csv, os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); RES = os.path.join(HERE, "results")
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

COL = {"FIFO": "#8E9AAF", "FR-FCFS": "#1F4E79", "Fair (T=500)": "#E07A1F"}
plt.rcParams.update({"font.size": 11, "axes.spines.top": False, "axes.spines.right": False, "axes.grid": True,
                     "grid.alpha": .25, "axes.axisbelow": True})
rd = lambda n: list(csv.DictReader(open(os.path.join(RES, n))))

# 1. single programs
s = rd("single_programs.csv"); progs = ["stream", "mixed", "matmul", "random", "linkedlist"]
names = {"stream": "Array sum\n(streaming)", "mixed": "Mixed\n(stream+random)", "matmul": "Matrix\nmultiply", "random": "Random\naccess", "linkedlist": "Linked\nlist"}
fig, ax = plt.subplots(1, 2, figsize=(12, 4))
for k, (met, title, fmt) in enumerate([("hit_rate", "Row-buffer hit rate", "{:.0%}"), ("avg_wait", "Average wait in queue (cycles)", "{:.0f}")]):
    x = np.arange(len(progs))
    for j, pol in enumerate(["FIFO", "FR-FCFS"]):
        v = [float([r for r in s if r["program"] == p and r["policy"] == pol][0][met]) for p in progs]
        b = ax[k].bar(x + (j - .5) * .38, v, .38, label=pol, color=COL[pol])
        for rect, val in zip(b, v): ax[k].text(rect.get_x() + rect.get_width() / 2, val, fmt.format(val), ha="center", va="bottom", fontsize=9)
    ax[k].set_xticks(x); ax[k].set_xticklabels([names[p] for p in progs]); ax[k].set_title(title)
ax[0].legend(frameon=False); plt.tight_layout(); plt.savefig(os.path.join(RES, "fig_single.png"), dpi=200); plt.close()

# 2. per-program slowdown, stream + random
m = rd("mixes.csv")
fig, ax = plt.subplots(figsize=(6.5, 4)); x = np.arange(2)
for j, pol in enumerate(["FIFO", "FR-FCFS", "Fair (T=500)"]):
    r = [r for r in m if r["mix"] == "mix_stream_random" and r["policy"] == pol][0]
    v = [float(t) for t in r["slowdowns"].split(";")]
    b = ax.bar(x + (j - 1) * .26, v, .26, label=f"{pol} (unfairness {float(r['unfairness']):.2f})", color=COL[pol])
    for rect, val in zip(b, v): ax.text(rect.get_x() + rect.get_width() / 2, val, f"{val:.2f}×", ha="center", va="bottom", fontsize=9)
ax.set_xticks(x); ax.set_xticklabels(["Streaming program", "Random-access program"]); ax.set_ylabel("Slowdown vs. running alone")
ax.set_ylim(0, 3); ax.legend(frameon=False, fontsize=9); plt.tight_layout(); plt.savefig(os.path.join(RES, "fig_slowdown.png"), dpi=200); plt.close()

# 3. T sweep
t = rd("T_sweep.csv")
fig, ax = plt.subplots(1, 2, figsize=(11, 4))
for k, mix in enumerate(["mix_stream_random", "mix4"]):
    rows = [r for r in t if r["mix"] == mix]; fr = [r for r in rows if r["T"] == "frfcfs"][0]; ff = [r for r in rows if r["T"] == "fifo"][0]
    sw = [r for r in rows if r["T"].isdigit()]
    ax[k].plot([int(r["T"]) for r in sw], [float(r["unfairness"]) for r in sw], "o-", color=COL["Fair (T=500)"], label="Fair: unfairness")
    ax[k].axhline(float(fr["unfairness"]), color=COL["FR-FCFS"], ls="--", label="FR-FCFS")
    ax[k].axhline(float(ff["unfairness"]), color=COL["FIFO"], ls=":", label="FIFO")
    ax[k].set_xscale("log"); ax[k].set_xlabel("waiting-time limit T (cycles)"); ax[k].set_ylabel("unfairness (max/min slowdown)")
    ax[k].set_title("Streaming + random (2 programs)" if k == 0 else "4-program mix"); ax[k].legend(frameon=False, fontsize=9)
plt.tight_layout(); plt.savefig(os.path.join(RES, "fig_T_sweep.png"), dpi=200); plt.close()

# 4. banks sweep (queue=32)
sw = rd("sweeps.csv"); banks = [1, 2, 4, 8, 16]
fig, ax = plt.subplots(1, 2, figsize=(11, 4))
for k, (met, yl) in enumerate([("unfairness", "unfairness (max/min slowdown)"), ("throughput", "throughput (requests/cycle)")]):
    for pol in ["FIFO", "FR-FCFS", "Fair (T=500)"]:
        ax[k].plot(banks, [float([r for r in sw if r["banks"] == str(b) and r["queue"] == "32" and r["policy"] == pol][0][met]) for b in banks], "o-", color=COL[pol], label=pol)
    ax[k].set_xscale("log", base=2); ax[k].set_xticks(banks); ax[k].set_xticklabels(banks); ax[k].set_xlabel("number of banks"); ax[k].set_ylabel(yl)
ax[0].legend(frameon=False); plt.tight_layout(); plt.savefig(os.path.join(RES, "fig_banks.png"), dpi=200); plt.close()
print("ok")
