# Results (2026‑08‑30) / 結果

## Environment

* GPU: NVIDIA GeForce RTX 5090 Laptop GPU, **Studio driver 596.36**, Windows 11 Pro 26200
  (adapters present: RTX 5090 Laptop, Intel Graphics, RTX PRO 6000 Blackwell (eGPU); the captured output is on the RTX 5090)
* Display: ASUS PA32UCDM via HDFury Vertex (EDID 4K60 444 HDR BT.2020), 3840×2160 @ 23.976 Hz, RGB 4:4:4 10 bpc full, Windows HDR on
* Capture: DeckLink 4K Extreme 12G HDMI in → `2160p23.98 RGB444+10bit hdr_present=1 eotf=2 MaxCLL=4000 MaxFALL=400`
* App: Qt 6.11.0 (PyQt6) QQuickWindow, D3D11 RHI, `QSG_RHI_HDR=scrgb|hdr10`, fullscreen (Independent Flip)

## 1. Prototype pattern (PQ‑linear ramp 0→2000 nit, 847 codes / 3840 px; `data/scrgb_ramp_row.csv`, `data/hdr10_ramp_row.csv`)

| Swapchain | Frame‑to‑frame changes, whole frame (60 f) | Adjacent diffs | Step widths (ideal 4.53 px) | 2‑code jumps |
|---|---|---|---|---|
| FP16 scRGB | 0.000 % | {0, 1} | 4: 397, 5: 449 (σ 0.50) | 0 |
| R10G10B10A2 HDR10 | 0.000 % | {0, 1, 2} | 4: 428, 5: 244, 6: 78, 8: 12, 9: 23, 10: 12, 11: 1 (σ 1.27) | 49 |

Flat patches (`data/prototype_patches.csv`, G channel, expected = round(PQ(nit/10000)·1023)):

| nit | expected | scRGB | HDR10 |
|---|---|---|---|
| 2 | 193 | 192 | 193 |
| 5 | 254 | 253 | 255 |
| 10 | 307 | 306 | 307 |
| 20 | 365 | 365 | 366 |
| 40 | 429 | 429 | 430 |
| 80 | 497 | 497 | 497 |
| 120 | 539 | 539 | 539 |
| 160 | 569 | 569 | 570 |
| 203 | 594 | 594 | 594 |
| 300 | 636 | 636 | 637 |
| 400 | 668 | 668 | 668 |
| 600 | 712 | 712 | 713 |
| 800 | 744 | 744 | 745 |
| 1000 | 769 | 769 | 769 |
| 1500 | 814 | 815 | 815 |
| 2000 | 846 | 847 | 848 |

## 2. Real 10‑bit YCbCr limited‑range ramp video through a viewer app (`data/app_*_ramp_row.csv`)

Source: code‑linear Y ramp (Y 64 at the centre → 959 at the edges, 1 code per 2 px), decoded exactly
(limited → full: (Y−64)/876), shown fullscreen with the viewer's SDR white set to 203 nit so the code
round‑trip is the identity. Expected full‑range code = round((Y−64)/876·1023).

| Swapchain | Frame‑to‑frame changes (ramp row) | captured − expected (Y 64..940, 3508 px) | Step widths |
|---|---|---|---|
| FP16 scRGB | 0 | {−1, 0, +1}; \|d\| ≥ 2: **0** | 2 px: 788 |
| R10G10B10A2 HDR10 | 0 | {−1 … +2}; \|d\| ≥ 2: **112** | 2 px: 714, **4 px: 37** (skipped codes) |

## 3. Conclusions

1. Neither path applies temporal dithering (all 60 frames bit‑identical; per‑pixel means are integers).
2. The R10G10B10A2 fullscreen path is quantised unevenly between the swapchain and the HDMI output;
   the FP16 path's conversion to PQ is exact to rounding. This matches the NVIDIA forum reports for RTX 50
   (5070 Ti, same monitor) and is not visible on AMD/Intel according to those reports.
3. Practical: for measurement / reference viewing on this hardware and driver range, prefer an FP16 (scRGB)
   swapchain. Not generalised beyond RTX 50 + this driver range.

## 4. Side finding: Qt silently falls back to SDR on a 300 %‑scaled HDR screen

With Windows display scaling at 300 % on the captured screen, Qt 6.11 logs (debug level only)

    Requested a scRGB swapchain but it is reported to be unsupported with the current display(s).
    In multi-screen configurations make sure the window is located on a HDR-enabled screen.
    Request ignored, using SDR swapchain.

and the content is composed as SDR (≥ 80 nit saturates at SDR white; PQ codes are treated as sRGB). DXGI
reports the output as `ColorSpace=12` (PQ/2020). `QT_D3D_ADAPTER_INDEX`, creating the native window first,
show → fullscreen, or `SetWindowPos` to the monitor's physical rect do not help; **`QT_ENABLE_HIGHDPI_SCALING=0`**
does. The first two captures of the day were invalid because of this — always check the Qt log.

## 5. Drafts posted / to be posted

See `docs/report_drafts.md` (English text for the NVIDIA thread reply and the Qt bug report, with Japanese versions).

## 6. Follow‑up (2026‑09‑04): presentation‑path‑verified re‑measurement + composition control

