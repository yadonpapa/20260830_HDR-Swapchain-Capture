# 外部報告の文案（2026-08-30）— NVIDIA フォーラム／Qt バグトラッカー

計測の一次データは `HDR表示の技術知見.md` 末尾、再現ツールは `tools/dither_capture.py` / `tools/dither_analyze.py` /
`tools/proto_hdr_view.py` / `tools/dxgi_outputs.cpp`。以下は英語の投稿用文案（コピーしてそのまま使える）。

---

## 1. NVIDIA Developer Forums（既存スレッドへの返信）

- 報告先: https://forums.developer.nvidia.com/t/uneven-banding-in-fullscreen-hdr-output-with-r10g10b10a2-swapchain/346429
  （初報スレッド https://forums.developer.nvidia.com/t/uneven-banding-in-fullscreen-hdr-output-with-r10g10b10a2-swapchain/343119
  にもリンクを付ける）。NVIDIA アカウントでログインして「Reply」。
- 添付候補: `outputs/dither/scrgb.npz` / `hdr10.npz` の要約表（本文に記載済み）、必要なら CSV 化した段幅分布。

```text
Subject: Quantitative confirmation with HDMI capture (RTX 5090 Laptop, Studio 596.36, PA32UCDM)

I can reproduce this and add measurements that isolate where the banding comes from.

Environment
- GPU: NVIDIA GeForce RTX 5090 Laptop GPU, Studio driver 596.36 (Windows 11 Pro 26200)
- Display: ASUS PA32UCDM via HDFury Vertex (EDID: 4K60 444 HDR BT.2020), 3840x2160 @ 23.976 Hz,
  RGB 4:4:4 10 bpc full range, Windows HDR on
- Capture: the same HDMI output split by the Vertex into a Blackmagic DeckLink 4K Extreme 12G HDMI input,
  captured as uncompressed 10-bit RGB 4:4:4 (r210). Detected input: 2160p23.98, RGB444 10-bit,
  HDR InfoFrame present (EOTF = PQ). So I am looking at the actual 10-bit codes on the wire, not at the panel.
- App: Qt Quick (D3D11 RHI) fullscreen window with either an R16G16B16A16_FLOAT (scRGB) or
  R10G10B10A2_UNORM (HDR10/PQ) swapchain, Independent Flip. The source image is the same FP16 texture.

Test pattern
- A horizontal ramp that is linear in PQ code space from 0 to 2000 nit (847 codes across 3840 px,
  i.e. ideally every code occupies 4.53 px), plus flat patches at 2 ... 2000 nit.
- 60 consecutive frames captured per configuration.

Results (row through the ramp; identical source for both)
| Swapchain            | Frame-to-frame changes (60 frames) | Step widths (ideal 4.53 px)         | 2-code jumps | Flat patches vs expected PQ code |
|----------------------|------------------------------------|--------------------------------------|--------------|----------------------------------|
| FP16 scRGB           | 0 pixels                           | only 4 or 5 px (std 0.50), monotonic | 0            | within ±1 code                   |
| R10G10B10A2 HDR10    | 0 pixels                           | 4 ... 11 px (std 1.27)               | 49           | many patches +1 (e.g. 20 nit -> 20.2, 600 -> 603.8, 2000 -> 2027.9 nit) |

Same trend with a real 10-bit YCbCr limited-range ramp video (code-linear Y, 2 px per code) decoded in
the app and shown fullscreen: scRGB output matches the source codes within ±1 with uniform 2-px steps;
the HDR10 swapchain output shows errors up to +2 codes and 37 steps of 4 px (skipped codes).

Observations
- Neither path shows temporal dithering (every one of the 60 frames is bit-identical), so the smoother look
  of the FP16 path is not dithering; the R10G10B10A2 path is simply quantised unevenly somewhere between
  the swapchain and the HDMI output.
- The FP16 -> PQ conversion done by the display pipeline is accurate (rounding only).
- Windows DXGI reports the output as DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, 10 bits/color.

So on this driver the HDR10 swapchain is not a pass-through of the 10-bit codes.

Practical takeaway (limited to what I measured: RTX 50 series, fullscreen Independent Flip, Windows 11)
- Until this is fixed, an R16G16B16A16_FLOAT (scRGB) swapchain gives measurably more accurate 10-bit output
  on the HDMI wire than R10G10B10A2 — the FP16 -> PQ conversion in the display pipeline is exact to rounding,
  while the 10-bit path adds uneven quantisation. For measurement/reference viewing apps that is the safer
  default on this hardware. I have not tested other GPU generations or drivers, so I would not generalise
  beyond RTX 50 + this driver range.

Reproduction package (MIT): test-pattern window, DeckLink capture/analysis scripts, the DeckLink wrapper source,
the step-by-step procedure and the captured ramp rows / patch tables as CSV are at
https://github.com/yadonpapa/20260830_HDR-Swapchain-Capture
Raw .npz captures available on request.
```

