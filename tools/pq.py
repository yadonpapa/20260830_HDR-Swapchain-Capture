"""SMPTE ST 2084 (PQ) and sRGB transfer functions used by the test-pattern generator.

Constants from ITU-R BT.2100-2 Table 4 (PQ) and IEC 61966-2-1 (sRGB).
"""
from __future__ import annotations

import numpy as np

_PQ_M1 = 2610.0 / 16384.0
_PQ_M2 = 2523.0 / 4096.0 * 128.0
_PQ_C1 = 3424.0 / 4096.0
_PQ_C2 = 2413.0 / 4096.0 * 32.0
_PQ_C3 = 2392.0 / 4096.0 * 32.0


def _pq_oetf(y: np.ndarray) -> np.ndarray:
    """Normalised luminance (1.0 = 10000 nit) -> PQ signal 0..1 (inverse EOTF)."""
    y = np.clip(np.asarray(y, dtype=np.float64), 0.0, 1.0)
    p = np.power(y, _PQ_M1)
    return np.power((_PQ_C1 + _PQ_C2 * p) / (1.0 + _PQ_C3 * p), _PQ_M2)


def _pq_eotf(code: np.ndarray) -> np.ndarray:
    """PQ signal 0..1 -> normalised luminance (1.0 = 10000 nit)."""
    code = np.clip(np.asarray(code, dtype=np.float64), 0.0, 1.0)
    p = np.power(code, 1.0 / _PQ_M2)
    num = np.maximum(p - _PQ_C1, 0.0)
    den = _PQ_C2 - _PQ_C3 * p
    return np.power(num / den, 1.0 / _PQ_M1)


def _srgb_oetf(lin: np.ndarray) -> np.ndarray:
    """Linear 0..1 -> sRGB-encoded 0..1."""
    lin = np.clip(np.asarray(lin, dtype=np.float64), 0.0, 1.0)
    return np.where(lin <= 0.0031308, 12.92 * lin, 1.055 * np.power(lin, 1.0 / 2.4) - 0.055)


def pq_nit_for_code(code) -> np.ndarray:
    return _pq_eotf(np.asarray(code, dtype=np.float64)) * 10000.0
