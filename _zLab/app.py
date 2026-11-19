#!/usr/bin/env python3
"""Local RunPod laboratory controller; networking is confined to this program."""
import json
import math
import os
import webbrowser
from pathlib import Path
import secrets
import select
import signal
import subprocess
import sys
import tempfile
import threading
import time

from runpod import RunPodClient, RunPodError

IMAGE = 'runpod/pytorch:1.0.2-cu1281-torch280-ubuntu2404'
GPU_MODELS = ('RTX PRO 6000', 'RTX 5090', 'RTX 4090')


def configure_dark_theme(root):
    """Apply a readable dark palette to the lab's Tk and ttk controls."""
    background, surface, field = '#20242c', '#303741', '#171b22'
    foreground, muted = '#f0f2f5', '#bec6d1'
    selection, border = '#365f91', '#697584'
    root.configure(background=background)
    root.tk_setPalette(background=background, foreground=foreground,
                       activeBackground=surface, activeForeground=foreground,
                       selectBackground=selection, selectForeground=foreground,
                       disabledForeground=muted, insertBackground=foreground,
                       highlightBackground=background, highlightColor=selection)
    style = ttk.Style(root)
    if 'clam' in style.theme_names():
        style.theme_use('clam')
    style.configure('.', background=background, foreground=foreground,
                    fieldbackground=field, selectbackground=selection,
                    selectforeground=foreground, insertcolor=foreground,
                    bordercolor=border, darkcolor=background, lightcolor=surface)
    style.configure('TFrame', background=background)
    style.configure('TLabel', background=background, foreground=foreground)
    style.configure('Hint.TLabel', background=background, foreground=muted)
    style.configure('TButton', background=surface, foreground=foreground, padding=(7, 4))
    style.map('TButton', background=[('disabled', background), ('pressed', selection),
                                    ('active', '#414c5b')],
              foreground=[('disabled', muted)])
    style.configure('TEntry', fieldbackground=field, foreground=foreground,
                    insertcolor=foreground)
    style.map('TEntry', fieldbackground=[('disabled', surface)], foreground=[('disabled', muted)])
    style.configure('TCombobox', fieldbackground=field, background=surface,
                    foreground=foreground, arrowcolor=foreground, insertcolor=foreground)
    style.map('TCombobox', fieldbackground=[('readonly', field), ('disabled', surface)],
              foreground=[('readonly', foreground), ('disabled', muted)],
              selectbackground=[('readonly', selection)], selectforeground=[('readonly', foreground)])
    # The drop-down list is a Tk listbox, not a ttk widget.
    root.option_add('*TCombobox*Listbox.background', field)
    root.option_add('*TCombobox*Listbox.foreground', foreground)
    root.option_add('*TCombobox*Listbox.selectBackground', selection)
    root.option_add('*TCombobox*Listbox.selectForeground', foreground)


def catalog_offer(item, kind, cloud):
    """Return the GUI label, create payload and stock for one supported SKU."""
    if not isinstance(item, dict) or not isinstance(item.get('id'), str) or not item['id']:
        return None
    identity = item['id']
    stock = item.get('availability')
    if stock not in ('NONE', 'LOW', 'MEDIUM', 'HIGH'):
        stock = 'UNKNOWN'
    prices = item.get('price')
    prices = prices if isinstance(prices, dict) else {}
    if kind == 'gpu':
        if not any(model in identity.upper() for model in GPU_MODELS):
            return None
        if item.get(cloud.lower()) is False:
            stock = 'NONE'
        compute = {'gpu': {'id': identity, 'count': 1}, 'cloud': cloud}
        label = f'GPU: {identity}'
        price = prices.get(cloud.lower())
    else:
        if cloud != 'SECURE':
            return None
        try:
            bounds = item['vcpu']
            memory = item['ramGbPerVcpu'] * 32
            if not (bounds['min'] <= 32 <= bounds['max'] and math.isfinite(memory) and memory >= 128):
                return None
        except (KeyError, TypeError, OverflowError):
            return None
        compute = {'cpu': {'id': identity, 'vcpuCount': 32}, 'cloud': cloud}
        label = f'CPU: {identity}, 32 vCPU / {memory:g} GB'
        per_vcpu = prices.get('securePerVcpu')
        price = per_vcpu * 32 if type(per_vcpu) in (int, float) else None
    label += f' · stock {stock.lower()}'
    if type(price) in (int, float) and math.isfinite(price) and price > 0:
        label += f' · ${price:g}/hr'
    else:
        label += ' · price unknown'
    return label, compute, stock


def options(gpus, cpus, cloud='SECURE'):
    offers = []
    for kind, catalog in (('gpu', gpus), ('cpu', cpus)):
        for item in catalog:
            offer = catalog_offer(item, kind, cloud)
            if offer and offer[2] != 'NONE':
                offers.append(offer)

    def priority(offer):
        _, compute, stock = offer
        gpu = compute.get('gpu', {}).get('id', '').upper()
        model_rank = next((i for i, model in enumerate(GPU_MODELS) if model in gpu), len(GPU_MODELS))
        return stock == 'NONE', stock == 'UNKNOWN', not gpu, 'MIG' in gpu.split(), model_rank, 'MAX-Q' in gpu

    return [(label, compute) for label, compute, _ in sorted(offers, key=priority)]


def creation_quote(client, compute):
    """Recheck the exact selected SKU and tier before confirming a paid request."""
    cloud = compute['cloud']
    kind = 'gpu' if 'gpu' in compute else 'cpu'
    items = client.list_gpus(cloud) if kind == 'gpu' else client.list_cpus()
    matches = [item for item in items if isinstance(item, dict) and item.get('id') == compute[kind]['id']]
    offer = catalog_offer(matches[0], kind, cloud) if len(matches) == 1 else None
    if not offer or offer[1] != compute:
        raise ValueError('This compute option is no longer listed for the selected tier. Reload the catalog.')
    label, _, stock = offer
    if stock == 'NONE':
        raise ValueError(f'{label} on {cloud.title()} Cloud. Reload the catalog for another placement.')
    return label


def load_catalog(client, cloud='SECURE'):
    """Load each independent catalog so one failure cannot hide the other.

    Do not include exception text in the GUI: even if the built-in client
    sanitizes its errors, a provider or injected transport can expose a key or
    other request data. Map status codes to fixed, actionable messages.
    """
    catalogs, failures = {}, []
    fetches = [('GPU', lambda: client.list_gpus(cloud))]
    if cloud == 'SECURE':
        fetches.append(('CPU', client.list_cpus))
    for kind, fetch in fetches:
        try:
            catalogs[kind] = fetch()
        except RunPodError as error:
            catalogs[kind] = []
            if error.status == 403:
                detail = 'access denied (HTTP 403); check catalog:read and account permissions'
            elif error.status == 401:
                detail = 'authentication rejected (HTTP 401); check the API key'
            elif error.status == 429:
                detail = 'rate limited (HTTP 429); try again later'
            elif type(error.status) is int and 100 <= error.status <= 599:
                detail = f'request failed (HTTP {error.status})'
            else:
                detail = 'request failed; check the connection and try again'
            failures.append(f'{kind} catalog {detail}')
    return options(catalogs['GPU'], catalogs.get('CPU', []), cloud), failures


