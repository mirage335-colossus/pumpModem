#!/usr/bin/env python3
"""Checked orchestration for the cooperative board; no service or second registry.

This helper fails closed on unsupported records/claims. It cannot establish the
truth of a handoff, stop another writer, or constrain uncooperative processes.
Review scope-relevant handoffs before committing; a receipt is not a permission
token. Qualified wrappers or isolated writes handle unsupported environments.
"""
import argparse
from collections import namedtuple
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import re
import socket
import sys
import time
import uuid

sys.dont_write_bytecode = True


def load_neighbor(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'),
                                                Path(__file__).with_name(name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


BOARD = load_neighbor('agent-board')
CHECK = BOARD.record_checker()
RegistryLock = namedtuple('RegistryLock', 'fd token')


class CoordinationError(ValueError):
    pass


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def digest(value):
    return hashlib.sha256(value).hexdigest()


def fingerprint(value):
    return digest(json.dumps(value, sort_keys=True, separators=(',', ':')).encode())


def process_start():
    try:
        start = Path('/proc/self/stat').read_text().rsplit(') ', 1)[1].split()[19]
        boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        return f'linux boot={boot}; start_ticks={start}'
    except (OSError, IndexError):
        return 'unavailable on this platform'


@contextmanager
def registry_mutex(board, session, *, intent='checked record transaction', wait=0):
    """Yield an acquisition-specific handle; never steal or do work under the lock."""
    BOARD.require_capabilities()
    board = BOARD.absolute_path(board)
    BOARD.safe_id(session)
    if not intent or '\n' in intent or '\r' in intent:
        raise CoordinationError('mutex intent must be one nonempty line')
    if not 0 <= wait <= 30:
        raise CoordinationError('mutex wait must be between 0 and 30 seconds')
    with BOARD.open_directory(board) as root:
        deadline = time.monotonic() + wait
        while True:
            try:
                os.mkdir('registry.lock', mode=0o700, dir_fd=root)
                break
            except FileExistsError as exc:
                if time.monotonic() >= deadline:
                    raise CoordinationError('registry busy; inspect or retry; never steal the lock') from exc
                time.sleep(min(random.uniform(.02, .08), max(0, deadline - time.monotonic())))
        with BOARD.child_directory(root, 'registry.lock') as lock:
            token = uuid.uuid4().hex
            owner = (f'- Session: {session}\n- Acquisition token: {token}\n'
                     f'- Host: {socket.gethostname()}\n'
                     f'- UTC: {utc_now()}\n- Role: registry lock holder, not session worker\n'
                     f'- PID/start identity: {os.getpid()} / {process_start()}\n'
                     f'- Intent: {intent}\n').encode()
            created = False
            version = None
            try:
                fd = os.open('owner.md', os.O_CREAT | os.O_EXCL | os.O_WRONLY | os.O_NOFOLLOW,
                             0o600, dir_fd=lock)
                created = True
                with os.fdopen(fd, 'wb') as stream:
                    stream.write(owner)
                    stream.flush()
                    os.fsync(stream.fileno())
                _, version = BOARD.read_regular(lock, 'owner.md')
                yield RegistryLock(lock, token)
            finally:
                BOARD.verify_board(board, root)
                BOARD.verify_directory(root, 'registry.lock', lock)
                if created:
                    current, current_version = BOARD.read_regular(lock, 'owner.md')
                    if current != owner or (version is not None and current_version != version):
                        raise CoordinationError('lock owner changed; cleanup refused; inspect state')
                    # Some mounted filesystems retain the directory listing as
                    # of open(). Reopen relative to the verified lock, keeping
                    # descriptor-relative identity and unknown-entry protection.
                    with BOARD.child_directory(lock, '.') as fresh:
                        if BOARD.identity(os.fstat(fresh)) != BOARD.identity(os.fstat(lock)):
                            raise CoordinationError('mutex identity changed; cleanup refused')
                        entries = sorted(os.listdir(fresh))
                    if entries != ['owner.md']:
                        raise CoordinationError(f'unexpected mutex contents {entries!r}; '
                                                'preserve owner and inspect state')
                    BOARD.verify_directory(root, 'registry.lock', lock)
                    os.unlink('owner.md', dir_fd=lock)
                os.rmdir('registry.lock', dir_fd=root)


def replace_section(text, name, value):
    if not value.strip():
        raise CoordinationError(f'{name}: explicit current text required')
    headings = CHECK.headings_in(text)
    CHECK.section_text(text, headings, name)
    heading = next(h for h in headings if h[2].strip() == name)
    end = next((h.start() for h in headings if h.start() > heading.start()
                and len(h[1]) <= 2), len(text))
    return text[:heading.end()] + '\n' + value.strip() + '\n' + text[end:]


def set_fields(text, changes):
    section = CHECK.section_text(text, CHECK.headings_in(text), 'Current checkpoint')
    for key, value in changes.items():
        if key not in CHECK.REQUIRED_FIELDS or not isinstance(value, str) or '\n' in value or '\r' in value:
            raise CoordinationError('checkpoint fields need known labels and single-line strings')
        section, count = re.subn(r'^- ' + re.escape(key) + r':[^\n]*$',
                                lambda _: f'- {key}: {value}', section, flags=re.M)
        if count != 1:
            raise CoordinationError(f'{key}: need exactly one existing field')
    return replace_section(text, 'Current checkpoint', section)


def checkpoint(before, *, state, running_jobs, progress, handoff, next_check,
               progress_at=None, inbox_at=None, updated_at=None):
    """Replace current status together, retaining event times unless explicit.

    Times describe completed observations, never wrapper invocation. This builder
    cannot change claims or close a session. Commit stamps publication time again.
    """
    if state not in {'active', 'waiting', 'paused'}:
        raise CoordinationError('checkpoint cannot close/reopen a session')
    changes = {'State': state, 'Running jobs': running_jobs,
               'Next check (UTC) / action': next_check,
               'Updated (UTC)': updated_at or utc_now()}
    if progress_at is not None:
        changes['Last meaningful progress (UTC)'] = progress_at
    if inbox_at is not None:
        changes['Last inbox check (UTC)'] = inbox_at
    result = set_fields(before, changes)
    result = replace_section(result, 'Progress and checks', progress)
    result = replace_section(result, 'Blockers and handoff', handoff)
    CHECK.check_transition(before, result)
    if CHECK.parse_record(before)[2] != CHECK.parse_record(result)[2]:
        raise CoordinationError('checkpoint unexpectedly changed claims')
    return result


def input_state(value):
    """Exact observed baseline, including absence/identity; not an input lease.

    Pair this check with stable inputs or an isolated snapshot. It cannot prove
    no edit/revert happened between observations or freeze a later read/write.
    """
    original = BOARD.absolute_path(value)
    physical = original.resolve(strict=False)
    try:
        with BOARD.open_directory(physical.parent) as parent:
            data, version = BOARD.read_regular(parent, physical.name)
        if original.resolve(strict=False) != physical:
            raise CoordinationError(f'{original}: alias changed during input observation')
        return {'path': str(original), 'physical': str(physical), 'kind': 'file',
                'sha256': digest(data), 'version': list(version)}
    except FileNotFoundError:
        # Resolve again to catch an ancestor/link changing while checking absence.
        if original.resolve(strict=False) != physical or original.exists():
            raise CoordinationError(f'{original}: changed during missing-input observation')
        return {'path': str(original), 'physical': str(physical), 'kind': 'missing'}


def normalized_scopes(scopes):
    return [CHECK.canonical_scope(s['kind'], s['value']) for s in scopes]


def read_record_bytes(path):
    """Keep exact publication bytes while matching the reader's newline semantics."""
    with BOARD.open_directory(path.parent) as parent:
        data, _ = BOARD.read_regular(parent, path.name)
    text = data.decode('utf-8').replace('\r\n', '\n').replace('\r', '\n')
    return data, text


def review(board, *, scopes=(), handoffs=(), inputs=(), legacy_reviews=None):
    """Return a review token; no lock or ownership decision is made here.

    All current ownership is scanned. Selected handoffs are exact snapshots;
    All handoff sections are fingerprinted to detect unchanged-byte intervening
    ownership, including in an already-existing record. This conservative check
    can retry on unrelated handoff edits; ordinary progress timestamps do not.
    """
    board = BOARD.absolute_path(board)
    scopes = normalized_scopes(scopes)
    with BOARD.open_directory(board) as opened:
        board_identity = list(BOARD.identity(os.fstat(opened)))
    legacy_reviews = legacy_reviews or {}
    scan = CHECK.scan_sessions(board / 'sessions')
    ownership, hashes, relevant, all_handoffs = [], {}, {}, {}
    interpreted = set()
    for error in scan['errors']:
        path = Path(error['path'])
        session = path.stem
        assessment = legacy_reviews.get(session)
        if (path.parent != board / 'sessions' or path.name != session + '.md' or
                assessment is None or not isinstance(assessment.get('reason'), str) or
                not assessment['reason'].strip() or session in interpreted or
                any(r['id'] == session for r in scan['records'])):
            raise CoordinationError('incomplete registry; inspect every reported entry: ' +
                                    json.dumps(scan['errors'], separators=(',', ':')))
        BOARD.safe_id(session)
        raw, text = read_record_bytes(path)
        hashed = digest(raw)
        if hashed != assessment.get('sha256'):
            raise CoordinationError('legacy record differs from exact manually reviewed bytes')
        claims = normalized_scopes(assessment['claims'])
        ownership.append({'id': session, 'claims': claims})
        hashes[session] = hashed
        all_handoffs[session] = {'manual_record_sha256': hashed}
        relevant[session] = {'manual_review': assessment['reason'], 'sha256': hashed}
        interpreted.add(session)
    if interpreted != set(legacy_reviews):
        raise CoordinationError('legacy review is absent, duplicate, or no longer needed; review again')
    for record in scan['records']:
        claims = CHECK.parse_claims(record['claims'])
        ownership.append({'id': record['id'], 'claims': claims})
        path = Path(record['path'])
        raw, text = read_record_bytes(path)
        # Reject changing ownership between the scan and this selective reread.
        if CHECK.parse_record(text)[2] != record['claims']:
            raise CoordinationError('claims changed during review; retry')
        hashes[record['id']] = digest(raw)
        handoff = CHECK.section_text(text, CHECK.headings_in(text), 'Blockers and handoff')
        all_handoffs[record['id']] = handoff
        selected = record['id'] in handoffs
        error = None
        if scopes:
            try:
                selected = CHECK.handoff_relevance(text, scopes) or selected
            except ValueError as exc:
                selected, error = True, str(exc)
        if selected:
            relevant[record['id']] = {'handoff': handoff, 'uncertainty': error}
    if set(handoffs) - set(hashes):
        raise CoordinationError('requested handoff record is absent; resolve provenance')
    with BOARD.open_directory(board) as opened:
        if list(BOARD.identity(os.fstat(opened))) != board_identity:
            raise CoordinationError('board replaced during review; inspect agreed root')
    token = {'board': str(board), 'board_identity': board_identity,
             'scopes': scopes, 'handoffs': list(handoffs),
             'ownership': ownership, 'relevant_handoffs': relevant,
             'legacy_reviews': legacy_reviews,
             'handoff_fingerprint': fingerprint(all_handoffs),
             'record_hashes': hashes, 'inputs': [input_state(p) for p in inputs]}
    token['fingerprint'] = fingerprint({key: token[key] for key in
                                      ('board', 'board_identity', 'scopes', 'ownership',
                                       'handoff_fingerprint', 'inputs')})
    return token


def commit(board, session, candidate, reviewed, *, create=False, expected_sha256=None,
           precondition=None, after=None, wait=0, handoffs_reviewed=()):
    """Publish/verify/clean up, then run dependent work outside the mutex.

    precondition performs read-only semantic review *before* locking and must
    return True. handoffs_reviewed explicitly acknowledges reading/resolving all
    returned discovery candidates; it does not choose or verify the last owner.
    Any failure prevents after(), including uncertain publication/cleanup. The
    exact saved record remains authoritative even when this function raises.
    """
    BOARD.safe_id(session)
    board = BOARD.absolute_path(board)
    if reviewed['board'] != str(board):
        raise CoordinationError('review belongs to another board')
    if create == (expected_sha256 is not None):
        raise CoordinationError('choose create or exact reviewed own-record SHA-256')
    if not create and reviewed['record_hashes'].get(session) != expected_sha256:
        raise CoordinationError('own record hash does not match the reviewed snapshot')
    if precondition is not None and precondition() is not True:
        raise CoordinationError('precondition failed; no acquisition or dependent action')
    CHECK.scan_record(candidate, board / 'sessions' / (session + '.md'))
    _, candidate_fields, candidate_claims = CHECK.parse_record(candidate)
    if candidate_fields['State'] in CHECK.TERMINAL and after is not None:
        raise CoordinationError('terminal commit cannot run an after callback into released scope')
    proposed = CHECK.parse_claims(candidate_claims)
    prior = next((r['claims'] for r in reviewed['ownership'] if r['id'] == session), [])
    additions = [scope for scope in proposed if scope not in prior]
    for scope in proposed:
        if scope not in prior and scope not in reviewed['scopes']:
            raise CoordinationError('every added claim requires exact scope review')
    with registry_mutex(board, session, wait=wait) as lock:
        current = review(board, scopes=reviewed['scopes'], handoffs=reviewed['handoffs'],
                         inputs=[entry['path'] for entry in reviewed['inputs']],
                         legacy_reviews=reviewed['legacy_reviews'])
        if current['fingerprint'] != reviewed['fingerprint']:
            raise CoordinationError('reviewed ownership, handoff or input changed; review again')
        if additions and set(handoffs_reviewed) != set(reviewed['relevant_handoffs']):
            raise CoordinationError('read and resolve every returned candidate/uncertain handoff; '
                                    'pass their exact IDs as handoffs_reviewed; no last owner is inferred')
        for owner in current['ownership']:
            if owner['id'] != session:
                for theirs in owner['claims']:
                    if any(CHECK.scopes_overlap(ours, theirs) for ours in proposed):
                        raise CoordinationError(f'claim overlaps owner {owner["id"]}; request handoff')
        candidate = set_fields(candidate, {'Updated (UTC)': utc_now()})
        data = candidate.encode()
        with BOARD.staged_bytes(lock.fd, data) as temporary:
            BOARD.publish_record(board, session, str(board / 'registry.lock' / temporary),
                                 create=create, expected_sha256=expected_sha256,
                                 lock_token=lock.token)
        with BOARD.open_directory(board / 'sessions') as records:
            saved, _ = BOARD.read_regular(records, session + '.md')
        if saved != data:
            raise CoordinationError('saved record differs; publication unconfirmed')
        receipt = {'board': str(board), 'session': session, 'record_sha256': digest(saved),
                   'claims': proposed, 'updated': CHECK.parse_record(candidate)[1]['Updated (UTC)']}
    # No user callback or command executes while holding the registry mutex.
    if after is not None:
        after(receipt)
    return receipt


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('review', 'checkpoint', 'commit'))
    parser.add_argument('--input', default='-', help='JSON request on stdin or claimed absolute file')
    args = parser.parse_args(argv)
    try:
        request = json.loads(BOARD.source_bytes(args.input))
        if args.operation == 'review':
            result = review(**request)
        elif args.operation == 'checkpoint':
            result = {'candidate': checkpoint(**request)}
        else:
            result = commit(**request)
        print(json.dumps(result, separators=(',', ':')))
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(1, f'agent-session: {exc}\nStop dependent actions; inspect saved state before retrying.\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
