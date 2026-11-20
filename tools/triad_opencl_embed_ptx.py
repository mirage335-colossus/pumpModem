#!/usr/bin/env python3
"""Embed the offline NVFP4 kernels with OpenCL's buffer argument metadata."""

from pathlib import Path
import re
import sys


# Nonzero values are pointer element alignments. Zeros denote u32 scalar
# parameters. Reject ABI changes here rather than guessing which u64s are
# buffers when the driver's OpenCL loader imports the PTX.
SIGNATURES = {
    "resident_fine_fft": (8, 8, 8, 0, 0, 0),
    "resident_fine_map": (16, 8, 8, 0, 0),
    "resident_fine_clear": (8, 0, 0),
    "resident_fine_absent": (8, 1, 0, 0),
    "resident_fine_render_tiles": (16, 8, 8, 8, 16, 0, 0),
    "resident_fine_peaks": (8, 0, 0),
    "resident_fine_stats": (8, 8, 16, 16, 0, 0, 0, 0),
    "nvfp4_pack_basis": (8, 4, 1, 0, 0, 0, 0),
    "nvfp4_pack_input": (8, 4, 1, 0, 0, 0, 0),
    "nvfp4_project": (4, 1, 4, 1, 8, 8, 0, 0, 0, 0),
    "coarse_render_lanes": (8, 4, 8, 8, 0, 0, 0, 0),
    "coarse_warp_columns": (8, 8, 8, 0, 0, 0, 0, 0),
    "coarse_fir_decimate": (8, 4, 8, 0, 0, 0, 0, 0, 0),
    "coarse_bank_gram": (8, 8, 0, 0, 0),
    "coarse_column_peaks": (8, 4, 0, 0),
    "coarse_pack_bank": (8, 4, 4, 0, 0, 0, 0),
    "coarse_pack_observation": (8, 8, 4, 4, 0, 0, 0),
    "coarse_fp4_scores": (16, 8, 16, 8, 4, 0, 0, 0, 0, 0),
    "coarse_select_peaks": (4, 8, 0, 0),
    "coarse_rescore_peaks": (8, 8, 8, 8, 8, 0, 0, 0, 0, 0),
}


def with_buffer_parameters(ptx):
    if not re.search(r"(?m)^\.target[ \t]+sm_120a[ \t]*$", ptx):
        raise ValueError("expected an SM120a NVFP4 program")
    seen = set()

    def annotate(match):
        name, arguments = match.groups()
        if name not in SIGNATURES or name in seen:
            raise ValueError(f"unexpected NVFP4 entry {name}")
        seen.add(name)
        params = arguments.split(",")
        signature = SIGNATURES[name]
        if len(params) != len(signature):
            raise ValueError(f"changed parameters for {name}")
        declarations = []
        for index, (param, alignment) in enumerate(zip(params, signature)):
            scalar = ".u64" if alignment else ".u32"
            symbol = f"{name}_param_{index}"
            expected = f".param {scalar} {symbol}"
            annotated = f".param .u64 .ptr .global .align {alignment} {symbol}" if alignment else expected
            if " ".join(param.split()) not in (expected, annotated):
                raise ValueError(f"changed type or metadata for {symbol}")
            declarations.append("\t" + annotated)
        return f".visible .entry {name}(\n" + ",\n".join(declarations) + "\n)"

    result = re.sub(r"(?m)^\.visible[ \t]+\.entry[ \t]+(\w+)[ \t]*\(([^)]*)\)", annotate, ptx)
    if seen != SIGNATURES.keys() or len(re.findall(r"\s\.entry\s", result)) != len(seen):
        raise ValueError("unexpected NVFP4 entry inventory")
    return result


def embed(source, destination):
    encoded = with_buffer_parameters(source.read_text(encoding="ascii")).encode("ascii")
    body = "".join(
        "    " + ", ".join(f"0x{byte:02x}" for byte in encoded[i:i + 16]) + ",\n"
        for i in range(0, len(encoded), 16)
    )
    destination.write_text(
        "#pragma once\n#include <cstddef>\n\n"
        "namespace datapump::triad::v3::receiver_detail {\n"
        "inline constexpr unsigned char opencl_nvfp4_ptx[] = {\n"
        + body + "};\n"
        "inline constexpr std::size_t opencl_nvfp4_ptx_bytes = sizeof(opencl_nvfp4_ptx);\n"
        "} // namespace datapump::triad::v3::receiver_detail\n",
        encoding="ascii",
    )


if __name__ == "__main__":
    try:
        embed(Path(sys.argv[1]), Path(sys.argv[2]))
    except (OSError, ValueError, IndexError) as error:
        raise SystemExit(f"OpenCL NVFP4 PTX: {error}") from error
