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

## 7. Generation control (2026‑09‑04): RTX 3070 (Ampere) puts an 8‑bit lattice + spatio‑temporal dither on a 10‑bit link

To find out whether the R10G10B10A2 finding is a Blackwell (RTX 50) property or an NVIDIA‑wide one, the same
measurement was run on an **Ampere desktop GPU** (`data/osaka3070_*`, `data/osaka3070_summary.json`).

### Environment

* NVIDIA GeForce RTX 3070 (desktop, single GPU), driver **610.62** (same package as §6)
* Capture: **Blackmagic DeckLink 8K Pro G2** (PCIe) **HDMI 2.1 input**, r210 — no HDFury; the card's HDMI‑input
  EDID advertises PQ, BT.2020, DC_30/36bit, max TMDS 600 MHz and FRL (see §7 of PROCEDURE.md for the
  GeForce scaling pitfalls this EDID triggers)
* Signal verified three ways before capturing: DisplayConfig target mode 3840×2160 total 5500×2250 @ 23.976 Hz
  (296.7 MHz, scaling IDENTITY), Windows Advanced Color `bitsPerColorChannel = 10`, DeckLink detection
  `2160p23.98 RGB444 10bit eotf=PQ`
* PresentMon concurrent with every capture (30 s window): **716 / 716 presents `Hardware: Independent Flip`**
  for both swapchains (`--present-loop`, window brought to the front with `--foreground`)

### Results — the two swapchains behave identically, and differently from Blackwell

| | FP16 scRGB | R10G10B10A2 HDR10 |
|---|---|---|
| Codes in a single frame | **every pixel a multiple of 4** (213 distinct values) | 99.99 % multiples of 4 (257) |
| Frame‑to‑frame | **every non‑black pixel toggles by exactly ±4** (73.5 % of all channel samples; span 4 only) | same (span 4, a few 3) |
| Time average (60 frames × 16 ramp rows) − expected PQ code, 3840‑px ramp | mean +0.35, σ 0.26, max 0.90, no pixel > 1.5 | mean +0.33, σ 0.38, max 1.39, no pixel > 1.5 |
| Flat patches 2 … 2000 nit, time average − expected | +0.2 … +0.7 | −0.1 … +1.2 |
| Ramp step widths in a single frame | 1 … 20 px, ~620 isolated 1‑px flips (spatial dither) | same (~630) |
| Temporal autocorrelation lag 1–4 / same phase as neighbour | ≈ 0 / 48–52 % | same |

Interpretation: on Ampere the scanout stage **encodes on an 8‑bit lattice even though the link is 10 bpc**, and
carries the two missing bits as random spatio‑temporal dither — the time average converges on the correct
10‑bit PQ code to well within one code, a single frame does not. It does not depend on the swapchain format
(FP16 or R10G10B10A2), so it is an output‑stage property, unrelated to DWM (Independent Flip throughout).

Consequences:

* **The 2‑code jumps of §1/§6 cannot be evaluated on Ampere** — it never emits a 10‑bit lattice. The uneven
  10‑bit quantisation of the R10G10B10A2 fullscreen path is therefore specific to the **true 10‑bit direct
  scanout of the RTX 50 series (Blackwell)**; Ampere shows a different, unrelated degradation that affects
  scRGB and HDR10 alike. The scope of the NVIDIA report is "RTX 50 series", with Ampere noted as
  not comparable — posted to thread 346429 on 2026‑09‑04 (`docs/report_drafts.md` §1c).
* For **code‑level measurement work** (capturing what an app puts on the wire) an Ampere GPU is unsuitable
  unless the values are time‑averaged; for visual checks on a reference monitor the dither is harmless.
* Not tested: Windows HDR off (SDR 10 bpc), NVCP "default colour settings", YCbCr 4:2:2 12‑bit, other Ampere
  drivers. Next control: RTX PRO 6000 Blackwell (desktop Blackwell) — does the ≈16‑code periodic pattern
  reproduce there?

