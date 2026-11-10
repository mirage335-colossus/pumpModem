"""Work-only probes for actual Robust stream plumbing and message-state cost.

The fixture never claims that a synthetic slot receipt was received. Actual
banks and signal work are measured separately through PreparedCapacityCase.
This module runs the production stream's copies, bounded buffer, public-window
assembly, dispatch, receipt accounting and the production quiet-message state
machine/canonical native decoder.
"""
from __future__ import annotations

import copy
import math
import time

import numpy as np

from triad_abbreviated import BATCHES, LOOKS
from triad_case_metrics import summarize_service
from triad_stream_receiver import CompleteStreamReceiver, native_short_decoder


def _public_plan(source, index):
    plan = source.slot_plan(index)
    if (not isinstance(plan, dict) or plan.get('index') != index or
            plan['receiver_window_samples'] < 1):
        raise ValueError('a valid public native slot plan is required')
    return plan


def _work_bank(_source, manifest, *, public_address, source_identity,
               memory_limit_bytes, threads):
    """The separately measured bank is omitted from this orchestration fixture."""
    return dict(screen=object(), manifest=copy.deepcopy(manifest),
        template_identity=f"work-only:{public_address['pattern_epoch']}:{public_address['symbol_ordinal']}",
        work_receipt=dict(preparation_peak_estimate_bytes=0),
        omitted=True, threads=threads,
        source_identity=source_identity, declared_remaining_bytes=memory_limit_bytes)


class _WindowReceiptOnlyBackend:
    """Inert dispatch endpoint; it emits no DATA/QUIET evidence."""
    def __init__(self, manifest, screen, source, *, workers, memory_limit_bytes, policy=None):
        self.samples = int(manifest['calendar']['capture_samples'])
        self.origin = int(manifest['calendar']['physical_origin_sample'])
        self.rate = int(manifest['sample_rate_hz'])
        self.closed = False

    def receive(self, values, valid):
        if len(values) != self.samples or len(valid) != self.samples:
            raise ValueError('work-only backend got different window geometry')
        # CompleteStreamReceiver.push already performed the full finite-sample
        # scan; a second inert-backend scan would double count DSP input work.
        return dict(schema='triad-complete-bit-receiver-v1', status='scored',
            slot_classification='AMBIGUOUS', decision_bit=None,
            sample_rate_hz=self.rate,
            represented_baseband_seconds=self.samples/self.rate,
            ratio_processing_seconds_per_baseband_second=0.,
            coverage=dict(complete_observation=True, all_input_valid=bool(valid.all()),
                consumed_samples=self.samples, expected_samples=self.samples,
                physical_first_sample=self.origin, physical_stop_sample=self.origin+self.samples,
                zero_h0_verified=False, valid_noise_statistics=False),
            stages={name: dict(included_in_service=True,
                represented_baseband_seconds=self.samples/self.rate,
                ratio_processing_seconds_per_baseband_second=0.)
                for name in ('helper','coarse','refinement_selection','fine_preparation',
                             'tracking','decision')},
            workspace=dict(estimated_peak_bytes=0))

    def close(self):
        self.closed = True


def _new_work_stream(prepared, plan, *, maximum_slots=418, source=None):
    manifest = copy.deepcopy(prepared.manifest)
    manifest['sample_rate_hz'] = plan['sample_rate_hz']
    source = prepared.basis_source if source is None else source
    return CompleteStreamReceiver(manifest, source,
        continuous=False,
        source_identity=dict(work_fixture=True),
        memory_limit_bytes=prepared.memory_limit_bytes,
        workers=1, decode_short_message=lambda _bits: {
            'decoded_bytes':0, 'filtered_text':''}, maximum_slots=maximum_slots,
        maximum_data_bits=208, maximum_input_chunk_samples=16384,
        maximum_buffer_samples=prepared.memory_limit_bytes//68,
        maximum_gap_samples=16384, bank_builder=_work_bank,
        receiver_factory=_WindowReceiptOnlyBackend)


