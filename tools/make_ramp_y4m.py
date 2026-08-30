"""Generate a 10-bit limited-range YCbCr test video (y4m) with a code-linear luma ramp.

Layout (3840x2160, yuv420p10, Cb = Cr = 512 everywhere):
  rows 370..700 : Y ramps from 64 at the horizontal centre to 959 at both edges (1 code per 2 px),
                  i.e. the same layout as the private test signal used in the measurements.
  rows   0.. 80 : Y = 1023 (full code, above limited-range white; useful for over-white tests)
  elsewhere     : Y = 64 (black)

Usage: uv run python tools/make_ramp_y4m.py out.y4m
Play/decode with any tool that reads y4m (ffmpeg, the viewer app). The y4m container carries no colour
tags, so tell your viewer to interpret it as PQ / BT.2020 if you want the HDR10 mapping.
"""
from __future__ import annotations

import sys

import numpy as np

W, H = 3840, 2160


def main() -> None:
    out = sys.argv[1] if len(sys.argv) > 1 else "ramp_limited_10bit.y4m"
    y = np.full((H, W), 64, np.uint16)
    half = W // 2
    ramp = np.minimum(np.arange(half) // 2, 895).astype(np.uint16)          # 0..895 codes over 1792 px
    y[370:700, half:half + half] = 64 + ramp[None, :]                          # centre -> right edge
    y[370:700, :half] = (64 + ramp[::-1])[None, :]                               # left edge <- centre
    y[:80, :] = 1023
    c = np.full((H // 2, W // 2), 512, np.uint16)
    with open(out, "wb") as f:
        f.write(f"YUV4MPEG2 W{W} H{H} F24000:1001 Ip A1:1 C420p10 XYSCSS=420P10\n".encode())
        for _ in range(4):
            f.write(b"FRAME\n")
            f.write(y.astype("<u2").tobytes()); f.write(c.astype("<u2").tobytes()); f.write(c.astype("<u2").tobytes())
    print("wrote", out)


if __name__ == "__main__":
    main()
