#!/bin/sh
# A small, offline entry point; CMake remains the build system.
set -eu

usage() {
    cat <<'HELP'
Usage: ./build.sh [build|test GROUP|sanitize [GROUP]|package] [OPTIONS] [-- CMAKE_ARGS...]

  build                Build pump and the FLTK GUI (default).
  test GROUP           Build then run contract, regular, fast, legacy, gui,
                       native, packaging, build, or all tests.
  sanitize [GROUP]     Run instrumented headless tests (default: contract).
  package              Build and verify portable TGZ and ZIP bundles.

  --cli                Omit the native GUI (shared GUI tests remain available).
  --backend fltk|rev   Select a GUI backend; Rev needs its own suitable toolchain.
  --jobs N, -j N       Parallel build/test limit (default: 2).
  --build-dir PATH     Separate output tree, e.g. for another compiler/toolchain.
  --sdk PATH           Use a prepared source SDK; keep host dependencies separate.
  --help, -h           Show this help.

Environment: DATAPUMP_JOBS (or CMAKE_BUILD_PARALLEL_LEVEL), CC, CXX,
CMAKE_GENERATOR, DATAPUMP_MAX_GLIBC (optional explicit package ABI ceiling).
Ninja is preferred for new trees; Unix Makefiles is the fallback. No downloads.
Examples:
  ./build.sh test contract --jobs 4
  ./build.sh --cli -- -DDATAPUMP_COMPILER_CACHE=OFF
  ./build.sh --build-dir build/custom -- -DCMAKE_TOOLCHAIN_FILE=/path/toolchain.cmake
HELP
}

die() { printf '%s\n' "build.sh: $*" >&2; exit 2; }
need_value() { [ "$#" -ge 2 ] || die "$1 needs a value"; }

command_name=build
command_seen=no
group=
backend=fltk
backend_explicit=no
cli=no
jobs=${DATAPUMP_JOBS:-${CMAKE_BUILD_PARALLEL_LEVEL:-2}}
build_dir=
sdk_root=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --cli) cli=yes; shift ;;
        --backend) need_value "$@"; backend=$2; backend_explicit=yes; shift 2 ;;
        --jobs|-j) need_value "$@"; jobs=$2; shift 2 ;;
        --build-dir) need_value "$@"; build_dir=$2; shift 2 ;;
        --sdk) need_value "$@"; [ -n "$2" ] || die "--sdk needs a nonempty path"; sdk_root=$2; shift 2 ;;
        --) shift; break ;;
        build|test|sanitize|package)
            if [ "$command_seen" = no ]; then
                command_name=$1; command_seen=yes
            elif [ "$1" = build ] && [ -z "$group" ] &&
                 { [ "$command_name" = test ] || [ "$command_name" = sanitize ]; }; then
                group=$1
            else die "unexpected command: $1"; fi
            shift ;;
        -*) die "unknown option: $1 (pass CMake options after --)" ;;
        *)
            [ -z "$group" ] || die "unexpected argument: $1"
            case "$command_name" in test|sanitize) group=$1 ;; *) die "unexpected argument: $1" ;; esac
            shift ;;
    esac
done
case "$backend" in fltk|rev) ;; *) die "backend must be fltk or rev" ;; esac
case "$jobs" in ''|*[!0-9]*|0) die "jobs must be a positive integer" ;; esac
[ "$jobs" -gt 0 ] || die "jobs must be a positive integer"
if [ -n "${DATAPUMP_MAX_GLIBC:-}" ]; then
    case "$DATAPUMP_MAX_GLIBC" in *[!0-9.]*|.*|*..*|*.) die "DATAPUMP_MAX_GLIBC must be a dotted version, e.g. 2.35" ;; esac
    case "$DATAPUMP_MAX_GLIBC" in *.*) ;; *) die "DATAPUMP_MAX_GLIBC must be a dotted version, e.g. 2.35" ;; esac
fi
case "$command_name" in
    test) [ -n "$group" ] || die "test requires a group; see --help" ;;
    sanitize) group=${group:-contract} ;;
esac
if [ -n "$group" ]; then
    case "$group" in contract|regular|fast|legacy|gui|native|packaging|build|all) ;;
        *) die "unknown test group: $group" ;;
    esac
