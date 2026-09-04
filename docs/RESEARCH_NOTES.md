# Research notes (2026‑09‑04): GPU generations, NVIDIA driver branches, AMD Radeon — what is publicly known

Companion to `RESULTS.md` §6/§7. After measuring a true‑10‑bit Blackwell (RTX 5090 Laptop: uneven R10G10B10A2
quantisation, ≈16‑code periodic skips) and an Ampere part (RTX 3070: 8‑bit lattice + random spatio‑temporal dither on a
10 bpc link), three questions came up. This note records what the public record says, so the next measurements can be
chosen deliberately. Everything below is desk research, not our own measurement; each item is tagged
**[measured]** (third‑party data), **[vendor]** (official documents / staff statements) or **[hearsay]** (forum opinion).

## Executive summary

| Question | Finding | Evidence |
|---|---|---|
| Does **RTX 40 (Ada)** output true 10‑bit codes, or 8‑bit + dither like Ampere? | **No public capture‑level measurement exists.** The nouveau/GSP display‑class tables show AD102 shares its window/cursor display classes with GA102, while GB20x uses a new class (CA7D / "NVD5.0") — structurally Ada is an Ampere‑generation display engine, so Ampere‑like behaviour is *expected*, not shown. | structural only |
| Periodic HDR10 skips on RTX 40? | No reproduction and no refutation reported; NVIDIA threads 343119/346429 are RTX 50 only. | none |
| Do **Game Ready / Studio / RTX Enterprise** drivers change what is on the wire? | **No evidence that they do.** NVIDIA describes the branches as differing in validation scope, cadence and support period; the Production Branch "contains the features and enhancements of the Studio Driver of the same version number". An RTX A4000 (Ampere, Enterprise driver) was captured with default temporal dithering on a true‑10‑bit monitor — same behaviour as our GeForce RTX 3070. The RTX 5070 Ti banding reproduces on GR 581.08/577.0 and Studio 580.97 alike. | vendor + measured (indirect) |
| Branch × generation matrix | Nobody has measured it. Observed differences track the GPU generation and the output settings (range, depth, format), not the branch. | none |
| "Radeon has only one driver" | **Wrong, with a grain of truth.** AMD ships Adrenalin, PRO Edition and Cloud Edition; consumer RX cards may officially install PRO Edition. But AMD never gated features such as 10 bpc output behind a branch the way NVIDIA once gated 30‑bit OpenGL to Quadro (lifted in 2019). | vendor |
| "Radeon reproduces gradients better than NVIDIA" | **Plausible for SDR 8‑bit desktop banding** (AMD dithers by default; NVIDIA on Windows does not for full‑range RGB), **unmeasured for HDR 10‑bit quantisation.** AMD's display core (shared Linux/Windows DC code) applies *static spatial* dithering even at 10 bpc output (`SPATIAL10`, `FRAME_RANDOM = 0`) and disables it only at 12 bpc — a third pattern (no frame‑to‑frame change, but 1‑px spatial flips) that nobody has captured. | hearsay + source code |
| "Switch to Radeon (or use exclusive fullscreen / madVR passthrough) to get bit‑exact HDR10 under iFLIP" (two generative‑AI answers, fact‑checked in §D) | **Mechanism wrong, Radeon still unmeasured.** Independent Flip bypasses DWM by definition (Microsoft docs), and our composed‑vs‑iFlip control puts the RTX 50 defect in the direct scanout stage, not in DWM/ACM (ACM is an SDR‑display feature). Exclusive fullscreen uses the same scanout hardware; madVR "passthrough" only switches HDR metadata through private APIs. The suggested AMD registry keys (`PP_ThermalRegulatorOptions`, `TMDS_Dither`, `DP_Dither` = 0) do not exist; the real `*_DisableDither` = 1 values are legacy `atikmdag.sys` strings, and the documented control is the public ADL dither API (`ADL_DL_DISPLAY_DITHER_DISABLED`, exposed by ColorControl) or a 12 bpc link (DC default: no dither). | vendor docs + our measurement |

## Implications for the next measurements

1. **RTX 40 is worth one capture** — it would be the first public data point. Expected outcome: Ampere‑like 8‑bit lattice + dither,
   but that is inference from shared display classes.
2. **Driver‑branch comparisons are low priority.** Vendor statements and the indirect measurements agree that the branch does not
   change the display pipeline. If done at all, one same‑GPU / same‑version‑number GR‑vs‑Studio capture is enough to confirm "no difference".
   Enterprise (PB/NFB) drivers do not install on GeForce, so that comparison has to be made on an RTX PRO part.
3. **AMD is worth measuring.** No capture‑level data exists on whether Radeon puts true 10‑bit codes, 8‑bit + dither, or
   10‑bit + static spatial dither on the wire. The Linux DC defaults predict the third; a single RDNA3/4 capture would answer the HDR
   version of the "better gradients" claim with primary data.
