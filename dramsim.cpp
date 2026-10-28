// Minimal DRAM memory-controller simulator: one channel, N banks, one row buffer per bank.
// Policies: FIFO, FRFCFS, FAIR (FR-FCFS with a waiting-time limit T).
//
// Request file (CSV): program_id,address,R|W,arrival_cycle  (+ optional "#win pid W" lines)
// Usage: dramsim <trace.csv> <policy> [banks=4] [queue=32] [T=500]
//        dramsim --selftest
// Output: one line of key=value pairs (see report()).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using namespace std;

// DDR3-1600 11-11-11 (in memory-clock cycles)
static const int T_HIT = 11;       // tCL
static const int T_CLOSED = 22;    // tRCD + tCL (bank has no open row)
static const int T_CONFLICT = 33;  // tRP + tRCD + tCL
static const int T_ISSUE = 4;      // channel can start one request every 4 cycles (burst length)
static const uint64_t ROW_BYTES = 8192;

enum Policy { FIFO, FRFCFS, FAIR };

struct Req {
  int pid;
  uint64_t addr;
  long arrival;
  int bank;
  long row;
  long issue = -1, done = -1;
  bool hit = false;
};

struct Bank {
  long open_row = -1;
  long free_at = 0;
};

struct Stats {
  long n = 0, hits = 0, lat_sum = 0, wait_sum = 0, last_done = 0;
};

struct Result {
  map<int, Stats> per;  // per program
  Stats all;
};

// Closed-loop cores: program p issues its k-th request `think` cycles after issuing its (k-1)-th,
// but never has more than window[p] requests outstanding (queued or in service).
// The trace "arrival" column is turned into think times (arrival_k - arrival_{k-1}).
static Result simulate(vector<Req> reqs, map<int, int> window, Policy pol, int nbanks, int qsize,
                       long T, long init_open_row = -1) {
  map<int, vector<int>> by_prog;
  for (size_t i = 0; i < reqs.size(); i++) {
    uint64_t line_row = reqs[i].addr / ROW_BYTES;  // row-interleaved mapping
    reqs[i].bank = (int)(line_row % nbanks);
    reqs[i].row = (long)(line_row / nbanks);
    by_prog[reqs[i].pid].push_back((int)i);
  }
  struct Prog { vector<int> idx; size_t next = 0; long ready = 0; int outstanding = 0; int win = 8; };
  map<int, Prog> progs;
  for (auto &kv : by_prog) {
    Prog &p = progs[kv.first];
    p.idx = kv.second;
    p.win = window.count(kv.first) ? window[kv.first] : 8;
    p.ready = reqs[p.idx[0]].arrival;
  }
  vector<Bank> banks(nbanks);
  for (auto &b : banks) b.open_row = init_open_row;
  vector<int> queue, inflight;  // queue: waiting, age-ordered; inflight: issued, not yet done
  size_t finished = 0;
  long now = 0, last_issue = -T_ISSUE;
  vector<long> think(reqs.size(), 0), enq(reqs.size(), 0);
  for (auto &kv : progs)
    for (size_t k = 1; k < kv.second.idx.size(); k++)
      think[kv.second.idx[k]] = max(1L, reqs[kv.second.idx[k]].arrival - reqs[kv.second.idx[k - 1]].arrival);
  while (finished < reqs.size()) {
    for (size_t k = 0; k < inflight.size();)  // retire finished requests
      if (reqs[inflight[k]].done <= now) { progs[reqs[inflight[k]].pid].outstanding--; inflight[k] = inflight.back(); inflight.pop_back(); }
      else k++;
    // admit: programs with the earliest ready time first, while the queue has room
    for (;;) {
      Prog *best = nullptr;
      for (auto &kv : progs) {
        Prog &p = kv.second;
        if (p.next < p.idx.size() && p.ready <= now && p.outstanding < p.win && (int)queue.size() < qsize)
          if (!best || p.ready < best->ready) best = &p;
      }
      if (!best) break;
      int i = best->idx[best->next++];
      reqs[i].arrival = now;  // effective arrival at the controller
      queue.push_back(i);
      best->outstanding++;
      if (best->next < best->idx.size()) best->ready = now + think[best->idx[best->next]];
    }
    int pick = -1;
    if (!queue.empty() && now - last_issue >= T_ISSUE) {
      auto ready = [&](int i) { return banks[reqs[i].bank].free_at <= now; };
      auto is_hit = [&](int i) { return banks[reqs[i].bank].open_row == reqs[i].row; };
      if (pol == FIFO) {
        if (ready(queue[0])) pick = 0;  // strictly oldest first
      } else {
        // priority: aged (FAIR only) > row hit > oldest; only requests whose bank is free
        int best = -1, best_class = -1;
        for (size_t q = 0; q < queue.size(); q++) {
          int i = queue[q];
          if (!ready(i)) continue;
          int cls = 0;
          if (pol == FAIR && now - reqs[i].arrival >= T) cls = 2;
          else if (is_hit(i)) cls = 1;
          if (cls > best_class) { best_class = cls; best = (int)q; }  // queue is age-ordered
        }
        pick = best;
      }
    }
    if (pick >= 0) {
      int qi = queue[pick];
      Req &r = reqs[qi];
      Bank &b = banks[r.bank];
      int lat = (b.open_row == r.row) ? T_HIT : (b.open_row < 0 ? T_CLOSED : T_CONFLICT);
      r.hit = (b.open_row == r.row);
      r.issue = now;
      r.done = now + lat;
      b.open_row = r.row;
      b.free_at = r.done;
      last_issue = now;
      queue.erase(queue.begin() + pick);
      inflight.push_back(qi);
      finished++;
    }
    now++;
  }
  Result res;
  for (auto &r : reqs) {
    for (Stats *s : {&res.per[r.pid], &res.all}) {
      s->n++;
      s->hits += r.hit;
      s->lat_sum += r.done - r.arrival;
      s->wait_sum += r.issue - r.arrival;
      s->last_done = max(s->last_done, r.done);
    }
  }
  return res;
}