fi
[ "$group" != native ] || [ "$cli" != yes ] || die "native tests require the GUI; omit --cli"

# Resolve paths before changing directory so invocation from elsewhere works.
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
if [ -n "$build_dir" ]; then
    case "$build_dir" in /*) ;; *) build_dir=$PWD/$build_dir ;; esac
fi
if [ -n "$sdk_root" ]; then
    [ -d "$sdk_root" ] || die "SDK directory is missing: $sdk_root"
    sdk_root=$(CDPATH= cd -- "$sdk_root" && pwd -P)
    [ -f "$sdk_root/share/datapump-sdk/manifest.json" ] ||
        die "SDK manifest is missing; prepare the SDK explicitly before building"
    relocated_root=
    if [ -f "$sdk_root/share/datapump-sdk/relocated-root.txt" ]; then
        IFS= read -r relocated_root < "$sdk_root/share/datapump-sdk/relocated-root.txt" || :
    fi
    [ "$relocated_root" = "$sdk_root" ] ||
        die "SDK needs explicit installation/relocation; use tools/build-sdk.py install before building"
    [ -z "${CC:-}${CXX:-}${CMAKE_TOOLCHAIN_FILE:-}" ] ||
        die "--sdk selects its own compiler and toolchain; unset CC, CXX and CMAKE_TOOLCHAIN_FILE"
    # GCC can search these outside --sysroot, making the result host-dependent.
    [ -z "${CPATH:-}${C_INCLUDE_PATH:-}${CPLUS_INCLUDE_PATH:-}${OBJC_INCLUDE_PATH:-}${LIBRARY_PATH:-}${GCC_EXEC_PREFIX:-}${COMPILER_PATH:-}" ] ||
        die "--sdk requires compiler search-path environment overrides to be unset"
    PATH=$sdk_root/bin:$PATH
    export PATH
fi
cd "$source_dir"
preset=dev
build_config=Release
gui=ON
portable=OFF
native=OFF
case "$command_name" in
    package) preset=release; portable=ON ;;
    sanitize) preset=sanitize; build_config=Debug; gui=OFF ;;
esac
if [ "$backend" = rev ]; then
    [ "$preset" != dev ] || preset=rev
    gui=ON
fi
if [ "$backend_explicit" = yes ] || [ "$group" = native ]; then gui=ON; fi
if [ "$cli" = yes ]; then gui=OFF; fi
if [ "$group" = native ]; then native=ON; fi
if [ "$group" = packaging ]; then portable=ON; fi
if [ -z "$build_dir" ]; then
    build_dir=$source_dir/build/$preset
    if [ "$group" = packaging ]; then
        build_dir=$source_dir/build/package-tests
        if [ "$preset" = sanitize ]; then build_dir=$source_dir/build/sanitize-package-tests; fi
        if [ "$backend" = rev ]; then build_dir=$build_dir-rev; fi
    fi
    if [ "$backend" = rev ] && [ "$preset" != rev ] && [ "$group" != packaging ]; then
        build_dir=$build_dir-rev
    fi
    # Keep CLI variants apart from normal GUI output and native sanitizer builds.
    if [ "$cli" = yes ] && [ "$preset" != sanitize ]; then build_dir=$build_dir-cli; fi
    if [ "$preset" = sanitize ] && [ "$gui" = ON ] && [ "$backend" = fltk ]; then
        build_dir=$build_dir-gui
    fi
    if [ -n "$sdk_root" ]; then build_dir=$build_dir-sdk; fi
fi

# Never reuse cached native paths with an SDK, or vice versa. CMake also checks
# the SDK manifest fingerprint so upgrading it in place needs a fresh tree.
if [ -f "$build_dir/CMakeCache.txt" ]; then
    cached_sdk=$(sed -n 's/^DATAPUMP_CONFIGURED_SDK_ROOT:[^=]*=//p' "$build_dir/CMakeCache.txt")
    [ "$cached_sdk" = "$sdk_root" ] ||
        die "SDK differs from the configured tree; use --build-dir with a new directory"
fi

command -v cmake >/dev/null 2>&1 || die "CMake 3.21 or newer is required"
case "$command_name" in test|sanitize)
    command -v ctest >/dev/null 2>&1 || die "CTest is required" ;;
esac

# Honor a cached generator; selecting Ninja again would reject an existing Make tree.
generator=${CMAKE_GENERATOR:-}
cmake_define_next=no
for arg do
    option=$arg
    if [ "$cmake_define_next" = yes ]; then option=-D$arg; cmake_define_next=no; fi
    if [ "$arg" = -D ]; then cmake_define_next=yes; fi
    case "$arg" in
        -G|-G?*) generator=explicit ;;
        -B|-B?*|-S|-S?*|--preset|--preset=*)
            die "use --build-dir for output paths; source and preset are selected by this script" ;;
    esac
    if [ -n "$sdk_root" ]; then
        case "$option" in
            --toolchain|--toolchain=*|-DCMAKE_TOOLCHAIN_FILE=*|-DCMAKE_TOOLCHAIN_FILE:*=*|\
            -DCMAKE_C_COMPILER=*|-DCMAKE_C_COMPILER:*=*|-DCMAKE_CXX_COMPILER=*|-DCMAKE_CXX_COMPILER:*=*|\
            -DCMAKE_SYSROOT=*|-DCMAKE_SYSROOT:*=*|-DDATAPUMP_SDK_ROOT=*|-DDATAPUMP_SDK_ROOT:*=*|\
            -DDATAPUMP_DEPENDENCY_PREFIX=*|-DDATAPUMP_DEPENDENCY_PREFIX:*=*)
                die "--sdk selects the compiler, toolchain and dependency root; remove the competing CMake option: $option" ;;
        esac
    fi
done
if [ -z "$generator" ] && [ ! -f "$build_dir/CMakeCache.txt" ]; then
    if command -v ninja >/dev/null 2>&1; then generator=Ninja; else generator='Unix Makefiles'; fi
fi
if [ -n "$generator" ] && [ "$generator" != explicit ]; then set -- -G "$generator" "$@"; fi
if [ -n "$sdk_root" ]; then
    set -- "-DCMAKE_TOOLCHAIN_FILE=$source_dir/cmake/toolchains/source-sdk.cmake" \
        "-DDATAPUMP_SDK_ROOT=$sdk_root" "$@"
fi

# Match CMake's simple compiler-name + raw PROGRAM_ARGS form without evaluating
# shell text. Compiler arguments can contain quotes; they are compared verbatim.
compiler_environment() {
    requested_compiler=$(printf '%s' "$1" | sed 's/^[[:space:]]*//')
    requested_arguments=
    case "$requested_compiler" in
        \"*|\'*)
            quote=${requested_compiler%"${requested_compiler#?}"}
            remainder=${requested_compiler#?}
            case "$remainder" in
                *"$quote"*)
                    requested_compiler=${remainder%%"$quote"*}
                    requested_arguments=${remainder#*"$quote"} ;;
                *) die "unclosed compiler-name quote in CC/CXX" ;;
            esac ;;
        *)
            # An existing executable path may itself contain unquoted spaces.
            resolved=$(command -v "$requested_compiler" 2>/dev/null || :)
            if [ -z "$resolved" ]; then
                remainder=$requested_compiler
                requested_compiler=${remainder%%[[:space:]]*}
                requested_arguments=${remainder#"$requested_compiler"}
            fi ;;
    esac
    case "$requested_compiler" in
        ''|*\"*|*\'*|*\\*) die "unsupported compiler-name quoting in CC/CXX; use a plain executable path or quote the whole path" ;;
    esac
    case "$requested_arguments" in
        ''|[[:space:]]*) ;;
        *) die "unsupported compiler-name quoting in CC/CXX; put whitespace between the executable and arguments" ;;
    esac
}

