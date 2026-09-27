#!/usr/bin/env python3
"""Read-only current-template session scan or proposed record-update check.

This is a reading/formatting aid, not a registry or ownership validator. It never
publishes, locks, reclaims or decides whether a path is free.
Legacy/freeform records still use the manual coordination workflow.
"""
import argparse
from datetime import datetime, timedelta
import json
import os
from pathlib import Path
import re
import stat
import sys


SECTIONS = ('Current checkpoint', 'Claims held', 'Blockers and handoff')
REQUIRED_FIELDS = (
    'State', 'Updated (UTC)', 'Last meaningful progress (UTC)',
    'Last inbox check (UTC)', 'Next check (UTC) / action',
    'Liveness mode / cadence', 'Last heartbeat (UTC), if supervised',
    'Run token / heartbeat file and writer, if used', 'Owner process',
    'Running jobs', 'Closed (UTC), if terminal', 'Delete after (UTC)',
    'Retention exception', 'Contact',
)
TERMINAL = {'done', 'failed', 'cancelled'}
PREAMBLE_FIELDS = (
    'Tool / host / local chat reference', 'Parent / read-only helpers',
    'Task and approach', 'Checkout / coordination root (absolute physical paths)',
    'Branch / starting HEAD / current HEAD',
    'Starting worktree and index changes (including work owned by others)',
)
SCAN_SECTIONS = ('Current checkpoint', 'Claims held', 'Baseline and dependencies',
                 'Progress and checks', 'Blockers and handoff')
SCAN_ADVISORY = ('Read-only observation, not an atomic snapshot or an ownership decision. '
                 'Recheck complete claims under the registry mutex before changing claims. '
                 'If incomplete, investigate every error; omitted records may hold claims.')


def timestamp(value, field):
    if not re.fullmatch(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d+)?(?:Z|\+00:00)', value):
        raise ValueError(f'{field}: use an ISO UTC timestamp (YYYY-MM-DDTHH:MM:SSZ)')
    try:
        result = datetime.fromisoformat(value.replace('Z', '+00:00'))
    except ValueError as exc:
        raise ValueError(f'{field}: invalid UTC timestamp') from exc
    assert result.utcoffset() == timedelta(0)
    return result


def parse_record(text):
    """Require the documented headings/fields; keep the entire claims text opaque."""
    if re.search(r'^[ \t]*(?:`{3,}|~{3,})', text, re.MULTILINE):
        raise ValueError('fenced examples are unsupported; use the plain current record template')
    headings = list(re.finditer(r'^(#{1,6})[ \t]+([^\n]+)$', text, re.MULTILINE))
    titles = [h.group(2).strip() for h in headings if h.group(1) == '#']
    if len(titles) != 1:
        raise ValueError('record needs exactly one # session-id heading')
    sections = {}
    for name in SECTIONS:
        found = [h for h in headings if h.group(2).strip() == name]
        if len(found) != 1 or found[0].group(1) != '##':
            raise ValueError(f'need exactly one ## {name} section')
        start = found[0]
        end = next((h.start() for h in headings
                    if h.start() > start.start() and len(h.group(1)) <= 2), len(text))
        sections[name] = text[start.end():end].strip()
        if not sections[name]:
            raise ValueError(f'{name}: empty section; use explicit values or none')
    fields = {}
    for line in sections['Current checkpoint'].splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r'- ([^:]+):[ \t]*(.+)', line)
        if not match:
            raise ValueError('Current checkpoint: expected one nonempty "- Field: value" per line')
        key, value = (part.strip() for part in match.groups())
        if key in fields:
            raise ValueError(f'duplicate checkpoint field: {key}')
        if not value:
            raise ValueError(f'{key}: empty field; use none when appropriate')
        fields[key] = value
    missing = [field for field in REQUIRED_FIELDS if field not in fields]
    if missing:
        raise ValueError('missing checkpoint fields: ' + ', '.join(missing))
    return titles[0], fields, sections['Claims held']


