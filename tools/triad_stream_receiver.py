"""Bounded headless Robust reception through scored quiet-slot completion.

Input chunks are already captured complex baseband. Native channel generation
belongs to the caller, outside ``push``. Every recurring public-slot bank build,
input copy, complete signal-receiver call and canonical source decode is inside
the measured receive service. No transmitted bit count or transport endpoint
selects the physical message end.
"""
from __future__ import annotations

from collections import deque
import copy
import json
import math
from pathlib import Path
import subprocess
import time

import numpy as np

from triad_case_replay import build_public_screen
from triad_message_receiver import QuietSlotMessageReceiver
from triad_receiver import CompleteBitReceiver


_MAXIMUM_SLOTS = 418
_MAXIMUM_BITS = 208
_RECEIPT_BYTES = 16 * 1024


def _integer(value, name, minimum=1):
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValueError(name + ' is outside its local integer domain')
    return value


def native_short_decoder(executable):
    """Return the bounded canonical codec adapter, exposing filtered text only."""
    binary = str(Path(executable))
    def decode(bits):
        if (not isinstance(bits, tuple) or len(bits) > _MAXIMUM_BITS or
                any(type(bit) is not int or bit not in (0, 1) for bit in bits)):
            raise ValueError('canonical decoder requires bounded exact bits')
        result = subprocess.run([binary, '--short-decode', ''.join(map(str, bits))],
            capture_output=True, check=True, timeout=10)
        if len(result.stdout) > _RECEIPT_BYTES:
            raise ValueError('canonical decoder response exceeds its local bound')
        receipt = json.loads(result.stdout)
        if (not isinstance(receipt, dict) or receipt.get('schema') != 'triad-short-codec-v1' or
                receipt.get('operation') != 'decode' or receipt.get('codec') != 'short-prefix' or
                type(receipt.get('received_bits')) is not int or receipt['received_bits'] != len(bits)):
            raise ValueError('canonical decoder acknowledgement differs from input')
        return dict(decoded_bytes=receipt.get('decoded_bytes'),
                    filtered_text=receipt.get('text'))
    return decode


