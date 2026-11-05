#!/usr/bin/env python3
"""Verify compact evidence without extraction, simulation or numerical libraries."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile


def main():
    root = Path(__file__).resolve().parent
    archive = root / 'evidence.tar.xz'
    expected_sha, expected_name = (root / 'SHA256SUMS').read_text().split()
    if expected_name != archive.name or hashlib.sha256(archive.read_bytes()).hexdigest() != expected_sha:
        raise ValueError('archive checksum mismatch')
    binding_bytes = (root / 'source-bindings.json').read_bytes()
    bindings = json.loads(binding_bytes)
    with tarfile.open(archive, 'r:xz') as stream:
        members = stream.getmembers()
        names = [member.name for member in members]
        if len(names) != len(set(names)) or set(names) != set(bindings['files']) | {'source-bindings.json'}:
            raise ValueError('unexpected, duplicate or missing archive member')
        for member in members:
            path = PurePosixPath(member.name)
            if not member.isfile() or path.is_absolute() or '..' in path.parts:
                raise ValueError(f'unsafe member: {member.name}')
            data = stream.extractfile(member).read()
            if member.name == 'source-bindings.json':
                if data != binding_bytes:
                    raise ValueError('source bindings differ')
                continue
            expected = bindings['files'][member.name]
            if len(data) != expected['bytes'] or hashlib.sha256(data).hexdigest() != expected['sha256']:
                raise ValueError(f'member identity mismatch: {member.name}')
    print(f'Verified {len(members)} members; archive SHA-256 {expected_sha}')


if __name__ == '__main__':
    main()
