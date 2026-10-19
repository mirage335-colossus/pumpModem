#!/usr/bin/env python3
"""Emit a private reversible receiver overlay; never edit the input checkout."""
import argparse
import hashlib
import json
from pathlib import Path

BASE_COMMIT = "117538bd7d946de88d481fc8ab82e022d803dd8d"
EXPECTED = {
    "src/pattern_receiver.cpp": "a5860a8130a9e6d851c7b8c68bced746c2c12a9a6ad7c0bc15c8062b6ecd52c8",
    "src/live.cpp": "536c2005424ba5c973d6db15163ef60c6bf2fe8d3983a5046be931805b1bea9a",
    "src/channel.cpp": "73b633723256cf8d8a2bb63a2edb003a94d91b40b2c3fcf7bdb4411ffab5951d",
    "src/live_simulation_clock.hpp": "c212cfe323561c0276ccaf5c594542ea7445b80cda9aace85ca7af1061628faa",
    "src/pattern_code.cpp": "3e837e010ee937136b0cca72e2200b2c3de938bec344e0292478d5e1cde05cd4",
    "src/audio_trace.hpp": "2e476953b87dbb555e5734357a7de68873709070a97986b6b48fa82397bc52d9",
    "tests/test_live.cpp": "4c38f29eff44f89f39170a2408b7d967f30194c32914302cd3b6ac4a88ee91b7",
}

STEP = r'''                ::datapump::diagnostic::raw_chain_score::capture("step",config.stream_epoch,
                    search.input_sample_origin,search.input_sample_origin+track.burst.stream_first_sample,[&](auto& f) {
                    f.add("stream_first_sample",track.burst.stream_first_sample);f.add("stream_first_symbol",track.burst.stream_first_symbol);
                    f.add("stream_phase",track.burst.stream_phase_samples);f.add("phase_lower",track.phase_lower);f.add("phase_upper",track.phase_upper);
                    f.add("anchor_start_bin",track.anchor_start);f.add("anchor_index",track.anchor_index);f.add("stream_index",track.index);
                    f.add("established_before",track.established);f.add("admitted_before",track.admitted);f.add("confirmed_before",track.confirmed);
                    f.add("pending_n",count);f.add("pending_sum",track.pending_score);f.add("chain_score",chain_score);
                    f.add("actually_added",track.established?best.score:chain_score);f.add("best_score",best.score);f.add("alternative_score",best.alternative_score);
                    f.add("bit",best.bit);f.add("cumulative_penalty",track.penalty);f.add("penalty_increment",raw_probe_penalty_increment);
                    f.add("bound",bound);f.add("threshold",threshold());f.add("standalone",standalone);f.add("presence",presence);
                    f.add("trials_before",raw_probe_trials_before);f.add("trials_after",trials);
                    f.add("selected_start",best.first_sample);f.add("selected_end",best.end_sample);f.add("prior_end",raw_probe_prior_end);
                    f.add("observed_begin",observed_begin);f.add("confirmed_end",track.confirmed_end);
                    f.add("chosen_bin",chosen);f.add("low_bin",low);f.add("high_bin",high);f.add("timing_radius",radius);
                    f.add("phase_groups",group_count);f.add("selected_phase_lower",selected.lower);f.add("selected_phase_upper",selected.upper);
                    f.add("carrier_count",templates.size());f.add("other_carrier_jobs",jobs);f.add("prior_carrier",track.frequency);
                    f.add("selected_carrier",selected_frequency);f.add("frequency_hz",best.frequency_hz);
                    const auto prior_bin=raw_probe_prior_end/bin_samples;
                    const auto skipped=static_cast<std::size_t>(prior_bin>chosen?std::min<std::uint64_t>(length,prior_bin-chosen):0);
                    f.add("bin_samples",bin_samples);f.add("length_bins",length);f.add("skipped_bins",skipped);
                    f.add("effective_evidence_count",evidence_count(length-skipped));f.add("sample_fit",sample_fit);f.add("real_rank",real_rank);
                    f.add("noise_condition",noise_condition);f.add("projection_image_ratio",projection_image_ratio);
                    f.add("drift_sections",drift_sections);f.add("differential_window",differential_window);
                    f.add("outer_presence_guard",search.outer_presence_guard);f.add("dsss_factor",config.dsss_factor);
                    f.add("symbol_support",symbol_support);f.add("total_support",track.total_support);
                    f.add("support_role","arbitration support, not admission evidence");
                });
'''
SEED = r'''        ::datapump::diagnostic::raw_chain_score::capture("seed",config.stream_epoch,
            search.input_sample_origin,search.input_sample_origin+track.burst.stream_first_sample,[&](auto& f) {
            f.add("stream_first_sample",track.burst.stream_first_sample);f.add("stream_first_symbol",track.burst.stream_first_symbol);
            f.add("stream_phase",track.burst.stream_phase_samples);f.add("phase_lower",track.phase_lower);f.add("phase_upper",track.phase_upper);
            f.add("anchor_start_bin",track.anchor_start);f.add("anchor_index",track.anchor_index);f.add("stream_index",track.index);
            f.add("established_before",track.established);f.add("admitted_before",track.admitted);f.add("confirmed_before",track.confirmed);
            f.add("pending_n",track.burst.bits.size()-track.confirmed);f.add("pending_sum",track.pending_score);
            f.add("chain_score",chain_score);f.add("best_score",item.score);f.add("alternative_score",item.alternative_score);f.add("bit",item.bit);
            f.add("cumulative_penalty",track.penalty);f.add("threshold",threshold());f.add("trials_after",trials);
            f.add("selected_start",item.first_sample);f.add("selected_end",item.end_sample);f.add("selected_carrier",track.frequency);
            f.add("total_support",track.total_support);f.add("support_role","arbitration support, not admission evidence");
        });
'''

