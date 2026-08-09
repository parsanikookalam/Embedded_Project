#!/usr/bin/env python3
"""Stdlib-only Part 2 charts → SVG (+ optional PNG via PIL). No matplotlib required."""
from __future__ import annotations

import csv
import json
import struct
import zlib
from pathlib import Path

FIG = Path(__file__).resolve().parents[1] / "report" / "part 2" / "fig"
W, H = 900, 480
PAD_L, PAD_R, PAD_T, PAD_B = 70, 30, 50, 55


def read_xy(path: Path, xkey: str, ykey: str) -> list[tuple[float, float]]:
    pts: list[tuple[float, float]] = []
    if not path.is_file():
        return pts
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if not row.get(xkey) or row.get(ykey) in (None, ""):
                continue
            pts.append((float(row[xkey]), float(row[ykey])))
    return pts


def scale(pts, x0, x1, y0, y1, left, top, width, height):
    out = []
    dx = (x1 - x0) or 1.0
    dy = (y1 - y0) or 1.0
    for x, y in pts:
        px = left + (x - x0) / dx * width
        py = top + height - (y - y0) / dy * height
        out.append((px, py))
    return out


def polyline(pts, color, width=2.2):
    if len(pts) < 2:
        return ""
    d = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
    return f'<polyline fill="none" stroke="{color}" stroke-width="{width}" points="{d}"/>'


def markers(pts, color):
    return "".join(
        f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.2" fill="{color}"/>' for x, y in pts
    )


def axes_and_grid(x0, x1, y0, y1, xlabel, ylabel, title, nx=6, ny=6):
    left, top = PAD_L, PAD_T
    width, height = W - PAD_L - PAD_R, H - PAD_T - PAD_B
    parts = [
        f'<rect x="0" y="0" width="{W}" height="{H}" fill="#ffffff"/>',
        f'<text x="{W/2:.0f}" y="28" text-anchor="middle" font-family="DejaVu Sans, Arial" font-size="16" font-weight="700">{title}</text>',
        f'<rect x="{left}" y="{top}" width="{width}" height="{height}" fill="#fafafa" stroke="#333"/>',
    ]
    for i in range(nx + 1):
        x = left + width * i / nx
        xv = x0 + (x1 - x0) * i / nx
        parts.append(f'<line x1="{x:.1f}" y1="{top}" x2="{x:.1f}" y2="{top+height}" stroke="#ddd"/>')
        parts.append(
            f'<text x="{x:.1f}" y="{top+height+18}" text-anchor="middle" font-family="DejaVu Sans, Arial" font-size="11">{xv:.0f}</text>'
        )
    for i in range(ny + 1):
        y = top + height * i / ny
        yv = y1 - (y1 - y0) * i / ny
        parts.append(f'<line x1="{left}" y1="{y:.1f}" x2="{left+width}" y2="{y:.1f}" stroke="#ddd"/>')
        parts.append(
            f'<text x="{left-8}" y="{y+4:.1f}" text-anchor="end" font-family="DejaVu Sans, Arial" font-size="11">{yv:.1f}</text>'
        )
    parts.append(
        f'<text x="{W/2:.0f}" y="{H-12}" text-anchor="middle" font-family="DejaVu Sans, Arial" font-size="12">{xlabel}</text>'
    )
    parts.append(
        f'<text x="18" y="{H/2:.0f}" text-anchor="middle" transform="rotate(-90 18 {H/2:.0f})" font-family="DejaVu Sans, Arial" font-size="12">{ylabel}</text>'
    )
    return "\n".join(parts), left, top, width, height


def write_svg(path: Path, body: str):
    path.write_text(
        f'<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">\n'
        f"{body}\n</svg>\n",
        encoding="utf-8",
    )
    print(f"[ok] {path}")


def try_png_from_svg(svg_path: Path, png_path: Path) -> bool:
    """Best-effort PNG: PIL render is not available for SVG; use rsvg/convert if present."""
    import shutil
    import subprocess

    for cmd in (
        ["rsvg-convert", "-o", str(png_path), str(svg_path)],
        ["convert", str(svg_path), str(png_path)],
        ["inkscape", str(svg_path), "--export-filename=" + str(png_path)],
    ):
        if shutil.which(cmd[0]):
            try:
                subprocess.run(cmd, check=True, capture_output=True)
                print(f"[ok] {png_path} via {cmd[0]}")
                return True
            except Exception as e:
                print(f"[warn] {cmd[0]} failed: {e}")
    return False