---

## 1b. NVIDIA フォーラムへの追補（2026-09-04・**投稿済み**）

- 前回返信（§1）への追加投稿。経路確認付き再計測＋対照計測の要点と、約 16 コード周期という
  新しい手掛かりを伝える。**2026-09-04 にフォーラムへ投稿済み**（以下は投稿文の控え）。

```text
Follow-up with new evidence: presentation-path-verified re-measurement, a newer driver, and a
composition control that isolates the defect to the direct scanout path.

1) Re-measurement with the presentation path recorded
My earlier measurement assumed Independent Flip but did not record it. I repeated it with PresentMon
running concurrently with every capture: every present of the pattern window during capture was
"Hardware: Independent Flip" (~24 presents/s, no drops). Environment changes on purpose:
- a second unit of the same laptop model (RTX 5090 Laptop GPU)
- driver 610.62 (previously Studio 596.36) - the issue persists on the newer driver
- a different capture device (Blackmagic UltraStudio 4K Mini, no HDFury in the chain; its HDMI-input
  EDID advertises HDR10 + RGB 10-bit itself), same signal: 2160p23.976 RGB 4:4:4 10 bpc full, PQ.
Result: identical numbers, including the SAME 49 two-code jumps in the PQ-linear ramp
(scRGB: only 4/5-px steps, monotonic, 0 jumps, patches within ±1 - also identical to before).

2) New observation: the skipped codes are quasi-periodic, roughly every 16 codes
(16, 32, 79, 112, 172, 189, 204, 220, 236, 251, 269, 285, 299, 315, ..., 838; full list in the repo,
data/m25_summary.json). This looks like segment boundaries of a piecewise-linear LUT in the scanout
path quantiser.

3) Composition control (same app, same swapchains, window deliberately occluded so PresentMon shows
"Composed: Flip" for every present):
- R10G10B10A2: the periodic mid-tone jumps disappear entirely. The only artifacts left are 23 skipped
  codes near black (all <= code 144: 1, 4, 9, 16, 37, ...), consistent with DWM converting the PQ
  swapchain into its FP16 linear canvas and the output stage re-encoding to PQ.
- FP16 scRGB: the captured ramp row is bit-identical between Independent Flip and composition.
This matches the "windowed looks clean" observations in this thread and pins the uneven quantisation
to the direct scanout path of the R10G10B10A2 fullscreen swapchain.

Updated data, procedure (including the PresentMon verification step) and the pattern tool are in the
same repository: https://github.com/yadonpapa/20260830_HDR-Swapchain-Capture (data/m25_*).
```

---

## 1c. NVIDIA フォーラムへの追補 2（2026-09-04・世代切り分け・**投稿済み**）

- §1b への追加投稿。Ampere（RTX 3070）で同計測を行い、報告の適用範囲を「RTX 50 系」に絞る根拠を伝える。
  **2026-09-04 にフォーラムへ投稿済み**（以下は投稿文の控え）。