### 日本語（世代切り分け 2026‑09‑04）

* RTX 5090（Blackwell）の結果が世代固有か NVIDIA 全般かを見るため、**RTX 3070（Ampere・デスクトップ）**＋
  DeckLink 8K Pro G2 の HDMI 2.1 入力で同計測（ドライバ 610.62・全 716 Present が Independent Flip・
  DisplayConfig / Windows ACI2 / DeckLink 検出の 3 点で 2160p23.976 RGB 10bpc を確認）。
* 結果: scRGB / HDR10 とも**全コードが 4 の倍数（8bit 格子）**で、黒以外の全画素が毎フレーム ±4 で揺れる
  **ランダム時空間ディザ**。60 フレームの時間平均は期待 10bit PQ コードに +0.35 ±0.3（最大 0.9 / 1.4）で一致
  ＝10bit 相当の情報はディザで載っている。前後フレーム・隣接画素と無相関。
* 含意: Ampere は 10bit の格子を出さないので **2 コード飛びは評価不能＝現象は RTX 50 系の真 10bit 直接出力に
  固有**。報告の適用範囲は「RTX 50 系」、Ampere は「比較対象にならない（別種の劣化）」と付記。コード値照合の
  計測用途には Ampere は不向き（時間平均なら ±1）。次の切り分けは RTX PRO 6000 Blackwell。

---

## 8. Desktop Blackwell control (2026‑09‑08): RTX PRO 6000 Blackwell reproduces the RTX 5090 Laptop result bit for bit

§7 left one question open: is the ≈16‑code periodic quantiser a property of the *laptop* RTX 5090 (HDMI port,
hybrid‑GPU platform, Game Ready / Studio branch) or of Blackwell in general? Same measurement on a **desktop
Blackwell workstation GPU** driven through **DisplayPort** (`data/rog6000_*`, `data/rog6000_summary.json`).

### Environment

* NVIDIA **RTX PRO 6000 Blackwell Workstation Edition** (GB202), driver **616.56**, in a Thunderbolt 5 eGPU
  enclosure on the same ROG laptop as 08‑30; pattern window pinned to it with `QT_D3D_ADAPTER_INDEX=1`
* Output: the card has DisplayPort 2.1 only → **Club3D CAC‑1088** (DP 1.4 → HDMI 2.1 active adapter,
  Synaptics, bus‑powered) → HDMI cable → **DeckLink 4K Extreme 12G HDMI in** (r210). No HDFury: the GPU sees
  the DeckLink's own HDMI‑input EDID (`BMD HDMI`). See PROCEDURE.md §8 for the adapter/cable pitfalls.
* Signal verified: Windows Advanced Color `bitsPerColorChannel = 10`, HDR on, DeckLink detection
  `2160p23.98 RGB444+10bit hdr_present=1 eotf=2` (PQ). 4K23.976 RGB 10‑bit is inside the uncompressed DP 1.4
  budget, so the adapter runs without DSC.
* PresentMon 2.5.1 concurrent with every capture (40 s window): **757/761 (scRGB) and 758/762 (HDR10)
  presents `Hardware Composed: Independent Flip`** (the MPO‑plane flavour of independent flip in PresentMon 2.x
  naming); the 4 `Composed: Flip` rows are the first 4 presents at window creation, 12 s before the capture.

### Results — identical to the RTX 5090 Laptop (M25, §6) to the pixel

| Swapchain | Steps (ideal 4.53 px) | 2‑code jumps | Patches | Temporal | vs `m25_*` |
|---|---|---|---|---|---|
| FP16 scRGB | only 4/5 px (4: 397, 5: 449; σ 0.50), monotonic | 0 | ±1 | 0 changed pixels | ramp row **0 differing pixels**, 16 patches identical |
| R10G10B10A2 HDR10 | 4 … 11 px (4: 428, 5: 244, 6: 78, 8: 12, 9: 23, 10: 12, 11: 1; σ 1.27) | **49, at the same codes** | many +1 | 0 changed pixels | ramp row **0 differing pixels**, 16 patches identical |

