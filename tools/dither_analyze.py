"""dither_capture.py の npz からランプ行の段構造を解析する（2026-08-30）。

ランプ（PQ 空間で 0→2000 nit を横方向に連続値で描いたもの）を 10bit で受けると、理想は
「一定幅の階段（1 コード刻み）」。段幅の分布・単調性・隣接ノイズ・フレーム間変動を出す。
  uv run python tools/dither_analyze.py outputs/dither/scrgb.npz outputs/dither/hdr10.npz [--row 4]
"""
from __future__ import annotations

import argparse

import numpy as np


def analyze(path: str, row: int) -> None:
    z = np.load(path)
    rs = z["roi"]                                  # N×rh×rw×3
    n, rh, rw, _ = rs.shape
    print(f"=== {path} [{z['label']}] n={n} roi={z['roi_box'].tolist()}  {z['info']}")
    g = rs[:, row, :, 1].astype(int)               # N×rw（G）
    first = g[0]
    print(f"  行 {row}: min={first.min()} max={first.max()}  フレーム間で変動した画素={int((g.max(0) != g.min(0)).sum())}/{rw}")
    # R/G/B 一致（無彩色パターンなので一致が理想）
    f3 = rs[0, row].astype(int)
    print(f"  R==G==B の画素率={((f3[:, 0] == f3[:, 1]) & (f3[:, 1] == f3[:, 2])).mean() * 100:.2f}%")
    # 単調性・段構造
    d = np.diff(first)
    print(f"  隣接差分 unique={np.unique(d)[:20]}  負の差分（非単調）={int((d < 0).sum())}  |差分|>=2 の箇所={int((np.abs(d) >= 2).sum())}")
    # 段（同一値の連続区間）の幅分布
    idx = np.flatnonzero(d != 0)
    widths = np.diff(np.concatenate([[0], idx + 1, [rw]]))
    vals = first[np.concatenate([[0], idx + 1])]
    # 端（0 や飽和）を除いた中間部
    mid = (vals > first.min()) & (vals < first.max())
    w = widths[mid]
    if w.size:
        print(f"  段数={vals.size}  中間部の段幅: mean={w.mean():.2f} std={w.std():.2f} min={w.min()} max={w.max()}  "
              f"分布={dict(zip(*np.unique(w, return_counts=True)))}")
    # 空間ディザの兆候: 同一段の内部で値が揺れる（1 px の孤立した変化）
    iso = int(((d[:-1] != 0) & (d[1:] == -d[:-1])).sum())
    print(f"  孤立 1px の往復変化（ディザ/ノイズの兆候）={iso}")
    # フレーム平均（時間軸ディザがあれば非整数）
    m = g.mean(0)
    print(f"  フレーム平均の非整数度={np.abs(m - np.rint(m)).mean():.5f}")
    # 期待との比較: ランプは PQ コードが x に比例（0 → code(2000nit)=0.7518）
    pq_max = 0.7518
    exp_code = np.rint(np.linspace(0, pq_max, rw) * 1023).astype(int)
    err = first - exp_code
    print(f"  期待コード（線形ランプ・4K に等倍拡大想定）との差: mean={err.mean():+.2f} std={err.std():.2f} "
          f"min={err.min()} max={err.max()}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--row", type=int, default=4)
    a = ap.parse_args()
    for p in a.paths:
        analyze(p, a.row)


if __name__ == "__main__":
    main()
