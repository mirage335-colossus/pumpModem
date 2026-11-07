#!/usr/bin/env python3
"""Bounded public-window scheduling for continuous isolated triad experiments.

This module schedules a declared finite list of windows; it does not discover
transmission onsets, choose epochs, or admit bits. Ingestion never waits for the
scoring worker. Missing data and overload are explicit unresolved outcomes.
"""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import math
import threading
import time

import numpy as np


def _integer(value, name, *, positive=False):
    if isinstance(value, (bool, np.bool_)) or not isinstance(value, (int, np.integer)):
        raise ValueError(name+' must be an integer')
    value=int(value)
    if positive and value <= 0:
        raise ValueError(name+' must be positive')
    return value


@dataclass(frozen=True)
class WindowRequest:
    name: str
    start_sample: int
    length: int

    @property
    def end_sample(self):
        return self.start_sample+self.length


class StreamingWindowScheduler:
    """One ingesting producer, bounded asynchronous scorers and numeric storage.

    ``score(index, raw, valid)`` receives a private complete window copy. Index
    is its index in the original public request list, not completion order.
    Requests may overlap and finish out of start order. Starts must be ordered.

    ``append`` accepts consecutive I/Q; optional ``start_sample`` must equal the
    current integer frontier. Supply missing samples through ``gap`` so their
    times remain present. Invalid windows are never passed to the scorer and
    cannot be counted as scored absence. Queue overload also returns a named
    unresolved record instead of blocking acquisition or quietly losing work.

    ``close(timeout=None)`` stops ingestion and drains queued jobs. A timeout
    reports ``drained=False``; it does not kill or detach active work. Call close
    again after resolving a blocked callback to observe its eventual completion.
    Results are caller-owned compact objects: storing complete raw input in a
    callback result would defeat the documented numeric workspace bound.
    With more than one worker the callback must own its mutable workspaces per
    job. Shared immutable templates are safe; a single mutable receiver is not.
    """

    def __init__(self, requests, score, *, start_sample=0,
                 ring_capacity_samples=None, queue_capacity=2,
                 dtype=np.complex64, maximum_gap_records=128, workers=1,
                 worker_initializer=None):
        self.start_sample=_integer(start_sample,'stream start sample')
        self.frontier=self.start_sample
        if not callable(score):raise ValueError('scorer must be callable')
        if worker_initializer is not None and not callable(worker_initializer):
            raise ValueError('worker initializer must be callable')
        self.score=score
        self.worker_initializer=worker_initializer
        self.dtype=np.dtype(dtype)
        if self.dtype not in (np.dtype(np.complex64),np.dtype(np.complex128)):
            raise ValueError('storage dtype must be complex64 or complex128')
        self.queue_capacity=_integer(queue_capacity,'queue capacity',positive=True)
        self.workers=_integer(workers,'workers',positive=True)
        self.maximum_gap_records=_integer(maximum_gap_records,'maximum gap records',positive=True)
        self.requests=[]
        names=set();previous=None
        for source in requests:
            if isinstance(source,dict):
                try:source=WindowRequest(**source)
                except TypeError as error:raise ValueError('request needs name, start_sample and length')from error
            if not isinstance(source,WindowRequest):raise ValueError('WindowRequest or matching dictionary required')
            if not isinstance(source.name,str)or not source.name or source.name in names:
                raise ValueError('window names must be nonempty and unique')
            start=_integer(source.start_sample,'window start')
            length=_integer(source.length,'window length',positive=True)
            if start<self.start_sample or (previous is not None and start<previous):
                raise ValueError('window starts must be ordered and inside the stream')
            self.requests.append(WindowRequest(source.name,start,length));names.add(source.name);previous=start
        if not self.requests:raise ValueError('at least one public window required')
        self.requests=tuple(self.requests)
        self.maximum_window=max(r.length for r in self.requests)
        if ring_capacity_samples is None:ring_capacity_samples=2*self.maximum_window
        self.capacity=_integer(ring_capacity_samples,'ring capacity',positive=True)
        if self.capacity<self.maximum_window:raise ValueError('ring capacity must cover the largest full window')
        self._ring=np.empty(self.capacity,dtype=self.dtype)
        self._valid=np.zeros(self.capacity,dtype=bool)
        self._ready_order=sorted(range(len(self.requests)),key=lambda i:(self.requests[i].end_sample,i))
        self._next_ready=0
        self._queue=deque()
        self._condition=threading.Condition()
        self._ingest_lock=threading.Lock()
        self._closing=False
        self._started_wall=time.perf_counter();self._started_cpu=time.process_time()
        self._inflight=set()
        self._worker_initializations=[]
        self._ingested=0;self._observed=0;self._invalid=0;self._gap_samples=0
        self._gap_records=[];self._gap_count=0
        self._queue_highwater=0;self._ring_highwater=0
        self._records=[dict(index=i,name=r.name,start_sample=r.start_sample,length=r.length,
            end_sample=r.end_sample,status='pending',valid_samples=None,result=None,
            ready_wall_seconds=None,submitted_wall_seconds=None,started_wall_seconds=None,
            finished_wall_seconds=None)for i,r in enumerate(self.requests)]
        self._threads=[threading.Thread(target=self._worker,
            name='triad-window-scorer-'+str(i),daemon=True)for i in range(self.workers)]
        for thread in self._threads:thread.start()

    def _wall(self):return time.perf_counter()-self._started_wall

    def _worker(self):
        initialization_error=None
        try:
            receipt=self.worker_initializer()if self.worker_initializer is not None else None
            initialization=dict(status='ready',receipt=receipt)
        except Exception as error:
            initialization_error=type(error).__name__+': '+str(error)
            initialization=dict(status='error',error=initialization_error)
        with self._condition:
            self._worker_initializations.append(dict(worker=threading.current_thread().name,**initialization))
        while True:
            with self._condition:
                while not self._queue and not self._closing:self._condition.wait()
                if not self._queue:return
                index,raw,valid=self._queue.popleft()
                self._inflight.add(index)
                record=self._records[index]
                record.update(status='running',started_wall_seconds=self._wall())
            cpu,thread_cpu,wall=time.process_time(),time.thread_time(),time.perf_counter()
            result=None
            try:
                if initialization_error is not None:
                    raise RuntimeError('worker initialization failed: '+initialization_error)
                result=self.score(index,raw,valid)
                outcome=dict(status='scored',result=result)
            except Exception as error:
                outcome=dict(status='scorer_error',error_type=type(error).__name__,error=str(error))
            process_delta=time.process_time()-cpu
            thread_delta=time.thread_time()-thread_cpu
            elapsed=time.perf_counter()-wall
            with self._condition:
                record.update(outcome,finished_wall_seconds=self._wall(),
                    scoring_wall_seconds=elapsed,process_cpu_seconds=process_delta,
                    worker_thread_cpu_seconds=thread_delta)
                record['queue_wait_seconds']=record['started_wall_seconds']-record['submitted_wall_seconds']
                record['completion_backlog_seconds']=record['finished_wall_seconds']-record['ready_wall_seconds']
                self._inflight.remove(index)
                self._condition.notify_all()
            # Release private numerical copies before taking the next job.
            del raw,valid,result,outcome

    def _copy_window(self,request):
        if request.start_sample<self.frontier-self.capacity or request.end_sample>self.frontier:
            raise RuntimeError('public window support was overwritten or is incomplete')
        first=request.start_sample%self.capacity
        split=min(request.length,self.capacity-first)
        raw=np.empty(request.length,dtype=self.dtype);valid=np.empty(request.length,dtype=bool)
        raw[:split]=self._ring[first:first+split];valid[:split]=self._valid[first:first+split]
        if split<request.length:
            raw[split:]=self._ring[:request.length-split];valid[split:]=self._valid[:request.length-split]
        return raw,valid

    def _submit_ready(self):
        while self._next_ready<len(self._ready_order):
            index=self._ready_order[self._next_ready];request=self.requests[index]
            if request.end_sample>self.frontier:break
            self._next_ready+=1
            ready=self._wall()
            raw,valid=self._copy_window(request)
            with self._condition:
                record=self._records[index]
                record.update(ready_wall_seconds=ready,valid_samples=int(valid.sum()),
                    supplied_invalid_samples=int((~valid).sum()))
                if not valid.all():
                    record.update(status='unresolved_invalid_samples',finished_wall_seconds=self._wall())
                elif len(self._queue)>=self.queue_capacity:
                    record.update(status='unresolved_overload',finished_wall_seconds=self._wall())
                else:
                    record.update(status='queued',submitted_wall_seconds=self._wall())
                    self._queue.append((index,raw,valid))
                    self._queue_highwater=max(self._queue_highwater,len(self._queue))
                    self._condition.notify()

    def _append(self,raw,valid,*,gap):
        offset=0
        while offset<len(raw):
            # Stop at every public completion point. A large caller chunk can
            # therefore never overwrite a requested window before its copy.
            next_end=(self.requests[self._ready_order[self._next_ready]].end_sample
                      if self._next_ready<len(self._ready_order)else self.frontier+len(raw)-offset)
            count=min(len(raw)-offset,self.capacity,next_end-self.frontier)
            if count<=0:raise RuntimeError('window readiness did not advance')
            values=np.asarray(raw[offset:offset+count],dtype=self.dtype)
            mask=valid[offset:offset+count]
            at=self.frontier%self.capacity;split=min(count,self.capacity-at)
            self._ring[at:at+split]=values[:split];self._valid[at:at+split]=mask[:split]
            if split<count:
                self._ring[:count-split]=values[split:];self._valid[:count-split]=mask[split:]
            self.frontier+=count;self._ingested+=count;offset+=count
            self._ring_highwater=max(self._ring_highwater,min(self.capacity,self._ingested))
            self._invalid+=int((~mask).sum())
            if not gap:self._observed+=count
            self._submit_ready()

    def append(self,raw,valid=None,*,start_sample=None):
        if not self._ingest_lock.acquire(blocking=False):raise RuntimeError('concurrent producers are unsupported')
        try:
            if self._closing:raise RuntimeError('scheduler is closed for ingestion')
            if start_sample is not None and _integer(start_sample,'chunk start')!=self.frontier:
                raise ValueError('chunk does not start at the frontier; supply an explicit gap')
            raw=np.asarray(raw)
            if raw.ndim!=1 or not np.iscomplexobj(raw):raise ValueError('one-dimensional complex I/Q required')
            mask=np.ones(len(raw),bool)if valid is None else np.asarray(valid)
            if mask.shape!=raw.shape or not np.isin(mask,(0,1)).all():raise ValueError('binary validity must match I/Q')
            mask=mask.astype(bool,copy=False)
            if not np.isfinite(raw[mask]).all():raise ValueError('valid I/Q must be finite')
            if raw.dtype.itemsize>self.dtype.itemsize:
                maximum=np.finfo(np.float32).max
                if np.any(abs(raw.real[mask])>maximum)or np.any(abs(raw.imag[mask])>maximum):
                    raise ValueError('valid I/Q overflows selected storage')
            self._append(raw,mask,gap=False)
            return self.frontier
        finally:self._ingest_lock.release()

    def gap(self,samples):
        count=_integer(samples,'gap samples')
        if count<0:raise ValueError('gap samples must be nonnegative')
        if not self._ingest_lock.acquire(blocking=False):raise RuntimeError('concurrent producers are unsupported')
        try:
            if self._closing:raise RuntimeError('scheduler is closed for ingestion')
            if count:
                self._gap_count+=1
                if len(self._gap_records)<self.maximum_gap_records:
                    self._gap_records.append(dict(start_sample=self.frontier,length=count))
                self._gap_samples+=count
            remaining=count
            while remaining:
                n=min(self.capacity,remaining)
                self._append(np.zeros(n,dtype=self.dtype),np.zeros(n,bool),gap=True)
                remaining-=n
            return self.frontier
        finally:self._ingest_lock.release()

    def snapshot(self):
        with self._condition:
            records=[dict(r)for r in self._records]
            backlog=[r['completion_backlog_seconds']for r in records if 'completion_backlog_seconds'in r]
            return dict(schema='triad-stream-window-scheduler-v1',
                scope='Fixed public window schedule; no generic onset/epoch discovery, bit admission or physical-end inference',
                start_sample=self.start_sample,frontier_sample=self.frontier,
                ingested_samples=self._ingested,observed_input_samples=self._observed,
                invalid_input_samples=self._invalid,gap_samples=self._gap_samples,
                gap_events=self._gap_count,gap_records=list(self._gap_records),
                gap_records_truncated=self._gap_count>len(self._gap_records),
                ring_capacity_samples=self.capacity,ring_highwater_samples=self._ring_highwater,
                queue_capacity=self.queue_capacity,queue_depth=len(self._queue),
                queue_highwater=self._queue_highwater,workers=self.workers,
                inflight_index=next(iter(self._inflight))if len(self._inflight)==1 else None,
                inflight_indices=sorted(self._inflight),
                worker_initializations=list(self._worker_initializations),
                numeric_array_bound_bytes=(self.capacity+(self.queue_capacity+self.workers+1)*self.maximum_window)*(self.dtype.itemsize+1),
                numeric_bound_scope='Ring plus queued, all worker and one producer window copy; excludes caller input, callback workspace/results and Python metadata',
                elapsed_wall_seconds=self._wall(),process_cpu_seconds=time.process_time()-self._started_cpu,
                per_job_cpu_scope='Process CPU deltas include concurrent ingest and other process threads; do not sum them as exclusive worker cost. Worker thread CPU excludes numerical-library worker threads.',
                maximum_completion_backlog_seconds=max(backlog,default=0.),
                closing=self._closing,drained=self._closing and not any(t.is_alive()for t in self._threads),records=records)

    def close(self,timeout=None):
        if timeout is not None and (not math.isfinite(float(timeout))or float(timeout)<0):
            raise ValueError('close timeout must be finite and nonnegative')
        if not self._ingest_lock.acquire(blocking=False):raise RuntimeError('cannot close during concurrent ingestion')
        try:
            with self._condition:
                if not self._closing:
                    self._closing=True
                    for record in self._records:
                        if record['status']=='pending':
                            record.update(status='unresolved_incomplete',finished_wall_seconds=self._wall(),
                                available_samples=max(0,min(self.frontier,record['end_sample'])-record['start_sample']))
                    self._condition.notify_all()
        finally:self._ingest_lock.release()
        deadline=None if timeout is None else time.perf_counter()+timeout
        for thread in self._threads:
            thread.join(None if deadline is None else max(0.,deadline-time.perf_counter()))
        return self.snapshot()
