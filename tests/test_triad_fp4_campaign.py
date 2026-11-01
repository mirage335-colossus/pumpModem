"""Focused checks for exhaustive sampled FP4 campaign orchestration."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock
import sys

import numpy as np
import torch


SCRIPT = Path(__file__).parents[1] / "tools" / "triad-fp4-campaign.py"
sys.path.insert(0, str(SCRIPT.parent))
SPEC = importlib.util.spec_from_file_location("triad_fp4_campaign", SCRIPT)
campaign = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(campaign)


def _capture(tmp_path):
    rng = np.random.default_rng(83)
    candidates, samples, taps = 4, 12, 2
    basis = (rng.normal(size=(candidates, samples, taps)) +
             1j*rng.normal(size=(candidates, samples, taps))).astype(np.complex128)
    observation = (rng.normal(size=samples+2) + 1j*rng.normal(size=samples+2)).astype(np.complex128)
    basis.tofile(tmp_path / "basis.c128")
    observation.tofile(tmp_path / "observation.c128")
    np.ones(samples + 2, dtype=np.uint8).tofile(tmp_path / "validity.u8")
    manifest = dict(format=campaign.FORMAT, complex_type="complex128", byte_order="little",
                    candidate_count=candidates, sample_count=samples+2,
                    basis_shape=[candidates, samples, taps], observation_shape=[samples+2],
                    template_sample_count=samples, observation_sample_count=samples+2,
                    tap_count=taps, validity_file="validity.u8",
                    sample_rate=8., noise_variance_pre_frontend=.4,
                    candidates=[dict(bit=i//2, epoch=i % 2) for i in range(candidates)],
                    truth=dict(bit=1), time_origin_seconds=3600.)
    (tmp_path / "manifest.json").write_text(json.dumps(manifest))
    return basis, observation, manifest


def _args():
    return SimpleNamespace(sample_rate=8., cfo_hz=[-.1, .1], lag_start=0, lag_stop=2,
                            lag_step=2, block_samples=4, device="cpu", candidate_tile=2,
                            frequency_tile=1, column_tile=3, template_cache_mib=8,
                            max_block_output_mib=1, phase_states=8,
                            relative_phase_states=2, residual_frequency_bound=.001,
                            residual_frequency_std=.0001, frequency_correlation=100.,
                            phase_diffusion=.001, save_scores=True)


class CampaignTests(unittest.TestCase):
  def test_generation_binding_requires_complete_successful_payload_hashes(self):
    with tempfile.TemporaryDirectory() as temporary:
      directory = Path(temporary) / "capture"
      directory.mkdir()
      path = directory.parent / "capture.run.json"
      hashes = {"manifest.json": "a", "basis.c128": "b", "observation.c128": "c", "validity.u8": "d"}
      record = dict(returncode=0, manifest_sha256="a", exported_file_hashes=dict(list(hashes.items())[1:]))
      path.write_text(json.dumps(record))
      self.assertEqual(campaign.generation_provenance(directory, hashes)["status"], "verified payload binding")
      del record["exported_file_hashes"]["observation.c128"]
      path.write_text(json.dumps(record))
      with self.assertRaisesRegex(ValueError, "payload hashes"):
        campaign.generation_provenance(directory, hashes)
      record["returncode"] = 1
      path.write_text(json.dumps(record))
      with self.assertRaisesRegex(ValueError, "successful generation"):
        campaign.generation_provenance(directory, hashes)

  def test_consumer_generation_schema_retains_exact_payload_binding(self):
    with tempfile.TemporaryDirectory() as temporary:
      directory = Path(temporary) / "capture"
      directory.mkdir()
      path = directory.parent / "capture.run.json"
      hashes = {"manifest.json": "a", "basis.c128": "b", "observation.c128": "c", "validity.u8": "d"}
      record = dict(format="consumer-gpu-input-v1", status="complete", returncode=0,
                    input_file_hashes=hashes.copy())
      path.write_text(json.dumps(record))
      self.assertEqual(campaign.generation_provenance(directory, hashes)["status"], "verified payload binding")
      record["input_file_hashes"]["observation.c128"] = "wrong"
      path.write_text(json.dumps(record))
      with self.assertRaisesRegex(ValueError, "payload hashes"):
        campaign.generation_provenance(directory, hashes)
      record["input_file_hashes"] = hashes.copy()
      del record["input_file_hashes"]["manifest.json"]
      path.write_text(json.dumps(record))
      with self.assertRaisesRegex(ValueError, "payload hashes"):
        campaign.generation_provenance(directory, hashes)

  def test_campaign_keeps_every_cfo_candidate_lag_and_runs_full_fp64_refinement(self):
    with tempfile.TemporaryDirectory() as temporary:
      tmp_path = Path(temporary)
      basis, observation, manifest = _capture(tmp_path)
      capture = campaign.load_capture(tmp_path)
      runs, _, verification = campaign.score_capture(capture, _args(), precision="fp32",
                                                      inference_precision="fp64", repeats=2,
                                                      strict_all=True)

    # The extra final pass is a whole-family FP64 rerun. Intermediate repeat
    # scans reuse exactly the same observation and are timing samples only.
      self.assertEqual([row["precision"] for row in runs], ["fp32", "fp32", "fp64"])
      self.assertEqual(runs[-1]["score_matrix"].shape, (1, 2, 4, 2))
      self.assertGreater(runs[1]["bank"]["a_cache_hits"], 0)
      self.assertTrue(all(row["bank"]["matmul_calls"] > 0 for row in runs))
      self.assertEqual(len(verification), 2)
      self.assertEqual(verification[-1]["compared_values"], 16)

      expected_hypotheses = len(manifest["candidates"]) * len(_args().cfo_hz) * 2
      self.assertEqual(expected_hypotheses, 16)
    # Compare downstream scores after an independent direct complex128
    # calculation, including every frequency, candidate, lag and tap axis.
      direct = campaign.direct_fp64_reference(basis, observation[None, :], 8.,
                                               [-.1, .1], [0, 2], 4, .4,
                                               time_origin_seconds=3600.)
      receiver = campaign._new_receiver(4, 4, 2, torch.device("cpu"), "fp64", "reference", _args())
      for block_index, (r, gram) in enumerate(direct):
          direct_r = torch.as_tensor(r, dtype=torch.complex128)
          direct_r = direct_r.permute(0, 1, 3, 2, 4).reshape(4, 4, 2)
          receiver.update(direct_r, torch.as_tensor(gram), 0. if block_index == 0 else .5)
      direct_scores = receiver.scores().reshape(1, 2, 2, 4).permute(0, 1, 3, 2).detach().numpy()
      np.testing.assert_allclose(runs[-1]["score_matrix"], direct_scores, rtol=1e-12, atol=1e-12)


  def test_input_rejects_unbalanced_candidate_mixture_and_short_support(self):
    with tempfile.TemporaryDirectory() as temporary:
      tmp_path = Path(temporary)
      _capture(tmp_path)
      manifest_path = tmp_path / "manifest.json"
      manifest = json.loads(manifest_path.read_text())
      manifest["candidates"][-1]["bit"] = 0
      manifest_path.write_text(json.dumps(manifest))
      with self.assertRaisesRegex(ValueError, "equal size"):
          campaign.load_capture(tmp_path)

      manifest["candidates"][-1]["bit"] = 1
      manifest_path.write_text(json.dumps(manifest))
      capture = campaign.load_capture(tmp_path)
      args = _args()
      args.lag_stop = 4
      with self.assertRaisesRegex(ValueError, "support leaves"):
          campaign.score_capture(capture, args, precision="fp64", repeats=1)

  def test_zero_suffix_trim_is_explicit_and_exact(self):
    with tempfile.TemporaryDirectory() as temporary:
      tmp_path = Path(temporary)
      basis, _, manifest = _capture(tmp_path)
      basis[:, -2:, :] = 0
      basis.tofile(tmp_path / "basis.c128")
      capture = campaign.load_capture(tmp_path, trim_zero_suffix=True)
      self.assertEqual(capture["original_template_samples"], manifest["basis_shape"][1])
      self.assertEqual(capture["template_samples"], manifest["basis_shape"][1] - 2)
      self.assertEqual(capture["trimmed_template_samples"], 2)
      self.assertTrue(np.all(capture["basis"][:, -1] != 0))

  def test_repeated_observation_batch_is_marked_non_independent(self):
    with tempfile.TemporaryDirectory() as temporary:
      tmp_path = Path(temporary)
      _capture(tmp_path)
      capture = campaign.load_capture(tmp_path)
      args = _args()
      args.repeat_observations = 3
      runs, _, _ = campaign.score_capture(capture, args, precision="fp64", repeats=1)
      self.assertEqual(runs[0]["timing_observation_copies"], 3)
      self.assertFalse(runs[0]["timing_copies_are_independent_trials"])
      self.assertEqual(runs[0]["exhaustive_hypotheses_processed"], 16 * 3)
      self.assertEqual(len(runs[0]["bit_scores"]), 3)
      self.assertIsNone(runs[0]["single_capture_latency_seconds"])
      self.assertGreater(runs[0]["batch_latency_seconds"], 0)

  def test_mixed_tap_groups_preserve_each_candidate_gain_prior(self):
    with tempfile.TemporaryDirectory() as temporary:
      path = Path(temporary)
      basis, observation, manifest = _capture(path)
      for index, count in enumerate((1, 2, 1, 2)):
        manifest["candidates"][index]["tap_count"] = count
        basis[index, :, count:] = 0
      basis.tofile(path / "basis.c128")
      (path / "manifest.json").write_text(json.dumps(manifest))
      capture = campaign.load_capture(path)
      runs, _, _ = campaign.score_capture(capture, _args(), "fp64", repeats=2)
      direct = campaign.direct_fp64_reference(basis, observation[None], 8., [-.1, .1], [0, 2], 4, .4,
                                              time_origin_seconds=3600.)
      expected = []
      for candidate, count in enumerate((1, 2, 1, 2)):
        receiver = campaign._new_receiver(4, 1, count, torch.device("cpu"), "fp64", "reference", _args())
        for block, (r, gram) in enumerate(direct):
          row = torch.tensor(r[:, :, candidate:candidate+1, :, :count]).permute(0, 1, 3, 2, 4).reshape(4, 1, count)
          receiver.update(row, torch.tensor(gram[candidate:candidate+1, :count, :count]), 0. if block == 0 else .5)
        expected.append(receiver.scores().reshape(1, 2, 2).numpy())
      expected = np.stack(expected, axis=2)
      np.testing.assert_allclose(runs[0]["score_matrix"], expected, rtol=1e-12, atol=1e-12)
      np.testing.assert_array_equal(runs[0]["score_matrix"], runs[1]["score_matrix"])

  def test_native_factory_matches_actual_backend_and_control_counts(self):
    with tempfile.TemporaryDirectory() as temporary:
      path = Path(temporary)
      _, _, manifest = _capture(path)
      args = campaign.parse_args([str(path), "--output", str(path / "out.json"),
                                 "--sample-rate", "8", "--block-samples", "4", "--cfo-hz", "0",
                                 "--phase-states", "8", "--relative-phase-states", "2", "--repeats", "1"])
      args.precision = "native-fp4"
      with mock.patch.object(campaign, "score_capture", side_effect=ValueError("factory reached scorer")):
        with self.assertRaisesRegex(ValueError, "factory reached scorer"):
          campaign.run(args)
      args.precision = "fp64"
      manifest.update(control="wrong-key", signal_enabled=True, signal_bit_simulation_truth=1)
      (path / "manifest.json").write_text(json.dumps(manifest))
      result = campaign.run(args)
      for decision in result["decisions"]["final"]:
        self.assertIsNone(decision["correct_accepted"])
        self.assertEqual(decision["wrong_key_admitted"], decision["admitted"])

  def test_correlation_only_still_processes_all_hypotheses_without_inference(self):
    with tempfile.TemporaryDirectory() as temporary:
      tmp_path = Path(temporary)
      _capture(tmp_path)
      capture = campaign.load_capture(tmp_path)
      runs, scores, verification = campaign.score_capture(
          capture, _args(), precision="fp64", repeats=1, correlation_only=True)
      self.assertIsNone(scores)
      self.assertIsNone(verification)
      self.assertEqual(runs[0]["block_hypothesis_products"], 16 * 3)
      self.assertTrue(runs[0]["correlation_only"])
      self.assertEqual(runs[0]["inference_implementation"], "none")

  def test_graph_fp64_requires_cuda_and_fp32_state(self):
    with self.assertRaisesRegex(ValueError, "requires CUDA"):
      campaign._new_receiver(1, 2, 1, torch.device("cpu"), "fp32", "graph-fp64", _args())

  def test_consumer_arithmetic_selection_is_explicit(self):
    from triad_tensor_native import TensorBackend
    for precision in ("fp32", "fp64"):
      self.assertIsNone(campaign.make_backend(SimpleNamespace(precision=precision)))
    backend = campaign.make_backend(SimpleNamespace(precision="tensor", tensor_format="fp16",
                                                    tensor_engine="torch"))
    self.assertIsInstance(backend, TensorBackend)
    self.assertEqual(backend.contract_metadata()["multiplication"], "fp16")
    self.assertEqual(backend.contract_metadata()["accumulation"], "fp32")
    with self.assertRaisesRegex(ValueError, "explicit tensor_format"):
      campaign.make_backend(SimpleNamespace(precision="tensor", tensor_format=None))
    diagnostic = campaign.make_backend(SimpleNamespace(precision="diagnostic-fp64",
        tensor_format="bf16", tensor_engine="triton", representation_mode="observation-only"))
    self.assertEqual(diagnostic.native_backend.format, "bf16")
    self.assertEqual(diagnostic.mode, "observation-only")

  def test_turing_default_keeps_complete_eager_receiver(self):
    with mock.patch.object(torch.cuda, "get_device_capability", return_value=(7, 5)):
      self.assertEqual(campaign.default_inference_implementation("cuda:0"), "reference")
    with mock.patch.object(torch.cuda, "get_device_capability", return_value=(8, 9)):
      self.assertEqual(campaign.default_inference_implementation("cuda:0"), "graph-fp64")
    with mock.patch.object(torch.cuda, "get_device_capability", side_effect=AssertionError("CPU queried CUDA")):
      self.assertEqual(campaign.default_inference_implementation("cpu"), "reference")

  def test_remote_source_revision_can_be_supplied_without_git(self):
    with tempfile.TemporaryDirectory() as temporary:
      self.assertEqual(campaign._source_revision(Path(temporary)),
                       "unavailable; source hashes identify files")
      self.assertEqual(campaign._source_revision(Path(temporary), "bundle-abc123"),
                       "bundle-abc123")


  def test_threshold_accounts_full_family_and_repeated_attempts(self):
    gate = campaign._threshold(.01, 43200., 60., 3, 1, 48)
    self.assertEqual(gate["attempts"], 720)
    self.assertEqual(gate["family_size"], 720 * 3 * 48)
    self.assertAlmostEqual(gate["log_likelihood_threshold"], np.log((720 * 3 * 48) / .01))


if __name__ == "__main__":
    unittest.main()

