#!/bin/sh
# Container bootstrap: signed distribution mirrors and bounded APT recovery.
# Requires only the base image's shell, coreutils, sed, grep, dpkg and apt-get.
# CI_APT_ROOT is a source-configuration fixture root for offline tests; APT and
# sleep can be replaced through PATH. It is not an installation/chroot prefix.
set -eu

usage() {
    echo "usage: $0 configure | update | install PACKAGE..." >&2
    exit 2
}
[ "$#" -gt 0 ] || usage
operation=$1
shift

# Opt-in acquisition watchdog is validated before configure can mutate APT
# configuration. Empty, malformed, zero, and unsupported watchdogs fail closed.
download_timeout_enabled=0
download_timeout=
if [ "${CI_APT_DOWNLOAD_TIMEOUT+x}" = x ]; then
    download_timeout=$CI_APT_DOWNLOAD_TIMEOUT
    case "$download_timeout" in
        ''|*[!0-9]*) echo 'APT: CI_APT_DOWNLOAD_TIMEOUT must be a positive integer number of seconds.' >&2; exit 2 ;;
    esac
    timeout_digits=$(printf '%s' "$download_timeout" | sed 's/^0*//')
    [ -n "$timeout_digits" ] || { echo 'APT: CI_APT_DOWNLOAD_TIMEOUT must be greater than zero.' >&2; exit 2; }
    timeout_bin=$(command -v timeout 2>/dev/null || true)
    if [ -z "$timeout_bin" ] || ! LC_ALL=C "$timeout_bin" --version 2>/dev/null | grep -q 'GNU coreutils' ||
       ! LC_ALL=C "$timeout_bin" --help 2>/dev/null | grep -q -- '--verbose'; then
        echo 'APT: CI_APT_DOWNLOAD_TIMEOUT requires GNU coreutils timeout; refusing to continue.' >&2
        exit 2
    fi
    download_timeout_enabled=1
fi
case "$operation" in
    configure|update) [ "$#" -eq 0 ] || usage ;;
    install) [ "$#" -gt 0 ] || usage ;;
    *) usage ;;
esac