```text
Generation control: the same measurement on an Ampere GPU (GeForce RTX 3070, desktop, driver 610.62).

Setup: RTX 3070 HDMI -> Blackmagic DeckLink 8K Pro G2 HDMI 2.1 input, 2160p23.976 RGB 4:4:4 10 bpc full,
PQ InfoFrame. The link was verified as 10 bpc three ways (DisplayConfig target mode 3840x2160 @ 23.976
with identity scaling, Windows Advanced Color bitsPerColorChannel = 10, DeckLink detection RGB444 10-bit),
and PresentMon ran concurrently: 716/716 presents "Hardware: Independent Flip" for both swapchains.

Result - Ampere behaves differently, and identically for FP16 scRGB and R10G10B10A2:
- every code on the wire is a multiple of 4 (an 8-bit lattice on the 10-bit link), and
- every non-black pixel toggles by exactly +-4 between frames (random spatio-temporal dither; no
  temporal or spatial correlation). The 60-frame time average matches the expected 10-bit PQ code to
  within one code over the whole ramp and all flat patches, a single frame does not.

So on Ampere the two-code jumps cannot be evaluated at all (there is no 10-bit lattice to skip codes
from). This narrows the uneven quantisation I reported to the true-10-bit direct scanout path of the
RTX 50 series; Ampere shows a different, unrelated behaviour (8-bit lattice + dither) that affects
scRGB and HDR10 alike. Data (ramp rows with per-pixel min/max/mean over 60 frames, patch table,
summary) are in the same repository under data/osaka3070_*.

Practical note for anyone reproducing with a DeckLink 8K Pro G2: with NVIDIA CP defaults (GPU
scaling) the 4K desktop is output as a 7680x4320 timing and the link drops to 8 bpc; "Perform scaling
on: Display" plus re-applying 10 bpc is needed to get the native 4K 10 bpc timing (PROCEDURE.md §7).
```

---

## 2. Qt バグトラッカー（新規 issue・**投稿済み 2026-09-04: [QTBUG-149927](https://bugreports.qt.io/browse/QTBUG-149927)**）

- 報告先: https://bugreports.qt.io/ （= https://qt-project.atlassian.net/ へリダイレクト）→ 「作成」→
  プロジェクト **Qt (QTBUG)**、コンポーネント **Qt RHI**（任意で **GUI: High-DPI** / **Quick: SceneGraph** を追加。
  "GUI: RHI" という名前は存在しないので注意）、影響するバージョン **6.11.0**、Platform/s **Windows**。
  Qt アカウント（無料）が必要。
- 再現コードとして `tools/proto_hdr_view.py`（PyQt6）を最小化して添付できる。C++ で求められたら
  QQuickWindow ＋ `QSG_RHI_HDR=scrgb` の最小例に置き換える。