// Trace lines "#win <pid> <W>" set the per-program outstanding-request window (default 8).
static vector<Req> load(const string &path, map<int, int> &window) {
  ifstream f(path);
  if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); exit(1); }
  vector<Req> v;
  string line;
  while (getline(f, line)) {
    if (line.empty()) continue;
    if (line[0] == '#') {
      int pid, w;
      if (sscanf(line.c_str(), "#win %d %d", &pid, &w) == 2) window[pid] = w;
      continue;
    }
    stringstream ss(line);
    string a, b, c, d;
    getline(ss, a, ','); getline(ss, b, ','); getline(ss, c, ','); getline(ss, d, ',');
    Req r;
    r.pid = atoi(a.c_str());
    r.addr = strtoull(b.c_str(), nullptr, 0);
    r.arrival = atol(d.c_str());
    v.push_back(r);
  }
  stable_sort(v.begin(), v.end(), [](const Req &x, const Req &y) { return x.arrival < y.arrival; });
  return v;
}

static Policy parse_policy(const string &s) {
  if (s == "fifo") return FIFO;
  if (s == "frfcfs") return FRFCFS;
  if (s == "fair") return FAIR;
  fprintf(stderr, "policy must be fifo|frfcfs|fair\n");
  exit(1);
}

// ---------------------------------------------------------------------------------------------
// Hand-checked tests. Every expected number below was worked out on paper first.
// Each request is its own program (pid = age order), so all requests are in the queue at cycle 0.
// Row r of bank b lives at address (r*nbanks + b) * ROW_BYTES.
// Timing: hit 11, closed bank 22, conflict 33 (a conflict = precharge + activate + read).
// ---------------------------------------------------------------------------------------------
struct Probe { long makespan; long hits; long wait0; };

static Probe probe(const vector<pair<int, long>> &bank_row, int nbanks, Policy pol, long T,
                   long init_row) {
  vector<Req> v;
  for (size_t i = 0; i < bank_row.size(); i++) {
    Req r; r.pid = (int)i; r.arrival = 0;
    r.addr = ((uint64_t)bank_row[i].second * nbanks + bank_row[i].first) * ROW_BYTES;
    v.push_back(r);
  }
  Result res = simulate(v, {}, pol, nbanks, 32, T, init_row);
  return {res.all.last_done, res.all.hits, res.per[0].wait_sum};
}