def png_line_chart(
    png_path: Path,
    series: list[tuple[str, list[tuple[float, float]], tuple[int, int, int]]],
    title: str,
    xlabel: str,
    ylabel: str,
) -> bool:
    """Write a simple RGB PNG line chart with stdlib only (no deps)."""
    all_pts = [p for _, pts, _ in series for p in pts]
    if not all_pts:
        print(f"[skip] {png_path.name}: no points")
        return False

    xs = [p[0] for p in all_pts]
    ys = [p[1] for p in all_pts]
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    if y1 - y0 < 1e-6:
        y0 -= 1
        y1 += 1
    else:
        pad = (y1 - y0) * 0.08
        y0 -= pad
        y1 += pad

    # raster buffer
    img = [[(255, 255, 255) for _ in range(W)] for _ in range(H)]

    def setp(x, y, c):
        if 0 <= x < W and 0 <= y < H:
            img[y][x] = c

    def line(x0i, y0i, x1i, y1i, c):
        dx = abs(x1i - x0i)
        dy = -abs(y1i - y0i)
        sx = 1 if x0i < x1i else -1
        sy = 1 if y0i < y1i else -1
        err = dx + dy
        x, y = x0i, y0i
        while True:
            setp(x, y, c)
            if x == x1i and y == y1i:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x += sx
            if e2 <= dx:
                err += dx
                y += sy

    left, top = PAD_L, PAD_T
    width, height = W - PAD_L - PAD_R, H - PAD_T - PAD_B
    # frame + light grid
    for i in range(7):
        x = left + int(width * i / 6)
        line(x, top, x, top + height, (230, 230, 230))
        y = top + int(height * i / 6)
        line(left, y, left + width, y, (230, 230, 230))
    line(left, top, left + width, top, (40, 40, 40))
    line(left, top + height, left + width, top + height, (40, 40, 40))
    line(left, top, left, top + height, (40, 40, 40))
    line(left + width, top, left + width, top + height, (40, 40, 40))

    scaled_series = []
    for label, pts, color in series:
        sp = scale(pts, x0, x1, y0, y1, left, top, width, height)
        scaled_series.append((label, sp, color))
        for i in range(1, len(sp)):
            line(int(sp[i - 1][0]), int(sp[i - 1][1]), int(sp[i][0]), int(sp[i][1]), color)
        for px, py in sp:
            for dx in range(-2, 3):
                for dy in range(-2, 3):
                    if dx * dx + dy * dy <= 4:
                        setp(int(px) + dx, int(py) + dy, color)

    # crude 5x7 font
    FONT = {
        " ": [0, 0, 0, 0, 0],
        "-": [0, 0, 31, 0, 0],
        ".": [0, 0, 0, 0, 4],
        "0": [14, 17, 19, 21, 14],
        "1": [4, 12, 4, 4, 14],
        "2": [14, 1, 14, 16, 31],
        "3": [30, 1, 14, 1, 30],
        "4": [18, 18, 31, 2, 2],
        "5": [31, 16, 30, 1, 30],
        "6": [14, 16, 30, 17, 14],
        "7": [31, 1, 2, 4, 4],
        "8": [14, 17, 14, 17, 14],
        "9": [14, 17, 15, 1, 14],
        "A": [14, 17, 31, 17, 17],
        "B": [30, 17, 30, 17, 30],
        "C": [14, 17, 16, 17, 14],
        "D": [30, 17, 17, 17, 30],
        "E": [31, 16, 30, 16, 31],
        "F": [31, 16, 30, 16, 16],
        "G": [14, 16, 19, 17, 14],
        "H": [17, 17, 31, 17, 17],
        "I": [14, 4, 4, 4, 14],
        "K": [17, 18, 28, 18, 17],
        "L": [16, 16, 16, 16, 31],
        "M": [17, 27, 21, 17, 17],
        "N": [17, 25, 21, 19, 17],
        "O": [14, 17, 17, 17, 14],
        "P": [30, 17, 30, 16, 16],
        "R": [30, 17, 30, 18, 17],
        "S": [15, 16, 14, 1, 30],
        "T": [31, 4, 4, 4, 4],
        "U": [17, 17, 17, 17, 14],
        "V": [17, 17, 17, 10, 4],
        "W": [17, 17, 21, 27, 17],
        "X": [17, 10, 4, 10, 17],
        "Y": [17, 10, 4, 4, 4],
        "a": [0, 14, 1, 15, 15],
        "b": [16, 30, 17, 17, 30],
        "c": [0, 14, 16, 16, 14],
        "d": [1, 15, 17, 17, 15],
        "e": [0, 14, 31, 16, 14],
        "g": [0, 15, 17, 15, 1],
        "i": [4, 0, 12, 4, 14],
        "l": [12, 4, 4, 4, 14],
        "m": [0, 26, 21, 21, 21],
        "n": [0, 30, 17, 17, 17],
        "o": [0, 14, 17, 17, 14],
        "p": [0, 30, 17, 30, 16],
        "r": [0, 22, 24, 16, 16],
        "s": [0, 15, 14, 1, 30],
        "t": [8, 28, 8, 8, 6],
        "u": [0, 17, 17, 17, 15],
        "v": [0, 17, 17, 10, 4],
        "w": [0, 17, 21, 21, 10],
        "x": [0, 17, 10, 10, 17],
        "y": [0, 17, 17, 15, 1],
        "+": [0, 4, 14, 4, 0],
        "(": [4, 8, 8, 8, 4],
        ")": [8, 4, 4, 4, 8],
        "/": [1, 2, 4, 8, 16],
        ":": [0, 4, 0, 4, 0],
        ",": [0, 0, 0, 4, 8],
        "%": [25, 26, 4, 11, 19],
        "°": [14, 10, 14, 0, 0],
    }

    def draw_text(x, y, text, color=(20, 20, 20), scale=2):
        # Glyphs are 5 rows × 5 cols (each int is one row bitmask).
        cx = x
        for ch in text:
            glyph = FONT.get(ch) or FONT.get(ch.upper()) or FONT[" "]
            for row, bits in enumerate(glyph):
                for col in range(5):
                    if bits & (1 << (4 - col)):
                        for sx in range(scale):
                            for sy in range(scale):
                                setp(cx + col * scale + sx, y + row * scale + sy, color)
            cx += 6 * scale

    draw_text(max(8, W // 2 - len(title) * 6), 12, title, (10, 10, 10), 2)
    draw_text(max(8, W // 2 - len(xlabel) * 6), H - 28, xlabel, scale=2)
    draw_text(8, 40, ylabel[:22], scale=2)

    # tick labels
    for i in range(7):
        xv = x0 + (x1 - x0) * i / 6
        x = left + int(width * i / 6)
        draw_text(max(0, x - 12), top + height + 6, f"{xv:.0f}", scale=2)
        yv = y1 - (y1 - y0) * i / 6
        y = top + int(height * i / 6)
        draw_text(max(0, left - 56), max(0, y - 4), f"{yv:.1f}", scale=2)

    # legend
    lx, ly = left + 10, top + 10
    for label, _, color in scaled_series:
        for dx in range(18):
            for dy in range(3):
                setp(lx + dx, ly + 4 + dy, color)
        draw_text(lx + 24, ly, label[:26], color, scale=2)
        ly += 18

    # pack PNG
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes([c for rgb in row for c in rgb]) for row in img)
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    png_path.write_bytes(png)
    print(f"[ok] {png_path}")
    return True


def plot_temp():
    series_def = [
        ("Idle (camera OFF)", FIG / "temp_idle.csv", (31, 119, 180)),
        ("Stream only", FIG / "temp_stream.csv", (44, 160, 44)),
        ("Stream + detection", FIG / "temp_detect.csv", (214, 39, 40)),
    ]
    series = []
    for label, path, color in series_def:
        pts = read_xy(path, "t_sec", "cpu_temp")
        if pts:
            series.append((label, pts, color))
        else:
            print(f"[warn] empty: {path.name}")
    png_line_chart(
        FIG / "01_temp_vs_time.png",
        series,
        "Fig 2-1a CPU temp vs time",
        "Time (s)",
        "CPU temp (C)",
    )


def plot_mem():
    pts = read_xy(FIG / "mem_web_server.csv", "t_sec", "rss_kb")
    png_line_chart(
        FIG / "03_mem_vs_time.png",
        [("web_server RSS (KB)", pts, (148, 103, 189))],
        "Fig 2-2 web_server RSS vs time",
        "Time (s)",
        "RSS (KB)",
    )


def plot_latency():
    path = FIG / "load_latencies.txt"
    pts = []
    with path.open(encoding="utf-8") as f:
        for i, line in enumerate(f, start=1):
            parts = line.strip().split()
            if len(parts) >= 2:
                pts.append((float(i), float(parts[1]) * 1000.0))
    png_line_chart(
        FIG / "04_telemetry_latency.png",
        [("latency (ms)", pts, (23, 190, 207))],
        "Fig 2-3a telemetry latency (50 req)",
        "Request #",
        "Latency (ms)",
    )


def main():
    FIG.mkdir(parents=True, exist_ok=True)
    plot_temp()
    plot_mem()
    plot_latency()
    before = json.loads((FIG / "load_before.json").read_text(encoding="utf-8"))
    after = json.loads((FIG / "load_after.json").read_text(encoding="utf-8"))
    idle = read_xy(FIG / "temp_idle.csv", "t_sec", "cpu_temp")
    mem = read_xy(FIG / "mem_web_server.csv", "t_sec", "rss_kb")
    print("\n=== numbers ===")
    print(f"idle max temp: {max(y for _, y in idle):.2f}" if idle else "idle: EMPTY")
    print(f"mem initial/final/peak: {mem[0][1]:.0f}/{mem[-1][1]:.0f}/{max(y for _, y in mem):.0f}" if mem else "mem: EMPTY")
    print(
        f"load Δtemp={after['cpu_temp']-before['cpu_temp']:.2f} "
        f"Δcpu%={after['cpu_usage_percent']-before['cpu_usage_percent']:.2f} "
        f"Δmem%={after['mem_used_percent']-before['mem_used_percent']:.2f}"
    )
    if not read_xy(FIG / "temp_stream.csv", "t_sec", "cpu_temp") or not read_xy(
        FIG / "temp_detect.csv", "t_sec", "cpu_temp"
    ):
        print("[NEED] temp_stream.csv and/or temp_detect.csv are empty — re-run sampling then this script.")


if __name__ == "__main__":
    main()