# CMake otherwise silently ignores a changed CC/CXX environment in a cached tree.
check_compiler() {
    requested_compiler=$1
    language=$2
    kind=$3
    [ -n "$requested_compiler" ] || return 0
    if [ "$kind" = environment ]; then compiler_environment "$requested_compiler"; fi
    [ -f "$build_dir/CMakeCache.txt" ] || return 0
    cached=$(sed -n "s/^CMAKE_${language}_COMPILER:[^=]*=//p" "$build_dir/CMakeCache.txt")
    [ -n "$cached" ] || return 0
    resolved=$(command -v "$requested_compiler" 2>/dev/null || :)
    [ "$requested_compiler" = "$cached" ] || [ "$resolved" = "$cached" ] ||
        die "${language} compiler differs from $cached; use --build-dir with a new directory when changing CC/CXX"
    if [ "$kind" = environment ]; then
        cached_arguments=$(sed -n "s/^CMAKE_${language}_COMPILER_ARG1:[^=]*=//p" "$build_dir/CMakeCache.txt")
        [ "$requested_arguments" = "$cached_arguments" ] ||
            die "${language} compiler arguments differ from the configured tree; use --build-dir with a new directory when changing CC/CXX"
    fi
}
check_compiler "${CC:-}" C environment
check_compiler "${CXX:-}" CXX environment
for arg do
    case "$arg" in
        -DCMAKE_C_COMPILER=*|-DCMAKE_C_COMPILER:*=*) check_compiler "${arg#*=}" C path ;;
        -DCMAKE_CXX_COMPILER=*|-DCMAKE_CXX_COMPILER:*=*) check_compiler "${arg#*=}" CXX path ;;
    esac
