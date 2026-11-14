#!/bin/sh
# All configuration and output belong to this contained appraisal.
set -eu
appraisal_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$appraisal_dir/../.." && pwd)
action=${1:-build}
if [ "$#" -gt 0 ]; then shift; fi
case "$action" in build|test|configure) ;; *) echo "Usage: $0 [build|test|configure] [CMake arguments...]" >&2; exit 2;; esac
appraisal_build=${APPRAISAL_BUILD_DIR:-"$appraisal_dir/.build"}
case "$appraisal_build" in "$appraisal_dir"/.build*) ;; *) echo 'APPRAISAL_BUILD_DIR must be an appraisal-owned .build* path' >&2; exit 2;; esac
"$repo_dir/build.sh" configure --cli --build-dir "$appraisal_build" -- \
  -DCMAKE_PROJECT_DataPump_INCLUDE="$appraisal_dir/cmake/register.cmake" \
  -DDATAPUMP_PORTABLE=OFF -DBUILD_TESTING=ON "$@"
if [ "$action" = configure ]; then exit 0; fi
appraisal_jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-$(python3 -B "$repo_dir/tools/build_capacity.py")}
# Read compatible shared cache entries without writing outside this subtree.
mkdir -p "$appraisal_build/ccache-tmp"
export CCACHE_TEMPDIR="$appraisal_build/ccache-tmp"
export CCACHE_READONLY=true
cmake --build "$appraisal_build" --target high_fidelity_checks --parallel "$appraisal_jobs"
if [ "$action" = test ]; then
  ctest --test-dir "$appraisal_build" -L '^appraisal$' --output-on-failure --parallel 1
fi
