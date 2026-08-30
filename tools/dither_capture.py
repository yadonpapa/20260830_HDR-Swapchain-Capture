"""DeckLink HDMI 入力で連続フレームを取り込み、GPU 出力の時間軸/空間ディザを計測する（2026-08-30）。

目的: HDR ビューワの scRGB（FP16）出力と HDR10（R10G10B10A2）出力で、静止パターンの画素コードが
フレーム間で変動するか（時間軸ディザ）・同一フレーム内の平坦部で散るか（空間ディザ）を数値で確かめる
（メモリ scrgb-dither-hypothesis / home-hdr-capture-setup）。

前提: rawdecklink_core.dll（decklink_core/ をビルド）の rdl_input_*（フォーマット自動検出。RGB 4:4:4 なら 10bit RGB r210）。
GPU → HDFury（EDID パススルー）→ DeckLink 4K Extreme 12G HDMI 入力。Windows の HDR は ON。

使い方:
  uv run python tools/dither_capture.py --list
  uv run python tools/dither_capture.py --device 1 --frames 60 --out outputs/dither/scrgb.npz \\
      --roi 1800,500,320,120 --label scrgb
  uv run python tools/dither_capture.py --compare outputs/dither/scrgb.npz outputs/dither/hdr10.npz
出力（.npz）: first（最初のフレーム u16 h×w×3）・vmin/vmax（全フレーム）・mean（float32）・
changed（フレーム間で一度でも変わった画素の回数 u16）・roi（N×rh×rw×3 u16 の逐次値）・info。
"""
from __future__ import annotations

import argparse
import ctypes
import os
import sys
import time
from ctypes import POINTER, byref, c_char, c_char_p, c_double, c_int, c_int64, c_void_p

import numpy as np

RDL_OK = 0


class RdlDeviceInfo(ctypes.Structure):
    _pack_ = 8
    _fields_ = [("name", c_char * 256), ("has_sdi_output", c_int64), ("has_hdmi_output", c_int64)]


def default_dll_path():
    """rawdecklink_core.dll の探索: 環境変数 RDL_CORE_DLL → ./bin → ./decklink_core/build → スクリプト隣。"""
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    name = "rawdecklink_core.dll" if sys.platform == "win32" else "librawdecklink_core.dylib"
    cands = [os.environ.get("RDL_CORE_DLL", ""), os.path.join(root, "bin", name),
             os.path.join(root, "decklink_core", "build", name), os.path.join(here, name)]
    for c in cands:
        if c and os.path.isfile(c):
            return c
    return None

RDL_E_NOSIGNAL = -8
CONN_SDI, CONN_HDMI = 1, 2
FLAG_YCBCR422, FLAG_RGB444, FLAG_12BIT, FLAG_10BIT, FLAG_8BIT = 1, 2, 8, 16, 32
FOURCC_R210 = 0x72323130   # 'r210'
FOURCC_V210 = 0x76323130   # 'v210'


class RdlCapturedInfo(ctypes.Structure):
    _pack_ = 8
    _fields_ = [("mode_id", c_int64), ("mode_name", c_char * 64),
                ("width", c_int64), ("height", c_int64),
                ("row_bytes", c_int64), ("pixel_format", c_int64),
                ("detected_flags", c_int64), ("hdr_present", c_int64),
                ("eotf", c_int64),
                ("max_cll", c_double), ("max_fall", c_double),
                ("max_dml", c_double), ("min_dml", c_double)]


def unpack_r210(buf: bytes, w: int, h: int, row_bytes: int) -> np.ndarray:
    """r210（ビッグエンディアン 2:10:10:10）→ (h,w,3) uint16（LUT-Adapt core/decklink_capture と同じ）。"""
    arr = np.frombuffer(buf, dtype=np.uint8)[: h * row_bytes]
    words = arr.reshape(h, row_bytes)[:, : w * 4].reshape(h, w, 4)
    words = words.view(">u4").reshape(h, w).astype(np.uint32)
    out = np.empty((h, w, 3), dtype=np.uint16)
    out[:, :, 0] = (words >> 20) & 0x3FF
    out[:, :, 1] = (words >> 10) & 0x3FF
    out[:, :, 2] = words & 0x3FF
    return out


