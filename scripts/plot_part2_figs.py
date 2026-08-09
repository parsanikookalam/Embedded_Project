#!/usr/bin/env python3
"""Generate Part 2 report charts from report/part 2/fig/*.csv|txt."""
from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

FIG = Path(__file__).resolve().parents[1] / "report" / "part 2" / "fig"


def read_temp(path: Path) -> tuple[list[float], list[float]]:
    t, y = [], []
    if not path.is_file():
        return t, y
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if not row.get("t_sec") or row.get("cpu_temp") in (None, ""):
                continue
            t.append(float(row["t_sec"]))
            y.append(float(row["cpu_temp"]))
    return t, y


def plot_temp() -> dict[str, float | None]:
    series = {
        "Idle (camera OFF)": FIG / "temp_idle.csv",
        "Stream only (no person)": FIG / "temp_stream.csv",
        "Stream + detection": FIG / "temp_detect.csv",
    }
    colors = ["#1f77b4", "#2ca02c", "#d62728"]
    fig, ax = plt.subplots(figsize=(9, 4.8), dpi=140)
    maxima: dict[str, float | None] = {}
    plotted = 0
    for (label, path), color in zip(series.items(), colors):
        t, y = read_temp(path)
        maxima[label] = max(y) if y else None
        if not t:
            print(f"[warn] empty / missing: {path.name}")
            continue
        ax.plot(t, y, marker="o", markersize=4, linewidth=1.8, label=label, color=color)
        plotted += 1
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("CPU temperature (°C)")
    ax.set_title("Figure 2-1a — CPU temperature vs time (three states)")
    ax.grid(True, alpha=0.35)
    if plotted:
        ax.legend(loc="best")
    out = FIG / "01_temp_vs_time.png"
    fig.tight_layout()
    fig.savefig(out)
    plt.close(fig)
    print(f"[ok] {out}" if plotted else f"[skip] {out} (no series)")
    return maxima


def plot_mem() -> dict[str, float | None]:
    path = FIG / "mem_web_server.csv"
    t, rss = [], []
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if not row.get("t_sec"):
                continue
            t.append(float(row["t_sec"]))
            rss.append(float(row["rss_kb"]))
    fig, ax = plt.subplots(figsize=(9, 4.8), dpi=140)
    ax.plot(t, rss, color="#9467bd", linewidth=2.0, marker="o", markersize=3)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("web_server RSS (KB)")
    ax.set_title("Figure 2-2 — C web_server memory (RSS) vs time")
    ax.grid(True, alpha=0.35)
    if rss:
        lo, hi = min(rss), max(rss)
        pad = max(32.0, (hi - lo) * 0.15 + 16)
        ax.set_ylim(lo - pad, hi + pad)
    out = FIG / "03_mem_vs_time.png"
    fig.tight_layout()
    fig.savefig(out)
    plt.close(fig)
    print(f"[ok] {out}")
    return {
        "initial": rss[0] if rss else None,
        "final": rss[-1] if rss else None,
        "peak": max(rss) if rss else None,
        "delta": (rss[-1] - rss[0]) if rss else None,
    }


def plot_latency() -> dict[str, float | None]:
    path = FIG / "load_latencies.txt"
    times_ms: list[float] = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 2:
                try:
                    times_ms.append(float(parts[1]) * 1000.0)
                except ValueError:
                    continue
    if not times_ms:
        print(f"[warn] no latencies in {path}")
        return {}

    idx = list(range(1, len(times_ms) + 1))
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.2), dpi=140)

    axes[0].bar(idx, times_ms, color="#17becf", width=0.85)
    axes[0].set_xlabel("Request #")
    axes[0].set_ylabel("Latency (ms)")
    axes[0].set_title("Per-request latency (50× /api/v1/telemetry)")
    axes[0].grid(True, axis="y", alpha=0.35)

    axes[1].plot(idx, times_ms, color="#e377c2", marker="o", markersize=3, linewidth=1.4)
    axes[1].axhline(sum(times_ms) / len(times_ms), color="#333", linestyle="--", linewidth=1.2, label="mean")
    axes[1].set_xlabel("Request #")
    axes[1].set_ylabel("Latency (ms)")
    axes[1].set_title("Latency trend")
    axes[1].legend(loc="best")
    axes[1].grid(True, alpha=0.35)

    out = FIG / "04_telemetry_latency.png"
    fig.suptitle("Figure 2-3a — Telemetry request latency under burst", y=1.02)
    fig.tight_layout()
    fig.savefig(out, bbox_inches="tight")
    plt.close(fig)
    print(f"[ok] {out}")
    return {
        "n": float(len(times_ms)),
        "min_ms": min(times_ms),
        "max_ms": max(times_ms),
        "mean_ms": sum(times_ms) / len(times_ms),
    }


def main() -> int:
    FIG.mkdir(parents=True, exist_ok=True)
    print(f"FIG dir: {FIG}")
    tmax = plot_temp()
    m = plot_mem()
    lat = plot_latency()

    before = json.loads((FIG / "load_before.json").read_text(encoding="utf-8"))
    after = json.loads((FIG / "load_after.json").read_text(encoding="utf-8"))

    print("\n=== fill helpers for report.md ===")
    for k, v in tmax.items():
        print(f"temp max {k}: {v if v is not None else 'MISSING DATA'}")
    print(
        f"mem RSS KB initial/final/peak/delta: "
        f"{m.get('initial')}/{m.get('final')}/{m.get('peak')}/{m.get('delta')}"
    )
    print(
        f"latency ms min/mean/max (n={int(lat.get('n', 0))}): "
        f"{lat.get('min_ms'):.2f}/{lat.get('mean_ms'):.2f}/{lat.get('max_ms'):.2f}"
        if lat
        else "latency: MISSING"
    )
    print(
        f"load Δtemp={after['cpu_temp']-before['cpu_temp']:.2f} "
        f"Δcpu%={after['cpu_usage_percent']-before['cpu_usage_percent']:.2f} "
        f"Δmem%={after['mem_used_percent']-before['mem_used_percent']:.2f}"
    )
    if tmax.get("Stream only (no person)") is None or tmax.get("Stream + detection") is None:
        print(
            "\n[NEED] Fill temp_stream.csv and temp_detect.csv (same format as temp_idle.csv), "
            "then re-run this script for a complete 2-1 chart."
        )
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
