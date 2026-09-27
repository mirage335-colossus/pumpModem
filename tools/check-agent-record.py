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
INSPECT_ADVISORY = (SCAN_ADVISORY + ' Targeted fallback is unvalidated extraction, not '
                    'legacy-format validation; resolve all errors by manual review.')
PRIVATE_FIELDS = ('Task and approach', PREAMBLE_FIELDS[-1])
# Explicit aliases only: a substring match could expose unrelated progress prose.
LEGACY_METADATA_FIELDS = (
    'Closed (UTC)', 'Delete after', 'Liveness', 'Liveness mode', 'Owner process identity',
    'Closed / Updated / last meaningful progress (UTC)',
    'Closed / updated / last meaningful progress', 'Closed / updated / last progress',
    'Updated / last meaningful progress / Closed (UTC)',
)


def headings_in(text):
    return list(re.finditer(r'^(#{1,6})[ \t]+([^\n]+)$', text, re.MULTILINE))


def section_text(text, headings, name):
    found = [h for h in headings if h.group(2).strip() == name]
    if len(found) != 1 or found[0].group(1) != '##':
        raise ValueError(f'need exactly one ## {name} section')
    start = found[0]
    end = next((h.start() for h in headings
                if h.start() > start.start() and len(h.group(1)) <= 2), len(text))
    value = text[start.end():end].strip()
    if not value:
        raise ValueError(f'{name}: empty section; use explicit values or none')
    return value


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
    headings = headings_in(text)
    titles = [h.group(2).strip() for h in headings if h.group(1) == '#']
    if len(titles) != 1:
        raise ValueError('record needs exactly one # session-id heading')
    sections = {}
    for name in SECTIONS:
        sections[name] = section_text(text, headings, name)
    fields = {}
    for line in sections['Current checkpoint'].splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r'- ([^:]+):[ \t]*(.*)', line)
        if not match:
            raise ValueError('Current checkpoint: expected one nonempty "- Field: value" per line')
        key, value = (part.strip() for part in match.groups())
        if key not in REQUIRED_FIELDS:
            raise ValueError('unknown checkpoint field; use --fields for exact template labels')
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
    if old['State'] in TERMINAL and state not in TERMINAL:
        raise ValueError('terminal session cannot reopen under the same ID; register a fresh '
                         'session ID and reacquire claims')
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
        deletion = timestamp(deadline, 'Delete after')
        if deletion < closure:
            raise ValueError('Delete after must not precede Closed')
        if deletion > closure + timedelta(days=30) and new['Retention exception'].lower() in ('none', 'none.'):
            raise ValueError('Delete after beyond closure + 30 days requires an explicit '
                             'Retention exception with exact material, live dependency, '
                             'responsible owner, reason and review date')
        if new['Running jobs'].lower() not in ('none', 'none.'):
            raise ValueError('terminal Running jobs must be literal none (or None.); resolve jobs '
                             'first, then move completed-job details to Progress and checks '
                             'or Blockers and handoff')
    elif closed.lower() != 'none' or deadline.lower() != 'none':
        raise ValueError('nonterminal record needs none in closure/deletion fields')


def scan_record(text, path):
    """Extract named metadata and all claims; unknown layouts need manual review.

    This intentionally does not use check_transition: terminal claims, old next
    checks and unresolved jobs must remain visible rather than disappearing.
    """
    headings = headings_in(text)
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
    missing = [field for field in PREAMBLE_FIELDS if field not in identity]
    if missing:
        raise ValueError('missing current-template preamble fields: ' + ', '.join(missing))
    # Parser diagnostics name only known fields, never unknown labels or values.
    session_id, checkpoint, claims = parse_record(text)
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
                if key not in PRIVATE_FIELDS}
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
        text = stream.read()
        if record_identity(os.fstat(stream.fileno())) != record_identity(opened):
            raise ValueError('record changed while being read; retry after publication settles')
        return text


def read_record(path):
    """Read a single stable direct regular record without following a leaf symlink."""
    before = path.lstat()
    if path.suffix != '.md' or not stat.S_ISREG(before.st_mode):
        raise ValueError('expected direct regular .md record; no symlinks or subdirectories')
    text = read_scan_record(path, before)
    if record_identity(path.lstat()) != record_identity(before):
        raise ValueError('record changed while being read; retry after publication settles')
    return text, before