class Capture:
    def __init__(self, dll_path: str | None = None):
        path = dll_path or default_dll_path()
        if not path:
            raise SystemExit("rawdecklink_core.dll が見つからない（decklink_core/ をビルドして bin/ に置くか RDL_CORE_DLL で指定）")
        self.lib = L = ctypes.CDLL(str(path))
        L.rdl_initialize.restype = c_int
        L.rdl_get_device_count.restype = c_int
        L.rdl_get_device_info.argtypes = [c_int, POINTER(RdlDeviceInfo)]
        L.rdl_input_get_connections.argtypes = [c_int, POINTER(c_int64)]
        L.rdl_input_open.argtypes = [c_int, c_int64]
        L.rdl_input_open.restype = c_void_p
        L.rdl_input_start.argtypes = [c_void_p]
        L.rdl_input_grab.argtypes = [c_void_p, c_int, c_int, c_void_p, c_int64, POINTER(RdlCapturedInfo)]
        L.rdl_input_stop.argtypes = [c_void_p]
        L.rdl_input_close.argtypes = [c_void_p]
        L.rdl_input_close.restype = None
        L.rdl_input_get_last_error.argtypes = [c_void_p, c_char_p, c_int]
        rc = L.rdl_initialize()
        if rc != RDL_OK:
            raise SystemExit(f"rdl_initialize failed ({rc})")
        self.h = None
        self.buf = ctypes.create_string_buffer(64 * 1024 * 1024)

    def devices(self) -> list[tuple[int, str, int]]:
        out = []
        for i in range(self.lib.rdl_get_device_count()):
            info = RdlDeviceInfo()
            if self.lib.rdl_get_device_info(i, byref(info)) == RDL_OK:
                mask = c_int64(0)
                self.lib.rdl_input_get_connections(i, byref(mask))
                out.append((i, info.name.decode("utf-8", "replace"), int(mask.value)))
        return out

    def open(self, device: int, connection: int = CONN_HDMI) -> None:
        h = self.lib.rdl_input_open(device, connection)
        if not h:
            raise SystemExit("rdl_input_open failed")
        rc = self.lib.rdl_input_start(h)
        if rc != RDL_OK:
            raise SystemExit(f"rdl_input_start failed ({rc}): {self._err(h)}")
        self.h = h

    def grab(self, skip: int = 0, timeout_ms: int = 5000) -> tuple[np.ndarray, RdlCapturedInfo]:
        info = RdlCapturedInfo()
        rc = self.lib.rdl_input_grab(self.h, skip, timeout_ms, self.buf, len(self.buf), byref(info))
        if rc == RDL_E_NOSIGNAL:
            raise SystemExit(f"入力信号なし: {self._err(self.h)}")
        if rc != RDL_OK:
            raise SystemExit(f"rdl_input_grab failed ({rc}): {self._err(self.h)}")
        w, h, rb = int(info.width), int(info.height), int(info.row_bytes)
        if int(info.pixel_format) != FOURCC_R210:
            raise SystemExit(f"想定外の pixel_format 0x{int(info.pixel_format):08x}（RGB 4:4:4 で入力されていない。"
                             f"flags={int(info.detected_flags)}）")
        return unpack_r210(self.buf.raw[: h * rb], w, h, rb), info

    def close(self) -> None:
        if self.h:
            self.lib.rdl_input_stop(self.h)
            self.lib.rdl_input_close(self.h)
            self.h = None

    def _err(self, h) -> str:
        b = ctypes.create_string_buffer(512)
        self.lib.rdl_input_get_last_error(h, b, 512)
        return b.value.decode("utf-8", "replace")


def describe(info: RdlCapturedInfo) -> str:
    f = int(info.detected_flags)
    fl = [n for n, v in (("YCbCr422", FLAG_YCBCR422), ("RGB444", FLAG_RGB444), ("12bit", FLAG_12BIT),
                         ("10bit", FLAG_10BIT), ("8bit", FLAG_8BIT)) if f & v]
    return (f"{info.mode_name.decode('utf-8', 'replace')} {int(info.width)}x{int(info.height)} "
            f"pixfmt=0x{int(info.pixel_format):08x} flags={'+'.join(fl) or f} "
            f"hdr_present={int(info.hdr_present)} eotf={int(info.eotf)} "
            f"MaxCLL={info.max_cll:g} MaxFALL={info.max_fall:g} DML={info.min_dml:g}..{info.max_dml:g}")