```text
Summary: D3D11 RHI: scRGB/HDR10 swapchain silently falls back to SDR on an HDR screen with 300% display scaling (QT_ENABLE_HIGHDPI_SCALING=0 avoids it)

Environment
- Qt 6.11.0 (PyQt6 6.11.0), Windows 11 Pro 26200, D3D11 RHI backend (default)
- GPU: NVIDIA GeForce RTX 5090 Laptop GPU (driver 596.36). Multi-monitor:
  - \\.\DISPLAY6: 3840x2160, Windows scale 300%, HDR enabled (DXGI IDXGIOutput6::GetDesc1:
    ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 (12), BitsPerColor = 10)
  - \\.\DISPLAY1 (laptop panel, Intel adapter): HDR enabled, lower scale factor
  - \\.\DISPLAY5: SDR, primary

Steps
1. Set QSG_RHI_HDR=scrgb (or hdr10) before creating a QQuickWindow.
2. window.setScreen(<DISPLAY6 screen>); window.setGeometry(screen.geometry()); window.showFullScreen().
3. Enable qt.scenegraph.general.debug logging.

Actual
  "Requested a scRGB swapchain but it is reported to be unsupported with the current display(s).
   In multi-screen configurations make sure the window is located on a HDR-enabled screen.
   Request ignored, using SDR swapchain."
  The window is in fact on DISPLAY6 (verified with MonitorFromWindow on the native HWND). The content is
  then composed as SDR: values above SDR white saturate, PQ codes are treated as sRGB (verified by capturing
  the HDMI output with a DeckLink card).

Expected
  The HDR swapchain is created (DXGI reports the output as HDR).

What does / does not help
- QT_ENABLE_HIGHDPI_SCALING=0 -> the scRGB/HDR10 swapchain is created and works correctly.
- QT_D3D_ADAPTER_INDEX (0 = the adapter owning DISPLAY6, or others): no effect for THIS variant (see below
  for a second variant where it is the fix).
- Creating the native window first (QWindow::create()), showing normal then fullscreen, positioning at the
  screen centre, or moving the HWND with SetWindowPos to the monitor's physical rect before show: no effect.
- Same code on the laptop panel (HDR, small scale factor) works with high-DPI scaling enabled.

Second, independent way the same check fails (hybrid-GPU laptop; measured on another unit, driver 610.62)
- Topology: internal panel driven by the Intel iGPU (DXGI adapter 0), external HDMI screen wired directly
  to the NVIDIA dGPU (adapter 1) - the standard wiring on gaming laptops.
- With the default adapter, requesting scRGB/HDR10 on the HDMI screen produces the exact same debug log and
  silent SDR fallback, regardless of the scale factor (reproduces at 150%, and QT_ENABLE_HIGHDPI_SCALING=0
  does not help here). The HDR capability check appears to search for the window's screen only among the
  outputs of the adapter the device was created on, so a screen attached to a different adapter is always
  "unsupported". DXGI itself reports that output as ColorSpace = 12 (PQ/2020), BitsPerColor = 10.
- Workaround for this variant: QT_D3D_ADAPTER_INDEX=<index of the adapter that owns the target screen>
  (read per QRhi creation, so it can even be set per window right before creating it) -> the HDR swapchain
  is created and reports the correct HDR output info (maxLuminance etc.).

So the window->screen HDR capability check for the D3D11 swapchain fails in at least two independent ways
(high-DPI scaling of the target screen; screen owned by a different adapter), and in both cases the
fallback is silent (debug-level log only) while the app believes it is rendering HDR. A visible warning, or
ideally a correct check (physical-coordinate screen lookup; enumerate outputs across adapters, or create
the device on the adapter that owns the window's screen), would prevent silently wrong output.

Root cause analysis (from reading the dev branch; the code is unchanged since 2024-11, so dev/6.12+
should be equally affected - src/gui/rhi/qdxgihdrinfo.cpp):
- Variant 2 (adapter): QD3D11SwapChain::isFormatSupported() and createOrResize() call
  QDxgiHdrInfo(rhiD->activeAdapter).isHdrCapable(m_window). With an adapter set, QDxgiHdrInfo
  enumerates only that adapter's outputs, so a window on a screen owned by a different adapter can
  never match (its all-adapters path is only taken when no adapter is passed).
- Variant 1 (high-DPI): the window-to-output mapping does
      QRect wr = w->geometry();
      wr = QRect(wr.topLeft() * w->devicePixelRatio(), wr.size() * w->devicePixelRatio());
  i.e. it multiplies the window's position in Qt's global *logical* coordinate space by the
  devicePixelRatio. In a mixed-DPI multi-monitor layout the logical origin of a screen is not its
  physical origin divided by that screen's scale factor, so for a 300%-scaled secondary screen the
  computed center lands outside the screen's DXGI desktop rect and no output matches. QWindow's
  native (physical) geometry, or QPlatformScreen/MonitorFromWindow, would map correctly.
Reproducer (PyQt6 test-pattern window with --mode scrgb|hdr10 and --screen), the DXGI output dump tool and the
capture evidence are public at https://github.com/yadonpapa/20260830_HDR-Swapchain-Capture (tools/proto_hdr_view.py, tools/dxgi_outputs.cpp, docs/RESULTS.md §4).
```

