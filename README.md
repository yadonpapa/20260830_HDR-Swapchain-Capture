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

**Generation control 2026‑09‑04** (`docs/RESULTS.md` §7, `data/osaka3070_*`): the same measurement on an
**RTX 3070 (Ampere, desktop)** with a DeckLink 8K Pro G2 HDMI 2.1 input, 716/716 presents Independent Flip.
Ampere behaves differently — and identically for both swapchains: **every code on the 10‑bit link is a
multiple of 4 (an 8‑bit lattice) and every non‑black pixel toggles by ±4 between frames** (random
spatio‑temporal dither; the 60‑frame average matches the expected 10‑bit PQ code within one code). So the
uneven 10‑bit quantisation above is specific to the **true‑10‑bit direct scanout of the RTX 50 series**; Ampere
is not a comparable reference (and unsuitable for code‑level capture work). Getting a native 4K 10 bpc timing
out of a GeForce into the 8K Pro G2 needed NVIDIA CP *No scaling + Perform scaling on: Display* and a
re‑applied 10 bpc — see `docs/PROCEDURE.md` §7.

What the public record says about **RTX 40**, **driver branches** and **AMD Radeon** (and why AMD and RTX 40 are the next
measurements worth taking) is collected in `docs/RESEARCH_NOTES.md`.

Related threads: NVIDIA Developer Forums
[346429](https://forums.developer.nvidia.com/t/uneven-banding-in-fullscreen-hdr-output-with-r10g10b10a2-swapchain/346429) /
[343119](https://forums.developer.nvidia.com/t/uneven-banding-in-fullscreen-hdr-output-with-r10g10b10a2-swapchain/343119);
Qt bug (silent SDR fallback of the HDR swapchain, both variants):
[QTBUG-149927](https://bugreports.qt.io/browse/QTBUG-149927).

## What is in here

| Path | Purpose |
|---|---|
| `tools/proto_hdr_view.py` | Fullscreen Qt Quick test‑pattern window (PQ‑linear ramp 0→2000 nit + flat patches). `--mode scrgb\|hdr10\|srgb`, `--screen N`, `--list`, `--present-loop` (keep presenting every frame so PresentMon can log the presentation path), `--foreground` (bring the window to the front when launched from a script) |
| `tools/dither_capture.py` | Grab N consecutive frames from a DeckLink HDMI input as 10‑bit RGB (`r210`), per‑pixel min/max/mean, ROI time series → `.npz` |
| `tools/dither_analyze.py` | Step‑width / monotonicity / temporal statistics of the ramp row |
| `tools/make_ramp_y4m.py` | Synthetic 10‑bit limited‑range YCbCr ramp video (replaces the private test clip) |
| `tools/dxgi_outputs.cpp` | Dump DXGI adapters/outputs with `ColorSpace`, bits, luminance — proves what Windows thinks the output is |
| `tools/hdr_display.py`, `tools/pq.py` | Windows Advanced‑Color probe (ctypes) and PQ/sRGB curves |
| `decklink_core/` | C++ DeckLink wrapper DLL (capture + playback). Needs the Blackmagic SDK, see its README |
| `data/` | Captured ramp rows (CSV: x, R, G, B, per‑pixel min/max over frames), patch table, `summary.json`; `m25_*` = the 2026‑09‑04 PresentMon‑verified re‑measurement incl. the composition control (`m25_summary.json`); `osaka3070_*` = the RTX 3070 (Ampere) generation control incl. the 60‑frame time‑average column (`osaka3070_summary.json`) |
| `docs/PROCEDURE.md` | Step‑by‑step setup and measurement procedure (EN / 日本語) |
| `docs/RESULTS.md` | Full results, side findings, and the drafts posted to NVIDIA / Qt |
| `docs/RESEARCH_NOTES.md` | Desk research (2026‑09‑04): what is publicly known about RTX 40 output depth/dither, NVIDIA driver branches (Game Ready / Studio / Enterprise) and AMD Radeon drivers & dithering, plus a fact‑check (§D) of generative‑AI answers claiming Radeon / exclusive fullscreen / madVR would give bit‑exact HDR10 under Independent Flip — with implications for the next measurements (EN summary + full JA notes) |

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

**世代切り分け（2026‑09‑04）**: 同じ計測を **RTX 3070（Ampere・デスクトップ）**＋DeckLink 8K Pro G2 の
HDMI 2.1 入力で実施（716/716 Present が Independent Flip）。Ampere は両スワップチェーンとも **10bit リンク上の
全コードが 4 の倍数（8bit 格子）で、黒以外の全画素が毎フレーム ±4 で揺れる**（ランダム時空間ディザ。60 フレーム
平均は期待 10bit PQ コードに ±1 未満で一致）。上記の不均一量子化は **RTX 50 系の真 10bit 直接出力に固有**で、
Ampere は比較対象にならない（コード値照合の計測にも不向き）。GeForce → 8K Pro G2 で 4K 10bpc のネイティブ
タイミングを出すには NVIDIA CP の「スケーリングなし＋実行デバイス＝ディスプレイ」と 10 bpc の再適用が必要
（`docs/PROCEDURE.md` §7）。詳細は `docs/RESULTS.md` §7・データは `data/osaka3070_*`。

RTX 40 の出力挙動・NVIDIA ドライバ系統（Game Ready / Studio / Enterprise）・AMD Radeon のドライバと階調再現について
公開情報を調べた結果（次に計測すべき対象の根拠）と、「Radeon／排他的フルスクリーン／madVR なら Bit-Exact になる」という生成 AI 回答の検証（§D）は `docs/RESEARCH_NOTES.md`。