Interpretation:

* The uneven quantisation of the R10G10B10A2 direct‑scanout path is a **Blackwell display‑pipeline property**:
  it reproduces on a desktop workstation part, through DisplayPort 2.1 + an external DP→HDMI converter, on a
  third driver package (596.36 / 610.62 / 616.56), with the same 49 skipped codes. It sits **upstream of the
  link encoder** (HDMI on the laptop, DP here) and is independent of the platform (hybrid‑GPU laptop vs eGPU).
* The scRGB path is byte‑exact here as well, so the CAC‑1088 is **transparent** at 4K23.976 RGB 10‑bit: a
  DP→HDMI active adapter is an acceptable way to bring a DP‑only card into an HDMI capture card for this
  kind of measurement, provided it really runs 10 bpc (PROCEDURE.md §8).
* Scope of the NVIDIA report can be stated as "Blackwell (RTX 50 series and RTX PRO Blackwell), Independent
  Flip, R10G10B10A2 fullscreen" — not laptop‑specific, not branch‑specific. Posted to thread 346429 on
  2026‑09‑08 (`docs/report_drafts.md` §1d).
* Still untested: RTX 40 (Ada) and AMD Radeon (see RESEARCH_NOTES.md), Windows HDR off (SDR 10 bpc),
  YCbCr 4:2:2 12‑bit.

### 日本語（デスクトップ Blackwell 対照 2026‑09‑08）

* §7 の残課題「約 16 コード周期の量子化は RTX 5090 *Laptop*（HDMI・ハイブリッド GPU・GeForce 系ドライバ）固有か、
  Blackwell 全般か」を、**RTX PRO 6000 Blackwell Workstation Edition**（GB202・ドライバ 616.56・TB5 eGPU）で確認。
  出力は DP 2.1 のみなので **Club3D CAC‑1088**（DP 1.4 → HDMI 2.1 アクティブ・バスパワー）→ DeckLink 4K Extreme 12G
  HDMI 入力（HDFury 無し・DeckLink 自身の EDID）。Windows ACI2 10bit・DeckLink 検出 2160p23.98 RGB444 10bit PQ・
  PresentMon 757/761・758/762 が Independent Flip（残り 4 件はウィンドウ生成時、取り込み開始の 12 秒前）。
* 結果: scRGB は段幅 4/5 のみ・2 コード飛び 0、HDR10 は段幅 4〜11・**2 コード飛び 49 箇所（同じコード）**。
  ランプ行 3840 px・平坦パッチ 16 点とも **M25（RTX 5090 Laptop）と 1 画素も違わない**。
* 含意: 不均一量子化は **Blackwell のディスプレイパイプラインの性質**（デスクトップ WS 版・DP 2.1＋外部変換・
  第 3 のドライバ版でも同一）＝リンクエンコーダより上流・プラットフォーム非依存。報告の適用範囲は
  「Blackwell（RTX 50 系＋RTX PRO Blackwell）」と言える（2026‑09‑08 にスレッド 346429 へ投稿済み・`report_drafts.md` §1d）。scRGB がここでもビット一致なので CAC‑1088 は
  4K23.976 RGB 10bit で透過（変換器経由でもコード値計測に使える。手順は PROCEDURE.md §8）。
* 未計測: RTX 40（Ada）・AMD Radeon・Windows HDR オフ（SDR 10bpc）・YCbCr 4:2:2 12bit。

---

## 9. Experiment A (2026‑09‑11): the missing codes are not in the swapchain — integer codes copied into the back buffer, read back, and still 49 skipped codes on the wire

