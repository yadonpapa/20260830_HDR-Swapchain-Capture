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

### 6. Verifying the presentation path (added 2026‑09‑04)

The results depend on the window being on the **direct scanout path (Independent Flip)** — DWM composition
changes the numbers (see RESULTS.md §6). Record it instead of assuming it:

1. Show the pattern with `--present-loop`. A static Qt Quick scene **stops presenting** after a few frames
   and produces no PresentMon rows at all; the flag requests `update()` every frame (image unchanged).
2. Run [PresentMon](https://github.com/GameTechDev/PresentMon) **concurrently with the capture**
   (needs admin or membership in the *Performance Log Users* group):

   ```powershell
   PresentMon-x64.exe --output_file pm.csv --timed 25 --terminate_after_timed --session_name pm1 --no_console_stats
   ```

   Then check that every row of the presenting python process says `Hardware: Independent Flip`
   (`Composed: Flip` = DWM composition; re‑do the capture).
3. Pitfalls:
   * If you kill PresentMon instead of letting `--terminate_after_timed` end it, the ETW session leaks and
     **later sessions record nothing**. `logman query -ets`, then `logman stop <session_name> -ets`.
   * Do not touch mouse/keyboard during the capture. Focus moving to a window on *another* screen did not
     demote the window in our tests, but occlusion by any window on the *same* screen does — and DPI‑unaware
     helper windows get virtualised ×2–3 on high‑scaling screens and can occlude far more than intended.
   * On hybrid‑GPU laptops pin the pattern window to the adapter that owns the captured output
     (`QT_D3D_ADAPTER_INDEX=<idx>`), otherwise Qt may silently fall back to an SDR swapchain
     (same family of problems as the 300 % scaling issue in RESULTS.md §4).
4. No HDFury needed with capture devices whose HDMI‑input EDID advertises HDR itself (e.g. UltraStudio
   4K Mini: HDR10 + RGB 10‑bit). Notes: Windows disables HDR while displays are duplicated (extend instead);
   changing the colour format in NVIDIA CP can silently reset the refresh rate back to 60 Hz (re‑check
   that the mode is still 4K ≤ 30 Hz RGB 10 bpc, e.g. with a probe capture).

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

### 6. プレゼンテーション経路の確認（2026‑09‑04 追加）

結果は「**直接スキャンアウト（Independent Flip）か DWM 合成か**」で変わります（RESULTS.md §6）。推測せず記録します。

1. パターン表示に `--present-loop` を付ける（静止シーンの Qt Quick は数フレームで Present を止め、
   PresentMon に**行が一切出なくなる**。このフラグは毎フレーム update() を要求する＝絵は不変）。
2. [PresentMon](https://github.com/GameTechDev/PresentMon) を**取り込みと同時刻**に走らせる
   （管理者か Performance Log Users グループ所属が必要）。上の英語欄のコマンド例を参照。
   表示プロセスの全行が `Hardware: Independent Flip` であること（`Composed: Flip` なら合成＝取り直し）。
3. 落とし穴: PresentMon を強制終了すると ETW セッションが残骸化して**以後の記録が 0 件**になる
   （`logman query -ets` → `logman stop <名前> -ets` で清掃）。取り込み中は無操作（別画面への
   フォーカス移動だけでは落ちなかったが、**同一画面上の遮蔽**で合成に落ちる。DPI 非対応の補助窓は
   高拡大率画面で 2〜3 倍に仮想化拡大されて予定外の領域を隠す）。ハイブリッド GPU ノートは
   `QT_D3D_ADAPTER_INDEX` で取り込み画面の所有アダプタを明示（さもないと SDR に無言降格し得る）。
4. HDMI 入力の EDID が HDR を宣言するキャプチャ機（例: UltraStudio 4K Mini＝HDR10＋RGB 10bit）なら
   HDFury は不要。ただし複製表示中は Windows が HDR を無効化（拡張にする）・NVIDIA CP の色形式変更で
   リフレッシュが 60 Hz に戻ることがある（probe 取り込みで 4K ≤30 Hz RGB 10bpc を再確認）。
