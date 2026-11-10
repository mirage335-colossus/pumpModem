"""Native coarse rank-two arithmetic using the current exporter's sibling library.

The screen's validated Gram inverse and bounded FFT geometry are unchanged.
Tied or unsupported peak frontiers return to the canonical SciPy selection.
"""
from __future__ import annotations

import ctypes
from functools import lru_cache
import math

import numpy as np

from triad_tracking_basis import mapped_kernel_path


@lru_cache(maxsize=4)
def _load(path):
    function = getattr(ctypes.CDLL(str(path)), 'triad_coarse_rank2_peaks_v1')
    function.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t,
        ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_void_p,
        ctypes.c_double, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_void_p,
        ctypes.c_void_p, ctypes.c_void_p]
    function.restype = ctypes.c_int
    return function


class CoarseRank2Kernel:
    def __init__(self, exporter):
        self.path = mapped_kernel_path(exporter)
        self.function = _load(self.path)

    def __call__(self, correlations, inverse, variance, distance, maximum):
        values = np.asarray(correlations)
        gram_inverse = np.asarray(inverse)
        if (values.ndim != 3 or values.shape[1] != 2 or
                values.dtype not in (np.dtype('complex64'), np.dtype('complex128')) or
                not values.flags.aligned or values.strides[-1] != values.dtype.itemsize or
                not values.shape[0] or not values.shape[2] or
                values.strides[1] < values.shape[2]*values.dtype.itemsize or
                values.strides[0] < 2*values.strides[1] or
                gram_inverse.shape != (2, 2) or gram_inverse.dtype != np.complex128 or
                not gram_inverse.flags.c_contiguous or not gram_inverse.flags.aligned or
                not np.isfinite(gram_inverse).all() or
                isinstance(distance, bool) or not isinstance(distance, (int, np.integer)) or
                not 1 <= distance <= np.iinfo(np.uintp).max or
                isinstance(maximum, bool) or not isinstance(maximum, (int, np.integer)) or not 1 <= maximum <= 64 or
                not math.isfinite(variance) or variance <= 0):
            raise ValueError('native coarse kernel requires bounded rank-two correlations and validated inverse')
        scores = np.empty((values.shape[0], values.shape[2]), np.float64)
        indices = np.empty((values.shape[0], 2), np.int64)
        counts = np.empty(values.shape[0], np.int64)
        status = self.function(values.ctypes.data, values.shape[0], values.shape[2],
            values.strides[0], values.strides[1], values.dtype.itemsize,
            gram_inverse.ctypes.data, variance, distance, maximum,
            scores.ctypes.data, indices.ctypes.data, counts.ctypes.data)
        if status:
            raise ValueError('native coarse kernel rejected its bounded descriptors')
        return scores, indices, counts
