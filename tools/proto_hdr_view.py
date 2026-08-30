"""P10 Phase 2 プロトタイプ: QQuickWindow + QSG_RHI_HDR による 4 方式出力の実証。

経路（P10仕様 §6）: numpy → QImage(Format_RGBA16FPx4) →
QQuickWindow.createTextureFromImage → QSGSimpleTextureNode（Python updatePaintNode）。
スワップチェーンは環境変数 QSG_RHI_HDR=scrgb|hdr10 で要求する。

用法:
  uv run python tools/proto_hdr_view.py --list                  # 画面一覧＋HDR 状態
  uv run python tools/proto_hdr_view.py --mode auto --screen 0  # 全画面表示
  uv run python tools/proto_hdr_view.py --mode scrgb --screen 1 --windowed
  uv run python tools/proto_hdr_view.py --mode scrgb --windowed --auto-close 2  # スモーク
操作: ESC / Q で閉じる。

HDR モニタでの目視判定（P10仕様 §8 Phase 2）:
  - 上段は輝度パッチ列（2 → 2000 nit 相当）。scrgb/hdr10 が機能していれば、
    SDR 白（既定 203 nit）のパッチより右が「白より明るく光る」段階として見える。
  - 中段は左=SDR 白（sdr_white nit）/ 右=1000 nit の大面積比較。
    sRGB モード（またはクリップ時）は左右が同じ明るさになる。
  - 下段は 0 → 2000 nit の PQ 空間ランプ（知覚的に滑らかな傾斜）。
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402

from pq import _pq_eotf, _pq_oetf, _srgb_oetf  # noqa: E402
from hdr_display import info_for_screen_name  # noqa: E402

PATCH_NITS = [2, 5, 10, 20, 40, 80, 120, 160, 203, 300, 400, 600, 800, 1000, 1500, 2000]
TEX_W, TEX_H = 1920, 1080


def encode_nits(nits: np.ndarray, mode: str, sdr_white: float) -> np.ndarray:
    """絶対輝度(nit) → 各モードでスワップチェーンに渡す値（P10仕様 §5）。"""
    nits = np.asarray(nits, dtype=np.float64)
    if mode == "scrgb":
        return nits / 80.0                      # scRGB: 1.0 = 80 nits（リニア）
    if mode == "hdr10":
        return _pq_oetf(nits / 10000.0)         # PQ 符号化 0..1
    # srgb: SDR 白を 1.0 に正規化してクリップ → sRGB OETF
    return _srgb_oetf(np.clip(nits / sdr_white, 0.0, 1.0))


def build_pattern_nits() -> np.ndarray:
    """テストパターンを絶対輝度(nit)の 2D 配列で組む（モード非依存）。"""
    img = np.zeros((TEX_H, TEX_W), dtype=np.float64)
    # 上段: 輝度パッチ列（y 40..320）
    pw = TEX_W // len(PATCH_NITS)
    for i, nit in enumerate(PATCH_NITS):
        img[40:320, i * pw:(i + 1) * pw - 4] = nit
    # 中段: 左=SDR 白(203) / 右=1000 nit の大面積比較（y 420..720）
    img[420:720, 40:TEX_W // 2 - 20] = 203.0
    img[420:720, TEX_W // 2 + 20:TEX_W - 40] = 1000.0
    # 下段: PQ 空間で 0 → 2000 nit のランプ（y 800..1040）
    code = np.linspace(0.0, float(_pq_oetf(np.array(2000.0 / 10000.0))), TEX_W)
    ramp = _pq_eotf(code) * 10000.0
    img[800:1040, :] = ramp[None, :]
    return img


def build_fp16_qimage(mode: str, sdr_white: float):
    """パターン → FP16 RGBA QImage（テクスチャ元）。"""
    from PyQt6.QtGui import QImage

    vals = encode_nits(build_pattern_nits(), mode, sdr_white)
    rgba = np.zeros((TEX_H, TEX_W, 4), dtype=np.float16)
    rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = vals.astype(np.float16)
    rgba[..., 3] = 1.0
    buf = np.ascontiguousarray(rgba)
    img = QImage(buf.data, TEX_W, TEX_H, TEX_W * 8,
                 QImage.Format.Format_RGBA16FPx4).copy()
    _draw_labels(img, mode, sdr_white)
    return img


def _draw_labels(img, mode: str, sdr_white: float) -> None:
    """パッチ下に nit 値の文字を描く（FP16 QImage への QPainter が不可なら黙って省略）。"""
    from PyQt6.QtGui import QColor, QFont, QPainter

    try:
        p = QPainter(img)
        if not p.isActive():
            return
        p.setFont(QFont("Segoe UI", 20))
        p.setPen(QColor(255, 255, 255))
        pw = TEX_W // len(PATCH_NITS)
        for i, nit in enumerate(PATCH_NITS):
            p.drawText(i * pw + 8, 360, f"{nit}")
        p.drawText(60, 770, f"SDR white ({sdr_white:.0f} nit)")
        p.drawText(TEX_W // 2 + 40, 770, "1000 nit")
        p.drawText(60, 34, f"mode={mode}  (QSG_RHI_HDR={os.environ.get('QSG_RHI_HDR', '-')})")
        p.end()
    except Exception as e:  # noqa: BLE001
        print(f"ラベル描画スキップ（FP16 QImage への QPainter 不可）: {e}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--mode", choices=["auto", "srgb", "scrgb", "hdr10"], default="auto")
    ap.add_argument("--screen", type=int, default=0, help="表示先画面 index（--list 参照）")
    ap.add_argument("--sdr-white", type=float, default=0.0,
                    help="SDR 白レベル nits（0=表示先から検出。検出不可なら 203）")
    ap.add_argument("--windowed", action="store_true", help="全画面にせず窓表示")
    ap.add_argument("--auto-close", type=float, default=0.0, help="指定秒後に自動終了")
    ap.add_argument("--list", action="store_true", help="画面一覧と HDR 状態を表示して終了")
    args = ap.parse_args()

    # 診断ログ（rhi/scenegraph）。Windows では Qt ログがコンソールに出ないことが
    # あるため、メッセージハンドラで確実に拾う（スワップチェーン形式の確認用）。
    os.environ.setdefault("QSG_INFO", "1")
    os.environ.setdefault("QT_LOGGING_RULES", "qt.rhi.general=true")
    from PyQt6.QtCore import qInstallMessageHandler

    def _qt_msg(_m, ctx, msg):
        cat = ctx.category or ""
        if cat.startswith(("qt.rhi", "qt.scenegraph")) or cat == "default":
            print(f"[qt] {cat}: {msg}")

    qInstallMessageHandler(_qt_msg)

    from PyQt6.QtGui import QGuiApplication
    app = QGuiApplication(sys.argv)
    screens = app.screens()

    if args.list:
        for i, s in enumerate(screens):
            info = info_for_screen_name(s.name())
            print(f"[{i}] {s.name()}  {s.geometry().width()}x{s.geometry().height()}"
                  f"  -> {info.summary() if info else 'HDR 状態不明'}")
        # Python ハンドラを残したまま終了すると interpreter 終了後の Qt ログで segfault
        qInstallMessageHandler(None)
        return 0

    if not 0 <= args.screen < len(screens):
        print(f"--screen {args.screen} は範囲外（0..{len(screens) - 1}）")
        return 2
    screen = screens[args.screen]
    info = info_for_screen_name(screen.name())

    mode = args.mode
    if mode == "auto":
        mode = "scrgb" if (info and info.hdr_enabled) else "srgb"
        print(f"auto 解決: 表示先 {screen.name()} の HDR "
              f"{'有効' if (info and info.hdr_enabled) else '無効/不明'} -> {mode}")
    sdr_white = args.sdr_white
    if sdr_white <= 0:
        sdr_white = info.sdr_white_nits if (info and info.sdr_white_nits > 80.0) else 203.0

    # QSG_RHI_HDR はシーングラフ初期化（最初の QQuickWindow 生成時）に評価される。
    # QGuiApplication 生成後でも、ウィンドウ生成前ならここで設定して間に合う。
    if mode in ("scrgb", "hdr10"):
        os.environ["QSG_RHI_HDR"] = mode
    else:
        os.environ.pop("QSG_RHI_HDR", None)

    print(f"出力方式: {mode}  SDR白: {sdr_white:.0f} nits  "
          f"表示先: {screen.name()} ({info.friendly_name if info else '?'})")
    if mode in ("scrgb", "hdr10") and not (info and info.hdr_enabled):
        print("警告: 表示先の Windows HDR が無効です。>SDR白 はクリップ/破綻して見えます。")

    from PyQt6.QtCore import Qt, QTimer
    from PyQt6.QtGui import QColor
    from PyQt6.QtQuick import (QQuickItem, QQuickWindow, QSGSimpleTextureNode,
                               QSGTexture)

    image = build_fp16_qimage(mode, sdr_white)
    print(f"テクスチャ元 QImage: {image.width()}x{image.height()} format={image.format()}")

    from PyQt6 import sip

    class TextureItem(QQuickItem):
        def __init__(self, img, parent=None):
            super().__init__(parent)
            self._img = img
            self._tex = None
            self.setFlag(QQuickItem.Flag.ItemHasContents, True)

        def updatePaintNode(self, node, _data):  # レンダースレッドで呼ばれる
            if node is None:
                node = QSGSimpleTextureNode()
            if self._tex is None:
                self._tex = self.window().createTextureFromImage(self._img)
                print(f"テクスチャ生成: size={self._tex.textureSize().width()}"
                      f"x{self._tex.textureSize().height()}")
                node.setFiltering(QSGTexture.Filtering.Linear)
            node.setTexture(self._tex)
            node.setRect(0, 0, self.width(), self.height())
            return node

        def release_texture(self):
            # rhi 破棄前（sceneGraphInvalidated・レンダースレッド上）に C++ 側を
            # 即時削除する。放置すると終了時に Python GC が rhi 亡き後に触って segfault。
            if self._tex is not None:
                sip.delete(self._tex)
                self._tex = None

    class ViewerWindow(QQuickWindow):
        def __init__(self):
            super().__init__()
            self.setColor(QColor(0, 0, 0))
            self.setTitle(f"P10 proto — {mode}")
            self._item = TextureItem(image, self.contentItem())
            self.sceneGraphInvalidated.connect(
                self._item.release_texture, Qt.ConnectionType.DirectConnection)
            self._fit()

        def _fit(self):
            self._item.setSize(self.size().toSizeF()
                               if hasattr(self.size(), "toSizeF") else self.size())
            self._item.setWidth(float(self.width()))
            self._item.setHeight(float(self.height()))

        def resizeEvent(self, ev):
            super().resizeEvent(ev)
            self._fit()

        def keyPressEvent(self, ev):
            if ev.key() in (Qt.Key.Key_Escape, Qt.Key.Key_Q):
                self.close()
            else:
                super().keyPressEvent(ev)

    win = ViewerWindow()
    win.setScreen(screen)
    win.frameSwapped.connect(lambda: None)  # 生存確認用（初回描画で以降のログが出る）
    g = screen.geometry()
    if args.windowed:
        win.setGeometry(g.x() + 80, g.y() + 80, 960, 540)
        win.show()
    else:
        win.setGeometry(g)
        win.showFullScreen()

    if args.auto_close > 0:
        # quit 直叩きでなく close 経由（レンダースレッドの後始末を通常経路で通す）
        QTimer.singleShot(int(args.auto_close * 1000), win.close)
    rc = app.exec()
    qInstallMessageHandler(None)
    del win
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