Every capture up to §8 used the Qt proto pattern, where the PQ codes of the R10G10B10A2 swapchain were produced
by the application (CPU PQ encode → FP16 texture → bilinear ×2 → output‑merger float→UNORM) and the back buffer
was never read back. The adversarial review (`docs/hdr-swapchain-adversarial-review-20260911.md`, attack 1) pointed
out that "app encodes unevenly, driver is fine" was still open. This experiment closes it (`data/a_*`,
`data/a_summary.json`, procedure in PROCEDURE.md §9).

### Method

* `tools/hdr10_direct.cpp` (plain D3D11): the 10‑bit codes are computed on the CPU as **integers**, placed in a
  DEFAULT texture and copied into the back buffer with `CopyResource` — no shader, no filtering, no float→UNORM
  stage. Ramp: `code(x) = round(x · 846 / 3839)`, every code 0..846 exactly once (4–5 px); 16 flat patches.
* Before every 24th `Present` the back buffer is copied to a staging texture and compared byte for byte with the
  source; the ramp row is written to a CSV. **80/80 readbacks bit‑identical per run**; the back‑buffer ramp row has
  0 jumps, step widths {4, 5}, no skipped codes (`a_hdr10_backbuffer` in `a_summary.json`).
* GPU: RTX PRO 6000 Blackwell (616.56, TB5 eGPU) → DP 2.1 → CAC‑1088 → **HDFury Vertex (EDID emulation)** →
  HDMI → **UltraStudio 4K Mini** HDMI in (r210). Signal `2160p23.98 RGB444+10bit hdr_present=1 eotf=2`.
  PresentMon 2.5.1 (elevated) concurrent, 70 s window: **1563/1568 presents `Hardware Composed: Independent
  Flip`** per run (5 window‑creation presents `Composed: Flip`, before the capture).
* Control: `--mode scrgb` — the same pattern as IEEE halves (nit/80) into an R16G16B16A16_FLOAT swapchain
  through the same `CopyResource` path.

### Results (ramp row, 3840 px, 60 frames; all 8 ROI rows identical, R = G = B everywhere)

| Swapchain | Back buffer (readback) | Wire: steps | Wire: 2‑code jumps | Wire: skipped codes | Wire − source |
|---|---|---|---|---|---|
| **R10G10B10A2 HDR10** | every code 0..846, 4/5 px, 0 skips | 4: 346, 5: 403, **9: 45, 10: 3** | **49** | **the same 49 codes as `m25_hdr10` / `rog6000_hdr10`** (16, 32, 79, 112, 172, …, 838) | {−1: 14, 0: 2475, +1: 1351} |
| FP16 scRGB (control) | every code 0..846 (as halves), 0 skips | 4: 377, 5: 441, 9: 13 | 14 | 14, **all ≤ 135** (1, 4, 8, 16, 37, 45, 49, 51, 54, 58, 103, 118, 133, 135) | {−1: 275, 0: 3565} |

Flat patches (G, wire): HDR10 +1 on 11 of 16 (5, 20, 40, 120, 160, 300, 400, 600, 800, 1500, 2000 nit),
scRGB −1 on 2/5/10 nit and +1 on 1500/2000 nit, the rest exact. No temporal change in any of the 3840×2160×3
samples over 60 frames on either path; no isolated 1‑px flips (no spatial dither).

### Interpretation

1. **The application‑side hypothesis is dead.** The swapchain provably contained every code; the wire is missing
   exactly the 49 codes that the Qt proto measurements found on three Blackwell units. The quantiser sits between
   the R10G10B10A2 swapchain and the link, in the direct‑scanout path — a driver/display‑pipeline property.
