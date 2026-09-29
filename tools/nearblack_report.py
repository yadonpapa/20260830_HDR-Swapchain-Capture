"""Near-black ramp report for captures that may carry temporal dither (2026-09-29).

For every capture (.npz of dither_capture.py) this writes a ramp-row CSV in the data/ format
(x,R,G,B,min_over_frames_G,max_over_frames_G,mean_over_frames_G,mean_over_frames_and_rows_G) and a JSON
entry with

* single-frame statistics of one frame row: codes on the 8-bit lattice, step widths, adjacent differences,
  two-code jumps, skipped codes, non-monotonic steps, R = G = B;
* whole-frame temporal statistics: fraction of channel samples that vary, span histogram;
* with --ref: the time average (all frames) averaged over all frame rows, compared with a dither-free
  reference capture of the same ramp. The ramp starts at a slightly different x in every capture (the
  pattern is positioned by the viewer), so the horizontal shift that minimises the mean absolute
  difference is applied first. Reported: the difference statistics and, per reference code, the mean
  captured code - i.e. whether neighbouring codes stay one code apart.

The ramp is assumed to be horizontal and uniform along y (checked: `uniform_along_y`).

  uv run python tools/nearblack_report.py outputs/nb/hdfury_scrgb.npz --label osaka3070nb_hdfury_scrgb \\
      --out-dir data --out-json data/osaka3070nb_summary.json
  uv run python tools/nearblack_report.py outputs/nb/direct_hdr10.npz --label osaka3070nb_direct_hdr10 \\
      --ref outputs/nb/hdfury_scrgb.npz --out-dir data --out-json data/osaka3070nb_summary.json
"""
from __future__ import annotations

import argparse
import csv
import json
import os

import numpy as np

EDGE = 100          # px ignored at both ends when comparing with the reference
MAX_SHIFT = 80      # px searched for the horizontal alignment


def load(path: str) -> dict:
    """Read the arrays once (an open .npz decompresses an array again on every access)."""
    with np.load(path) as z:
        return {k: z[k] for k in ("first", "mean", "vmin", "vmax", "n", "info", "label")}


def _hist(values) -> dict:
    keys, counts = np.unique(np.asarray(values), return_counts=True)
    return {str(int(k)): int(v) for k, v in zip(keys, counts)}


def single_frame(line: np.ndarray) -> dict:
    """line: W×3 integer codes of one frame row."""
    g = line[:, 1]
    d = np.diff(g)
    idx = np.flatnonzero(d != 0)
    starts = np.concatenate([[0], idx + 1])
    widths = np.diff(np.concatenate([starts, [g.size]]))
    inner = widths[1:-1] if widths.size > 2 else widths
    present = set(g.tolist())
    return {
        "code_range": [int(g.min()), int(g.max())],
        "distinct_codes": int(len(present)),
        "codes_multiple_of_4_fraction": round(float(np.mean(line % 4 == 0)), 4),
        "adjacent_diff_histogram": _hist(d[d != 0]),
        "two_code_jumps": int((np.abs(d) == 2).sum()),
        "non_monotonic_steps": int((d < 0).sum()),
        "skipped_codes": [int(v) for v in range(int(g.min()), int(g.max()) + 1) if v not in present],
        "step_width_px": {"min": int(inner.min()), "median": float(np.median(inner)),
                          "max": int(inner.max()), "sd": round(float(inner.std()), 2)},
        "rgb_equal_fraction": round(float(np.mean((line[:, 0] == line[:, 1])
                                                  & (line[:, 1] == line[:, 2]))), 4),
    }


def ramp_row(z, row: int | None):
    first = z["first"].astype(int)
    row = first.shape[0] // 2 if row is None else row
    return first, row


def mean_line(z) -> np.ndarray:
    """Time average (all frames) of G, averaged over all frame rows."""
    return z["mean"][..., 1].astype(np.float64).mean(axis=0)


