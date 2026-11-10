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
import os
from pathlib import Path
import subprocess
import sys
import time

import numpy as np

from triad_case_replay import build_public_screen
from triad_cpu import resolve_cpu_budget
from triad_message_receiver import QuietSlotMessageReceiver
from triad_receiver import CompleteBitReceiver
from triad_slot_executor import (SlotProcessBackend, score_public_slot,
                                 spawn_auxiliary_reservation_bytes)


_MAXIMUM_SLOTS = 418
_MAXIMUM_BITS = 208
_RECEIPT_BYTES = 16 * 1024
_SERVICE_SPAN_LIMIT = 4096
_SERVICE_SPAN_BYTES = 512 * 1024


class _ServiceUnion:
    """Bounded union of controller and child monotonic-clock service spans."""
    def __init__(self):
        self.settled = 0.
        self.spans = []

    def add(self, start, stop):
        if not (math.isfinite(start) and math.isfinite(stop) and stop >= start):
            raise ValueError('invalid receiver service interval')
        merged = []
        for left, right in sorted([*self.spans, (start, stop)]):
            if merged and left <= merged[-1][1]:
                merged[-1] = (merged[-1][0], max(right, merged[-1][1]))
            else:
                merged.append((left, right))
        self.spans = merged
        if len(merged) > _SERVICE_SPAN_LIMIT:
            raise MemoryError('receiver service interval ledger exhausted')

    def settle(self, before):
        pending = []
        for start, stop in self.spans:
            end = min(stop, before)
            if end > start:
                self.settled += end-start
            if stop > before:
                pending.append((max(start, before), stop))
        self.spans = pending

    @property
    def seconds(self):
        return self.settled+sum(stop-start for start, stop in self.spans)


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
    """Listen continuously on a configured public calendar, with bounded state.

    ``public_manifest`` is receiver configuration, not a native emission
    descriptor. A public ``slot_plan`` supplies each observation window even
    beyond an emitter's actual payload. ``basis_source`` has one pipe owner;
    this object closes the current bit receiver before changing its address.
    Completed messages are drained explicitly; a full output queue applies
    backpressure before accepting more input. ``continuous=False`` retains the
    bounded one-message interface used by functional/capacity gates. Neither
    mode discovers an arbitrary new transmitter's calendar or keyed ordinal.
    """
    def __init__(self, public_manifest, basis_source, *, source_identity,
                 memory_limit_bytes, workers=None, native_executable=None,
                 decode_short_message=None, maximum_slots=_MAXIMUM_SLOTS,
                 maximum_data_bits=_MAXIMUM_BITS, quiet_slots=2,
                 maximum_input_chunk_samples=16384, maximum_buffer_samples=None,
                 maximum_gap_samples=None, bank_builder=build_public_screen,
                 receiver_factory=CompleteBitReceiver, receiver_policy=None,
                 continuous=True, maximum_pending_messages=16,
                 recent_slot_receipts=32, slot_processes=None,
                 process_backend_factory=None):
        began = time.perf_counter()
        if not isinstance(public_manifest, dict):
            raise TypeError('a public receiver configuration is required')
        self.manifest = copy.deepcopy(public_manifest)
        self.sample_rate = _integer(self.manifest.get('sample_rate_hz'), 'sample rate')
        self.memory_limit = _integer(memory_limit_bytes, 'receiver memory limit')
        self.workers = _integer(resolve_cpu_budget()['threads'] if workers is None else workers,
                                'receiver workers')
        if type(continuous) is not bool:
            raise ValueError('continuous listening must be a boolean')
        self.continuous = continuous
        self.maximum_pending_messages = _integer(maximum_pending_messages, 'pending message limit')
        self.recent_slot_receipts = _integer(recent_slot_receipts, 'recent slot receipt limit')
        if self.maximum_pending_messages > 64 or self.recent_slot_receipts > _MAXIMUM_SLOTS:
            raise ValueError('continuous receipt limits exceed local bounds')
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
        if slot_processes is not None and (type(slot_processes) is not int or slot_processes not in (1, 2)):
            raise ValueError('slot processes must be1,2 or automatic')
        from triad_continuous import NativeStream
        canonical = (isinstance(basis_source, NativeStream) and
                     bank_builder is build_public_screen and receiver_factory is CompleteBitReceiver)
        self._pipeline = bool(continuous and self.workers >= 2 and slot_processes != 1 and
                              (canonical or process_backend_factory is not None))
        self._backend_factory = process_backend_factory or SlotProcessBackend
        self._custom_backend = process_backend_factory is not None
        if self._pipeline and not self._custom_backend and not sys.platform.startswith('linux'):
            self._pipeline = False
        self._backend = None
        self._jobs = {}
        self._plan_cache = {}
        self._dispatch_index = 0
        self._dispatched_stop = 0
        self._pending_interval_valid = True
        self._input_closed = False
        self._service_clock = _ServiceUnion() if self._pipeline else None
        self._slot_clock = _ServiceUnion() if self._pipeline else None
        self._open_service = []
        self._controller_resident_bytes = 0
        self._spawn_auxiliary_reserve = 0
        if self._pipeline and not self._custom_backend:
            try:
                resident = [int(Path('/proc', str(pid), 'statm').read_text().split()[1])*
                    os.sysconf('SC_PAGE_SIZE') for pid in (os.getpid(), self.source.process.pid)]
                native_bound = max(int(self.source.descriptor.get('fixed_workspace_bytes', 0)),
                                   int(self.source.descriptor.get('workspace_budget_bytes', 0)))
                self._controller_resident_bytes = resident[0]+max(resident[1], native_bound)
            except (OSError, ValueError, IndexError):
                self._pipeline = False
        self._pipeline_fallback_reason = None
        self._workspace_retry_count = 0
        self._pipeline_jobs_submitted = self._pipeline_parallel_jobs = 0
        self._owner_startup_seconds = 0.
        self._peak_owned_process_bytes = 0
        self._decoder = decode_short_message
        self._blocks = deque()
        self._buffer_bytes = self._buffer_samples = 0
        self._peak_buffer_bytes = self._peak_combined_bytes = 0
        self._input_cursor = 0
        self._present_samples = self._valid_samples = self._gap_samples = 0
        self._last_present_first = 0
        self._decision_stop = None
        self._processed_stop = self._processed_present_samples = 0
        self._all_input_valid = True
        self._episode_input_valid = True
        self._service = 0.
        self._slot_service = 0.
        self._inflight_input_bytes = 0
        self._slot_index = 0
        self._phase = 'public_slot_planning'
        self._error_phase = self._error_type = None
        self._slot_receipts = []
        self._slot_receipt_sizes = []
        self._receipt_bytes = 0
        self._messages = deque()
        self._message_bytes = self._completed_messages = 0
        self._recovery = self._last_rejected_episode = None
        self._recovery_quiet_run = self._rejected_episodes = 0
        self._capacity_receipts_complete = True
        self._classification_counts = dict(DATA=0, QUIET=0, AMBIGUOUS=0)
        self._workspace_receipts_complete = True
        # Reserve stored diagnostics plus their bounded result copy. Current
        # message state and one newly constructed event have separate headroom.
        self._receipt_reserve = ((2*(self.recent_slot_receipts+self.maximum_pending_messages)+2)
            if self.continuous else self.maximum_slots) * _RECEIPT_BYTES
        if self._pipeline:
            self._receipt_reserve += _SERVICE_SPAN_BYTES
        if self._receipt_reserve >= self.memory_limit:
            raise MemoryError('bounded message receipts exhaust the receiver memory budget')
        self._state, self._reason = 'receiving', None
        self._next_plan = self._plan(0)
        self._plan_cache[0] = self._next_plan
        self._message_options = dict(quiet_slots=quiet_slots, maximum_slots=self.maximum_slots,
            maximum_data_bits=maximum_data_bits, sample_rate_hz=self.sample_rate)
        self._new_message()
        self.receiver_preparation_seconds = time.perf_counter()-began

    def _new_message(self):
        self.message = QuietSlotMessageReceiver(self._decode, **self._message_options)
        self._message_slot_index = self._leading_quiet_run = 0
        self._episode_first_slot = self._slot_index
        self._episode_input_valid = True

    def _decode(self, bits):
        # Missing physical input cannot be repaired by a source codec. In
        # particular, never decode a suffix after an unobserved leading gap.
        if (not self._episode_input_valid if self.continuous else
                self._gap_samples or not self._all_input_valid):
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

    def _queue_message(self, message, plan):
        event = dict(schema='triad-stream-message-event-v1', index=self._completed_messages,
            kind='physical_completion', physical_complete=True,
            first_public_slot=self._episode_first_slot, stop_public_slot=self._slot_index,
            decision_physical_stop_sample=plan['receiver_window_stop_sample'], message=message)
        event_size = len(json.dumps(event, allow_nan=False).encode())
        if event_size > _RECEIPT_BYTES:
            raise MemoryError('completed message event exceeds its bounded size')
        if len(self._messages) >= self.maximum_pending_messages:
            raise RuntimeError('message event queue would overwrite unread output')
        self._messages.append((event, event_size))
        self._message_bytes += event_size
        self._completed_messages += 1

    def _terminal(self):
        return self._state != 'receiving'

    def _backpressured(self):
        return self.continuous and len(self._messages) >= self.maximum_pending_messages

    def _fail(self, state, reason):
        self._state, self._reason = state, reason

    def _reserved_bytes(self):
        if self._spawn_auxiliary_reserve:
            self._spawn_auxiliary_reserve = max(self._spawn_auxiliary_reserve,
                                                spawn_auxiliary_reservation_bytes())
        resident = self._backend.resident_bytes if self._backend is not None else 0
        self._peak_owned_process_bytes = max(self._peak_owned_process_bytes, resident)
        if self._backend is not None:
            resident = max(resident, self._backend.native_workspace_bytes)
        return (self._controller_resident_bytes+self._spawn_auxiliary_reserve+
                self._buffer_bytes+self._receipt_reserve+self._inflight_input_bytes+resident+
                sum(job['reservation'] for job in self._jobs.values()))

    def _settle_service(self, *, finalize=False):
        if self._service_clock is None:
            return
        if finalize:
            pending = [job['submitted'] for job in self._jobs.values() if not job.get('finished')]
            pending.extend(self._open_service)
            before = min(pending) if pending else time.perf_counter()
            self._service_clock.settle(before)
            self._slot_clock.settle(before)
        self._service = self._service_clock.seconds
        self._slot_service = self._slot_clock.seconds

    def _begin_service(self):
        began = time.perf_counter()
        self._open_service.append(began)
        return began

    def _account_parent(self, began):
        stopped = time.perf_counter()
        if self._service_clock is None:
            self._service += max(0., stopped-began)
        else:
            self._service_clock.add(began, stopped)
        self._open_service.remove(began)
        self._settle_service(finalize=True)

    def _record_worker(self, record):
        start, stop = record['service_start_monotonic'], record['service_stop_monotonic']
        if self._service_clock is None:
            self._slot_service += max(0., stop-start)
        else:
            self._service_clock.add(start, stop)
            self._slot_clock.add(start, stop)
            self._settle_service()

    def _get_plan(self, index):
        if index not in self._plan_cache:
            self._plan_cache[index] = self._plan(index)
        return self._plan_cache[index]

    def _advance_dispatch(self, plan):
        next_plan = self._get_plan(plan['index']+1)
        if (next_plan['receiver_window_first_sample'] <= plan['receiver_window_first_sample'] or
                next_plan['receiver_window_stop_sample'] <= plan['receiver_window_stop_sample']):
            raise ValueError('public slot windows do not advance in physical time')
        self._dispatch_index = plan['index']+1
        self._next_plan = next_plan
        self._plan_cache.pop(plan['index'], None)
        self._prune(next_plan['receiver_window_first_sample'])

    def _disable_pipeline(self, reason):
        if self._backend is not None:
            self._backend.close()
            self._backend = None
        self._pipeline = False
        self._pipeline_fallback_reason = reason
        # The legacy serial workspace already reserves its native owner.
        self._controller_resident_bytes = 0

    def _ensure_backend(self):
        if self._backend is not None or not self._pipeline:
            return
        prospective_auxiliary = max(self._spawn_auxiliary_reserve,
                                    spawn_auxiliary_reservation_bytes())
        if not self._custom_backend:
            try:
                resident = int(Path('/proc/self/statm').read_text().split()[1])*os.sysconf('SC_PAGE_SIZE')
            except (OSError, ValueError, IndexError):
                self._disable_pipeline('runtime_resident_memory_unavailable')
                return
            native = max(int(self.source.descriptor.get('fixed_workspace_bytes', 0)),
                         int(self.source.descriptor.get('workspace_budget_bytes', 0)))
            if (self._reserved_bytes()+prospective_auxiliary-self._spawn_auxiliary_reserve+
                    2*(resident+native) >= self.memory_limit):
                self._disable_pipeline('two_owner_startup_memory_admission')
                return
        # multiprocessing's process-scoped tracker can outlive both owners.
        # Count it once before launch, and retain the allowance after any
        # attempted launch, including a failed launch or serial fallback.
        self._spawn_auxiliary_reserve = prospective_auxiliary
        startup_began = time.perf_counter()
        try:
            self._backend = self._backend_factory(self.source.command, max_processes=2,
                                                   total_workers=self.workers)
        except MemoryError:
            self._disable_pipeline('two_owner_startup_workspace_rejection')
            return
        finally:
            self._owner_startup_seconds += time.perf_counter()-startup_began
        self._peak_owned_process_bytes = max(self._peak_owned_process_bytes, self._backend.resident_bytes)
        if self._reserved_bytes() >= self.memory_limit:
            self._disable_pipeline('two_owner_resident_memory_admission')

    def _collect(self, *, wait=False):
        if self._backend is None:
            return
        for record in self._backend.poll(wait=wait):
            job = self._jobs.get(record.get('job_id'))
            if job is None or job.get('finished'):
                raise RuntimeError('slot owner returned an unknown or duplicate result')
            self._record_worker(record)
            job['finished'] = True
            if 'result' in record:
                job['result'] = record['result']
                job['values'] = job['valid'] = None
                job['reservation'] = 4*1024**2
            elif record.get('error_type') == 'MemoryError':
                job['workspace_rejected'] = True
                job['reservation'] = 17*job['samples']
            else:
                self._error_phase = str(record.get('error_phase', 'slot_owner'))[:64]
                raise RuntimeError('slot owner failed without a workspace retry condition')
        self._peak_owned_process_bytes = max(self._peak_owned_process_bytes, self._backend.resident_bytes)
        self._settle_service()

    def _commit_ready(self):
        while not self._backpressured() and self._slot_index in self._jobs:
            job = self._jobs[self._slot_index]
            if job.get('result') is None:
                break
            self._commit_slot(job, job['result'])
            del self._jobs[job['index']]
        self._settle_service()

    def _retry_workspace_rejections(self):
        if not any(job.get('workspace_rejected') for job in self._jobs.values()):
            return False
        if any(not job.get('finished') for job in self._jobs.values()):
            return False
        self._disable_pipeline('bounded_child_workspace_rejection')
        for job in sorted(self._jobs.values(), key=lambda item: item['index']):
            if job.get('workspace_rejected'):
                self._workspace_retry_count += 1
                job['result'] = self._score_serial(job)
                job['workspace_rejected'] = False
                job['values'] = job['valid'] = None
                job['reservation'] = 4*1024**2
        self._commit_ready()
        return True

    def _dispatch_ready(self):
        if self._terminal() or self._backpressured():
            return
        if not self._pipeline:
            while not self._terminal() and not self._backpressured() and not self._jobs and (
                    self._input_cursor >= self._next_plan['receiver_window_stop_sample']):
                self._process_slot(self._next_plan)
            return
        if self._input_cursor < self._next_plan['receiver_window_stop_sample']:
            return
        self._ensure_backend()
        if not self._pipeline:
            self._dispatch_ready()
            return
        if any(job.get('workspace_rejected') for job in self._jobs.values()):
            return
        running = [job for job in self._jobs.values() if not job.get('finished')]
        if len(self._jobs) >= 2 or any(job['workers'] == self.workers for job in running):
            return
        ready = [self._next_plan]
        if not self._jobs:
            following = self._get_plan(self._dispatch_index+1)
            if self._input_cursor >= following['receiver_window_stop_sample']:
                ready.append(following)
        allocations = ([self.workers//2, self.workers-self.workers//2] if len(ready) == 2 else
                       [self.workers-running[0]['workers']] if running else [self.workers])
        reserve_inputs = sum(self._backend.input_reservation_bytes(plan['receiver_window_samples']) for plan in ready)
        ingress_headroom = max(0, 17*self._backlog_limit_samples()-self._buffer_bytes)
        ingress_headroom += max(0, 64*self.maximum_input_chunk_samples-self._inflight_input_bytes)
        available = self.memory_limit-self._reserved_bytes()-reserve_inputs-ingress_headroom
        budget = available//len(ready)
        if budget <= max(17*plan['receiver_window_samples'] for plan in ready):
            if not self._jobs:
                self._disable_pipeline('parallel_slot_workspace_admission')
                self._dispatch_ready()
            return
        for plan, workers in zip(ready, allocations):
            job = self._assemble_job(plan)
            job.update(workers=workers, memory_limit_bytes=budget,
                parent_reservation=self.memory_limit-budget,
                reservation=budget+self._backend.input_reservation_bytes(job['samples']))
            self._backend.submit(job['index'], manifest=job['manifest'], public_address=job['address'],
                source_identity=self.source_identity, values=job['values'], valid=job['valid'],
                workers=workers, memory_limit_bytes=budget, receiver_policy=self.receiver_policy)
            self._jobs[job['index']] = job
            self._pipeline_jobs_submitted += 1
            self._pipeline_parallel_jobs += int(workers < self.workers)
            self._advance_dispatch(plan)
        self._peak_combined_bytes = max(self._peak_combined_bytes, self._reserved_bytes())
        if self._reserved_bytes() > self.memory_limit:
            raise MemoryError('owned slot processes exceed the aggregate receiver memory estimate')

    def _backlog_limit_samples(self):
        if not self._pipeline:
            return self.maximum_buffer_samples+self.maximum_input_chunk_samples
        last = self._get_plan(self._dispatch_index+1)
        span = last['receiver_window_stop_sample']-self._next_plan['receiver_window_first_sample']
        return min(self.maximum_buffer_samples+self.maximum_input_chunk_samples,
                   span+2*self.maximum_input_chunk_samples)

    def _can_accept(self, first, count):
        extra = max(0, first+count-max(self._input_cursor,
                    self._next_plan['receiver_window_first_sample']))
        return (self._buffer_samples+extra <= self._backlog_limit_samples() and
                self._reserved_bytes()+17*extra <= self.memory_limit)

    def _pump(self, *, wait=False):
        while not self._terminal():
            self._collect(wait=False)
            self._commit_ready()
            self._retry_workspace_rejections()
            if self._backpressured():
                self._phase = 'message_output_backpressure'
                return
            self._dispatch_ready()
            if not wait:
                return
            if any(not job.get('finished') for job in self._jobs.values()):
                self._collect(wait=True)
                continue
            self._commit_ready()
            if not self._jobs and self._input_cursor < self._next_plan['receiver_window_stop_sample']:
                return
            if not self._pipeline and not self._jobs:
                return

    def _stop_children(self):
        if self._backend is not None:
            self._backend.close()
            self._backend = None
        self._jobs.clear()
        self._settle_service()

    def _processing_error(self, error):
        self._error_type = ''.join(char if char.isascii() and (char.isalnum() or char == '_')
                                  else '_' for char in type(error).__name__[:64])
        self._error_phase = self._error_phase or self._phase
        self._fail('resource_limit' if isinstance(error, MemoryError) else 'failed',
            'receiver_workspace_limit' if isinstance(error, MemoryError)
            else 'complete_slot_processing_failed')
        try:
            self._stop_children()
        except Exception:
            # Retain the owner reservation and forbid further input even when
            # the bounded shutdown cannot prove that an owner exited.
            self._fail('failed', 'slot_owner_shutdown_failed')
            self._error_phase = 'slot_owner_shutdown'

    def _interrupt(self):
        self._fail('cancelled', 'receiver_service_interrupted')
        try:
            self._stop_children()
        except Exception:
            self._fail('failed', 'slot_owner_shutdown_failed')
            self._error_phase = 'slot_owner_shutdown'

    def _drain_accepted(self, *, wait):
        try:
            self._pump(wait=wait)
            if (self._input_closed and not self._terminal() and not self._backpressured()
                    and not self._jobs and
                    self._input_cursor < self._next_plan['receiver_window_stop_sample']):
                self.message.finish_input()
                self._fail('input_ended', 'transport_ended_before_physical_completion')
                self._stop_children()
        except Exception as error:
            self._processing_error(error)

    def _make_ingress_progress(self):
        """Release one completed owner, then refill work without draining it.

        Ingress needs room for its next bounded chunk. Waiting for every ready
        window here would prevent feeding the following pair while it runs.
        """
        try:
            if any(not job.get('finished') for job in self._jobs.values()):
                self._collect(wait=True)
            self._pump(wait=False)
        except Exception as error:
            self._processing_error(error)

    def _prune(self, first):
        if self.continuous:
            # Preserve integrity of unobserved inter-window samples. Already
            # processed samples belong to the preceding message/quiet boundary;
            # they must not poison a fresh episode after physical resynchronization.
            for begin, _, valid, _, _, _ in self._blocks:
                start, stop = max(begin, self._dispatched_stop), min(begin+len(valid), first)
                if stop > start and not valid[start-begin:stop-begin].all():
                    self._pending_interval_valid = False
        while self._blocks and self._blocks[0][0]+len(self._blocks[0][1]) <= first:
            _, values, _, allocated, _, _ = self._blocks.popleft()
            self._buffer_bytes -= allocated
            self._buffer_samples -= len(values)
        if self._blocks and self._blocks[0][0] < first:
            start, values, valid, allocated, prefix, supplied = self._blocks[0]
            skip = first-start
            self._blocks[0] = (first, values[skip:], valid[skip:], allocated,
                               prefix+skip if supplied else prefix, supplied)
            self._buffer_samples -= skip

    def _append(self, first, values, valid, *, supplied=True):
        keep = max(first, self._next_plan['receiver_window_first_sample'])
        stop = first+len(values)
        offset = keep-first
        if self.continuous:
            start, end = max(first, self._dispatched_stop), min(stop, keep)
            if end > start and not valid[start-first:end-first].all():
                self._pending_interval_valid = False
        if stop <= keep:
            return
        count = stop-keep
        if self._buffer_samples+count > self.maximum_buffer_samples+self.maximum_input_chunk_samples:
            raise MemoryError('overlap input buffer exceeds its local sample bound')
        allocated = 17*count
        if self._reserved_bytes()+allocated > self.memory_limit:
            raise MemoryError('overlap input buffer exceeds its local memory bound')
        value_copy = np.array(values[offset:], dtype=np.complex128, copy=True)
        mask_copy = np.array(valid[offset:], dtype=np.uint8, copy=True)
        prefix = self._present_samples-len(values)+offset if supplied else self._present_samples
        self._blocks.append((keep, value_copy, mask_copy, allocated, prefix, supplied))
        self._buffer_bytes += allocated
        self._buffer_samples += count
        self._peak_buffer_bytes = max(self._peak_buffer_bytes, self._buffer_bytes)

    def _window(self, plan):
        first, stop = plan['receiver_window_first_sample'], plan['receiver_window_stop_sample']
        count = stop-first
        remaining = self.memory_limit-self._reserved_bytes()
        if remaining <= 17*count:
            raise MemoryError('assembled public window exhausts receiver memory')
        values, valid = np.empty(count, np.complex128), np.empty(count, np.uint8)
        cursor = first
        present_prefix = None
        for begin, data, mask, _, prefix, supplied in self._blocks:
            end = begin+len(data)
            if end <= cursor:
                continue
            if begin > cursor:
                raise RuntimeError('bounded input buffer has an unrepresented gap')
            end = min(end, stop)
            values[cursor-first:end-first] = data[cursor-begin:end-begin]
            valid[cursor-first:end-first] = mask[cursor-begin:end-begin]
            cursor = end
            present_prefix = prefix+(end-begin if supplied else 0)
            if cursor == stop:
                break
        if cursor != stop:
            raise RuntimeError('public observation is not fully buffered')
        return values, valid, remaining, present_prefix

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

    def _assemble_job(self, plan):
        self._phase = 'slot_window_assembly'
        values, valid, remaining, present_prefix = self._window(plan)
        job = dict(index=plan['index'], plan=plan, manifest=self._slot_manifest(plan),
            address=dict(pattern_epoch=plan['pattern_epoch'], symbol_ordinal=plan['symbol_ordinal']),
            values=values, valid=valid, samples=len(values), present_prefix=present_prefix,
            integrity_valid=self._pending_interval_valid and bool(valid.all()),
            memory_limit_bytes=remaining, reservation=0, submitted=time.perf_counter(), result=None)
        self._pending_interval_valid = True
        self._dispatched_stop = plan['receiver_window_stop_sample']
        return job

    def _score_serial(self, job):
        self._phase = 'recurring_public_bank_preparation'
        remaining = self.memory_limit-self._reserved_bytes()+job.get('reservation', 0)
        job['memory_limit_bytes'] = remaining
        job['parent_reservation'] = self.memory_limit-remaining
        score = score_public_slot(self.source, job['manifest'], public_address=job['address'],
            source_identity=self.source_identity, values=job['values'], valid=job['valid'],
            workers=self.workers, memory_limit_bytes=remaining,
            bank_builder=self.bank_builder, receiver_factory=self.receiver_factory,
            receiver_policy=self.receiver_policy)
        self._record_worker(score)
        job['workers'] = self.workers
        return score

    def _process_slot(self, plan):
        job = self._assemble_job(plan)
        score = self._score_serial(job)
        self._commit_slot(job, score)
        if not self._terminal():
            self._advance_dispatch(plan)

    def _commit_slot(self, job, score):
        plan, row, address = job['plan'], score['row'], job['address']
        remaining, count = job['memory_limit_bytes'], job['samples']
        self._workspace_receipts_complete &= bool(score.get('workspace_receipts_complete', True))
        preparation_peak = score.get('bank_work_receipt', {}).get('preparation_peak_estimate_bytes')
        if (isinstance(preparation_peak, bool) or not isinstance(preparation_peak, (int, float)) or
                not math.isfinite(preparation_peak) or preparation_peak < 0):
            self._workspace_receipts_complete = False
        else:
            self._peak_combined_bytes = max(self._peak_combined_bytes,
                job['parent_reservation']+17*count+int(preparation_peak))
        exposure = count/self.sample_rate
        coverage = row.get('coverage', {}) if isinstance(row, dict) else {}
        if (not isinstance(row, dict) or not isinstance(coverage, dict) or
                type(row.get('sample_rate_hz')) is not int or
                row.get('sample_rate_hz') != self.sample_rate or
                any(type(coverage.get(key)) is not int for key in (
                    'physical_first_sample', 'physical_stop_sample', 'expected_samples')) or
                coverage.get('physical_first_sample') != plan['receiver_window_first_sample'] or
                coverage.get('physical_stop_sample') != plan['receiver_window_stop_sample'] or
                coverage.get('expected_samples') != count):
            row = dict(status='unresolved', slot_classification='AMBIGUOUS',
                reason='backend_window_geometry_mismatch', sample_rate_hz=self.sample_rate,
                represented_baseband_seconds=exposure,
                coverage=dict(complete_observation=False, all_input_valid=False,
                    consumed_samples=0, expected_samples=count,
                    physical_first_sample=plan['receiver_window_first_sample'],
                    physical_stop_sample=plan['receiver_window_stop_sample']))
        row['ratio_processing_seconds_per_baseband_second'] = score['service_seconds']/exposure
        workspace = row.get('workspace')
        peak = workspace.get('estimated_peak_bytes') if isinstance(workspace, dict) else None
        declared_limit = workspace.get('memory_limit_bytes') if isinstance(workspace, dict) else None
        valid_peak = (not isinstance(peak, bool) and isinstance(peak, (int, float)) and
                      math.isfinite(peak) and peak >= 0)
        valid_limit = type(declared_limit) is int and declared_limit > 0
        if not valid_peak or not valid_limit:
            self._workspace_receipts_complete = False
        if (valid_peak and peak > remaining or valid_limit and declared_limit > remaining or
                valid_peak and valid_limit and peak > declared_limit):
            raise MemoryError('complete bit receiver exceeded its declared workspace bound')
        if valid_peak:
            self._peak_combined_bytes = max(self._peak_combined_bytes,
                job['parent_reservation']+int(peak))
        self._phase = 'quiet_slot_message_completion'
        if not job['integrity_valid']:
            self._episode_input_valid = False
        category = QuietSlotMessageReceiver._classification(row)
        recovering = self._recovery is not None
        if recovering:
            # The rejected episode is frozen at its local bound. Continue all
            # signal work, but admit no suffix into the codec or its history.
            message = self.message.result()
            capacity_eligible = self.message._capacity_eligible_slot(row, True)
        else:
            message = self.message.consume_slot(
                self._message_slot_index if self.continuous else self._slot_index, row)
            self._message_slot_index += 1
            capacity_eligible = message['complete_capacity_receipts']
        self._classification_counts[category] += 1
        self._capacity_receipts_complete &= capacity_eligible
        compact = {key: copy.deepcopy(row[key]) for key in (
            'schema', 'status', 'reason', 'slot_classification', 'decision_bit',
            'candidate_bit', 'bit_scores', 'operational',
            'sample_rate_hz', 'represented_baseband_seconds',
            'ratio_processing_seconds_per_baseband_second', 'procedure_id',
            'coverage', 'stages', 'workspace', 'quiet') if key in row}
        compact.update(index=self._slot_index, public_address=address,
            template_identity=score.get('template_identity'), slot_workers=job['workers'])
        encoded_size = len(json.dumps(compact, allow_nan=False).encode())
        if encoded_size > _RECEIPT_BYTES:
            raise MemoryError('slot diagnostic exceeds its bounded receipt size')
        self._slot_receipts.append(compact)
        self._slot_receipt_sizes.append(encoded_size)
        self._receipt_bytes += encoded_size
        if self.continuous and len(self._slot_receipts) > self.recent_slot_receipts:
            self._slot_receipts.pop(0)
            self._receipt_bytes -= self._slot_receipt_sizes.pop(0)
        self._slot_index += 1
        self._processed_stop = plan['receiver_window_stop_sample']
        self._processed_present_samples = job['present_prefix']
        if recovering:
            self._recovery_quiet_run = self._recovery_quiet_run+1 if category == 'QUIET' else 0
            if self._recovery_quiet_run >= self.message.quiet_slots:
                self._last_rejected_episode = dict(self._recovery,
                    resynchronized_at_public_slot=self._slot_index)
                self._recovery = None
                self._recovery_quiet_run = 0
                self._new_message()
        elif message['physical_complete']:
            self._decision_stop = plan['receiver_window_stop_sample']
            if self.continuous:
                self._queue_message(message, plan)
                self._new_message()
            else:
                self._state = 'finished'
                # The explicit one-message mode ends at its physical boundary.
                self._blocks.clear()
                self._buffer_bytes = self._buffer_samples = 0
                self._phase = 'physical_completion'
                return
        elif self.continuous and not message['data_bit_count']:
            self._leading_quiet_run = self._leading_quiet_run+1 if category == 'QUIET' else 0
            if self._leading_quiet_run >= self.message.quiet_slots:
                # A complete scored quiet boundary discards no decoded DATA.
                # Ambiguous leading history remains an erasure until this
                # resynchronization; quotas and transport events cannot reset it.
                self._new_message()
        if not recovering and (message['status'] == 'resource_limit' or
                (self._message_slot_index if self.continuous else self._slot_index) >= self.maximum_slots):
            reason = (message.get('reason') if message['status'] == 'resource_limit'
                      else 'local_slot_limit_before_physical_completion')
            if self.continuous:
                self._recovery = dict(reason=reason, first_public_slot=self._episode_first_slot,
                    rejected_at_public_slot=self._slot_index-1, physical_complete=False)
                self._last_rejected_episode = dict(self._recovery)
                self._recovery_quiet_run = 1 if category == 'QUIET' else 0
                self._rejected_episodes += 1
            else:
                self._fail('resource_limit', reason)
                return
        self._phase = 'public_slot_planning'

    def _ready(self):
        if self._pipeline or self._jobs:
            self._pump(wait=False)
            return
        while (not self._terminal() and not self._backpressured() and
                self._input_cursor >= self._next_plan['receiver_window_stop_sample']):
            self._process_slot(self._next_plan)
        if self._backpressured() and not self._terminal():
            self._phase = 'message_output_backpressure'

    def push(self, first_sample, values, valid=None):
        """Accept bounded captured input; native continuous DSP runs in owners.

        A full raw backlog waits for bounded ordered progress. A full message
        queue rejects this input without consuming it; drain then resume/retry.
        """
        if self._terminal() or self._input_closed:
            raise RuntimeError('stream receiver is terminal or input is closed')
        began = self._begin_service()
        try:
            if self._pipeline or self._jobs:
                self._drain_accepted(wait=False)
                if self._terminal():
                    return self.result()
                if len(self._service_clock.spans) >= _SERVICE_SPAN_LIMIT-8:
                    self._drain_accepted(wait=True)
                    if self._terminal():
                        return self.result()
            if self._backpressured():
                raise RuntimeError('message backpressure: drain messages and resume before retrying this input')
            if (not self._pipeline and not self._jobs and self.continuous and
                    self._input_cursor >= self._next_plan['receiver_window_stop_sample']):
                raise RuntimeError('retained ready input must be resumed before accepting new input')
            self._phase = 'input_validation'
            first = _integer(first_sample, 'input sample origin', 0)
            if not 1 <= len(values) <= self.maximum_input_chunk_samples:
                raise ValueError('input chunk exceeds the local count bound')
            data = np.asarray(values)
            if data.ndim != 1 or not np.iscomplexobj(data) or not np.isfinite(data).all():
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
            self._inflight_input_bytes = data.nbytes+mask.nbytes
            while self._pipeline and not self._can_accept(first, len(data)):
                self._make_ingress_progress()
                if self._terminal():
                    return self.result()
                if self._backpressured():
                    raise RuntimeError('message backpressure: drain messages and resume before retrying this input')
                if not self._jobs and not self._can_accept(first, len(data)):
                    self._processing_error(MemoryError('captured input exceeds the bounded receiver backlog'))
                    return self.result()
            try:
                self._phase = 'input_buffering'
                if gap:
                    self._gap_samples += gap
                    self._all_input_valid = False
                    while self._input_cursor < first and not self._terminal():
                        count = min(self.maximum_input_chunk_samples, first-self._input_cursor)
                        self._append(self._input_cursor, np.zeros(count, complex),
                                     np.zeros(count, np.uint8), supplied=False)
                        self._input_cursor += count
                        if not self.continuous:
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
                self._processing_error(error)
            except Exception as error:
                self._processing_error(error)
        except (KeyboardInterrupt, SystemExit):
            self._interrupt()
            raise
        finally:
            self._inflight_input_bytes = 0
            self._account_parent(began)
        return self.result(include_slots=self._terminal())

    def drain_messages(self):
        """Transfer all bounded completion events to the caller, without DSP.

        Call ``resume`` afterward to process any retained ready input. Input
        rejected due to output backpressure has not been consumed and can then
        be retried with its original sample origin.
        """
        began = self._begin_service()
        events = [event for event, _ in self._messages]
        self._messages.clear()
        self._message_bytes = 0
        self._account_parent(began)
        return events

    def resume(self, *, wait=True):
        """Drain accepted work, or only poll it with ``wait=False``.

        No new samples are consumed. Unread output still applies backpressure.
        After ``finish_input`` this drains retained work before reporting EOF.
        """
        if self._terminal():
            raise RuntimeError('stream receiver is terminal')
        if type(wait) is not bool:
            raise ValueError('wait must be a boolean')
        began = self._begin_service()
        try:
            self._drain_accepted(wait=wait)
        except (KeyboardInterrupt, SystemExit):
            self._interrupt()
            raise
        finally:
            self._account_parent(began)
        return self.result(include_slots=self._terminal())

    def finish_input(self):
        """Drain captured work; transport exhaustion never establishes quiet."""
        began = self._begin_service()
        try:
            self._input_closed = True
            if not self._terminal():
                self._drain_accepted(wait=True)
        except (KeyboardInterrupt, SystemExit):
            self._interrupt()
            raise
        finally:
            self._account_parent(began)
        return self.result()

    def cancel(self):
        began = self._begin_service()
        try:
            if not self._terminal():
                self.message.cancel()
                self._fail('cancelled', 'cancelled_before_physical_completion')
            try:
                self._stop_children()
            except Exception as error:
                self._processing_error(error)
        finally:
            self._account_parent(began)
        return self.result()

    def _result(self, *, include_slots):
        message = self.message.result()
        after_decision = (0 if self._decision_stop is None else
            max(0, self._input_cursor-max(self._decision_stop, self._last_present_first)))
        represented_samples = (self._processed_present_samples if self.continuous else
                               max(0, self._present_samples-after_decision))
        seconds = represented_samples/self.sample_rate
        pending_work = bool(self._jobs or (not self._terminal() and
            self._input_cursor >= self._next_plan['receiver_window_stop_sample']))
        complete = bool(not pending_work and self._workspace_receipts_complete and self._all_input_valid and
            not self._gap_samples and (self._capacity_receipts_complete and self._slot_index and
                self._state == 'receiving' if self.continuous else
                message['physical_complete'] and message['complete_capacity_receipts'] and
                self._state == 'finished'))
        backpressure = self._backpressured() and not self._terminal()
        return dict(schema='triad-complete-stream-receiver-v1',
            status=('message_backpressure' if backpressure else
                    message['status'] if self._state == 'finished' else self._state),
            reason=('completed_message_queue_full' if backpressure else
                    message.get('reason') if self._state == 'finished' else self._reason),
            diagnostic=dict(phase=self._phase, error_phase=self._error_phase,
                            exception_type=self._error_type),
            message=message, physical_complete=bool(not self.continuous and message['physical_complete']),
            continuous_listening=self.continuous, worker_allocation=self.workers,
            slot_pipeline=dict(enabled=self._pipeline, maximum_processes=2 if self._pipeline else 1,
                pending_jobs=len(self._jobs), submitted_jobs=self._pipeline_jobs_submitted,
                split_budget_jobs=self._pipeline_parallel_jobs,
                workspace_retry_count=self._workspace_retry_count,
                owner_startup_seconds=self._owner_startup_seconds,
                owner_startup_included_in_service=True,
                serial_fallback_reason=self._pipeline_fallback_reason,
                ordering='Public slot order; one parent message state machine.',
                service_clock='Union of actual worker execution and controller API intervals.'),
            public_schedule_scope='Configured public calendar and keyed ordinal; no arbitrary transmitter onset/address discovery.',
            completed_slot_count=self._slot_index,
            slot_classification_counts=dict(self._classification_counts),
            completed_message_count=self._completed_messages,
            rejected_episode_count=self._rejected_episodes,
            waiting_for_quiet_resynchronization=self._recovery is not None,
            recovery_quiet_slot_count=self._recovery_quiet_run,
            last_rejected_episode=copy.deepcopy(self._last_rejected_episode) if include_slots else None,
            pending_message_count=len(self._messages),
            messages=copy.deepcopy([event for event, _ in self._messages]) if include_slots else [],
            complete_receiver_work_covered=complete,
            sample_rate_hz=self.sample_rate,
            ingested_stop_sample=self._input_cursor, ingested_samples=self._present_samples,
            valid_ingested_samples=self._valid_samples, timestamp_gap_samples=self._gap_samples,
            decision_physical_stop_sample=self._decision_stop,
            processed_physical_stop_sample=self._processed_stop,
            received_samples_through_decision=represented_samples,
            represented_baseband_seconds=seconds,
            observed_ratio_processing_seconds_per_baseband_second=(self._service/seconds if seconds else None),
            ratio_processing_seconds_per_baseband_second=(self._service/seconds if complete and seconds else None),
            receiver_preparation_seconds=self.receiver_preparation_seconds,
            service_components=dict(
                complete_slot_pipeline_ratio=(self._slot_service/seconds if seconds else None),
                stream_orchestration_ratio=(max(0., self._service-self._slot_service)/seconds
                    if seconds else None)),
            processing_metric_scope=('Union of elapsed controller API intervals and actual background worker service, including input copies, public planning, recurring banks, full DSP, scheduling, quiet state machine and native source decoding. Simulator generation and waits between API calls are outside service. Denominator counts each physically supplied sample once, excludes timestamp gaps and stops at the latest completely processed public window; pending input is excluded. Overlapped search windows are never added. A covered prefix is not a whole-message sensitivity qualification.' if self.continuous else
                'Elapsed receive service including input copies, public planning, recurring banks, full DSP, scheduling, quiet state machine and native source decoding. Simulator generation is outside push. Denominator counts each physically supplied sample once, excludes timestamp gaps and stops at the actual quiet decision; overlapped search windows are never added.'),
            workspace=dict(peak_overlap_buffer_bytes=self._peak_buffer_bytes,
                peak_combined_estimate_bytes=self._peak_combined_bytes,
                retained_receipt_bytes=self._receipt_bytes,
                retained_message_bytes=self._message_bytes,
                reserved_receipt_bytes=self._receipt_reserve,
                complete_preparation_receipts=self._workspace_receipts_complete,
                local_memory_limit_bytes=self.memory_limit,
                controller_resident_baseline_bytes=self._controller_resident_bytes,
                spawn_auxiliary_reserved_bytes=self._spawn_auxiliary_reserve,
                peak_owned_process_snapshot_bytes=self._peak_owned_process_bytes,
                reservation_scope='Conservative owned-runtime and workspace estimate; RSS snapshots are not an OS hard cap.'),
            slot_receipt_count=len(self._slot_receipts),
            slots=copy.deepcopy(self._slot_receipts) if include_slots else [])

    def result(self, *, include_slots=True):
        """Materialize a bounded receipt and include that work in service."""
        began = self._begin_service()
        receipt = self._result(include_slots=include_slots)
        self._account_parent(began)
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
        began = self._begin_service()
        if not self._terminal():
            self.message.cancel()
            self._fail('cancelled', 'cancelled_before_physical_completion')
        try:
            self._stop_children()
        except Exception as error:
            self._processing_error(error)
        self._blocks.clear()
        self._buffer_bytes = self._buffer_samples = 0
        self._account_parent(began)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
