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

### 7. DeckLink 8K Pro G2 HDMI input + GeForce: get a *native* 4K 10 bpc timing (added 2026‑09‑04)

The 8K Pro G2's HDMI‑input EDID has a 1920×1200 preferred timing plus 8K VICs (194–199). With NVIDIA's defaults
this combination silently gives you the wrong signal:

1. **GPU scaling to 8K.** NVIDIA CP → *Adjust desktop size and position* defaults to *Full‑screen* scaling
   *performed on the GPU*: the 3840×2160 desktop is output as a **7680×4320 @ 23.976 timing** (DisplayConfig:
   source 3840×2160, target 7680×4320, scaling STRETCHED). The DeckLink detects `4320p23.98`, the frame is
   133 MB (hence the 160 MB capture buffer), the picture went through the GPU scaler, and the link **drops to
   8 bpc** (8K24 RGB 10‑bit does not fit). *No scaling* alone only centres the 4K picture in the 8K timing
   (CENTERED, still 8 bpc). Set **"Perform scaling on: Display"** — then the GPU sends 3840×2160 total
   5500×2250 @ 23.976 (296.7 MHz) directly. `SetDisplayConfig` from Windows (IDENTITY, explicit target mode)
   is overridden by the NVIDIA driver and does not help.
2. **Re‑apply the colour depth.** After the scaling change the panel still showed 10 bpc but the link was
   8 bpc (Windows Advanced Color `bitsPerColorChannel = 8`, DeckLink `RGB444+8bit`). Select 8 bpc → Apply →
   10 bpc → Apply. Verify with *both* the Windows probe (`tools/hdr_display.py`) and the DeckLink detection
   flags in the capture log — codes that are all multiples of 4 mean an 8‑bit link (or, on Ampere, the
   GPU's own 8‑bit lattice, see RESULTS.md §7).
3. The refresh‑rate box on the *Adjust desktop size and position* page is a **preview** control (it may show
   120 Hz / 30 Hz); the applied mode is on *Change resolution* and in DisplayConfig.
4. **Secondary taskbar.** If you operate other windows (e.g. NVIDIA CP) during the run, the taskbar on the
   captured screen stays on top and every present becomes `Composed: Flip`; `--foreground`
   (`SwitchToThisWindow`) cannot win against the foreground lock. Hands off during the capture.
5. EDID facts (8K Pro G2 HDMI in): HDMI VSDB DC_30/36bit + DC_Y444, max TMDS 300 MHz; HF‑VSDB max TMDS 600 MHz,
   SCDC, FRL; colorimetry BT2020 RGB/YCC; HDR static metadata SDR/HDR/PQ (no HLG); 4:2:0 only for 8K 48/50/60.
   4K24 RGB 10‑bit (TMDS 371 MHz) is within the sink's capabilities — the 8 bpc above is the driver's choice.

### 8. DisplayPort‑only GPU into an HDMI capture card: DP → HDMI active adapters (added 2026‑09‑08)

The RTX PRO 6000 Blackwell has DisplayPort 2.1 outputs only. What worked and what did not
(`data/rog6000_*`, RESULTS.md §8):

1. **Use an adapter that really carries 10 bpc.** With an older HDMI‑2.0‑era DP→HDMI adapter the NVIDIA CP
   *Output color depth* list contained **8 bpc only** whatever the desktop colour depth (the adapter's DPCD
   advertises 8 bpc). Worse, that adapter (a) dropped the HDR InfoFrame (`hdr_present=0`), (b) told the sink
   "RGB 4:4:4 10‑bit deep colour" regardless of the GPU setting (DeckLink kept detecting `RGB444+10bit` even
   with the link at 8 bpc, and after re‑plugging), and (c) **scrambled the pixel packing**: a flat patch arrived
   as five distinct values repeating every 10 px, rotated across R/G/B (only 0 and full‑scale survived). Such
   captures are unusable at 10 *and* 8 bpc. The **Club3D CAC‑1088** (DP 1.4 → HDMI 2.1, Synaptics, bus‑powered,
   no DSC needed at 4K23.976 RGB 10‑bit) was **bit‑exact** against the direct‑HDMI captures (`m25_*`).
2. **If the resolution/refresh list is locked, the adapter did not read the sink's EDID.** Symptom: monitor name
   `Non‑PnP`, `tools/hdr_display.py` shows no HDR support, the registry EDID under
   `HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\SYN3000\…` is the adapter's 128‑byte fallback (preferred
   1024×768, no CEA extension), the mode list is 1080p60 + VGA modes, and the DeckLink reports *no input
   signal* (the adapter never starts transmitting). Re‑plugging the HDMI end and reversing the plug‑in order did
   not help; **a different HDMI cable did** (DDC/EDID read failed on the first cable although the DeckLink's
   EDID itself is valid — checksums OK, CEA block with 4K VICs). After the fix the monitor name is `BMD HDMI`
   and the 4K/23.976 Hz/10 bpc entries appear.