The 2026‑08‑30 numbers above were taken fullscreen but **without recording the presentation path**; a later
finding (interacting with other windows can silently demote a fullscreen window to DWM composition) made
that a gap worth closing. Re‑measured with PresentMon running **concurrently with every capture**.

### Environment (deliberately varied from 08‑30)

* Second unit of the same laptop model (RTX 5090 Laptop GPU), driver **610.62** (08‑30: Studio 596.36)
* Capture: **Blackmagic UltraStudio 4K Mini** (Thunderbolt) HDMI input — no HDFury; the UltraStudio's own
  HDMI‑input EDID advertises HDR10 (PQ) and RGB 10‑bit (DC_30bit), so the GPU drives it directly
* Same signal: 3840×2160 @ 23.976 Hz RGB 4:4:4 10 bpc full, HDR InfoFrame eotf=PQ (r210 capture)
* Pattern window presents continuously (`--present-loop`; a static Qt Quick scene stops presenting and
  becomes invisible to PresentMon), `QT_D3D_ADAPTER_INDEX` pinned to the NVIDIA adapter (hybrid‑GPU laptop)

### Results with `PresentMode = Hardware: Independent Flip` for every present during capture

| Swapchain | Steps (ideal 4.53 px) | 2‑code jumps | Patches | Temporal |
|---|---|---|---|---|
| FP16 scRGB | only 4/5 px (σ 0.50), monotonic | 0 | ±1 | 0 changed pixels |
| R10G10B10A2 HDR10 | 4 … 11 px (σ 1.27) | **49 — the same count as 08‑30** | many +1 | 0 changed pixels |

Identical numbers on a different unit, different capture device, different EDID chain and a newer driver —
the 08‑30 conclusion stands, now with the Independent Flip precondition proven
(`data/m25_*_ramp_row.csv`, `data/m25_summary.json`).

**New observation**: the 49 skipped codes are quasi‑periodic, ≈ 16 codes apart
(16, 32, 79, 112, 172, 189, 204, 220, …, 838 — full list in `m25_summary.json`), which suggests a
piecewise‑linear LUT (segment boundaries every ~16 codes) in the scanout‑path quantiser.

### Control: the same captures with the window demoted to DWM composition (`Composed: Flip` verified)

| Swapchain | vs Independent Flip |
|---|---|
| FP16 scRGB | ramp row **bit‑identical** (0 differing pixels) |
| R10G10B10A2 HDR10 | the periodic mid‑tone jumps **disappear entirely**; instead 23 near‑black codes (all ≤ 144: 1, 4, 9, 16, 37, …) are skipped — consistent with DWM converting the PQ swapchain into its FP16 linear canvas and the output stage re‑encoding to PQ (the same exit the scRGB path uses) |

This is the missing piece: **the uneven quantisation is specific to the direct scanout path of the
R10G10B10A2 fullscreen swapchain** (it vanishes under composition, matching the "windowed looks clean"
observations in the NVIDIA forum threads), and the scRGB path is byte‑exact regardless of composition.

Side note on demotion: merely moving focus to a window on *another* screen did **not** demote the
fullscreen window (still Independent Flip); occlusion by a window on the *same* screen did.

---

### 日本語（追補 2026‑09‑04）

* 8/30 と同条件の再計測を、**取り込みと同時刻の PresentMon 記録付き**（全 Present が
  Hardware: Independent Flip）で実施。別個体（同一機種）・別キャプチャ（UltraStudio 4K Mini・
  Vertex 無し）・新ドライバ **610.62** でも、段幅分布・2 コード飛び **49 箇所（同数）** まで一致
  ＝ 8/30 の結論を経路確認付きで確定。
* 新知見: 飛びは**約 16 コード周期の準周期**（16, 32, 79, 112, …, 838）＝スキャンアウト段の
  区分線形 LUT（セグメント境界）を示唆。
* 対照計測（Composed: Flip 確認付き）: scRGB はランプ行が**完全ビット一致**・HDR10 は中間調の
  周期飛びが**全消滅**し近黒（≤144）の 23 コード欠落に置換（DWM の PQ→FP16→PQ 往復）。
  ＝不均一は **R10G10B10A2 全画面の直接スキャンアウト経路に固有**。
* 付随: 別画面へのフォーカス移動だけでは iFlip は落ちない（合成への降格は同一画面上の遮蔽で発生）。

---

## 日本語要約

* 環境: RTX 5090 Laptop（Studio 596.36）→ Vertex → PA32UCDM ＋ DeckLink 4K Extreme 12G。2160p23.98 RGB 4:4:4 10bit PQ。
* プロトパターン: scRGB は段幅 4/5 のみ・単調・パッチ ±1。HDR10 は段幅 4〜11・2 コード飛び 49・パッチに +1 多数。
* 実 YCbCr limited ランプ動画（ビューワ経由）: scRGB は期待コード ±1・段幅 2 px 一様。HDR10 は −1〜+2（≥2 が 112 px）・
  4 px 幅（コード飛び）37 段。
* 結論: 時間軸ディザは両経路とも無し。HDR10 全画面経路の量子化が不均一（NVIDIA フォーラムの RTX 50 報告と一致）。
  この機材・ドライバ範囲では scRGB を既定にするのが安全。
* 副次: Qt 6.11 は拡大率 300% の画面で HDR スワップチェーンを無言で SDR に落とす（`QT_ENABLE_HIGHDPI_SCALING=0` で回避）。