def versus_reference(z, ref, ref_label: str | None = None) -> dict:
    ref_first, ref_row = ramp_row(ref, None)
    ref_line = ref_first[ref_row, :, 1].astype(np.float64)
    line = mean_line(z)
    shift = min(range(-MAX_SHIFT, MAX_SHIFT + 1),
                key=lambda s: float(np.abs(np.roll(ref_line, s)[EDGE:-EDGE] - line[EDGE:-EDGE]).mean()))
    aligned = np.roll(ref_line, shift)
    err = (line - aligned)[EDGE:-EDGE]
    codes, means = [], []
    for code in range(int(aligned.min()), int(aligned.max()) + 1):
        sel = aligned == code
        sel[:EDGE] = False
        sel[-EDGE:] = False
        if int(sel.sum()) >= 8:
            codes.append(code)
            means.append(float(line[sel].mean()))
    step = np.diff(np.asarray(means))
    outliers = {str(int(c)): round(float(s), 2) for c, s in zip(codes[1:], step) if s < 0.5 or s > 1.5}
    return {
        "reference": ref_label or str(ref["label"]),
        "horizontal_shift_px": int(shift),
        "time_and_row_mean_minus_reference": {
            "mean": round(float(err.mean()), 3), "sd": round(float(err.std()), 3),
            "min": round(float(err.min()), 3), "max": round(float(err.max()), 3)},
        "mean_code_per_reference_code": {
            "codes_evaluated": [int(codes[0]), int(codes[-1])],
            "step_between_neighbouring_codes": {
                "min": round(float(step.min()), 3), "max": round(float(step.max()), 3),
                "sd": round(float(step.std()), 3), "ideal": 1.0},
            "steps_below_0.5_or_above_1.5": outliers},
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help=".npz from dither_capture.py")
    ap.add_argument("--label", default=None)
    ap.add_argument("--row", type=int, default=None, help="frame row of the ramp (default: the middle row)")
    ap.add_argument("--ref", default=None, help="dither-free reference capture (.npz) of the same ramp")
    ap.add_argument("--ref-label", default=None, help="name of the reference in the JSON (default: its label)")
    ap.add_argument("--out-dir", default=None, help="write <label>_ramp_row.csv here")
    ap.add_argument("--out-json", default=None, help="summary JSON (created or updated in place)")
    a = ap.parse_args()

    z = load(a.path)
    label = a.label or str(z["label"])
    first, row = ramp_row(z, a.row)
    line = first[row]
    span = z["vmax"].astype(int) - z["vmin"].astype(int)
    other = first[first.shape[0] // 4]
    entry = {
        "capture_info": str(z["info"]),
        "frames": int(z["n"]),
        "frame_row": int(row),
        "uniform_along_y": {"same_as_row_at_quarter_height": bool(np.array_equal(line, other)),
                            "max_abs_difference": int(np.abs(line - other).max())},
        "single_frame": single_frame(line),
        "whole_frame_channel_samples_varying_between_frames_fraction": round(float((span > 0).mean()), 4),
        "whole_frame_span_histogram": _hist(span),
        "time_mean_fractional_part_G_mean": round(
            float(np.abs(z["mean"][row, :, 1] - np.round(z["mean"][row, :, 1])).mean()), 4),
    }
    if a.ref:
        entry["vs_reference"] = versus_reference(z, load(a.ref), a.ref_label)

    print(f"=== {label}")
    print(json.dumps(entry, indent=2, ensure_ascii=False))

    if a.out_dir:
        os.makedirs(a.out_dir, exist_ok=True)
        path = os.path.join(a.out_dir, f"{label}_ramp_row.csv")
        mean_rows = mean_line(z)
        with open(path, "w", newline="", encoding="utf-8") as f:
            wr = csv.writer(f, lineterminator="\n")
            wr.writerow(["x", "R", "G", "B", "min_over_frames_G", "max_over_frames_G",
                         "mean_over_frames_G", "mean_over_frames_and_rows_G"])
            for x in range(line.shape[0]):
                wr.writerow([x, int(line[x, 0]), int(line[x, 1]), int(line[x, 2]),
                             int(z["vmin"][row, x, 1]), int(z["vmax"][row, x, 1]),
                             f"{float(z['mean'][row, x, 1]):.3f}", f"{float(mean_rows[x]):.3f}"])
        print("csv:", path)
    if a.out_json:
        data = {}
        if os.path.isfile(a.out_json):
            with open(a.out_json, encoding="utf-8") as f:
                data = json.load(f)
        data.setdefault("captures", {})[label] = entry
        with open(a.out_json, "w", encoding="utf-8", newline="\n") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
            f.write("\n")
        print("json:", a.out_json)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