3. **Verify all three again after any cable/adapter change**: `tools/hdr_display.py` (`bits/ch=10`, HDR on),
   the NVIDIA CP *Change resolution* page, and the DeckLink detection line of a probe capture
   (`2160p23.98 … RGB444+10bit hdr_present=1 eotf=2`).
4. PresentMon 2.5.1 labels the independent‑flip presents on this output `Hardware Composed: Independent Flip`
   (MPO plane); treat it like `Hardware: Independent Flip`. The first few presents at window creation are
   `Composed: Flip` and precede the capture (`--wait 12`).
5. Side issue seen on the eGPU host: the DeckLink 4K Extreme 12G (PCIe, in the enclosure) came up as
   *code 43* after a sleep/resume; **Disable → Enable** in Device Manager (admin) restored it without a reboot.
   Check `Get-PnpDevice -PresentOnly | ? FriendlyName -match DeckLink` before blaming the signal chain.
6. **AKiTiO Node behind a Thunderbolt dock / TB5 enclosure = card without PCI resources** (2026‑09‑11). With the Node
   daisy‑chained behind the Ugreen TB5 dock the DeckLink 4K Extreme 12G enumerated (PCIe Gen2 ×4 link up) but got
   **no memory range and no IRQ**; the driver failed with code 10 / `0xC00000C0` (STATUS_DEVICE_DOES_NOT_EXIST), and
   Disable → Enable did not help (the bridge window is sized at boot). Connecting the Node **directly** to the laptop's
   TB port fixed it — for a while: the card then dropped to code 43 twice within minutes (after Disable → Enable it
   came back as code 10 without resources again). Diagnose with `Get-PnpDeviceProperty … DEVPKEY_Device_ProblemStatus`
   and `Win32_PnPAllocatedResource`; if the Node is flaky, use the UltraStudio 4K Mini (Thunderbolt, no PCIe slot),
   which was stable all evening.
7. **CAC‑1088 → capture device EDID read failed again** (2026‑09‑11; a new HDMI cable / plug order did not help this
   time). Putting an **HDFury Vertex** between the adapter and the capture device (EDID emulation with HDR static
   metadata) made the output enumerate as a 3840×2160 10‑bit HDR display immediately. The Vertex's EDID carries
   MaxCLL 4000 / MaxFALL 400, which shows up in the DeckLink/UltraStudio InfoFrame detection — harmless.
8. **HDCP: a Vertex between a GPU's native HDMI port and a capture device can blank the picture** (2026‑09‑11). The RTX
   5090 Laptop's HDMI transmitter engaged **HDCP 2.2** with the Vertex (Vertex GUI: `RX0: … 2.2`), and the Vertex cannot
   pass decrypted video to a sink without HDCP, so its TX to the UltraStudio carried a valid 4K23.976 RGB 10‑bit PQ
   timing with **every pixel 0** (desktop included — check a probe frame for non‑zero pixels, not just the detection
   line). The RTX PRO 6000 → CAC‑1088 → Vertex path did not engage HDCP. Fix: connect the capture device **directly**
   (a sink that does not advertise HDCP cannot be encrypted to); the UltraStudio's own EDID advertises HDR10 + 10‑bit.
9. After re‑plugging a capture device with a 4K60‑preferred EDID (UltraStudio) the driver comes up at **4K60 YCbCr 4:2:2
   8 bpc**; re‑apply **23.976 Hz first, then RGB 10 bpc** (NVIDIA CP), and re‑probe (`RGB444+10bit`, Windows
   `bits/ch=10`). If NVIDIA CP opens on the capture display (invisible), move its window with `SetWindowPos`
   from PowerShell (process `nvcplui`) instead of guessing blind.

