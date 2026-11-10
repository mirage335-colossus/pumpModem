"""Candidate-parallel CPU FP64 updates for complete mapped one/two-tap blocks."""
from __future__ import annotations

import ctypes
from functools import lru_cache
import math

import numpy as np
import torch


@lru_cache(maxsize=4)
def _load(path):
    try:
        update = ctypes.CDLL(str(path)).triad_gaussian_update_v1
    except (OSError, AttributeError) as error:
        raise OSError(f'native Gaussian kernel unavailable; rebuild triad_helper_export: {path}') from error
    update.argtypes = [ctypes.c_size_t]*6 + [ctypes.c_void_p]*8
    update.restype = ctypes.c_int
    return update


_ERRORS = (
    '', 'native Gaussian batch extent is unsupported',
    'Gaussian observation or Gram contains nonfinite values',
    'Gaussian observation Gram is not Hermitian',
    'Gaussian observation Gram is not positive semidefinite',
    'Gaussian predicted covariance lost positive semidefiniteness',
    'Gaussian innovation information is not positive definite',
    'Gaussian likelihood state became nonfinite',
)


class NativeGaussianBatch:
    """One synchronous batch on the receiver's existing worker pool.

    Native jobs update disjoint candidate intervals across all hypotheses and
    coherence models. Their state is private until every submitted job joins.
    """

    def __init__(self, path, executor, workers):
        self.kernel = _load(path)
        self.executor = executor
        self.workers = workers

    @staticmethod
    def supports(taps, candidates, models):
        return taps in (1, 2) and 1 <= candidates <= 4096 and 1 <= models <= 64

    def __call__(self, runtimes, observations):
        if len(observations) not in (1, 2) or not runtimes:
            raise ValueError(_ERRORS[1])
        first, first_gram, _ = observations[0]
        hypotheses, candidates, taps = first.shape
        models, blocks = len(runtimes), len(observations)
        shape = (hypotheses, candidates, taps)
        gram_shape = (*shape, taps)
        powers = runtimes[0].prior.stationary_power
        if (hypotheses != 3 or len(powers) != taps or not self.supports(taps, candidates, models) or
                any(r.shape != shape or g.shape != gram_shape or r.dtype != torch.complex128 or
                    g.dtype != r.dtype or r.device.type != 'cpu' or g.device != r.device or
                    r.requires_grad or g.requires_grad for r, g, _ in observations) or
                any(runtime.prior.stationary_power != powers or runtime.dtype != torch.complex128 or
                    not runtime.check_inputs or runtime.relative_tolerance != 1e-10 or
                    (runtime.mean is not None and
                     (runtime._r_shape != shape or runtime._g_shape != gram_shape or
                      runtime._device.type != 'cpu')) for runtime in runtimes)):
            raise ValueError(_ERRORS[1])
        correlations = np.ascontiguousarray(np.stack([r.numpy() for r, _, _ in observations]))
        grams = np.ascontiguousarray(np.stack([g.numpy() for _, g, _ in observations]))
        powers = np.array(powers)
        transitions = np.empty((blocks, models, 4))
        for block, (_, _, elapsed) in enumerate(observations):
            dt = float(elapsed)
            if not math.isfinite(dt) or dt < 0:
                raise ValueError('Gaussian elapsed block time must be finite and nonnegative')
            for model, runtime in enumerate(runtimes):
                decay, innovation, rotation = runtime.prior._transition(dt)
                transitions[block, model] = decay*decay, innovation, rotation.real, rotation.imag

        # Existing incremental workspace covers these small model/candidate
        # state and observation copies while candidate render scratch is gone.
        mean = np.empty((models, *shape), dtype=np.complex128)
        covariance = np.empty((models, *gram_shape), dtype=np.complex128)
        score = np.empty((models, hypotheses, candidates), dtype=np.float64)
        for model, runtime in enumerate(runtimes):
            if runtime.mean is None:
                mean[model].fill(0.)
                covariance[model] = np.diag(powers)
                score[model].fill(0.)
            else:
                mean[model] = runtime.mean.numpy()
                covariance[model] = runtime.covariance.numpy()
                score[model] = runtime.log_likelihood_ratio.numpy()

        jobs = min(candidates, self.workers)
        diagnostics = np.zeros((jobs, models, 2))
        buffers = correlations, grams, powers, transitions, mean, covariance, score

        def update(index):
            # The closure owns these arrays even if submission or waiting is
            # interrupted before the controller can retain the Future.
            pointers = tuple(array.ctypes.data for array in buffers)
            status = self.kernel(taps, candidates, models, blocks,
                index*candidates//jobs, (index+1)*candidates//jobs,
                *pointers, diagnostics[index].ctypes.data)
            if status:
                raise ValueError(_ERRORS[status])

        if jobs == 1:
            update(0)
        else:
            pending, error = [], None
            for index in range(jobs):
                try:
                    pending.append(self.executor.submit(update, index))
                except BaseException as exc:
                    error = exc
                    break
            for task in pending:
                while True:
                    try:
                        task.result()
                        break
                    except BaseException as exc:
                        if error is None:
                            error = exc
                        if task.done():
                            break
            if error is not None:
                raise error

        for model, runtime in enumerate(runtimes):
            runtime._commit_native_batch(first, first_gram, torch.from_numpy(mean[model]),
                torch.from_numpy(covariance[model]), torch.from_numpy(score[model]), blocks,
                diagnostics[:, model, 0].min(), diagnostics[:, model, 1].max())
        return jobs
