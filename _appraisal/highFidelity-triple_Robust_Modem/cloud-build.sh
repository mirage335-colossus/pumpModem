#!/usr/bin/env bash
# Native build only. Provider launch, spending/deadline enforcement and cleanup
# remain with the authorized operator. No account metadata or credentials enter
# this source tree. Use an official RunPod PyTorch CUDA development image.
set -Eeuo pipefail

appraisal_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$appraisal_dir/../.." && pwd)
cache_dir=""
expected_gpu="RTX PRO 6000 Blackwell"
build_jobs=""
preflight_only=false
usage() {
    cat <<'TEXT'
Usage: bash cloud-build.sh --cache-dir /workspace/appraisal-cache [options]
  --jobs N                    Explicit compiler jobs; default native RAM/CPU selector.
  --expected-gpu TEXT          Required nvidia-smi name substring (default RTX PRO 6000 Blackwell).
  --preflight-only             Verify inputs, toolchain and GPU; do not configure/build.

Run from a fresh, manifest-verified extracted source bundle on a trusted official
RunPod PyTorch CUDA DEVELOPMENT image, with one full GPU. Required tools: Python3
with CUDA-enabled C++11-ABI PyTorch, CMake>=3.21, C/C++20 compiler, CUDA nvcc,
OpenSSL>=3 development files, and Ninja or Make. Python only locates/identifies
LibTorch and orchestrates metadata; modem and channel processing are compiled C++.

Before launching, the operator verifies the exact GPU edition/full allocation,
provider rate and aggregate budget, and sets a hard cleanup deadline. Verify GPU
name, memory, power, compute capability, driver/runtime and MIG-disabled status;
a provider WX/WK label or a compatible binary is insufficient. If choosing another
GPU, record the concrete workload/compatibility/availability/budget reason.

Example (after verifying the archive hash and extracting into a fresh directory):
  bash _appraisal/highFidelity-triple_Robust_Modem/cloud-build.sh \
    --cache-dir /workspace/appraisal-cache --preflight-only
  bash _appraisal/highFidelity-triple_Robust_Modem/cloud-build.sh \
    --cache-dir /workspace/appraisal-cache

The JSON final receipt gives the executable. Run a bounded plan first, then the
same compiled receiver with --backend cuda; inspect executed_gpu_work and actual
arithmetic. A successful build is not GPU execution or receiver qualification.
Keep ccache on the selected persistent volume. Build directories are keyed by the
source manifest and reuse that cache; remove superseded builds after their current
validation question is resolved. Retain the volume only with an explicit owner,
purpose and review/expiry date, and include its continuing cost in the budget.
TEXT
}
while (($#)); do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --cache-dir|--jobs|--expected-gpu)
            (($# >= 2)) || { echo "$1 needs a value" >&2; exit 2; }
            case "$1" in
                --cache-dir) cache_dir=$2 ;;
                --jobs) build_jobs=$2 ;;
                --expected-gpu) expected_gpu=$2 ;;
            esac
            shift 2 ;;
        --preflight-only) preflight_only=true; shift ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
