"""Reproduce Qt 6.11's HDR-capability probe point for every screen (QTBUG-149927, variant 1).

QDxgiHdrInfo::output6ForWindow (qtbase src/gui/rhi/qdxgihdrinfo.cpp) takes the window's *logical*
geometry, multiplies both the top-left and the size by devicePixelRatio, and looks for the DXGI output
whose DesktopCoordinates (physical pixels) contain the centre of that rect.  On Windows the logical
origin of a screen already is its native origin, so the probe point is shifted by origin * (dpr - 1);
once that exceeds half the screen the check fails and the scRGB/HDR10 swapchain silently becomes SDR.

Usage:  uv run python tools/qt_hdr_probe.py        (PyQt6 required; Windows only)
Prints one line per screen: logical origin, dpr, Qt's probe centre, the real physical rect, and whether
the centre lands inside it ("inside" = HDR swapchain possible, "OUTSIDE" = SDR fallback).
"""
import ctypes
import sys
from ctypes import wintypes

from PyQt6.QtCore import QPoint, QRect
from PyQt6.QtGui import QGuiApplication


class RECT(ctypes.Structure):
    _fields_ = [("l", ctypes.c_long), ("t", ctypes.c_long), ("r", ctypes.c_long), ("b", ctypes.c_long)]


class MONITORINFOEXW(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD), ("rcMonitor", RECT), ("rcWork", RECT),
                ("dwFlags", wintypes.DWORD), ("szDevice", ctypes.c_wchar * 32)]


def physical_rects() -> dict[str, QRect]:
    """GDI name -> physical monitor rect (the process is per-monitor-DPI-aware once Qt is up)."""
    user32 = ctypes.windll.user32
    out: dict[str, QRect] = {}
    proc_t = ctypes.WINFUNCTYPE(wintypes.BOOL, ctypes.c_void_p, ctypes.c_void_p,
                                ctypes.POINTER(RECT), ctypes.c_double)

    def cb(hmon, _hdc, _prc, _lp):
        mi = MONITORINFOEXW()
        mi.cbSize = ctypes.sizeof(mi)
        user32.GetMonitorInfoW(ctypes.c_void_p(hmon), ctypes.byref(mi))
        r = mi.rcMonitor
        out[mi.szDevice] = QRect(QPoint(r.l, r.t), QPoint(r.r - 1, r.b - 1))
        return True

    user32.EnumDisplayMonitors(None, None, proc_t(cb), 0)
    return out


def main() -> int:
    app = QGuiApplication(sys.argv)
    rects = physical_rects()
    print(f"{'screen':16s} {'logical origin':>16s} {'size':>10s} {'dpr':>5s} {'Qt probe centre':>16s}  "
          f"{'physical rect':30s} result")
    for s in app.screens():
        g = s.geometry()
        dpr = s.devicePixelRatio()
        wr = QRect(g.topLeft() * dpr, g.size() * dpr)      # the exact expression Qt uses
        c = wr.center()
        hits = [n for n, r in rects.items() if r.contains(c)]
        # QScreen::name() is the EDID name, not the GDI name; the logical origin equals the native origin
        # on Windows, so the screen's own physical rect is the one whose top-left matches (or contains) it.
        pr = next((r for r in rects.values() if r.topLeft() == g.topLeft()), None) or \
            next((r for r in rects.values() if r.contains(g.topLeft())), None)
        pr_txt = (f"({pr.left()},{pr.top()})-({pr.right()},{pr.bottom()})" if pr else "?")
        res = "inside" if hits else "OUTSIDE -> SDR fallback"
        print(f"{s.name()[:16]:16s} {f'({g.x()},{g.y()})':>16s} {f'{g.width()}x{g.height()}':>10s} {dpr:5.2f} "
              f"{f'({c.x()},{c.y()})':>16s}  {pr_txt:30s} {res}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