2. The pixel rows differ from `m25`/`rog6000` (885 px) only because the source ramp differs (exact integer codes
   at 4.53 px vs the proto's continuous FP16 ramp); the **skipped‑code list is identical**, and the +1 patch pattern
   matches on every patch whose source code is the same.
3. The scRGB control confirms the tool and the chain: no mid‑tone skip, wire within −1..0 of the source. Its 14
   near‑black skips (all ≤ 135) are the display pipeline's FP16→PQ re‑encode being exact only to ±1 code near
   black — a discrete code‑centre ramp exposes that as skips where the earlier continuous ramp absorbed it as
   step‑width jitter (same exit stage as the 23 near‑black skips of `m25_hdr10_composed`). Mid‑tones: bit‑exact.
   The earlier "scRGB exact to rounding" wording stands as "exact within ±1 code".
4. Side findings for the hardware log (PROCEDURE.md §8): the CAC‑1088 again failed to read the capture device's EDID
   directly (Non‑PnP 1024×768 fallback), and an HDFury Vertex in between (EDID emulation) fixed it; the DeckLink
   4K Extreme 12G in the AKiTiO Node was unusable when the Node sat behind the TB5 dock (no PCI resources,
   0xC00000C0) and flaky even when connected directly (code 43 twice) — the UltraStudio 4K Mini was used instead.

### Second unit, same evening: RTX 5090 Laptop, HDMI direct into the UltraStudio (`data/a5090_*`)

The same two runs on the **RTX 5090 Laptop GPU** of the same machine (the 2026‑08‑30 unit), driver 616.56, laptop
HDMI 2.1 port → UltraStudio 4K Mini directly (its own EDID; no Vertex — see below), signal
`2160p23.98 RGB444+10bit eotf=2`, PresentMon **1564/1571 (HDR10) and 1505/1510 (scRGB) presents
`Hardware: Independent Flip`** — the non‑MPO flavour this time. Result: the **whole captured frame
(3840×2160×3) is bit‑identical to the RTX PRO 6000 run for both swapchains** (0 differing pixels): the same 49
skipped codes with a provably complete back buffer, the same +1 patches, the same scRGB near‑black skips. Two
Blackwell parts, two link types (HDMI direct / DP → adapter → Vertex), two present‑mode flavours, one result.

HDCP note: with an HDFury Vertex between the laptop's HDMI port and the UltraStudio, the RTX 5090 engaged
**HDCP 2.2** on the link (Vertex RX0 showed `2.2`) and the Vertex blanked its non‑HDCP output: the capture locked
to a valid 4K23.976 RGB 10‑bit PQ timing but every pixel was 0, desktop included. The RTX PRO 6000 → CAC‑1088 →
Vertex path had not engaged HDCP. Connecting the UltraStudio directly (a sink without HDCP cannot be encrypted
to) fixed it; the mode then had to be re‑applied (the UltraStudio EDID prefers 4K60, which the driver serves as
YCbCr 4:2:2 8 bpc — set 23.976 Hz first, then RGB 10 bpc). PROCEDURE.md §8 items 8–9.

### 日本語（実験 A 2026‑09‑11）

* §8 までの取り込みは Qt proto パターン（アプリ側で PQ 符号化 → FP16 テクスチャ → 2 倍バイリニア → 出力マージャの
  float→UNORM）で、バックバッファの読み戻しも無かった。対立的検証（攻撃 1）の「アプリの符号化が不均一でドライバは正確」
  を潰すため、`tools/hdr10_direct.cpp`（素の D3D11）で **整数コードを CPU 生成し `CopyResource` でバックバッファへ転写**、
  24 回に 1 回 Present 直前に読み戻してソースとバイト比較（**80/80 一致**）、ランプ行を CSV 化（飛び 0・段幅 4/5・欠落 0）。
* 環境: RTX PRO 6000（616.56・TB5 eGPU）→ DP 2.1 → CAC‑1088 → **HDFury Vertex（EDID 偽装）**→ HDMI → **UltraStudio 4K Mini**。
  信号 2160p23.98 RGB444 10bit PQ。PresentMon（管理者）並走 70 秒: 1563/1568 Present が Independent Flip（残り 5 件は
  ウィンドウ生成時、取り込み前）。
* 結果: **HDR10 は線上で同じ 49 コードが欠落**（`m25_hdr10` / `rog6000_hdr10` と欠落一覧が完全一致、段幅 4/5/9/10、
  線上−ソース {−1: 14, 0: 2475, +1: 1351}、平坦パッチ 16 点中 11 点が +1）。バックバッファには全コードがあったのだから、
  **量子化はスワップチェーンとリンクの間＝直接スキャンアウト経路**にある。アプリ側説は消えた。
* 対照 scRGB（同じ経路で FP16）は中間調の欠落ゼロ・線上−ソース {−1: 275, 0: 3565}。近黒 14 コード（≤135）の欠落は、
  表示側の FP16→PQ 再符号化が近黒で ±1 コード精度であることを離散ランプが露わにしたもの（連続ランプでは段幅の揺れとして
  吸収されていた。`m25_hdr10_composed` の近黒 23 欠落と同じ出口段）。「scRGB は丸め誤差以内」は「±1 コード以内」と読む。
* 同夜、同じ 2 本を **RTX 5090 Laptop**（8/30 の個体・616.56）でも実施（`data/a5090_*`）: ノートの HDMI 2.1 → UltraStudio 4K Mini
  直結（UltraStudio 自身の EDID）、2160p23.98 RGB444 10bit PQ、PresentMon 1564/1571（HDR10）・1505/1510（scRGB）が
  `Hardware: Independent Flip`（MPO 無しの方）。**取り込んだ全フレーム（3840×2160×3）が RTX PRO 6000 の結果と両スワップ
  チェーンともビット一致**（差 0 画素）＝同じ 49 欠落・同じ +1 パッチ・同じ scRGB 近黒欠落。Blackwell 2 個体・リンク 2 種・
  提示モード 2 種で同一。
* HDCP メモ: ノート HDMI → Vertex → UltraStudio の構成では RTX 5090 が **HDCP 2.2** を掛け（Vertex RX0 に `2.2`）、Vertex が
  非 HDCP 出力をブランクした（4K23.976 RGB 10bit PQ のタイミングは来るが全画素 0、デスクトップも黒）。RTX PRO → CAC‑1088 →
  Vertex では HDCP は掛からなかった。UltraStudio 直結で解消。直結後は UltraStudio の EDID が 4K60 優先のため YCbCr 4:2:2 8 bpc
  になるので、23.976 Hz → RGB 10 bpc の順に再設定（PROCEDURE.md §8 の 8〜9）。
* 機材メモ: CAC‑1088 は今回も直接では EDID を読めず（Non‑PnP 1024×768）、Vertex の EDID 偽装で解消。AKiTiO Node の
  DeckLink 4K Extreme 12G は TB5 ドック配下では PCI リソース未割当（0xC00000C0）、直結でもコード 43 を 2 回起こしたため
  UltraStudio 4K Mini を使用（PROCEDURE.md §8）。

## 日本語要約

* 環境: RTX 5090 Laptop（Studio 596.36）→ Vertex → PA32UCDM ＋ DeckLink 4K Extreme 12G。2160p23.98 RGB 4:4:4 10bit PQ。
* プロトパターン: scRGB は段幅 4/5 のみ・単調・パッチ ±1。HDR10 は段幅 4〜11・2 コード飛び 49・パッチに +1 多数。
* 実 YCbCr limited ランプ動画（ビューワ経由）: scRGB は期待コード ±1・段幅 2 px 一様。HDR10 は −1〜+2（≥2 が 112 px）・
  4 px 幅（コード飛び）37 段。
* 結論: 時間軸ディザは両経路とも無し。HDR10 全画面経路の量子化が不均一（NVIDIA フォーラムの RTX 50 報告と一致）。
  この機材・ドライバ範囲では scRGB を既定にするのが安全。
* 副次: Qt 6.11 は拡大率 300% の画面で HDR スワップチェーンを無言で SDR に落とす（`QT_ENABLE_HIGHDPI_SCALING=0` で回避）。