### 9. Experiment A — integer codes written to the back buffer without a shader (added 2026‑09‑11; capture still to be run)

Why: in every capture so far the PQ codes in the R10G10B10A2 swapchain were produced *by the application*
(CPU PQ encode → FP16 texture → bilinear ×2 magnification in Qt's texture node → the output merger's
float→UNORM conversion), and the back buffer was never read back. A reviewer can therefore still say "the
app's encoding is uneven, the driver is fine" (`docs/hdr-swapchain-adversarial-review-20260911.md`, attack 1).
`tools/hdr10_direct.cpp` removes every one of those stages: the 10‑bit codes are computed on the CPU as
integers, placed in a DEFAULT texture and copied into the back buffer with `CopyResource`; before every
N‑th `Present` the back buffer is copied to a staging texture and compared byte for byte with the source,
and the ramp row is written to a CSV. What enters the display pipeline is then proven, not assumed.

1. Build (MinGW‑w64, same as `dxgi_outputs.cpp`):
   `g++ -std=c++17 -O2 tools/hdr10_direct.cpp -o tools/hdr10_direct.exe -ld3d11 -ldxgi -lole32 -luser32 -lgdi32`
2. `tools\hdr10_direct.exe --list` → the display index of the capture input (`BMD HDMI` / UltraStudio:
   3840×2160, `bits=10`, `ColorSpace=12`). The D3D device is created on the adapter that owns that output,
   so no `QT_D3D_ADAPTER_INDEX` is needed on hybrid‑GPU machines.
3. Pattern + capture + PresentMon (PresentMon needs an elevated shell, see §6):

   ```powershell
   Start-Process tools\hdr10_direct.exe -ArgumentList "--display <k> --mode hdr10 --seconds 70 --readback outputs\exp_a\a_hdr10_backbuffer.csv" -RedirectStandardOutput outputs\exp_a\a_hdr10.log
   PresentMon-x64.exe --output_file outputs\exp_a\pm_a_hdr10.csv --timed 40 --terminate_after_timed --session_name pmA --no_console_stats   # elevated
   uv run python tools/dither_capture.py --device <N> --frames 60 --skip 10 --wait 12 --roi 0,1800,3840,8 --out outputs/exp_a/a_hdr10.npz --label a_hdr10
   ```

   The log must contain `back buffer vs source — 0 of 2160 rows differ` for the first readback and end with
   `… 0 with mismatches` (exit code 0; 3 = a mismatch was seen). Repeat with `--mode scrgb` (FP16 values
   = nit/80 as IEEE halves, same `CopyResource` path) as the control that the tool itself is sound.
4. Report, in the `data/` format:

   ```powershell
   uv run python tools/ramp_report.py outputs/exp_a/a_hdr10.npz --row 4 --label a_hdr10 --ref data/m25_hdr10_ramp_row.csv --presentmon outputs/exp_a/pm_a_hdr10.csv --pm-app hdr10_direct.exe --out-csv data/a_hdr10_ramp_row.csv --out-json data/a_summary.json
   uv run python tools/ramp_report.py outputs/exp_a/a_hdr10_backbuffer.csv --label a_hdr10_backbuffer --out-json data/a_summary.json
   ```

   The back‑buffer CSV must report 0 two‑code jumps, step widths {4, 5} and no skipped codes (that is the
   proof of what was stored). The captured row is then compared with it.
5. Reading the result. The ramp of this tool is `code(x) = round(x · 846 / 3839)` — every code 0..846
   exactly once, 4 or 5 px — not the `proto_hdr_view` ramp, so compare the **skipped‑code lists** with
   `m25_summary.json` (the same 49 codes are expected if the quantiser sits in the scanout path), not the
   rows pixel for pixel. Jumps persist → the application‑side hypothesis is dead and the report can go to
   NVIDIA. Jumps vanish → the Qt path's encoding is at fault; fix it before reporting anything.
