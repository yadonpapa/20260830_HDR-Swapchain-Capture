# HDR Swapchain Capture

Measure what a Windows HDR application **actually puts on the HDMI wire** — not what the panel shows —
by splitting the GPU output into a Blackmagic DeckLink HDMI input and capturing the raw 10‑bit RGB codes.

Used on 2026‑08‑30 to compare an **FP16 scRGB** swapchain with an **R10G10B10A2 HDR10** swapchain on an
NVIDIA GeForce RTX 5090 Laptop GPU (Studio driver 596.36). Headline results:

| Path (fullscreen, Independent Flip) | Frame‑to‑frame changes (60 frames) | Ramp step widths (ideal 4.53 px) | 2‑code jumps | Flat patches vs expected PQ code |
|---|---|---|---|---|
| **FP16 scRGB** | 0 pixels | only 4 or 5 px (σ 0.50), monotonic | 0 | within ±1 |
| **R10G10B10A2 HDR10** | 0 pixels | 4 … 11 px (σ 1.27) | 49 | many +1 (20 nit → 20.2, 600 → 603.8, 2000 → 2027.9) |

* **No temporal dithering on either path** (all 60 frames bit‑identical).
* The **HDR10 swapchain is not a pass‑through of the 10‑bit codes** on this driver; the FP16 → PQ conversion
  done by the display pipeline is exact to rounding. Same trend with a real 10‑bit YCbCr limited‑range ramp
  video decoded by a viewer app (`data/app_*.csv`).
* Side finding: **Qt 6.11 silently falls back to an SDR swapchain** when the target screen uses 300 % scaling
  (`QT_ENABLE_HIGHDPI_SCALING=0` works around it). See `docs/RESULTS.md`.

