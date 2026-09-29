# Next steps (as of 2026-09-29) / 次にやること

Open work after the near-black measurement on the RTX 3070 (`docs/RESULTS.md` section 10). This file is a
hand-over note: what to measure next, what to record while measuring, and what is still missing before the
NVIDIA addendum 5 (`docs/report_drafts.md` section 1f) is posted. Update or delete items as they are done.

## 1. What section 10 left open

| Open question | Why it matters |
|---|---|
| Which property of the sink path switches the output dither of the RTX 3070 on and off | Directly connected to the capture card the GPU dithers (8-bit lattice); behind the HDFury Integral 2 it does not. Without knowing the switch, "Ampere dithers" is not a general statement |
| Whether the 10 skipped near-black codes of the HDR10 swapchain come from the GPU scanout, from Qt, or from a device in the path | The list equals the near-black skips of the FP16 control of Experiment A (section 9), which points at a linear -> PQ re-encode - an inference, not a measurement |
| Whether the result holds on another Ampere part and with another splitter | So far one GPU (RTX 3070), one splitter (Integral 2), one capture card |

## 2. Planned measurement: second Ampere part, other splitter

* GPU: **GeForce RTX 3080** in an eGPU enclosure, driven from the laptop used for the RTX PRO 6000 control
  (section 8 / section 9 setup).
* Splitter: **HDFury Vertex** (the Integral 2 is not available at that site).
* Capture: the capture device available at that site (Experiment A used an UltraStudio 4K Mini).
* Conditions: the four of section 10 - {FP16 scRGB, R10G10B10A2 HDR10} x {direct, through the splitter}.

How to read the outcome:

| Outcome | Conclusion |
|---|---|
| Through the Vertex: no dither, HDR10 skips the same 10 codes, FP16 none | The effect is common to Ampere and not specific to the Integral 2 |
| Through the Vertex: dither (8-bit lattice) | The dither switch depends on the splitter or on the EDID it presents; compare the EDIDs of the two setups |
| Direct: dither | Reproduces section 7 and section 10 on a second part |

Differences from section 10 that must be stated in the record: GPU model, eGPU link, splitter model, capture
device, monitor.

## 3. Record these while measuring (missing in section 10)

* Splitter configuration: EDID mode (which EDID the GPU reads), scaling, HDCP state of input and outputs
* A **PresentMon log** recorded concurrently with every capture (`tools/ramp_report.py --presentmon` reads it)
* The luminance of code 1.0 set in the application, and the size and bit depth of the source image, if an
  application ramp is used (section 10: size not recorded at capture time, most probably 1920 x 1080)
* NVIDIA Control Panel output settings of the captured display (colour format, depth, range, scaling)
* The EDID of the sink as the GPU sees it (monitor name and HDR state from `tools/proto_hdr_view.py --list`,
  `tools/dxgi_outputs.cpp`)

Known pitfalls from earlier runs:

* **HDCP**: with a Vertex between an RTX 5090 Laptop and the capture device the link engaged HDCP 2.2 and the
  capture was all black (section 9, PROCEDURE.md section 8). Check the Vertex status page first.
* **Mode after re-plugging**: a capture device's EDID may prefer 4K60, which the driver serves as YCbCr 4:2:2
  8 bpc. Set 23.976 Hz first, then RGB 10 bpc (PROCEDURE.md section 8).
* **Display name changes** when the wiring changes (`BVM-HX310` -> `BMD HDMI` in section 10). A viewer that
  remembers its target screen by name closes its fullscreen window; re-select the screen.
* `rawdecklink_core.dll` is not in this repository. Build `decklink_core/` or point `RDL_CORE_DLL` at an
  existing build.

## 4. Tool work that would make the result reproducible by others

| Item | Reason |
|---|---|
| A near-black mode in `tools/proto_hdr_view.py` (ramp PQ code 0 ... about 100, or SDR gamma 2.2 code 0 ... 0.05) | Section 10 used a private application as the source. With a pattern in this repository anyone can repeat it |
| The same near-black ramp in `tools/hdr10_direct.cpp` with the back-buffer readback | Proves what entered the swapchain, as Experiment A did for the full ramp |
| Capture + PresentMon in one command | Removes the "indicator read by the operator" weakness of section 10 |
| Decisive test for the dither switch: give the splitter the EDID of the capture card and re-capture | If the dither comes back, the EDID decides |

## 5. Before posting addendum 5

