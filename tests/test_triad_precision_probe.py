#!/usr/bin/env python3
"""Focused CPU checks for sampled-waveform precision experiments."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("triad_precision_probe_test", ROOT / "tools/triad_precision_probe.py")
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


def require(value, message):
    if not value:
        raise AssertionError(message)


def fixture(directory: Path):
    samples = 96
    basis = np.zeros((2, samples, 1), dtype=np.complex64)
    basis[0, :48, 0] = 1 / np.sqrt(48)
    basis[1, 48:, 0] = 1 / np.sqrt(48)
    signal = np.array(basis[0, :, 0], dtype=np.complex64)
    basis.tofile(directory / "basis.c64")
    signal.tofile(directory / "signal.c64")
    manifest = {"format": "triad-long-basis-v1", "basis_shape": [2, samples, 1],
        "signal_shape": [samples], "sample_rate": 64., "noise_reference_power": 1.,
        "cn0_db_hz": 16., "bitlabels": [0, 1], "candidateparams": [{}, {}],
        "true_candidate_index": 0, "signal_bit": 0, "basis_file": "basis.c64", "signal_file": "signal.c64"}
    (directory / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")


def main():
    generator = torch.Generator().manual_seed(4107)
    q = torch.randn((3, 127), generator=generator) + 1j * torch.randn((3, 127), generator=generator)
    y = torch.randn((127, 5), generator=generator) + 1j * torch.randn((127, 5), generator=generator)
    q = (q / torch.linalg.vector_norm(q, dim=-1, keepdim=True)).to(torch.complex64)
    y = y.to(torch.complex64)
    expected = q.conj() @ y
    actual = probe._real_decomposed(q.conj().contiguous(), y)
    require(torch.allclose(actual, expected, rtol=2e-5, atol=2e-5),
            "strict real-plane products disagree with direct complex products")

    raw_q = q.reshape(3, 127, 1).to(torch.complex128)
    samples = y.T.to(torch.complex128).contiguous()
    energy, _ = probe.score(raw_q, samples, "strict-fp64-reference", torch.device("cpu"))
    direct = (q.conj() @ y).abs().square().T.to(torch.float64)
    require(torch.allclose(energy, direct, rtol=2e-6, atol=2e-5),
            "FP64 score layout does not map [candidate,column,batch] consistently")

    obs = torch.tensor([[.1234567+0.7654321j, -.2345678+.3456789j]], dtype=torch.complex128)
    qbank = torch.ones((1, 2, 1), dtype=torch.complex128) / (2 ** .5)
    baseline, _ = probe.score(qbank, obs, "real-fp32-strict", torch.device("cpu"))
    quantized, detail = probe.score(qbank, obs, "bf16-observation-storage", torch.device("cpu"))
    require(detail["input_observation_nonlinearly_quantized"],
            "quantized observation mode did not declare the nonlinear input transformation")
    require(not torch.equal(baseline, quantized), "observation storage quantization did not change sample values")
    represented_q, represented_y = probe._represented_operands(qbank, obs, "bf16-observation-storage", torch.device("cpu"))
    same_rep = probe._reference_scores(represented_q, represented_y, 1, 1)
    original_q, original_y = probe._represented_operands(qbank, obs, "strict-fp64-reference", torch.device("cpu"))
    original = probe._reference_scores(original_q, original_y, 1, 1)
    require(float((same_rep-original).abs().max()) > 0,
            "FP64 reference failed to expose observation representation loss")
    total = quantized-original
    arithmetic = quantized-same_rep
    representation = same_rep-original
    require(torch.allclose(total, arithmetic+representation, atol=1e-12, rtol=1e-12),
            "arithmetic and represented-input effects do not compose against the original reference")
    _, fp32_output_cpu = probe.score(qbank, obs, "fp16-products-fp32-output-attempt", torch.device("cpu"))
    require(fp32_output_cpu["status"] == "unsupported",
            "low-input FP32-output attempt silently substituted CPU arithmetic")
    _, unsupported_tf32 = probe.score(qbank, obs, "tf32-requested", torch.device("cpu"))
    require(unsupported_tf32["status"] == "unsupported", "CPU TF32 request was silently run as FP32")
    _, unsupported_fp8 = probe.score(qbank, obs, "fp8-e4m3-attempt", torch.device("cpu"))
    require(unsupported_fp8["status"] == "unsupported", "CPU FP8 request was silently run as a different mode")

    with tempfile.TemporaryDirectory(prefix="triad-precision-test-") as temporary:
        directory = Path(temporary)
        fixture(directory)
        args = probe.build_parser().parse_args([str(directory), "--device", "cpu", "--trials", "12",
            "--seed", "1234", "--cn0-grid", "14,18", "--modes",
            "strict-fp64-reference,complex64-strict,real-fp32-strict,fp16-template-storage,fp16-observation-storage",
            "--warmup", "0"])
        args.modes = [x.strip() for x in args.modes.split(",")]
        args.cn0_grid = [float(x) for x in args.cn0_grid.split(",")]
        result = probe.evaluate(args)
        require(result["status"] == "screening_only", "probe claims operational validation")
        require(result["reference"]["waveform_independence"].startswith("not an independent waveform"),
                "exported complex64 samples were mislabeled as an independent waveform reference")
        require(len(result["results"]) == 10, "SNR/mode matrix is incomplete")
        ref = [x for x in result["results"] if x["mode"] == "strict-fp64-reference"]
        require(all(x["metrics"]["gate_validity"]["status"] == "analytic_partial_family_only" for x in ref),
                "strict sampled Gaussian reference failed to identify its partial-family gate")
        require(all(x["metrics"]["calibrated_counts"] == {"correct": None, "wrong": None, "missed": None, "null": None} for x in ref),
                "screening counts were mislabeled as calibrated operational counts")
        obs_mode = [x for x in result["results"] if x["mode"] == "fp16-observation-storage"]
        require(all(x["metrics"]["gate_validity"]["status"] == "invalid_for_this_precision_mode" for x in obs_mode),
                "quantized observations were incorrectly assigned a Gaussian analytic threshold")
        require(all(x["metrics"]["maximum_representation_score_change_vs_original_inputs"] > 0 for x in obs_mode),
                "observation quantization loss against original samples is missing")
        template_mode = [x for x in result["results"] if x["mode"] == "fp16-template-storage"]
        require(all(x["metrics"]["maximum_representation_score_change_vs_original_inputs"] > 0 and
                    x["metrics"]["maximum_arithmetic_score_error_same_represented_inputs"] <
                    x["metrics"]["maximum_total_score_error_vs_original_fp64_input_reference"]
                    for x in template_mode),
                "template representation loss was not separated from arithmetic error")
        require(all(x["timing"]["paired_batch_inference_wall_seconds"] >= 0 for x in result["results"]),
                "timing record omitted paired inference")
        require(result["timing"]["sum_of_measured_substages_seconds"] >= result["timing"]["paired_sample_noise_generation_seconds_sum_over_cn0"],
                "substage total omitted RNG/simulation work")
        require(result["timing"]["complete_evaluate_wall_seconds"] > 0,
                "complete evaluate wall time was not measured")
        args.trials = 10000
        args.memory_gib = .0001
        try:
            probe.evaluate(args)
        except ValueError as error:
            require("exceeds --memory-gib" in str(error), "memory cap failed for an unexpected reason")
        else:
            raise AssertionError("large trial stack was not rejected before allocation")
        args.trials = 12
        args.memory_gib = 2.
        args.horizon_seconds = float("nan")
        try:
            probe.evaluate(args)
        except ValueError as error:
            require("spacing/count" in str(error), "nonfinite horizon failed for an unexpected reason")
        else:
            raise AssertionError("nonfinite horizon was accepted")
        args.horizon_seconds = 86400.
        args.warmup = -1
        try:
            probe.evaluate(args)
        except ValueError as error:
            require("warmup" in str(error), "negative warmup failed for an unexpected reason")
        else:
            raise AssertionError("negative warmup was accepted")
    require(probe._git_revision(Path(tempfile.gettempdir())) is None,
            "unpacked source without Git metadata did not record a null revision")

    print(json.dumps({"status": "passed", "checks": ["complex-vs-real", "candidate-batch-order",
        "observation-quantization-reference-ladder", "unsupported-mode-reporting", "paired-snr-matrix",
        "gate-validity", "memory-preflight", "unpacked-source-metadata"]}))


if __name__ == "__main__":
    main()