---

## 3. ffmpeg

新規性のある報告は無し（swscale の端点誤差は Trac #979/#3801/#3785/#4805 で既知。7.1 の出力側色オプションによる
自動変換は commit a850f80e の意図的変更）。報告しない。

---

## 4. 日本語版（投稿用ではなく社内共有・記録用）

### 4.1 NVIDIA フォーラム返信の日本語版

```text
件名: HDMI キャプチャによる定量的な再現報告（RTX 5090 Laptop・Studio 596.36・PA32UCDM）

同じ現象を再現し、バンディングの発生箇所を切り分ける計測を行いました。

環境
- GPU: NVIDIA GeForce RTX 5090 Laptop GPU、Studio ドライバ 596.36（Windows 11 Pro 26200）
- 表示: ASUS PA32UCDM を HDFury Vertex 経由で接続（EDID: 4K60 444 HDR BT.2020）、3840x2160 @ 23.976 Hz、
  RGB 4:4:4 10 bpc フルレンジ、Windows の HDR 有効
- 取り込み: 同じ HDMI 出力を Vertex で分配し Blackmagic DeckLink 4K Extreme 12G の HDMI 入力へ。
  非圧縮 10bit RGB 4:4:4（r210）で取得。検出した入力: 2160p23.98、RGB444 10bit、HDR InfoFrame あり（EOTF=PQ）。
  つまりパネルの見え方ではなく、伝送路上の 10bit コードそのものを見ています。
- アプリ: Qt Quick（D3D11 RHI）の全画面ウィンドウ。スワップチェーンは R16G16B16A16_FLOAT（scRGB）または
  R10G10B10A2_UNORM（HDR10/PQ）、Independent Flip。元画像は同一の FP16 テクスチャ。

テストパターン
- PQ コード空間で 0→2000 nit に線形な横方向ランプ（3840 px に 847 コード＝理想は 1 コード 4.53 px）と、
  2〜2000 nit の平坦パッチ。
- 各条件で連続 60 フレームを取得。

結果（ランプを通る行。元は両者同一）
| スワップチェーン    | フレーム間変化（60 f） | 段幅（理想 4.53 px）             | 2 コード飛び | 平坦パッチ vs 期待 PQ コード |
|---------------------|------------------------|----------------------------------|--------------|------------------------------|
| FP16 scRGB          | 0 画素                 | 4 または 5 px のみ（std 0.50）・単調 | 0        | ±1 コード以内                |
| R10G10B10A2 HDR10   | 0 画素                 | 4〜11 px（std 1.27）             | 49 箇所      | +1 のパッチ多数（例 20 nit→20.2、600→603.8、2000→2027.9 nit） |

実際の 10bit YCbCr リミテッドレンジのランプ動画（コードリニアな Y、1 コード 2 px）をアプリで復号して全画面表示した
場合も同傾向: scRGB 出力は元コードと ±1 以内で一致し段幅 2 px 一様、HDR10 スワップチェーン出力は最大 +2 コードの
誤差と 4 px 幅（コード飛び）の段が 37 箇所。

所見
- どちらの経路にも時間軸ディザは無い（60 フレームすべてビット一致）。FP16 が滑らかに見えるのはディザのためではなく、
  R10G10B10A2 経路がスワップチェーン〜HDMI 出力の間のどこかで不均一に量子化されているため。
- 表示パイプラインの FP16→PQ 変換は正確（丸めのみ）。
- Windows の DXGI は当該出力を DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020・10 bit/color と報告。

したがって、このドライバでは HDR10 スワップチェーンは 10bit コードのパススルーになっていません。

実用上の結論（計測した範囲に限定: RTX 50 系・全画面 Independent Flip・Windows 11）
- 修正されるまでは、R16G16B16A16_FLOAT（scRGB）スワップチェーンの方が R10G10B10A2 より HDMI 上の 10bit 出力が
  計測上明らかに正確です。表示パイプラインの FP16→PQ 変換は丸め精度で正確なのに対し、10bit 経路は不均一な
  量子化を加えるためです。計測・リファレンス表示用途のアプリでは、このハードウェアでは scRGB を既定にするのが安全です。
  他世代の GPU や他ドライバは未検証なので、RTX 50 系＋このドライバ範囲以上には一般化しません。

再現パッケージ（MIT）: テストパターン表示、DeckLink 取り込み/解析スクリプト、DeckLink ラッパーのソース、手順書、
取り込んだランプ行とパッチ表の CSV を https://github.com/yadonpapa/20260830_HDR-Swapchain-Capture で公開しています。生の .npz は要望があれば提供します。
```

