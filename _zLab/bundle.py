#!/usr/bin/env python3
"""Build an allowlisted source archive for the standalone lab worker.

The canonical probe bundler supplies its checked native source inventory and
safe archive-writing rules.  The lab adds the relay and build-boundary checks
needed by the worker target. Build with DATAPUMP_PORTABLE=OFF; this archive is
not a complete source or release distribution. CUDA requires its own toolkit
and CUDA-enabled LibTorch on the pod, and is not established by this bundle.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import sys
import threading

_ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("datapump_triad_gpu_bundle", _ROOT / "tools/triad_gpu_bundle.py")
if _spec is None or _spec.loader is None:
    raise ImportError("Cannot load the native source bundle builder")
_native = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_native)
BundleError = _native.BundleError
_LOCK = threading.Lock()
_EXTRA_FILES = (
    "_zLab/worker_relay.py",
    "_zLab/probe_cuda_worker.cpp",
    "_zLab/prepare_vulkan.sh",
    "_zLab/audio/inject.cmake",
    "_zLab/audio/protocol.hpp",
    "_zLab/audio/remote.hpp",
    "_zLab/audio/remote.cpp",
    "_zLab/audio/provider.cpp",
    "_zLab/audio/worker.cpp",
    "_zLab/audio/worker_schedule.hpp",
    "_zLab/audio/local.hpp",
    "_zLab/audio/local.cpp",
    "_zLab/audio/local_native.cpp",
    "_zLab/audio/local_main.cpp",
    "tests/legacy_boundary.cmake",
    "tests/gui_adapter_boundary.cmake",
    "tests/fast_boundary.cmake",
    "third_party/ldpc/tables.hpp",
    "third_party/ldpc/short_tables.hpp",
    "third_party/ldpc/wifi_tables.hpp",
    "third_party/ldpc/LICENSE",
    "third_party/ldpc/WIFI-LICENSE",
)


def create_bundle(root: Path | str, destination: Path | str) -> dict[str, object]:
    """Create an exclusive private archive outside root with a content manifest.

    The native collector reads the extra exact entries with its descriptor-based
    no-symlink and source-change checks. Restore its inventory on every exit.
    """
    with _LOCK:
        original = _native._EXACT_FILES
        try:
            # The editable manual-case list is not needed to build the worker
            # and may contain local experiment inputs.
            _native._EXACT_FILES = tuple(path for path in original
                                         if path != "_tests-RobustModem.md") + _EXTRA_FILES
            return _native.create_bundle(root, destination)
        finally:
            _native._EXACT_FILES = original


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=_ROOT)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(create_bundle(args.root, args.output), sort_keys=True))
    except (OSError, BundleError) as error:
        print(f"lab bundle: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
