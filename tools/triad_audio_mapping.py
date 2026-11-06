"""Bounded mapped-v1 native capture frames; never infer timing from signal data."""
from __future__ import annotations
import math
import struct
import numpy as np

HEADER = struct.Struct('<8sIIQQII')
MAGIC = b'TRIADMAP'


def exact_read(stream, count):
    chunks = bytearray()
    while len(chunks) < count:
        part = stream.read(count-len(chunks))
        if not part:
            raise EOFError('mapped capture ended inside a declared frame')
        chunks.extend(part)
    return bytes(chunks)


def rescale_coordinates(origin, offsets, from_rate, to_rate):
    """Convert sample units without rounding an absolute large integer to FP64."""
    if any(isinstance(x, bool) or not isinstance(x, int) or x <= 0 for x in (from_rate, to_rate)):
        raise ValueError('positive integer sample rates required')
    if not isinstance(origin, int) or not 0 <= origin < 1 << 64:
        raise ValueError('invalid physical sample origin')
    whole, remainder = divmod(origin*to_rate, from_rate)
    return whole, np.asarray(offsets, dtype=np.float64)*(to_rate/from_rate) + remainder/from_rate


def mapped_phase(origin, offsets, frequency, sample_rate):
    """Audio NCO at original ADC coordinates, with exact integer-origin modulo."""
    if not math.isfinite(frequency):
        raise ValueError('finite carrier required')
    numerator, denominator = float(frequency).as_integer_ratio()
    period = denominator*int(sample_rate)
    phase0 = ((int(origin)*numerator) % period)/period
    return 2*np.pi*np.remainder(phase0 + np.asarray(offsets)*(frequency/sample_rate), 1.)


class MappedCaptureReader:
    def __init__(self, stream):
        self.stream = stream
        self.next_packet = 0
        self.next_output = 0
        self.native_rate = None
        self.last_origin = None
        self.last_offset = None
        self.pending = None
        self.cursor = 0
        self.packets = 0

    def _packet(self):
        magic, version, count, first, origin, rate, reserved = HEADER.unpack(exact_read(self.stream, HEADER.size))
        if (magic != MAGIC or version != 1 or reserved or not 1 <= count <= 4096 or
            first != self.next_packet or first+count >= 1 << 64 or not 1 <= rate <= 120_000_000):
            raise ValueError('invalid mapped capture header or discontinuous output calendar')
        if self.native_rate is not None and rate != self.native_rate:
            raise ValueError('mapped capture hardware rate changed')
        offsets = np.frombuffer(exact_read(self.stream, count*8), dtype='<f8')
        pcm = np.frombuffer(exact_read(self.stream, count*4), dtype='<f4')
        if (not np.isfinite(offsets).all() or not np.isfinite(pcm).all() or
            not 0 <= offsets[0] < 1 or np.any(np.diff(offsets) <= 0) or
            offsets[-1] >= (1 << 64)-origin):
            raise ValueError('invalid mapped capture samples or source coordinates')
        if self.last_origin is not None and (origin-self.last_origin)+float(offsets[0]) <= self.last_offset:
            raise ValueError('mapped capture source coordinate moved backward')
        self.native_rate = rate
        self.last_origin, self.last_offset = origin, float(offsets[-1])
        self.next_packet += count
        self.packets += 1
        self.pending = (origin, offsets, pcm)
        self.cursor = 0

    def read(self, count):
        if not isinstance(count, int) or not 1 <= count <= 8192:
            raise ValueError('mapped capture read must be 1..8192 frames')
        remaining, base = count, None
        positions, samples = [], []
        while remaining:
            if self.pending is None or self.cursor == len(self.pending[2]):
                self._packet()
            origin, offsets, pcm = self.pending
            n = min(remaining, len(pcm)-self.cursor)
            if base is None:
                base = origin + math.floor(float(offsets[self.cursor]))
            positions.append((origin-base) + offsets[self.cursor:self.cursor+n])
            samples.append(pcm[self.cursor:self.cursor+n])
            remaining -= n
            self.cursor += n
        self.next_output += count
        return np.concatenate(samples), base, np.concatenate(positions), self.native_rate