class State:
    """Persist only ownership records, before sending create requests."""
    def __init__(self, path=None):
        self.path = Path(path or (Path(os.environ.get('XDG_STATE_HOME', Path.home()/'.local/state')) /
                                  'pumpModem/runpod-lab/jobs.json'))

    def read(self):
        try:
            records = json.loads(self.path.read_text())
        except FileNotFoundError:
            return []
        if not isinstance(records, list) or any(not isinstance(r, dict) for r in records):
            raise ValueError('Invalid ownership state; preserve it and inspect manually')
        return records

    def write(self, records):
        self.path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        fd, tmp = tempfile.mkstemp(prefix='.jobs-', dir=self.path.parent)
        try:
            os.fchmod(fd, 0o600)
            with os.fdopen(fd, 'w') as out:
                json.dump(records, out)
                out.flush()
                os.fsync(out.fileno())
            os.replace(tmp, self.path)
            directory = os.open(self.path.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
        finally:
            if os.path.exists(tmp):
                os.unlink(tmp)


class Ownership:
    TERMINATING = 'Terminating'
    TERMINATED = 'Terminated'
    TERMINATION_FAILED = 'Termination failed'
    NOT_VISIBLE = 'No longer visible'
    _QUEUE_CLOSED = ('CANCELLED', 'FAILED', 'EXPIRED')
    _TERMINATION_WAIT = 120

    def __init__(self, state, client):
        self.state, self.client = state, client

    def create(self, compute, image, deadline=None):
        records = self.state.read()
        # A missing id might be an accepted, queued or timed-out create.
        if any(not r.get('id') and r.get('status') != self.TERMINATED for r in records):
            raise ValueError('An earlier create is unresolved. Reconcile it before creating another pod.')
        name = 'pump-lab-' + secrets.token_hex(12)
        if deadline is None or not isinstance(deadline, (int, float)) or deadline <= time.time():
            raise ValueError('A future job deadline is required')
        kind = 'gpu' if 'gpu' in compute else 'cpu' if 'cpu' in compute else None
        if not kind or not isinstance(compute[kind].get('id'), str):
            raise ValueError('Select a valid catalog compute type')
        cloud = compute.get('cloud', 'SECURE')
        if cloud not in ('SECURE', 'COMMUNITY') or (kind == 'cpu' and cloud != 'SECURE'):
            raise ValueError('Invalid cloud tier for the selected compute')
        record = {'name': name, 'status': 'create pending', 'deadline': deadline, 'cloud': cloud,
                  'compute_kind': kind, 'compute_id': compute[kind]['id']}
        if kind == 'cpu':
            vcpu = compute[kind].get('vcpuCount')
            if type(vcpu) is not int or vcpu != 32:
                raise ValueError('CPU choice must request 32 vCPUs')
            record.update(requested_vcpu=vcpu, minimum_memory_gb=128)
        from bootstrap import prepare_bootstrap, forget_bootstrap
        bootstrap_record, bootstrap_payload = prepare_bootstrap(self.state.path.parent, name, image)
        record.update(bootstrap_record)
        records.append(record)
        try:
            self.state.write(records)
        except (OSError, ValueError):
            forget_bootstrap(record, self.state.path.parent)
            raise
        try:
            status, result = self.client.create_pod({
                'name': name, 'image': image, 'disk': 50, 'startSsh': True,
                'ports': ['22/tcp'], kind: compute[kind], 'cloud': cloud,
                **bootstrap_payload,
            })
        except RunPodError as exc:
            if not exc.uncertain and exc.status and 400 <= exc.status < 500:
                records.remove(record)
                self.state.write(records)
                forget_bootstrap(record, self.state.path.parent)
            raise
        if status == 202 or 'podId' in result or str(result.get('status', '')).upper() == 'QUEUED':
            record['queue_id'] = result.get('id')
            record['status'] = 'queued; inspect and cancel in RunPod console'
        elif status == 201 and result.get('id') and result.get('name') == name:
            record['id'] = result['id']
            record['status'] = str(result.get('status', 'created'))
        else:
            record['status'] = 'create response requires reconciliation'
        self.state.write(records)
        return result

    @staticmethod
    def _find_owned(items, identity, name):
        matches = [item for item in items if item.get('name') == name
                   or (identity and item.get('id') == identity)]
        if not matches:
            return None
        if (len(matches) != 1 or matches[0].get('name') != name
                or not isinstance(matches[0].get('id'), str) or not matches[0]['id']
                or (identity and matches[0]['id'] != identity)):
            raise ValueError('RunPod inventory does not match the owned record.')
        return matches[0]

    def _identify(self, record, pods, queues):
        name = record.get('name')
        if not isinstance(name, str) or not name:
            raise ValueError('The ownership record has no pod name.')
        pod = self._find_owned(pods, record.get('id'), name)
        queue = self._find_owned(queues, record.get('queue_id'), name)
        if pod:
            record['id'] = pod['id']
        if queue:
            record['queue_id'] = queue['id']
        return pod, queue

    def reconcile(self):
        records = self.state.read()
        if not records:
            return records
        pods, queues = self.client.list_pods(), self.client.list_queued()
        for record in records:
            if record.get('status') == self.TERMINATED:
                continue
            try:
                pod, queue = self._identify(record, pods, queues)
                if record.get('status') == self.NOT_VISIBLE:
                    if not (pod or queue):
                        continue
                    if self._has_termination_intent(record):
                        self._begin_termination(record, time.time())
                        continue
                if (record.get('status') == self.TERMINATION_FAILED and record.get('id')
                        and not (record.get('queue_id') or pod or queue)):
                    self._reconcile_failed_pod(record, records)
                    continue
                if self._has_termination_intent(record):
                    continue
                if pod:
                    record['status'] = str(pod.get('status', pod.get('desiredStatus', 'unknown')))
                elif queue:
                    record['status'] = 'Queue: ' + str(queue.get('status', 'unknown'))
                    if not record.get('id') and queue.get('status') in self._QUEUE_CLOSED:
                        detail = self._get_owned(self.client.get_queued, record['queue_id'], record['name'])
                        if detail and detail.get('status') in self._QUEUE_CLOSED and not detail.get('podId'):
                            record['status'] = self.TERMINATED
                            error = self._retire_ssh(record, records)
                            if error:
                                record['termination_error'] = error
                elif record.get('id'):
                    detail = self._get_owned(self.client.get_pod, record['id'], record['name'])
                    # An absent ID might belong to a different authenticated account.
                    record['status'] = str(detail.get('status', 'unknown')) if detail else self.NOT_VISIBLE
                else:
                    record['status'] = 'Waiting to confirm pod creation'
            except ValueError:
                if not self._has_termination_intent(record):
                    record['status'] = 'RunPod identity mismatch'
        self.state.write(records)
        return records

    def _reconcile_failed_pod(self, record, records):
        try:
            if self._get_owned(self.client.get_pod, record['id'], record['name']):
                return
            pod, queue = self._identify(record, self.client.list_pods(), self.client.list_queued())
        except (OSError, RunPodError):
            # Keep the existing failure and continue checking other rows.
            return
        if pod or queue:
            return
        if self._pod_termination_confirmed(record):
            self._mark_terminated(record)
            error = self._retire_ssh(record, records)
            if error:
                record['termination_error'] = error
        else:
            self._mark_not_visible(record)

    @classmethod
    def _has_termination_intent(cls, record):
        status = record.get('status')
        return (status in (cls.TERMINATING, cls.TERMINATION_FAILED)
                or (status == cls.NOT_VISIBLE and 'termination_started' in record)
                or bool(record.get('deadline_action'))
                or status in ('termination requested; verify in RunPod console',
                              'queue cancellation requested; verify in RunPod console'))

    @classmethod
    def needs_poll(cls, record, now):
        if record.get('status') in (cls.TERMINATED, cls.TERMINATION_FAILED, cls.NOT_VISIBLE):
            return False
        deadline = record.get('deadline')
        return (cls._has_termination_intent(record)
                or (type(deadline) in (int, float) and math.isfinite(deadline) and deadline <= now))

    @classmethod
    def _begin_termination(cls, record, now):
        record['status'] = cls.TERMINATING
        record['termination_started'] = now
        record.pop('termination_error', None)

    def request_termination(self, record):
        """Persist the intent before any provider work, using the selected identity."""
        records = self.state.read()
        matches = [item for item in records if item.get('name') == record.get('name')]
        if (not record.get('name') or len(matches) != 1 or
                any(matches[0].get(key) != record.get(key) for key in ('id', 'queue_id'))):
            raise ValueError('The selected pod changed. Review the pod list and try again.')
        current = matches[0]
        if current.get('status') not in (self.TERMINATING, self.TERMINATED):
            self._begin_termination(current, time.time())
            self.state.write(records)
        return current

    def delete(self, record):
        """Set the termination intent and advance it once."""
        self.request_termination(record)
        return self.enforce_deadlines()

    @staticmethod
    def _get_owned(get, identity, name):
        try:
            detail = get(identity)
        except RunPodError as exc:
            if exc.status == 404:
                return None
            raise
        if not isinstance(detail, dict) or detail.get('id') != identity or detail.get('name') != name:
            raise ValueError('RunPod details do not match the owned resource.')
        return detail

    @staticmethod
    def _submit_termination(record, field, delete):
        identity = record[field]
        sent = record.get('termination_sent')
        if not isinstance(sent, dict):
            sent = record['termination_sent'] = {}
        request = {'id': identity, 'started': record['termination_started']}
        if sent.get(field) == request:
            return 0
        try:
            delete(identity)
        except RunPodError as exc:
            if exc.status != 404:
                raise
        sent[field] = request
        return 1

    def _advance_termination(self, record):
        name = record['name']
        if not (record.get('id') or record.get('queue_id')):
            self._identify(record, self.client.list_pods(), self.client.list_queued())
            if not (record.get('id') or record.get('queue_id')):
                return 0

        queue_id = record.get('queue_id')
        queue = self._get_owned(self.client.get_queued, queue_id, name) if queue_id else None
        if queue:
            queue_status = queue.get('status')
            if queue_status == 'QUEUED' and not queue.get('podId'):
                return self._submit_termination(record, 'queue_id', self.client.cancel_queued)
            if queue_status == 'DEPLOYED':
                pod_id = queue.get('podId')
                if (not isinstance(pod_id, str) or not pod_id
                        or record.get('id') not in (None, pod_id)):
                    raise ValueError('The queued request has a conflicting pod identity.')
                record['id'] = pod_id
            elif queue_status not in self._QUEUE_CLOSED or queue.get('podId'):
                raise ValueError('RunPod returned an unconfirmed queue status.')

        pod_id = record.get('id')
        pod = self._get_owned(self.client.get_pod, pod_id, name) if pod_id else None
        if pod:
            return self._submit_termination(record, 'id', self.client.delete_pod)

        # Check inventories after the individual lookups. A queued request may
        # have produced a pod while its cancellation was in flight.
        pod, queue_list = self._identify(record, self.client.list_pods(), self.client.list_queued())
        if pod or queue_id != record.get('queue_id'):
            return 0
        if queue_list and (not queue or queue_list.get('status') != queue.get('status')
                           or queue_list.get('podId') != queue.get('podId')):
            return 0
        if pod_id and not self._pod_termination_confirmed(record):
            if not queue_id:
                # The current account cannot see this pod. Keep ownership and
                # SSH material because absence is not evidence of deletion.
                self._mark_not_visible(record)
            return 0
        if queue_id and not queue:
            # Cancellation can lose the race to deployment. Check the actual
            # terminal queue status, even after a successful cancel response.
            return 0
        self._mark_terminated(record)
        return 0

    @staticmethod
    def _pod_termination_confirmed(record):
        pod_id = record['id']
        sent = record.get('termination_sent')
        receipt = sent.get('id') if isinstance(sent, dict) else None
        return (receipt == pod_id or (isinstance(receipt, dict) and receipt.get('id') == pod_id)
                or record.get('deadline_action') == f'pod:{pod_id}')

    def _mark_not_visible(self, record):
        # Older failed rows may only carry their termination intent in status.
        record.setdefault('termination_started', time.time())
        record['status'] = self.NOT_VISIBLE
        record.pop('termination_error', None)

    def _mark_terminated(self, record):
        record['status'] = self.TERMINATED
        record.pop('termination_error', None)
        record.pop('termination_sent', None)
        record.pop('deadline_action', None)

    def _retire_ssh(self, record, records):
        from bootstrap import forget_bootstrap
        from host_identity import forget_host_key
        pod_id = record.get('id')
        try:
            if pod_id and not any(item is not record and item.get('id') == pod_id
                                  for item in records):
                forget_host_key(pod_id, self.state.path.parent)
            forget_bootstrap(record, self.state.path.parent)
            record.pop('ssh_identity', None)
        except (OSError, ValueError):
            return 'Pod terminated. Could not remove its local SSH files.'
        return None

    def enforce_deadlines(self, now=None):
        """Advance each termination, keeping failures isolated to their own rows."""
        now = time.time() if now is None else now
        records = self.state.read()
        count, errors = 0, []
        for record in records:
            if not self.needs_poll(record, now):
                continue
            if record.get('status') != self.TERMINATING:
                self._begin_termination(record, now)
                self.state.write(records)
            started = record.get('termination_started')
            if type(started) not in (int, float) or not math.isfinite(started):
                started = record['termination_started'] = now
            error = None
            try:
                count += self._advance_termination(record)
            except (OSError, ValueError, RunPodError) as exc:
                status = exc.status if isinstance(exc, RunPodError) else None
                if type(status) is int:
                    error = f'RunPod HTTP {status}.'
                elif isinstance(exc, ValueError):
                    error = str(exc)
                else:
                    error = 'RunPod could not confirm the request.'
                if isinstance(exc, RunPodError) and (exc.uncertain or status in (None, 429)
                                                    or (type(status) is int and status >= 500)):
                    # Retry temporary failures after rechecking ownership.
                    record['termination_error'] = error
                    error = None
            if record['status'] == self.TERMINATED:
                error = self._retire_ssh(record, records)
            elif record['status'] == self.TERMINATING and now - started >= self._TERMINATION_WAIT:
                error = error or 'RunPod did not confirm termination. Check the account and try again.'
            if error:
                if record['status'] != self.TERMINATED:
                    record['status'] = self.TERMINATION_FAILED
                record['termination_error'] = error
                errors.append(error)
            self.state.write(records)
        return count, errors


def owned_pod_details(client, record):
    """Fetch connection details for the exact locally owned pod.

    The list endpoint may omit SSH details, so connection flows must use the
    individual pod endpoint and verify that its response matches the record.
    """
    pod_id, name = record.get('id'), record.get('name')
    if not isinstance(pod_id, str) or not pod_id or not isinstance(name, str) or not name:
        raise ValueError('The ownership record is missing a pod ID or name')
    pod = client.get_pod(pod_id)
    if not isinstance(pod, dict) or pod.get('id') != pod_id or pod.get('name') != name:
        raise ValueError('RunPod pod details do not match the selected ownership record')
    return pod


def owned_pod_connection(client, record, known_hosts, state_dir):
    """Resolve the SSH trust file for this selection before opening a connection."""
    pod = owned_pod_details(client, record)
    fingerprint = known_hosts.strip()
    if fingerprint and not fingerprint.startswith('SHA256:'):
        return pod, known_hosts

    from host_identity import existing_host_key, verify_host_key
    direct = (pod.get('ssh') or {}).get('direct')
    if not isinstance(direct, dict) or not direct.get('host') or not direct.get('port'):
        raise ValueError('Direct SSH is not ready for this pod. Wait for startup and try again.')
    pod_id, host, port = pod['id'], direct['host'], direct['port']
    if fingerprint:
        path = verify_host_key(pod_id, host, port, fingerprint, state_dir)
        return pod, str(path)
    path = existing_host_key(pod_id, host, port, state_dir)
    if path is None:
        fingerprint = record.get('ssh_fingerprint')
        if not fingerprint:
            try:
                fingerprint = client.get_pod_ssh_fingerprint(pod_id)
            except RunPodError as error:
                raise ValueError(f'{error}. This older pod needs its existing SSH fingerprint. '
                                 'New pods created by this version configure SSH automatically.') from None
        path = verify_host_key(pod_id, host, port, fingerprint, state_dir)
    return pod, str(path)


def simulation_gpu(record, pod):
    # Older ownership records have no requested tier. Every new record
    # records it, and GET /pods/{id} reports it for CPU and GPU pods.
    if 'cloud' in record and (record['cloud'] not in ('SECURE', 'COMMUNITY')
                              or pod.get('cloud') != record['cloud']):
        raise ValueError('RunPod did not confirm the requested cloud tier. Check the selected pod details.')
    kind, expected = record.get('compute_kind'), record.get('compute_id')
    if kind == 'gpu':
        actual = pod.get('gpu') or {}
        if actual.get('id') != expected or type(actual.get('count')) is not int or actual['count'] != 1:
            raise ValueError('RunPod did not report exactly one GPU of the requested catalog type')
        return True
    if kind == 'cpu':
        actual = pod.get('cpu') or {}
        if actual.get('id') != expected or pod.get('gpu'):
            raise ValueError('Pod CPU identity does not match the requested CPU type')
        vcpu, memory = actual.get('vcpuCount'), actual.get('memory')
        if (type(vcpu) is not int or vcpu != record.get('requested_vcpu') or
                type(memory) not in (int, float) or not math.isfinite(memory) or
                memory < record.get('minimum_memory_gb', 128)):
            raise ValueError('RunPod did not report the requested CPU count and at least 128 GB host memory')
        return False
    raise ValueError('Pod compute type is unknown; this record cannot launch a simulation')


def clock_bounds(local_utc_uncertainty, max_clock_offset, remote_clock_drift_ppm):
    try:
        values = (float(local_utc_uncertainty), float(max_clock_offset), float(remote_clock_drift_ppm))
    except ValueError as exc:
        raise ValueError('Enter positive finite clock bounds no greater than 3600') from exc
    if any(not math.isfinite(value) or not 0 < value <= 3600 for value in values):
        raise ValueError('Enter positive finite clock bounds no greater than 3600')
    return values


def file_root(value):
    if not value.strip():
        return None
    path = Path(value).expanduser().resolve(strict=True)
    if not path.is_dir():
        raise ValueError('Local file root must be a directory')
    return path


def ssh_command(pod, known_hosts, identity=None, require_gpu=False, file_channel=False, live=False):
    from deploy import _connection
    direct = (pod.get('ssh') or {}).get('direct') or {}
    port = direct.get('port')
    host, user, options = _connection(direct.get('host'), direct.get('username'),
                                      port, known_hosts, identity or None)
    flag = (' --require-gpu' if require_gpu else '') + (' --file-channel' if file_channel or live else '') + (' --live' if live else '')
    worker = 'datapump-lab-worker' if live else 'datapump-worker'
    remote_dir = '/workspace/pump-lab-live' if live else '/workspace/pump-lab'
    return ['ssh', '-F', '/dev/null', '-T', '-p', str(port), '-l', user, *options, host,
            'python3 ' + remote_dir + '/_zLab/worker_relay.py' + flag + ' ' + remote_dir + '/build/lab/' + worker]


def deploy_selected(pod, record, known_hosts, identity, secret='', deploy_func=None, profile='simulation'):
    """Deploy an admitted selection and retain only bounded, redacted receipts."""
    from deploy import deploy, _connection
    gpu = simulation_gpu(record, pod)
    direct = (pod.get('ssh') or {}).get('direct') or {}
    host, user, port = direct.get('host'), direct.get('username'), direct.get('port')
    _connection(host, user, port, known_hosts, identity)
    deadline = record.get('deadline')
    if type(deadline) not in (int, float) or not math.isfinite(deadline):
        raise ValueError('A finite pod deadline is required for deployment')
    remaining = deadline - time.time()
    if remaining <= 0:
        raise ValueError('Pod deadline expired before deployment began')
    if profile not in ('simulation', 'live'):
        raise ValueError('Invalid deployment profile')
    output = []
    (deploy_func or deploy)(host, user, port, known_hosts, identity=identity,
                            mode='gpu' if gpu else 'cpu', timeout=min(remaining, 900 if gpu else 300), profile=profile,
                            on_output=output.append)
    lines = []
    for line in '\n'.join(output).splitlines():
        if line.startswith(('PUMP_LAB_PROFILE=', 'PUMP_LAB_MODE=', 'PUMP_LAB_DEVICE=', 'PUMP_LAB_TORCH=',
                            'PUMP_LAB_CUDA=', 'PUMP_LAB_WORKER_SHA256=', 'PUMP_LAB_CPU_MODEL=',
                            'PUMP_LAB_CPU_AFFINITY_LOGICAL=', 'PUMP_LAB_CGROUP_MEMORY_LIMIT_BYTES=',
                            'PUMP_LAB_CGROUP_CPU_QUOTA_CORES=', 'PUMP_LAB_CPU_MODEL_SCOPE=',
                            'PUMP_LAB_CGROUP_SCOPE=', 'PUMP_LAB_KERNEL_MEMORY_TOTAL_BYTES=',
                            'PUMP_LAB_RECEIPT_SCOPE=', 'PUMP_LAB_GPU_SCOPE=',
                            'PUMP_LAB_GPU_VISIBLE_MEMORY_BYTES=', 'PUMP_LAB_GPU_VISIBLE_UUID=',
                            'PUMP_LAB_GPU_PARTITION_STATUS=', 'PUMP_LAB_GPU_INVENTORY=')):
            clean = ''.join(ch if 32 <= ord(ch) < 127 else '?' for ch in line[:250])
            if secret:
                clean = clean.replace(secret, '[redacted]')
            lines.append(clean)
    return '\n'.join(lines[:19]) or 'Build succeeded; structured device and binary receipt was unavailable.'


def device_query(argv, _input, timeout):
    """Collect at most 4096 bytes, rejecting overflow and timeout."""
    process = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL, start_new_session=True)
    output = bytearray()
    end = time.monotonic() + timeout
    try:
        while True:
            remaining = end - time.monotonic()
            if remaining <= 0 or not select.select([process.stdout], [], [], remaining)[0]:
                raise ValueError('Local audio device discovery timed out')
            block = os.read(process.stdout.fileno(), 4097 - len(output))
            if not block:
                break
            output.extend(block)
            if len(output) > 4096:
                raise ValueError('Local audio device list exceeds 4096 bytes')
        # Observe exit without reaping the group leader; keeping the PID reserved
        # prevents a later killpg from signalling a reused process group.
        while True:
            info = os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
            if info is not None:
                code = info.si_status if info.si_code == os.CLD_EXITED else -info.si_status
                break
            if time.monotonic() >= end:
                raise ValueError('Local audio device discovery timed out')
            time.sleep(.01)
        return code, bytes(output)
    finally:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()
        process.stdout.close()