def _push_units(prepared, units, *, chunk_samples=16384):
    if isinstance(units, bool) or not isinstance(units, int) or units not in BATCHES:
        raise ValueError('work probe units must be in the declared1/2/4 set')
    plan = _public_plan(prepared.basis_source, 0)
    if prepared.observation_first_sample != 0:
        raise ValueError('orchestration fixture requires the actual global-zero input prefix')
    if plan['receiver_window_first_sample'] + plan['receiver_window_samples'] > len(prepared.raw):
        raise ValueError('prepared native observation does not cover public window zero')
    count = units*chunk_samples
    if count >= plan['receiver_window_stop_sample']:
        raise ValueError('training push must stop before full-window assembly')
    if 17*count*2 + prepared.raw.nbytes+prepared.valid.nbytes >= prepared.memory_limit_bytes:
        raise MemoryError('bounded chunk work probe exceeds declared workspace')
    stream = _new_work_stream(prepared, plan)
    try:
        began = time.perf_counter()
        at = 0
        while at < count:
            stop = min(count, at+chunk_samples)
            stream.push(at, prepared.raw[at:stop], prepared.valid[at:stop])
            at = stop
        elapsed = time.perf_counter()-began
        retained=max(0,count-plan['receiver_window_first_sample'])
        if stream._slot_index or stream._buffer_samples != retained:
            raise RuntimeError('chunk training unexpectedly dispatched an incomplete window')
        # Denominator is each input sample once, not summed overlapping slots.
        seconds = count/prepared.fs
        return dict(ratio_processing_seconds_per_baseband_second=elapsed/seconds,
            represented_baseband_seconds=seconds, units=units,
            samples=count, chunks=units, public_plan_index=0,
            status=stream._state, peak_overlap_buffer_bytes=stream._peak_buffer_bytes,
            peak_combined_estimate_bytes=stream._peak_combined_bytes,
            scope='Actual production stream input validation, copying and bounded overlap retention; before a complete slot is ready.')
    finally:
        stream.close()


def _full_window_assembly(prepared, *, recurring_slot=None, resident_bank_bytes=None):
    index = 0 if recurring_slot is None else recurring_slot
    plan = _public_plan(prepared.basis_source, index)
    count = plan['receiver_window_samples']
    native_first = plan['receiver_window_first_sample']
    input_first = 0
    if recurring_slot is None:
        if prepared.observation_first_sample != 0:
            raise ValueError('orchestration fixture requires the actual global-zero input prefix')
        if resident_bank_bytes is None:
            resident_bank_bytes = prepared.screen.resident_estimate
    else:
        # Exercise the real public successor geometry and its input gap.
        # For overlapping windows, copying the entire current window is a
        # conservative work fixture; an actual stream retains some old rows.
        previous = prepared.calendar['plans'][index-1]
        input_first = min(native_first, previous['receiver_window_stop_sample'])
        if resident_bank_bytes is None:
            raise ValueError('recurring orchestration requires its resident bank estimate')
    first = native_first-input_first
    input_count=plan['receiver_window_stop_sample']-input_first
    if (len(prepared.raw) < (input_count if recurring_slot is None else count)
            or input_count != first+count):
        raise ValueError('prepared native observation does not cover the complete public window')
    # Count the already resident observation once; the work fixture does not
    # regenerate native channel samples or allocate another full observation.
    chunk = 16384
    stream_reserve = getattr(prepared, 'stream_workspace', {}).get(
        'simultaneous_outer_reserve_bytes', 17*(count+chunk)+418*16384)
    expected = (prepared.raw.nbytes+prepared.valid.nbytes + 17*count+
        stream_reserve + resident_bank_bytes +
        max(int(prepared.basis_source.descriptor.get('fixed_workspace_bytes',0)),
            int(prepared.basis_source.descriptor.get('workspace_budget_bytes',0))))
    if expected >= prepared.memory_limit_bytes:
        raise MemoryError('full-window stream work probe exceeds current host workspace budget')
    class TimedSource:
        def __init__(self, source):
            self._source = source
            self.elapsed = 0.
        def slot_plan(self, index):
            began = time.perf_counter()
            try:
                row = self._source.slot_plan(index+(recurring_slot or 0))
                if recurring_slot is not None:
                    # Only physical assembly coordinates are consumed by
                    # this inert endpoint. Local zero replaces the earlier
                    # stream history; all sampled window/gap lengths survive.
                    row = dict(row, index=index)
                    for name in ('receiver_window_first_sample', 'receiver_window_stop_sample',
                                 'receiver_window_physical_origin_sample'):
                        row[name] -= input_first
                return row
            finally:
                self.elapsed += max(0.,time.perf_counter()-began)
        def __getattr__(self, name):
            return getattr(self._source,name)
    timed_source=TimedSource(prepared.basis_source)
    stream = _new_work_stream(prepared, plan, source=timed_source)
    # Stream construction gets public plan0 as fixed receiver readiness; only
    # planning performed inside timed push belongs to sustained service.
    timed_source.elapsed=0.
    wrapper_elapsed = 0.
    original_consume = stream.message.consume_slot
    def timed_consume(*args, **kwargs):
        nonlocal wrapper_elapsed
        began = time.perf_counter()
        try:
            return original_consume(*args, **kwargs)
        finally:
            wrapper_elapsed += max(0., time.perf_counter()-began)
    stream.message.consume_slot = timed_consume
    before_rss = None
    try:
        from triad_case_receiver import _rss_snapshot
        before_rss = _rss_snapshot(prepared.basis_source)
        began = time.perf_counter()
        at = 0
        while at < input_count:
            stop = min(input_count, at+chunk)
            if recurring_slot is None:
                begin, end = at, stop
            elif at < first:
                stop = min(stop, first, at+len(prepared.raw))
                # Reuse already-owned input only for the pre-window copy
                # fixture. It cannot initialize a signal receiver.
                begin, end = 0, stop-at
            else:
                begin, end = at-first, stop-first
            stream.push(at, prepared.raw[begin:end], prepared.valid[begin:end])
            at = stop
        elapsed = time.perf_counter()-began
        seconds = input_count/prepared.fs
        plan_seconds=timed_source.elapsed
        controller_elapsed=max(0.,elapsed-wrapper_elapsed-plan_seconds)
        receipt = stream.result()
        if stream._slot_index != 1 or stream.message._classification_counts['AMBIGUOUS'] != 1:
            raise RuntimeError('full-window work fixture did not dispatch exactly one ambiguous slot')
        return dict(ratio_processing_seconds_per_baseband_second=elapsed/seconds,
            represented_baseband_seconds=seconds,
            exact_window_samples=count, exact_window_seconds=count/prepared.fs,
            physical_ingested_samples=input_count,
            public_slot_index=index, native_window_first_sample=native_first,
            actual_prefix_samples=first,
            ingest_copy_assembly_dispatch_including_one_state_call=True,
            state_machine_elapsed_seconds_per_baseband_second=wrapper_elapsed/seconds,
            public_slot_planning_seconds_per_baseband_second=plan_seconds/seconds,
            nonoverlap_stream_orchestration_seconds_per_baseband_second=controller_elapsed/seconds,
            signal_receipt='AMBIGUOUS work-only synthetic dispatch; no reception decision',
            complete_receiver_work_covered=False,
            estimated_peak_bytes=max(expected, receipt['workspace']['peak_combined_estimate_bytes']),
            memory_budget_bytes=prepared.memory_limit_bytes,
            resident_sample_before=before_rss,
            resident_sample_after=_rss_snapshot(prepared.basis_source),
            slot_receipts=receipt['slot_receipt_count'],
            scope='One actual native public-window assembly/dispatch through the production stream code using an already-owned baseband observation. Bank and signal DSP are omitted and measured separately.')
    finally:
        stream.close()