def run_capture(a) -> None:
    if a.wait > 0:
        print(f"  {a.wait:.0f} s 待機…")
        time.sleep(a.wait)
    cap = Capture(a.dll)
    cap.open(a.device, CONN_HDMI)
    try:
        first, info = cap.grab(skip=a.skip, timeout_ms=5000 + int(a.skip * 60))
        print("入力:", describe(info))
        h, w = first.shape[:2]
        rx, ry, rw, rh = a.roi if a.roi else (w // 2 - 64, h // 2 - 32, 128, 64)
        vmin = first.copy()
        vmax = first.copy()
        acc = first.astype(np.float64)
        changed = np.zeros((h, w, 3), np.uint16)
        prev = first
        rois = [first[ry:ry + rh, rx:rx + rw].copy()]
        t0 = time.time()
        for i in range(1, a.frames):
            cur, _ = cap.grab(skip=0)
            np.minimum(vmin, cur, out=vmin)
            np.maximum(vmax, cur, out=vmax)
            acc += cur
            changed += (cur != prev)
            rois.append(cur[ry:ry + rh, rx:rx + rw].copy())
            prev = cur
            if i % 10 == 0:
                print(f"  {i}/{a.frames} frames ({time.time() - t0:.1f}s)")
    finally:
        cap.close()
    n = a.frames
    mean = (acc / n).astype(np.float32)
    span = (vmax.astype(int) - vmin.astype(int))
    varying = span > 0
    print(f"--- {a.label or a.out}: {n} フレーム {w}x{h}")
    print(f"  フレーム間で値が変わった画素（ch 単位）: {varying.mean() * 100:.3f}%  "
          f"（span 分布: 1={int((span == 1).sum())}, 2={int((span == 2).sum())}, >=3={int((span >= 3).sum())}）")
    # 平坦部の空間散布（ROI の最初のフレーム）
    r0 = rois[0].astype(int)
    print(f"  ROI({rx},{ry},{rw}x{rh}) 最初のフレーム: G の unique={np.unique(r0[..., 1])[:12]} ...")
    rs = np.stack(rois)                                   # N×rh×rw×3
    print(f"  ROI 時間軸: 画素平均の非整数度（mean の小数部の平均）= "
          f"{np.abs(rs.mean(axis=0) - np.rint(rs.mean(axis=0))).mean():.4f}  "
          f"フレーム間変動画素率={(rs.max(axis=0) != rs.min(axis=0)).mean() * 100:.2f}%")
    # 行方向のプロファイル（ROI 中央行・G）: 最初のフレームと平均
    mid = rh // 2
    print("  ROI 中央行 G 先頭 16px  first:", r0[mid, :16, 1].tolist())
    print("                          mean :", np.round(rs[:, mid, :16, 1].mean(axis=0), 2).tolist())
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    np.savez_compressed(a.out, first=first, vmin=vmin, vmax=vmax, mean=mean, changed=changed,
                        roi=rs, roi_box=np.array([rx, ry, rw, rh]), n=n,
                        info=np.array(describe(info)), label=np.array(a.label or ""))
    print("  保存:", a.out)


def compare(paths: list[str]) -> None:
    for p in paths:
        z = np.load(p)
        span = z["vmax"].astype(int) - z["vmin"].astype(int)
        rs = z["roi"]
        print(f"{p} [{z['label']}] n={int(z['n'])} {z['info']}")
        print(f"  変動画素率={(span > 0).mean() * 100:.3f}%  ROI 変動率={(rs.max(axis=0) != rs.min(axis=0)).mean() * 100:.2f}%  "
              f"ROI 平均の非整数度={np.abs(rs.mean(axis=0) - np.rint(rs.mean(axis=0))).mean():.4f}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="DeckLink デバイスと入力コネクタを列挙")
    ap.add_argument("--device", type=int, default=None, help="デバイス index（--list で確認）")
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--skip", type=int, default=5, help="最初に捨てるフレーム数（ロック待ち）")
    ap.add_argument("--wait", type=float, default=0.0, help="取り込み開始前に待つ秒数（表示側の起動待ち）")
    ap.add_argument("--roi", type=lambda s: tuple(int(v) for v in s.split(",")), default=None,
                    help="x,y,w,h（既定: 画面中央 128x64）")
    ap.add_argument("--out", default="outputs/dither/capture.npz")
    ap.add_argument("--label", default="")
    ap.add_argument("--dll", default=None)
    ap.add_argument("--compare", nargs="+", default=None, help="保存済み npz を並べて要約")
    a = ap.parse_args()
    if a.compare:
        compare(a.compare)
        return
    if a.list or a.device is None:
        cap = Capture(a.dll)
        for i, name, mask in cap.devices():
            conns = [n for n, v in (("SDI", CONN_SDI), ("HDMI", CONN_HDMI)) if mask & v]
            print(f"[{i}] {name}  input={'/'.join(conns) or '-'}")
        if a.device is None:
            return
    run_capture(a)


if __name__ == "__main__":
    main()