done

printf 'Configuring %s in %s\n' "$preset" "$build_dir"
if ! cmake --preset "$preset" -S "$source_dir" -B "$build_dir" \
    "-DDATAPUMP_BUILD_GUI=$gui" "-DDATAPUMP_GUI_BACKEND=$backend" \
    "-DDATAPUMP_PORTABLE=$portable" "-DOPENSSL_USE_STATIC_LIBS=$portable" \
    "-DDATAPUMP_TEST_NATIVE_GUI=$native" "$@"; then
    die "configuration failed; check dependencies and use a new --build-dir for a different compiler/generator"
fi

target=datapump-apps
if [ -n "$group" ]; then target=datapump-tests-$group; fi
if [ "$group" = all ]; then target=datapump-tests; fi
if [ "$command_name" = package ]; then target=package; fi
cmake --build "$build_dir" --config "$build_config" --target "$target" --parallel "$jobs"

if [ -n "$group" ]; then
    label=$group
    if [ "$group" = native ]; then label=native_gui; fi
    if [ "$group" = all ]; then
        ctest --test-dir "$build_dir" -C "$build_config" --output-on-failure \
            --no-tests=error --parallel "$jobs" -LE native_gui
    elif [ "$group" = native ]; then
        ctest --test-dir "$build_dir" -C "$build_config" --output-on-failure \
            --no-tests=error --parallel "$jobs" -L "^$label$"
    else
        ctest --test-dir "$build_dir" -C "$build_config" --output-on-failure \
            --no-tests=error --parallel "$jobs" -L "^$label$" -LE native_gui
    fi
elif [ "$command_name" = package ]; then
    set -- "-DARCHIVE_DIR=$build_dir/releases" "-DBUILD_DIR=$build_dir" -DGUI_SMOKE=OFF
    if [ -n "${DATAPUMP_MAX_GLIBC:-}" ]; then
        set -- "$@" "-DMAX_GLIBC=$DATAPUMP_MAX_GLIBC"
    elif [ -n "$sdk_root" ]; then
        [ -f "$build_dir/CMakeCache.txt" ] || die "SDK configuration cache is missing"
        sdk_glibc=$(sed -n 's/^DATAPUMP_SDK_GLIBC_MAX:[^=]*=//p' "$build_dir/CMakeCache.txt")
        [ -n "$sdk_glibc" ] || die "SDK configuration did not provide its glibc baseline"
        set -- "$@" "-DMAX_GLIBC=$sdk_glibc"
    fi
    # These are newly generated local archives, not downloaded release artifacts.
    rm -f "$build_dir/releases/SHA256SUMS.txt"
    cmake "$@" -P "$source_dir/tools/verify-native-archives.cmake"
    printf 'Verified portable archives: %s/releases\n' "$build_dir"
else
    executable_dir=$build_dir
    if [ -f "$build_dir/CMakeCache.txt" ]; then
        configurations=$(sed -n 's/^CMAKE_CONFIGURATION_TYPES:[^=]*=//p' "$build_dir/CMakeCache.txt")
        if [ -n "$configurations" ]; then executable_dir=$build_dir/$build_config; fi
    fi
    printf 'Application: %s/pump\n' "$executable_dir"
    if [ "$gui" = ON ]; then printf 'GUI: %s/datapump-gui\n' "$executable_dir"; fi
fi
