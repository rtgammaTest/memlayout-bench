// memlayout-bench: how data layout meets the memory hierarchy on an options-chain workload.
//
// Experiments (all print CSV rows: experiment,variant,param,value,unit):
//   latency  - dependent pointer chase over working sets 4 KiB .. 512 MiB.
//              Exposes the L1 / L2 / L3 / DRAM latency plateaus of this machine.
//   stride   - sum every k-th int64 of a large array. Cost is paid per cache line,
//              not per element, until the stride exceeds one line.
//   layout   - array-of-structs vs struct-of-arrays for an options chain, on a
//              narrow query (net delta: 2 of 16 fields) and a wide one (most fields).
//
// Build: g++ -O2 -march=native -std=c++17 -o bench src/bench.cpp
// Usage: ./bench [latency|stride|layout|all] [--quick]

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

template <class T>
inline void do_not_optimize(T const& v) { asm volatile("" : : "r,m"(v) : "memory"); }

static double seconds_since(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

static bool g_quick = false;

// ---------------------------------------------------------------- latency
// One node per cache line; Sattolo's algorithm gives a single random cycle through
// every node, so the hardware prefetcher cannot predict the next address.
struct alignas(64) Node { uint64_t next; char pad[56]; };

static void bench_latency() {
    std::mt19937_64 rng(42);
    size_t max_kib = g_quick ? 64 * 1024 : 512 * 1024;
    for (size_t kib = 4; kib <= max_kib; kib *= 2) {
        size_t n = kib * 1024 / sizeof(Node);
        std::vector<Node> nodes(n);
        std::vector<uint64_t> order(n);
        std::iota(order.begin(), order.end(), 0);
        for (size_t i = n - 1; i > 0; --i) {           // Sattolo: single cycle
            std::uniform_int_distribution<size_t> d(0, i - 1);
            std::swap(order[i], order[d(rng)]);
        }
        for (size_t i = 0; i < n; ++i) nodes[order[i]].next = order[(i + 1) % n];

        size_t steps = std::max<size_t>(n * 4, g_quick ? 2'000'000 : 20'000'000);
        uint64_t p = 0;
        for (size_t i = 0; i < n; ++i) p = nodes[p].next;  // warm
        auto t0 = Clock::now();
        for (size_t i = 0; i < steps; ++i) p = nodes[p].next;
        double ns = seconds_since(t0) * 1e9 / steps;
        do_not_optimize(p);
        std::printf("latency,chase,%zu,%.3f,ns_per_load\n", kib, ns);
        std::fflush(stdout);
    }
}

// ---------------------------------------------------------------- stride
template <size_t S>
static void stride_case(const std::vector<int64_t>& a) {
    // Stride is a compile-time constant so the stride-1 case vectorizes like real code would.
    const size_t n = a.size();
    const int64_t* p = a.data();
    double best = 1e30;
    int64_t s = 0;
    for (int rep = 0; rep < 3; ++rep) {
        auto t0 = Clock::now();
        s = 0;
        for (size_t i = 0; i < n; i += S) s += p[i];
        best = std::min(best, seconds_since(t0));
    }
    do_not_optimize(s);
    size_t touched = n / S;
    size_t lines = std::min(touched, n / 8);  // 8 x int64 per 64-byte line
    std::printf("stride,sum,%zu,%.3f,ns_per_element\n", S, best * 1e9 / touched);
    std::printf("stride,sum,%zu,%.3f,ns_per_cacheline\n", S, best * 1e9 / lines);
    std::fflush(stdout);
}

static void bench_stride() {
    size_t n = (g_quick ? 16 : 64) * 1024 * 1024;  // int64s: 128 MiB or 512 MiB
    std::vector<int64_t> a(n, 1);
    stride_case<1>(a); stride_case<2>(a); stride_case<4>(a); stride_case<8>(a);
    stride_case<16>(a); stride_case<32>(a); stride_case<64>(a);
}

// ---------------------------------------------------------------- layout
// 16 fields, 104 bytes: roughly what a per-contract row looks like once quotes,
// Greeks and position are joined.
struct OptionRow {
    int64_t ts;
    uint32_t cid;
    uint32_t flags;
    double bid, ask, last, iv, delta, gamma, vega, theta, rho;
    int32_t bid_sz, ask_sz, oi, pos;
};
static_assert(sizeof(OptionRow) == 104, "row layout changed");

struct OptionColumns {
    std::vector<int64_t> ts;
    std::vector<uint32_t> cid, flags;
    std::vector<double> bid, ask, last, iv, delta, gamma, vega, theta, rho;
    std::vector<int32_t> bid_sz, ask_sz, oi, pos;
    explicit OptionColumns(size_t n)
        : ts(n), cid(n), flags(n), bid(n), ask(n), last(n), iv(n), delta(n), gamma(n),
          vega(n), theta(n), rho(n), bid_sz(n), ask_sz(n), oi(n), pos(n) {}
};

template <class F>
static double best_of(int reps, F&& f) {
    double best = 1e30;
    for (int r = 0; r < reps; ++r) {
        auto t0 = Clock::now();
        f();
        best = std::min(best, seconds_since(t0));
    }
    return best;
}

static void bench_layout() {
    size_t n = g_quick ? 1'000'000 : 8'000'000;  // AoS = 104 B/row -> 104 MB or 832 MB
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<OptionRow> rows(n);
    OptionColumns cols(n);
    for (size_t i = 0; i < n; ++i) {
        OptionRow r{};
        r.ts = (int64_t)i; r.cid = (uint32_t)i;
        r.bid = 1 + u(rng); r.ask = r.bid + 0.1; r.last = r.bid + 0.05;
        r.iv = 0.2 + 0.1 * u(rng); r.delta = u(rng) * 2 - 1; r.gamma = u(rng) * 0.01;
        r.vega = u(rng); r.theta = -u(rng); r.rho = u(rng) * 0.1;
        r.bid_sz = 10; r.ask_sz = 12; r.oi = 1000; r.pos = (int32_t)(rng() % 21) - 10;
        rows[i] = r;
        cols.ts[i] = r.ts; cols.cid[i] = r.cid; cols.flags[i] = r.flags;
        cols.bid[i] = r.bid; cols.ask[i] = r.ask; cols.last[i] = r.last; cols.iv[i] = r.iv;
        cols.delta[i] = r.delta; cols.gamma[i] = r.gamma; cols.vega[i] = r.vega;
        cols.theta[i] = r.theta; cols.rho[i] = r.rho; cols.bid_sz[i] = r.bid_sz;
        cols.ask_sz[i] = r.ask_sz; cols.oi[i] = r.oi; cols.pos[i] = r.pos;
    }
    const int reps = 5;

    // Narrow query: net delta = sum(delta * pos). Needs 12 of 104 bytes per row.
    double sa = 0, sb = 0;
    double t_aos = best_of(reps, [&] {
        double s = 0;
        for (size_t i = 0; i < n; ++i) s += rows[i].delta * rows[i].pos;
        sa = s;
    });
    double t_soa = best_of(reps, [&] {
        double s = 0;
        const double* d = cols.delta.data();
        const int32_t* p = cols.pos.data();
        for (size_t i = 0; i < n; ++i) s += d[i] * p[i];
        sb = s;
    });
    do_not_optimize(sa); do_not_optimize(sb);
    if (sa != sb) std::fprintf(stderr, "net delta mismatch %f vs %f\n", sa, sb);
    // Bytes the CPU actually has to pull from memory: whole lines for AoS, 12 B/row for SoA.
    double aos_bytes = double(n) * sizeof(OptionRow);
    double soa_bytes = double(n) * (sizeof(double) + sizeof(int32_t));
    std::printf("layout,narrow_aos,%zu,%.3f,ns_per_row\n", n, t_aos * 1e9 / n);
    std::printf("layout,narrow_soa,%zu,%.3f,ns_per_row\n", n, t_soa * 1e9 / n);
    std::printf("layout,narrow_aos,%zu,%.2f,GB_per_s_moved\n", n, aos_bytes / t_aos / 1e9);
    std::printf("layout,narrow_soa,%zu,%.2f,GB_per_s_moved\n", n, soa_bytes / t_soa / 1e9);
    std::printf("layout,narrow_speedup,%zu,%.2f,x\n", n, t_aos / t_soa);
    std::fflush(stdout);

    // Wide query: a risk/mark row touching 13 of 16 fields.
    auto wide_row = [](double bid, double ask, double last, double iv, double delta, double gamma,
                       double vega, double theta, double rho, int32_t bsz, int32_t asz, int32_t oi,
                       int32_t pos) {
        double mid = 0.5 * (bid + ask);
        return pos * (mid + delta + 0.5 * gamma + vega * iv + theta + rho) + (bsz - asz) * 1e-3 +
               oi * 1e-6 + last * 1e-3;
    };
    double wa = 0, wb = 0;
    double tw_aos = best_of(reps, [&] {
        double s = 0;
        for (size_t i = 0; i < n; ++i) {
            const OptionRow& r = rows[i];
            s += wide_row(r.bid, r.ask, r.last, r.iv, r.delta, r.gamma, r.vega, r.theta, r.rho,
                          r.bid_sz, r.ask_sz, r.oi, r.pos);
        }
        wa = s;
    });
    double tw_soa = best_of(reps, [&] {
        double s = 0;
        for (size_t i = 0; i < n; ++i)
            s += wide_row(cols.bid[i], cols.ask[i], cols.last[i], cols.iv[i], cols.delta[i],
                          cols.gamma[i], cols.vega[i], cols.theta[i], cols.rho[i], cols.bid_sz[i],
                          cols.ask_sz[i], cols.oi[i], cols.pos[i]);
        wb = s;
    });
    do_not_optimize(wa); do_not_optimize(wb);
    std::printf("layout,wide_aos,%zu,%.3f,ns_per_row\n", n, tw_aos * 1e9 / n);
    std::printf("layout,wide_soa,%zu,%.3f,ns_per_row\n", n, tw_soa * 1e9 / n);
    std::printf("layout,wide_speedup,%zu,%.2f,x\n", n, tw_aos / tw_soa);
    std::fflush(stdout);
}

int main(int argc, char** argv) {
    std::string which = "all";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quick") == 0) g_quick = true;
        else which = argv[i];
    }
    std::printf("experiment,variant,param,value,unit\n");
    if (which == "latency" || which == "all") bench_latency();
    if (which == "stride" || which == "all") bench_stride();
    if (which == "layout" || which == "all") bench_layout();
    return 0;
}
