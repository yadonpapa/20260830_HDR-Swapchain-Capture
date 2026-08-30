"""Windows ディスプレイの HDR（Advanced Color）状態検出（P10仕様 §4）。

user32 の QueryDisplayConfig / DisplayConfigGetDeviceInfo を ctypes で直接呼ぶ
（追加依存なし）。取得するのはアクティブなディスプレイパスごとの
GDI ソース名（QScreen.name() と一致）・モニタ名・HDR 対応/有効・SDR 白レベル。

- Win11 24H2+ の GET_ADVANCED_COLOR_INFO_2（activeColorMode で SDR/WCG/HDR を区別）を
  優先し、失敗時は従来の GET_ADVANCED_COLOR_INFO へフォールバックする
  （旧 API の advancedColorEnabled は 24H2+ では SDR の自動色管理でも立つため）。
- Windows 以外では probe() が空リストを返す no-op。

CLI プローブ:
    uv run python -m core.hdr_display
"""

from __future__ import annotations

import sys
from dataclasses import dataclass


@dataclass
class DisplayHdrInfo:
    """1 ディスプレイパス分の HDR 状態。"""

    gdi_name: str = ""            # 例 "\\\\.\\DISPLAY1"（QScreen.name() と一致）
    friendly_name: str = ""       # モニタ名（EDID）
    hdr_supported: bool = False   # ディスプレイが HDR 対応か
    hdr_enabled: bool = False     # Windows 設定で「HDR を使用する」がオンか
    active_color_mode: str = ""   # "SDR" / "WCG" / "HDR"（新 API 時のみ。旧 API では ""）
    bits_per_channel: int = 0
    sdr_white_nits: float = 0.0   # HDR 有効時の SDR コンテンツ白レベル（nits）
    api: str = ""                 # 判定に使った API: "ACI2"（24H2+）/ "ACI"（従来）

    def summary(self) -> str:
        st = "HDR 有効" if self.hdr_enabled else ("HDR 対応(オフ)" if self.hdr_supported else "SDR")
        sdr = f"・SDR白 {self.sdr_white_nits:.0f} nits" if self.sdr_white_nits > 0 else ""
        return f"{self.gdi_name} {self.friendly_name}: {st}{sdr}"


def probe() -> list[DisplayHdrInfo]:
    """全アクティブディスプレイの HDR 状態を返す（Windows 以外は空リスト）。"""
    if sys.platform != "win32":
        return []
    try:
        return _probe_win32()
    except Exception:
        # 検出失敗でアプリ本体を巻き込まない（呼び出し側は「不明」扱い）
        return []


def info_for_screen_name(name: str) -> DisplayHdrInfo | None:
    """QScreen.name() に対応するパスの情報を返す。

    Qt6/Windows の QScreen.name() は GDI 名（\\\\.\\DISPLAYn）ではなく
    モニタ名（EDID friendly name。例 "HP E242"）を返す（2026-07-17 実測）ため、
    GDI 名 → モニタ名（一意のときのみ）の順で突合する。
    同型モニタが複数あると特定できない（Phase 3 で矩形ベースの対応付けを検討）。
    """
    infos = probe()
    for info in infos:
        if info.gdi_name == name:
            return info
    hits = [i for i in infos if i.friendly_name == name]
    return hits[0] if len(hits) == 1 else None


# --- 以下 Windows 実装（ctypes） ---------------------------------------------