root=${CI_APT_ROOT:-}
case "$root" in
    ''|/*) ;;
    *) echo 'CI_APT_ROOT must be an absolute fixture path' >&2; exit 2 ;;
esac
apt_dir=$root/etc/apt
scratch=$(mktemp -d)
acquisition_pid=
acquisition_reaped=0
acquisition_tee_pid=
close_acquisition() {
    trap '' INT TERM HUP
    # Download-only work may be killed on cancellation. Never use this for
    # offline dpkg installation, which has no acquisition process owner.
    if [ -n "$acquisition_pid" ]; then
        kill -KILL "-$acquisition_pid" 2>/dev/null || true
        if [ "$acquisition_reaped" -eq 0 ]; then
            kill -KILL "$acquisition_pid" 2>/dev/null || true
            wait "$acquisition_pid" 2>/dev/null || true
            # Cover cancellation before timeout establishes its process group:
            # its owned launcher is now reaped and cannot create new workers.
            kill -KILL "-$acquisition_pid" 2>/dev/null || true
        fi
        acquisition_pid=
    fi
    if [ -n "$acquisition_tee_pid" ]; then
        kill -TERM "$acquisition_tee_pid" 2>/dev/null || true
        wait "$acquisition_tee_pid" 2>/dev/null || true
        acquisition_tee_pid=
    fi
}
trap 'close_acquisition; rm -rf "$scratch"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP

# Keep HTTP bootstrap usable before ca-certificates is installed. Existing
# HTTPS sources receive HTTPS-only mirrors, so configuration never drops TLS.
# APT continues to verify the original archive signatures and package hashes.
mirror_list() {
    list_name=$1
    list_scheme=$2
    shift 2
    list_path=$apt_dir/ci-$list_name-$list_scheme.mirrors
    : > "$list_path"
    priority=1
    for mirror in "$@"; do
        printf '%s://%s\tpriority:%s\n' "$list_scheme" "$mirror" "$priority" >> "$list_path"
        priority=$((priority + 1))
    done
    chmod 644 "$list_path"
    printf '%s\n' "$list_path" >> "$scratch/managed-mirrors"
}

map_uri() {
    uri_pattern=$1
    list_name=$2
    for scheme in http https; do
        # Escape only the replacement: URI patterns below are fixed literals.
        replacement=$(printf '%s' "mirror+file:$apt_dir/ci-$list_name-$scheme.mirrors" | sed 's/[\\&@]/\\&/g')
        mapping=$((mapping + 1))
        # Require a complete URI token, not a known URL inside a third-party
        # URI's path/query. Repeat for adjacent URIs sharing a space in deb822.
        printf ':uri%s\ns@([[:space:]])%s://%s/?([[:space:]]|$)@\\1%s\\2@g\nt uri%s\n' \
            "$mapping" "$scheme" "$uri_pattern" "$replacement" "$mapping" >> "$scratch/rewrite.sed"
    done
}

configure() {
    [ -f "$root/etc/os-release" ] || return 0
    distro=$(sed -n 's/^ID=//p' "$root/etc/os-release" | tr -d '\042\047')
    codename=$(sed -n 's/^VERSION_CODENAME=//p' "$root/etc/os-release" | tr -d '\042\047')
    architecture=$(dpkg --print-architecture)
    mkdir -p "$apt_dir/apt.conf.d"
    # Released source may still invoke apt-get directly later in the job.
    # Persist acquisition policy for those calls as well as this wrapper.
    cat > "$apt_dir/apt.conf.d/99datapump-ci" <<'APTCONFIG'
Acquire::Retries "3";
Acquire::http::Timeout "30";
Acquire::https::Timeout "30";
APT::Update::Error-Mode "any";
APTCONFIG
    # Only rewrite URI fields on active APT source lines. Preserve all options,
    # suites, components, Signed-By fields and unrelated repositories verbatim.
    printf '/^[[:space:]]*(deb(-src)?[[:space:]]|URIs:[[:space:]])/ {\n' > "$scratch/rewrite.sed"
    mapping=0
    case "$distro:$architecture:$codename" in
        ubuntu:amd64:*|ubuntu:arm64:resolute)
            for scheme in http https; do
                mirror_list ubuntu "$scheme" azure.archive.ubuntu.com/ubuntu/ archive.ubuntu.com/ubuntu/ security.ubuntu.com/ubuntu/
            done
            map_uri 'archive\.ubuntu\.com/ubuntu' ubuntu
            map_uri 'security\.ubuntu\.com/ubuntu' ubuntu
            map_uri 'azure\.archive\.ubuntu\.com/ubuntu' ubuntu
            echo 'APT: prefer the Azure Ubuntu mirror with archive/security fallback.'
            ;;
        ubuntu:*)
            # Older arm64 and other ports architectures require ports mirrors.
            # Do not guess that the primary/Azure archive serves their suites.
            echo 'APT: preserving Ubuntu ports/other architecture sources.'
            ;;
        debian:*)
            for scheme in http https; do
                mirror_list debian "$scheme" deb.debian.org/debian/ ftp.debian.org/debian/
                mirror_list debian-security "$scheme" deb.debian.org/debian-security/ security.debian.org/debian-security/
            done
            map_uri 'deb\.debian\.org/debian-security' debian-security
            map_uri 'security\.debian\.org/debian-security' debian-security
            map_uri 'deb\.debian\.org/debian' debian
            map_uri 'ftp\.debian\.org/debian' debian
            echo 'APT: prefer the Debian CDN with separate archive/security fallback.'
            ;;
        *) echo "APT: preserving sources for $distro." ;;
    esac
    printf '}\n' >> "$scratch/rewrite.sed"
    for source in "$apt_dir/sources.list" "$apt_dir"/sources.list.d/*.list "$apt_dir"/sources.list.d/*.sources; do
        [ -f "$source" ] || continue
        sed -i -E -f "$scratch/rewrite.sed" "$source"
    done
}

run_apt() {
    if [ "$attempt" -gt 1 ]; then
        # Revalidate retry metadata even through an intermediary cache.
        set -- -o Acquire::http::No-Cache=true -o Acquire::https::No-Cache=true "$@"
    fi
    # A pipeline keeps installation progress visible while preserving APT's
    # status in POSIX sh, which has no portable pipefail option.
    {
        apt_result=0
        LC_ALL=C apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=30 \
            -o Acquire::https::Timeout=30 -o APT::Update::Error-Mode=any "$@" || apt_result=$?
        printf '%s\n' "$apt_result" > "$scratch/status"
    } 2>&1 | tee "$scratch/apt.log"
    apt_result=$(cat "$scratch/status")
    return "$apt_result"
}

fatal_apt_failure() {
    # Never recover authentication, repository policy, package resolution or
    # dpkg failures. Unknown failures also remain fatal.
    grep -Eiq 'NO_PUBKEY|BADSIG|EXPKEYSIG|signature|not signed|not authenticated|unauthenticated|does not have a Release file|Release file.*(expired|not valid)|certificate verification|Certificate verification|Hash Sum mismatch|Hashes of expected file|unexpected size|dpkg:|Sub-process .*/dpkg|unmet dependencies|Unable to locate package|has no installation candidate|held broken packages' "$scratch/apt.log"
}