def local_devices(binary, runner=None):
    path = Path(binary).expanduser().resolve(strict=True)
    if not path.is_file() or not os.access(path, os.X_OK):
        raise ValueError('Local audio companion must be an executable file')
    code, output = (runner or device_query)([str(path), '--list-devices'], None, 10)
    if code or len(output) > 4096:
        raise ValueError('Local audio device discovery failed')
    try:
        values = json.loads(output)
        if not isinstance(values, list) or len(values) > 128:
            raise ValueError()
        result = {}
        for item in values:
            device, description = item['id'], item['description']
            if (not isinstance(device, str) or not device or len(device) > 512 or
                    not isinstance(description, str) or len(description) > 512 or
                    any(ord(c) < 32 for c in device + description)):
                raise ValueError()
            result[device + ' — ' + description] = device
        return result
    except (ValueError, TypeError, KeyError) as exc:
        raise ValueError('Invalid local audio device list') from exc


def live_command(remote, binary, device, capture_bytes, playback_bytes, capture_frames,
                 playback_prebuffer, local_uncertainty, max_offset, remote_drift):
    local, maximum, drift = clock_bounds(local_uncertainty, max_offset, remote_drift)
    path = Path(binary).expanduser().resolve(strict=True)
    if not path.is_file() or not os.access(path, os.X_OK):
        raise ValueError('Local audio companion must be an executable file')
    if not device or len(device) > 512 or any(ord(ch) < 32 for ch in device):
        raise ValueError('Enter a local audio device ID')
    try:
        cap, play, frames, prebuffer = map(int, (capture_bytes, playback_bytes, capture_frames, playback_prebuffer))
    except ValueError as exc:
        raise ValueError('Enter integer audio memory and frame limits') from exc
    if not 16896 <= cap <= (1 << 40) or not 16896 <= play <= (1 << 40):
        raise ValueError('Capture and playback limits must each be 16896 bytes to 1 TiB')
    if not 0 < frames <= (1 << 63) - 1:
        raise ValueError('Capture frame limit must be a positive 64-bit value')
    if not 0 < prebuffer <= min(131072, (play - 512) // 4):
        raise ValueError('Playback prebuffer exceeds the playback capacity or 131072 frames')
    companion = [str(path), '--device', device, '--capture-bytes', str(cap),
                 '--playback-bytes', str(play), '--capture-frames', str(frames),
                 '--playback-prebuffer-frames', str(prebuffer)]
    transport = Path(__file__).resolve().parent / 'audio/transport.py'
    return [sys.executable, str(transport), '--max-clock-offset', str(maximum),
            '--local-utc-uncertainty', str(local), '--remote-clock-drift-ppm', str(drift),
            '--remote-json', json.dumps(remote), '--local-json', json.dumps(companion), '--report-resources']


class App:
    _CLOSE_GRACE = 6

    def __init__(self, root, *, repository_directory=None):
        self.root, self.state, self.choices = root, State(), []
        configure_dark_theme(root)
        self.busy = False
        self.create_interrupted = False
        self.preferred_compute = None
        self.bridge = None
        self.bridge_close_failed = False
        self.browser_attached = False
        self.session_record = None
        self.session_is_new = False
        self.setup_cancelled = None
        self.exit_after = None
        self.close_after = None
        self.close_attempted = set()
        self.close_generation = 0
        self.confirming_create = False
        self.window_closed = threading.Event()
        self.next_provider_poll = 0
        self.closing_bridge = False
        self.deploying = False
        self.enumerating = False
        self.setting_up = False
        self.setup_after = None
        self.devices = {}
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.title('Robust RunPod Lab')
        root.geometry('920x420')
        root.minsize(640, 320)
        box = ttk.Frame(root, padding=12)
        box.pack(fill='both', expand=True)
        self.key = tk.StringVar()
        self.compute = tk.StringVar()
        self.cloud = tk.StringVar(value='SECURE')
        self.image = tk.StringVar(value=IMAGE)
        self.deadline_minutes = tk.StringVar(value='60')
        self.known_hosts = tk.StringVar()
        self.identity = tk.StringVar()
        self.files = tk.StringVar()
        self.deployment_receipt = tk.StringVar()
        self.local_utc_uncertainty = tk.StringVar()
        self.max_clock_offset = tk.StringVar()
        self.remote_clock_drift_ppm = tk.StringVar()
        self.profile = tk.StringVar(value='simulation')
        self.audio_binary = tk.StringVar()
        self.audio_device = tk.StringVar(value='default')
        self.capture_bytes = tk.StringVar()
        self.playback_bytes = tk.StringVar()
        self.capture_frames = tk.StringVar()
        self.playback_prebuffer = tk.StringVar()
        self.status = tk.StringVar(value='Enter your RunPod API key to load available compute.')
        ttk.Label(box, text='RunPod API key').grid(row=0, column=0, sticky='w', padx=(0, 12))
        self.key_entry = ttk.Entry(box, textvariable=self.key, show='•')
        self.key_entry.grid(row=0, column=1, sticky='ew')
        self.key_entry.bind('<FocusOut>', self.key_edited)
        self.key_entry.bind('<Return>', self.key_edited)
        ttk.Label(box, text='Compute').grid(row=1, column=0, sticky='w', padx=(0, 12), pady=(8, 0))
        self.dropdown = ttk.Combobox(box, textvariable=self.compute, state='readonly')
        self.dropdown.grid(row=1, column=1, sticky='ew', pady=(8, 0))
        actions = ttk.Frame(box)
        actions.grid(row=2, column=0, columnspan=2, sticky='ew', pady=12)
        ttk.Button(actions, text='Start simulation', command=self.start_simulation).pack(side='left')
        ttk.Button(actions, text='End simulation', command=self.end_simulation).pack(side='left', padx=8)
        ttk.Button(actions, text='Advanced', command=self.toggle_advanced).pack(side='right')
        ttk.Label(box, text='Pods').grid(row=3, column=0, sticky='w')
        self.list = tk.Listbox(box, height=6, exportselection=False,
                               background='#171b22', foreground='#f0f2f5',
                               selectbackground='#365f91', selectforeground='#f0f2f5',
                               highlightbackground='#697584', highlightcolor='#365f91')
        self.list.grid(row=4, column=0, columnspan=2, sticky='nsew', pady=(4, 8))
        self.fingerprint_pod = None
        self.known_hosts.trace_add('write', self.bind_fingerprint)
        self.list.bind('<<ListboxSelect>>', self.clear_stale_fingerprint)
        status_box = ttk.Frame(box)
        status_box.grid(row=5, column=0, columnspan=2, sticky='ew')
        self.status_label = ttk.Label(status_box, textvariable=self.status, wraplength=760)
        self.status_label.pack(anchor='w')
        self.status_label.bind('<Double-Button-1>', self.copy_status)
        self.advanced = ttk.LabelFrame(box, text='Advanced', padding=10)
        panel = self.advanced
        panel.grid(row=6, column=0, columnspan=2, sticky='ew', pady=(10, 0))
        panel.columnconfigure(1, weight=1)
        for row, (label, field) in enumerate((
            ('Container image', self.image), ('Deadline (minutes)', self.deadline_minutes),
            ('SSH trust', self.known_hosts), ('SSH identity', self.identity),
            ('Local file root', self.files),
        )):
            ttk.Label(panel, text=label).grid(row=row, column=0, sticky='w', padx=(0, 12))
            ttk.Entry(panel, textvariable=field).grid(row=row, column=1, sticky='ew')
        options_row = ttk.Frame(panel)
        options_row.grid(row=5, column=0, columnspan=2, sticky='w', pady=6)
        ttk.Label(options_row, text='Cloud tier').pack(side='left')
        ttk.Combobox(options_row, textvariable=self.cloud, values=('SECURE', 'COMMUNITY'),
                     width=12, state='readonly').pack(side='left', padx=(4, 12))
        ttk.Label(options_row, text='Worker profile').pack(side='left')
        ttk.Combobox(options_row, textvariable=self.profile, values=('simulation', 'live'),
                     width=12, state='readonly').pack(side='left', padx=(4, 0))
        self.cloud.trace_add('write', self.tier_changed)
        ttk.Label(panel, text='Saved key file').grid(row=6, column=0, sticky='w')
        ttk.Label(panel, text='_zLab/runpod-api-key').grid(row=6, column=1, sticky='w')
        advanced_actions = ttk.Frame(panel)
        advanced_actions.grid(row=7, column=0, columnspan=2, sticky='w', pady=(6, 0))
        ttk.Button(advanced_actions, text='Refresh compute', command=self.catalog).pack(side='left')
        ttk.Button(advanced_actions, text='Clear key', command=self.clear_key).pack(side='left', padx=4)
        ttk.Button(advanced_actions, text='Configure live audio limits', command=self.audio_settings).pack(side='left')
        ttk.Button(advanced_actions, text='Deployment receipt', command=self.show_receipt).pack(side='left', padx=4)
        ttk.Button(advanced_actions, text='Dismiss missing request', command=self.dismiss_missing).pack(side='left')
        self.advanced.grid_remove()
        box.columnconfigure(1, weight=1)
        box.rowconfigure(4, weight=1)
        self.refresh()
        self.lifecycle_after = self.root.after(1000, self.lifecycle_tick)
        self.root.bind('<Destroy>', self.window_destroyed, add='+')
        from credentials import RepositoryApiKeyStore
        location = {} if repository_directory is None else {'repository_directory': repository_directory}
        self.key_store = RepositoryApiKeyStore(self.state.path.parent, **location)
        self.catalog_key = None
        self.saved_key = None
        try:
            self.saved_key = self.key_store.load()
            self.key.set(self.saved_key)
        except (OSError, ValueError):
            self.status.set('Could not read the saved API key. Check ' + str(self.key_store.path))
        if self.key.get():
            self.root.after_idle(self.initialize_saved_session)

    def initialize_saved_session(self):
        if self.records:
            self.poll_owned(reconcile=True, after=self.startup_catalog)
        else:
            self.startup_catalog()

    def toggle_advanced(self):
        if self.advanced.winfo_manager():
            self.advanced.grid_remove()
            self.root.geometry(f'{self.root.winfo_width()}x420')
        else:
            self.advanced.grid()
            if self.root.winfo_height() < 590:
                self.root.geometry(f'{self.root.winfo_width()}x590')

    def window_destroyed(self, event):
        if event.widget is self.root:
            self.window_closed.set()
            for attribute in ('close_after', 'lifecycle_after', 'setup_after'):
                scheduled = getattr(self, attribute)
                if scheduled is not None:
                    self.root.after_cancel(scheduled)
                    setattr(self, attribute, None)

    def post_to_window(self, callback):
        if self.window_closed.is_set():
            return False
        try:
            self.root.after(0, callback)
            return True
        except (RuntimeError, tk.TclError):
            return False

    def show_receipt(self):
        messagebox.showinfo('Deployment receipt', self.deployment_receipt.get() or 'No receipt available.')

    def dismiss_missing(self):
        if self.busy or self.session_record or self.deploying:
            self.status.set('Wait for the current RunPod operation to finish.')
            return
        record = self.selected_record()
        if (not record or record.get('status') != Ownership.TERMINATION_FAILED
                or record.get('id') or record.get('queue_id')):
            self.status.set('Select a failed request with no confirmed RunPod ID.')
            return
        key = self.key.get().strip()
        if not messagebox.askyesno(
            'Dismiss missing request',
            f'In the RunPod account for this API key, check that no pod or queued request is named {record["name"]}. '
            'Have you verified that this request is missing?',
        ):
            return
        if self.busy or self.session_record or self.key.get().strip() != key:
            self.status.set('The RunPod session changed. Review the request and try again.')
            return

        def verify(client):
            name = record['name']
            pods, queued = client.list_pods(), client.list_queued()
            if any(row.get('name') == name for row in (*pods, *queued)):
                raise ValueError('RunPod still lists this request. Use End simulation to retry cleanup.')

        def discard(_result):
            records = self.state.read()
            if record not in records:
                self.status.set('The selected request changed. Review the pod list.')
                return
            from bootstrap import forget_bootstrap
            try:
                forget_bootstrap(record, self.state.path.parent)
                records.remove(record)
                self.state.write(records)
            except (OSError, ValueError):
                self.status.set('Could not dismiss the request. Check the lab state directory.')
                return
            self.refresh()
            self.status.set('Missing request dismissed.')

        self.background(verify, discard)

    def key_edited(self, _event=None):
        if self.busy or (self.session_record and self.saved_key):
            return
        if self.save_key() and self.key.get().strip() != self.catalog_key:
            self.tier_changed()
            if not self.key.get().strip():
                self.status.set('Enter your RunPod API key to load available compute.')

    def startup_catalog(self):
        if self.exit_after is not None:
            return
        if self.busy:
            self.root.after(100, self.startup_catalog)
        elif self.key.get().strip():
            self.catalog()

    def save_key(self):
        key = self.key.get().strip()
        try:
            if key != self.saved_key:
                self.key_store.save(key)
                self.saved_key = key
            return True
        except (OSError, ValueError):
            self.status.set('Could not save the API key. Check ' + str(self.key_store.path))
            return False

    def clear_key(self):
        if self.session_record or self.busy or self.deploying or any(
                Ownership._has_termination_intent(record) for record in self.records):
            self.status.set('Wait for the simulation and pending termination before clearing the API key.')
            return
        self.key.set('')
        if self.save_key():
            self.tier_changed()
            self.status.set('Saved API key removed.')

    def copy_status(self, _event=None):
        """Copy exactly the visible status text, never other form fields."""
        message = self.status.get()
        if not message:
            return
        try:
            self.root.clipboard_clear()
            self.root.clipboard_append(message)
        except tk.TclError:
            self.status.set('Could not copy the status message to the clipboard.')

    def selected_pod_id(self):
        selected = self.list.curselection()
        return self.records[selected[0]].get('id') if selected else None

    def bind_fingerprint(self, *_):
        self.fingerprint_pod = (self.selected_pod_id()
                                if self.known_hosts.get().strip().startswith('SHA256:') else None)

    def clear_stale_fingerprint(self, _event=None):
        if self.known_hosts.get().strip().startswith('SHA256:') and self.fingerprint_pod != self.selected_pod_id():
            self.known_hosts.set('')
            self.status.set('The selected pod changed. Its console fingerprint can be entered in the SSH trust field.')
            return True
        return False

    def lifecycle_tick(self):
        record = self.active_record()
        if (self.exit_after is None and record and self.bridge and not self.closing_bridge
                and not self.bridge_close_failed and not self.busy):
            state, detail = self.bridge.status()
            if state == 'active':
                self.browser_attached = True
            elif state == 'ended' or (state == 'failed' and self.browser_attached):
                self.request_end(record, notice=detail if state == 'failed' else '')
            elif state == 'failed':
                self.setup_failed(detail or 'The browser session failed.')
        if time.monotonic() >= self.next_provider_poll:
            self.enforce_deadlines()
        self.finish_session_end()
        self.lifecycle_after = self.root.after(1000, self.lifecycle_tick)

    def enforce_deadlines(self):
        if self.busy or not self.key.get().strip():
            return
        now = time.time()
        records = self.state.read()
        due = any(Ownership.needs_poll(record, now) for record in records)
        unresolved = any(
            (not record.get('id')
             and record.get('status') not in (Ownership.TERMINATED, Ownership.TERMINATION_FAILED))
            or (record.get('id') and record.get('status') in (Ownership.NOT_VISIBLE, Ownership.TERMINATION_FAILED))
            for record in records
        )
        self.next_provider_poll = time.monotonic() + (2 if due else 15)
        if due or (unresolved and not self.setting_up):
            self.poll_owned(reconcile=unresolved and not due)

    def poll_owned(self, *, reconcile=False, after=None):
        def work(client):
            owner = Ownership(self.state, client)
            if reconcile:
                owner.reconcile()
            return owner.enforce_deadlines()

        def done(result):
            self.refresh()
            if result[1]:
                self.status.set(result[1][0])
            self.finish_session_end()
            if after:
                after()

        def failed(_message):
            if after:
                after()

        self.background(work, done, failed, show_progress=False)

    def refresh(self):
        selected = self.list.curselection()
        previous = getattr(self, 'records', [])
        name = (previous[selected[0]].get('name')
                if selected and selected[0] < len(previous) else None)
        self.records = self.state.read()
        self.list.delete(0, 'end')
        for index, r in enumerate(self.records):
            deadline = r.get('deadline')
            when = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(deadline)) if isinstance(deadline, (int, float)) else 'not set'
            status = r.get('status', 'Unknown')
            compute = r.get('compute_id') or r.get('name')
            identity = r.get('id') or r.get('queue_id') or 'ID pending'
            self.list.insert('end', f'{status}  |  {compute}  |  {identity}  |  deadline {when}')
            if name is not None and r.get('name') == name:
                self.list.selection_set(index)
                self.list.activate(index)
                self.list.see(index)
        self.clear_stale_fingerprint()
        key_in_use = self.session_record or any(
            record.get('status') == Ownership.TERMINATING for record in self.records)
        self.key_entry.state(['disabled' if key_in_use and self.key.get().strip() else '!disabled'])

    def background(self, fn, done, failed=None, *, show_progress=True):
        if self.busy:
            return
        key = self.key.get().strip()
        if not key:
            self.status.set('Enter an API key first.')
            return
        if not self.save_key():
            return
        self.busy = True
        if show_progress:
            self.status.set('Contacting RunPod…')
        def work():
            error = None
            try:
                result = fn(RunPodClient(key))
            except Exception as exc:
                # Avoid exception text that could include user-entered secrets.
                result, error = None, str(exc).replace(key, '[redacted]')
            self.post_to_window(lambda: finish(result, error))
        def finish(result, error):
            if self.window_closed.is_set():
                return
            self.busy = False
            self.refresh()
            if error:
                self.status.set(error)
                if failed:
                    failed(error)
            else:
                done(result)
            if self.create_interrupted and not self.busy:
                self.create_interrupted = False
                self.status.set(self.status.get() + ' Pod creation was interrupted. Click Start simulation again to review a fresh quote.')
        threading.Thread(target=work, daemon=True).start()

    def tier_changed(self, *_):
        self.preferred_compute = self.selected_compute() or self.preferred_compute
        self.choices = []
        self.compute.set('')
        self.dropdown['values'] = []
        if self.key.get().strip():
            self.root.after_idle(self.startup_catalog)

    def selected_compute(self):
        selected = self.compute.get()
        return next((value for label, value in self.choices if label == selected), None)

    def catalog(self):
        if self.exit_after is not None:
            return
        cloud = self.cloud.get()
        key = self.key.get().strip()
        def done(result):
            if self.cloud.get() != cloud or self.key.get().strip() != key:
                self.root.after_idle(self.startup_catalog)
                return
            self.catalog_key = key
            previous = self.selected_compute() or self.preferred_compute
            if previous:
                self.preferred_compute = previous
                previous = dict(previous, cloud=cloud)
            choices, failures = result
            self.choices = choices
            self.dropdown['values'] = [label for label, _ in choices]
            label = next((label for label, value in choices if value == previous), '')
            if not previous and choices:
                label = choices[0][0]
            self.compute.set(label)
            if failures:
                prefix = 'Catalog partially loaded. ' if choices else 'Catalog unavailable. '
                self.status.set(prefix + '; '.join(failures) + '.')
            elif not choices:
                self.status.set('Catalog loaded, but no supported compute options were found for this tier.')
            elif previous and not label:
                self.status.set('The selected option is no longer in the catalog. Choose a compute option.')
            else:
                self.status.set('Choose compute, then start the simulation.')
        self.background(lambda c: load_catalog(c, cloud), done)

    def create(self, settings):
        if self.exit_after is not None or self.window_closed.is_set():
            return
        if self.setting_up or self.deploying:
            self.status.set('Wait for the current setup to finish.')
            return
        selected = self.selected_compute()
        image = self.image.get().strip()
        if not selected or not image:
            self.status.set('Choose catalog compute and enter an image.')
            return
        deadline_text = self.deadline_minutes.get()
        try:
            minutes = int(deadline_text)
            if not 1 <= minutes <= 1440:
                raise ValueError()
        except ValueError:
            self.status.set('Deadline must be 1–1440 minutes.')
            return
        key = self.key.get().strip()
        close_generation = self.close_generation
        def settings_changed():
            return (self.selected_compute() != selected or self.key.get().strip() != key
                    or self.image.get().strip() != image or self.deadline_minutes.get() != deadline_text)
        def confirm(quote):
            if self.close_generation != close_generation or self.window_closed.is_set():
                return
            if settings_changed():
                self.status.set('The pod settings changed. Review the selection and start again.')
                return
            prompt = (f'{quote} on {selected["cloud"].title()} Cloud\n'
                      'The hourly rate is the catalog compute list price. Storage may add to the charge.\n'
                      f'Image: {image}\n'
                      f'Create one pod with a {minutes}-minute deadline? '
                      'Automatic termination works only while this app is open with the API key entered. '
                      'Charges can continue after it closes.')
            if selected['cloud'] == 'COMMUNITY':
                prompt += ('\n\nThe worker needs direct SSH via a public IP. '
                           'The Community Cloud stock estimate does not verify public IP availability.')
            self.confirming_create = True
            try:
                approved = messagebox.askyesno('Billable RunPod', prompt)
            except tk.TclError:
                if self.window_closed.is_set():
                    return
                raise
            finally:
                self.confirming_create = False
            if self.close_generation != close_generation or self.window_closed.is_set():
                return
            if not approved:
                self.status.set('Pod creation cancelled.')
                return
            # Tk services events while its modal confirmation is open. An
            # expired pod still needs immediate attention during that time.
            if self.busy:
                self.create_interrupted = True
                self.status.set('Another provider operation is running. Pod creation was interrupted.')
                return
            if settings_changed():
                self.status.set('The pod settings changed. Review the selection and start again.')
                return
            deadline = time.time() + minutes * 60
            names = {record.get('name') for record in self.state.read()}

            def created(_result):
                record = next((r for r in self.records if r.get('name') not in names), None)
                if record:
                    self.select_record(record)
                    if self.exit_after is not None:
                        self.session_record = record
                        self.request_end(record)
                    else:
                        self.prepare_worker(record, settings, new=True)

            def failed(_message):
                # A timed-out POST can leave a real resource under the saved intent.
                # Recover only that intent, never send another create.
                created(None)

            self.background(lambda c: Ownership(self.state, c).create(selected, image, deadline), created, failed)
        self.background(lambda c: creation_quote(c, selected), confirm)

    def select_record(self, record):
        self.refresh()
        self.list.selection_clear(0, 'end')
        for index, item in enumerate(self.records):
            if item.get('name') == record.get('name'):
                self.list.selection_set(index)
                self.list.activate(index)
                self.list.see(index)
                break

    def session_settings(self):
        profile = self.profile.get()
        if profile not in ('simulation', 'live'):
            raise ValueError('Choose a worker profile.')
        files = file_root(self.files.get())
        audio = (self.audio_binary.get(), self.devices.get(self.audio_device.get(), self.audio_device.get()),
                 self.capture_bytes.get(), self.playback_bytes.get(), self.capture_frames.get(),
                 self.playback_prebuffer.get(), self.local_utc_uncertainty.get(),
                 self.max_clock_offset.get(), self.remote_clock_drift_ppm.get())
        if profile == 'live':
            live_command(['ssh'], *audio)
        return profile, files, audio

    def selected_record(self):
        selected = self.list.curselection()
        return self.records[selected[0]] if selected else None

    def active_record(self):
        if self.session_record:
            name = self.session_record['name']
            return next((record for record in self.records if record.get('name') == name), None)
        return None

    def start_simulation(self):
        if self.exit_after is not None:
            return
        if self.bridge:
            self.status.set('Choose End simulation to retry closing the previous connection.' if self.bridge_close_failed
                            else 'The simulation is already open in your browser.')
            return
        if self.session_record or self.busy or self.setting_up or self.deploying or self.closing_bridge:
            self.status.set('Wait for the current setup or termination to finish.')
            return
        try:
            settings = self.session_settings()
        except (OSError, ValueError) as error:
            self.status.set(str(error))
            return
        record = self.selected_record()
        if record and record.get('status') not in (Ownership.TERMINATED, Ownership.NOT_VISIBLE):
            if Ownership._has_termination_intent(record) or Ownership.needs_poll(record, time.time()):
                self.status.set('This pod is ending. Select another pod or wait for termination.')
                return
            self.prepare_worker(record, settings)
        elif self.key.get().strip() != self.catalog_key:
            self.catalog()
        else:
            self.create(settings)

    def prepare_worker(self, record, settings, *, new=False):
        deadline = record.get('deadline')
        if type(deadline) not in (int, float) or not math.isfinite(deadline) or deadline <= time.time():
            self.status.set('The selected pod deadline is missing or expired.')
            self.enforce_deadlines()
            return
        key = self.key.get().strip()
        if not key or not self.save_key():
            self.status.set('Enter a RunPod API key to start the simulation.')
            return
        self.session_record = record
        self.session_is_new = new
        self.setting_up = True
        cancelled = self.setup_cancelled = threading.Event()
        self.refresh()
        known_hosts = self.known_hosts.get()
        identity = self.identity.get().strip() or record.get('ssh_identity') or None
        ready_until = min(deadline, time.time() + 600)
        profile, files, audio = settings
        name = record['name']

        def connect(client):
            current = self.active_record()
            if not current or current.get('status') in (Ownership.TERMINATED, Ownership.TERMINATING,
                                                        Ownership.TERMINATION_FAILED):
                return None
            if not current.get('id'):
                records = Ownership(self.state, client).reconcile()
                current = next((item for item in records if item.get('name') == name), None)
                if not current or not current.get('id'):
                    return None
            if record.get('id') and current['id'] != record['id']:
                raise ValueError('The selected pod identity changed.')
            pod = owned_pod_details(client, current)
            if pod.get('status') in ('ERROR', 'EXITED', 'TERMINATED', 'FAILED', 'STOPPED'):
                raise ValueError('The selected pod is ' + pod['status'] + '.')
            if pod.get('status') != 'RUNNING' or not (pod.get('ssh') or {}).get('direct'):
                return None
            simulation_gpu(current, pod)
            try:
                return current, *owned_pod_connection(client, current, known_hosts, self.state.path.parent)
            except ValueError as error:
                if str(error).startswith(('Could not read a bounded SSH host key', 'Direct SSH is not ready')):
                    return None
                raise

        def check_ready():
            self.setup_after = None
            if cancelled.is_set():
                return
            if self.key.get().strip() != key:
                self.setup_failed('The API key changed during setup.')
                return
            if time.time() >= ready_until:
                self.setup_failed('Timed out waiting for the pod and its authenticated SSH endpoint.')
                self.enforce_deadlines()
                return
            if self.busy:
                self.setup_after = self.root.after(250, check_ready)
                return
            self.background(connect, connected, self.setup_failed, show_progress=False)

        def connected(connection):
            if cancelled.is_set():
                return
            if connection is None:
                self.status.set('Waiting for the RunPod instance to start.')
                self.setup_after = self.root.after(2000, check_ready)
                return
            current, pod, pin = connection
            self.session_record = current
            self.deploying = True
            self.status.set('Preparing the simulation on RunPod…')

            def work():
                bridge = None
                receipt = ''
                error = ''
                try:
                    receipt = deploy_selected(pod, current, pin, identity, key, profile=profile)
                    if not cancelled.is_set():
                        command = ssh_command(pod, pin, identity, simulation_gpu(current, pod),
                                              files is not None, live=profile == 'live')
                        if profile == 'live':
                            command = live_command(command, *audio)
                        from browser_bridge import start
                        remaining = deadline - time.time()
                        if remaining <= 0:
                            raise ValueError('The pod deadline expired during setup.')
                        bridge = start(command, files=files, live=profile == 'live', managed=True,
                                       ready_timeout=min(30, remaining))
                except Exception as exc:
                    error = str(exc).replace(key, '[redacted]')[:1000]
                posted = self.post_to_window(lambda: deployed(current, receipt, bridge, error))
                if not posted and bridge is not None:
                    try:
                        bridge.close()
                    except Exception:
                        pass

            threading.Thread(target=work, daemon=True).start()

        def deployed(current, receipt, bridge, error):
            if self.busy and not cancelled.is_set():
                self.root.after(100, lambda: deployed(current, receipt, bridge, error))
                return
            self.deploying = False
            self.bridge = bridge
            self.browser_attached = False
            if cancelled.is_set():
                self.release_bridge()
                self.finish_session_end()
                return
            if error:
                self.setup_failed(error)
                return
            try:
                records = self.state.read()
                row = next(item for item in records if item.get('id') == current['id'] and item.get('name') == name)
                if Ownership._has_termination_intent(row) or time.time() >= deadline:
                    raise ValueError('The pod deadline expired during setup.')
                row.setdefault('deployments', {})[profile] = receipt
                row['status'] = 'Running'
                self.state.write(records)
                self.deployment_receipt.set(receipt)
                if not webbrowser.open(bridge.url):
                    raise ValueError('Could not open the simulation in your browser.')
                self.setting_up = False
                self.status.set('Simulation opened. Closing the browser ends its pod.' if profile == 'simulation'
                                else 'Experimental Live/Duplex opened. Closing the browser ends its pod.')
                self.refresh()
            except Exception as exc:
                self.setup_failed(str(exc).replace(key, '[redacted]')[:1000])

        self.status.set('Waiting for the RunPod instance to start.')
        check_ready()

    def cancel_setup(self):
        self.setting_up = False
        if self.setup_cancelled:
            self.setup_cancelled.set()
        if self.setup_after:
            self.root.after_cancel(self.setup_after)
            self.setup_after = None

    def setup_failed(self, message):
        self.cancel_setup()
        self.release_bridge()
        if self.exit_after is not None:
            return
        record = self.active_record()
        if record and self.session_is_new:
            self.request_end(record, notice=f'Setup stopped. {message}')
        else:
            self.session_record = None
            self.status.set(f'Setup stopped. {message} Start again to retry the selected pod or end it.')
            self.refresh()

    def release_bridge(self):
        bridge = self.bridge
        if bridge is None or self.closing_bridge:
            return
        self.closing_bridge = True
        self.bridge_close_failed = False

        def work():
            for attempt in range(2):
                try:
                    bridge.close()
                    self.post_to_window(lambda: finished(False))
                    return
                except Exception:
                    if attempt == 0:
                        time.sleep(0.25)
            self.post_to_window(lambda: finished(True))

        def finished(failed):
            self.closing_bridge = False
            self.bridge_close_failed = failed
            if not failed:
                self.bridge = None
            self.finish_session_end()
            if failed:
                self.status.set(self.status.get() + ' Choose End simulation to retry closing the local connection.')

        threading.Thread(target=work, daemon=True).start()

    def end_simulation(self):
        if self.busy:
            self.status.set('Wait for the current RunPod request to finish.')
            return
        active = self.active_record()
        record = active or self.selected_record()
        if not record or record.get('status') == Ownership.TERMINATED:
            if self.bridge_close_failed:
                self.release_bridge()
                return
            self.status.set('Select a pod to end its simulation.')
            return
        if not active and not messagebox.askyesno('End simulation', 'Terminate the selected pod and discard its storage?'):
            return
        self.request_end(record)

    def request_end(self, record, *, notice=''):
        if self.busy:
            return
        if not self.key.get().strip() or not self.save_key():
            self.status.set('Enter a RunPod API key to terminate the selected pod.')
            return
        try:
            marked = Ownership(self.state, None).request_termination(record)
        except (OSError, ValueError) as error:
            self.status.set(str(error))
            return
        self.cancel_setup()
        self.session_record = marked
        self.end_notice = notice
        self.release_bridge()
        self.status.set(' '.join(filter(None, (notice, 'Terminating the pod…'))))
        self.refresh()
        self.enforce_deadlines()

    def finish_session_end(self):
        record = self.active_record()
        status = record.get('status') if record else None
        settled = (Ownership.TERMINATED, Ownership.NOT_VISIBLE, Ownership.TERMINATION_FAILED)
        if status == Ownership.TERMINATING or status in settled:
            self.cancel_setup()
            if not self.bridge_close_failed:
                self.release_bridge()
        if status in settled:
            if self.closing_bridge or self.deploying or self.busy:
                return
            if self.exit_after is None:
                self.session_record = None
                self.refresh()
            if status == Ownership.TERMINATION_FAILED:
                error = record.get('termination_error', '')
                self.status.set(f'Termination failed. {error} Select the pod and choose End simulation to retry.')
            elif status == Ownership.NOT_VISIBLE:
                self.status.set('The pod is no longer visible to this RunPod API key.')
            else:
                self.status.set(' '.join(filter(None, (getattr(self, 'end_notice', ''), 'Pod terminated.'))))

    def close(self):
        if self.exit_after is not None or self.window_closed.is_set():
            return
        self.close_generation += 1
        self.exit_after = time.monotonic() + self._CLOSE_GRACE
        self.close_attempted.clear()
        self.cancel_setup()
        if self.bridge:
            self.release_bridge()
        self.status.set('Closing RunPod Lab…')
        self.next_provider_poll = 0
        self.check_close()

    def check_close(self):
        self.close_after = None
        record = self.active_record()
        if not record or record.get('status') in (Ownership.TERMINATED, Ownership.NOT_VISIBLE):
            record = next((item for item in self.records if item.get('status') == Ownership.TERMINATING), None)
        if record:
            self.session_record = record
            status = record.get('status')
            if status == Ownership.TERMINATING:
                if time.monotonic() >= self.next_provider_poll:
                    self.enforce_deadlines()
            elif status != Ownership.TERMINATION_FAILED and not self.busy and record['name'] not in self.close_attempted:
                self.close_attempted.add(record['name'])
                self.request_end(record)
        elif not (self.bridge or self.busy or self.deploying or self.enumerating or self.confirming_create):
            if self.save_key():
                self.root.destroy()
                return

        if time.monotonic() < self.exit_after:
            self.close_after = self.root.after(100, self.check_close)
            return
        if messagebox.askyesno(
            'Close RunPod Lab?',
            'A RunPod or local operation is still unresolved. Close the lab anyway? '
            'Saved pod records will be checked again when you reopen the lab. '
            'Check RunPod for any pod that may still be running.',
            default=messagebox.NO, parent=self.root,
        ):
            self.root.destroy()
            return
        self.exit_after = None
        self.finish_session_end()

    def audio_settings(self):
        window = tk.Toplevel(self.root)
        window.configure(background='#20242c')
        window.title('Experimental live audio limits')
        ttk.Label(window, text='Local companion executable').grid(row=0, column=0, sticky='w')
        ttk.Entry(window, textvariable=self.audio_binary, width=60).grid(row=0, column=1, sticky='ew')
        ttk.Label(window, text='Local audio device').grid(row=1, column=0, sticky='w')
        dropdown = ttk.Combobox(window, textvariable=self.audio_device, values=tuple(self.devices))
        dropdown.grid(row=1, column=1, sticky='ew')
        def refresh_devices():
            if self.enumerating:
                return
            self.enumerating = True
            binary = self.audio_binary.get()
            self.status.set('Discovering local audio devices…')
            def work():
                try:
                    found = local_devices(binary)
                    error = None
                except Exception as exc:
                    found, error = None, str(exc)
                self.post_to_window(lambda: finish(found, error))
            def finish(found, error):
                self.enumerating = False
                if error:
                    self.status.set('Audio device discovery failed: ' + error)
                else:
                    self.devices = found
                    if window.winfo_exists():
                        dropdown['values'] = tuple(found)
                    self.status.set(f'Found {len(found)} local audio devices.')
            threading.Thread(target=work, daemon=True).start()
        ttk.Button(window, text='Refresh devices', command=refresh_devices).grid(row=1, column=2)
        fields = [('Capture spool limit (bytes)', self.capture_bytes), ('Playback buffer limit (bytes)', self.playback_bytes),
                  ('Capture hard limit (frames)', self.capture_frames), ('Playback prebuffer (frames)', self.playback_prebuffer),
                  ('Verified local UTC uncertainty (s)', self.local_utc_uncertainty), ('Max remote/local offset (s)', self.max_clock_offset),
                  ('Verified relative clock drift (ppm)', self.remote_clock_drift_ppm)]
        for row, (name, variable) in enumerate(fields, start=2):
            ttk.Label(window, text=name).grid(row=row, column=0, sticky='w')
            ttk.Entry(window, textvariable=variable, width=60).grid(row=row, column=1, sticky='ew')
        end = len(fields) + 2
        ttk.Label(window, text='Limits must cover the full episode, quiet slots and filter tails. Exhaustion fails capture. Native admission checks local headroom. Browser receipts describe audio allocations, not the whole process. Clock bounds must be independently established.', wraplength=650).grid(row=end, column=0, columnspan=3, sticky='w')
        ttk.Button(window, text='Close', command=window.destroy).grid(row=end+1, column=1, sticky='e')



def main():
    import fcntl
    global tk, ttk, messagebox
    try:
        import tkinter as tk
        from tkinter import ttk, messagebox
    except ModuleNotFoundError as error:
        if error.name not in ('tkinter', '_tkinter'):
            raise
        raise SystemExit(
            'RunPod Lab requires Python Tk. On Debian or Ubuntu, install it with '
            '`sudo apt install python3-tk`, then run the app again. For a custom '
            'Python interpreter, install its matching Tk support.'
        ) from error
    state = State()
    state.path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    lock = open(state.path.parent / 'gui.lock', 'a')
    os.chmod(lock.name, 0o600)
    try:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise SystemExit('Another RunPod Lab controller is already running')
    root = tk.Tk()
    App(root)
    root.mainloop()
    lock.close()


if __name__ == '__main__':
    main()