6. Notes: the window is a borderless popup exactly covering the output (`--windowed` for a smoke test);
   `--metadata` additionally sets ST.2086 HDR metadata on the swapchain (off by default); the cursor is
   hidden over the window. Smoke test 2026‑09‑11 on the ROG (PA32UCDM on the RTX 5090, 60 Hz): fullscreen
   3840×2160, colour‑space support flags 0x3 (present + overlay), 11/11 readbacks bit‑identical, ~60
   presents/s. Run for real the same evening (RTX PRO 6000 → CAC‑1088 → HDFury Vertex → UltraStudio 4K Mini, PresentMon
   elevated, 1563/1568 Independent Flip): results in RESULTS.md §9, `data/a_*`.

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

### 7. DeckLink 8K Pro G2 の HDMI 入力＋GeForce で 4K 10bpc の**ネイティブ**タイミングを出す（2026‑09‑04 追加）

8K Pro G2 の HDMI 入力 EDID は優先タイミングが 1920×1200 で、VIC に 8K（194〜199）を持つ。NVIDIA 既定のままだと
無言で別の信号になる:

1. **GPU スケーリングで 8K に拡大される。** NVIDIA CP「デスクトップのサイズと位置の調整」は既定で
   「全画面表示」＋「実行デバイス: GPU」＝ 3840×2160 のデスクトップが **7680×4320 @ 23.976 のタイミング**で出る
   （DisplayConfig: source 3840×2160・target 7680×4320・STRETCHED）。DeckLink は `4320p23.98` で検出し
   （フレーム 133 MB → 取り込みバッファを 160 MB に拡大）、絵は GPU スケーラ経由、リンクは **8bpc に降格**
   （8K24 RGB 10bit は通らない）。「スケーリングなし」だけでは 8K タイミングの中央に 4K を置くだけ（CENTERED・
   8bpc のまま）。**「スケーリングを実行するデバイス: ディスプレイ」**にすると 3840×2160 total 5500×2250 @ 23.976
   （296.7 MHz）が直接出る。Windows 側の SetDisplayConfig（IDENTITY・target モード明示）は NVIDIA ドライバに
   上書きされて効かない。
2. **色深度を再適用する。** スケーリング変更後、表示は 10 bpc でも実リンクは 8bpc だった（Windows ACI2 bits/ch=8・
   DeckLink `RGB444+8bit`）。8 bpc → 適用 → 10 bpc → 適用。確認は Windows 側プローブ（`tools/hdr_display.py`）と
   取り込みログの DeckLink 検出フラグの**両方**で行う。コードが全て 4 の倍数なら 8bit リンク（または Ampere の
   GPU 自身の 8bit 格子＝RESULTS.md §7）。
3. 「デスクトップのサイズと位置の調整」ページのリフレッシュレート欄は**プレビュー用**（120 Hz / 30 Hz 等と出る）。
   適用値は「解像度の変更」と DisplayConfig で見る。
4. **セカンダリタスクバー。** 計測中に他のウィンドウ（NVIDIA CP 等）を操作すると、取り込み画面のタスクバーが前面に
   残り全 Present が `Composed: Flip` になる。`--foreground`（SwitchToThisWindow）はフォアグラウンドロックに勝てない。
   取り込み中は無操作。
5. EDID 要旨（8K Pro G2 HDMI 入力）: HDMI VSDB DC_30/36bit＋DC_Y444・Max TMDS 300 MHz、HF‑VSDB Max TMDS 600 MHz・
   SCDC・FRL、Colorimetry BT2020 RGB/YCC、HDR SM SDR/HDR/PQ（HLG 無し）、4:2:0 は 8K 48/50/60 のみ。
   4K24 RGB 10bit（TMDS 371 MHz）は sink 側の制約なし＝上の 8bpc はドライバの選択。

### 8. DP 専用 GPU を HDMI キャプチャに入れる: DP → HDMI アクティブ変換（2026‑09‑08 追加）

RTX PRO 6000 Blackwell の出力は DisplayPort 2.1 のみ。動いたもの・動かなかったもの（`data/rog6000_*`・RESULTS.md §8）:

1. **10 bpc を本当に通す変換器を使う。** HDMI 2.0 世代の古い DP→HDMI 変換器では、NVIDIA CP の「出力の色の深度」が
   デスクトップの色の深度に関係なく **8 bpc のみ**（変換器の DPCD が 8 bpc を申告）。さらにその変換器は
   (a) HDR InfoFrame を落とし（`hdr_present=0`）、(b) GPU 設定に関係なく sink へ「RGB 4:4:4 10bit 深色」を宣言し
   （リンクを 8 bpc にしても・挿し直しても DeckLink は `RGB444+10bit` のまま）、(c) **画素のビット詰めを壊す**
   （平坦パッチが 5 値・周期 10 px で R/G/B に回転して届く。0 と飽和だけ無事）。10 bpc でも 8 bpc でも計測に使えない。
   **Club3D CAC‑1088**（DP 1.4 → HDMI 2.1・Synaptics・バスパワー・4K23.976 RGB 10bit は DSC 不要）は
   HDMI 直結の取り込み（`m25_*`）と**ビット一致**。
2. **解像度・リフレッシュの一覧が固定されていたら、変換器が sink の EDID を読めていない。** 症状: モニタ名 `Non‑PnP`、
   `tools/hdr_display.py` で HDR 非対応、レジストリ `HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\SYN3000\…` の EDID が
   変換器内蔵の 128 バイト代替品（優先 1024×768・CEA 拡張なし）、モード一覧が 1080p60＋VGA 系のみ、DeckLink は
   「入力信号なし」（変換器が送信を始めない）。HDMI 側の挿し直し・接続順の逆転では変わらず、**HDMI ケーブルの交換で
   解消**（DeckLink の EDID 自体は正常＝チェックサム OK・4K VIC 入り CEA ブロック。1 本目のケーブルで DDC 読み出しが
   失敗していた）。解消後はモニタ名が `BMD HDMI` になり 4K / 23.976 Hz / 10 bpc が選べる。
3. **ケーブル・変換器を替えたら 3 点を再確認**: `tools/hdr_display.py`（`bits/ch=10`・HDR 有効）、NVIDIA CP
   「解像度の変更」、probe 取り込みの DeckLink 検出行（`2160p23.98 … RGB444+10bit hdr_present=1 eotf=2`）。
4. PresentMon 2.5.1 はこの出力の独立フリップを `Hardware Composed: Independent Flip`（MPO プレーン）と表示する。
   `Hardware: Independent Flip` と同等に扱う。ウィンドウ生成直後の数件は `Composed: Flip` だが取り込み開始
   （`--wait 12`）より前。
5. 付随: eGPU ホスト側で DeckLink 4K Extreme 12G（エンクロージャ内 PCIe）がスリープ復帰後に**コード 43** になった。
   デバイスマネージャーで **無効 → 有効**（管理者）で再起動なしに復旧。信号経路を疑う前に
   `Get-PnpDevice -PresentOnly | ? FriendlyName -match DeckLink` を確認する。
6. **AKiTiO Node を Thunderbolt ドック／TB5 エンクロージャの下流につなぐと、カードに PCI リソースが割り当てられない**
   （2026‑09‑11）。Ugreen TB5 ドック配下では DeckLink 4K Extreme 12G は列挙される（PCIe Gen2 ×4 リンクは張れる）が
   **メモリ範囲も IRQ も無し**で、ドライバはコード 10 / `0xC00000C0`（STATUS_DEVICE_DOES_NOT_EXIST）で開始失敗。
   無効→有効では直らない（ブリッジのウィンドウは起動時に決まる）。Node を PC の TB ポートへ**直結**すると復旧したが、
   数分後にコード 43 に落ちる事象が 2 回続いた（無効→有効後は再びコード 10・リソース無し）。診断は
   `Get-PnpDeviceProperty … DEVPKEY_Device_ProblemStatus` と `Win32_PnPAllocatedResource`。Node が不安定なら、
   一晩安定していた UltraStudio 4K Mini（Thunderbolt・PCIe スロット無し）を使う。
7. **CAC‑1088 が取り込み機の EDID を読めない**症状が再発（2026‑09‑11、ケーブル交換・接続順では直らず）。変換器と
   取り込み機の間に **HDFury Vertex**（HDR 静的メタデータ入り EDID の偽装）を入れたら即座に 3840×2160 10bit HDR の
   ディスプレイとして列挙された。Vertex の EDID の MaxCLL 4000 / MaxFALL 400 が InfoFrame 検出に出るが無害。
