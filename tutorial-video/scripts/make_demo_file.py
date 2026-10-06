#!/usr/bin/env python3
"""Create the deliberately compressible 30,000-byte tutorial fixture externally."""
import argparse
import hashlib
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("output", type=Path)
a = p.parse_args()
out = a.output.resolve()
repo = Path(__file__).resolve().parents[2]
if out.is_relative_to(repo) or out.exists():
    p.error("Choose a new output file outside the repository")
line = b"Data Pump tutorial: this intentionally compressible file is exactly 30,000 bytes.\n"
data = (line * (30000 // len(line) + 1))[:30000]
out.parent.mkdir(parents=True, exist_ok=True)
with out.open("xb") as f:
    f.write(data)
print(len(data), hashlib.sha256(data).hexdigest())