static int selftest() {
  int fails = 0;
  auto check = [&](const char *name, long got, long want) {
    bool ok = got == want;
    printf("  %-58s got %4ld  expect %4ld  %s\n", name, got, want, ok ? "ok" : "FAIL");
    fails += !ok;
  };
  const long A = 1, B = 2, X = 2;
  // Test 1: slide-8 example. One bank, rows A,B,A,A,B, other row (99) open at start.
  //   FIFO:    4 conflicts + 1 hit            = 4*33 + 11 = 143, 1 hit
  //   FR-FCFS: A(33) A(11) A(11) B(33) B(11)  = 99,           3 hits
  printf("Test 1: five requests, one bank (A,B,A,A,B)\n");
  vector<pair<int, long>> t1 = {{0, A}, {0, B}, {0, A}, {0, A}, {0, B}};
  check("FIFO makespan", probe(t1, 1, FIFO, 0, 99).makespan, 143);
  check("FIFO row hits", probe(t1, 1, FIFO, 0, 99).hits, 1);
  check("FR-FCFS makespan", probe(t1, 1, FRFCFS, 0, 99).makespan, 99);
  check("FR-FCFS row hits", probe(t1, 1, FRFCFS, 0, 99).hits, 3);
  // Test 2: one request to a bank with no open row costs precharge-free activate + read = 22.
  printf("Test 2: single request, idle bank\n");
  check("makespan (tRCD + tCL)", probe({{0, A}}, 1, FRFCFS, 0, -1).makespan, 22);
  check("row hits", probe({{0, A}}, 1, FRFCFS, 0, -1).hits, 0);
  // Test 3: two requests, same row, idle bank: 22 for the first, then a hit (11) once the bank is free.
  printf("Test 3: two requests to the same row\n");
  check("makespan (22 + 11)", probe({{0, A}, {0, A}}, 1, FRFCFS, 0, -1).makespan, 33);
  check("row hits", probe({{0, A}, {0, A}}, 1, FRFCFS, 0, -1).hits, 1);
  // Test 4: two requests to different banks overlap. Channel starts one request every 4 cycles,
  //   so request 0 runs 0..22 and request 1 runs 4..26.
  printf("Test 4: two banks work in parallel\n");
  check("makespan (4 + 22)", probe({{0, A}, {1, A}}, 2, FRFCFS, 0, -1).makespan, 26);
  // Test 5: row hit vs. conflict on one bank, other row open: conflict 33 then hit 11 = 44.
  printf("Test 5: conflict then hit\n");
  check("makespan (33 + 11)", probe({{0, A}, {0, A}}, 1, FRFCFS, 0, 99).makespan, 44);
  // Test 6: fairness-aware limit. Row 1 open; queue = X(row 2, oldest), A, A (row 1).
  //   FR-FCFS: A hit 0-11, A hit 11-22, X conflict 22-55  -> makespan 55, 2 hits, X waited 22
  //   FAIR T=10: A hit 0-11; at 11 X has waited 11 >= T so X goes: conflict 11-44;
  //              last A is now a conflict 44-77          -> makespan 77, 1 hit,  X waited 11
  //   FIFO:    X conflict 0-33, A conflict 33-66, A hit 66-77 -> makespan 77, 1 hit, X waited 0
  printf("Test 6: waiting-time limit (T = 10) on one bank\n");
  vector<pair<int, long>> t6 = {{0, X}, {0, A}, {0, A}};
  Probe fr = probe(t6, 1, FRFCFS, 0, 1), fa = probe(t6, 1, FAIR, 10, 1), fi = probe(t6, 1, FIFO, 0, 1);
  check("FR-FCFS makespan", fr.makespan, 55);  check("FR-FCFS row hits", fr.hits, 2);
  check("FR-FCFS wait of the oldest request", fr.wait0, 22);
  check("FAIR makespan", fa.makespan, 77);     check("FAIR row hits", fa.hits, 1);
  check("FAIR wait of the oldest request", fa.wait0, 11);
  check("FIFO makespan", fi.makespan, 77);     check("FIFO wait of the oldest request", fi.wait0, 0);
  // Test 7: with a huge T the fairness-aware policy must behave exactly like FR-FCFS.
  printf("Test 7: T = infinity equals FR-FCFS\n");
  check("makespan", probe(t6, 1, FAIR, 1000000, 1).makespan, 55);
  printf(fails ? "SELFTEST FAIL (%d)\n" : "SELFTEST PASS (all checks)\n", fails);
  return fails ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "--selftest")) return selftest();
  if (argc < 3) { fprintf(stderr, "usage: dramsim trace.csv fifo|frfcfs|fair [banks] [queue] [T]\n"); return 1; }
  int banks = argc > 3 ? atoi(argv[3]) : 4;
  int qsize = argc > 4 ? atoi(argv[4]) : 32;
  long T = argc > 5 ? atol(argv[5]) : 500;
  map<int, int> window;
  vector<Req> reqs = load(argv[1], window);
  Result r = simulate(reqs, window, parse_policy(argv[2]), banks, qsize, T);
  printf("n=%ld hit_rate=%.4f avg_wait=%.2f avg_lat=%.2f throughput=%.5f makespan=%ld\n", r.all.n,
         (double)r.all.hits / r.all.n, (double)r.all.wait_sum / r.all.n,
         (double)r.all.lat_sum / r.all.n, (double)r.all.n / r.all.last_done, r.all.last_done);
  for (auto &kv : r.per)
    printf("pid=%d n=%ld hit_rate=%.4f avg_wait=%.2f avg_lat=%.2f time=%ld\n", kv.first, kv.second.n,
           (double)kv.second.hits / kv.second.n, (double)kv.second.wait_sum / kv.second.n,
           (double)kv.second.lat_sum / kv.second.n, kv.second.last_done);
  return 0;
}