### 4.1b NVIDIA フォーラム追補の日本語版（記録用・2026-09-04）

- **経路確認付き再計測**: PresentMon を取り込みと同時刻に走らせ、取り込み中の全 Present が
  Hardware: Independent Flip であることを記録。同一機種の別個体・ドライバ 610.62（旧 596.36）・
  別キャプチャ機（UltraStudio 4K Mini・HDFury 無し）で **2 コード飛び 49 箇所まで同一の結果**
  ＝新ドライバでも継続。
- **新知見**: 欠落コードは約 16 コード周期の準周期（16, 32, 79, 112, …, 838）
  ＝スキャンアウト段の区分線形 LUT のセグメント境界を示唆。
- **対照計測**（Composed: Flip 確認付き）: HDR10 の中間調周期飛びは合成で**全消滅**（残るのは
  近黒 ≤144 の 23 コード欠落＝DWM の PQ→FP16→PQ 往復）・scRGB は iFlip と**完全ビット一致**。
  ＝不均一は R10G10B10A2 全画面の**直接スキャンアウト経路に固有**（「ウィンドウ表示では消える」
  報告と整合）。

### 4.1c NVIDIA フォーラム追補 2 の日本語版（記録用・2026-09-04・投稿済み）

- 世代切り分け: RTX 3070（Ampere・610.62）→ DeckLink 8K Pro G2 HDMI 2.1 入力、2160p23.976 RGB 10bpc PQ。
  リンク 10bpc を DisplayConfig / Windows ACI2 / DeckLink 検出の 3 点で確認、PresentMon 716/716 Independent Flip。
- 結果: scRGB / HDR10 とも全コードが 4 の倍数（8bit 格子）＋黒以外の全画素が毎フレーム ±4 で揺れるランダム
  時空間ディザ。60 フレーム平均は期待 10bit PQ コードに ±1 未満で一致、単一フレームは一致しない。
- 含意: Ampere では 2 コード飛びは評価不能＝報告した不均一量子化は RTX 50 系の真 10bit 直接出力に固有。
  Ampere は別種の挙動（8bit 格子＋ディザ・scRGB/HDR10 共通）。データは data/osaka3070_*。
- 再現者向け注意: 8K Pro G2＋NVIDIA CP 既定（GPU スケーリング）だと 4K デスクトップが 8K タイミングで出て 8bpc に
  降格。「実行デバイス: ディスプレイ」＋10 bpc 再適用で 4K 10bpc ネイティブ（PROCEDURE.md §7）。

投稿文（§1c）の全文対訳:

```text
世代切り分け: 同じ計測を Ampere 世代の GPU（GeForce RTX 3070・デスクトップ・ドライバ 610.62）で実施しました。

構成: RTX 3070 の HDMI → Blackmagic DeckLink 8K Pro G2 の HDMI 2.1 入力、2160p23.976 RGB 4:4:4 10 bpc フル、
PQ InfoFrame。リンクが 10 bpc であることは 3 つの方法で確認しました（DisplayConfig の target モードが
3840x2160 @ 23.976 で identity スケーリング、Windows Advanced Color の bitsPerColorChannel = 10、
DeckLink の検出が RGB444 10-bit）。PresentMon は取り込みと同時に走らせ、両スワップチェーンとも
716/716 の Present が "Hardware: Independent Flip" でした。

結果 — Ampere は挙動が異なり、しかも FP16 scRGB と R10G10B10A2 で同一でした:
- 伝送路上の全コードが 4 の倍数（10 bit リンク上の 8 bit 格子）。
- 黒以外の全画素がフレーム間でちょうど ±4 だけ入れ替わる（ランダムな時空間ディザ。時間方向・
  空間方向とも相関なし）。60 フレームの時間平均は、ランプ全域と全パッチで期待 10 bit PQ コードに
  1 コード未満で一致しますが、単一フレームでは一致しません。

したがって Ampere では 2 コード飛びをそもそも評価できません（コードを飛ばす元になる 10 bit の格子が
存在しないため）。これにより、私が報告した不均一な量子化は RTX 50 系の真 10 bit 直接スキャンアウト経路に
絞られます。Ampere は別種の無関係な挙動（8 bit 格子＋ディザ）を示し、それは scRGB と HDR10 に等しく
影響します。データ（60 フレームの画素別 min/max/mean 付きランプ行、パッチ表、要約）は同じリポジトリの
data/osaka3070_* にあります。

DeckLink 8K Pro G2 で再現する方への実務的な注意: NVIDIA コントロールパネル既定（GPU スケーリング）では
4K デスクトップが 7680x4320 のタイミングで出力され、リンクが 8 bpc に落ちます。4K 10 bpc のネイティブ
タイミングを得るには「スケーリングを実行するデバイス: ディスプレイ」と 10 bpc の再適用が必要です
（PROCEDURE.md §7）。
```

### 4.2 Qt バグ報告の日本語版