4. **Dither control is undocumented on both vendors' Windows drivers.** NVIDIA: no control‑panel item, only the `DitherRegistryKey`
   registry path and an undocumented NvAPI (ColorControl / novideo_srgb); Ampere's low‑level dither survives NvAPI "Disabled"
   (ColorControl #335), matching our RTX 3070 capture. AMD: 24 dither modes in the public ADL API; the legacy `DisableDither`
   registry strings are absent from current drivers (effect unverified).
5. **When Radeon is measured, include the dither controls in the matrix.** Capture at 10 bpc with ADL dither
   DRIVER_DEFAULT and DISABLED, and at 12 bpc if the capture EDID allows it (DeckLink 8K Pro G2 advertises DC_36bit),
   for both swapchain formats. The DC design predicts that FP16 carries the same OPP‑stage spatial dither as
   R10G10B10A2, and that DWM composition does not remove it (§D‑4).

The full research record (in Japanese, with all sources) follows.

---

# 日本語（調査メモ全文）

RESULTS.md §6/§7（RTX 5090 Laptop＝真 10bit・HDR10 全画面で ≈16 コード周期の 2 コード飛び、
RTX 3070＝10bit リンクでも 8bit 格子＋時空間ディザ）を受けて、次の 3 点を Web で調査した結果の記録。
調査は 3 並列のサブエージェントで実施し、各報告をほぼ原文で §A〜§C に収録、先頭に統合要約を置く。

凡例: **[実測]**＝計測データ付きの報告／**[公式]**＝ベンダー文書・スタッフ発言／**[伝聞]**＝フォーラム意見・主観。

---

## 統合要約（本リポジトリの計画への含意）

| 問い | 判定 | 根拠の強さ |
|---|---|---|
| RTX 40（Ada）は真 10bit か 8bit＋ディザか | **公開の定量計測なし**。表示エンジンのクラス ID が Ampere（GA102）と共用（nouveau/GSP ソース）＝Ampere 同様の挙動が予想されるが推論 | 構造的根拠のみ |
| RTX 40 で HDR10 全画面の周期飛びは出るか | 再現・反証とも報告なし。346429/343119 は RTX 50 のみ | 情報なし |
| Game Ready / Studio / Enterprise で線上のコードが変わるか | **変わる証拠なし**。差は検証対象・リリース周期・サポート期間・ISV 認証。PB は「同版 Studio と同機能」と NVIDIA が明言。RTX A4000（Ampere プロ＋Enterprise）でも既定で時間ディザが実測されている | 公式＋実測（傍証） |
| 世代ごとのドライバ差 | ブランチ×世代の比較計測は存在しない。観測差は GPU 世代と出力設定に帰属するのが整合的 | 情報なし |
| 「Radeon はドライバが 1 種類」 | **誤り（部分的に正しい）**。Adrenalin / PRO Edition / Cloud Edition の 3 系統。ただし NVIDIA のように「機能をブランチで解禁」する差は歴史的に無い | 公式 |
| 「Radeon の方がグラデーション再現が良い」 | **SDR 8bit デスクトップのバンディングに限れば概ね支持**（AMD は既定ディザ ON、NVIDIA Windows は Full range で既定ディザ無し）。**HDR 10bit の量子化精度を計測した比較は存在しない**。AMD の 10bpc 出力は設計上「内部精度→10bit の静的空間ディザ」（Linux DC ソース）で、真 10bit コードの均一性は未計測 | 伝聞＋設計ソース |
| 「Radeon に替えれば（または排他的フルスクリーン／madVR パススルーなら）iFLIP HDR10 で Bit-Exact になる」（生成 AI の回答 2 件・§D で検証） | **機序が誤り・Radeon は依然未計測**。Independent Flip は定義上 DWM を通らず（MS 文書）、本リポジトリの合成 vs iFLIP 対照は RTX 50 の欠陥を直接スキャンアウト段に置く。FSE も同じ表示エンジンを通る。madVR の passthrough はプライベート API による HDR メタデータ切替。提示された AMD レジストリ値（`PP_ThermalRegulatorOptions`・`TMDS_Dither`・`DP_Dither`=0）は実在せず、実在する `*_DisableDither`=1 は`atikmdag.sys` 世代の遺物。文書化された制御は公開 ADL ディザ API（DISABLED）か 12 bpc リンク | 公式文書＋本リポジトリ実測 |

**計画への含意**

1. **RTX 40 の計測は公開初出になり得る。** Ada は Ampere と表示クラスを共用しているので「8bit 格子＋ディザ」が予想されるが、
   誰も測っていない。1 台借りられれば 346429 への追補 3 に値する（RTX PRO 6000 Blackwell と並ぶ優先度）。
2. **ドライバ系統の比較は優先度を下げてよい。** 公式・実測ともに「ブランチで表示パイプラインは変わらない」方向で一致。
   検証するなら「同一 GPU・同日リリースの GR と Studio（同版番号）」で 1 回取れば十分で、差が出ないことの確認が目的になる。
   PB/NFB の Enterprise ドライバは GeForce にはインストールできないので、RTX PRO 6000 側で Studio/GR 相当品との比較になる。
3. **Radeon は測る価値が高い。** AMD が線上で真 10bit を出すか、8bit＋ディザか、10bit＋空間ディザか、を DeckLink で測った
   公開データが無い。Linux DC ソースの既定（10bpc 出力でも SPATIAL10・FRAME_RANDOM=0＝時間軸ディザ無しの静的空間ディザ、
   12bpc で DISABLE）が Windows でも同じなら、「フレーム間変動 0・単一フレームで 1px 孤立反転あり」という
   NVIDIA 両世代と異なる第 3 のパターンが観測されるはず。RDNA3/4 の dGPU か APU で 1 回計測すれば、Claim B の HDR 版に
   一次データで答えられる。
4. **ディザの制御は NVIDIA/AMD とも「非公開」寄り。** NVIDIA は Windows の CP に項目が無く、レジストリ `DitherRegistryKey` と
   非公開 NvAPI（ColorControl / novideo_srgb）のみ。Ampere の「消せない下位レベルディザ」は NvAPI Disabled でも残る
   （ColorControl #335）＝本リポジトリの 3070 実測と整合。AMD は ADL 公開 API にディザ 24 モードがあり、
   `DisableDither` レジストリは新ドライバで文字列が消えている（有効性未検証）。
5. **Radeon を測るときはディザ制御を計測マトリクスに入れる。** 10 bpc で ADL ディザ DRIVER_DEFAULT / DISABLED の
   両方、キャプチャ側 EDID が許せば 12 bpc（DeckLink 8K Pro G2 は DC_36bit を広告）でも撮り、FP16 と R10G10B10A2 の
   両方を対象にする。DC の設計上、FP16 にも同じ OPP 段の空間ディザが乗り、DWM 合成でも消えないと予想される（§D-4）。

---

## §A. RTX 40（Ada Lovelace）の表示出力ビット深度・ディザ挙動

**結論を先に言うと、RTX 40 についてはキャプチャ等の定量測定は公開されておらず、質問 1・2 は「直接の証拠なし」である。**

### A-1. Ada は 10bpc リンクで真の 10bit を出すか、Ampere 同様 8bit＋時間ディザか

- **[実測なし]** RTX 40 の出力コードをキャプチャカード・オシロ等で調べた公開データは見つからなかった。LEDStrain の
  キャプチャ検証（DVI2PCIe＋VideoDiff / 自作比較ツール）は GT 210〜GTX 1060 と RTX A4000 まで。
- **[実測（Ampere 側の傍証）]** ColorControl Issue #335（2024-03）: RTX A4000（Ampere）で ColorControl（NvAPI）の
  ディザを Disabled にしても DVI2PCIe キャプチャで消えない「下位レベルのディザ」を確認、RTX 3070 FE でも同様との追記。
  保守者 Maassoft は「NvAPI にこれ以外のディザ関数は見当たらない＝非公開機能かもしれない」と回答。NVIDIA 開発者
  フォーラム 279427（A4000）でも同じ手法で「ドライバ導入後に時間ディザが自動有効化・真の 10bit モニタでも」と報告、
  NVIDIA 側は「OS のコントロールパネルで設定できる項目ではない」と回答のみ。→ 本リポジトリの 3070 実測（±4 トグル）と整合。
- **[伝聞]** 同スレッドで一利用者が「消せない強いディザは RTX 2000/3000/**4000** 系すべての問題」と述べるが測定なし。
- **[伝聞（主観）]** LEDStrain ユーザー ag3x: ディザ過敏症状ベースで「4090 は不快、**4070 Ti と 4060 は全く問題なし**、
  5080/5070 Ti は不可」。測定なし。ただし「同じ Ada 内で機種差がある」示唆は、DSC/リンク条件依存の可能性を含めて注意点。
- **[伝聞（NVIDIA サポート経由）]** Blur Busters（2025-07、RTX 50）: ユーザーが NVIDIA サポートから「新しい RTX カードは
  FRC をより積極的に使う」と言われたと報告。原文なし。
- **[伝聞]** GeForce フォーラム「4090 dithering issues」: Odyssey G9 240 Hz で単色に横線、120 Hz では出ない（DSC 有効時の
  ディザ設定不整合＝Blur Busters で dogelition が「DSC 出力ではディザ設定が画面左半分にしか効かない」と指摘した現象と
  同型の可能性）。

### A-2. RTX 40 の HDR10（R10G10B10A2）全画面での不均一量子化・バンディング報告

- **[実測]** NVIDIA 開発者フォーラム 343119（2025-08-25 初出、RTX 5070 Ti、581.08/577.0/580.97）と 346429（本リポジトリ
  追記: 5090 Laptop 610.62・DeckLink 定量、RTX 3070 の別パターン）以外に、同種の「全画面のみ・R10G10B10A2 のみ・FP16 では
  出ない」報告は見つからなかった。**RTX 40 での再現報告・反証報告ともに無し。** NVIDIA からの公式回答も無し。
- **[伝聞]** GeForce フォーラム「Bug – Banding in fullscreen SDR content with HDR enabled」（395205、43 ページ超）は
  「Windows HDR 有効時の SDR 全画面コンテンツのバンディング」で本件とは機序が異なる（JS 描画のため取得不可）。
  ドライバフィードバックスレッドに「NVCP を 10bit→8bit にするとバンディングが消え、HDR はディザで維持される」旨の
  書き込みがあるが GPU 世代不明。
- **[伝聞]** DisplayCAL フォーラム「bit depth mismatch with Nvidia RTX and 10 bit panel」: 10bit 出力・10bit パネルで
  G の 16/17, 35/36, 52/53, 68/69 が同色になる「ソフトバンディング」を観察し「8bit LUT を 4 倍補間して 1024 エントリに
  している」と推測（機種・日付不明、測定器なし）。本リポジトリの 5090 で見た「約 16 コードごとの 2 コード飛び＝
  区分線形 LUT」とパターンが近く興味深いが、世代特定不能。

### A-3. NVIDIA のディザ制御（Windows レジストリ / NvAPI / Linux）と世代別既定

- **[公式]** Linux README `FlatPanelProperties`: `Dithering = Auto|Enabled|Disabled`、
  `DitheringMode = Auto|Static-2x2|Dynamic-2x2|Temporal`。Auto は「ドライバがディザするか判断する」とだけ記され、
  **判断条件（リンク深度 < 内部精度等）や世代別の既定は文書化されていない**。NVCtrl.h も同文。
- **[公式（NvAPI 公開版）]** docs.nvidia.com の NVAPI R590 にディザ関数は掲載なし。ColorControl/novideo_srgb は非公開の
  `NvAPI_Disp_GetDitherControl/SetDitherControl`（state/bits/mode＋対応深度・モードのフラグ 2 語、version 0x10018）を使用。
- **[伝聞（実験ベース）]** DisplayCAL フォーラム（Guzz, 2019-02、GTX 1060/1080Ti/RTX 2080Ti）:
  `HKLM\SYSTEM\CurrentControlSet\Services\nvlddmkm\State\DisplayDatabase\<display>\DitherRegistryKey`。既定は「出力色深度・
  色フォーマット・ダイナミックレンジに依存」— **Full range RGB＝ディザ無し、Limited＝Temporal 有効**。ColorControl Issue #83
  （2022）: 8bit Full RGB で自動読み出しが「6bit SpatialDynamic」を返すなど、報告値と実挙動の不一致あり。
- **[伝聞]** anandtech: 「NVCP で 10bit を選べればドライバはディザせず真の 10bit を送る」（Pascal 時代）。NVIDIA Linux
  フォーラムのモデレータ Generix: 「ディザは 6/8bpp 用、10bit 表示では off にすべき」（スタッフではない）。
- **世代別既定（Ampere/Ada/Blackwell）を述べた文書は見つからず。** 「内部精度 > リンク深度のときのみ時間ディザ」という
  条件も公式には未記載（コミュニティの通説のみ）。

### A-4. Ampere → Ada → Blackwell の表示エンジン変更

- **[公式]** Blackwell 白書 V1.1: DisplayPort 2.1b UHBR20（80 Gbps）、ハードウェア Flip Metering（フレームペーシング論理を
  表示エンジンへ移管）。HotHardware/Tom's: 「表示・メディアエンジンは Ada から**アップグレード**」。**ビット深度・LUT 精度・
  ディザに関する記述は無い。** Ada（GeForce/ProViz）白書には表示エンジン章自体が無い。Quadro RTX 4000（Turing）仕様に
  「12-bit internal display pipeline（12bit scanout 対応）」の記載はあるが、Ada/Blackwell の RTX PRO データシートには
  その文言が無い（RTX PRO 4000 Blackwell は「HDR and higher color depth support」のみ）。
- **[実測（ドライバソース）]** nouveau/GSP パッチ「add display class ids to gpu hal」: **AD102 の表示クラスは root/core 以外
  （window/wimm/cursor）を GA102 と共用**＝Ada の表示エンジンは Ampere とほぼ同一世代。一方 GB20x は新クラス **CA7D／
  「NVD5.0（DISPv0502）」**で、HEAD メソッド間隔が 0x400→0x800 に倍増・専用 `engine/disp/gb202.c` を新設（2026-08-20）。
  → 「Ampere と Ada が同挙動・Blackwell が別挙動」という仮説を裏づける構造的根拠。ただしディザ/量子化の挙動差そのものを
  示すものではない。

### A-5. 出典

- https://forums.developer.nvidia.com/t/uneven-banding-in-fullscreen-hdr-output-with-r10g10b10a2-swapchain/346429 — 本件スレッド
- https://forums.developer.nvidia.com/t/uneven-banding-in-fullscreen-hdr-output-with-r10g10b10a2-swapchain/343119 — 初出報告（RTX 5070 Ti、2025-08）
- https://github.com/Maassoft/ColorControl/issues/335 — A4000/3070 の消せない下位ディザ（DVI2PCIe 実測、2024-03）
- https://forums.developer.nvidia.com/t/disable-dithering-on-rtx-a4000/279427 — A4000 時間ディザ自動有効・NVIDIA 回答
- https://forums.blurbusters.com/viewtopic.php?t=12953 — 8→10bit ディザ、DSC 制約、NVIDIA サポート発言（伝聞）
- https://ledstrain.org/u/ag3x — 4090/5080 不快・4070Ti/4060 良好（主観）
- https://www.nvidia.com/en-us/geforce/forums/geforce-graphics-cards/5/510224/4090-dithering-issues/ — 4090 240Hz 横線（伝聞）
- https://hub.displaycal.net/forums/topic/how-to-enable-dithering-on-nvidia-geforce-with-windows-os/ — DitherRegistryKey 値と既定条件（2019）
- https://github.com/Maassoft/ColorControl/issues/83 — 自動読み出し値と実挙動の不一致
- https://github.com/Maassoft/ColorControl/issues/68 — NvAPI ディザ構造体（非公開 API）
- https://download.nvidia.com/XFree86/Linux-x86_64/550.142/README/xconfigoptions.html — Linux FlatPanelProperties Dithering/DitheringMode（公式）
- https://raw.githubusercontent.com/NVIDIA/nvidia-settings/main/src/libXNVCtrl/NVCtrl.h — NV_CTRL_DITHERING（公式）
- https://docs.nvidia.com/nvapi/group__dispcontrol.html — 公開 NVAPI にディザ API 無し
- https://forums.developer.nvidia.com/t/question-linux-driver-10-bit-color-depth-support/249345 — 「ディザは 6/8bpp 用」発言
- https://forums.anandtech.com/threads/nvidia-geforce-lut.2464973/post-38053251 — 10bit 選択時はディザしない（Pascal 期）
- https://hub.displaycal.net/forums/topic/the-complicacy-of-bit-depth-mismatch-with-nvidia-rtx-and-10-bit-panel/ — 10bit でのソフトバンディング観察（8bit LUT 補間説）
- https://hub.displaycal.net/forums/topic/nvidia-drivers-color-accuracy-mode/ — Reference/Accurate モードの説明
- https://images.nvidia.com/aem-dam/Solutions/geforce/blackwell/nvidia-rtx-blackwell-gpu-architecture.pdf — Blackwell 白書（DP 2.1b・p.30）
- https://hothardware.com/reviews/nvidia-rtx-blackwell-architecture-overview — Flip Metering・表示エンジン刷新
- https://www.mail-archive.com/nouveau@lists.freedesktop.org/msg46723.html — AD102 が GA102 表示クラスを共用（nouveau/GSP）
- http://www.mail-archive.com/nouveau@lists.freedesktop.org/msg54116.html — GB20x＝CA7D／NVD5.0（DISPv0502）、0x800 ストライド
- https://www.leadtek.com/eng/products/workstation_graphics(2)/nvidia_quadro_rtx4000(10827)/detail — Turing「12-bit internal display pipeline」
- https://www.nvidia.com/content/dam/en-zz/Solutions/products/workstations/professional-desktop-gpus/rtx-pro-4000/workstation-datasheet-rtx-pro-4000-nvidia-us-web.pdf — RTX PRO 4000 Blackwell データシート

---

## §B. NVIDIA Windows ドライバブランチと表示パイプライン挙動

### B-1. Game Ready / Studio / RTX Enterprise（PB / NFB）の実際の差

- **[公式]** NVIDIA のドライバページは GR を「主要ゲームを early access から DLC まで網羅的にテスト」、Studio を「主要
  クリエイティブアプリで extensive testing」と説明し、「either can support running the best games and creative apps」と明記。
  差は**検証対象とリリース時期**として説明され、機能差は謳っていない。
- **[公式]** RTX Enterprise ドライバ説明文（R595 U6 596.72、2026-06-19）: Production Branch（PB）は旧 Quadro ODE の改名で、
  「Production Branch drivers also contain the features and enhancements of **NVIDIA Studio Drivers of the same version
  number**」。NFB は「PB リリース間の新機能・バグ修正・新 OS 対応」提供でサポート期間が短い。
- **[公式]** Data Center Drivers 文書（r580、2025-08-04）: NFB＝約四半期ごとに新ブランチ番号、PB＝年 2 回・1 年サポート、
  LTSB＝PB の一種で 3 年サポート。
- **バイナリ比較（nvlddmkm 同一性）の公開分析は見つからなかった**。間接証拠として、(a) Data Center Windows 580.88 /
  GeForce GR 580.88 / RTX Enterprise R580 U1 580.88 が同版・同日（2025-07-31〜08-04）で並ぶ、(b) 2026 年は GR と Studio が
  同版・同日リリース（596.36＝4/28、610.62＝6/16、616.56＝8/26、Neowin は 616.56 を「同じ Release 615 ブランチ」と記載）。
  **[伝聞]** 「Quadro 版は WHQL 待ち以外は同一＋精度系オプション追加」（HardForum）、「same DNA」（CGDirector 2023）程度で、
  ハッシュ一致等の裏付けは無い。

### B-2. ブランチ差 vs 製品クラス差（GeForce vs Quadro/RTX PRO）

- **30-bit（10 bpc）OpenGL**: **[公式]** 2019-07 Studio Driver が「GeForce/TITAN 含む全製品で初めて」対応、GR 436.02
  （2019-08-20）が同機能を GR へ展開。Puget Systems の 10-bit 記事（2018-08-17、2019-07-29 追記）が「OpenGL 10-bit は以前
  Quadro 専用、Studio で GeForce も可に」と確認。以後この製品クラス差は解消。
- **Dithering 制御**: **[伝聞・複数一致]** Windows の NVIDIA コントロールパネルに GeForce 向け Dithering 項目は無く、
  Linux（GeForce/Quadro）と Windows Quadro（NVWMI 経由）にのみ存在してきた。GeForce でも
  `HKLM\SYSTEM\CurrentControlSet\Services\nvlddmkm\State\DisplayDatabase\<display>\DitherRegistryKey`（state / 6・8・10 bit /
  SpatialDynamic・SpatialStatic・2x2 各種・Temporal）や非公開 NVAPI（ColorControl・novideo_srgb）で操作可能（guru3D・
  DisplayCAL・Special K、2019 頃〜）。
- **[公式]** NVIDIA スタッフ MarkusHoHo（Dev Forum「Disable dithering on RTX A4000?」2024）: 「This is not a configurable option
  as such in any OS control panel settings」— **プロ GPU＋Enterprise ドライバでも Windows CP に制御は無い**。
- **[伝聞]** 既定挙動は「出力深度・色形式・ダイナミックレンジで駆動側が決定。Full＝無効、Limited＝Temporal 有効」
  （Special K/guru3D）。NVIDIA 自身の Windows 既定値の文書は無い。
- 12 bpc は GeForce の CP でも HDMI で選択可。製品クラス差の裏付けは見つからず。

### B-3. 同一 GPU で GR/Studio/Enterprise により線上のコードが変わる証拠

- **ブランチ間の同一 GPU 比較計測は見つからなかった**。
- **[実測]** NVIDIA Dev Forum 346429（2025-09-29 octatecreations）: RTX 5070 Ti の R10G10B10A2 全画面 HDR の不均一バンディングが
  **GR 581.08 / 577.0 と Studio 580.97 の全部で再現**（同一 R580 系）→ 少なくとも同ブランチ世代では GR/Studio 差なし。
  本リポジトリの追補（RTX 5090 Laptop: Studio 596.36→610.62、RTX 3070: 610.62）も同スレッド。
- **[実測]** RTX A4000（Ampere プロ GPU・Enterprise ドライバ）が真 10-bit ディスプレイでも**既定で Temporal dithering**、
  DVI2PCIe ロスレスキャプチャで確認（2024）→「プロ GPU＋Enterprise なら dither 無しの真 10-bit」という仮説に**反する**。
- **[伝聞]** Tom's Hardware（2025-01）: RTX 3090 の 10-bit が「偽（8-bit 相当）」との報告と、3080 Ti で DisplayHDR CTS 1.2 が
  真 10-bit を示したとの反論が並立、いずれも写真評価・ドライバ版不明。

### B-4. 版番号とブランチ対応

- ブランチ Rxxx の Windows 版は先頭 2〜3 桁で読める: R570→572.60〜573.76、R575→576.xx、R580→580.88〜582.78、R590→591.xx、
  R595→596.xx、R610→610.47/610.62/610.88、R615→616.56。Linux は別採番（R595: Linux 595.71.05 = Windows 596.36）で、
  **Data Center Release Notes が両者を対で公開**。
- eosl.date 集計（NVIDIA 公表 EOL 由来）: R570 PB（2025-01-27）、R575 NFB（2025-06-03）、R580 LTSB（2025-08-04、EOL 2028-08-04）、
  R590 NFB（2025-12-22）、R595 PB（2026-03-24）、R610 NFB（2026-08-03）。※別ソースは R570 を LTSB/R580 を PB とし表記が揺れる。
- 本リポジトリ使用の **596.36 = R595（PB）、610.62 = R610（NFB）**。Studio と GR の同版・同日リリースが 2026 年は常態で、
  「同じコードベースか」は**同一版番号＋同日**で判定できる（NVIDIA も PB について「同版の Studio と同機能」と明言）。

### B-5. NVIDIA の dithering 文書

- **[公式]** Linux README Appendix B（435.17〜580.65.06 で同文）: `"Dithering"`＝Auto（the driver will decide when to dither）/
  Enabled / Disabled、`"DitheringMode"`＝Auto / Dynamic-2x2（毎フレーム更新）/ Static-2x2 / Temporal（pseudo-random）。
- **[公式]** README「Configuring Depth 30 Displays」（550.67）: 「Devices connected via DVI or HDMI, as well as laptop internal
  panels connected via LVDS, will be **dithered to 8 or 6 bits per component by default**」、VGA/DisplayPort の一部は full
  10 bit。**「いつ dither するか」の Windows 側公式記述は存在しない**。
- Windows レジストリ対応物（`DitherRegistryKey`）は非公開仕様で、上記フォーラムのリバース結果のみ。

### B-6. 結論

ドライバブランチは検証・サポート期間・パッケージ（プロファイル/ISV 認証）の差であり、表示パイプライン（dither/量子化）が
ブランチで変わる証拠は無い。観測差は **GPU 世代（Ampere vs Blackwell）と出力設定（範囲・深度・フォーマット）** に帰属する
とみるのが、入手可能な証拠と整合する。

### B-7. 出典

- https://www.nvidia.com/en-us/geforce/drivers/ — GR/Studio の説明、PB/NFB 定義
- https://www.softexia.com/drivers/nvidia-rtx-quadro-driver — RTX Enterprise R595 U6 596.72 の NVIDIA 説明文転載（PB＝Studio 同版と同機能）
- https://docs.nvidia.com/datacenter/tesla/pdf/NVIDIA_Datacenter_Drivers.pdf — Data Center Drivers r580: NFB/PB/LTSB の定義
- https://docs.nvidia.com/datacenter/tesla/tesla-release-notes-595-71-05/index.html — R595: Linux 595.71.05 / Windows 596.36
- https://docs.nvidia.com/datacenter/tesla/tesla-release-notes-580-65-06/index.html — R580: Linux 580.65.06 / Windows 580.88
- https://eosl.date/eol/product/nvidia/ — ブランチ種別・EOL 一覧（R570〜R610）
- https://www.nvidia.com/en-us/geforce/news/gamescom-2019-game-ready-driver/ — GR 436.02 30-bit を全製品へ
- https://www.nvidia.com/en-us/geforce/news/studio-driver/ — Studio Driver 2019-07: GeForce/TITAN 初の 30-bit
- https://www.pugetsystems.com/labs/articles/setting-graphics-card-software-to-display-10-bit-output-1221/ — OpenGL 10-bit の Quadro 限定→解消
- https://download.nvidia.com/XFree86/Linux-x86_64/580.65.06/README/xconfigoptions.html — Linux README: Dithering/DitheringMode
- https://download.nvidia.com/XFree86/Linux-x86_64/550.67/README/depth30.html — Depth 30 章: DVI/HDMI は既定で 8/6 bit へ dither
- https://github.com/NVIDIA/nvidia-settings/blob/main/src/libXNVCtrl/NVCtrl.h — NV_CTRL_DITHERING 定義
- https://forums.developer.nvidia.com/t/disable-dithering-on-rtx-a4000/279427 — RTX A4000 既定 Temporal dither・スタッフ回答
- https://forums.developer.nvidia.com/t/uneven-banding-in-fullscreen-hdr-output-with-r10g10b10a2-swapchain/346429 — RTX 5070 Ti で GR/Studio 双方再現
- https://steamcommunity.com/groups/SpecialK_Mods/discussions/0/2789318172123104780/ — GeForce の DitherRegistryKey 一覧・既定挙動
- https://forums.guru3d.com/threads/nvidia-and-dithering-controls-how-to-enable.436621/ — Windows dithering レジストリ（403・検索要約のみ）
- https://forums.tomshardware.com/threads/10bit-color-support-on-the-rtx-3000-cards.3869023/ — RTX 30 系 10-bit 真偽の論争（2025-01）
- https://www.neowin.net/news/nvidia-releases-new-game-ready-and-studio-drivers-with-cuda-133-support/ — 616.56 GR/Studio 同日・同 Release 615
- https://www.station-drivers.com/index.php/en/forum/nvidia-drivers-firmwares-utilities/1042-nvidia-studio-driver-v610-62-whql — Studio 610.62 = 2026-06-16
- https://hardforum.com/threads/using-geforce-drivers-on-quadro.2016601/ — [伝聞] Quadro/GeForce ドライバ「同一＋精度オプション」
- https://www.cgdirector.com/nvidia-studio-vs-game-ready-driver/ — [伝聞] 「same DNA」

---

## §C. AMD Radeon のドライバ系統と表示パイプライン精度

### C-1. ドライバ系統（Claim A「Radeon はドライバが 1 種類」）

**[公式]** AMD の Windows 向けグラフィックスドライバは少なくとも 3 系統が並存する。
- **AMD Software: Adrenalin Edition**（コンシューマ・毎月〜隔月）
- **AMD Software: PRO Edition**（旧 Radeon Pro Software for Enterprise。四半期 xx.Qn 版。22.Q2 以降この名称）
- **AMD Software: Cloud Edition**（Radeon PRO V620 等のクラウド向け）

PRO Edition 25.Q4 リリースノート p.12「AMD Radeon Graphics Support」は「19.Q2 以降、PRO Edition に Radeon（コンシューマ）製品を
含めている。企業が業務用・民生用ハードを混在させるため。ゲーム用途には非推奨。ワークステーション性能・機能・ISV 認証は
Radeon には適用されず、Radeon PRO 以外では利点はない」と明記。互換表には RX 7000/6000/5700/5500/Vega/RX 500/400 が並ぶ
（RX 9000 は未掲載）。つまり **コンシューマ RX に PRO Edition は公式にインストール可能**。

カーネルドライバの共有: PRO 25.Q2 のパッケージ版は RDNA 向け「WHQL 25.10.10 / Store 32.0.21010.10」、Vega/Polaris 向け
「23.19.23.01」の 2 本立てで、Adrenalin と同じ `amdkmdag.sys` 系の版番号体系。同一バイナリかどうかは AMD が明言しておらず
**未検証**（フォーラムでは「ほぼ同じパッケージ」[伝聞]）。

**判定: Claim A は誤り（部分的に正しい）。** NVIDIA の Game Ready/Studio/Enterprise に相当する複数系統が存在する。ただし
「NVIDIA のように 10-bit 出力を Studio 系統で解禁するといった機能差の系統分けはない」という意味なら、AMD 側にそのような
制限は歴史的に無く、その限りでは一理ある。

### C-2. 表示パイプライン精度とディザ

- **[公式]** Linux カーネル文書（DCN overview）: DPP で内部浮動小数点形式に変換し、OPP まで維持。OPP で「bit-depth
  reduction/dithering」と regamma を行い、DIO で整数出力。同文書は「AMD の表示エンジンは他 OS と部分的に共有され、
  DC（OS 非依存部）と DM（OS 依存部）に分かれる」と記す。したがって DC のディザ規定は Windows 側の挙動を推定する最良の一次情報。
- **[公式・ソース]** `dc/core/dc_resource.c` `resource_build_bit_depth_reduction_params()`（現行 master）:
  `DITHER_OPTION_DEFAULT` のとき出力深度 6/8/10 bpc → `SPATIAL6/8/10`（HIGHPASS_RANDOM=1、RGB 出力なら RGB_RANDOM=1、
  **FRAME_RANDOM=0**＝フレーム間で乱数を変えない静的空間ディザ、テンポラル FM 無し）、**12 bpc → DISABLE**。stream は
  kzalloc なので未指定時は DEFAULT。すなわち **10 bpc 出力でもディザは切れず、内部精度→10 bit への空間ディザが掛かる**設計
  （「10-bit にするとディザ無効」というフォーラム説 [伝聞] とは異なる）。
- **[公式]** ADL 公開 API（`ADL_DL_DISPLAY_DITHER_*`）に Driver Default／Disabled／FM6/8/10（テンポラル）／DITH6/8/10（空間）／
  TRUN 等 24 モードが定義され、Windows でもディザ深度をアプリから設定できる。ColorControl は AMD の「dithering」設定に対応と明記。
- **[公式]** Adrenalin の「Color Depth」は接続・表示器に応じ 6/8/10/12 bpc を提示（FAQ DH3-008、2024-11-08 更新）。
  「10-Bit Pixel Format」は別物で、22.7.1（22.20.15.01）から「HDR と非互換。HDR 表示では無効化を推奨」[公式]。
- **[伝聞・レジストリ]** 旧来の `DP_/HDMI_/TMDS_/Embedded_DisableDither`（`atikmdag.sys` 配下）は HP Anyware/VPixx が案内するが、
  ColorControl issue #343（2024-03）は「新ドライバはその文字列を含まない」と指摘＝RDNA 世代で有効かは**未検証**。
- **[実測] AMD が線上で真の 10-bit コードを出すか／8-bit＋ディザかを DeckLink 等で計測した公開データは見つからなかった。**

### C-3. AMD vs NVIDIA のグラデーション比較（Claim B「Radeon の方がグラデーション再現が良い」）

- **[伝聞]** Tom's HW（2020-07）R9 290→RTX 2080S で「8bit+ディザ的な」バンディング、guru3D/NVIDIA フォーラムで「AMD はディザ既定
  ON、NVIDIA Windows はデスクトップ 8-bit でディザ無し」という報告が多数。写真付きの同一モニタ比較・キャプチャデータは
  **アクセスできる範囲で見つからず**。
- **[実測・部分]** NVIDIA 開発者フォーラム 346429 の OP は同一コンテンツを NVIDIA/Intel/AMD iGPU で撮影比較し、AMD iGPU では
  不均一バンディングが出ないと写真で示した（絶対的な 10-bit 精度計測ではない）。
- **[公式]** NVIDIA スタッフ（2024-02、A4000 スレッド）: ディザは「どの OS のコントロールパネルでも設定項目ではない」。
  ColorControl #335（2024-03）は A4000 で「下位レベルの別ディザ」をロスレスキャプチャで確認と主張＝本リポジトリの Ampere
  計測（8-bit 格子＋テンポラル）と整合。

**判定: Claim B は「SDR 8-bit デスクトップのバンディング」に限れば概ね支持されるが、根拠は主観比較と NVIDIA 側の既定ディザ無し
（または 8-bit 基準ディザ）に由来し、HDR 10-bit の量子化精度を計測した比較は存在しない。** ソース上 AMD の 10 bpc 出力は
「12→10 の空間ディザ」であり、真の 10-bit コード配置が均一かどうかは実測が必要。

### C-4. AMD の Windows HDR 出力

**[公式]** 10-Bit Pixel Format と HDR の非互換（22.7.1〜）。Auto HDR 終了後に色が「washed out」になる既知不具合（24.4.1）。
FreeSync Premium Pro は `FreeSync2_Gamma22`（R10G10B10A2、表示器ネイティブ色域）と `FreeSync2_scRGB`（FP16）の 2 モードで
OS トーンマップをバイパス（GPUOpen）。**[伝聞]** 9070 XT の HDR ゲーム内バンディング報告（pcforum.amd.com、2025-09。内容未取得）。
NVIDIA 346429 に相当する「全画面 R10G10B10A2 の不均一量子化」の AMD 報告は**見つからず**。

### C-5. 全画面（Independent Flip/MPO）と DWM 合成でのディザ差

**[公式]** DC のディザは OPP（スキャンアウト直前）で stream 単位に掛かるため、設計上はプレゼンテーション経路に依存しない。
AMD による経路別の明言は無し。**未検証**。

### C-6. 未検証事項

Windows 版での FRAME_RANDOM 既定値・RDNA3/4 の DisableDither レジストリ有効性・線上コードの実測・PRO/Adrenalin のバイナリ同一性。
本リポジトリの DeckLink 計測を AMD（RDNA3/4 の dGPU と APU）で行えば、公開情報に無い一次データになる。

### C-7. 出典

- https://drivers.amd.com/relnotes/amd_software_pro_software_25.q4_full_set.pdf — PRO Edition 25.Q4 リリースノート（p.12 Radeon 対応の方針・互換表）
- https://www.amd.com/en/resources/support-articles/release-notes/RN-PRO-WIN-25-Q2.html — PRO 25.Q2 パッケージ版番号
- https://www.amd.com/en/support/downloads/previous-drivers.html/graphics/radeon-pro/radeon-pro-v-series/radeon-pro-v620.html — Cloud Edition の存在
- https://www.amd.com/en/resources/support-articles/faqs/DH3-008.html — Color Depth 設定 FAQ
- https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-22-7-1.html — 10-Bit Pixel Format と HDR 非互換
- https://docs.kernel.org/gpu/amdgpu/display/dcn-overview.html — DCN パイプライン（OPP でのディザ）
- https://docs.kernel.org/gpu/amdgpu/display/index.html — DC が他 OS と共有される旨
- https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/display/dc/core/dc_resource.c — `resource_build_bit_depth_reduction_params`（既定ディザ規定）
- https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/display/dc/dc_hw_types.h — `enum dc_dither_option`
- https://gpuopen-librariesandsdks.github.io/adl/group__define__dither__states.html — ADL ディザ状態定義
- https://gpuopen.com/learn/using-amd-freesync-2-hdr-color-spaces/ — FreeSync Premium Pro HDR モード
- https://forums.developer.nvidia.com/t/uneven-banding-in-fullscreen-hdr-output-with-r10g10b10a2-swapchain/346429 — NVIDIA 不均一量子化（AMD/Intel 比較写真）
- https://forums.developer.nvidia.com/t/disable-dithering-on-rtx-a4000/279427 — NVIDIA スタッフ「ディザは設定項目ではない」
- https://github.com/Maassoft/ColorControl/issues/335 — 新世代 NVIDIA の下位ディザ（キャプチャ主張）
- https://github.com/Maassoft/ColorControl/issues/343 — AMD 新ドライバで DisableDither 文字列が無い
- https://github.com/xiao-mantou/GamingTweaks-CHS/blob/master/.github/Registry%20tweaks/Dithering/AMD/Disable%20dithering.reg — AMD ディザ無効化レジストリ例
- https://anyware.hp.com/knowledge/how-do-i-turn-off-temporal-dithering-in-an-amd-graphics-card — HP の AMD ディザ無効化案内
- https://docs.vpixx.com/vocal/diagnosing-and-disabling-dithering-in-the-graphics — VPixx のディザ診断手法
- https://forums.tomshardware.com/threads/switched-a-amd-card-for-a-nvidia-card-and-now-only-got-8bit-dither-gradients.3628924/ — AMD→NVIDIA 乗換でのバンディング報告（2020）
- https://lkml.iu.edu/1812.1/03111.html — amdgpu「max bpc」プロパティ（既定 8 bpc）
- https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-24-4-1.html — Auto HDR 後の washed out 既知不具合

---

## §D. 「Radeon なら iFLIP HDR10 で Bit-Exact になるか」— 生成 AI（Gemini）回答の検証（2026-09-04）

NVIDIA iFLIP の HDR10 が Bit-Exact でないことが確定した後、「Radeon ではどうか」を Gemini に尋ねた回答が 2 件ある
（1 件目はリポジトリ提示前、2 件目は本リポジトリを提示して再検証させたもの）。どちらも一見もっともらしいが、
機序の説明と具体的な回避策に誤りがあったので、主張ごとの判定と一次情報を記録する。凡例は §A〜§C と同じ。
**[本実測]**＝本リポジトリの DeckLink 計測（RESULTS.md）。

### D-1. 前提: 本リポジトリの実測が固定している事実

- **[本実測]** RTX 5090 Laptop（Blackwell）: PresentMon で全 Present が `Hardware: Independent Flip` の状態で、
  R10G10B10A2 に ≈16 コード周期の 2 コード飛び 49 箇所。**同じ窓を DWM 合成（`Composed: Flip`）に降格させると
  周期飛びは全消滅**し、代わりに近黒（≤144）23 コードの欠落に置き換わる（PQ→FP16→PQ 往復）。FP16 scRGB は
  iFLIP でも合成でもランプ行がビット一致で、丸め精度で正確（RESULTS.md §6）。
- **[本実測]** RTX 3070（Ampere）: 全 716 Present が iFLIP の状態で、FP16 / R10G10B10A2 とも 8bit 格子＋
  ランダム時空間ディザ。スワップチェーン形式に依存しない出力段の性質（RESULTS.md §7）。
- したがって劣化は **GPU のスキャンアウト段（表示エンジン）に固有**で、DWM の処理ではない。合成経路は
  「別の劣化」を持つ別経路であって、Bit-Exact な経路ではない。

### D-2. 第 1 回答（リポジトリ提示前）の主張と判定

| 主張 | 判定 | 根拠 |
|---|---|---|
| HDR 有効時は ACM（自動色管理）が背後で動き、iFLIP でも DWM が PQ を scRGB FP16 に再マップして再エンコードするので、GPU を問わず OS レベルで Bit-Exact でない | **誤り** | **[公式]** 「自動色管理」のトグルは **SDR ディスプレイ向け**機能で、MHC ICC プロファイルで provisioning された機種のみ対象（MS Learn「Use DirectX with Advanced Color」「ICC profile behavior with Advanced Color」）。HDR モードの色管理（DWM が各アプリを CCCS＝scRGB FP16 へ変換 → 表示カーネルが線形式へ変換）は**合成される内容にだけ**掛かる。**[公式]** flip model 文書: DirectFlip/Independent Flip は「デスクトップ合成を完全にバイパスしてアプリのフレームを直接画面へ送る。排他的フルスクリーンと同じ方式」。**[本実測]** 合成 vs iFLIP で劣化の形が変わる（D-1）。出典 [1] は SDR/ACM/8bit に関する MS Q&A の一般投稿で HDR も iFLIP も扱っていない |
| Radeon の「10-Bit Pixel Format」は HDR と競合するので OFF にする必要があり、それは OS の色再計算を受け入れることを意味する | **前半は正しい・後半は飛躍** | **[公式]** AMD 22.7.1 リリースノート「10-Bit Pixel Format は HDR と非互換・HDR 表示では無効化推奨」（§C-2）。ただしこれは OpenGL の 10bit バッファ用設定で、OFF にしても iFLIP の直接スキャンアウトは変わらない |
| 排他的フルスクリーン（FSE）なら DWM/ACM をバイパスしてフレームバッファの 10bit がそのまま出る | **誤り** | **[公式]** Windows 10 1803 以降の Fullscreen Optimizations は FSE の flip model アプリを「ボーダレス＋iFLIP」として動かす（DirectX Developer Blog）。**[公式]** iFLIP は「FSE と同じ効率で直接送出」（flip model 文書）。真の FSE でも画素は同じ表示エンジン（LUT・CSC・ディザ）を通り、表示エンジンを経ずに線へ出る経路は存在しない。**[本実測]** iFLIP＝既に DWM をバイパスした状態で劣化が出ている |
| AMD ドライバは D3D11/12 の完全排他 FSE ＋ 10bit の瞬間だけ OS をバイパスするネイティブ HDR10 経路を通す（出典 [6]） | **出典の誤読** | [6] は Steam のゲームスレッドで、Doom9 の「AMD の**プライベート HDR 切替 API** は D3D11 全画面 10bit 再生中しか効かない」という書き込みの転載 **[伝聞]**。HDR モード切替（メタデータ送出）の話で、画素経路の話ではない |
| madVR の HDR パススルーで OS の色管理バッファを素通りできる | **誤解** | **[伝聞・複数一致]** madVR の「passthrough」は NVIDIA/AMD のプライベート API で HDR インフォフレームを直接送り、Windows の HDR 切替を使わずに表示器を HDR にする機能（Windows 10 の HDR 対応以前の回避策。AVS Forum / Doom9）。画素は同じ GPU 表示パイプラインを通る |
| 「Radeon でも iFLIP のままの Bit-Exact は不可能。NVIDIA の問題はおそらく DWM/MPO の仕様」 | **根拠なし・実測と矛盾** | **[本実測]** RTX 50 の FP16 scRGB は iFLIP で丸め精度まで正確かつ合成経路とビット一致。「OS が必ず崩す」なら scRGB も崩れるはず。**[実測なし]** Radeon の線上コードは誰も測っていない（§C-2）。**[公式・ソース]** DC の既定は 10 bpc で静的空間ディザ（§C-2）＝別種の非 Bit-Exact が予想される |

出典の質: [1] MS Q&A 一般投稿、[2]〜[5] LTT / Reddit、[6] Steam スレッド。ベンダー文書・OS 文書の一次情報は含まれていない。

### D-3. 第 2 回答（リポジトリ提示後）の主張と判定

リポジトリの実測（RTX 50 の周期飛び・Ampere の 8bit 格子＋ディザ・FP16 が正確・合成で周期飛びが消える）の
読み取りは正確。origin は公開リポジトリ `yadonpapa/20260830_HDR-Swapchain-Capture` で、実際に読めたと考えてよい。
問題は **Gemini が独自に付け加えた部分**にある。

| 主張 | 判定 | 根拠 |
|---|---|---|
| Radeon PRO 系の「AMD Radiance Display Engine」 | **ほぼ正しい（帰属が不正確）** | **[公式]** Radiance Display Engine は RDNA 3（RX 7900）で導入された表示エンジンの名称で 12 bpc・DP 2.1 対応。PRO 専用ではなく、RX 9070（RDNA 4）も同系統 |
| 内部精度は 12bit 以上で、NVIDIA のような周期的 LUT バグは公開検証で報告なし | **正しいが「未計測」を「無い」に読み替えないこと** | **[公式]** DCN 文書: DPP で内部浮動小数点に変換し OPP まで保持（§C-2）。AMD の同種報告が無いのは事実だが、DeckLink 級の計測が存在しない |
| Radeon は 10bpc でも空間ディザを強制し、フレーム間変動なしで隣接画素に数コードのパターンが出る | **本リポジトリ §C-2 の推定と一致（未実測）** | **[公式・ソース]** `resource_build_bit_depth_reduction_params`: 10bpc 出力で SPATIAL10・FRAME_RANDOM=0、12bpc で DISABLE。Windows 実測は無い。振幅は内部値→10bit の LSB なので ±1 コード程度が予想され「数コード」ではない。Gemini の「伝統的に知られる」は本リポジトリの推定の言い換えとみられる |
| FP16 scRGB に一本化すれば Radeon でも最も正確 | **半分正しい** | RTX 50 では実測どおり。Radeon では DC のディザが OPP（パイプライン末尾）で stream 単位に掛かるため、**FP16 でも同じ空間ディザが乗る**設計。10bit 入力固有の LUT 経路を避ける効果はあり得る。iFLIP で「DWM が FP16 を扱う」という説明は D-2 と同じ誤り |
| `PP_ThermalRegulatorOptions`・`TMDS_Dither`・`DP_Dither` を 0 にしてディザ回路を物理的に停止できる | **誤り（名前・極性とも）** | `PP_` 接頭辞は PowerPlay の電力/温度系（`PP_ThermalAutoThrottlingEnable` 等）でディザと無関係。**[伝聞・複数一致]** 実在する値は `DP_DisableDither` / `HDMI_DisableDither` / `TMDS_DisableDither` / `Embedded_DisableDither` を **1** にする（VPixx 文書、公開 .reg、HP Anyware）。**[伝聞]** ColorControl Issue #343: これらは `atikmdag.sys` 由来で「新しいドライバはその文字列を含まない」＝RDNA 世代（`amdkmdag.sys`）で効くかは未検証 |
| Adrenalin/PRO の標準 UI にディザのオフスイッチが無いのでレジストリしかない | **前半は正しい・後半は誤り** | **[公式]** 公開 ADL API `ADL2_Display_DitherState_Set` に `ADL_DL_DISPLAY_DITHER_DISABLED`（0）を含む 24 モード（§C-2）。**[実装確認]** ColorControl の AMD サービス（`AmdService.cs` `SetDithering` → `ADLWrapper.SetDisplayDitherState`）がこの API を呼んでおり、GUI から設定できる |
| DWM 合成（Composed: Flip）を介在させた方が線形性が担保されやすいというパラドックス | **不完全で誤解を招く** | **[本実測]** RTX 50 の合成経路は周期飛びが消える代わりに近黒 23 コードが欠落（D-1）。Bit-Exact ではなく別種の劣化。**[公式・ソース]** AMD ではディザが OPP で stream 単位に掛かるため、設計上は合成経路でも消えない（§C-5） |
| Radeon 移行は検証価値が高い | **同意** | §C-6 / 統合要約 3 と一致。ただし「条件付きで可能」の条件が上記の効かない可能性が高いレジストリ値に依存しており、現時点の正直な答えは「未計測・機序は NVIDIA と別」 |

### D-4. Radeon で Bit-Exact を狙うときの手順（公開情報に基づく優先順）

1. **ADL でディザを DISABLED にする。** ColorControl の AMD プリセットで設定でき、自作不要。設定前後を DeckLink で撮り、
   単一フレーム内の 1px 孤立反転が消えるかを見る。
2. **12 bpc リンクで撮る。** DC の既定は 12bpc 出力でディザ無効。DeckLink 8K Pro G2 の HDMI 入力 EDID は DC_36bit を
   広告している（RESULTS.md §7）ので、この機材なら試せる。UltraStudio 4K Mini は DC_30 のみ（RESULTS.md §6）で不可。
3. **レジストリの `*_DisableDither=1` は最後に、効果を実測で確認する前提で。** 現行ドライバに文字列が無いという報告があり
   期待値は低い。
4. いずれも PresentMon で `Hardware: Independent Flip` を同時記録し、FP16 と R10G10B10A2 の両方を撮る。DC の設計上、
   FP16 にも同じ OPP 段ディザが乗ると予想されるので、両形式の差は「10bit 入力固有の LUT 経路の有無」を切り分ける。

### D-5. 出典

- https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range — Advanced Color: HDR モードの CCCS（scRGB FP16）合成・2 段階色管理、SDR 向け自動色管理は provisioning 済み機種のみ（公式）
- https://learn.microsoft.com/en-us/windows/win32/wcs/advanced-color-icc-profiles — ICC profile behavior with Advanced Color（公式）
- https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model — DirectFlip / Independent Flip は「デスクトップ合成を完全にバイパス・FSE と同じ方式」（公式）
- https://devblogs.microsoft.com/directx/demystifying-full-screen-optimizations/ — Fullscreen Optimizations: FSE をボーダレスで実行（公式ブログ）
- https://learn.microsoft.com/en-us/surface/configure-sdr-and-hdr-display — Surface の SDR/HDR 計測設定（ACM は SDR モードの項目）（公式）
- https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md — PresentMode の定義（Hardware: Independent Flip / Composed: Flip）
- https://learn.microsoft.com/en-us/answers/questions/5757709/windows-11-not-rendering-10bit-color-unles-acm-is — Gemini 出典 [1]（MS Q&A 一般投稿。SDR/ACM/8bit の話）
- https://steamcommunity.com/app/306760/discussions/1/1486613649677087480/?l=italian&ctp=2 — Gemini 出典 [6]（Steam スレッド。Doom9 の AMD プライベート HDR 切替 API の転載）
- https://www.avsforum.com/threads/madvr-hdr-passthrough-isnt-working.2880609/ — madVR HDR passthrough＝プライベート API による HDR 切替（伝聞）
- https://www.amd.com/en/resources/support-articles/release-notes/RN-RAD-WIN-22-7-1.html — 10-Bit Pixel Format と HDR 非互換（公式・§C-7 再掲）
- https://gpuopen-librariesandsdks.github.io/adl/group__define__dither__states.html — ADL ディザ状態（DISABLED / DRIVER_DEFAULT / DITH10 / FM10 …）（公式）
- https://gpuopen-librariesandsdks.github.io/adl/group__DISPLAYAPI.html — `ADL_Display_DitherState_Get/Set`（公式）
- https://github.com/Maassoft/ColorControl/blob/master/ColorControl/Services/AMD/AmdService.cs — ColorControl の AMD ディザ設定実装（`ADL2_Display_DitherState_Set`）
- https://github.com/Maassoft/ColorControl/issues/343 — 新ドライバに `*_DisableDither` 文字列が無い（伝聞）
- https://docs.vpixx.com/vocal/diagnosing-and-disabling-dithering-in-the-graphics — `DP_DisableDither` / `TMDS_DisableDither` = 1（VPixx）
- https://github.com/xiao-mantou/GamingTweaks-CHS/blob/master/.github/Registry%20tweaks/Dithering/AMD/Disable%20dithering.reg — 公開 .reg（DP/HDMI/TMDS/Embedded_DisableDither・`{4D36E968-…}\0000`）
- https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/display/dc/core/dc_resource.c — DC 既定ディザ（10bpc SPATIAL10・12bpc DISABLE）（公式・§C-7 再掲）
- https://www.techpowerup.com/300632/amd-announces-the-usd-999-radeon-rx-7900-xtx-and-usd-899-rx-7900-xt-5nm-rdna3-displayport-2-1-fsr-3-0-fluidmotion — Radiance Display Engine（RDNA 3・12 bpc・DP 2.1）