def replacements():
    penalty = "                track.total_score+=best.score;track.pending_score+=track.established?best.score:chain_score;track.penalty+=continuation_penalty(radius);\n"
    bound = "                const auto bound=track.pending_score>count?track.pending_score-count-count*std::log(track.pending_score/count)-track.penalty:0;\n"
    return [
        ('#include "audio_trace.hpp"\n', '#include "audio_trace.hpp"\n#include "raw_chain_score_probe.hpp"\n'),
        ('    void stream_phase(std::uint64_t phase) {\n', '    ~Impl() {::datapump::diagnostic::raw_chain_score::summary(config.stream_epoch,search.input_sample_origin);}\n    void stream_phase(std::uint64_t phase) {\n'),
        ('            while(track.next<=latest_start) {\n', '            while(track.next<=latest_start) {\n                const auto raw_probe_trials_before=trials;\n'),
        ('                append_bit(track.burst.bits,static_cast<std::uint8_t>(best.bit));track.burst.end_sample=best.end_sample;\n',
         '                const auto raw_probe_prior_end=track.burst.end_sample;\n                append_bit(track.burst.bits,static_cast<std::uint8_t>(best.bit));track.burst.end_sample=best.end_sample;\n'),
        (penalty, '                track.total_score+=best.score;track.pending_score+=track.established?best.score:chain_score;\n                const auto raw_probe_penalty_increment=continuation_penalty(radius);track.penalty+=raw_probe_penalty_increment;\n'),
        (bound, bound + STEP),
        ('        else track.pending_score=chain_score;\n', '        else track.pending_score=chain_score;\n' + SEED),
    ]

def transform(source):
    result = source
    edits = replacements()
    for old, new in edits:
        if result.count(old) != 1:
            raise ValueError(f"anchor must match exactly once: {old[:100]!r}")
        result = result.replace(old, new, 1)
    reversed_source = result
    for old, new in reversed(edits):
        if reversed_source.count(new) != 1:
            raise ValueError("reverse anchor is ambiguous or missing")
        reversed_source = reversed_source.replace(new, old, 1)
    if reversed_source != source:
        raise ValueError("overlay is not byte reversible")
    return result, reversed_source

def sha(data):
    return hashlib.sha256(data).hexdigest()

def prepare(source_root, output):
    root, output = source_root.resolve(strict=True), output.resolve()
    source = root / "src/pattern_receiver.cpp"
    if output == root or output == root / "src" or output / "pattern_receiver.cpp" == source:
        raise ValueError("output must be a fresh isolated overlay directory")
    actual = {name: sha((root / name).read_bytes()) for name in EXPECTED}
    if actual != EXPECTED:
        raise ValueError(f"source closure differs from {BASE_COMMIT}: {actual}")
    before = source.read_bytes()
    after, reverse = transform(before.decode("utf-8"))
    header_path = Path(__file__).with_name("raw_chain_score_probe.hpp")
    header = header_path.read_bytes()
    # Exclusive directory creation prevents replacing another prepared result.
    output.mkdir(parents=True, exist_ok=False)
    (output / "pattern_receiver.cpp").write_bytes(after.encode("utf-8"))
    (output / "pattern_receiver.reversed.cpp").write_bytes(reverse.encode("utf-8"))
    (output / header_path.name).write_bytes(header)
    if (output / "pattern_receiver.cpp").read_bytes() != after.encode("utf-8") or \
            (output / "pattern_receiver.reversed.cpp").read_bytes() != before or \
            (output / header_path.name).read_bytes() != header:
        raise ValueError("private output readback differs")
    manifest = {
        "schema": 1, "status": "private_preparation_only", "base_commit": BASE_COMMIT,
        "source_root": str(root), "output_dir": str(output), "source_closure": actual,
        "before_sha256": sha(before), "after_sha256": sha(after.encode()),
        "reverse_sha256": sha(reverse.encode()), "support_sha256": sha(header),
        "tool_sha256": sha(Path(__file__).read_bytes()), "anchors": len(replacements()),
        "environment": "DATAPUMP_RAW_CHAIN_SCORE_DIAGNOSTIC=0|1",
        "record_limit_per_receiver": 256,
        "filter": [[1792403097, 0, 8496], [1792403100, 10009, 16321]],
        "summary_boundary": "Impl destructor while component audio trace Scope is alive",
        "clock_scope": "4 explicit clock-read attempts per matching probe; trace adds one per probe/summary; excludes filter/bookkeeping/summary cost",
        "required_comparison": "same overlaid binary off/on; PCM/config/public reports match accepted component39235964462 before score inference",
        "limits": "not a math/policy fix; no null-law proof; strict full trace/seal and both bounded summaries required",
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.source_root, args.output_dir), indent=2))