```text
概要: D3D11 RHI: 拡大率 300% の HDR 画面で scRGB/HDR10 スワップチェーンが無言で SDR にフォールバックする（QT_ENABLE_HIGHDPI_SCALING=0 で回避可）

環境
- Qt 6.11.0（PyQt6 6.11.0）、Windows 11 Pro 26200、D3D11 RHI バックエンド（既定）
- GPU: NVIDIA GeForce RTX 5090 Laptop GPU（ドライバ 596.36）。マルチモニタ:
  - \.\DISPLAY6: 3840x2160、Windows 拡大率 300%、HDR 有効（DXGI IDXGIOutput6::GetDesc1:
    ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 (12)、BitsPerColor = 10）
  - \.\DISPLAY1（ノート内蔵パネル・Intel アダプタ）: HDR 有効、拡大率は小さい
  - \.\DISPLAY5: SDR、プライマリ

手順
1. QQuickWindow を作る前に QSG_RHI_HDR=scrgb（または hdr10）を設定。
2. window.setScreen(<DISPLAY6 の画面>); window.setGeometry(screen.geometry()); window.showFullScreen()。
3. qt.scenegraph.general.debug のログを有効化。

実際の動作
  "Requested a scRGB swapchain but it is reported to be unsupported with the current display(s).
   In multi-screen configurations make sure the window is located on a HDR-enabled screen.
   Request ignored, using SDR swapchain."
  ウィンドウは実際には DISPLAY6 上にある（ネイティブ HWND に対する MonitorFromWindow で確認）。以後の内容は SDR として
  合成される: SDR 白を超える値は飽和し、PQ コードは sRGB として解釈される（DeckLink で HDMI 出力を取り込んで確認）。

期待する動作
  HDR スワップチェーンが作成される（DXGI は当該出力を HDR と報告している）。

効いたもの／効かなかったもの
- QT_ENABLE_HIGHDPI_SCALING=0 → scRGB/HDR10 スワップチェーンが作成され正しく動作する。
- QT_D3D_ADAPTER_INDEX（DISPLAY6 を持つアダプタ 0、その他）: この変種には効果なし（下の第 2 変種では回避策になる）。
- ネイティブウィンドウの先行生成（QWindow::create()）、通常表示→全画面、画面中央への配置、
  表示前に SetWindowPos でモニタの物理矩形へ HWND を移動: いずれも効果なし。
- 同じコードはノート内蔵パネル（HDR・拡大率小）では高 DPI スケーリング有効のまま動作する。

同じ判定が独立に壊れる第 2 変種（ハイブリッド GPU ノート・別個体・ドライバ 610.62 で実測）
- 構成: 内蔵パネル＝Intel iGPU（DXGI アダプタ 0）・HDMI 画面＝NVIDIA dGPU 直結（アダプタ 1）
  ＝ゲーミングノートの標準配線。
- 既定アダプタのまま HDMI 画面へ scRGB/HDR10 を要求すると、拡大率と無関係に（150% でも・
  QT_ENABLE_HIGHDPI_SCALING=0 でも）同じログで無言 SDR 降格。HDR 可否判定がデバイス作成先アダプタの
  出力一覧の中でしかウィンドウの画面を探さないため、別アダプタ所属の画面は常に「非対応」になる。
  DXGI 自体は当該出力を ColorSpace=12（PQ/2020）・10bit と正しく報告している。
- この変種の回避策: QT_D3D_ADAPTER_INDEX=<対象画面を所有するアダプタ>（QRhi 生成のたびに読まれるので
  ウィンドウ生成直前の設定で per-window に効く）→ HDR スワップチェーン成立・HDR output info も正しい。

したがって D3D11 スワップチェーンの「ウィンドウ→画面」HDR 可否判定は少なくとも 2 通り（対象画面の高 DPI
スケーリング／別アダプタ所属の画面）で独立に壊れており、いずれもフォールバックは無言（debug レベルのログのみ）で、
アプリは HDR を描いているつもりのまま誤った出力になります。可視の警告か、正しい判定（物理座標での画面照合・
アダプタ横断の出力列挙、またはウィンドウの画面を所有するアダプタでのデバイス作成）を希望します。

原因箇所の分析（dev ブランチのソースを確認。2024-11 以降機能変更なし＝dev/6.12+ も同罪のはず。
src/gui/rhi/qdxgihdrinfo.cpp）:
- 変種 2（アダプタ）: QD3D11SwapChain::isFormatSupported() / createOrResize() は
  QDxgiHdrInfo(rhiD->activeAdapter).isHdrCapable(m_window) と自分のアダプタを渡して構築する。
  アダプタ指定ありの QDxgiHdrInfo は**そのアダプタの出力しか列挙しない**ため、別アダプタ所属の
  画面上のウィンドウは決して一致しない（全アダプタ走査はアダプタ未指定時のみ）。
- 変種 1（高 DPI）: 窓→出力の照合が
      QRect wr = w->geometry();
      wr = QRect(wr.topLeft() * w->devicePixelRatio(), wr.size() * w->devicePixelRatio());
  ＝ Qt の**グローバル論理座標の位置に devicePixelRatio を単純乗算**している。混在 DPI の
  マルチモニタでは画面の論理原点は「物理原点 ÷ その画面のスケール」にならないため、300% の
  サブ画面では計算された中心が DXGI のデスクトップ矩形の外に落ちて一致しない。ネイティブ
  （物理）ジオメトリか MonitorFromWindow を使えば正しく照合できる。
再現コード（PyQt6 のテストパターン表示 --mode scrgb|hdr10 / --screen）、DXGI 出力ダンプツール、取り込みの証拠は
https://github.com/yadonpapa/20260830_HDR-Swapchain-Capture で公開しています（tools/proto_hdr_view.py、tools/dxgi_outputs.cpp、docs/RESULTS.md §4）。
```
