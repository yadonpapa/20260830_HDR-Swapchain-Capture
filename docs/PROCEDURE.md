# Measurement procedure / 計測手順

## English

### 0. Signal chain

```
GPU HDMI ──► HDFury Vertex (EDID: copy of the monitor, pass-through) ──┬──► ASUS PA32UCDM
                                                                        └──► DeckLink 4K Extreme 12G, HDMI in
```

The capture card sees exactly the bytes the monitor sees. Everything below is about making sure the GPU
emits RGB 4:4:4 10‑bit PQ and that the window really has an HDR swapchain.

### 1. GPU / Windows settings

1. NVIDIA Control Panel → *Change resolution* → **3840×2160 @ 23.976 Hz** (or 1080p60). 2160p60 RGB 10‑bit
   does not fit HDMI 2.0 and the driver silently drops to 4:2:2 or 8 bpc.
2. Same page → *Use NVIDIA color settings*: **RGB, 10 bpc, Full**.
3. Windows Settings → Display → select the captured screen → **Use HDR: On**. Check with
   `uv run python tools/proto_hdr_view.py --list` (must show *HDR enabled*) and, for the DXGI view,
   build and run `tools/dxgi_outputs.cpp` (`ColorSpace=12` = PQ/2020).
4. HDFury: EDID = copy of the monitor (must contain the HDR static metadata block), all colour processing off.

### 2. Capture card

* Build `decklink_core` (see its README), copy `rawdecklink_core.dll` to `bin/`.
* `uv run python tools/dither_capture.py --list` → note the device index (DeckLink 4K Extreme 12G).
* A first probe: `... --device N --frames 3 --skip 10 --out outputs/probe.npz`. The *入力:* line must read
  `RGB444+10bit hdr_present=1 eotf=2` (PQ). If it says YCbCr422 or 8bit, fix step 1.

### 3. Pattern + capture

```powershell
$env:QT_ENABLE_HIGHDPI_SCALING = "0"      # !! see RESULTS.md — otherwise Qt may give you an SDR swapchain
Start-Process uv -ArgumentList "run python tools/proto_hdr_view.py --mode scrgb --screen <idx> --auto-close 60"
uv run python tools/dither_capture.py --device <N> --frames 60 --skip 10 --wait 12 --roi 0,1800,3840,8 --out outputs/scrgb.npz --label scrgb
# wait for the window to close, then the same with --mode hdr10 → outputs/hdr10.npz
uv run python tools/dither_analyze.py outputs/scrgb.npz outputs/hdr10.npz
```

* `--wait 12` gives the window time to appear; capturing immediately after `showFullScreen()` can return a
  black first frame.
* ROI `0,1800,3840,8`: rows 1800–1807 cross the ramp band (pattern rows 800–1040 scaled ×2).
* The ramp is linear in PQ code (0 → code of 2000 nit = 847 codes over 3840 px). Ideal output: every code
  occupies 4 or 5 px, adjacent differences are 0 or 1, no frame‑to‑frame change unless dithering is applied.

### 4. Real video instead of the synthetic pattern

`uv run python tools/make_ramp_y4m.py outputs/ramp.y4m` creates a 10‑bit limited‑range YCbCr ramp
(Y 64 → 959, 1 code per 2 px). Show it with any HDR viewer that interprets it as PQ and captures ROI
`0,536,3840,8`; expected full‑range code = round((Y − 64) / 876 × 1023).

### 5. Reading the numbers

`dither_analyze.py` prints, per capture: pixels that changed between frames (temporal dither),
adjacent‑difference values (monotonicity, code skips), step‑width histogram (uniformity), and the mean
fractional part (non‑integer means would indicate temporal dithering).

---

## 日本語

### 0. 信号経路

GPU の HDMI → HDFury Vertex（EDID はモニタのコピー・パススルー）→ PA32UCDM と DeckLink 4K Extreme 12G の HDMI 入力へ分配。
キャプチャカードはモニタと同じバイト列を受けます。以下は「GPU が RGB 4:4:4 10bit PQ を出していること」と
「ウィンドウが本当に HDR スワップチェーンを持っていること」を確認する手順です。

### 1. GPU / Windows 設定

1. NVIDIA コントロールパネル → 解像度: **3840×2160 @ 23.976 Hz**（または 1080p60）。2160p60 の RGB 10bit は HDMI 2.0 に
   入らず、ドライバが黙って 4:2:2 か 8bpc に落とします。
2. 同ページ → NVIDIA のカラー設定: **RGB・10 bpc・フル**。
3. Windows 設定 → ディスプレイ → 取り込む画面を選び **HDR を使用する: オン**。
   `uv run python tools/proto_hdr_view.py --list` で「HDR 有効」を確認。DXGI 側は `tools/dxgi_outputs.cpp` をビルドして実行
   （`ColorSpace=12` = PQ/2020）。
4. HDFury: EDID はモニタのコピー（HDR 静的メタデータブロック必須）、色処理はすべてオフ。

### 2. キャプチャカード

* `decklink_core` をビルド（README 参照）し `rawdecklink_core.dll` を `bin/` へ。
* `uv run python tools/dither_capture.py --list` でデバイス index を確認。
* 試し取り: `... --device N --frames 3 --skip 10 --out outputs/probe.npz`。「入力:」行が `RGB444+10bit hdr_present=1 eotf=2`
  であること。YCbCr422 や 8bit なら手順 1 を見直す。

### 3. パターン表示と取り込み

上の PowerShell と同じ。`QT_ENABLE_HIGHDPI_SCALING=0` を必ず付ける（付けないと拡大率の大きい画面で Qt が SDR に落ちる）。
`--wait 12` は表示直後の黒フレーム対策。ROI `0,1800,3840,8` はランプ帯（パターン行 800〜1040 の 2 倍拡大）を横切る行。
理想は「各コードが 4〜5 px・隣接差分 0/1・フレーム間変化なし」。

### 4. 合成動画

`tools/make_ramp_y4m.py` で 10bit limited の Y ランプ（64→959・1 コード 2 px）の y4m を作れます。PQ として解釈する
ビューワで表示し ROI `0,536,3840,8` を取り込み、期待値 round((Y−64)/876×1023) と比較します。

### 5. 数値の読み方

`dither_analyze.py` はフレーム間で変化した画素数（時間軸ディザ）、隣接差分の種類（単調性・コード飛び）、段幅の分布
（均一性）、平均値の小数部（時間軸ディザがあれば非整数）を出します。
