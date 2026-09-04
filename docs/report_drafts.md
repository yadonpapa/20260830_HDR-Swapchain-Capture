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

## 2. Qt バグトラッカー（新規 issue）

- 報告先: https://bugreports.qt.io/ → 「Create」→ Project **Qt (QTBUG)**、Component **GUI: RHI**（または
  **Quick: SceneGraph**）、Affects Version **6.11.0**、Platform **Windows**。Qt アカウント（無料）が必要。
- 再現コードとして `tools/proto_hdr_view.py`（PyQt6）を最小化して添付できる。C++ で求められたら
  QQuickWindow ＋ `QSG_RHI_HDR=scrgb` の最小例に置き換える。

```text
Summary: D3D11 RHI: HDR swapchain (scRGB/HDR10) reported unsupported on an HDR screen with 300% scaling
         unless QT_ENABLE_HIGHDPI_SCALING=0

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
- QT_D3D_ADAPTER_INDEX (0 = the adapter owning DISPLAY6, or others): no effect.
- Creating the native window first (QWindow::create()), showing normal then fullscreen, positioning at the
  screen centre, or moving the HWND with SetWindowPos to the monitor's physical rect before show: no effect.
- Same code on the laptop panel (HDR, small scale factor) works with high-DPI scaling enabled.

So the HDR capability check in the D3D11 swapchain (isFormatSupported / output lookup for the window)
seems to break under high-DPI scaling of the target screen, and the fallback is silent (debug-level log only).
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

### 4.2 Qt バグ報告の日本語版

```text
概要: D3D11 RHI: 拡大率 300% の HDR 画面で HDR スワップチェーン（scRGB/HDR10）が「未対応」と判定される
      （QT_ENABLE_HIGHDPI_SCALING=0 のときだけ作成できる）

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
- QT_D3D_ADAPTER_INDEX（DISPLAY6 を持つアダプタ 0、その他）: 効果なし。
- ネイティブウィンドウの先行生成（QWindow::create()）、通常表示→全画面、画面中央への配置、
  表示前に SetWindowPos でモニタの物理矩形へ HWND を移動: いずれも効果なし。
- 同じコードはノート内蔵パネル（HDR・拡大率小）では高 DPI スケーリング有効のまま動作する。

したがって D3D11 スワップチェーンの HDR 可否判定（isFormatSupported／ウィンドウに対する出力の照合）が対象画面の高 DPI
スケーリング下で壊れており、しかもフォールバックは無言（debug レベルのログのみ）です。
再現コード（PyQt6 のテストパターン表示 --mode scrgb|hdr10 / --screen）、DXGI 出力ダンプツール、取り込みの証拠は
https://github.com/yadonpapa/20260830_HDR-Swapchain-Capture で公開しています（tools/proto_hdr_view.py、tools/dxgi_outputs.cpp、docs/RESULTS.md §4）。
```
