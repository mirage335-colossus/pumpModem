#!/usr/bin/env python3
"""Continuous, fixed-geometry audio DDC for the isolated triad study.

This receiver projection is deliberately separate from the propagation channel.
It never classifies samples by carrier or resets at Helper/DATA/hop boundaries.
The caller must retain timestamps, continue feeding ADC samples through gaps,
and distinguish numeric zero-extension tails from physically observed samples.
"""
from __future__ import annotations

import hashlib
import math

import numpy as np
from scipy import signal


def _integer(value, name, *, positive=False):
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, np.integer)):
        raise ValueError(name+' must be an integer')
    value = int(value)
    if positive and value <= 0:
        raise ValueError(name+' must be positive')
    return value


def _positive(value, name):
    if isinstance(value, (bool, np.bool_)):
        raise ValueError(name+' must be finite and positive')
    value = float(value)
    if not math.isfinite(value) or value <= 0:
        raise ValueError(name+' must be finite and positive')
    return value


class StreamingAudioDDC:
    """Real PCM -> complex baseband with continuous phase/FIR/decimator state.

    ``process(pcm, valid=None)`` returns ``(samples, valid, receipt)``. Output
    samples belong to absolute ADC indices divisible by ``decimation``; their
    *content* time is that index minus ``group_delay_samples``, divided by the
    input rate. Keep the integer index and a separate epoch origin when absolute
    time is large. The start index may be any integer, including one above 2**53.

    Input must be consecutive mono PCM. A missing ADC sample must be supplied
    as invalid (its numeric value is zero-filled), not omitted from the clock.
    Unknown history invalidates the first FIR support; every erasure invalidates
    the complete causal FIR support that depends on it. ``flush`` emits the last
    finite numeric tail, labels it unobserved, and closes the stream. Device
    silence should instead be passed to ``process`` as actual ADC observations.

    Only integer-rate downsampling is implemented. FP32 taps are shared by both
    arithmetic choices so FP64 is a reference for the same physical filter.
    """

    _phase_block = 4096

    def __init__(self, input_sample_rate=48000, output_sample_rate=6000,
                 carrier_hz=1725., *, pass_edge_hz=1450., stop_edge_hz=None,
                 attenuation_db=80., undo_fixed_gain=1., start_sample_index=0,
                 phase_at_zero_radians=0., precision='fp32', maximum_chunk_samples=65536):
        self.input_rate = _integer(input_sample_rate, 'input sample rate', positive=True)
        self.output_rate = _integer(output_sample_rate, 'output sample rate', positive=True)
        if self.input_rate < self.output_rate or self.input_rate % self.output_rate:
            raise ValueError('output rate must be an integer divisor of input rate')
        self.decimation = self.input_rate//self.output_rate
        self.carrier_hz = _positive(carrier_hz, 'carrier')
        if self.carrier_hz >= self.input_rate/2:
            raise ValueError('carrier must be below input Nyquist')
        self.pass_edge_hz = _positive(pass_edge_hz, 'pass edge')
        image_distance = min(2*self.carrier_hz, self.input_rate-2*self.carrier_hz)
        self.image_edge_hz = image_distance-self.pass_edge_hz
        largest_stop = min(self.output_rate/2, self.image_edge_hz)
        self.stop_edge_hz = largest_stop if stop_edge_hz is None else _positive(stop_edge_hz, 'stop edge')
        if not self.pass_edge_hz < self.stop_edge_hz <= largest_stop:
            raise ValueError('pass/stop edges must separate both decimation aliases and the declared real-audio image')
        self.attenuation_db = _positive(attenuation_db, 'attenuation')
        self.gain = _positive(undo_fixed_gain, 'declared transmit gain')
        self.scale = math.sqrt(2.)/self.gain
        self.start_index = _integer(start_sample_index, 'start sample index')
        self.next_input_index = self.start_index
        self.phase_at_zero_radians = float(phase_at_zero_radians)
        if not math.isfinite(self.phase_at_zero_radians):
            raise ValueError('phase at zero must be finite')
        self.maximum_chunk_samples = _integer(maximum_chunk_samples, 'maximum chunk samples', positive=True)
        if self.maximum_chunk_samples > 1_048_576:
            raise ValueError('maximum chunk samples exceeds bounded workspace policy')
        if precision not in ('fp32', 'fp64'):
            raise ValueError('precision must be fp32 or fp64')
        self.precision = precision
        self.dtype = np.dtype(np.complex64 if precision == 'fp32' else np.complex128)
        self.real_dtype = np.dtype(np.float32 if precision == 'fp32' else np.float64)
        count, beta = signal.kaiserord(self.attenuation_db,
            (self.stop_edge_hz-self.pass_edge_hz)/(self.input_rate/2))
        count = max(3, int(count) | 1)
        if count > 65537:
            raise ValueError('frontend requires more than65537 FIR taps')
        self.taps = signal.firwin(count, (self.pass_edge_hz+self.stop_edge_hz)/2,
            fs=self.input_rate, window=('kaiser', beta)).astype(np.float32)
        self.taps.flags.writeable = False
        self._taps = self.taps.astype(self.real_dtype, copy=False)
        self.support_samples = count-1
        self.group_delay_samples = self.support_samples//2
        self._history = np.zeros(self.support_samples, dtype=self.dtype)
        self._history_valid = np.zeros(self.support_samples, dtype=bool)
        # Exact integer origin reduction avoids float(global_sample_index).
        numerator, denominator = self.carrier_hz.as_integer_ratio()
        self._frequency_numerator = numerator
        self._phase_denominator = denominator*self.input_rate
        self._phase_block_index = None
        self._phase_values = None
        self.input_samples = 0
        self.output_samples = 0
        self.closed = False

    def metadata(self):
        energy = float(np.dot(self.taps.astype(float), self.taps.astype(float)))
        return dict(schema='triad-continuous-audio-ddc-v1', input_sample_rate=self.input_rate,
            output_sample_rate=self.output_rate, carrier_hz=self.carrier_hz,
            pass_edge_hz=self.pass_edge_hz, stop_edge_hz=self.stop_edge_hz,
            declared_conjugate_image_edge_hz=self.image_edge_hz,
            attenuation_design_db=self.attenuation_db, fir_taps=len(self.taps),
            taps_sha256=hashlib.sha256(self.taps.astype('<f4').tobytes()).hexdigest(),
            causal_history_samples=self.support_samples, group_delay_input_samples=self.group_delay_samples,
            group_delay_seconds=self.group_delay_samples/self.input_rate,
            start_input_sample=self.start_index, decimation=self.decimation,
            output_lattice='absolute input sample index modulo decimation equals zero',
            content_time='(output_input_sample_index - group_delay_input_samples) / input_sample_rate; retain integer index plus separate epoch origin',
            arithmetic=self.precision, tap_arithmetic='fp32', phase_arithmetic='exact integer global reduction; FP64 bounded local phase',
            continuous_cross_window_filter_state=True, declared_transmit_gain_undone=self.gain,
            mixing_scale=self.scale, fir_energy=energy,
            white_real_pcm_variance_to_complex_output_variance=self.scale**2*energy,
            noise_scope='Variance factor for independent zero-mean real white PCM; output is generally colored/improper and is not an automatically whitened receiver null.',
            memory_scope='FIR/validity history plus bounded internal chunk workspace; returned output storage scales with requested input chunk',
            maximum_internal_chunk_samples=self.maximum_chunk_samples,
            projection_scope='Fixed receiver low-pass. Preserves declared passband, not all noncompact stochastic sidebands. Apply matching operator to model/templates; new admission calibration required.',
            source_effects='No propagation, gain fitting, AGC, channel reset, carrier-label filtering or post-channel hop gate.')

    def _phase(self, first, count):
        result = np.empty(count, dtype=self.dtype)
        offset = 0
        while offset < count:
            index = first+offset
            block, local = divmod(index, self._phase_block)
            if self._phase_block_index != block:
                origin = block*self._phase_block
                reduced = (origin*self._frequency_numerator) % self._phase_denominator
                cycles = (reduced/self._phase_denominator+
                    np.arange(self._phase_block, dtype=np.float64)*(self.carrier_hz/self.input_rate))
                angle = -2*np.pi*np.remainder(cycles, 1.)-self.phase_at_zero_radians
                self._phase_values = np.exp(1j*angle).astype(self.dtype)
                self._phase_block_index = block
            take = min(count-offset, self._phase_block-local)
            result[offset:offset+take] = self._phase_values[local:local+take]
            offset += take
        return result

    def _block(self, pcm, valid):
        first = self.next_input_index
        count = len(pcm)
        mixed = (pcm*self.scale*self._phase(first, count)).astype(self.dtype)
        mixed[~valid] = 0
        combined = np.concatenate((self._history, mixed))
        combined_valid = np.concatenate((self._history_valid, valid))
        # Align polyphase index zero to the global decimation lattice. Padded
        # values cannot affect retained outputs because complete FIR history is
        # present for every current sample.
        combined_first = first-self.support_samples
        prefix = combined_first % self.decimation
        if prefix:
            work = np.concatenate((np.zeros(prefix, dtype=self.dtype), combined))
        else:
            work = combined
        filtered = signal.upfirdn(self._taps, work, down=self.decimation)
        first_output = first+(-first % self.decimation)
        output_count = max(0, (first+count-1-first_output)//self.decimation+1)
        begin = (first_output-(combined_first-prefix))//self.decimation
        output = np.asarray(filtered[begin:begin+output_count], dtype=self.dtype)
        # Exact integer support erosion: any unknown/missing ADC input makes
        # every output depending on it invalid, regardless of its filled value.
        sums = np.r_[0, np.cumsum(combined_valid, dtype=np.int64)]
        complete = sums[len(self.taps):]-sums[:-len(self.taps)] == len(self.taps)
        output_valid = complete[(first_output-first)::self.decimation].copy()
        if len(output_valid) != output_count:
            raise RuntimeError('decimator validity alignment changed')
        self._history = combined[-self.support_samples:].copy()
        self._history_valid = combined_valid[-self.support_samples:].copy()
        self.next_input_index += count
        self.output_samples += output_count
        return output, output_valid, first_output if output_count else None

    def _process(self, pcm, valid, *, tail):
        first_input = self.next_input_index
        parts, masks, first_output = [], [], None
        for first in range(0, len(pcm), self.maximum_chunk_samples):
            last = min(len(pcm), first+self.maximum_chunk_samples)
            samples, mask, index = self._block(pcm[first:last], valid[first:last])
            if first_output is None and index is not None:
                first_output = index
            parts.append(samples); masks.append(mask)
        samples = np.concatenate(parts) if parts else np.empty(0, dtype=self.dtype)
        mask = np.concatenate(masks) if masks else np.empty(0, dtype=bool)
        receipt = dict(schema='triad-continuous-audio-chunk-v1',
            first_input_sample=first_input, input_samples=len(pcm),
            next_input_sample=self.next_input_index, first_output_input_sample=first_output,
            output_input_stride=self.decimation, output_samples=len(samples),
            group_delay_input_samples=self.group_delay_samples,
            valid_output_samples=int(mask.sum()), supplied_invalid_samples=int((~valid).sum()),
            finite_zero_extension_tail=tail, output_is_observed=not tail,
            source_start_is_receiver_clock=True)
        return samples, mask, receipt

    def process(self, pcm, valid=None):
        if self.closed:
            raise RuntimeError('stream is closed; construct a new frontend for a new ADC stream')
        raw = np.asarray(pcm)
        if raw.ndim != 1 or np.iscomplexobj(raw) or not np.issubdtype(raw.dtype, np.number):
            raise ValueError('mono real numeric PCM vector required')
        mask = np.ones(len(raw), dtype=bool) if valid is None else np.asarray(valid)
        if mask.shape != raw.shape or not np.isin(mask, (0, 1)).all():
            raise ValueError('binary validity vector must match PCM')
        mask = mask.astype(bool, copy=False)
        if not np.isfinite(raw[mask]).all():
            raise ValueError('valid PCM samples must be finite')
        # Do not allow nonfinite erased values to poison mixer/filter history.
        safe = np.where(mask, raw, 0).astype(self.real_dtype)
        if not np.isfinite(safe).all() or not np.isfinite(safe*self.scale).all():
            raise ValueError('PCM or declared gain overflows selected arithmetic')
        result = self._process(safe, mask, tail=False)
        self.input_samples += len(raw)
        return result

    def flush(self):
        if self.closed:
            raise RuntimeError('stream is already closed')
        # A numeric finite-support drain is not an observation of physical
        # silence and must not be used for absent-symbol completion evidence.
        count = self.support_samples if self.input_samples else 0
        result = self._process(np.zeros(count, dtype=self.real_dtype),
            np.zeros(count, dtype=bool), tail=True)
        self.closed = True
        return result
