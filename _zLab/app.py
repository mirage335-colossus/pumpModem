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
    def __init__(self, state, client):
        self.state, self.client = state, client

    def create(self, compute, image, deadline=None):
        records = self.state.read()
        # A missing id might be an accepted, queued or timed-out create.
        if any(not r.get('id') for r in records):
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

    def reconcile(self):
        records = self.state.read()
        pods = self.client.list_pods()
        queued = self.client.list_queued()
        for record in records:
            matches = [p for p in pods if p.get('name') == record.get('name')]
            matching_queue = [p for p in queued if p.get('name') == record.get('name')]
            if len(matches) == 1:
                record['id'] = matches[0].get('id') or record.get('id')
                if record.get('id') == record.get('deadline_absent_pod'):
                    record.pop('deadline_absent_pod', None)
                record['status'] = str(matches[0].get('status', matches[0].get('desiredStatus', 'unknown')))
            elif len(matches) > 1:
                record['status'] = 'multiple matches; inspect RunPod console'
            elif len(matching_queue) == 1:
                record['queue_id'] = matching_queue[0].get('id')
                if (record.get('queue_id') == record.get('deadline_absent_queue')
                        and str(matching_queue[0].get('status', '')).upper() == 'QUEUED'):
                    record.pop('deadline_absent_queue', None)
                record['status'] = 'queue status: ' + str(matching_queue[0].get('status', 'unknown')) + '; inspect RunPod console'
            elif len(matching_queue) > 1:
                record['status'] = 'multiple queued matches; inspect RunPod console'
            else:
                record['status'] = 'not in list; may be queued or deleted; inspect RunPod console'
        self.state.write(records)
        return records

    def delete(self, record):
        if record.get('queue_id') and not record.get('id'):
            self.client.cancel_queued(record['queue_id'])
            records = self.state.read()
            for item in records:
                if item.get('name') == record['name']:
                    item['status'] = 'queue cancellation requested; verify in RunPod console'
            self.state.write(records)
            return
        if not record.get('id'):
            raise ValueError('No confirmed pod or queue ID. Inspect RunPod before proceeding.')
        self.client.delete_pod(record['id'])
        # Keep ownership until absence is confirmed; delete may be asynchronous.
        records = self.state.read()
        for item in records:
            if item.get('name') == record['name']:
                item['status'] = 'termination requested; verify in RunPod console'
        self.state.write(records)

    def enforce_deadlines(self, now=None):
        """Reconcile and request termination independently for each expired job."""
        now = time.time() if now is None else now
        records = self.state.read()
        if not any(isinstance(r.get('deadline'), (int, float)) and r['deadline'] <= now for r in records):
            return 0, []
        records = self.reconcile()
        count, errors = 0, []
        for record in records:
            deadline = record.get('deadline')
            if not isinstance(deadline, (int, float)) or deadline > now:
                continue
            pod_id, queue_id = record.get('id'), record.get('queue_id')
            if pod_id and pod_id != record.get('deadline_absent_pod'):
                kind, identity = 'pod', pod_id
            elif queue_id and queue_id != record.get('deadline_absent_queue'):
                kind, identity = 'queue', queue_id
            else:
                errors.append(f"{record.get('name')}: no active provider ID confirmed; inspect RunPod")
                continue
            action = f'{kind}:{identity}'
            if record.get('deadline_action') == action:
                message = f"{record.get('name')}: termination requested; provider status {record.get('status', 'unknown')}; verify in RunPod"
                errors.append(message)
                latest = self.state.read()
                for item in latest:
                    if item.get('name') == record.get('name'):
                        item['status'] = message
                self.state.write(latest)
                continue
            try:
                # Restrict queue cancellation to a queue ID even if an old pod ID remains.
                selected = {'name': record['name'], 'id': identity} if kind == 'pod' else {'name': record['name'], 'queue_id': identity}
                self.delete(selected)
            except RunPodError as exc:
                if exc.status == 404:
                    latest = self.state.read()
                    for item in latest:
                        if item.get('name') == record.get('name'):
                            item['deadline_absent_' + kind] = identity
                            item['status'] = f'{kind} ID not found; verify the RunPod account and queued resources'
                    self.state.write(latest)
                errors.append(f"{record.get('name')}: {kind} termination failed (HTTP {exc.status or 'unknown'}); inspect RunPod")
                continue
            except (OSError, ValueError) as exc:
                errors.append(f"{record.get('name')}: {kind} termination failed ({type(exc).__name__}); inspect RunPod")
                continue
            latest = self.state.read()
            for item in latest:
                if item.get('name') == record.get('name'):
                    item['deadline_action'] = action
            self.state.write(latest)
            count += 1
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
    def __init__(self, root):
        self.root, self.state, self.choices = root, State(), []
        configure_dark_theme(root)
        self.busy = False
        self.create_interrupted = False
        self.preferred_compute = None
        self.bridge = None
        self.closing_bridge = False
        self.deploying = False
        self.enumerating = False
        self.setting_up = False
        self.setup_after = None
        self.devices = {}
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.title('Robust RunPod Lab — provisioning')
        root.geometry('920x680')
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
        self.status = tk.StringVar(value='Enter a RunPod API key and load the catalog. No pod is created automatically.')
        ttk.Label(box, text='RunPod API key (saved)').grid(row=0, column=0, sticky='w')
        key_entry = ttk.Entry(box, textvariable=self.key, show='•', width=60)
        key_entry.grid(row=0, column=1, sticky='ew')
        key_entry.bind('<FocusOut>', lambda _: self.save_key())
        key_actions = ttk.Frame(box)
        key_actions.grid(row=0, column=2)
        ttk.Button(key_actions, text='Load catalog', command=self.catalog).pack(side='left')
        ttk.Button(key_actions, text='Clear key', command=self.clear_key).pack(side='left')
        ttk.Label(box, text='Compute').grid(row=1, column=0, sticky='w')
        self.dropdown = ttk.Combobox(box, textvariable=self.compute, state='readonly')
        self.dropdown.grid(row=1, column=1, columnspan=2, sticky='ew')
        ttk.Label(box, text='Container image').grid(row=2, column=0, sticky='w')
        ttk.Entry(box, textvariable=self.image).grid(row=2, column=1, sticky='ew')
        ttk.Label(box, text='Deadline (minutes)').grid(row=3, column=0, sticky='w')
        placement = ttk.Frame(box)
        placement.grid(row=3, column=1, sticky='ew')
        ttk.Entry(placement, textvariable=self.deadline_minutes, width=10).pack(side='left')
        ttk.Label(placement, text='Cloud tier').pack(side='left', padx=(16, 4))
        ttk.Combobox(placement, textvariable=self.cloud, values=('SECURE', 'COMMUNITY'),
                     width=12, state='readonly').pack(side='left')
        self.cloud.trace_add('write', self.tier_changed)
        ttk.Label(box, text='Automatic termination requires this app to remain open with the API key entered. Billing can continue if it is closed.').grid(row=9, column=0, columnspan=3, sticky='w', pady=8)
        ttk.Button(box, text='Start simulation', command=self.start_simulation).grid(row=4, column=0, sticky='w')
        pod_actions = ttk.Frame(box)
        pod_actions.grid(row=4, column=1, sticky='w')
        ttk.Button(pod_actions, text='Create billable pod', command=self.create).pack(side='left')
        ttk.Button(pod_actions, text='Reconcile owned pods', command=self.reconcile).pack(side='left')
        ttk.Button(box, text='Terminate selected pod', command=self.terminate).grid(row=4, column=2)
        self.list = tk.Listbox(box, height=8, exportselection=False,
                               background='#171b22', foreground='#f0f2f5',
                               selectbackground='#365f91', selectforeground='#f0f2f5',
                               highlightbackground='#697584', highlightcolor='#365f91')
        self.list.grid(row=5, column=0, columnspan=3, sticky='nsew', pady=8)
        self.fingerprint_pod = None
        self.known_hosts.trace_add('write', self.bind_fingerprint)
        self.list.bind('<<ListboxSelect>>', self.clear_stale_fingerprint)
        status_box = ttk.Frame(box)
        status_box.grid(row=6, column=0, columnspan=3, sticky='ew')
        self.status_label = ttk.Label(status_box, textvariable=self.status, wraplength=760)
        self.status_label.pack(anchor='w')
        self.status_label.bind('<Double-Button-1>', self.copy_status)
        ttk.Label(status_box, text='Double-click the status message to copy it.',
                  style='Hint.TLabel').pack(anchor='w')
        ttk.Button(box, text='Open selected worker', command=self.open_simulation).grid(row=7, column=0, sticky='w')
        ttk.Button(box, text='Forget verified closed record', command=self.forget).grid(row=7, column=1, sticky='w')
        ttk.Button(box, text='Deploy worker to selected pod', command=self.deploy_worker).grid(row=7, column=2, sticky='w')
        ttk.Label(box, text='Start simulation creates a pod, configures SSH, deploys the worker and opens the simulation. Select an owned pod to continue using it. The controls below support manual setup and older pods.', wraplength=760).grid(row=8, column=0, columnspan=3, sticky='w', pady=8)
        ttk.Label(box, text='SSH trust (optional)').grid(row=10, column=0, sticky='w')
        ttk.Entry(box, textvariable=self.known_hosts).grid(row=10, column=1, sticky='ew')
        ttk.Label(box, text='SSH identity (optional)').grid(row=11, column=0, sticky='w')
        ttk.Entry(box, textvariable=self.identity).grid(row=11, column=1, sticky='ew')
        ttk.Label(box, text='Local file root (optional)').grid(row=12, column=0, sticky='w')
        ttk.Entry(box, textvariable=self.files).grid(row=12, column=1, sticky='ew')
        ttk.Label(box, text='Selected files, including modem keys, are sent to the pod.', wraplength=760).grid(row=13, column=0, columnspan=3, sticky='w')
        ttk.Button(box, text='Show deployment receipt', command=lambda: messagebox.showinfo('Deployment receipt', self.deployment_receipt.get() or 'No receipt available.')).grid(row=14, column=0, sticky='w')
        ttk.Button(box, text='Close worker session', command=self.close_bridge).grid(row=14, column=1, sticky='w')
        ttk.Button(box, text='Show selected pod details', command=self.details).grid(row=14, column=2)
        ttk.Label(box, text='Worker profile').grid(row=20, column=0, sticky='w')
        ttk.Combobox(box, textvariable=self.profile, values=('simulation', 'live'), state='readonly').grid(row=20, column=1, sticky='ew')
        ttk.Button(box, text='Configure live audio limits', command=self.audio_settings).grid(row=20, column=2)
        box.columnconfigure(1, weight=1)
        box.rowconfigure(5, weight=1)
        self.refresh()
        self.key.trace_add('write', lambda *_: self.root.after_idle(self.enforce_deadlines))
        self.root.after(15000, self.deadline_tick)
        from credentials import ApiKeyStore
        self.key_store = ApiKeyStore(self.state.path.parent)
        try:
            self.key.set(self.key_store.load())
        except (OSError, ValueError):
            self.status.set('Could not read the saved API key. Check ' + str(self.key_store.path))
        if self.key.get():
            self.root.after_idle(self.startup_catalog)

    def startup_catalog(self):
        if self.busy:
            self.root.after(100, self.startup_catalog)
        elif self.key.get().strip():
            self.catalog()

    def save_key(self):
        try:
            self.key_store.save(self.key.get().strip())
            return True
        except (OSError, ValueError):
            self.status.set('Could not save the API key. Check ' + str(self.key_store.path))
            return False

    def clear_key(self):
        if self.setting_up or self.busy or self.deploying:
            self.status.set('Wait for setup or the current request before clearing the API key.')
            return
        self.key.set('')
        if self.save_key():
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

    def deadline_tick(self):
        self.enforce_deadlines()
        self.root.after(15000, self.deadline_tick)

    def enforce_deadlines(self):
        if self.busy or not self.key.get().strip():
            return
        now = time.time()
        if not any(isinstance(r.get('deadline'), (int, float)) and r['deadline'] <= now
                   for r in self.state.read()):
            return
        self.background(lambda c: Ownership(self.state, c).enforce_deadlines(),
                        lambda result: self.status.set(
                            f'Deadline check: {result[0]} requests sent. ' +
                            ('; '.join(result[1]) if result[1] else 'Verify status in RunPod.')))

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
            self.list.insert('end', f"{r.get('name')} | {r.get('id') or 'ID unknown'} | {r.get('status')} | deadline {when}")
            if name is not None and r.get('name') == name:
                self.list.selection_set(index)
                self.list.activate(index)
                self.list.see(index)
        self.clear_stale_fingerprint()

    def background(self, fn, done, failed=None):
        if self.busy:
            return
        key = self.key.get().strip()
        if not key:
            self.status.set('Enter an API key first.')
            return
        if not self.save_key():
            return
        self.busy = True
        self.status.set('Contacting RunPod…')
        def work():
            try:
                result = fn(RunPodClient(key))
                self.root.after(0, lambda: finish(result, None))
            except Exception as exc:
                # Avoid exception text that could include user-entered secrets.
                message = str(exc).replace(key, '[redacted]')
                self.root.after(0, lambda: finish(None, message))
        def finish(result, error):
            self.busy = False
            if error:
                self.status.set(error)
                if failed:
                    failed(error)
            else:
                done(result)
            if self.create_interrupted and not self.busy:
                self.create_interrupted = False
                self.status.set(self.status.get() + ' Pod creation was interrupted. Click Create again to review a fresh quote.')
            self.refresh()
        threading.Thread(target=work, daemon=True).start()

    def tier_changed(self, *_):
        self.preferred_compute = self.selected_compute() or self.preferred_compute
        self.choices = []
        self.compute.set('')
        self.dropdown['values'] = []
        self.status.set('Load the catalog for the selected cloud tier.')

    def selected_compute(self):
        selected = self.compute.get()
        return next((value for label, value in self.choices if label == selected), None)

    def catalog(self):
        cloud = self.cloud.get()
        def done(result):
            if self.cloud.get() != cloud:
                self.tier_changed()
                return
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
                self.status.set('Catalog loaded. Stock and compute price are checked again before creation.')
        self.background(lambda c: load_catalog(c, cloud), done)

    def create(self, after_create=None):
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
        def settings_changed():
            return (self.selected_compute() != selected or self.key.get().strip() != key
                    or self.image.get().strip() != image or self.deadline_minutes.get() != deadline_text)
        def confirm(quote):
            if settings_changed():
                self.status.set('The pod settings changed. Review the selection and click Create again.')
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
            if not messagebox.askyesno('Billable RunPod', prompt):
                self.status.set('Pod creation cancelled.')
                return
            # Tk services events while its modal confirmation is open. An
            # expired pod still needs immediate attention during that time.
            if self.busy:
                self.create_interrupted = True
                self.status.set('Another provider operation is running. Pod creation was interrupted.')
                return
            if settings_changed():
                self.status.set('The pod settings changed. Review the selection and click Create again.')
                return
            deadline = time.time() + minutes * 60
            def created(result):
                self.refresh()
                record = next((r for r in self.records if r.get('id') == result.get('id')
                               and r.get('name') == result.get('name')), None)
                if record and after_create:
                    self.select_record(record)
                    after_create(record)
                else:
                    self.status.set('Create submitted. Reconcile for status; charges may continue.')
            self.background(lambda c: Ownership(self.state, c).create(selected, image, deadline), created)
        self.background(lambda c: creation_quote(c, selected), confirm)

    def reconcile(self):
        self.background(lambda c: Ownership(self.state, c).reconcile(),
                        lambda _: self.status.set('Ownership status refreshed; verify unresolved or queued pods in the RunPod console.'))

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

    def start_simulation(self):
        if self.setting_up or self.busy or self.deploying or self.closing_bridge:
            self.status.set('Wait for the current setup or provider request to finish.')
            return
        if self.bridge:
            webbrowser.open(self.bridge.url)
            return
        try:
            settings = self.session_settings()
        except (OSError, ValueError) as error:
            self.status.set(str(error))
            return
        selected = self.list.curselection()
        if selected:
            record = dict(self.records[selected[0]])
            if not record.get('id'):
                self.status.set('Reconcile the selected pending request before starting its session.')
                return
            self.prepare_worker(record, settings)
        else:
            self.create(after_create=lambda record: self.prepare_worker(record, settings))

    def prepare_worker(self, record, settings):
        deadline = record.get('deadline')
        if type(deadline) not in (int, float) or not math.isfinite(deadline) or deadline <= time.time():
            self.status.set('The pod deadline is missing or expired. Review the owned pod before continuing.')
            return
        if not self.key.get().strip() or not self.save_key():
            self.status.set('Enter an API key to continue setup.')
            return
        self.setting_up = True
        key = self.key.get().strip()
        known_hosts = self.known_hosts.get()
        identity = self.identity.get().strip() or record.get('ssh_identity') or None
        ready_until = min(deadline, time.time() + 600)
        profile, files, audio = settings

        def failed(message):
            self.setting_up = False
            self.status.set('Setup stopped. ' + message + ' The pod remains owned. Use Start simulation to retry or terminate it.')

        def connect(client):
            pod = owned_pod_details(client, record)
            if pod.get('status') in ('ERROR', 'EXITED', 'TERMINATED', 'FAILED', 'STOPPED'):
                raise ValueError('The selected pod is ' + pod['status'] + '.')
            if pod.get('status') != 'RUNNING' or not (pod.get('ssh') or {}).get('direct'):
                return None
            simulation_gpu(record, pod)
            try:
                return owned_pod_connection(client, record, known_hosts, self.state.path.parent)
            except ValueError as error:
                if str(error).startswith(('Could not read a bounded SSH host key', 'Direct SSH is not ready')):
                    return None
                raise

        def check_ready():
            self.setup_after = None
            if self.key.get().strip() != key:
                failed('The API key changed during setup.')
                return
            if time.time() >= ready_until:
                failed('Timed out waiting for the pod and its authenticated SSH endpoint.')
                self.enforce_deadlines()
                return
            if self.busy:
                self.setup_after = self.root.after(250, check_ready)
                return
            self.background(connect, connected, failed)

        def connected(connection):
            if connection is None:
                self.status.set('Waiting for RunPod startup and SSH. The pod deadline remains active.')
                self.setup_after = self.root.after(2000, check_ready)
                return
            pod, pin = connection
            self.deploying = True
            self.status.set('Deploying the native worker. The pod deadline remains active.')
            def work():
                try:
                    receipt = deploy_selected(pod, record, pin, identity, key, profile=profile)
                    error = None
                except Exception as exc:
                    receipt, error = '', str(exc).replace(key, '[redacted]')[:4096]
                self.root.after(0, lambda: deployed(receipt, error, pod, pin))
            threading.Thread(target=work, daemon=True).start()

        def deployed(receipt, error, pod, pin):
            if self.busy:
                self.root.after(100, lambda: deployed(receipt, error, pod, pin))
                return
            self.deploying = False
            if error:
                failed(error)
                return
            if time.time() >= deadline:
                failed('The pod deadline expired during deployment.')
                self.enforce_deadlines()
                return
            try:
                records = self.state.read()
                current = next(r for r in records if r.get('id') == record.get('id') and r.get('name') == record.get('name'))
                current.setdefault('deployments', {})[profile] = receipt
                self.state.write(records)
                self.deployment_receipt.set(receipt)
                command = ssh_command(pod, pin, identity, simulation_gpu(record, pod), files is not None, live=profile == 'live')
                if profile == 'live':
                    command = live_command(command, *audio)
                from browser_bridge import start
                self.bridge = start(command, files=files, live=True) if profile == 'live' else start(command, files=files) if files else start(command)
                webbrowser.open(self.bridge.url)
                self.setting_up = False
                self.status.set('Simulation opened.' if profile == 'simulation' else 'Experimental Live/Duplex session opened.')
            except Exception as exc:
                failed(str(exc).replace(key, '[redacted]')[:4096])

        check_ready()

    def close(self):
        if self.busy or self.deploying or self.enumerating or self.closing_bridge or self.setting_up:
            self.status.set('Wait for the current provider request or bounded deployment to finish before closing.')
            return
        if not self.save_key():
            return
        if self.bridge:
            self.bridge.close()
        self.root.destroy()

    def close_bridge(self):
        """Release the local bridge without affecting pod ownership or billing."""
        if self.busy:
            self.status.set('Wait for the current provider request before closing the worker session.')
            return
        if self.closing_bridge:
            self.status.set('The worker session is already closing.')
            return
        bridge = self.bridge
        if bridge is None:
            self.status.set('No worker session is open. Pod resources were not changed.')
            return
        self.closing_bridge = True
        self.status.set('Closing the worker session. The pod may continue to incur charges.')
        def work():
            try:
                bridge.close()
                error = False
            except Exception:
                error = True
            self.root.after(0, lambda: finish(error))
        def finish(error):
            self.closing_bridge = False
            if error:
                self.status.set('Could not close the worker session. Retry or inspect the connection; pod charges may continue.')
            else:
                if self.bridge is bridge:
                    self.bridge = None
                self.status.set('Worker session closed. The pod remains active; terminate it when finished.')
        threading.Thread(target=work, daemon=True).start()

    def forget(self):
        if self.busy or getattr(self, 'setting_up', False) or getattr(self, 'deploying', False):
            self.status.set('Wait for the current provider request to finish before removing a record.')
            return
        idx = self.list.curselection()
        if not idx:
            self.status.set('Select an owned record first.')
            return
        record = self.records[idx[0]]
        if not messagebox.askyesno('Forget ownership record', 'Have you verified in RunPod that this pod is terminated and no queued request remains? Forgetting a live resource can leave charges running.'):
            return
        # Modal dialogs run Tk's event loop; a deadline request may have begun.
        if self.busy:
            self.status.set('Wait for the current provider request to finish before removing a record.')
            return
        records = self.state.read()
        if record not in records:
            self.refresh()
            self.status.set('The selected record changed. Review it again before removal.')
            return
        records.remove(record)
        pod_id = record.get('id')
        try:
            if pod_id and not any(r.get('id') == pod_id for r in records):
                from host_identity import forget_host_key
                forget_host_key(pod_id, self.state.path.parent)
            from bootstrap import forget_bootstrap
            forget_bootstrap(record, self.state.path.parent)
            self.state.write(records)
        except (OSError, ValueError):
            self.status.set('Could not finish removing the local record and its SSH pin. Check the lab state directory.')
            return
        self.refresh()
        self.status.set('Local record removed. Provider resources were not changed.')

    def deploy_worker(self):
        if getattr(self, 'setting_up', False):
            self.status.set('Automatic setup is already in progress.')
            return
        if self.clear_stale_fingerprint():
            return
        idx = self.list.curselection()
        if not idx or not self.records[idx[0]].get('id'):
            self.status.set('Select an owned pod with a known ID.')
            return
        if self.deploying:
            self.status.set('A deployment is already in progress.')
            return
        record = dict(self.records[idx[0]])
        if not isinstance(record.get('deadline'), (int, float)) or record['deadline'] <= time.time():
            self.status.set('Pod deadline is missing or expired; deployment is unavailable.')
            return
        known_hosts = self.known_hosts.get()
        identity = self.identity.get().strip() or record.get('ssh_identity') or None
        profile = self.profile.get()
        secret = self.key.get().strip()
        def launch(connection):
            pod, verified_hosts = connection
            self.deploying = True
            self.status.set('Deploying and building the native worker; the deadline remains active. Pod charges continue.')
            def work():
                try:
                    receipt = deploy_selected(pod, record, verified_hosts, identity, secret, profile=profile)
                    result = 'Worker build finished. Open the selected worker to start a session. Pod charges continue.'
                except Exception as exc:
                    receipt = ''
                    message = str(exc).replace(secret, '[redacted]') if secret else str(exc)
                    message = ''.join(ch if ch in '\n\t' or 32 <= ord(ch) < 127 else '?' for ch in message[:4096])
                    result = 'Deployment failed or was interrupted: ' + message + '. Pod charges may continue; inspect before retrying.'
                self.root.after(0, lambda: finish(result, receipt))
            def finish(result, receipt):
                self.deploying = False
                self.deployment_receipt.set(receipt)
                self.status.set(result)
            threading.Thread(target=work, daemon=True).start()
        self.background(lambda c: owned_pod_connection(c, record, known_hosts, self.state.path.parent), launch)

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
                self.root.after(0, lambda: finish(found, error))
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

    def open_simulation(self):
        if getattr(self, 'setting_up', False):
            self.status.set('Automatic setup will open the session when the worker is ready.')
            return
        if self.clear_stale_fingerprint():
            return
        if self.closing_bridge:
            self.status.set('Wait for the worker session to finish closing.')
            return
        idx = self.list.curselection()
        if not idx or not self.records[idx[0]].get('id'):
            self.status.set('Select an owned pod with a known ID.')
            return
        if self.bridge:
            self.status.set('A worker session is already open; use Close worker session before opening another.')
            return
        record = dict(self.records[idx[0]])
        known_hosts, identity, files = self.known_hosts.get(), self.identity.get().strip() or record.get('ssh_identity'), self.files.get()
        profile = self.profile.get()
        settings = (self.audio_binary.get(), self.devices.get(self.audio_device.get(), self.audio_device.get()), self.capture_bytes.get(), self.playback_bytes.get(), self.capture_frames.get(), self.playback_prebuffer.get(), self.local_utc_uncertainty.get(), self.max_clock_offset.get(), self.remote_clock_drift_ppm.get())
        def done(connection):
            pod, verified_hosts = connection
            try:
                root = file_root(files)
                live = profile == 'live'
                command = ssh_command(pod, verified_hosts, identity, simulation_gpu(record, pod), root is not None, live=live)
                if live:
                    command = live_command(command, *settings)
                from browser_bridge import start
                self.bridge = start(command, files=root, live=True) if live else start(command, files=root) if root else start(command)
                webbrowser.open(self.bridge.url)
                self.status.set('Experimental Live/Duplex session opened. Remote physical audio remains unqualified.' if live else 'Simulation bridge opened.')
            except Exception as exc:
                self.status.set(str(exc))
        self.background(lambda c: owned_pod_connection(c, record, known_hosts, self.state.path.parent), done)

    def details(self):
        idx = self.list.curselection()
        if not idx or not self.records[idx[0]].get('id'):
            self.status.set('Select an owned pod with a known ID.')
            return
        record = dict(self.records[idx[0]])
        def show(pod):
            # Show provider fields as text; never execute a returned command.
            fields = ('id', 'name', 'status', 'cloud', 'image', 'gpu', 'cpu', 'cost', 'ssh', 'ports', 'createdAt')
            messagebox.showinfo('RunPod details', json.dumps({key: pod[key] for key in fields if key in pod}, indent=2))
            self.status.set('Pod details loaded.')
        self.background(lambda c: owned_pod_details(c, record), show)

    def terminate(self):
        idx = self.list.curselection()
        if not idx:
            self.status.set('Select an owned pod first.')
            return
        record = self.records[idx[0]]
        if messagebox.askyesno('Terminate pod', f"Permanently terminate {record.get('name')}? Pod storage may be lost."):
            self.background(lambda c: Ownership(self.state, c).delete(record),
                            lambda _: self.status.set('Termination requested; verify the result in RunPod.'))


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
