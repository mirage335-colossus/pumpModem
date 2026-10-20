#!/usr/bin/env python3
"""Retain bounded files from one failed synthetic GUI smoke directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys

MAX_FILES = 8
MAX_FILE_BYTES = 134_218_123
MAX_TOTAL_BYTES = 150 * 1024 * 1024
LOG_HEADER = '# GUI smoke invocation '


def fail(message):
    raise ValueError(message)


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def read_invocation(log_path):
    try:
        with log_path.open('rb') as stream:
            line = stream.readline(64 * 1024 + 1)
        if len(line) > 64 * 1024 or not line.startswith(LOG_HEADER.encode()):
            fail('smoke log invocation header is missing or exceeds 64 KiB')
        value = json.loads(line[len(LOG_HEADER):].decode('utf-8'))
        if not isinstance(value, dict) or not isinstance(value.get('command'), list):
            fail('smoke log invocation header has no command array')
        return value
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        fail('cannot read exact smoke invocation: ' + str(exc))


def capture(source, destination, log_path, report_path, package_manifest, archive_name, archive_sha256):
    source = Path(os.path.abspath(source))
    destination = Path(os.path.abspath(destination))
    if source.is_symlink() or not source.is_dir():
        fail('synthetic smoke source is missing or is a symlink')
    if destination.exists() or destination.is_symlink():
        fail('fixture evidence destination already exists')
    if source == destination or source in destination.parents or destination in source.parents:
        fail('fixture source and destination overlap')
    if not re.fullmatch(r'DataPump-[A-Za-z0-9_.-]+-native-rev\.(?:tar\.gz|zip)', archive_name):
        fail('archive identity is not an ordinary Rev native archive name')
    if not re.fullmatch(r'[0-9a-f]{64}', archive_sha256):
        fail('archive SHA-256 identity is missing or malformed')

    root_fd = os.open(source, os.O_RDONLY | getattr(os, 'O_DIRECTORY', 0) | getattr(os, 'O_NOFOLLOW', 0))
    try:
        names = sorted(os.listdir(root_fd))
        if not names or len(names) > MAX_FILES:
            fail(f'expected 1..{MAX_FILES} direct smoke fixture files, found {len(names)}')
        expected = []
        total = 0
        for name in names:
            if name in ('.', '..') or '/' in name or '\\' in name:
                fail('unsafe synthetic fixture filename')
            info = os.stat(name, dir_fd=root_fd, follow_symlinks=False)
            if not stat.S_ISREG(info.st_mode):
                fail('synthetic fixture is not a regular file: ' + name)
            if info.st_size < 0 or info.st_size > MAX_FILE_BYTES:
                fail('synthetic fixture exceeds per-file bound: ' + name)
            total += info.st_size
            if total > MAX_TOTAL_BYTES:
                fail('synthetic fixtures exceed total size bound')
            expected.append((name, info.st_size, info.st_mtime_ns, info.st_ino))

        destination.mkdir(mode=0o700)
        records = []
        for name, original_size, original_mtime, original_inode in expected:
            fd = os.open(name, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0), dir_fd=root_fd)
            try:
                info = os.fstat(fd)
                if (not stat.S_ISREG(info.st_mode) or info.st_size != original_size or
                        info.st_mtime_ns != original_mtime or info.st_ino != original_inode):
                    fail('synthetic fixture changed before capture: ' + name)
                target = destination / name
                digest = hashlib.sha256()
                copied = 0
                with os.fdopen(fd, 'rb', closefd=False) as src, target.open('xb') as dst:
                    while True:
                        block = src.read(1024 * 1024)
                        if not block:
                            break
                        copied += len(block)
                        if copied > MAX_FILE_BYTES or copied > original_size:
                            fail('synthetic fixture changed or exceeded its copy bound: ' + name)
                        digest.update(block)
                        dst.write(block)
                    dst.flush()
                    os.fsync(dst.fileno())
                after = os.stat(name, dir_fd=root_fd, follow_symlinks=False)
                if (copied != original_size or after.st_ino != original_inode or
                        after.st_mtime_ns != original_mtime or not stat.S_ISREG(after.st_mode)):
                    fail('synthetic fixture changed during capture: ' + name)
                records.append({'name': name, 'bytes': copied, 'sha256': digest.hexdigest()})
            finally:
                os.close(fd)

        invocation = read_invocation(Path(log_path))
        try:
            report = json.loads(Path(report_path).read_text(encoding='utf-8')) if report_path else None
            report_error = None
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            report, report_error = None, str(exc)
        key_files = [item['name'] for item in records
                     if item['name'].startswith('generated keys café-') and item['name'].endswith('.key')]
        if len(key_files) > 1:
            fail('multiple generated synthetic key files found')
        key_record = ({'status': 'retained', 'name': key_files[0]}
                      if key_files else {'status': 'missing',
                                         'reason': 'smoke source directory contained no generated synthetic key'})
        manifest = {
            'format': 1,
            'capture': 'failed ordinary SDK Rev native archive GUI smoke',
            'archive_name': archive_name,
            'archive_sha256': archive_sha256,
            'package_inventory_sha256': sha256(Path(package_manifest)),
            'smoke_invocation': invocation,
            'smoke_report': report,
            'smoke_report_error': report_error,
            'synthetic_key': key_record,
            'fixtures': records,
            'limits': {'max_files': MAX_FILES, 'max_file_bytes': MAX_FILE_BYTES,
                       'max_total_bytes': MAX_TOTAL_BYTES},
        }
        manifest_path = destination / 'capture-manifest.json'
        with manifest_path.open('x', encoding='utf-8') as stream:
            json.dump(manifest, stream, ensure_ascii=False, indent=2, sort_keys=True)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
    finally:
        os.close(root_fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True)
    parser.add_argument('--destination', required=True)
    parser.add_argument('--log', required=True)
    parser.add_argument('--report', required=True)
    parser.add_argument('--package-manifest', required=True)
    parser.add_argument('--archive-name', required=True)
    parser.add_argument('--archive-sha256', required=True)
    args = parser.parse_args()
    capture(args.source, args.destination, args.log, args.report,
            args.package_manifest, args.archive_name, args.archive_sha256)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError) as exc:
        print('GUI smoke fixture capture failed: ' + str(exc), file=sys.stderr)
        raise SystemExit(1)