**Follow‑up 2026‑09‑04** (`docs/RESULTS.md` §6, `data/m25_*`): re‑measured with PresentMon running
concurrently — every present during capture was `Hardware: Independent Flip` — on a **second unit** of the
same laptop model, a **different capture device** (UltraStudio 4K Mini, no HDFury) and a **newer driver
(610.62)**: identical numbers, including the same 49 two‑code jumps. The skipped codes are quasi‑periodic
(≈ 16 codes apart), suggesting a piecewise‑linear LUT in the scanout path. A composition control
(`Composed: Flip` verified) makes the periodic jumps **disappear** (replaced by 23 near‑black skips from
DWM's PQ→FP16→PQ round trip) while the scRGB ramp stays **bit‑identical** — the uneven quantisation is
specific to the direct scanout path of the R10G10B10A2 fullscreen swapchain.

Related threads: NVIDIA Developer Forums
[346429](https://forums.developer.nvidia.com/t/uneven-banding-in-fullscreen-hdr-output-with-r10g10b10a2-swapchain/346429) /
[343119](https://forums.developer.nvidia.com/t/uneven-banding-in-fullscreen-hdr-output-with-r10g10b10a2-swapchain/343119).

## What is in here

| Path | Purpose |
|---|---|
| `tools/proto_hdr_view.py` | Fullscreen Qt Quick test‑pattern window (PQ‑linear ramp 0→2000 nit + flat patches). `--mode scrgb\|hdr10\|srgb`, `--screen N`, `--list`, `--present-loop` (keep presenting every frame so PresentMon can log the presentation path) |
| `tools/dither_capture.py` | Grab N consecutive frames from a DeckLink HDMI input as 10‑bit RGB (`r210`), per‑pixel min/max/mean, ROI time series → `.npz` |
| `tools/dither_analyze.py` | Step‑width / monotonicity / temporal statistics of the ramp row |
| `tools/make_ramp_y4m.py` | Synthetic 10‑bit limited‑range YCbCr ramp video (replaces the private test clip) |
| `tools/dxgi_outputs.cpp` | Dump DXGI adapters/outputs with `ColorSpace`, bits, luminance — proves what Windows thinks the output is |
| `tools/hdr_display.py`, `tools/pq.py` | Windows Advanced‑Color probe (ctypes) and PQ/sRGB curves |
| `decklink_core/` | C++ DeckLink wrapper DLL (capture + playback). Needs the Blackmagic SDK, see its README |
| `data/` | Captured ramp rows (CSV: x, R, G, B, per‑pixel min/max over frames), patch table, `summary.json`; `m25_*` = the 2026‑09‑04 PresentMon‑verified re‑measurement incl. the composition control (`m25_summary.json`) |
| `docs/PROCEDURE.md` | Step‑by‑step setup and measurement procedure (EN / 日本語) |
| `docs/RESULTS.md` | Full results, side findings, and the drafts posted to NVIDIA / Qt |

## Quick start

```powershell
uv sync                                           # Python 3.12+, numpy, PyQt6
# 1. build decklink_core (see decklink_core/README.md) and copy rawdecklink_core.dll to bin/
uv run python tools/dither_capture.py --list      # DeckLink devices and their input connectors
uv run python tools/proto_hdr_view.py --list      # screens and their Windows HDR state
# 2. show the pattern fullscreen on the captured screen (high-DPI scaling OFF, see RESULTS.md)
$env:QT_ENABLE_HIGHDPI_SCALING = "0"
Start-Process uv -ArgumentList "run python tools/proto_hdr_view.py --mode scrgb --screen 1 --auto-close 60"
uv run python tools/dither_capture.py --device 1 --frames 60 --skip 10 --wait 12 --roi 0,1800,3840,8 --out outputs/scrgb.npz --label scrgb
# 3. repeat with --mode hdr10 → outputs/hdr10.npz, then
uv run python tools/dither_analyze.py outputs/scrgb.npz outputs/hdr10.npz
```

Hardware used: HDFury Vertex (EDID copy of the monitor, pass‑through), ASUS PA32UCDM, DeckLink 4K Extreme 12G
(HDMI 2.0b, 8/10/12‑bit, 4:4:4). Output mode 3840×2160 @ 23.976 Hz RGB 4:4:4 10 bpc so the signal fits HDMI 2.0
without the GPU silently dropping to 4:2:2 / 8 bit.

## License

MIT (see `LICENSE`). The Blackmagic DeckLink SDK is not included and has its own license.

---

## 日本語

Windows の HDR アプリが**パネル上の見え方ではなく HDMI 伝送路に実際に出している 10bit コード**を、GPU 出力を
HDFury で分配して Blackmagic DeckLink の HDMI 入力に取り込むことで計測する手順とツール、およびその結果です。

2026‑08‑30 に RTX 5090 Laptop（Studio 596.36）で **FP16 scRGB** と **R10G10B10A2 HDR10** の全画面出力を比較:
両経路とも時間軸ディザは無く（60 フレーム全画素一致）、HDR10 経路は段幅 4〜11 px・2 コード飛び 49 箇所・+1〜+2 の
誤差と不均一に量子化される一方、scRGB は 1 コード刻み・±1 で正確でした。副次的に、Qt 6.11 が拡大率 300% の画面で
HDR スワップチェーンを無言で SDR に落とす問題（`QT_ENABLE_HIGHDPI_SCALING=0` で回避）も見つかっています。

手順は `docs/PROCEDURE.md`、結果と外部報告文案は `docs/RESULTS.md`、生データは `data/`。

**追補（2026‑09‑04）**: PresentMon を取り込みと同時刻に走らせ（全 Present が Hardware: Independent Flip）、
同一機種の別個体・別キャプチャ機（UltraStudio 4K Mini・HDFury 無し）・新ドライバ 610.62 で再計測 →
2 コード飛び 49 箇所まで**同一の結果**。飛びは約 16 コード周期＝スキャンアウト段の区分線形 LUT を示唆。
対照計測（DWM 合成・Composed: Flip 確認付き）では周期飛びが**全消滅**（近黒 23 コードの欠落に置換）・
scRGB は**完全ビット一致**＝不均一は R10G10B10A2 全画面の直接スキャンアウト経路に固有。
詳細は `docs/RESULTS.md` §6・データは `data/m25_*`。