def _message_state_fixture(prepared, *, family_size):
    """Measure exact22-bit fox+2quiet state/codec CPU using public slot plans.

    Test receipts are local work input, never claims that the represented signal
    was received. The decoder itself is the canonical native short codec.
    """
    from triad_message_receiver import QuietSlotMessageReceiver
    encoded = __import__('subprocess').run([str(prepared.executable), '--short-encode','fox'],
        capture_output=True, check=True, timeout=10)
    bits = __import__('json').loads(encoded.stdout)['bits']
    if len(bits) != 22 or any(char not in '01' for char in bits):
        raise RuntimeError('canonical literal fox codec no longer has its declared22-bit payload')
    plan_rows = []
    plans = []
    for _ in range(LOOKS[-1]):
        began = time.perf_counter()
        plans.append([_public_plan(prepared.basis_source,index) for index in range(24)])
        plan_rows.append(time.perf_counter()-began)
    plans = plans[0]
    rate = prepared.fs
    physical_start = plans[0]['receiver_window_first_sample']
    # The outer service input contains each physical baseband sample once. Use
    # the fixed public profile's last plan endpoint, including overlap/gaps.
    physical_stop = plans[-1]['receiver_window_stop_sample']
    span = (physical_stop-physical_start)/rate
    if span <= 0:
        raise ValueError('public message fixture has no positive physical span')
    decoder = native_short_decoder(prepared.executable)
    fixtures=[]
    for index, plan in enumerate(plans):
        count = plan['receiver_window_samples']
        first = plan['receiver_window_first_sample']
        quiet = index >= 22
        result = dict(status='scored', slot_classification='QUIET' if quiet else 'DATA',
            decision_bit=None if quiet else int(bits[index]),
            operational={'passes_conditional_bound':not quiet},
            slot_classification_scope='fixed local work fixture; no radio observation',
            sample_rate_hz=rate,
            represented_baseband_seconds=count/rate,
            ratio_processing_seconds_per_baseband_second=0.,
            coverage=dict(complete_observation=True, all_input_valid=True,
                consumed_samples=count, expected_samples=count,
                physical_first_sample=first, physical_stop_sample=first+count,
                zero_h0_verified=True, valid_noise_statistics=True),
            quiet=dict(noise_consistent=quiet, projected_energy=0. if quiet else 1.,
                threshold=1. if quiet else 0., observed_projection_modes=20,
                noise_upper_tail_probability=.001),
            stages={name:dict(included_in_service=True,
                represented_baseband_seconds=count/rate,
                ratio_processing_seconds_per_baseband_second=0.)
                for name in ('helper','coarse','refinement_selection','fine_preparation',
                    'tracking','decision','recurring_bank_preparation')})
        fixtures.append((index,result))
    outputs = []
    for _ in range(LOOKS[-1]):
        receiver = QuietSlotMessageReceiver(decoder, maximum_slots=24,
            maximum_data_bits=208, sample_rate_hz=rate)
        began = time.perf_counter()
        for index, result in fixtures:
            receipt = receiver.consume_slot(index,result)
        elapsed = time.perf_counter()-began
        if (receipt['status'] != 'complete' or receipt['decoded_content'] !=
                {'status':'decoded','decoded_bytes':3,'filtered_text':'fox'}):
            raise RuntimeError('work-only fox wrapper fixture did not exercise canonical decode')
        outputs.append(elapsed/span)
    # Fixed repeat rows use the existing arithmetic interval machinery. No
    # allocated CPU speedup is inferred from these wrapper-only measurements.
    from triad_case_metrics import arithmetic_interval
    ratios = [outputs[index] for index in range(len(outputs))]
    alpha = .05/(family_size*len(LOOKS)*3)
    point, interval = arithmetic_interval(ratios, alpha=alpha)
    plan_ratios=[elapsed/span for elapsed in plan_rows]
    plan_point,plan_interval=arithmetic_interval(plan_ratios,alpha=alpha)
    return dict(work_only=True, codec='canonical native fox 22 bits',
        physical_slots=24, data_fixture_slots=22, quiet_fixture_slots=2,
        public_physical_span_seconds=span,
        ratio_processing_seconds_per_baseband_second=point,
        ratio_processing_seconds_per_baseband_second_interval=interval,
        samples=ratios, decoder_invocations=len(outputs),
        public_slot_plan_service=dict(
            ratio_processing_seconds_per_baseband_second=plan_point,
            ratio_processing_seconds_per_baseband_second_interval=plan_interval,
            samples=plan_ratios, plans_per_sample=24,
            scope='Native public slot-plan control cost measured separately from the synthetic message state fixture.'),
        scope='Actual quiet-slot state machine and canonical native codec subprocess on explicitly synthetic local receipts. This is receiver work cost only; the slot decisions are fabricated for the bounded work fixture and do not establish signal or message reception.')