def inspect_record(text, path, section='metadata-claims'):
    """Select complete named sections, reporting failed validation even on fallback.

    Metadata fallback reads only the preamble and an unambiguous checkpoint. It
    recognizes explicit old labels, never task/progress/handoff prose by keyword.
    Claims/handoff extraction stops at the next same-or-higher-level heading and
    preserves nested headings. Duplicate/misleveled sections and fences are unsafe.
    """
    if section not in ('metadata-claims', 'handoff'):
        raise ValueError('section must be metadata-claims or handoff')
    result = {'complete': False, 'path': str(path), 'section': section,
              'errors': [], 'advisory': INSPECT_ADVISORY}
    try:
        current = scan_record(text, path)
    except ValueError as exc:
        result['errors'].append({'path': str(path), 'error': str(exc)})
        current = None
    headings = headings_in(text)
    name = 'Claims held' if section == 'metadata-claims' else 'Blockers and handoff'
    key = 'claims' if section == 'metadata-claims' else 'handoff'
    try:
        if re.search(r'^[ \t]*(?:`{3,}|~{3,})', text, re.MULTILINE):
            raise ValueError('fenced examples make targeted section boundaries ambiguous; review manually')
        result[key] = section_text(text, headings, name)
    except ValueError as exc:
        error = {'path': str(path), 'error': str(exc)}
        if error not in result['errors']:
            result['errors'].append(error)
    if section == 'metadata-claims':
        if current is not None:
            result['id'] = current['id']
            result['metadata'] = [{'field': key, 'value': value}
                                  for key, value in current['metadata'].items()]
        else:
            # Do not infer a trusted ID, timestamps or empty ownership from this.
            first_section = headings[1].start() if len(headings) > 1 else len(text)
            chunks = [text[:first_section]]
            try:
                chunks.append(section_text(text, headings, 'Current checkpoint'))
            except ValueError:
                pass  # The strict validation error already makes this incomplete.
            if (not headings or headings[0].start() != 0 or headings[0][1] != '#' or
                    re.search(r'^[ \t]*(?:`{3,}|~{3,})', text, re.MULTILINE)):
                chunks = []
            known = (set(PREAMBLE_FIELDS) | set(REQUIRED_FIELDS) | set(LEGACY_METADATA_FIELDS)) - set(PRIVATE_FIELDS)
            fields = []
            for chunk in chunks:
                for line in chunk.splitlines():
                    match = re.fullmatch(r'- ([^:]+):[ \t]*(\S.*)', line)
                    if not match or match[1] not in known:
                        continue
                    label, value = match[1], match[2].strip()
                    if label == 'Next check (UTC) / action':
                        label = 'Next check (UTC)'
                        value = value.partition(' / ')[0]
                        if value != 'none':
                            try:
                                timestamp(value, label)
                            except ValueError:
                                continue  # Never leak an unrecognized action as a timestamp.
                    fields.append({'field': label, 'value': value})
            result['metadata'] = fields
    result['complete'] = not result['errors']
    return result


def inspect_path(path, section='metadata-claims'):
    path = path.absolute()
    try:
        text, _ = read_record(path)
        return inspect_record(text, path, section)
    except (OSError, UnicodeError, ValueError) as exc:
        return {'complete': False, 'path': str(path), 'section': section,
                'errors': [{'path': str(path), 'error': str(exc)}],
                'advisory': INSPECT_ADVISORY}


def scan_sessions(directory):
    """Read only direct regular .md entries; never silently skip a possible owner."""
    directory = directory.absolute()
    result = {'complete': False, 'records': [], 'errors': [], 'advisory': SCAN_ADVISORY}
    try:
        directory_before = directory.lstat()
        if not stat.S_ISDIR(directory_before.st_mode):
            raise ValueError('scan target must be a directory, not a symlink')
        entries = sorted(directory.iterdir())
    except (OSError, ValueError) as exc:
        result['errors'].append({'path': str(directory), 'error': str(exc)})
        return result
    observed = {}
    for path in entries:
        try:
            text, before = read_record(path)
            observed[path] = before
            result['records'].append(scan_record(text, path))
        except (OSError, UnicodeError, ValueError) as exc:
            result['errors'].append({'path': str(path), 'error': str(exc)})
    try:
        if (sorted(directory.iterdir()) != entries or
                record_identity(directory.lstat()) != record_identity(directory_before)):
            raise ValueError('directory entries changed during scan; retry after publication settles')
    except (OSError, ValueError) as exc:
        result['errors'].append({'path': str(directory), 'error': str(exc)})
    for path, before in observed.items():
        try:
            if record_identity(path.lstat()) != record_identity(before):
                raise ValueError('record changed after read during scan; retry after publication settles')
        except (OSError, ValueError) as exc:
            result['errors'].append({'path': str(path), 'error': str(exc)})
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
    parser.add_argument('--compact', action='store_true', help=(
        'minify scan/inspection JSON formatting only; preserve every metadata field, whole '
        'claim and unresolved error; complete-claim review is still required'))
    parser.add_argument('--inspect', type=Path, metavar='RECORD', help=(
        'inspect only selected metadata/whole claims or handoff; legacy fallback stays '
        'incomplete (exit 1), with safe extraction and errors requiring manual review'))
    parser.add_argument('--section', choices=('metadata-claims', 'handoff'), help=(
        'selection for --inspect (default: metadata-claims); handoff omits all other sections'))
    parser.add_argument('--fields', action='store_true', help='list exact current-template field and section labels')
    args = parser.parse_args(argv)
    if args.fields:
        if any((args.before, args.after, args.scan, args.inspect, args.section, args.compact)):
            parser.error('--fields cannot be combined with other modes/options')
        print(json.dumps({'preamble': PREAMBLE_FIELDS, 'checkpoint': REQUIRED_FIELDS,
                          'sections': SCAN_SECTIONS}, indent=2))
        return 0
    if args.section and not args.inspect:
        parser.error('--section requires --inspect')
    if args.compact and not (args.scan or args.inspect):
        parser.error('--compact requires --scan or --inspect')
    if args.inspect is not None:
        if any((args.scan, args.before, args.after)):
            parser.error('--inspect cannot be combined with --scan, --before or --after')
        result = inspect_path(args.inspect, args.section or 'metadata-claims')
        print(json.dumps(result, **({'separators': (',', ':')} if args.compact else {'indent': 2})))
        return 0 if result['complete'] else 1
    if args.scan is not None:
        if args.before is not None or args.after is not None:
            parser.error('--scan cannot be combined with --before or --after')
        result = scan_sessions(args.scan)
        print(json.dumps(result, **({'separators': (',', ':')} if args.compact else {'indent': 2})))
        return 0 if result['complete'] else 1
    if args.before is None or args.after is None:
        parser.error('provide both --before and --after, or --scan, --inspect or --fields')
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
