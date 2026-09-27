#!/usr/bin/env python3
"""Read-only checks for a proposed update to one current-template session record.

This is a formatting/timing aid, not a registry or ownership validator. It reads
only the two explicit files and never publishes, locks, scans, or reclaims.
Legacy/freeform records still use the manual coordination workflow.
"""
import argparse
from datetime import datetime, timedelta
from pathlib import Path
import re
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


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, epilog=(
        'Both files must use the current guide template (exact checkpoint field labels). '
        'Fenced examples are unsupported. '
        'Use ISO UTC timestamps; Next check uses "timestamp / action" ("none / action" '
        'only when paused or terminal). Empty claims use standalone "None.". '
        'Nonempty claims are opaque: paths, overlaps, blockers, receipts, real event times '
        'and stopped writers still require manual review. A pass never authorizes a write.'))
    parser.add_argument('--before', type=Path, required=True, help='saved current record')
    parser.add_argument('--after', type=Path, required=True, help='proposed complete replacement')
    args = parser.parse_args(argv)
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