def _probe_win32() -> list[DisplayHdrInfo]:
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.windll.user32

    QDC_ONLY_ACTIVE_PATHS = 2
    ERROR_SUCCESS = 0
    # DISPLAYCONFIG_DEVICE_INFO_TYPE
    GET_SOURCE_NAME = 1
    GET_TARGET_NAME = 2
    GET_ADVANCED_COLOR_INFO = 9
    GET_SDR_WHITE_LEVEL = 11
    GET_ADVANCED_COLOR_INFO_2 = 15   # Win11 24H2+（wingdi.h の連番）

    class LUID(ctypes.Structure):
        _fields_ = [("LowPart", wintypes.DWORD), ("HighPart", wintypes.LONG)]

    class RATIONAL(ctypes.Structure):
        _fields_ = [("Numerator", wintypes.UINT), ("Denominator", wintypes.UINT)]

    class PATH_SOURCE_INFO(ctypes.Structure):
        _fields_ = [("adapterId", LUID), ("id", wintypes.UINT),
                    ("modeInfoIdx", wintypes.UINT), ("statusFlags", wintypes.UINT)]

    class PATH_TARGET_INFO(ctypes.Structure):
        _fields_ = [("adapterId", LUID), ("id", wintypes.UINT),
                    ("modeInfoIdx", wintypes.UINT), ("outputTechnology", wintypes.UINT),
                    ("rotation", wintypes.UINT), ("scaling", wintypes.UINT),
                    ("refreshRate", RATIONAL), ("scanLineOrdering", wintypes.UINT),
                    ("targetAvailable", wintypes.BOOL), ("statusFlags", wintypes.UINT)]

    class PATH_INFO(ctypes.Structure):
        _fields_ = [("sourceInfo", PATH_SOURCE_INFO),
                    ("targetInfo", PATH_TARGET_INFO), ("flags", wintypes.UINT)]

    class MODE_INFO(ctypes.Structure):
        # 中身は使わない（QueryDisplayConfig が要求する受け皿のみ。union 48 バイト）
        _fields_ = [("infoType", wintypes.UINT), ("id", wintypes.UINT),
                    ("adapterId", LUID), ("_data", ctypes.c_ubyte * 48)]

    class DEVINFO_HEADER(ctypes.Structure):
        _fields_ = [("type", wintypes.UINT), ("size", wintypes.UINT),
                    ("adapterId", LUID), ("id", wintypes.UINT)]

    class SOURCE_NAME(ctypes.Structure):
        _fields_ = [("header", DEVINFO_HEADER),
                    ("viewGdiDeviceName", ctypes.c_wchar * 32)]

    class TARGET_NAME(ctypes.Structure):
        _fields_ = [("header", DEVINFO_HEADER), ("flags", wintypes.UINT),
                    ("outputTechnology", wintypes.UINT),
                    ("edidManufactureId", wintypes.USHORT),
                    ("edidProductCodeId", wintypes.USHORT),
                    ("connectorInstance", wintypes.UINT),
                    ("monitorFriendlyDeviceName", ctypes.c_wchar * 64),
                    ("monitorDevicePath", ctypes.c_wchar * 128)]

    class ADVANCED_COLOR_INFO(ctypes.Structure):
        # value ビット: b0=advancedColorSupported, b1=advancedColorEnabled
        _fields_ = [("header", DEVINFO_HEADER), ("value", wintypes.UINT),
                    ("colorEncoding", wintypes.UINT),
                    ("bitsPerColorChannel", wintypes.UINT)]

    class ADVANCED_COLOR_INFO_2(ctypes.Structure):
        # value ビット: b0=advancedColorSupported, b1=advancedColorActive,
        #   b3=advancedColorLimitedByPolicy, b4=highDynamicRangeSupported,
        #   b5=highDynamicRangeUserEnabled, b6=wideColorSupported, b7=wideColorUserEnabled
        _fields_ = [("header", DEVINFO_HEADER), ("value", wintypes.UINT),
                    ("colorEncoding", wintypes.UINT),
                    ("bitsPerColorChannel", wintypes.UINT),
                    ("activeColorMode", wintypes.UINT)]

    class SDR_WHITE_LEVEL(ctypes.Structure):
        _fields_ = [("header", DEVINFO_HEADER), ("SDRWhiteLevel", wintypes.UINT)]

    def dev_info(struct: ctypes.Structure, info_type: int,
                 adapter: LUID, target_id: int) -> bool:
        struct.header.type = info_type
        struct.header.size = ctypes.sizeof(struct)
        struct.header.adapterId = adapter
        struct.header.id = target_id
        return user32.DisplayConfigGetDeviceInfo(ctypes.byref(struct)) == ERROR_SUCCESS

    num_paths = wintypes.UINT(0)
    num_modes = wintypes.UINT(0)
    if user32.GetDisplayConfigBufferSizes(
            QDC_ONLY_ACTIVE_PATHS, ctypes.byref(num_paths),
            ctypes.byref(num_modes)) != ERROR_SUCCESS:
        return []
    paths = (PATH_INFO * num_paths.value)()
    modes = (MODE_INFO * num_modes.value)()
    if user32.QueryDisplayConfig(
            QDC_ONLY_ACTIVE_PATHS, ctypes.byref(num_paths), paths,
            ctypes.byref(num_modes), modes, None) != ERROR_SUCCESS:
        return []

    color_modes = {0: "SDR", 1: "WCG", 2: "HDR"}
    out: list[DisplayHdrInfo] = []
    for i in range(num_paths.value):
        path = paths[i]
        info = DisplayHdrInfo()

        src = SOURCE_NAME()
        if dev_info(src, GET_SOURCE_NAME,
                    path.sourceInfo.adapterId, path.sourceInfo.id):
            info.gdi_name = src.viewGdiDeviceName

        tgt = TARGET_NAME()
        if dev_info(tgt, GET_TARGET_NAME,
                    path.targetInfo.adapterId, path.targetInfo.id):
            info.friendly_name = tgt.monitorFriendlyDeviceName

        aci2 = ADVANCED_COLOR_INFO_2()
        if (dev_info(aci2, GET_ADVANCED_COLOR_INFO_2,
                     path.targetInfo.adapterId, path.targetInfo.id)
                and aci2.activeColorMode in color_modes):
            info.api = "ACI2"
            info.hdr_supported = bool(aci2.value & (1 << 4))
            info.hdr_enabled = aci2.activeColorMode == 2
            info.active_color_mode = color_modes[aci2.activeColorMode]
            info.bits_per_channel = aci2.bitsPerColorChannel
        else:
            aci = ADVANCED_COLOR_INFO()
            if dev_info(aci, GET_ADVANCED_COLOR_INFO,
                        path.targetInfo.adapterId, path.targetInfo.id):
                info.api = "ACI"
                info.hdr_supported = bool(aci.value & (1 << 0))
                info.hdr_enabled = bool(aci.value & (1 << 1))
                info.bits_per_channel = aci.bitsPerColorChannel

        wl = SDR_WHITE_LEVEL()
        if dev_info(wl, GET_SDR_WHITE_LEVEL,
                    path.targetInfo.adapterId, path.targetInfo.id):
            info.sdr_white_nits = wl.SDRWhiteLevel / 1000.0 * 80.0

        out.append(info)
    return out


def _main() -> int:
    infos = probe()
    if not infos:
        print("検出結果なし（Windows 以外、または QueryDisplayConfig 失敗）")
        return 1
    print(f"アクティブディスプレイ: {len(infos)} 面")
    for k, info in enumerate(infos):
        print(f"[{k}] {info.gdi_name}  ({info.friendly_name})")
        print(f"    api={info.api}  mode={info.active_color_mode or '-'}  "
              f"hdr_supported={info.hdr_supported}  hdr_enabled={info.hdr_enabled}")
        print(f"    bits/ch={info.bits_per_channel}  "
              f"SDR白={info.sdr_white_nits:.1f} nits")
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