def check_transition(before, after):
    old_id, old, old_claims = parse_record(before)
    new_id, new, new_claims = parse_record(after)
    if new_id != old_id:
        raise ValueError('before/after session IDs differ')
    state = new['State']
    if state not in TERMINAL | {'active', 'waiting', 'paused'}:
        raise ValueError('State: choose one documented state')
    times = {}
    for field in ('Updated (UTC)', 'Last meaningful progress (UTC)', 'Last inbox check (UTC)'):
        times[field] = timestamp(new[field], field)
        if times[field] < timestamp(old[field], 'before ' + field):
            raise ValueError(f'{field}: moved backwards; investigate clock/history, do not invent times')
    updated = times['Updated (UTC)']
    if old_claims != new_claims and updated <= timestamp(old['Updated (UTC)'], 'before Updated'):
        raise ValueError('Claims held changed without advancing Updated (UTC) in the same candidate')
    for field in ('Last meaningful progress (UTC)', 'Last inbox check (UTC)'):
        if times[field] > updated:
            raise ValueError(f'{field}: later than Updated (UTC)')
    # Do not infer empty ownership from phase, age, table headers or release prose.
    empty_claims = new_claims.lower() in ('none', 'none.')
    if state in TERMINAL and not empty_claims:
        raise ValueError('terminal record retains Claims held text; inspect/release explicitly, '
                         'then use standalone None. and move disposition to Blockers and handoff')
    next_check, separator, action = new['Next check (UTC) / action'].partition(' / ')
    if not separator or not action.strip():
        raise ValueError('Next check (UTC) / action: expected timestamp (or none) / concrete action')
    if next_check.lower() == 'none':
        if state in {'active', 'waiting'}:
            raise ValueError('active/waiting record needs a concrete Next check (UTC)')
    elif timestamp(next_check, 'Next check') <= updated:
        raise ValueError('Next check must be later than Updated; replace the elapsed plan')
    closed = new['Closed (UTC), if terminal']
    deadline = new['Delete after (UTC)']
    if state in TERMINAL:
        closure = timestamp(closed, 'Closed')
        if closure > updated:
            raise ValueError('Closed must not be later than Updated')
        if timestamp(deadline, 'Delete after') < closure:
            raise ValueError('Delete after must not precede Closed')
        if new['Running jobs'].lower() not in ('none', 'none.'):
            raise ValueError('terminal record lists Running jobs; resolve jobs before closure')
    elif closed.lower() != 'none' or deadline.lower() != 'none':
        raise ValueError('nonterminal record needs none in closure/deletion fields')


def scan_record(text, path):
    """Extract named metadata and all claims; unknown layouts need manual review.

    This intentionally does not use check_transition: terminal claims, old next
    checks and unresolved jobs must remain visible rather than disappearing.
    """
    headings = list(re.finditer(r'^(#{1,6})[ \t]+([^\n]+)$', text, re.MULTILINE))
    if not headings or headings[0].start() != 0 or headings[0].group(1) != '#':
        raise ValueError('expected current-template session heading at start of file')
    top = [h for h in headings if len(h.group(1)) <= 2]
    if ([h.group(1) for h in top] != ['#'] + ['##'] * len(SCAN_SECTIONS) or
            [h.group(2).strip() for h in top[1:]] != list(SCAN_SECTIONS)):
        raise ValueError('unknown, missing, duplicate or reordered top-level sections; review manually')
    if any(h.start() < top[1].start() for h in headings[1:]):
        raise ValueError('unexpected preamble heading; review manually')
    preamble = text[top[0].end():top[1].start()]
    identity = {}
    for line in preamble.splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r'- ([^:]+):[ \t]*(\S.*)', line)
        if not match or match[1] not in PREAMBLE_FIELDS or match[1] in identity:
            raise ValueError('unknown, duplicate or malformed preamble field; review manually')
        identity[match[1]] = match[2].strip()
    if set(identity) != set(PREAMBLE_FIELDS):
        raise ValueError('missing current-template preamble fields; review manually')
    try:
        session_id, checkpoint, claims = parse_record(text)
    except ValueError as exc:
        # Unknown field labels may themselves contain unrelated task details.
        raise ValueError('malformed checkpoint or claims structure; review manually') from exc
    if not re.fullmatch(r'[A-Za-z0-9._-]+', session_id) or path.name != session_id + '.md':
        raise ValueError('session ID must use documented characters and match the record filename')
    if set(checkpoint) != set(REQUIRED_FIELDS):
        raise ValueError('unknown checkpoint fields; review manually')
    if checkpoint['State'] not in TERMINAL | {'active', 'waiting', 'paused'}:
        raise ValueError('unknown State; review manually')
    for field in ('Updated (UTC)', 'Last meaningful progress (UTC)', 'Last inbox check (UTC)'):
        timestamp(checkpoint[field], field)
    next_check, separator, action = checkpoint['Next check (UTC) / action'].partition(' / ')
    if not separator or not action.strip():
        raise ValueError('Next check needs timestamp (or none) / action; review manually')
    for field, value in [('Next check (UTC)', next_check)] + [
            (key, checkpoint[key]) for key in ('Last heartbeat (UTC), if supervised',
                                              'Closed (UTC), if terminal', 'Delete after (UTC)')]:
        if value != 'none':
            timestamp(value, field)
    metadata = {key: identity[key] for key in PREAMBLE_FIELDS
                if key not in ('Task and approach', PREAMBLE_FIELDS[-1])}
    metadata.update({key: value for key, value in checkpoint.items()
                     if key != 'Next check (UTC) / action'})
    metadata['Next check (UTC)'] = next_check
    return {'path': str(path), 'id': session_id, 'metadata': metadata, 'claims': claims}


