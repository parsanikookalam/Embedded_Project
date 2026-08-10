#!/usr/bin/env python3
"""Part 3-3 resolution comparison → report/part 3/fig/06_resolution_compare.png (stdlib PNG)."""
from __future__ import annotations

import struct
import zlib
from pathlib import Path

FIG = Path(__file__).resolve().parents[1] / "report" / "part 3" / "fig"
OUT = FIG / "06_resolution_compare.png"

W, H = 900, 520
PAD_L, PAD_R, PAD_T, PAD_B = 70, 40, 55, 70

RES = [320, 480, 640]
FPS = [18.4, 12.1, 9.6]
TEMP = [71.0, 78.0, 84.0]
ACC = [86.0, 93.0, 97.0]

FONT = {
    " ": [0, 0, 0, 0, 0],
    "-": [0, 0, 31, 0, 0],
    ".": [0, 0, 0, 0, 4],
    ":": [0, 4, 0, 4, 0],
    "/": [1, 2, 4, 8, 16],
    "%": [17, 2, 4, 8, 17],
    "(": [4, 8, 8, 8, 4],
    ")": [8, 4, 4, 4, 8],
    "0": [14, 17, 19, 21, 14],
    "1": [4, 12, 4, 4, 14],
    "2": [14, 1, 14, 16, 31],
    "3": [30, 1, 14, 1, 30],
    "4": [18, 18, 31, 2, 2],
    "5": [31, 16, 30, 1, 30],
    "6": [14, 16, 30, 17, 14],
    "7": [31, 1, 2, 4, 8],
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
    "X": [17, 10, 4, 10, 17],
    "Y": [17, 10, 4, 4, 4],
    "a": [0, 14, 1, 15, 15],
    "c": [0, 14, 16, 16, 14],
    "e": [0, 14, 31, 16, 14],
    "i": [4, 0, 12, 4, 14],
    "l": [12, 4, 4, 4, 14],
    "m": [0, 26, 21, 21, 21],
    "n": [0, 30, 17, 17, 17],
    "o": [0, 14, 17, 17, 14],
    "p": [0, 30, 17, 30, 16],
    "r": [0, 14, 17, 16, 16],
    "s": [0, 15, 14, 1, 30],
    "t": [8, 28, 8, 8, 6],
    "u": [0, 17, 17, 17, 15],
    "v": [0, 17, 17, 10, 4],
    "y": [0, 17, 15, 1, 14],
}


def write_png(path: Path, img: list[list[tuple[int, int, int]]]) -> None:
    h = len(img)
    w = len(img[0])
    raw = bytearray()
    for row in img:
        raw.append(0)
        for r, g, b in row:
            raw.extend((r, g, b))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def main() -> None:
    FIG.mkdir(parents=True, exist_ok=True)
    img = [[(255, 255, 255) for _ in range(W)] for _ in range(H)]

    def setp(x: int, y: int, c: tuple[int, int, int]) -> None:
        if 0 <= x < W and 0 <= y < H:
            img[y][x] = c

    def line(x0: int, y0: int, x1: int, y1: int, c: tuple[int, int, int]) -> None:
        dx = abs(x1 - x0)
        dy = -abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx + dy
        x, y = x0, y0
        while True:
            setp(x, y, c)
            if x == x1 and y == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x += sx
            if e2 <= dx:
                err += dx
                y += sy

    def text(x: int, y: int, s: str, c: tuple[int, int, int] = (30, 30, 30), scale: int = 2) -> None:
        cx = x
        for ch in s:
            bits = FONT.get(ch, FONT.get(ch.upper(), FONT[" "]))
            for col, mask in enumerate(bits):
                for row in range(5):
                    if mask & (1 << row):
                        for dy in range(scale):
                            for dx in range(scale):
                                setp(cx + col * scale + dx, y + row * scale + dy, c)
            cx += 6 * scale

    left, top = PAD_L, PAD_T
    width, height = W - PAD_L - PAD_R, H - PAD_T - PAD_B

    # background panel + grid
    for yy in range(top, top + height):
        for xx in range(left, left + width):
            img[yy][xx] = (250, 250, 250)
    for i in range(6):
        x = left + int(width * i / 5)
        line(x, top, x, top + height, (230, 230, 230))
        y = top + int(height * i / 5)
        line(left, y, left + width, y, (230, 230, 230))
    line(left, top, left + width, top, (40, 40, 40))
    line(left, top + height, left + width, top + height, (40, 40, 40))
    line(left, top, left, top + height, (40, 40, 40))
    line(left + width, top, left + width, top + height, (40, 40, 40))

    # left axis 0..110 for FPS and Accuracy
    # right axis 60..95 for temp — map to same pixel Y via separate scale
    def sx(i: int) -> int:
        return left + int(i / 2 * width)

    def sy_left(v: float) -> int:
        return top + height - int((v - 0) / 110 * height)

    def sy_temp(v: float) -> int:
        return top + height - int((v - 60) / 35 * height)

    series = [
        ("FPS", FPS, (31, 119, 180), sy_left),
        ("Acc%", ACC, (44, 160, 44), sy_left),
        ("Temp", TEMP, (214, 39, 40), sy_temp),
    ]
    for _name, vals, color, syn in series:
        pts = [(sx(i), syn(vals[i])) for i in range(3)]
        for i in range(1, 3):
            line(pts[i - 1][0], pts[i - 1][1], pts[i][0], pts[i][1], color)
            # thicker
            line(pts[i - 1][0], pts[i - 1][1] + 1, pts[i][0], pts[i][1] + 1, color)
        for px, py in pts:
            for dx in range(-3, 4):
                for dy in range(-3, 4):
                    if dx * dx + dy * dy <= 9:
                        setp(px + dx, py + dy, color)

    text(W // 2 - 280, 18, "Experiment 3-3 — Resolution vs FPS / temp / accuracy", (20, 20, 20), 2)
    text(W // 2 - 120, H - 28, "Detection input resolution", (40, 40, 40), 2)
    for i, r in enumerate(RES):
        text(sx(i) - 18, top + height + 12, str(r), (40, 40, 40), 2)

    # legend
    text(left + 10, top + 12, "FPS", (31, 119, 180), 2)
    text(left + 70, top + 12, "Accuracy %", (44, 160, 44), 2)
    text(left + 210, top + 12, "CPU temp C", (214, 39, 40), 2)
    text(left + 360, top + 12, "Optimum: 480", (20, 20, 20), 2)

    # arrow-ish note at 480
    text(sx(1) + 12, sy_left(FPS[1]) - 18, "best trade-off", (80, 80, 80), 2)

    write_png(OUT, img)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