fetch_failure() {
    fatal_apt_failure && return 1
    grep -Eiq '(Failed to fetch|Failed to download|Unable to fetch).*(404|408|429|500|502|503|504|Not Found|timed out|Timeout|Temporary failure|Could not resolve|Could not connect|Unable to connect|Connection|Network is unreachable|TLS connection|TLS handshake)|Temporary failure resolving|Could not resolve|Could not connect|Unable to connect|Connection (failed|timed out|reset|refused)|Network is unreachable' "$scratch/apt.log"
}

unclassified_acquisition_error() {
    grep -E '^(E:|Err:|W: GPG error:)' "$scratch/acquisition.log" > "$scratch/typed-errors" || return 1
    while IFS= read -r error_line; do
        case "$error_line" in
            'E: Unable to fetch some archives, maybe run apt-get update or try with --fix-missing?') continue ;;
            'E: Failed to fetch '*|'E: Failed to download '*|'E: Unable to fetch '*)
                printf '%s\n' "$error_line" > "$scratch/one-error"
                if grep -Eiq '(404|408|429|500|502|503|504|Not Found|timed out|Timeout|Temporary failure|Could not resolve|Could not connect|Unable to connect|Connection|Network is unreachable|TLS connection|TLS handshake)' "$scratch/one-error"; then
                    continue
                fi ;;
        esac
        return 0
    done < "$scratch/typed-errors"
    return 1
}

rotate_official_mirrors() {
    rotated=0
    [ -f "$scratch/managed-mirrors" ] || return 0
    while IFS= read -r mirror_file; do
        [ -f "$mirror_file" ] || continue
        first=
        count=0
        temporary_file=$scratch/mirrors.$rotated
        : > "$temporary_file"
        while IFS= read -r mirror_line || [ -n "$mirror_line" ]; do
            [ -n "$mirror_line" ] || continue
            mirror_uri=${mirror_line%%	*}
            count=$((count + 1))
            if [ "$count" -eq 1 ]; then
                first=$mirror_uri
            else
                priority=$((count - 1))
                printf '%s\tpriority:%s\n' "$mirror_uri" "$priority" >> "$temporary_file"
            fi
        done < "$mirror_file"
        if [ "$count" -gt 1 ]; then
            printf '%s\tpriority:%s\n' "$first" "$count" >> "$temporary_file"
            cat "$temporary_file" > "$mirror_file"
            rotated=$((rotated + 1))
        fi
    done < "$scratch/managed-mirrors"
}

