#!/bin/sh
# Diagnostic only: no archive edits, dependency builds or certification claim.
set -eu
package_root=$1
diagnostic_log=$2
test -f "$package_root/manifest.sha256"
test -x "$package_root/bin/datapump-gui"
printf '%s\n' 'Host software GLX implementation:'
timeout 30s glxinfo -B || printf '%s\n' 'Host GLX inspection failed; continuing to capture application diagnostics.'
printf '%s\n' 'Published executable and bundled runtime inventory:'
readelf -d "$package_root/bin/datapump-gui"
cat "$package_root/manifest.sha256"
set +e
timeout -k 2s 15s env PATH= LD_LIBRARY_PATH= PYTHONHOME=/nonexistent PYTHONPATH=/nonexistent \
  LIBGL_DEBUG=verbose LD_DEBUG=libs,versions \
  "$package_root/bin/datapump-gui" --smoke-test --smoke-timeout 10 >"$diagnostic_log" 2>&1
diagnostic_status=$?
set -e
printf 'Bounded startup diagnostic exit: %s (a timeout or partial smoke is not a certification pass)\n' "$diagnostic_status"
python3 - "$diagnostic_log" <<'PY'
from pathlib import Path
import sys
lines = Path(sys.argv[1]).read_text(errors='replace').splitlines()
needles = ('error', 'failed', 'not found', 'undefined', 'libgl', 'libegl', 'libstdc++',
           'libgcc', 'libllvm', 'dri/', 'dri.so', 'swrast', 'nativewindow', 'warning')
selected = [line for line in lines if any(word in line.lower() for word in needles)]
print('\n'.join(selected[-250:]))
print('Final application/loader output:')
print('\n'.join(lines[-40:]))
PY
exit "$diagnostic_status"