def measure_orchestration(prepared, *, family_size=1):
    """Return bounded work-only stream plumbing and physical-completion costs."""
    from triad_case_metrics import arithmetic_interval
    if isinstance(family_size,bool) or not isinstance(family_size,int) or family_size<1:
        raise ValueError('capacity family size must be a positive integer')
    chunk_rows=[]
    for look in LOOKS:
        while len(chunk_rows)<look:
            row=[None]*len(BATCHES)
            for index in ((len(chunk_rows)+offset)%len(BATCHES) for offset in range(len(BATCHES))):
                sample=_push_units(prepared,BATCHES[index])
                row[index]=sample['ratio_processing_seconds_per_baseband_second']
            chunk_rows.append(row)
        summary=summarize_service(chunk_rows,family_size=family_size)
        if summary['stop']:
            break
    chunk_summary=dict(summary,geometry=prepared.public_geometry,
        model='One/two chunks train, four chunks hold out for linear input-copy and validity-check work.',
        coverage='Short chunk model does not claim resident-to-memory full-window assembly.')
    full=_full_window_assembly(prepared)
    message=_message_state_fixture(prepared,family_size=family_size)
    # This module reports components but never makes an absolute receiver pass.
    return dict(schema='triad-complete-orchestration-work-v1',work_only=True,
        reception_success_established=False,absolute_capacity_status='unresolved_work_only_fixture',
        outer_timing_rows=len(chunk_rows),chunk_ingest=chunk_summary,
        full_window_assembly=full,message_state_and_codec=message,
        fixed_work_risk='Full public-window assembly is measured directly. Per-message wrapper/codec fixture has no DSP backend; native bank and complete DSP costs must be combined separately.',
        sample_repeat_independence=False,
        scope='Production stream copy/assemble/dispatch and quiet-message state/codec work are executed. Inert backend/bank/slot receipts are work fixtures only. No physical signal, absolute success or CPU-capacity pass is claimed.')