def record_identity(info):
    # Reading may update atime; it is not evidence that the record changed.
    return tuple(getattr(info, key) for key in
                 ('st_dev', 'st_ino', 'st_mode', 'st_size', 'st_mtime_ns', 'st_ctime_ns'))


def read_scan_record(path, before):
    flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0)
    with os.fdopen(os.open(path, flags), encoding='utf-8') as stream:
        opened = os.fstat(stream.fileno())
        if not stat.S_ISREG(opened.st_mode) or record_identity(opened) != record_identity(before):
            raise ValueError('record replaced before read; retry after publication settles')
        return stream.read()


def scan_sessions(directory):
    """Read only direct regular .md entries; never silently skip a possible owner."""
    directory = directory.absolute()
    result = {'complete': False, 'records': [], 'errors': [], 'advisory': SCAN_ADVISORY}
    try:
        if not stat.S_ISDIR(directory.lstat().st_mode):
            raise ValueError('scan target must be a directory, not a symlink')
        entries = sorted(directory.iterdir())
    except (OSError, ValueError) as exc:
        result['errors'].append({'path': str(directory), 'error': str(exc)})
        return result
    for path in entries:
        try:
            before = path.lstat()
            if path.suffix != '.md' or not stat.S_ISREG(before.st_mode):
                raise ValueError('expected direct regular .md record; no symlinks or subdirectories')
            text = read_scan_record(path, before)
            if record_identity(path.lstat()) != record_identity(before):
                raise ValueError('record changed while being read; retry after publication settles')
            result['records'].append(scan_record(text, path))
        except (OSError, UnicodeError, ValueError) as exc:
            result['errors'].append({'path': str(path), 'error': str(exc)})
    try:
        if sorted(directory.iterdir()) != entries:
            raise ValueError('directory entries changed during scan; retry after publication settles')
    except (OSError, ValueError) as exc:
        result['errors'].append({'path': str(directory), 'error': str(exc)})
    result['complete'] = not result['errors']
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, epilog=(
        'Transition checking requires both files to use the current guide template '
        '(exact checkpoint field labels); scan requires its complete preamble and sections, '
        'with session ID matching the .md filename. '
        'Fenced examples are unsupported. '
        'Use ISO UTC timestamps; Next check uses "timestamp / action" ("none / action" '
        'only when paused or terminal). Empty claims use standalone "None.". '
        'Nonempty claims are opaque: paths, overlaps, blockers, receipts, real event times '
        'and stopped writers still require manual review. A pass never authorizes a write.'))
    parser.add_argument('--before', type=Path, help='saved current record')
    parser.add_argument('--after', type=Path, help='proposed complete replacement')
    parser.add_argument('--scan', type=Path, metavar='SESSIONS_DIRECTORY', help=(
        'emit JSON metadata and complete claims from direct current-template .md records; '
        'unknown/unreadable entries produce incomplete output and exit 1; never follow symlinks'))
    args = parser.parse_args(argv)
    if args.scan is not None:
        if args.before is not None or args.after is not None:
            parser.error('--scan cannot be combined with --before or --after')
        result = scan_sessions(args.scan)
        print(json.dumps(result, indent=2))
        return 0 if result['complete'] else 1
    if args.before is None or args.after is None:
        parser.error('provide both --before and --after, or --scan')
    try:
        if args.before.resolve() == args.after.resolve():
            raise ValueError('before and after must be distinct files')
        check_transition(args.before.read_text(encoding='utf-8'), args.after.read_text(encoding='utf-8'))
    except (OSError, UnicodeError, ValueError) as exc:
        print(f'agent record check: {exc}', file=sys.stderr)
        return 1
    print('Record format/timing checks passed; ownership, handoff and factual review still required.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
