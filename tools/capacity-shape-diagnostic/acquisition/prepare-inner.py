#!/usr/bin/env python3
"""Apply the reversible acquisition-inner CPU partition to accepted postimages."""
import argparse
import difflib
import hashlib
import json
import subprocess
from pathlib import Path

BASE_COMMIT = "34fcedb04d00363943f9a4a7439bbbf180e3e1cd"
FILES = {
    "src/pattern_receiver.cpp": "dbf277ed152a1d3c1c75f3596dfff5af256591ef9f31c820a69b0ace5bae3c91",
    "src/scheduled_work_profile.hpp": "3d427712a8ba8c24cc31075817968269b4de6ef823bb4d1ccd71ca91b36beb65",
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def once(text: str, old: str, new: str) -> str:
    count = text.count(old)
    if count != 1:
        raise ValueError(f"anchor must occur exactly once ({count}): {old[:100]!r}")
    return text.replace(old, new, 1)


def edits_from(before: str, edits: list[tuple[str, str]]) -> tuple[str, str]:
    after = before
    for old, new in edits:
        after = once(after, old, new)
    reverse = after
    for old, new in reversed(edits):
        reverse = once(reverse, new, old)
    if reverse != before:
        raise ValueError("exact reverse reconstruction failed")
    return after, reverse


def prepare(source_root: Path, first_layer: Path, output: Path) -> dict:
    source_root = source_root.resolve(strict=True)
    output = output.resolve()
    if output.exists() or output == source_root:
        raise ValueError("output must be a fresh private directory")
    producer = source_root / "tools/capacity-shape-diagnostic/prepare.py"
    producer_output = first_layer.resolve(strict=True)
    producer_manifest = json.loads((producer_output / "manifest.json").read_text())
    producer_hash = sha(producer.read_bytes())
    if producer_manifest.get("base_commit") != "81c7da84121be60fe0ce1e7252e94cdf952773e8":
        raise ValueError("first-layer producer baseline metadata changed")
    if producer_manifest.get("source_root") != str(source_root):
        raise ValueError("first-layer producer was run against another source root")
    if producer_manifest.get("tool_sha256") != producer_hash:
        raise ValueError("first-layer producer differs from the accepted producer output")
    if producer_manifest.get("status") != "private_preparation_only":
        raise ValueError("first-layer producer output is not private preparation")

    originals: dict[str, str] = {}
    for rel, expected in FILES.items():
        postimage = producer_output / "prepared-source" / rel
        data = postimage.read_bytes()
        if sha(data) != expected:
            raise ValueError(f"first-layer postimage hash changed for {rel}")
        originals[rel] = data.decode()

    pattern_edits = [
        (
            "            detail::execute_fft_search_cpu(batch,jobs,\n"
            "                std::span(scoring_outputs).first(jobs.size()*hop),scoring_workspaces,stop);\n",
            "            {rx_cpu::Scope cpu_stage(rx_cpu::Stage::ordinary_acq_execute);\n"
            "            detail::execute_fft_search_cpu(batch,jobs,\n"
            "                std::span(scoring_outputs).first(jobs.size()*hop),scoring_workspaces,stop);}\n",
        ),
        (
            "            std::size_t collected_unique=0;\n"
            "            for(std::size_t job=0;job<queued;++job) {\n",
            "            {rx_cpu::Scope cpu_stage(rx_cpu::Stage::ordinary_acq_collect);\n"
            "            std::size_t collected_unique=0;\n"
            "            for(std::size_t job=0;job<queued;++job) {\n",
        ),
        (
            "            }\n"
            "            queued=0;\n"
            "        };\n"
            "        for(std::size_t index=0;index<search.initial_stream_symbols;++index) {\n",
            "            }\n"
            "            }\n"
            "            queued=0;\n"
            "        };\n"
            "        for(std::size_t index=0;index<search.initial_stream_symbols;++index) {\n",
        ),
        (
            "                if(!cached_templates.empty() || reuse_templates)prepare_templates(index,stop);\n",
            "                if(!cached_templates.empty() || reuse_templates) {\n"
            "                    rx_cpu::Scope cpu_stage(rx_cpu::Stage::ordinary_acq_reference);\n"
            "                    prepare_templates(index,stop);\n"
            "                }\n",
        ),
        (
            "        if(!fft_workload->restricted_timing_active && !sample_fit && drift_sections==1 && !differential_window &&\n"
            "           acquisition_arithmetic()==SearchArithmetic::fp64 && batch.observations.empty() &&\n"
            "           !batch.observation_skip && work.size()==transform && reference.size()==transform &&\n"
            "           std::fegetround()==FE_TONEAREST) {\n"
            "            batch.carrier_square={};\n"
            "            batch.double_tables=detail::search_fft::prepare_double_tables(\n"
            "                std::span(work).first(transform-1),std::span(reference).first(transform-1),stop);\n"
            "        }\n",
            "        if(!fft_workload->restricted_timing_active && !sample_fit && drift_sections==1 && !differential_window &&\n"
            "           acquisition_arithmetic()==SearchArithmetic::fp64 && batch.observations.empty() &&\n"
            "           !batch.observation_skip && work.size()==transform && reference.size()==transform &&\n"
            "           std::fegetround()==FE_TONEAREST) {\n"
            "            rx_cpu::Scope cpu_stage(rx_cpu::Stage::ordinary_acq_reference);\n"
            "            batch.carrier_square={};\n"
            "            batch.double_tables=detail::search_fft::prepare_double_tables(\n"
            "                std::span(work).first(transform-1),std::span(reference).first(transform-1),stop);\n"
            "        }\n",
        ),
    ]
    pattern_edits.insert(0, (
        "            ensure_execution_scratch();\n"
        "            std::fill(work.begin(),work.end(),Complex{});energy_prefix[0]=0;\n"
        "            for(std::size_t i=0;i<observed+length-1;++i) {\n"
        "                work[i]=at(next_start+i);energy_prefix[i+1]=energy_prefix[i]+std::norm(work[i]);\n"
        "            }\n",
        "            ensure_execution_scratch();\n"
        "            {rx_cpu::Scope cpu_stage(rx_cpu::Stage::ordinary_acq_input);\n"
        "            std::fill(work.begin(),work.end(),Complex{});energy_prefix[0]=0;\n"
        "            for(std::size_t i=0;i<observed+length-1;++i) {\n"
        "                work[i]=at(next_start+i);energy_prefix[i+1]=energy_prefix[i]+std::norm(work[i]);\n"
        "            }}\n",
    ))
    pattern_edits.append((
        "            spectrum=work;pattern_fft(spectrum,false,stop,acquisition_arithmetic());++fft_workload->input_transforms;\n",
        "            {rx_cpu::Scope cpu_stage(rx_cpu::Stage::ordinary_acq_input);\n"
        "            spectrum=work;pattern_fft(spectrum,false,stop,acquisition_arithmetic());++fft_workload->input_transforms;\n",
    ))
    # The input copy/fill and transform are separate coarse phases; table prep,
    # worker dispatch, and peak collection each have one scoped section per hop.
    pattern_edits.append((
        "            if(sample_fit && drift_sections<=1)for(std::size_t j=0;j<count;++j)work[j]=carrier_square(next_start+j);\n",
        "            if(sample_fit && drift_sections<=1)for(std::size_t j=0;j<count;++j)work[j]=carrier_square(next_start+j);}\n",
    ))
    pattern_after, pattern_reverse = edits_from(originals["src/pattern_receiver.cpp"], pattern_edits)

    profile_edits = [
        (
            "scheduled_candidate_residual, common_raw, ordinary_acquisition, ordinary_continuation, ordinary_source_generation, count\n",
            "scheduled_candidate_residual, common_raw, ordinary_acquisition, ordinary_continuation, ordinary_source_generation, ordinary_acq_input, ordinary_acq_reference, ordinary_acq_execute, ordinary_acq_collect, count\n",
        ),
        (
            '"scheduled_common_residual","scheduled_candidate_residual","common_raw","ordinary_acquisition","ordinary_continuation","ordinary_source_generation"\n',
            '"scheduled_common_residual","scheduled_candidate_residual","common_raw","ordinary_acquisition","ordinary_continuation","ordinary_source_generation","ordinary_acq_input","ordinary_acq_reference","ordinary_acq_execute","ordinary_acq_collect"\n',
        ),
    ]
    profile_after, profile_reverse = edits_from(originals["src/scheduled_work_profile.hpp"], profile_edits)

    class_anchor = "#undef DATAPUMP_PROFILE_COLD_NOINLINE\n"
    inner_class = '// Private diagnostic: matched same-binary scope ON/OFF, no detector changes.\ninline bool acquisition_inner_enabled() noexcept {\n    static const bool enabled=[] {const char* p=std::getenv("DATAPUMP_ACQUISITION_INNER_CPU");return p&&std::strcmp(p,"1")==0;}();\n    return enabled;\n}\nclass InnerScope {\n    std::optional<Scope> scope_;\npublic:\n    explicit InnerScope(Stage category) noexcept {if(acquisition_inner_enabled())scope_.emplace(category);}\n};\n'
    profile_unconditional = profile_after
    profile_after = once(profile_after, class_anchor, inner_class+class_anchor)
    pattern_unconditional = pattern_after
    for stage in ("input", "reference", "execute", "collect"):
        old = "rx_cpu::Scope cpu_stage(rx_cpu::Stage::ordinary_acq_"+stage+");"
        new = "rx_cpu::InnerScope cpu_stage(rx_cpu::Stage::ordinary_acq_"+stage+");"
        if not pattern_after.count(old): raise ValueError("missing inner scope: "+stage)
        pattern_after = pattern_after.replace(old,new)
    restored = pattern_after.replace("rx_cpu::InnerScope cpu_stage(","rx_cpu::Scope cpu_stage(")
    if restored != pattern_unconditional: raise ValueError("conditional scope reverse mismatch")
    if once(profile_after,inner_class+class_anchor,class_anchor) != profile_unconditional:
        raise ValueError("conditional profiler reverse mismatch")

    output.mkdir(parents=True)
    results = {
        "src/pattern_receiver.cpp": (originals["src/pattern_receiver.cpp"], pattern_after, pattern_reverse),
        "src/scheduled_work_profile.hpp": (originals["src/scheduled_work_profile.hpp"], profile_after, profile_reverse),
    }
    for rel, (before, after, reverse) in results.items():
        name = Path(rel).name
        suffix = Path(rel).suffix
        (output / f"{name}.pre-inner").write_text(before)
        (output / f"{name}.post-inner").write_text(after)
        (output / f"{name}.reversed").write_text(reverse)
        patch = "".join(difflib.unified_diff(
            before.splitlines(True), after.splitlines(True),
            fromfile="producer-output/" + rel, tofile="inner-postimage/" + rel,
        ))
        (output / f"{name}.patch").write_text(patch)

    manifest = {
        "schema": 1,
        "status": "private_second_layer_preparation_only",
        "baseline_commit": BASE_COMMIT,
        "source_root": str(source_root),
        "first_layer_output": str(producer_output),
        "first_layer_manifest_sha256": sha((producer_output / "manifest.json").read_bytes()),
        "first_layer_producer_sha256": producer_hash,
        "source_closure": producer_manifest["source_closure"],
        "first_layer_postimage_sha256": {rel: sha((producer_output / "prepared-source" / rel).read_bytes()) for rel in FILES},
        "second_layer": {
            rel: {"before_sha256": sha(before.encode()), "after_sha256": sha(after.encode()), "reverse_sha256": sha(reverse.encode())}
            for rel, (before, after, reverse) in results.items()
        },
        "scope_switch": {"environment":"DATAPUMP_ACQUISITION_INNER_CPU", "off":"0", "on":"1", "same_binary":True},
        "stages": ["ordinary_acq_input", "ordinary_acq_reference", "ordinary_acq_execute", "ordinary_acq_collect"],
        "interpretation": {
            "ordinary_acq_input": "coarse per-hop receive upload and shared input FFT; fallback/guard work outside these scopes remains ordinary_acquisition residual",
            "ordinary_acq_reference": "coarse per-group template preparation and optional shared double-table preparation",
            "ordinary_acq_execute": "coarse batch scorer dispatch; submitted worker contexts inherit this stage through the accepted first-layer propagation",
            "ordinary_acq_collect": "coarse ordered threshold/peak collection per batch",
            "residual_paths": "drift-serial, bounded/direct/native fallback dispatch, skipped acquisitions, admission, and all uninstrumented acquisition work remain in ordinary_acquisition residual",
            "scope_limit": "stage labels partition exclusive native-thread CPU only when the existing profiler Context is active; no search arithmetic, order, coverage, deadlines, fixture input, or assertions change",
        },
        "outputs": {name: sha((output / name).read_bytes()) for name in sorted(p.name for p in output.iterdir() if p.is_file() and p.name != "manifest.json")},
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if json.loads((output / "manifest.json").read_text()) != manifest:
        raise ValueError("manifest readback mismatch")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--first-layer", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(prepare(args.source_root, args.first_layer, args.output_dir), indent=2))
