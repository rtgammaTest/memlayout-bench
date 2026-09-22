"""Build, run every experiment, and write results/RESULTS.md + results/latency.png.

    python run.py            # full run (~1 min, needs ~2 GB RAM)
    python run.py --quick    # smoke test (CI)

Run on an idle machine. For cache-miss counts on Linux, see the README.
"""
import csv
import io
import os
import platform
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "results")
quick = "--quick" in sys.argv


def sh(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, cwd=HERE).stdout


def cpu_name():
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def rows(text):
    return list(csv.DictReader(io.StringIO(text)))


def main():
    os.makedirs(OUT, exist_ok=True)
    sh(["g++", "-O3", "-march=native", "-std=c++17", "-o", "bench", "src/bench.cpp"])
    args = ["./bench"] + (["--quick"] if quick else [])
    cpp = sh(args)
    npy = sh([sys.executable, "bench_numpy.py"] + (["--rows", "1000000"] if quick else []))
    with open(os.path.join(OUT, "results.csv"), "w") as f:
        f.write(cpp)
        f.write("".join(npy.splitlines(keepends=True)[1:]))
    r = rows(cpp) + rows(npy)

    def get(exp, var, unit, param=None):
        for x in r:
            if x["experiment"] == exp and x["variant"] == var and x["unit"] == unit and (param is None or x["param"] == param):
                return float(x["value"])
        return float("nan")

    lat = [(int(x["param"]), float(x["value"])) for x in r if x["experiment"] == "latency"]
    strides = [(int(x["param"]), float(x["value"])) for x in r if x["experiment"] == "stride" and x["unit"] == "ns_per_cacheline"]

    md = [f"# Results\n", f"Machine: `{cpu_name()}` · {os.cpu_count()} logical CPUs · {platform.system()}\n"]
    md += ["## Options-chain layout (AoS 104-byte rows vs SoA columns)\n",
           "| Query | AoS ns/row | SoA ns/row | SoA speedup |", "|---|---|---|---|",
           f"| Net delta (2 of 16 fields) | {get('layout','narrow_aos','ns_per_row'):.2f} | {get('layout','narrow_soa','ns_per_row'):.2f} | **{get('layout','narrow_speedup','x'):.1f}x** |",
           f"| Wide risk row (13 of 16 fields) | {get('layout','wide_aos','ns_per_row'):.2f} | {get('layout','wide_soa','ns_per_row'):.2f} | {get('layout','wide_speedup','x'):.1f}x |",
           f"| NumPy: structured array vs contiguous arrays | {get('numpy','structured_fields','ns_per_row'):.2f} | {get('numpy','contiguous_arrays','ns_per_row'):.2f} | {get('numpy','speedup','x'):.1f}x |\n"]
    md += ["## Dependent-load latency by working-set size\n", "| Working set | ns per load |", "|---|---|"]
    md += [f"| {k // 1024} MiB | {v:.1f} |" if k >= 1024 else f"| {k} KiB | {v:.1f} |" for k, v in lat]
    md += ["", "![latency](latency.png)\n", "## Cost per cache line by stride (int64 elements)\n",
           "| Stride | ns per cache line |", "|---|---|"]
    md += [f"| {s} | {v:.2f} |" for s, v in strides]
    with open(os.path.join(OUT, "RESULTS.md"), "w") as f:
        f.write("\n".join(md) + "\n")

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(figsize=(7, 4))
        ax.plot([k * 1024 for k, _ in lat], [v for _, v in lat], marker="o")
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_xlabel("working set (bytes)")
        ax.set_ylabel("ns per dependent load")
        ax.set_title("Pointer-chase latency: the memory hierarchy, measured")
        ax.grid(True, which="both", alpha=0.3)
        fig.tight_layout()
        fig.savefig(os.path.join(OUT, "latency.png"), dpi=130)
    except ImportError:
        print("matplotlib not installed; skipped latency.png")
    print(open(os.path.join(OUT, "RESULTS.md")).read())


if __name__ == "__main__":
    main()
