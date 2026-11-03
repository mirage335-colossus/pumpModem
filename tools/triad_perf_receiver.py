#!/usr/bin/env python3
"""Persistent JSONL adapter for measuring the isolated triad receiver APIs.

This process deliberately imports the receiver implementation from an explicit
source checkout. Start a separate process for each revision being compared.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import sys
import time
import traceback

import numpy as np


MODULES = ("triad_fft_acquire", "triad_group_acquire", "triad_gaussian_window",
           "triad_noise_likelihood", "triad_gaussian_state", "triad_state_workspace")


def _sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def _jsonable(value):
    if isinstance(value, dict):
        return {str(k): _jsonable(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [_jsonable(v) for v in value]
    if isinstance(value, np.ndarray):
        return value.tolist()
    if isinstance(value, np.generic):
        return value.item()
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, float) and not math.isfinite(value):
        return str(value)
    return value


class ReceiverService:
    def __init__(self, source_root, threads):
        self.root = Path(source_root).resolve(strict=True)
        self.tools = self.root / "tools"
        if not self.tools.is_dir():
            raise ValueError("source-root must contain tools/")
        self.paths = {name: self.tools / (name + ".py") for name in MODULES}
        for path in self.paths.values():
            if path.is_symlink() or not path.is_file():
                raise ValueError(f"missing or symlinked source module: {path.name}")
        sys.path.insert(0, str(self.tools))
        import torch
        torch.set_num_threads(threads)
        torch.set_num_interop_threads(1)
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.backends.cudnn.allow_tf32 = False
        if hasattr(torch, "set_float32_matmul_precision"):
            torch.set_float32_matmul_precision("highest")
        self.torch = torch
        self.modules = {name: importlib.import_module(name) for name in MODULES}
        for name, module in self.modules.items():
            if Path(module.__file__).resolve() != self.paths[name].resolve():
                raise RuntimeError(f"module {name} did not load from requested source root")
        self.workspace = None
        self.sampled = None
        self.source_hashes = self._source_hashes()

    def _source_hashes(self):
        return {name + ".py": _sha(path) for name, path in self.paths.items()}

    def _check_sources(self):
        current = self._source_hashes()
        if current != self.source_hashes:
            raise RuntimeError("receiver source changed during measurement")

    def hello(self):
        self._check_sources()
        return {"source_root": str(self.root), "source_hashes": self.source_hashes,
                "python": sys.version.split()[0], "numpy": np.__version__,
                "torch": self.torch.__version__, "threads": self.torch.get_num_threads(),
                "cuda_available": self.torch.cuda.is_available(),
                "device_count": self.torch.cuda.device_count() if self.torch.cuda.is_available() else 0,
                "tf32_allowed": False, "startup_seconds": getattr(self, "startup_seconds", None)}

    def prepare(self, request):
        self._check_sources()
        started = time.perf_counter()
        inpath = Path(request["input_directory"])
        if inpath.is_symlink():
            raise ValueError("input_directory may not be a symlink")
        inpath = inpath.resolve(strict=True)
        # Drop the preceding full workspace before allocating its replacement.
        self.workspace = self.sampled = None
        sampled = self.modules["triad_fft_acquire"].SampledWorkspace(inpath)
        lag_max_seconds = float(request["lag_max_seconds"])
        step = request.get("lag_step_samples", 1)
        if not math.isfinite(lag_max_seconds) or lag_max_seconds < 0 or isinstance(step, bool) or not isinstance(step, int) or step < 1:
            raise ValueError("invalid lag extent or step")
        maximum = int(np.ceil(np.longdouble(lag_max_seconds) * np.longdouble(sampled.sample_rate)))
        if maximum >= sampled.samples:
            raise ValueError("requested lag extent exceeds the sampled observation")
        lags = self.modules["triad_fft_acquire"].lag_grid(maximum, step)
        memory = int(float(request.get("memory_mib", 512)) * 1024 * 1024)
        if memory < 1:
            raise ValueError("memory_mib must be positive")
        workspace = self.modules["triad_fft_acquire"].FFTWorkspace(
            sampled, lags, precision=request.get("precision", "fp64"),
            device=request.get("device", "cpu"), memory_limit_bytes=memory)
        sampled.verify_unchanged()
        self._check_sources()
        self.sampled, self.workspace = sampled, workspace
        self.group_tile = request.get("group_tile", 128)
        self.memory = memory
        manifest = sampled.directory / "manifest.json"
        hashes = {name: _sha(sampled.directory / name) for name in (*sampled._spec, "manifest.json")}
        return {"wall_seconds": time.perf_counter() - started,
                "stages": {"public_template_preparation_seconds": workspace.preparation_seconds,
                           "gram_fft_seconds": workspace.gram_fft_seconds,
                           "template_fft_seconds": workspace.template_fft_seconds,
                           "exact_support_seconds": workspace.support_seconds,
                           "direct_gram_refinement_seconds": workspace.direct_gram_refinement_seconds,
                           "total_preparation_seconds": workspace.preparation_total_seconds},
                "geometry": {"sample_count": sampled.samples, "sample_rate": sampled.sample_rate,
                             "nfft": workspace.nfft, "lags": len(lags), "lag_first_sample": int(lags[0]),
                             "lag_last_sample": int(lags[-1]), "lag_step_samples": step,
                             "group_tile": self.group_tile, "array_bound_bytes": workspace.array_bound,
                             "memory_limit_bytes": memory, "precision": workspace.precision,
                             "device": workspace.device},
                "input_hashes": hashes, "source_hashes": self.source_hashes}

    def score(self, request):
        self._check_sources()
        if self.workspace is None:
            raise ValueError("prepare must be called before score")
        self.sampled.verify_unchanged()
        frequencies = request.get("frequency_offsets_hz", [0.])
        if not isinstance(frequencies, list) or not frequencies or len(frequencies) > 4096:
            raise ValueError("frequency_offsets_hz must be a nonempty list")
        if any(not math.isfinite(float(x)) for x in frequencies) or len(set(map(float, frequencies))) != len(frequencies):
            raise ValueError("frequency offsets must be finite and unique")
        if request.get("drift_offsets_hz_per_second", [0.]) != [0.]:
            raise ValueError("performance adapter supports CFO-only search with zero drift")
        full = request.get("full_group_count_for_reporting")
        if isinstance(full, bool) or not isinstance(full, int) or full < len(frequencies) * len(self.workspace.lags):
            raise ValueError("full_group_count_for_reporting must cover the measured bank")
        fftmod = self.modules["triad_fft_acquire"]
        group = self.modules["triad_group_acquire"]
        config = group.AcquisitionConfig(doppler_width_hz=0., window_blocks=1, reblock=1,
            reference_variance=float(request.get("reference_variance", 1.)),
            stationary_power=tuple(request.get("stationary_power", [.5, .5])),
            group_tile=request.get("group_tile", self.group_tile), memory_limit_bytes=self.memory,
            device=self.workspace.device, precision=self.workspace.precision,
            known_noise_variance=request.get("known_noise_variance"),
            frequency_offsets_hz=tuple(map(float, frequencies)))
        started = time.perf_counter()
        result = fftmod.score_workspace(self.workspace, config, drift_offsets=(0.,), retain_group_scores=False)
        # Include wrapper-side result normalization and all post-return work in measured wall.
        geometry = dict(result["geometry"])
        # A million-entry lattice remains resident in the receiver but is not copied
        # into the per-repetition protocol response.
        geometry.pop("lag_samples", None)
        geometry = _jsonable(geometry)
        timing = _jsonable(result["timing"])
        arithmetic = _jsonable(result["arithmetic"])
        refinement = _jsonable(result["numerical_refinement"])
        resources = _jsonable(result["resources"])
        bit_ll = _jsonable(result["bit_log_likelihoods"])
        strict = _jsonable(result["strict"])
        relaxed = _jsonable(result["relaxed"])
        rank_histogram = _jsonable(result["calibration"]["rank_histogram"])
        finished = time.perf_counter()
        self.sampled.verify_unchanged()
        self._check_sources()
        return {"performance_only": True, "wall_seconds_including_finish": finished - started,
                "receiver_seconds": timing.get("total_receiver_seconds"),
                "timing": timing, "geometry": geometry,
                "arithmetic": arithmetic, "numerical_refinement": refinement,
                "resources": resources, "bit_log_likelihoods": bit_ll,
                "strict": strict, "relaxed": relaxed,
                "rank_histogram": rank_histogram,
                "measured_group_count": geometry["group_count"],
                "full_group_count_for_reporting": full,
                "admission_interpretation": "invalid: residual calibration and group allocation use only the measured CFO subset"}

    def gaussian(self, request):
        self._check_sources()
        blocks = request.get("blocks", 8)
        candidates = request.get("candidates", 64)
        taps = request.get("taps", 2)
        window_blocks = request.get("window_blocks", 8)
        if any(isinstance(v, bool) or not isinstance(v, int) for v in (blocks, candidates, taps, window_blocks)):
            raise ValueError("Gaussian dimensions must be integers")
        if not 1 <= blocks <= 4096 or not 1 <= candidates <= 256 or not 1 <= taps <= 16 or not 1 <= window_blocks <= blocks:
            raise ValueError("Gaussian fixture dimensions out of supported range")
        precision, device = request.get("precision", "fp32"), request.get("device", "cpu")
        seed = request.get("seed", 12345)
        if isinstance(seed, bool) or not isinstance(seed, int):
            raise ValueError("seed must be integer")
        memory = int(float(request.get("memory_mib", 256)) * 1024 * 1024)
        if memory < 1:
            raise ValueError("memory_mib must be positive")
        self.workspace = self.sampled = None
        condition = request.get("condition", "ordinary")
        if condition not in ("ordinary", "strong", "near-singular"):
            raise ValueError("unknown Gaussian qualification condition")
        # Charge generator intermediates, then release A/observations before the
        # plan. The remaining public inputs stay charged alongside its arrays.
        fixture_bound = (4 * blocks * candidates * 4 * taps * 16 +
                         blocks * candidates * taps * taps * 16 +
                         2 * blocks * candidates * taps * 16 + blocks * 4 * 16)
        if fixture_bound >= memory:
            raise MemoryError("algebraic fixture and Gram exceed requested memory budget")
        rng = np.random.default_rng(seed)
        t0 = time.perf_counter()
        shape = (blocks, candidates, 4, taps)
        A = (rng.standard_normal(shape) + 1j*rng.standard_normal(shape)) / math.sqrt(8.)
        observations = (rng.standard_normal((blocks, 4)) + 1j*rng.standard_normal((blocks, 4))) / math.sqrt(2.)
        if condition == "strong":
            observations *= 1e6
        elif condition == "near-singular" and taps >= 2:
            A[..., 1] = A[..., 0] + 1e-6 * A[..., 1]
        grams = np.einsum("bcnk,bcnl->bckl", A.conj(), A, optimize=True)
        correlations = np.einsum("bcnk,bn->bck", A.conj(), observations, optimize=True)[:, None]
        del A, observations
        resident_input_bytes = grams.nbytes + correlations.nbytes + blocks * 8
        fixture_seconds = time.perf_counter() - t0
        t0 = time.perf_counter()
        plan = self.modules["triad_gaussian_window"].PreparedWindowedGaussian(
            grams, np.arange(blocks, dtype=np.float64) * .4,
            doppler_width_hz=float(request.get("doppler_width_hz", 0.)),
            window_blocks=window_blocks, stationary_power=tuple([1. / taps] * taps),
            memory_limit_bytes=memory - resident_input_bytes)
        preparation_seconds = time.perf_counter() - t0
        t0 = time.perf_counter()
        score = plan.score(correlations, variance_scale=1., device=device, precision=precision)
        if device.startswith("cuda"):
            self.torch.cuda.synchronize(device)
        scores = score.detach().cpu().numpy().reshape(-1)
        scoring_seconds = time.perf_counter() - t0
        self._check_sources()
        if len(scores) <= 256:
            retained = scores.tolist()
        else:
            retained = None
        return {"performance_only": True, "fixture_kind": "deterministic algebraic PSD/shared-complex observation; not sampled propagation",
                "seed": seed, "condition": condition,
                "resources": {"fixture_peak_array_bound_bytes": fixture_bound,
                              "resident_input_bytes": resident_input_bytes,
                              "total_memory_limit_bytes": memory}, "shape": {"blocks": blocks, "candidates": candidates, "taps": taps, "window_blocks": window_blocks},
                "precision": precision, "device": device, "fixture_seconds": fixture_seconds,
                "preparation_seconds": preparation_seconds, "scoring_seconds": scoring_seconds,
                "checksum": float(np.sum(scores, dtype=np.float64)),
                "score_summary": {"count": len(scores), "min": float(np.min(scores)), "max": float(np.max(scores)),
                                  "mean": float(np.mean(scores)), "scores": retained},
                "diagnostics": _jsonable(plan.diagnostics())}

    def dispatch(self, request):
        self._check_sources()
        action = request.get("action")
        if action == "hello":
            return self.hello()
        if action == "prepare":
            return self.prepare(request)
        if action == "score":
            return self.score(request)
        if action == "gaussian":
            return self.gaussian(request)
        if action == "close":
            self.workspace = self.sampled = None
            return {"closed": True}
        raise ValueError("unknown action")


def serve(args):
    started = time.perf_counter()
    service = ReceiverService(args.source_root, args.threads)
    service.startup_seconds = time.perf_counter() - started
    for line in sys.stdin:
        request = None
        try:
            request = json.loads(line)
            if not isinstance(request, dict):
                raise ValueError("request must be a JSON object")
            result = service.dispatch(request)
            response = {"ok": True, "result": result}
        except Exception as error:
            response = {"ok": False, "error": f"{type(error).__name__}: {error}"}
        sys.stdout.write(json.dumps(response, separators=(",", ":"), allow_nan=False) + "\n")
        sys.stdout.flush()
        if isinstance(request, dict) and request.get("action") == "close":
            break


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serve", action="store_true", required=True)
    parser.add_argument("--source-root", required=True)
    parser.add_argument("--threads", type=int, default=1)
    args = parser.parse_args(argv)
    if args.threads < 1:
        parser.error("--threads must be positive")
    serve(args)


if __name__ == "__main__":
    main()
