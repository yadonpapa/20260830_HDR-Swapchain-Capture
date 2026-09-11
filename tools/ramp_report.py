"""Ramp-row report in the data/*_summary.json format, from a capture (.npz) or a CSV (2026-09-11).

Produces, for one ramp row: step-width histogram, adjacent-difference values, two-code jumps, skipped
codes, frame-to-frame variation, isolated 1-px flips (spatial dither), the comparison with the expected
integer codes, an optional bit comparison with a reference CSV (e.g. data/m25_hdr10_ramp_row.csv) and
the PresentMon present-mode histogram of a concurrent log. Output: a ramp-row CSV compatible with data/
(x,R,G,B,min_over_frames_G,max_over_frames_G) and a JSON entry compatible with data/m25_summary.json.

  uv run python tools/ramp_report.py outputs/exp_a/hdr10.npz --row 4 --label a_hdr10 \\
      --ref data/m25_hdr10_ramp_row.csv --presentmon outputs/exp_a/pm_hdr10.csv --pm-app hdr10_direct.exe \\
      --out-csv data/a_hdr10_ramp_row.csv --out-json data/a_summary.json
  uv run python tools/ramp_report.py outputs/exp_a/a_hdr10_backbuffer.csv --label a_hdr10_backbuffer

Input CSV: x,R,G,B[,...] (data/ format, or the --readback CSV of tools/hdr10_direct.exe). Expected codes:
round(x * 846 / (W - 1)) — the ramp of hdr10_direct (846 = round(1023 * PQ(2000 nit / 10000)));
--expected-legacy switches to the proto_hdr_view ramp (1920 texels linspace(0, PQ(0.2)), bilinear x2).
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import sys
from collections import Counter
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pq import _pq_oetf  # noqa: E402

RAMP_MAX_CODE = int(np.rint(1023 * float(_pq_oetf(np.array(2000.0 / 10000.0)))))   # 846


def expected_codes(w: int, legacy: bool) -> np.ndarray:
    if not legacy:
        return np.rint(np.arange(w) * RAMP_MAX_CODE / (w - 1)).astype(int)
    # proto_hdr_view: texture 1920 px, code linspace(0, PQ(0.2)), FP16 texture, bilinear magnification x2
    tex = np.linspace(0.0, float(_pq_oetf(np.array(0.2))), 1920).astype(np.float16).astype(np.float64)
    tc = np.clip((np.arange(w) + 0.5) * 1920 / w - 0.5, 0, 1919)
    i0 = np.floor(tc).astype(int)
    i1 = np.minimum(i0 + 1, 1919)
    f = tc - i0
    return np.rint((tex[i0] * (1 - f) + tex[i1] * f) * 1023).astype(int)


def load_csv(path: str):
    rows = list(csv.DictReader(open(path, newline="", encoding="utf-8")))
    g = np.array([[float(r["R"]), float(r["G"]), float(r["B"])] for r in rows])
    if np.all(np.abs(g - np.rint(g)) < 1e-9):
        g = np.rint(g).astype(int)
    else:                                   # scRGB readback (linear, 1.0 = 80 nit) -> PQ code
        g = np.rint(1023 * _pq_oetf(g * 80.0 / 10000.0)).astype(int)
    vmin = np.array([int(r["min_over_frames_G"]) for r in rows]) if "min_over_frames_G" in rows[0] else g[:, 1]
    vmax = np.array([int(r["max_over_frames_G"]) for r in rows]) if "max_over_frames_G" in rows[0] else g[:, 1]
    return g, vmin, vmax, {"frames": 1 if "min_over_frames_G" not in rows[0] else None, "source": path}


def load_npz(path: str, row: int):
    z = np.load(path)
    rs = z["roi"].astype(int)               # N×rh×rw×3
    g = rs[0, row]                          # rw×3
    vmin = rs[:, row, :, 1].min(0)
    vmax = rs[:, row, :, 1].max(0)
    return g, vmin, vmax, {"frames": int(rs.shape[0]), "roi_xywh": [int(v) for v in z["roi_box"]],
                           "ramp_row_in_roi": row, "capture_info": str(z["info"]), "source": path}


def presentmon_modes(path: str, app: str | None) -> dict:
    with open(path, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        return {"error": "empty"}
    key = "PresentMode" if "PresentMode" in rows[0] else next((k for k in rows[0] if "PresentMode" in k), None)
    appk = "Application" if "Application" in rows[0] else next((k for k in rows[0] if "Application" in k), None)
    sel = [r for r in rows if not app or (appk and r[appk].lower() == app.lower())]
    modes = Counter(r[key] for r in sel) if key else Counter()
    # PresentMon 1.x: TimeInSeconds; 2.x: TimeInMs (or CPUStartTimeInMs); values may be "NA"
    tk, scale = next(((k, s) for k, s in (("TimeInSeconds", 1.0), ("TimeInMs", 1e-3), ("CPUStartTimeInMs", 1e-3))
                      if k in rows[0]), (None, 1.0))
    t = []
    for r in sel:
        try:
            t.append(float(r[tk]) * scale)
        except (KeyError, TypeError, ValueError):
            pass
    return {"file": path, "app_filter": app, "presents": len(sel), "present_modes": dict(modes),
            "window_s": round(max(t) - min(t), 1) if t else None}


def report(g: np.ndarray, vmin: np.ndarray, vmax: np.ndarray, legacy: bool) -> dict:
    G = g[:, 1]
    w = G.size
    d = np.diff(G)
    idx = np.flatnonzero(d != 0)
    widths = np.diff(np.concatenate([[0], idx + 1, [w]]))
    vals = G[np.concatenate([[0], idx + 1])]
    mid = (vals > G.min()) & (vals < G.max())
    present = set(G.tolist())
    skipped = [int(v) for v in range(int(G.min()), int(G.max()) + 1) if v not in present]
    exp = expected_codes(w, legacy)
    err = G - exp
    iso = int(((d[:-1] != 0) & (d[1:] == -d[:-1])).sum())
    return {
        "ramp_row_pixels_varying_between_frames": int((vmax != vmin).sum()),
        "ramp_row_step_width_histogram": {str(int(k)): int(v) for k, v in zip(*np.unique(widths[mid], return_counts=True))},
        "ramp_row_adjacent_diff_values": [int(v) for v in np.unique(d)],
        "two_code_jumps": int((np.abs(d) >= 2).sum()),
        "non_monotonic_steps": int((d < 0).sum()),
        "isolated_1px_flips": iso,
        "skipped_codes": skipped,
        "code_range": [int(G.min()), int(G.max())],
        "rgb_equal_fraction": float(np.mean((g[:, 0] == g[:, 1]) & (g[:, 1] == g[:, 2]))),
        "vs_expected": {"model": "legacy proto_hdr_view ramp" if legacy else f"round(x*{RAMP_MAX_CODE}/(W-1))",
                        "mean": round(float(err.mean()), 3),
                        "histogram": {str(int(k)): int(v) for k, v in zip(*np.unique(err, return_counts=True))}},
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help=".npz from dither_capture.py or a ramp-row CSV")
    ap.add_argument("--row", type=int, default=4, help="row inside the ROI (npz only)")
    ap.add_argument("--label", default=None, help="entry name in the JSON (default: file stem)")
    ap.add_argument("--ref", default=None, help="reference ramp-row CSV for a bit comparison")
    ap.add_argument("--presentmon", default=None, help="PresentMon CSV recorded concurrently")
    ap.add_argument("--pm-app", default=None, help="process name to select in the PresentMon CSV")
    ap.add_argument("--expected-legacy", action="store_true", help="expected codes of the proto_hdr_view ramp")
    ap.add_argument("--out-csv", default=None)
    ap.add_argument("--out-json", default=None, help="summary JSON (created or updated in place)")
    a = ap.parse_args()

    if a.path.lower().endswith(".npz"):
        g, vmin, vmax, meta = load_npz(a.path, a.row)
    else:
        g, vmin, vmax, meta = load_csv(a.path)
    label = a.label or Path(a.path).stem
    entry = {k: v for k, v in meta.items() if v is not None}
    entry.update(report(g, vmin, vmax, a.expected_legacy))
    if a.ref:
        rg, *_ = load_csv(a.ref)
        n = min(rg.shape[0], g.shape[0])
        entry["vs_reference"] = {"file": a.ref, "differing_pixels_G": int((rg[:n, 1] != g[:n, 1]).sum()),
                                 "width_compared": n}
    if a.presentmon:
        entry["presentmon"] = presentmon_modes(a.presentmon, a.pm_app)

    print(f"=== {label}")
    for k, v in entry.items():
        if k == "skipped_codes":
            print(f"  {k} ({len(v)}): {v}")
        else:
            print(f"  {k}: {v}")

    if a.out_csv:
        os.makedirs(os.path.dirname(os.path.abspath(a.out_csv)), exist_ok=True)
        with open(a.out_csv, "w", newline="", encoding="utf-8") as f:
            wr = csv.writer(f, lineterminator="\n")
            wr.writerow(["x", "R", "G", "B", "min_over_frames_G", "max_over_frames_G"])
            for x in range(g.shape[0]):
                wr.writerow([x, int(g[x, 0]), int(g[x, 1]), int(g[x, 2]), int(vmin[x]), int(vmax[x])])
        print("  csv:", a.out_csv)
    if a.out_json:
        data = {}
        if os.path.isfile(a.out_json):
            data = json.load(open(a.out_json, encoding="utf-8"))
        data.setdefault("captures", {})[label] = entry
        os.makedirs(os.path.dirname(os.path.abspath(a.out_json)), exist_ok=True)
        with open(a.out_json, "w", encoding="utf-8", newline="\n") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
            f.write("\n")
        print("  json:", a.out_json)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
