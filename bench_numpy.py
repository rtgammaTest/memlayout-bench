"""The same layout effect, seen from Python.

A NumPy structured array is an array-of-structs: reading one field is a strided view,
so every cache line you pull in is mostly other fields. Separate arrays are struct-of-arrays.

    python bench_numpy.py [--rows N]
"""
import argparse
import time

import numpy as np

ROW = np.dtype([
    ("ts", "<i8"), ("cid", "<u4"), ("flags", "<u4"),
    ("bid", "<f8"), ("ask", "<f8"), ("last", "<f8"), ("iv", "<f8"), ("delta", "<f8"),
    ("gamma", "<f8"), ("vega", "<f8"), ("theta", "<f8"), ("rho", "<f8"),
    ("bid_sz", "<i4"), ("ask_sz", "<i4"), ("oi", "<i4"), ("pos", "<i4"),
])
assert ROW.itemsize == 104  # same row as the C++ benchmark


def best(f, reps=5):
    t = []
    for _ in range(reps):
        t0 = time.perf_counter()
        f()
        t.append(time.perf_counter() - t0)
    return min(t)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rows", type=int, default=8_000_000)
    n = ap.parse_args().rows
    rng = np.random.default_rng(0)
    aos = np.zeros(n, dtype=ROW)
    aos["delta"] = rng.uniform(-1, 1, n)
    aos["pos"] = rng.integers(-10, 11, n)
    delta = np.ascontiguousarray(aos["delta"])
    pos = np.ascontiguousarray(aos["pos"])

    # Identical arithmetic (float64 * int32, then sum); only the memory layout differs.
    t_aos = best(lambda: (aos["delta"] * aos["pos"]).sum())
    t_soa = best(lambda: (delta * pos).sum())
    t_copy = best(lambda: np.ascontiguousarray(aos["delta"]))

    print("experiment,variant,param,value,unit")
    print(f"numpy,structured_fields,{n},{t_aos * 1e9 / n:.3f},ns_per_row")
    print(f"numpy,contiguous_arrays,{n},{t_soa * 1e9 / n:.3f},ns_per_row")
    print(f"numpy,speedup,{n},{t_aos / t_soa:.2f},x")
    print(f"numpy,one_off_field_copy,{n},{t_copy * 1e9 / n:.3f},ns_per_row")


if __name__ == "__main__":
    main()
