# memlayout-bench

[![CI](https://github.com/rtgammaTest/memlayout-bench/actions/workflows/ci.yml/badge.svg)](https://github.com/rtgammaTest/memlayout-bench/actions/workflows/ci.yml)

**Where options-analytics code actually loses time: the memory hierarchy, measured.**

Most "slow Python/C++" in market-data systems isn't slow arithmetic. It's the CPU waiting on memory because the data layout makes it fetch 64-byte cache lines that are mostly bytes it doesn't need. This repo measures that directly on an options-chain workload.

## Headline

On a 104-byte-per-contract options chain (quotes + Greeks + position, 8M contracts):

| Query | Array-of-structs | Struct-of-arrays | Speedup |
|---|---|---|---|
| Net delta (reads 2 of 16 fields) | 7.26 ns/row | 1.13 ns/row | **6.4x** |
| Wide risk row (reads 13 of 16 fields) | 11.5 ns/row | 7.1 ns/row | 1.6x |
| Same net-delta query in NumPy (structured array vs separate arrays) | 9.9 ns/row | 2.3 ns/row | 4.3x |

Same arithmetic, same data, same compiler. The only variable is layout. The speedup scales with the fraction of each cache line the query throws away: the narrow query uses 12 of every 104 bytes it drags in, the wide query uses most of them.

*Reference run: Intel Xeon @ 2.1 GHz cloud VM, 1 vCPU, g++ 13 `-O3 -march=native`. Rerun on your own machine with `python run.py`; `results/RESULTS.md` is regenerated.*

## The three experiments

**1. Latency staircase (`./bench latency`).** A dependent pointer chase through a random single cycle of cache-line-sized nodes defeats the prefetcher, so each load pays the full latency of whichever level holds the working set:

![latency](results/latency.png)

| Level | Working set | ns per load |
|---|---|---|
| L1 | ≤ 32 KiB | ~2 |
| L2 | 64 KiB – 1 MiB | ~6–7 |
| L3 | 2 – 32 MiB | ~22–47 |
| DRAM | ≥ 64 MiB | ~120–145 |

A full SPX chain snapshot (~28.5K contracts at 40–104 bytes each) is 1–3 MB — L2/L3 territory. A session of quotes (~15 GB) is two orders of magnitude past any cache. That gap decides most design choices in a capture-and-analytics pipeline: what stays resident, what gets streamed, and what layout it's streamed in.

**2. You pay per cache line, not per element (`./bench stride`).** Summing every k-th int64 of a 512 MiB array: through stride 8 (one element per 64-byte line) the cost per *line* stays roughly flat, because the line is the unit of transfer. Beyond that, prefetch efficiency and TLB reach degrade and each line costs more.

**3. Layout (`./bench layout`, `bench_numpy.py`).** The AoS vs SoA comparison above. In NumPy, a structured array *is* an array-of-structs: `arr["delta"]` is a strided view, so the penalty shows up in Python code too, not just C++.

## Getting cache-miss counts (Linux)

The timings are the result; hardware counters are the explanation. With `perf` installed:

```bash
g++ -O3 -march=native -std=c++17 -o bench src/bench.cpp
perf stat -e cycles,instructions,cache-references,cache-misses,L1-dcache-load-misses,LLC-load-misses ./bench layout
```

Expect the AoS narrow query to show roughly an order of magnitude more LLC misses per row than SoA. On macOS, use Instruments' *Counters* template.

## Run it

```bash
python run.py           # full suite, ~1 minute, ~2 GB RAM
python run.py --quick   # smaller sizes, used by CI
```

Requirements: g++ (C++17), Python 3.10+, NumPy, matplotlib (optional, for the plot).

## Why this repo exists

At Qualcomm I led verification of a memory controller block — the hardware on the far side of every one of these loads. Bandwidth, latency, arbitration and contention were the day job. Moving to trading infrastructure, the same model turned out to predict where software was slow better than any profiler summary did. This is that model, made measurable.

MIT licensed.
