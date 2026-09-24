"""Keep the recovery word out of stored program/source bytes at every bit phase."""
import hashlib
from pathlib import Path
import sys


def contains_word(data, word):
    if word in data:
        return True
    for phase in range(1, 8):
        shifted = (int.from_bytes(word, "big") << (8 - phase)).to_bytes(len(word) + 1, "big")
        middle = shifted[1:-1]
        start = data.find(middle, 1)
        while start != -1:
            end = start + len(middle)
            if (end < len(data)
                    and data[start - 1] & (255 >> phase) == shifted[0]
                    and data[end] & ((255 << (8 - phase)) & 255) == shifted[-1]):
                return True
            start = data.find(middle, start + 1)
    return False


word = hashlib.sha256(b"DataPump/byte-boundary/v1").digest()[:12]
# Verify the scanner against deliberately non-byte-aligned, runtime-only words.
for phase in range(8):
    bit_string = "1" * phase + "".join(f"{byte:08b}" for byte in word)
    bit_string += "1" * ((-len(bit_string)) % 8)
    fixture = int(bit_string, 2).to_bytes(len(bit_string) // 8, "big")
    assert contains_word(fixture, word), f"scanner missed bit phase {phase}"

executable, root = map(Path, sys.argv[1:3])
paths = [executable, root / "README.md", root / "CMakeLists.txt"]
for directory in ("src", "include", "tests", "docs"):
    paths.extend(path for path in (root / directory).rglob("*")
                 if path.is_file() and "__pycache__" not in path.parts)
for path in paths:
    if contains_word(path.read_bytes(), word):
        raise SystemExit(f"Stored recovery word found in {path}; keep its wire form runtime-only")
print(f"Recovery word absent from executable and {len(paths) - 1} source/document files at all bit phases")