8. **HDCP: GPU の HDMI 端子と取り込み機の間に Vertex を入れると画が黒になることがある**（2026‑09‑11）。RTX 5090 Laptop の
   HDMI 送信側が Vertex と **HDCP 2.2** を張り（Vertex GUI の `RX0: … 2.2`）、Vertex は非 HDCP シンクへ復号映像を出せないため、
   UltraStudio へは 4K23.976 RGB 10bit PQ の正常なタイミングで**全画素 0**が届く（デスクトップも黒。検出行だけでなく probe
   フレームの非ゼロ画素を確認すること）。RTX PRO 6000 → CAC‑1088 → Vertex では HDCP は掛からなかった。対処は取り込み機の
   **直結**（HDCP を宣言しないシンクには暗号化できない）。UltraStudio 自身の EDID は HDR10 と 10bit を宣言している。
9. 4K60 優先の EDID を持つ取り込み機（UltraStudio）を挿し直すとドライバは **4K60 YCbCr 4:2:2 8 bpc** で立ち上がる。
   NVIDIA CP で **23.976 Hz を先に、次に RGB 10 bpc** を適用し、probe で `RGB444+10bit`・Windows `bits/ch=10` を再確認。
   NVIDIA CP が取り込み側の画面（見えない）に開いたら、PowerShell から `SetWindowPos`（プロセス `nvcplui`）で移動する。

### 9. 実験 A — シェーダを通さず整数コードをバックバッファへ直接書く（2026‑09‑11 追加・取り込みは未実施）

これまでの取り込みでは、R10G10B10A2 スワップチェーン内の PQ コードは**アプリ側**が作っていた
（CPU で PQ 符号化 → FP16 テクスチャ → Qt のテクスチャノードで 2 倍バイリニア拡大 → 出力マージャの
float→UNORM 変換）うえ、バックバッファの読み戻しも無い。「アプリの符号化が不均一でドライバは正確」という
反論（`docs/hdr-swapchain-adversarial-review-20260911.md` 攻撃 1）が残る。`tools/hdr10_direct.cpp` はこれらの段を
すべて外す: 10bit コードを CPU で整数として生成し、DEFAULT テクスチャから `CopyResource` でバックバッファへ転写、
N 回に 1 回 `Present` の直前にステージングへ読み戻してソースとバイト比較し、ランプ行を CSV に書く。

1. ビルド: `g++ -std=c++17 -O2 tools/hdr10_direct.cpp -o tools/hdr10_direct.exe -ld3d11 -ldxgi -lole32 -luser32 -lgdi32`
2. `tools\hdr10_direct.exe --list` で取り込み入力（`BMD HDMI` / UltraStudio: 3840×2160・`bits=10`・`ColorSpace=12`）の
   index を確認。デバイスはその出力を持つアダプタ上に作るので `QT_D3D_ADAPTER_INDEX` は不要。
3. 表示＋取り込み＋PresentMon（管理者シェル。§6）は英語側の手順 3 のコマンドどおり。ログに
   `back buffer vs source — 0 of 2160 rows differ`、末尾に `0 with mismatches`（終了コード 0。3 は不一致あり）。
   `--mode scrgb`（nit/80 の IEEE half を同じ経路で転写）も対照として取る。
4. 集計は `tools/ramp_report.py`（英語側の手順 4）。バックバッファ CSV は 2 コード飛び 0・段幅 {4,5}・欠落 0 で
   なければならない（格納コードの証明）。
5. 判定: このツールのランプは `code(x) = round(x·846/3839)`（全コード 0..846 が 1 回ずつ・4〜5 px）で proto のランプ
   とは違うので、比較は **欠落コード一覧**（`m25_summary.json` の 49 個と同じ位置か）で行い、画素単位では比べない。
   飛びが残る → アプリ側説は消え、NVIDIA へ正式報告へ。飛びが消える → Qt 経路の符号化が原因。先に直す。
6. 補足: `--windowed` はスモーク用、`--metadata` で ST.2086 メタデータを付ける（既定は無し）。2026‑09‑11 の
   スモーク（ROG・RTX 5090 上の PA32UCDM・60 Hz）: 全画面 3840×2160・色空間サポート 0x3・読み戻し 11/11 ビット一致。
   同日夜に本番実施（RTX PRO 6000 → CAC‑1088 → HDFury Vertex → UltraStudio 4K Mini・PresentMon 管理者・1563/1568 が
   Independent Flip）: 結果は RESULTS.md §9・`data/a_*`。