run_acquisition() {
    # Run apt-get directly under coreutils timeout so the watchdog owns and
    # reaps the acquisition process. Its verbose diagnostic distinguishes a
    # watchdog expiry (including status 124) from apt-get returning 124 itself.
    if [ "$attempt" -gt 1 ]; then
        set -- -o Acquire::http::No-Cache=true -o Acquire::https::No-Cache=true "$@"
    fi
    : > "$scratch/acquisition-stderr.log"
    : > "$scratch/watchdog.log"
    # Keep the watchdog as a directly owned background child. Waiting on an
    # asynchronous child lets POSIX sh handle parent signals immediately; an
    # ordinary foreground pipeline can defer the trap until APT finishes.
    mkfifo "$scratch/acquisition-stdout"
    # Catch cancellation without exiting between launch and $! capture. Exec
    # children receive default handlers; no signal is silently ignored.
    launch_signal=
    trap 'launch_signal=130' INT
    trap 'launch_signal=143' TERM HUP
    acquisition_reaped=0
    tee "$scratch/acquisition.log" < "$scratch/acquisition-stdout" &
    acquisition_tee_pid=$!
    LC_ALL=C "$timeout_bin" --verbose --signal=TERM --kill-after=10s \
        "$download_timeout" sh -c 'exec "$@" 2>"$0"' "$scratch/acquisition-stderr.log" \
        apt-get -o Acquire::Retries=3 -o Acquire::http::Timeout=30 \
        -o Acquire::https::Timeout=30 -o APT::Update::Error-Mode=any \
        install --download-only -y --no-install-recommends "$@" \
        > "$scratch/acquisition-stdout" 2> "$scratch/watchdog.log" &
    acquisition_pid=$!
    trap 'exit 130' INT
    trap 'exit 143' TERM HUP
    [ -z "$launch_signal" ] || exit "$launch_signal"
    acquisition_result=0
    wait "$acquisition_pid" || acquisition_result=$?
    acquisition_reaped=1
    # APT's main process may exit while a TERM-resistant worker still holds
    # stdout. Close the owned group before waiting for FIFO EOF; retain its
    # identity through the reader's closure, including cancellation here.
    kill -KILL "-$acquisition_pid" 2>/dev/null || true
    wait "$acquisition_tee_pid"
    acquisition_tee_pid=
    acquisition_pid=
    rm "$scratch/acquisition-stdout"
    cat "$scratch/acquisition-stderr.log" "$scratch/watchdog.log" | tee -a "$scratch/acquisition.log"
    cp "$scratch/acquisition.log" "$scratch/apt.log"
    if { [ "$acquisition_result" -eq 124 ] || [ "$acquisition_result" -eq 137 ]; } &&
       grep -Fq 'timeout: sending signal TERM to command' "$scratch/watchdog.log"; then
        watchdog_expired=1
    else
        watchdog_expired=0
    fi
    return "$acquisition_result"
}

configure
[ "$operation" != configure ] || exit 0
attempt=1
while :; do
    echo "APT: $operation attempt $attempt/3 (fresh strict package indexes)."
    result=0
    if run_apt update; then
        if [ "$operation" = update ]; then exit 0; fi
        if [ "$download_timeout_enabled" -eq 1 ]; then
            if run_acquisition "$@"; then
                # Do not refresh indexes here: Docker hooks may remove cached
                # .debs. Installing offline must immediately follow acquisition.
                run_apt install --no-download -y --no-install-recommends "$@" || result=$?
                [ "$result" -ne 0 ] || exit 0
            else
                result=$?
                if unclassified_acquisition_error; then
                    echo 'APT: acquisition reported an unclassified error; refusing recovery.' >&2
                    exit "$result"
                fi
                if [ "$watchdog_expired" -eq 1 ]; then
                    if fatal_apt_failure; then
                        echo 'APT: watchdog expired after a fatal repository/package error; refusing recovery.' >&2
                        exit "$result"
                    fi
                    echo "::warning::APT download acquisition exceeded ${download_timeout}s; rotating official mirrors before retry."
                    rotate_official_mirrors
                    timeout_recovery=1
                elif ! fetch_failure; then
                    echo 'APT: non-fetch acquisition failure; refusing to retry.' >&2
                    exit "$result"
                fi
            fi
        else
            run_apt install -y --no-install-recommends "$@" || result=$?
            [ "$result" -ne 0 ] || exit 0
        fi
    else
        result=$?
    fi
    [ "$result" -ne 0 ] || exit 0
    if [ "$attempt" -ge 3 ]; then
        if [ "${timeout_recovery:-0}" -eq 1 ]; then
            echo 'APT: download watchdog recovery exhausted after 3 attempts.' >&2
        else
            echo 'APT: fetch recovery exhausted after 3 attempts.' >&2
        fi
        exit "$result"
    fi
    if [ "${timeout_recovery:-0}" -eq 1 ]; then
        timeout_recovery=0
    else
        if ! fetch_failure; then
            echo 'APT: non-fetch failure; refusing to retry.' >&2
            exit "$result"
        fi
        delay=5
        [ "$attempt" -lt 2 ] || delay=15
        echo "::warning::APT fetch failed; refreshing indexes and retrying in $delay seconds."
        sleep "$delay"
    fi
    attempt=$((attempt + 1))
done
