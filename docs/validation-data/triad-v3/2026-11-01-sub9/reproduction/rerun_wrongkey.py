"""Replay the failed control analyses on the same saved observations.

Original execution layout is /workspace; map these paths for another host.
The bounded parent runner owns timeout, process-group termination and logs.
"""
import concurrent.futures
import json
import pathlib
import subprocess
import time

out = pathlib.Path("/workspace/sub9-holdout-analysis/wrong-key38-corrected")
out.mkdir()
prior = json.loads(pathlib.Path("/workspace/sub9-holdout-analysis/wrong-key38/runs.json").read_text())

def run(row):
    cmd = [v.replace("/workspace/triad/tools/", "/workspace/triad-controls-fixed/tools/")
           .replace("/workspace/sub9-holdout-analysis/wrong-key38/",
                    "/workspace/sub9-holdout-analysis/wrong-key38-corrected/")
           for v in row["command"]]
    name = pathlib.Path(cmd[cmd.index("--output") + 1]).stem
    started = time.monotonic()
    with (out / (name + ".log")).open("x") as log:
        result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
    return dict(row, command=cmd, returncode=result.returncode,
                seconds=time.monotonic() - started)

with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    rows = list(pool.map(run, prior))
(out / "runs.json").write_text(json.dumps(rows, indent=2))
raise SystemExit(any(row["returncode"] for row in rows))
