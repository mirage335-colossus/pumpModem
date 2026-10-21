#!/usr/bin/env python3
"""Generate a standalone C++ replay fixture from one sealed copied-help capture."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from decimal import Decimal


CONFIG_FIELDS = {
    "pattern_symbols": "bool", "search_arithmetic": "enum",
    "receiver_strategy": "enum", "outer_dsss_version": "enum",
    "spreading_factor": "unsigned", "stream_epoch": "uint64_t",
    "stream_phase_samples": "uint64_t", "sample_rate": "uint32_t",
    "constellation_bits": "unsigned", "carrier_hz": "double",
    "frequency_search_multiplier": "double", "bandwidth_hz": "double",
    "training_seconds": "double", "integration_seconds": "double",
    "spreading_mode": "enum", "pulse_shaping": "bool", "scramble": "bool",
    "dsss": "bool", "dsss_factor": "unsigned",
    "receiver_cpu_budget": "double", "memory_limit": "size_t",
}
OSC_FIELDS = {
    "oscillator_lf_accuracy_ppm": "lf.accuracy_ppm",
    "oscillator_lf_phase_noise_degrees_per_sqrt_second": "lf.phase_noise_degrees_per_sqrt_second",
    "oscillator_rf_accuracy_ppm": "rf.accuracy_ppm",
    "oscillator_rf_phase_noise_degrees_per_sqrt_second": "rf.phase_noise_degrees_per_sqrt_second",
    "oscillator_rf_shift_hz": "rf_shift_hz", "oscillator_margin": "margin",
    "oscillator_reference": "reference", "oscillator_sideband": "sideband",
}
SEARCH_FIELDS = {
    "admitted_frequency_half_width_hz": "optional_double",
    "admitted_clock_half_width_ppm": "optional_double",
    "complex_frequency_coverage": "optional_bool",
    "complex_clock_coverage": "optional_bool",
    "track_limit": "size_t", "bit_limit": "size_t",
    "receiver_bank_count": "size_t", "amplitude_receiver_bank_count": "size_t",
    "amplitude_strategy_workspace_bytes": "size_t", "receiver_schedule_key_count": "size_t",
    "receiver_strategy_workspace_bytes": "size_t", "amplitude_channel_limit": "size_t",
    "input_sample_origin": "uint64_t", "drift_tolerant": "bool",
    "differential_window_seconds": "double", "frequency_rate_competition": "bool",
    "expand_clock_search": "bool", "prefer_streamed_templates": "bool",
    "reuse_deferred_templates": "bool", "allow_local_clock_fallback": "bool",
    "couple_clock_to_carrier": "bool", "uncoupled_frequency_count": "size_t",
    "false_alarm_probability": "double", "retain_score": "double",
    "chunk_bits": "size_t", "candidate_limit": "size_t",
    "initial_stream_symbols": "size_t", "search_stream_phases": "bool",
    "compact_clock_search": "bool", "start_offset_seconds": "optional_double",
    "start_uncertainty_seconds": "double", "amplitude_epoch_stride_seconds": "size_t",
    "amplitude_lookahead_epochs": "size_t", "restricted_timing_search": "bool",
    "restricted_direct_search": "bool", "partitioned_timing_search": "bool",
    "early_qualified_fft": "bool", "component_qualified_search": "bool",
    "release_idle_fft_scratch": "bool", "worker_threads": "size_t",
    "outer_presence_guard": "bool",
}


def fail(message: str) -> "NoReturn":
    raise ValueError(message)


def json_lines(path: Path):
    try:
        with path.open("r", encoding="utf-8") as stream:
            for number, line in enumerate(stream, 1):
                if not line.endswith("\n"):
                    fail(f"{path}:{number}: partial JSONL line")
                try:
                    yield json.loads(line, parse_float=Decimal, parse_int=lambda token: Decimal(token) if token == "-0" else int(token),
                                      parse_constant=lambda x: fail(f"invalid number {x}"))
                except Exception as exc:
                    fail(f"{path}:{number}: invalid/truncated JSON record: {exc}")
    except OSError as exc:
        fail(f"cannot read {path}: {exc}")


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def cnum(value, kind="double") -> str:
    if value is None:
        return "{}"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, Decimal):
        # Preserve the JSON decimal through C++'s correctly-rounded conversion.
        token = format(value, "f") if value.adjusted() > -300 else str(value)
        if "." not in token and "e" not in token.lower():
            token += ".0"
        return token + ("L" if kind == "long_double" else "")
    if kind == "long_double":
        return str(value) + ".0L"
    return str(value)


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def required(obj, key):
    if key not in obj:
        fail(f"capture lacks required typed field {key!r}")
    return obj[key]


def assignment(name, value, kind, prefix=""):
    if kind.startswith("optional_"):
        if value is None:
            return f"    {prefix}{name}.reset();"
        return f"    {prefix}{name} = {cnum(value, kind[9:])};"
    if value is None:
        fail(f"unsupported null for required field {name}")
    if kind == "enum":
        return f"    {prefix}{name} = static_cast<std::remove_reference_t<decltype({prefix}{name})>>({cnum(value)});"
    return f"    {prefix}{name} = {cnum(value, kind)};"


def make_header(namespace: str, receiver: dict, feeds: list, candidates: list, manifest: dict) -> str:
    lines = ["#pragma once", "// Generated from sealed copied-help evidence; diagnostic replay fixture only.",
             '#include "copied_help_fixture_contract.hpp"', "#include <algorithm>",
             "#include <array>", "#include <cstddef>", "#include <cstdint>", "#include <optional>",
             "#include <string_view>", "#include <type_traits>", "",
             f"namespace datapump::diagnostic::copied_help_replay::{namespace} {{"]
    lines += ["inline modem::Config make_config(const Crypto& retained_key) {", "    modem::Config c;"]
    for key, kind in CONFIG_FIELDS.items():
        lines.append(assignment(key, required(receiver, key), kind, "c."))
    if required(receiver, "oscillator_search_present"):
        lines.append("    c.oscillator_search.emplace();")
        for key, member in OSC_FIELDS.items():
            kind = "enum" if key in ("oscillator_reference", "oscillator_sideband") else "double"
            lines.append(assignment(member, required(receiver, key), kind, "c.oscillator_search->"))
    else:
        lines.append("    c.oscillator_search.reset();")
    for forbidden in ("fake_hopping_present", "scheduled_hopping_present"):
        if required(receiver, forbidden):
            fail(f"opaque {forbidden} state is unsupported")
    if required(receiver, "burst_wait_symbols") != 0 or required(receiver, "burst_random_limit") != 0:
        fail("opaque symbol-burst state is unsupported")
    lines += ["    c.data_key = retained_key;",
              "    if(c.scramble) { const auto seed=retained_key.stream(StreamPurpose::Scrambler,0,0,c.spreading_seed.size()); std::copy(seed.begin(),seed.end(),c.spreading_seed.begin()); }",
              "    if(c.dsss || c.dsss_factor>1) { const auto seed=retained_key.stream(StreamPurpose::Dsss,0,0,c.dsss_seed.size()); std::copy(seed.begin(),seed.end(),c.dsss_seed.begin()); }",
              "    return c;", "}", "inline modem::PatternSearch make_search() {", "    modem::PatternSearch q;"]
    for key, kind in SEARCH_FIELDS.items():
        lines.append(assignment(key, required(receiver, key), kind, "q."))
    lines.append("    q.frequency_offsets_hz = {" + ", ".join(cnum(x) for x in required(receiver, "frequency_offsets_hz")) + "};")
    lines.append("    q.clock_errors_ppm = {" + ", ".join(cnum(x) for x in required(receiver, "clock_errors_ppm")) + "};")
    lines.append("    q.hypotheses = {")
    for freq, clock in required(receiver, "hypotheses"):
        lines.append(f"        modem::PatternFrequencyRateHypothesis{{{cnum(freq)}, {cnum(clock)}}},")
    lines.append("    };")
    for key in ("qualified_start_window", "amplitude_start_window"):
        value = required(receiver, key)
        if value is None:
            lines.append(f"    q.{key}.reset();")
        else:
            if not isinstance(value, list) or len(value) != 3:
                fail(f"invalid affine start-window field {key}")
            literals = ", ".join(cnum(x, "long_double") for x in value)
            lines.append(f"    q.{key} = modem::PatternStartWindow{{{literals}}};")
    for key in ("amplitude_start_offset_seconds", "amplitude_start_uncertainty_seconds"):
        lines.append(assignment(key, required(receiver, key), "optional_double", "q."))
    if required(receiver, "scheduled_capture_present"):
        fail("opaque scheduled_capture is unsupported")
    lines += ["    q.scheduled_capture.reset();", "    return q;", "}"]
    lines.append(f"inline constexpr std::array<Feed, {len(feeds)}> feeds{{{{")
    for item in feeds:
        vals = (str(item["first"]), str(item["count"]), str(item["pcm_offset"]), cnum(item["mixer_real"]),
                cnum(item["mixer_imag"]), cnum(item["after_real"]), cnum(item["after_imag"]),
                "true" if item["receiver_active"] else "false")
        lines.append("    Feed{" + ", ".join(vals) + "},")
    lines.append("}};")
    lines.append(f"inline constexpr std::array<ExpectedBurst, {len(candidates)}> expected{{{{")
    for item in candidates:
        score, support = item["score"], item["support_samples"]
        if not isinstance(score, str) or not score.startswith("0x") or not isinstance(support, str) or not support.startswith("0x"):
            fail("candidate score/support must retain captured hexadecimal floating representation")
        vals = (str(item["feed_first"]), str(item["physical_first"]), str(item["physical_end"]),
                str(item["stream_first_symbol"]), str(item["chunk_first_symbol"]),
                str(item["stream_phase_samples"]), str(item["missing_slots"]),
                "true" if item["complete"] else "false", cpp_string(item["chunk_bits"]),
                score, support,
                "std::nullopt" if item.get("physical_chunk_first") is None else str(item["physical_chunk_first"]),
                "std::nullopt" if item.get("frequency_hz") is None else cnum(item["frequency_hz"]))
        lines.append("    ExpectedBurst{" + ", ".join(vals) + "},")
    lines += ["}};", "inline const Fixture& fixture() {", "    static const Fixture value{",
              f"        {cpp_string(manifest['label'])}, {cpp_string(manifest['pcm_path'])}, {cpp_string(manifest['keyfile_path'])},",
              f"        {cpp_string(manifest['pcm_sha256'])}, {cpp_string(manifest['key_tag'])},",
              f"        {receiver['epoch']}, {receiver['receiver_id']}, {receiver['input_origin']}, {receiver['actual_constructor_grant']},",
              "        feeds, expected, &make_config, &make_search};", "    return value;", "}",
              f"}} // namespace datapump::diagnostic::copied_help_replay::{namespace}", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture-root", required=True, type=Path)
    parser.add_argument("--receiver-id", required=True, type=int)
    parser.add_argument("--namespace", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.receiver_id <= 0:
        fail("receiver-id must be positive")
    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", args.namespace):
        fail("namespace must be a C++ identifier")
    root = args.capture_root.resolve(strict=True)
    if root.name not in ("run-1", "run-2") or root.parent.name != "smokes":
        fail("capture-root must be run-1 or run-2 directly under evidence/smokes")
    out = args.output.resolve(strict=False)
    if out.exists():
        fail(f"output directory already exists: {out}")
    required_paths = [root / "pcm/input.f32", root / "pcm/events.jsonl", root / "pcm/feeds.jsonl",
                      root / "pcm/seal.json", root / "candidate.jsonl", root / "capture.jsonl", root / "run.json"]
    for path in required_paths:
        if not path.is_file():
            fail(f"missing or truncated capture input: {path}")
    seal = json.loads((root / "pcm/seal.json").read_text())
    if seal.get("complete") is not True or seal.get("data_files_ok") is not True:
        fail("PCM capture seal is incomplete")
    if digest(root / "pcm/events.jsonl") != seal.get("events_sha256") or digest(root / "pcm/feeds.jsonl") != seal.get("feeds_sha256"):
        fail("event/feed input hash disagrees with PCM seal")
    inventory = json.loads((root / "manifest.json").read_text())
    inventory_items = inventory.get("files")
    if not isinstance(inventory_items, list):
        fail("capture manifest has no file inventory")
    for path in required_paths:
        if path.name == "run.json":
            continue
        rel = path.relative_to(root).as_posix()
        matches = [x for x in inventory_items if x.get("path", "").endswith(f"/{root.name}/{rel}")]
        if len(matches) != 1 or matches[0].get("sha256") != digest(path) or matches[0].get("size_bytes") != path.stat().st_size:
            fail(f"capture manifest does not uniquely hash-bind {rel}")
    pcm = root / "pcm/input.f32"
    if pcm.stat().st_size != seal.get("samples", -1) * 4 or digest(pcm) != seal.get("pcm_sha256"):
        fail("PCM sample extent/hash disagrees with seal")
    events = list(json_lines(root / "pcm/events.jsonl"))
    capture_rows = list(json_lines(root / "capture.jsonl"))
    phase21 = [x for x in capture_rows if x.get("boundary") == "phase21_after_replay_before_help_evaluation"]
    if len(phase21) != 1 or phase21[0].get("snapshot_transmission_id") != 16:
        fail("capture.jsonl lacks the unique TX16 phase21 boundary")
    contexts = [x for x in events if x.get("event") == "source_context" and x.get("transmission_id") == 16]
    tuples = {(x.get("session"), x.get("bank"), x.get("generation"), x.get("revision")) for x in contexts}
    if len(tuples) != 1 or None in next(iter(tuples)):
        fail(f"TX16 source_context tuple is missing or ambiguous: {tuples}")
    session, bank, generation, revision = next(iter(tuples))
    selected = [x for x in events if x.get("event") == "receiver_constructed" and
                (x.get("session"), x.get("bank"), x.get("generation"), x.get("revision")) == (session, bank, generation, revision) and
                x.get("receiver_id") == args.receiver_id]
    if len(selected) != 1:
        fail(f"TX16 target tuple has {len(selected)} constructors for receiver {args.receiver_id}")
    receiver = selected[0]
    target_epoch = receiver["epoch"]
    if receiver.get("config_replay_supported") is not True:
        fail("captured config uses an unsupported replay mode")
    if receiver.get("data_key_present") is not True:
        fail("selected receiver constructor did not retain a keyed Crypto")
    for k in CONFIG_FIELDS:
        required(receiver, k)
    for k in SEARCH_FIELDS:
        required(receiver, k)
    required(receiver, "frequency_offsets_hz"); required(receiver, "hypotheses"); required(receiver, "clock_errors_ppm")
    receiver_identity = hex(args.receiver_id)
    candidates_all = [x for x in json_lines(root / "candidate.jsonl") if x.get("type") == "candidate"]
    candidates = [x for x in candidates_all if x.get("bank_identity", "").lower() == hex(bank) and
                  x.get("generation") == generation and x.get("input_revision") == revision and
                  x.get("receiver_identity", "").lower() == receiver_identity and
                  x.get("epoch") == target_epoch and x.get("sample_origin") == receiver["input_origin"]]
    if not candidates:
        fail("no candidate sequence matches the selected actual receiver identity and epoch")
    candidates.sort(key=lambda x: x["ordinal"])
    if any(x.get("symbol_samples") not in (512, 1024) for x in candidates):
        fail("candidate sequence contains unsupported symbol geometry")
    if any(x.get("chunk_bits") is None or x.get("chunk_unknown_count") is None for x in candidates):
        fail("candidate chunk fields are missing")
    feeds_all = list(json_lines(root / "pcm/feeds.jsonl"))
    bank = receiver["bank"]
    feeds = [x for x in feeds_all if x.get("session") == session and x.get("bank") == bank and x.get("generation") == generation and x.get("revision") == revision]
    if not feeds or any(x.get("status") != 1 for x in feeds):
        fail("selected receiver feed stream is missing or contains uncommitted/truncated feeds")
    projections = [x for x in events if x.get("event") == "projection_begin" and x.get("session") == session and x.get("bank") == bank and x.get("generation") == generation and x.get("revision") == revision]
    if not projections:
        fail("selected receiver has no projection witnesses")
    feed_events = [x for x in events if x.get("event") == "feed_attempt" and x.get("session") == session and x.get("bank") == bank and x.get("generation") == generation and x.get("revision") == revision]
    projections_by_first = {(x["first"], x["count"]): x for x in projections}
    feed_by_ordinal = {x["ordinal"]: x for x in feed_events}
    feed_details = []
    for item in feeds:
        e = feed_by_ordinal.get(item["ordinal"])
        p = projections_by_first.get((item["first"], item["count"]))
        if not e or not p:
            fail(f"feed ordinal {item['ordinal']} lacks feed/projection event witness")
        feed_details.append({"first": item["first"], "count": item["count"], "pcm_offset": item["pcm_offset"],
                             "mixer_real": p["mixer_real"], "mixer_imag": p["mixer_imag"],
                             "after_real": e["mixer_real_after_projection"], "after_imag": e["mixer_imag_after_projection"],
                             "receiver_active": item["first"] >= receiver["input_origin"]})
    for item in candidates:
        containing = [f for f in feed_details if f["first"] + f["count"] == item["captured_samples"]]
        if len(containing) != 1:
            fail(f"candidate ordinal {item['ordinal']} does not map to one exact captured feed end")
        item["feed_first"] = containing[-1]["first"]
        if item["chunk_count"] != len(item["chunk_bits"]):
            fail(f"candidate ordinal {item['ordinal']} bit count disagrees with chunk_bits")
    last_extent = max(x["captured_samples"] for x in candidates)
    feed_details = [f for f in feed_details if f["first"] < last_extent]
    run = json.loads((root / "run.json").read_text())
    key_name = run["fixtures"]["key"]["path"].rsplit("/", 1)[-1]
    key_path = root / "smoke-dir" / key_name
    expected_key_sha = run["fixtures"]["key"].get("sha256")
    if not key_path.is_file() or not expected_key_sha or digest(key_path) != expected_key_sha:
        fail("retained keyfile is missing or disagrees with the capture manifest")
    # Attach first-divergence witnesses only when the original verbose drain
    # record uniquely identifies this exact captured candidate.
    verbose_path = root / "verbose.jsonl"
    if verbose_path.is_file():
        drain_rows = [x for x in json_lines(verbose_path) if x.get("event") == "receiver_pattern_drain"]
        for item in candidates:
            matches = [x for x in drain_rows if x.get("epoch") == item.get("epoch") and
                       x.get("family") == item.get("family") and
                       x.get("sample_origin") == item.get("sample_origin") and
                       x.get("captured_samples") == item["feed_first"] and
                       x.get("revision") == revision and
                       x.get("stream_first_sample") + receiver["input_origin"] == item["physical_first"] and
                       x.get("end_sample") + receiver["input_origin"] == item["physical_end"] and
                       x.get("stream_phase_samples") == item["stream_phase_samples"] and
                       x.get("first_stream_symbol") == item.get("chunk_first_symbol") and
                       x.get("accepted_count") == item.get("chunk_count") and
                       bool(x.get("complete")) == bool(item.get("complete"))]
            if len(matches) == 1:
                item["stream_first_symbol"] = matches[0]["stream_first_symbol"]
                item["missing_slots"] = matches[0]["missing"]
                item["physical_chunk_first"] = receiver["input_origin"] + matches[0]["first_sample"]
                item["frequency_hz"] = matches[0].get("frequency_hz")
            else:
                fail(f"candidate ordinal {item['ordinal']} has {len(matches)} detector-drain matches; cannot reconstruct stream identity")
    if not verbose_path.is_file():
        fail("full original detector-drain trace required for stable stream identity")
    manifest = {"capture_root": str(root), "run": root.name, "receiver_id": args.receiver_id,
                "epoch": target_epoch, "origin": receiver["input_origin"], "grant": receiver["actual_constructor_grant"],
                "key_hmac": receiver["key_tag_hmac_dp_rx_bank"], "capture_hashes": {p.relative_to(root).as_posix(): digest(p) for p in required_paths},
                "candidate_ordinals": [x["ordinal"] for x in candidates], "candidate_geometry": sorted({x["symbol_samples"] for x in candidates}),
                "feed_count": len(feed_details), "projection_count": len(projections),
                "label": f"{root.name}-receiver-{args.receiver_id}", "pcm_path": f"smokes/{root.name}/pcm/input.f32",
                "keyfile_path": f"smokes/{root.name}/smoke-dir/{key_name}", "pcm_sha256": seal["pcm_sha256"],
                "key_tag": receiver["key_tag_hmac_dp_rx_bank"], "keyfile_sha256": expected_key_sha,
                "capture_manifest_sha256": digest(root / "manifest.json")}
    header = make_header(args.namespace, receiver, feed_details, candidates, manifest)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.mkdir()
    try:
        (out / "fixture.hpp").write_text(header, encoding="utf-8", newline="\n")
        (out / "manifest.json").write_text(json.dumps(manifest, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    except Exception:
        raise
    print(f"wrote {out / 'fixture.hpp'} ({len(candidates)} candidate records, {len(feeds)} feeds, {len(projections)} projections)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(f"generate.py: {error}", file=sys.stderr)
        raise SystemExit(2)