[[ -n "$cache_dir" && "$cache_dir" = /* && "$cache_dir" != / ]] || { echo 'An absolute, non-root --cache-dir on the chosen volume is required' >&2; exit 2; }
[[ -n "$expected_gpu" ]] || { echo 'Expected GPU substring must not be empty' >&2; exit 2; }
if [[ -n "$build_jobs" && ! "$build_jobs" =~ ^[1-9][0-9]*$ ]]; then echo '--jobs must be a positive integer' >&2; exit 2; fi

ensure_cuda_path() {
    # Some official images install the development toolkit without adding its
    # bin directory to non-login PATH. Discover that installation; never fetch
    # another toolkit or replace an explicitly selected CUDA_HOME.
    if command -v nvcc >/dev/null; then return; fi
    local cuda_candidate
    if [[ -n "${CUDA_HOME:-}" ]]; then
        [[ -x "$CUDA_HOME/bin/nvcc" ]] || {
            echo 'CUDA_HOME is set but does not contain an executable bin/nvcc' >&2; return 2;
        }
        export PATH="$CUDA_HOME/bin:$PATH"
        return
    fi
    for cuda_candidate in /usr/local/cuda /usr/local/cuda-12.8; do
        if [[ -x "$cuda_candidate/bin/nvcc" ]]; then
            export CUDA_HOME="$cuda_candidate"
            export PATH="$cuda_candidate/bin:$PATH"
            return
        fi
    done
}
ensure_cuda_path
for program in python3 cmake c++ cc nvcc nvidia-smi; do
    command -v "$program" >/dev/null || { echo "Missing $program in the selected development image" >&2; exit 2; }
done

# No provider/account state is written here. This receipt contains technical
# source/toolchain/hardware provenance only; the operator keeps billing elsewhere.
python3 -B "$appraisal_dir/cloud-bundle.py" --root "$repo_dir" --verify-source
manifest_hash=$(sha256sum "$repo_dir/BUNDLE-MANIFEST.json")
manifest_hash=${manifest_hash%% *}
mkdir -p -- "$cache_dir"
cache_dir=$(CDPATH= cd -- "$cache_dir" && pwd)
case "$cache_dir" in "$repo_dir"|"$repo_dir"/*)
    echo 'Build/cache storage must be outside the extracted source directory' >&2; exit 2;;
esac
appraisal_build="$cache_dir/build-${manifest_hash:0:16}"
mkdir -p -- "$appraisal_build"

export APPRAISAL_EXPECTED_GPU="$expected_gpu"
python3 -B - "$appraisal_build/preflight.json" <<'PY'
import csv, json, os, pathlib, re, subprocess, sys
import torch
from torch.utils import cmake_prefix_path
if torch.version.cuda is None:
    raise SystemExit("CUDA-enabled LibTorch/PyTorch is required")
if not bool(torch._C._GLIBCXX_USE_CXX11_ABI):
    raise SystemExit("The canonical native receiver requires C++11-ABI LibTorch")
query = subprocess.check_output([
    "nvidia-smi", "--query-gpu=name,memory.total,power.limit,compute_cap,driver_version,mig.mode.current",
    "--format=csv,noheader,nounits"], text=True)
rows = list(csv.reader(query.splitlines()))
if len(rows) != 1:
    raise SystemExit("This bounded cloud recipe requires exactly one visible full GPU; verify allocation")
name, memory, power, capability, driver, mig = [value.strip() for value in rows[0]]
if os.environ["APPRAISAL_EXPECTED_GPU"] not in name:
    raise SystemExit(f"GPU name does not match the selected device: {name}")
if mig.lower() not in {"disabled", "[n/a]", "n/a", "not supported", "[not supported]"}:
    raise SystemExit(f"MIG is enabled or unresolved ({mig}); require full-device allocation")
visible = os.environ.get("CUDA_VISIBLE_DEVICES", "")
if "MIG-" in visible:
    raise SystemExit("CUDA_VISIBLE_DEVICES selects a MIG partition")
if "RTX PRO 6000 Blackwell" in name and float(memory) < 90000:
    raise SystemExit("RTX PRO 6000 Blackwell memory does not match a full 96-GB device")
parts = capability.split(".")
if len(parts) != 2 or not all(p.isdigit() for p in parts):
    raise SystemExit("Cannot establish exact CUDA compute capability")
architecture = "".join(parts)
nvcc = subprocess.check_output(["nvcc", "--version"], text=True).strip()
release = re.search(r"release\s+(\d+)\.(\d+)", nvcc)
if release is None:
    raise SystemExit("Cannot establish the CUDA compiler version")
if int(architecture) >= 120 and tuple(map(int, torch.version.cuda.split(".")[:2])) < (12, 8):
    raise SystemExit("Blackwell SM120 needs a compatible CUDA>=12.8 image/toolkit")
if int(architecture) >= 120 and tuple(map(int, release.groups())) < (12, 8):
    raise SystemExit("Blackwell SM120 requires nvcc>=12.8, independently of the PyTorch runtime")
receipt = {
    "gpu_name": name, "gpu_memory_mib": float(memory), "power_limit_w": power,
    "compute_capability": capability, "cuda_architecture": architecture,
    "driver_version": driver, "mig_mode": mig, "visible_gpu_count": len(rows),
    "torch_version": torch.__version__, "torch_cuda_runtime": torch.version.cuda,
    "torch_cxx11_abi": bool(torch._C._GLIBCXX_USE_CXX11_ABI),
    "torch_cmake_prefix": cmake_prefix_path,
    "compiler": subprocess.check_output(["c++", "--version"], text=True).splitlines()[0],
    "nvcc": nvcc, "nvcc_release": ".".join(release.groups()),
    "cmake": subprocess.check_output(["cmake", "--version"], text=True).splitlines()[0],
    "device_execution_qualified": False,
    "allocation_limit": "nvidia-smi evidence; operator must also verify full allocation and exact edition in provider console",
}
pathlib.Path(sys.argv[1]).write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
print(json.dumps(receipt, sort_keys=True))
PY
unset APPRAISAL_EXPECTED_GPU
if "$preflight_only"; then exit 0; fi

torch_prefix=$(python3 -B -c 'from torch.utils import cmake_prefix_path; print(cmake_prefix_path)')
cuda_arch=$(python3 -B - "$appraisal_build/preflight.json" <<'PY'
import json, sys
print(json.load(open(sys.argv[1]))["cuda_architecture"])
PY
)
if [[ -z "$build_jobs" ]]; then build_jobs=$(python3 -B "$repo_dir/tools/build_capacity.py"); fi
export CCACHE_DIR="$cache_dir/ccache"
export CCACHE_TEMPDIR="$cache_dir/ccache-tmp"
export CCACHE_BASEDIR="$repo_dir"
unset CCACHE_READONLY CCACHE_READONLY_DIRECT
mkdir -p -- "$CCACHE_DIR" "$CCACHE_TEMPDIR"
cmake_args=()
if command -v ninja >/dev/null; then cmake_args+=(-G Ninja); fi
if command -v ccache >/dev/null; then
    cmake_args+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi
cmake -S "$repo_dir" -B "$appraisal_build" "${cmake_args[@]}" \
    -DCMAKE_BUILD_TYPE=Release -DDATAPUMP_BUILD_GUI=OFF -DDATAPUMP_BUILD_CLI=ON \
    -DDATAPUMP_BUILD_TUI=OFF -DDATAPUMP_BUILD_FB=OFF -DDATAPUMP_BUILD_WEB_WORKER=OFF \
    -DDATAPUMP_PORTABLE=OFF -DBUILD_TESTING=OFF \
    -DDATAPUMP_TRIAD_TENSOR=ON -DDATAPUMP_TRIAD_TENSOR_REQUIRE_CUDA=ON \
    -DDATAPUMP_TRIAD_VULKAN=OFF -DCMAKE_PREFIX_PATH="$torch_prefix" \
    -DCMAKE_CUDA_ARCHITECTURES="$cuda_arch" \
    -DCMAKE_PROJECT_DataPump_INCLUDE="$appraisal_dir/cmake/register.cmake"
cmake --build "$appraisal_build" --target high_fidelity_appraisal --parallel "$build_jobs"
python3 -B - "$appraisal_build/bin/high_fidelity_appraisal" "$appraisal_build/preflight.json" "$manifest_hash" <<'PY'
import hashlib, json, pathlib, sys
binary = pathlib.Path(sys.argv[1])
digest = hashlib.sha256()
with binary.open("rb") as stream:
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        digest.update(block)
print(json.dumps({"status": "built", "binary": str(binary),
    "binary_sha256": digest.hexdigest(),
    "preflight": sys.argv[2], "source_manifest_sha256": sys.argv[3],
    "gpu_execution_qualified": False, "build_testing": False}, sort_keys=True))
PY
