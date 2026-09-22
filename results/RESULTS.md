# Results

Machine: `Intel(R) Xeon(R) Processor @ 2.10GHz` · 1 logical CPUs · Linux

## Options-chain layout (AoS 104-byte rows vs SoA columns)

| Query | AoS ns/row | SoA ns/row | SoA speedup |
|---|---|---|---|
| Net delta (2 of 16 fields) | 6.82 | 0.67 | **10.2x** |
| Wide risk row (13 of 16 fields) | 11.52 | 7.10 | 1.6x |
| NumPy: structured array vs contiguous arrays | 9.90 | 2.30 | 4.3x |

## Dependent-load latency by working-set size

| Working set | ns per load |
|---|---|
| 4 KiB | 2.1 |
| 8 KiB | 2.1 |
| 16 KiB | 2.1 |
| 32 KiB | 2.1 |
| 64 KiB | 6.5 |
| 128 KiB | 6.2 |
| 256 KiB | 6.0 |
| 512 KiB | 6.5 |
| 1 MiB | 7.1 |
| 2 MiB | 22.3 |
| 4 MiB | 34.2 |
| 8 MiB | 36.1 |
| 16 MiB | 42.4 |
| 32 MiB | 46.7 |
| 64 MiB | 123.0 |
| 128 MiB | 129.9 |
| 256 MiB | 132.9 |
| 512 MiB | 144.2 |

![latency](latency.png)

## Cost per cache line by stride (int64 elements)

| Stride | ns per cache line |
|---|---|
| 1 | 5.25 |
| 2 | 4.37 |
| 4 | 4.25 |
| 8 | 3.81 |
| 16 | 7.39 |
| 32 | 8.98 |
| 64 | 10.05 |