class CompleteStreamReceiver:
    """Receive one locally bounded short message without a received length.

    ``public_manifest`` is receiver configuration, not a native emission
    descriptor. A public ``slot_plan`` supplies each observation window even
    beyond an emitter's actual payload. ``basis_source`` has one pipe owner;
    this object closes the current bit receiver before changing its address.
    """
    def __init__(self, public_manifest, basis_source, *, source_identity,
                 memory_limit_bytes, workers=1, native_executable=None,
                 decode_short_message=None, maximum_slots=_MAXIMUM_SLOTS,
                 maximum_data_bits=_MAXIMUM_BITS, quiet_slots=2,
                 maximum_input_chunk_samples=16384, maximum_buffer_samples=None,
                 maximum_gap_samples=None, bank_builder=build_public_screen,
                 receiver_factory=CompleteBitReceiver, receiver_policy=None):
        began = time.perf_counter()
        if not isinstance(public_manifest, dict):
            raise TypeError('a public receiver configuration is required')
        self.manifest = copy.deepcopy(public_manifest)
        self.sample_rate = _integer(self.manifest.get('sample_rate_hz'), 'sample rate')
        self.memory_limit = _integer(memory_limit_bytes, 'receiver memory limit')
        self.workers = _integer(workers, 'receiver workers')
        self.maximum_slots = _integer(maximum_slots, 'maximum slots')
        if self.maximum_slots > _MAXIMUM_SLOTS:
            raise ValueError('maximum slots exceed the local short-message profile')
        self.maximum_input_chunk_samples = _integer(maximum_input_chunk_samples, 'input chunk limit')
        if self.maximum_input_chunk_samples > 16384:
            raise ValueError('input chunk limit exceeds the native bounded interface')
        default_buffer = self.memory_limit // (4 * 17)
        self.maximum_buffer_samples = _integer(
            default_buffer if maximum_buffer_samples is None else maximum_buffer_samples,
            'input buffer limit')
        if 17*self.maximum_buffer_samples > self.memory_limit:
            raise ValueError('input buffer exceeds the local receiver memory budget')
        self.maximum_gap_samples = _integer(
            self.maximum_input_chunk_samples if maximum_gap_samples is None else maximum_gap_samples,
            'input gap limit')
        if self.maximum_gap_samples > self.maximum_buffer_samples:
            raise ValueError('input gap limit exceeds the bounded buffer')
        if decode_short_message is None:
            if native_executable is None:
                raise ValueError('canonical native decoder executable or callback required')
            decode_short_message = native_short_decoder(native_executable)
        if not callable(decode_short_message) or not callable(bank_builder) or not callable(receiver_factory):
            raise TypeError('receiver, bank builder and canonical decoder must be callable')
        self.source = basis_source
        self.source_identity = source_identity
        self.bank_builder, self.receiver_factory = bank_builder, receiver_factory
        self.receiver_policy = copy.deepcopy(receiver_policy)
        self._decoder = decode_short_message
        self._blocks = deque()
        self._buffer_bytes = self._buffer_samples = 0
        self._peak_buffer_bytes = self._peak_combined_bytes = 0
        self._input_cursor = 0
        self._present_samples = self._valid_samples = self._gap_samples = 0
        self._last_present_first = 0
        self._decision_stop = None
        self._all_input_valid = True
        self._service = 0.
        self._slot_service = 0.
        self._inflight_input_bytes = 0
        self._slot_index = 0
        self._phase = 'public_slot_planning'
        self._error_phase = self._error_type = None
        self._slot_receipts = []
        self._receipt_bytes = 0
        self._workspace_receipts_complete = True
        self._receipt_reserve = self.maximum_slots * _RECEIPT_BYTES
        if self._receipt_reserve >= self.memory_limit:
            raise MemoryError('bounded message receipts exhaust the receiver memory budget')
        self._state, self._reason = 'receiving', None
        self._next_plan = self._plan(0)
        self.message = QuietSlotMessageReceiver(self._decode,
            quiet_slots=quiet_slots, maximum_slots=self.maximum_slots,
            maximum_data_bits=maximum_data_bits, sample_rate_hz=self.sample_rate)
        self.receiver_preparation_seconds = time.perf_counter()-began

    def _decode(self, bits):
        # Missing physical input cannot be repaired by a source codec. In
        # particular, never decode a suffix after an unobserved leading gap.
        if self._gap_samples or not self._all_input_valid:
            raise ValueError('physical input was incomplete')
        return self._decoder(bits)

    def _plan(self, index):
        plan = self.source.slot_plan(index)
        if (not isinstance(plan, dict) or plan.get('index') != index or
                plan.get('sample_rate_hz') != self.sample_rate):
            raise ValueError('public slot planner returned a different geometry')
        first = _integer(plan.get('receiver_window_first_sample'), 'window first', 0)
        stop = _integer(plan.get('receiver_window_stop_sample'), 'window stop')
        count = _integer(plan.get('receiver_window_samples'), 'window samples')
        if (stop-first != count or plan.get('receiver_window_physical_origin_sample') != first or
                count > self.maximum_buffer_samples):
            raise MemoryError('public slot exceeds the local input buffer')
        return plan

    def _terminal(self):
        return self._state != 'receiving'

    def _fail(self, state, reason):
        self._state, self._reason = state, reason

    def _prune(self, first):
        while self._blocks and self._blocks[0][0]+len(self._blocks[0][1]) <= first:
            _, values, _, allocated = self._blocks.popleft()
            self._buffer_bytes -= allocated
            self._buffer_samples -= len(values)
        if self._blocks and self._blocks[0][0] < first:
            start, values, valid, allocated = self._blocks[0]
            skip = first-start
            self._blocks[0] = (first, values[skip:], valid[skip:], allocated)
            self._buffer_samples -= skip

    def _append(self, first, values, valid):
        keep = max(first, self._next_plan['receiver_window_first_sample'])
        stop = first+len(values)
        if stop <= keep:
            return
        offset = keep-first
        count = stop-keep
        if self._buffer_samples+count > self.maximum_buffer_samples+self.maximum_input_chunk_samples:
            raise MemoryError('overlap input buffer exceeds its local sample bound')
        allocated = 17*count
        if self._buffer_bytes+allocated+self._receipt_reserve+self._inflight_input_bytes > self.memory_limit:
            raise MemoryError('overlap input buffer exceeds its local memory bound')
        value_copy = np.array(values[offset:], dtype=np.complex128, copy=True)
        mask_copy = np.array(valid[offset:], dtype=np.uint8, copy=True)
        self._blocks.append((keep, value_copy, mask_copy, allocated))
        self._buffer_bytes += allocated
        self._buffer_samples += count
        self._peak_buffer_bytes = max(self._peak_buffer_bytes, self._buffer_bytes)

    def _window(self, plan):
        first, stop = plan['receiver_window_first_sample'], plan['receiver_window_stop_sample']
        count = stop-first
        remaining = (self.memory_limit-self._buffer_bytes-self._receipt_reserve-
                     self._inflight_input_bytes)
        if remaining <= 17*count:
            raise MemoryError('assembled public window exhausts receiver memory')
        values, valid = np.empty(count, np.complex128), np.empty(count, np.uint8)
        cursor = first
        for begin, data, mask, _ in self._blocks:
            end = begin+len(data)
            if end <= cursor:
                continue
            if begin > cursor:
                raise RuntimeError('bounded input buffer has an unrepresented gap')
            end = min(end, stop)
            values[cursor-first:end-first] = data[cursor-begin:end-begin]
            valid[cursor-first:end-first] = mask[cursor-begin:end-begin]
            cursor = end
            if cursor == stop:
                break
        if cursor != stop:
            raise RuntimeError('public observation is not fully buffered')
        return values, valid, remaining

    def _slot_manifest(self, plan):
        manifest = copy.deepcopy(self.manifest)
        manifest['public_epoch_seconds'] = plan['pattern_epoch']
        manifest.setdefault('format', {})['symbol_ordinal'] = plan['symbol_ordinal']
        calendar = manifest.setdefault('calendar', {})
        calendar.update(capture_samples=plan['receiver_window_samples'],
            physical_origin_sample=plan['receiver_window_first_sample'],
            capture_lead_seconds=plan['window_capture_lead_seconds'],
            data_sample_offset=plan['source_begin_samples'],
            helper_start_seconds=plan['helper_begin_samples']/self.sample_rate,
            helper_end_seconds=plan['helper_end_samples']/self.sample_rate)
        for key in ('receiver_data_first_sample', 'receiver_data_stop_sample',
                    'receiver_previous_signal_stop_sample', 'receiver_next_signal_first_sample',
                    'quiet_aperture_first_sample', 'quiet_aperture_stop_sample'):
            if key in plan:
                calendar[key] = plan[key]
        return manifest

    def _process_slot(self, plan):
        started = time.perf_counter()
        self._phase = 'slot_window_assembly'
        values, valid, remaining = self._window(plan)
        manifest = self._slot_manifest(plan)
        address = dict(pattern_epoch=plan['pattern_epoch'], symbol_ordinal=plan['symbol_ordinal'])
        build_started = time.perf_counter()
        bank_limit = remaining-values.nbytes-valid.nbytes
        if bank_limit <= 0:
            raise MemoryError('public bank has no remaining workspace')
        self._phase = 'recurring_public_bank_preparation'
        bank = self.bank_builder(self.source, manifest, public_address=address,
            source_identity=self.source_identity,
            memory_limit_bytes=bank_limit, threads=self.workers)
        preparation_peak = bank.get('work_receipt', {}).get('preparation_peak_estimate_bytes')
        if (isinstance(preparation_peak, bool) or not isinstance(preparation_peak, (int, float)) or
                not math.isfinite(preparation_peak) or preparation_peak < 0):
            self._workspace_receipts_complete = False
        else:
            self._peak_combined_bytes = max(self._peak_combined_bytes,
                self._buffer_bytes+self._receipt_reserve+self._inflight_input_bytes+
                values.nbytes+valid.nbytes+int(preparation_peak))
        receiver = None
        try:
            self._phase = 'complete_bit_receiver_preparation'
            receiver = self.receiver_factory(bank['manifest'], bank['screen'], self.source,
                workers=self.workers, memory_limit_bytes=remaining, policy=self.receiver_policy)
            preparation = time.perf_counter()-build_started
            self._phase = 'complete_bit_receiver_processing'
            row = receiver.receive(values, valid)
        finally:
            if receiver is not None:
                receiver.close()
        exposure = len(values)/self.sample_rate
        coverage = row.get('coverage', {}) if isinstance(row, dict) else {}
        if (not isinstance(row, dict) or row.get('sample_rate_hz') != self.sample_rate or
                coverage.get('physical_first_sample') != plan['receiver_window_first_sample'] or
                coverage.get('physical_stop_sample') != plan['receiver_window_stop_sample'] or
                coverage.get('expected_samples') != len(values)):
            row = dict(status='unresolved', slot_classification='AMBIGUOUS',
                reason='backend_window_geometry_mismatch', sample_rate_hz=self.sample_rate,
                represented_baseband_seconds=exposure,
                coverage=dict(complete_observation=False, all_input_valid=False,
                    consumed_samples=0, expected_samples=len(values),
                    physical_first_sample=plan['receiver_window_first_sample'],
                    physical_stop_sample=plan['receiver_window_stop_sample']))
        row.setdefault('stages', {})['recurring_bank_preparation'] = dict(
            included_in_service=True, represented_baseband_seconds=exposure,
            ratio_processing_seconds_per_baseband_second=preparation/exposure,
            public_address=address, template_identity=bank.get('template_identity'))
        elapsed = time.perf_counter()-started
        self._slot_service += elapsed
        row['ratio_processing_seconds_per_baseband_second'] = elapsed/exposure
        peak = row.get('workspace', {}).get('estimated_peak_bytes')
        if isinstance(peak, (int, float)) and not isinstance(peak, bool) and math.isfinite(peak):
            self._peak_combined_bytes = max(self._peak_combined_bytes,
                self._buffer_bytes+self._receipt_reserve+self._inflight_input_bytes+int(peak))
        self._phase = 'quiet_slot_message_completion'
        self.message.consume_slot(self._slot_index, row)
        compact = {key: copy.deepcopy(row[key]) for key in (
            'schema', 'status', 'reason', 'slot_classification', 'decision_bit',
            'candidate_bit', 'bit_scores', 'operational',
            'sample_rate_hz', 'represented_baseband_seconds',
            'ratio_processing_seconds_per_baseband_second', 'procedure_id',
            'coverage', 'stages', 'workspace', 'quiet') if key in row}
        compact.update(index=self._slot_index, public_address=address,
            template_identity=bank.get('template_identity'))
        encoded_size = len(json.dumps(compact, allow_nan=False).encode())
        if encoded_size > _RECEIPT_BYTES:
            raise MemoryError('slot diagnostic exceeds its bounded receipt size')
        self._slot_receipts.append(compact)
        self._receipt_bytes += encoded_size
        self._slot_index += 1
        message = self.message.result()
        if message['physical_complete']:
            self._decision_stop = plan['receiver_window_stop_sample']
            self._state = 'finished'
            # Release completed input storage inside this measured push.
            self._blocks.clear()
            self._buffer_bytes = self._buffer_samples = 0
            self._phase = 'physical_completion'
            return
        if self._slot_index >= self.maximum_slots:
            self._fail('resource_limit', 'local_slot_limit_before_physical_completion')
            return
        self._phase = 'public_slot_planning'
        self._next_plan = self._plan(self._slot_index)
        if (self._next_plan['receiver_window_first_sample'] <= plan['receiver_window_first_sample'] or
                self._next_plan['receiver_window_stop_sample'] <= plan['receiver_window_stop_sample']):
            raise ValueError('public slot windows do not advance in physical time')
        self._prune(self._next_plan['receiver_window_first_sample'])

    def _ready(self):
        while not self._terminal() and self._input_cursor >= self._next_plan['receiver_window_stop_sample']:
            self._process_slot(self._next_plan)

    def push(self, first_sample, values, valid=None):
        """Consume monotonic captured samples; simulator generation is outside."""
        if self._terminal():
            raise RuntimeError('stream receiver is terminal')
        validation_began = time.perf_counter()
        self._phase = 'input_validation'
        try:
            first = _integer(first_sample, 'input sample origin', 0)
            if not 1 <= len(values) <= self.maximum_input_chunk_samples:
                raise ValueError('input chunk exceeds the local count bound')
            data = np.asarray(values)
            if (data.ndim != 1 or not np.iscomplexobj(data) or not np.isfinite(data).all()):
                raise ValueError('a bounded finite complex input chunk is required')
            if valid is not None and len(valid) != len(data):
                raise ValueError('a matching binary validity mask is required')
            mask = np.ones(len(data), np.uint8) if valid is None else np.asarray(valid)
            if mask.shape != data.shape or not np.isin(mask, (0, 1)).all():
                raise ValueError('a matching binary validity mask is required')
            if first < self._input_cursor:
                raise ValueError('input chunks overlap or move backwards')
            gap = first-self._input_cursor
            if gap > self.maximum_gap_samples:
                raise ValueError('timestamp gap exceeds the local receiver limit')
        finally:
            self._service += max(0., time.perf_counter()-validation_began)
        began = time.perf_counter()
        self._inflight_input_bytes = data.nbytes+mask.nbytes
        try:
            self._phase = 'input_buffering'
            if gap:
                self._gap_samples += gap
                self._all_input_valid = False
                while self._input_cursor < first and not self._terminal():
                    count = min(self.maximum_input_chunk_samples, first-self._input_cursor)
                    self._append(self._input_cursor, np.zeros(count, complex), np.zeros(count, np.uint8))
                    self._input_cursor += count
                    self._ready()
            if not self._terminal():
                self._last_present_first = first
                self._present_samples += len(data)
                self._valid_samples += int(np.count_nonzero(mask))
                self._all_input_valid = self._all_input_valid and bool(mask.all())
                self._append(first, data, mask)
                self._input_cursor = first+len(data)
                self._ready()
        except MemoryError as error:
            self._error_type, self._error_phase = type(error).__name__, self._phase
            self._fail('resource_limit', 'receiver_workspace_limit')
        except Exception as error:
            self._error_type = ''.join(char if char.isascii() and (char.isalnum() or char == '_')
                                      else '_' for char in type(error).__name__[:64])
            self._error_phase = self._phase
            self._fail('failed', 'complete_slot_processing_failed')
        finally:
            self._service += max(0., time.perf_counter()-began)
            self._inflight_input_bytes = 0
        return self.result(include_slots=self._terminal())

    def finish_input(self):
        """Transport exhaustion cannot establish quiet-slot completion."""
        began = time.perf_counter()
        if not self._terminal():
            self.message.finish_input()
            self._fail('input_ended', 'transport_ended_before_physical_completion')
        self._service += max(0., time.perf_counter()-began)
        return self.result()

    def cancel(self):
        began = time.perf_counter()
        if not self._terminal():
            self.message.cancel()
            self._fail('cancelled', 'cancelled_before_physical_completion')
        self._service += max(0., time.perf_counter()-began)
        return self.result()

    def _result(self, *, include_slots):
        message = self.message.result()
        after_decision = (0 if self._decision_stop is None else
            max(0, self._input_cursor-max(self._decision_stop, self._last_present_first)))
        represented_samples = max(0, self._present_samples-after_decision)
        seconds = represented_samples/self.sample_rate
        complete = bool(message['physical_complete'] and message['complete_capacity_receipts'] and
            self._workspace_receipts_complete and self._all_input_valid and
            not self._gap_samples and self._state == 'finished')
        return dict(schema='triad-complete-stream-receiver-v1',
            status=message['status'] if self._state == 'finished' else self._state,
            reason=message.get('reason') if self._state == 'finished' else self._reason,
            diagnostic=dict(phase=self._phase, error_phase=self._error_phase,
                            exception_type=self._error_type),
            message=message, physical_complete=bool(message['physical_complete']),
            complete_receiver_work_covered=complete,
            sample_rate_hz=self.sample_rate,
            ingested_stop_sample=self._input_cursor, ingested_samples=self._present_samples,
            valid_ingested_samples=self._valid_samples, timestamp_gap_samples=self._gap_samples,
            decision_physical_stop_sample=self._decision_stop,
            received_samples_through_decision=represented_samples,
            represented_baseband_seconds=seconds,
            observed_ratio_processing_seconds_per_baseband_second=(self._service/seconds if seconds else None),
            ratio_processing_seconds_per_baseband_second=(self._service/seconds if complete and seconds else None),
            receiver_preparation_seconds=self.receiver_preparation_seconds,
            service_components=dict(
                complete_slot_pipeline_ratio=(self._slot_service/seconds if seconds else None),
                stream_orchestration_ratio=(max(0., self._service-self._slot_service)/seconds
                    if seconds else None)),
            processing_metric_scope='Elapsed receive service including input copies, public planning, recurring banks, full DSP, scheduling, quiet state machine and native source decoding. Simulator generation is outside push. Denominator counts each physically supplied sample once, excludes timestamp gaps and stops at the actual quiet decision; overlapped search windows are never added.',
            workspace=dict(peak_overlap_buffer_bytes=self._peak_buffer_bytes,
                peak_combined_estimate_bytes=self._peak_combined_bytes,
                retained_receipt_bytes=self._receipt_bytes,
                reserved_receipt_bytes=self._receipt_reserve,
                complete_preparation_receipts=self._workspace_receipts_complete,
                local_memory_limit_bytes=self.memory_limit),
            slot_receipt_count=len(self._slot_receipts),
            slots=copy.deepcopy(self._slot_receipts) if include_slots else [])

    def result(self, *, include_slots=True):
        """Materialize a bounded receipt and include that work in service."""
        began = time.perf_counter()
        receipt = self._result(include_slots=include_slots)
        self._service += max(0., time.perf_counter()-began)
        # Refresh arithmetic only: do not make a second deep copy to account
        # for the first. The terminal slot receipts are materialized once.
        seconds = receipt['represented_baseband_seconds']
        ratio = self._service/seconds if seconds else None
        receipt['observed_ratio_processing_seconds_per_baseband_second'] = ratio
        receipt['ratio_processing_seconds_per_baseband_second'] = (
            ratio if receipt['complete_receiver_work_covered'] else None)
        receipt['service_components'] = dict(
            complete_slot_pipeline_ratio=self._slot_service/seconds if seconds else None,
            stream_orchestration_ratio=max(0., self._service-self._slot_service)/seconds
                if seconds else None)
        return receipt

    def close(self):
        began = time.perf_counter()
        if not self._terminal():
            self.message.cancel()
            self._fail('cancelled', 'cancelled_before_physical_completion')
        self._blocks.clear()
        self._buffer_bytes = self._buffer_samples = 0
        self._service += max(0., time.perf_counter()-began)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