- [ ] Splitter configuration recorded and added to the text
- [ ] PresentMon log recorded (or the weakness stated as it is now)
- [ ] Luminance of code 1.0 recorded (the draft says "about 200 nit", inferred from the top code)
- [ ] Decide whether to wait for the second Ampere part (item 2) - it would turn "one RTX 3070" into "Ampere"
- [ ] After posting: mark section 1f / 4.1f of `docs/report_drafts.md` as posted, with the date

---

## 日本語

RTX 3070 の近黒ランプの計測（`docs/RESULTS.md` §10）のあとに残っている作業の引き継ぎメモ。次に何を測るか、
測るときに何を記録するか、NVIDIA フォーラム追補 5（`docs/report_drafts.md` §1f）を投稿する前に何が足りないか。
済んだ項目は更新するか消す。

### 1. §10 で残った疑問

* RTX 3070 の出力ディザを切り替えているのは、接続先の経路のどの性質か（キャプチャカードへ直結するとディザあり、
  HDFury Integral 2 の後ろではディザなし）。
* HDR10 の近黒の 10 符号の欠落が、GPU のスキャンアウト・Qt・経路上の機器のどこで生じているか。一覧は実験 A（§9）の
  FP16 対照の近黒の欠落と同じで、リニア → PQ の再符号化を示唆するが、これは推論で、計測ではない。
* 別の Ampere の個体、別の分配器でも同じ結果になるか。いまは GPU 1 台（RTX 3070）・分配器 1 台（Integral 2）・
  キャプチャカード 1 台。

### 2. 予定している計測: 2 台目の Ampere、別の分配器

* GPU: **RTX 3080** を eGPU ユニットに載せ、RTX PRO 6000 の対照（§8・§9）で使ったノート PC から駆動する。
* 分配器: **HDFury Vertex**（その拠点には Integral 2 が無い）。
* 条件: §10 と同じ 4 つ = {FP16 scRGB, R10G10B10A2 HDR10} ×{直結, 分配器経由}。
* 結果の読み方:
  * Vertex 経由でディザなし・HDR10 だけ同じ 10 符号が欠落 → Ampere に共通で、Integral 2 に固有ではない。
  * Vertex 経由でディザあり → 切り替えは分配器か、分配器が見せる EDID で決まる。2 つの構成の EDID を比べる。
  * 直結でディザあり → §7・§10 を別の個体で再現。
* §10 との違い（GPU の型番・eGPU の接続・分配器・キャプチャ機器・モニター）は、記録に明記する。

### 3. 計測のときに記録するもの（§10 で不足）

* 分配器の設定: EDID のモード（GPU がどの EDID を読むか）・スケーリング・入出力の HDCP の状態
* 取り込みと同時刻の **PresentMon のログ**
* アプリのランプを使うなら、アプリで設定した「符号 1.0 の輝度」と、元の画像の大きさ・ビット深度
  （§10 は取り込みの時点で大きさを記録していない。おそらく 1920×1080）
* 取り込む画面の NVIDIA コントロールパネルの出力設定（色形式・深度・レンジ・スケーリング）
* GPU から見た接続先の EDID（モニター名と HDR の状態）

これまでの落とし穴: Vertex を挟むと HDCP 2.2 が掛かって全面黒になった例がある（§9）。配線を変えるとモードが
4K60 YCbCr 4:2:2 8bpc になることがある（23.976 Hz → RGB 10bpc の順に設定）。配線を変えると画面の名前が変わり、
名前で表示先を覚えているビューワは全画面を閉じる。`rawdecklink_core.dll` は本リポジトリに無い。

### 4. 第三者が再現できるようにするためのツール作業

* `tools/proto_hdr_view.py` に近黒のモードを足す（§10 は非公開のアプリが信号源だった）。
* `tools/hdr10_direct.cpp` にも同じ近黒ランプを足し、バックバッファを読み戻す（スワップチェーンに何が入ったかの証明）。
* 取り込みと PresentMon の記録を 1 回の操作で行う。
* ディザの切り替えの決め手になる試験: 分配器にキャプチャカードの EDID を持たせて取り直す（ディザが戻れば EDID が原因）。

### 5. 追補 5 を投稿する前に

* 分配器の設定を記録して本文に足す。
* PresentMon のログを取る（取れなければ、いまの文案のとおり弱点として明記したままにする）。
* 「符号 1.0 の輝度」を記録する（文案は上端の符号から逆算した「約 200 nit」）。
* 2 台目の Ampere（上の 2）を待つかを決める。待てば「RTX 3070 の 1 台」が「Ampere」になる。
* 投稿したら、`docs/report_drafts.md` の §1f・§4.1f を投稿済みに直し、日付を入れる。
