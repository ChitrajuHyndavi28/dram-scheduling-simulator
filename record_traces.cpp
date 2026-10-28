// Address-recording versions of small programs.
// Each program really runs its computation; every load/store goes through the Tracer, which
//   1. records the real address (offset inside one 8 KB-aligned arena, so row/bank bits are preserved),
//   2. filters it through a 256 KB 8-way LRU write-allocate cache,
//   3. emits only cache MISSES as DRAM requests, time-stamped from a simple CPU cost model.
// Usage: record_traces <stream|random|chunks|matmul|linkedlist> <out.rec> [max_requests=20000]
// Output lines: arena_offset,R|W,arrival_mem_cycle   (arrival is cumulative; the simulator turns it into think times)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace std;

static const int LINE = 64, SETS = 512, WAYS = 8;     // 256 KB cache
static const int CPU_CYCLES_PER_ACCESS = 2;           // load/store plus the add or multiply that uses it
static const int CPU_PER_MEM_CYCLE = 4;               // ~3.2 GHz core vs 800 MHz DDR3-1600 clock

struct Tracer {
  char *arena;
  uint64_t cpu = 0;
  size_t limit;
  bool done = false;
  vector<uint64_t> tag = vector<uint64_t>(SETS * WAYS, ~0ULL), stamp = vector<uint64_t>(SETS * WAYS, 0);
  uint64_t clock = 0;
  FILE *out;

  void access(const void *p, bool store) {
    if (done) return;
    cpu += CPU_CYCLES_PER_ACCESS;
    uint64_t off = (uint64_t)((const char *)p - arena), line = off / LINE;
    int set = (int)(line % SETS);
    uint64_t *t = &tag[set * WAYS], *s = &stamp[set * WAYS];
    int victim = 0;
    for (int w = 0; w < WAYS; w++) {
      if (t[w] == line) { s[w] = ++clock; return; }  // cache hit: no DRAM request
      if (s[w] < s[victim]) victim = w;
    }
    t[victim] = line; s[victim] = ++clock;            // miss: allocate, send to DRAM
    fprintf(out, "%llu,%c,%llu\n", (unsigned long long)(line * LINE), store ? 'W' : 'R',
            (unsigned long long)(cpu / CPU_PER_MEM_CYCLE));
    if (--limit == 0) done = true;
  }
};
static Tracer T;
template <class X> static inline X ld(const X *p) { T.access(p, false); return *p; }
template <class X> static inline void st(X *p, X v) { T.access(p, true); *p = v; }

static uint64_t rng_state = 88172645463325252ULL;
static inline uint64_t rnd() { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17; return rng_state; }

// array sum: sequential walk over a 16 MB array of longs
static void prog_stream() {
  long *a = (long *)T.arena; size_t n = (16 << 20) / sizeof(long); long sum = 0;
  while (!T.done) for (size_t i = 0; i < n && !T.done; i++) sum += ld(&a[i]);
  if (sum == 42) puts("");
}
// random access: sum of 64 MB array at pseudo-random indices
static void prog_random() {
  long *a = (long *)T.arena; size_t n = (64 << 20) / sizeof(long); long sum = 0;
  while (!T.done) sum += ld(&a[rnd() % n]);
  if (sum == 42) puts("");
}
// chunked scan: sum 8..64 consecutive cache lines at a random place, then jump (fragmented-file scan)
static void prog_chunks() {
  long *a = (long *)T.arena; size_t n = (64 << 20) / sizeof(long); long sum = 0;
  while (!T.done) {
    size_t start = (rnd() % (n - 4096)) & ~7UL, len = (8 + rnd() % 57) * 8;
    for (size_t i = 0; i < len && !T.done; i++) sum += ld(&a[start + i]);
  }
  if (sum == 42) puts("");
}
// naive matrix multiply C = A*B, 192x192 doubles (A, B, C = 295 KB each)
static void prog_matmul() {
  const int N = 192; double *A = (double *)T.arena, *B = A + N * N, *C = B + N * N;
  while (!T.done)
    for (int i = 0; i < N && !T.done; i++)
      for (int j = 0; j < N && !T.done; j++) {
        double s = 0;
        for (int k = 0; k < N && !T.done; k++) s += ld(&A[i * N + k]) * ld(&B[k * N + j]);
        st(&C[i * N + j], s);
      }
}
// linked list: 2^18 nodes of 64 bytes (16 MB) linked in a random cycle, traversed through the real pointers
struct Node { Node *next; long pad[7]; };
static void prog_linkedlist() {
  size_t n = 1 << 18; Node *nodes = (Node *)T.arena;
  vector<size_t> order(n); for (size_t i = 0; i < n; i++) order[i] = i;
  for (size_t i = n - 1; i > 0; i--) swap(order[i], order[rnd() % (i + 1)]);
  for (size_t i = 0; i < n; i++) nodes[order[i]].next = &nodes[order[(i + 1) % n]];
  Node *p = &nodes[order[0]];
  while (!T.done) p = ld(&p->next);
  if (p == nullptr) puts("");
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: record_traces stream|random|chunks|matmul|linkedlist out.rec [max]\n"); return 1; }
  T.limit = argc > 3 ? strtoul(argv[3], 0, 10) : 20000;
  size_t bytes = 96 << 20;
  T.arena = (char *)aligned_alloc(8192, bytes);   // 8 KB aligned so row-buffer boundaries are preserved
  memset(T.arena, 1, bytes);
  T.out = fopen(argv[2], "w");
  string p = argv[1];
  if (p == "stream") prog_stream(); else if (p == "random") prog_random(); else if (p == "chunks") prog_chunks();
  else if (p == "matmul") prog_matmul(); else if (p == "linkedlist") prog_linkedlist();
  else { fprintf(stderr, "unknown program %s\n", p.c_str()); return 1; }
  fclose(T.out);
  return 0;
}
