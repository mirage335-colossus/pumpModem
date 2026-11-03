import math
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from triad_perf_conditional import (
    ConditionalModelError,
    build_conditional_contract,
    check_conditional_sentinel,
    conditional_comparison,
    conditional_model_log_bounds,
)


def build(*, errors=None, training=None, anchors=None, training_ids=None,
          maximum_model_error=.20):
    errors = errors or {
        "reference": [.01, -.01, 0.],
        "candidate": [.01, 0., -.01],
        "paired": [0., .01, -.01],
    }
    training = training or {
        "reference": [10., 10.001, 9.999],
        "candidate": [20., 20.001, 19.999],
    }
    anchors = anchors or ["anchor-0", "anchor-1", "anchor-2"]
    training_ids = training_ids or ["train-0", "train-1", "train-2"]
    return build_conditional_contract(
        errors, training, applicability_key="signature-a",
        historical_anchor_ids=anchors, training_pair_ids=training_ids,
        maximum_model_error=maximum_model_error)


class ConditionalModelTests(unittest.TestCase):
    def test_ready_plan_freezes_history_forecast_and_separate_uncertainty(self):
        plan = build()
        self.assertEqual(plan["status"], "conditional_contract_ready")
        self.assertIn("plan_sha256", plan)
        self.assertAlmostEqual(plan["model_log_error_envelopes"]["reference"],
                               math.log1p(.10))
        self.assertEqual(plan["schema"], "triad-performance-conditional-v2")
        self.assertEqual(plan["paired_model_log_envelope"],
                         plan["model_log_error_envelopes"]["paired"])
        self.assertGreater(plan["training_forecasts"]["reference"]
                           ["timing_mean_confidence_interval_seconds"][1],
                           plan["training_forecasts"]["reference"]["point_seconds"])
        self.assertFalse(plan["future_coverage_qualified"])
        self.assertTrue(plan["timing_and_model_uncertainty_separate"])

    def test_historical_individual_scatter_does_not_inflate_mean_error_envelope(self):
        values = [.14 if i % 2 == 0 else -.14 for i in range(17)]
        values[-1] = 0.
        errors = {"reference": values, "candidate": values, "paired": values}
        plan = build(errors=errors,
                     anchors=[f"a-{i}" for i in range(17)],
                     training_ids=["t-0", "t-1", "t-2"])
        self.assertEqual(plan["status"], "conditional_contract_ready")
        self.assertAlmostEqual(plan["model_log_error_envelopes"]["reference"], math.log1p(.10))
        self.assertGreater(plan["sentinel_observation_log_envelopes"]["reference"],
                           plan["model_log_error_envelopes"]["reference"])
        self.assertLessEqual(max(plan["model_log_error_envelopes"].values()), math.log1p(.20))

    def test_more_training_repeats_do_not_absorb_historical_observation_scatter(self):
        values = [.14 if i % 2 == 0 else -.14 for i in range(17)]
        values[-1] = 0.
        errors = {"reference": values, "candidate": values, "paired": values}
        anchors = [f"a-{i}" for i in range(17)]
        first = build(errors=errors, anchors=anchors,
                      training={"reference": [10., 10.001, 9.999],
                                "candidate": [20., 20.001, 19.999]},
                      training_ids=["t0", "t1", "t2"])
        later = build(errors=errors, anchors=anchors,
                      training={"reference": [10., 10.001, 9.999, 10.0005,
                                              9.9995, 10.002, 9.998, 10.0015, 9.9985],
                                "candidate": [20., 20.002, 19.998, 20.001,
                                              19.999, 20.004, 19.996, 20.003, 19.997]},
                      training_ids=[f"t{i}" for i in range(9)])
        self.assertEqual(first["model_log_error_envelopes"],
                         later["model_log_error_envelopes"])
        self.assertEqual(first["sentinel_observation_log_envelopes"],
                         later["sentinel_observation_log_envelopes"])

    def test_matching_full_endpoint_sentinel_supports_conditional_gate(self):
        plan = build()
        before = deepcopy(plan["model_log_error_envelopes"])
        sentinel = check_conditional_sentinel(
            plan, {"reference": 10.5, "candidate": 21.},
            applicability_key="signature-a", sentinel_id="sentinel-0")
        self.assertEqual(sentinel["status"], "historically_supported_conditional")
        self.assertTrue(sentinel["conditional_performance_pass_eligible"])
        self.assertFalse(sentinel["sentinel_changes_model_envelope"])
        self.assertEqual(sentinel["model_log_error_envelopes"], before)
        bounds = conditional_model_log_bounds(plan, sentinel)
        self.assertTrue(bounds["eligible"])
        self.assertFalse(bounds["training_timing_uncertainty_included"])
        self.assertEqual(bounds["model_log_error_bounds"], before)
        passing = conditional_comparison([1.05, 1.06], bounds)
        self.assertEqual(passing["gate"], "pass")
        self.assertFalse(passing["future_coverage_qualified"])
        self.assertFalse(passing["training_timing_uncertainty_included"])

    def test_large_full_only_slowdown_requests_focused_qualification(self):
        plan = build()
        sentinel = check_conditional_sentinel(
            plan, {"reference": 10., "candidate": 26.},
            applicability_key="signature-a", sentinel_id="sentinel-slow")
        self.assertEqual(sentinel["status"], "unvalidated_request_focused_qualification")
        self.assertTrue(sentinel["focused_qualification_required"])
        self.assertEqual(sentinel["checks"]["candidate"]["classification"],
                         "outside_frozen_envelope")
        bounds = conditional_model_log_bounds(plan, sentinel)
        self.assertFalse(bounds["eligible"])
        gate = conditional_comparison([1., 1.], bounds)
        self.assertEqual(gate["gate"], "inconclusive")
        self.assertFalse(gate["conditional_performance_pass_eligible"])

    def test_regression_gate_uses_timing_interval_and_history_envelope_once(self):
        plan = build()
        sentinel = check_conditional_sentinel(
            plan, {"reference": 10., "candidate": 20.},
            applicability_key="signature-a", sentinel_id="sentinel-good")
        bounds = conditional_model_log_bounds(plan, sentinel)
        no_change = conditional_comparison([1.0, 1.02], bounds)
        clear_regression = conditional_comparison([1.5, 1.52], bounds)
        self.assertEqual(no_change["gate"], "pass")
        self.assertEqual(clear_regression["gate"], "performance_regression")
        self.assertEqual(clear_regression["paired_model_log_error_bound"],
                         plan["paired_model_log_envelope"])
        self.assertFalse(clear_regression["training_timing_uncertainty_included"])

    def test_training_timing_uncertainty_yields_inconclusive_not_wider_model(self):
        training = {"reference": [5., 20., 5.], "candidate": [10., 40., 10.]}
        plan = build(training=training)
        sentinel = check_conditional_sentinel(
            plan, {"reference": 10., "candidate": 20.},
            applicability_key="signature-a", sentinel_id="sentinel-noisy")
        self.assertEqual(sentinel["status"], "inconclusive")
        self.assertTrue(any(row["mean_model_plus_timing_uncertainty_exceeds_maximum"]
                            for row in sentinel["checks"].values()))
        self.assertEqual(plan["model_log_error_envelopes"],
                         build()["model_log_error_envelopes"])

    def test_history_bias_or_excess_scatter_disables_conditional_reuse(self):
        biased = {"reference": [math.log(1.25)] * 3,
                  "candidate": [0.] * 3, "paired": [0.] * 3}
        plan = build(errors=biased)
        self.assertEqual(plan["status"], "historical_model_invalid")
        sentinel = check_conditional_sentinel(
            plan, {"reference": 10., "candidate": 20.},
            applicability_key="signature-a", sentinel_id="sentinel-history")
        self.assertEqual(sentinel["status"], "unvalidated_request_focused_qualification")

        too_wide = {"reference": [.3, -.3, 0.],
                    "candidate": [0., 0., 0.], "paired": [0., 0., 0.]}
        plan = build(errors=too_wide)
        self.assertIn(plan["status"], ("historical_model_inconclusive",
                                       "historical_model_error_exceeds_maximum"))

    def test_wrong_applicability_nonindependent_sentinel_and_tampering_fail_closed(self):
        plan = build()
        wrong = check_conditional_sentinel(
            plan, {"reference": 10., "candidate": 20.},
            applicability_key="signature-b", sentinel_id="sentinel-0")
        self.assertEqual(wrong["status"], "unvalidated_request_focused_qualification")
        reused = check_conditional_sentinel(
            plan, {"reference": 10., "candidate": 20.},
            applicability_key="signature-a", sentinel_id="anchor-0")
        self.assertEqual(reused["status"], "unvalidated_request_focused_qualification")
        changed = deepcopy(plan)
        changed["model_log_error_envelopes"]["paired"] *= .5
        tampered = check_conditional_sentinel(
            changed, {"reference": 10., "candidate": 20.},
            applicability_key="signature-a", sentinel_id="sentinel-0")
        self.assertEqual(tampered["status"], "unvalidated_request_focused_qualification")
        self.assertFalse(tampered["frozen_contract_verified"])

    def test_old_schema_is_rejected_even_with_a_matching_recomputed_hash(self):
        plan = deepcopy(build())
        plan["schema"] = "triad-performance-conditional-v1"
        core = {key: value for key, value in plan.items() if key != "plan_sha256"}
        plan["plan_sha256"] = hashlib.sha256(
            json.dumps(core, sort_keys=True, separators=(",", ":"), allow_nan=False)
            .encode()).hexdigest()
        sentinel = check_conditional_sentinel(
            plan, {"reference": 10., "candidate": 20.},
            applicability_key="signature-a", sentinel_id="sentinel-v1")
        self.assertEqual(sentinel["status"], "unvalidated_request_focused_qualification")
        self.assertFalse(sentinel["frozen_contract_verified"])
        bounds = conditional_model_log_bounds(plan, sentinel)
        self.assertFalse(bounds["eligible"])

    def test_duplicate_history_or_training_ids_rejected(self):
        with self.assertRaisesRegex(ConditionalModelError, "unique"):
            build(anchors=["same", "same", "a-2"])
        with self.assertRaisesRegex(ConditionalModelError, "unique"):
            build(training_ids=["same", "same", "t-2"])
        with self.assertRaisesRegex(ConditionalModelError, "independent"):
            build(training_ids=["anchor-0", "t-1", "t-2"])


if __name__ == "__main__":
    unittest.main()

