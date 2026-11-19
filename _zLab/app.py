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


def options(gpus, cpus):
    """Resolve requested device names against the current provider catalog."""
    choices = []
    for keyword in ('RTX PRO 6000', 'RTX 5090', 'RTX 4090'):
        for gpu in gpus:
            identity = str(gpu.get('id', ''))
            if keyword.lower() in identity.lower() and identity:
                choices.append((f'GPU: {identity}', {'gpu': {'id': identity, 'count': 1}}))
    for cpu in cpus:
        try:
            bounds = cpu['vcpu']
            if bounds['min'] <= 32 <= bounds['max'] and cpu['ramGbPerVcpu'] * 32 >= 128:
                identity = cpu['id']
                choices.append((f'CPU: {identity}, 32 vCPU / {cpu["ramGbPerVcpu"] * 32:g} GB',
                                {'cpu': {'id': identity, 'vcpuCount': 32}}))
        except (KeyError, TypeError):
            continue
    return choices


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
        record = {'name': name, 'status': 'create pending', 'deadline': deadline,
                  'compute_kind': kind, 'compute_id': compute[kind]['id']}
        if kind == 'cpu':
            vcpu = compute[kind].get('vcpuCount')
            if type(vcpu) is not int or vcpu != 32:
                raise ValueError('CPU choice must request 32 vCPUs')
            record.update(requested_vcpu=vcpu, minimum_memory_gb=128)
        records.append(record)
        self.state.write(records)
        try:
            status, result = self.client.create_pod({'name': name, 'image': image, 'disk': 50, 'startSsh': True, 'ports': ['22/tcp'], **compute})
        except RunPodError as exc:
            if not exc.uncertain and exc.status and 400 <= exc.status < 500:
                records.remove(record)
                self.state.write(records)
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


def simulation_gpu(record, pod):
    kind, expected = record.get('compute_kind'), record.get('compute_id')
    if kind == 'gpu':
        actual = pod.get('gpu') or {}
        if actual.get('id') != expected or not isinstance(actual.get('count'), int) or actual['count'] < 1:
            raise ValueError('Pod GPU identity/count does not match the requested catalog GPU')
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
    if not isinstance(record.get('deadline'), (int, float)) or record['deadline'] <= time.time():
        raise ValueError('Pod deadline expired before deployment began')
    if profile not in ('simulation', 'live'):
        raise ValueError('Invalid deployment profile')
    output = []
    (deploy_func or deploy)(host, user, port, known_hosts, identity=identity,
                            mode='cuda' if gpu else 'cpu', timeout=300, profile=profile,
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
        self.busy = False
        self.bridge = None
        self.deploying = False
        self.enumerating = False
        self.devices = {}
        root.protocol('WM_DELETE_WINDOW', self.close)
        root.title('Robust RunPod Lab — provisioning')
        root.geometry('920x680')
        box = ttk.Frame(root, padding=12)
        box.pack(fill='both', expand=True)
        self.key = tk.StringVar()
        self.compute = tk.StringVar()
        self.image = tk.StringVar(value=IMAGE)
        self.deadline_minutes = tk.StringVar(value='60')
        self.known_hosts = tk.StringVar(value=str(Path.home() / '.ssh/known_hosts'))
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
        ttk.Label(box, text='RunPod API key (session only)').grid(row=0, column=0, sticky='w')
        ttk.Entry(box, textvariable=self.key, show='•', width=60).grid(row=0, column=1, sticky='ew')
        ttk.Button(box, text='Load catalog', command=self.catalog).grid(row=0, column=2)
        ttk.Label(box, text='Compute').grid(row=1, column=0, sticky='w')
        self.dropdown = ttk.Combobox(box, textvariable=self.compute, state='readonly')
        self.dropdown.grid(row=1, column=1, sticky='ew')
        ttk.Label(box, text='Container image').grid(row=2, column=0, sticky='w')
        ttk.Entry(box, textvariable=self.image).grid(row=2, column=1, sticky='ew')
        ttk.Label(box, text='Deadline (minutes)').grid(row=3, column=0, sticky='w')
        ttk.Entry(box, textvariable=self.deadline_minutes, width=10).grid(row=3, column=1, sticky='w')
        ttk.Label(box, text='Automatic termination requires this app to remain open with the API key entered. Billing can continue if it is closed.').grid(row=9, column=0, columnspan=3, sticky='w', pady=8)
        ttk.Button(box, text='Create billable pod', command=self.create).grid(row=4, column=0, sticky='w')
        ttk.Button(box, text='Reconcile owned pods', command=self.reconcile).grid(row=4, column=1, sticky='w')
        ttk.Button(box, text='Terminate selected pod', command=self.terminate).grid(row=4, column=2)
        ttk.Button(box, text='Show selected pod details', command=self.details).grid(row=1, column=2)
        self.list = tk.Listbox(box, height=8, exportselection=False)
        self.list.grid(row=5, column=0, columnspan=3, sticky='nsew', pady=8)
        ttk.Label(box, textvariable=self.status, wraplength=760).grid(row=6, column=0, columnspan=3, sticky='w')
        ttk.Button(box, text='Open selected worker', command=self.open_simulation).grid(row=7, column=0, sticky='w')
        ttk.Button(box, text='Forget verified closed record', command=self.forget).grid(row=7, column=1, sticky='w')
        ttk.Button(box, text='Deploy worker to selected pod', command=self.deploy_worker).grid(row=7, column=2, sticky='w')
        ttk.Label(box, text='Deploy the worker before simulation and verify the SSH host key independently. Live/Duplex: local CPU fake-device holdout decoded e and confirmed two consecutive quiet slots. Physical audio and RunPod GPU remain unqualified.', wraplength=760).grid(row=8, column=0, columnspan=3, sticky='w', pady=8)
        ttk.Label(box, text='Verified SSH known_hosts').grid(row=10, column=0, sticky='w')
        ttk.Entry(box, textvariable=self.known_hosts).grid(row=10, column=1, sticky='ew')
        ttk.Label(box, text='SSH identity (optional)').grid(row=11, column=0, sticky='w')
        ttk.Entry(box, textvariable=self.identity).grid(row=11, column=1, sticky='ew')
        ttk.Label(box, text='Local file root (optional)').grid(row=12, column=0, sticky='w')
        ttk.Entry(box, textvariable=self.files).grid(row=12, column=1, sticky='ew')
        ttk.Label(box, text='Selected files, including modem keys, are sent to the pod.', wraplength=760).grid(row=13, column=0, columnspan=3, sticky='w')
        ttk.Button(box, text='Show deployment receipt', command=lambda: messagebox.showinfo('Deployment receipt', self.deployment_receipt.get() or 'No receipt available.')).grid(row=14, column=0, sticky='w')
        ttk.Label(box, text='Worker profile').grid(row=20, column=0, sticky='w')
        ttk.Combobox(box, textvariable=self.profile, values=('simulation', 'live'), state='readonly').grid(row=20, column=1, sticky='ew')
        ttk.Button(box, text='Configure live audio limits', command=self.audio_settings).grid(row=20, column=2)
        box.columnconfigure(1, weight=1)
        box.rowconfigure(5, weight=1)
        self.refresh()
        self.key.trace_add('write', lambda *_: self.root.after_idle(self.enforce_deadlines))
        self.root.after(15000, self.deadline_tick)

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
        self.records = self.state.read()
        self.list.delete(0, 'end')
        for r in self.records:
            deadline = r.get('deadline')
            when = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(deadline)) if isinstance(deadline, (int, float)) else 'not set'
            self.list.insert('end', f"{r.get('name')} | {r.get('id') or 'ID unknown'} | {r.get('status')} | deadline {when}")

    def background(self, fn, done):
        if self.busy:
            return
        key = self.key.get().strip()
        if not key:
            self.status.set('Enter an API key first.')
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
            else:
                done(result)
            self.refresh()
        threading.Thread(target=work, daemon=True).start()

    def catalog(self):
        def done(result):
            self.choices = result
            self.dropdown['values'] = [label for label, _ in result]
            self.compute.set(result[0][0] if result else '')
            self.status.set('Catalog loaded; availability and actual hardware are not yet verified.')
        self.background(lambda c: options(c.list_gpus(), c.list_cpus()), done)

    def create(self):
        selected = next((value for label, value in self.choices if label == self.compute.get()), None)
        image = self.image.get().strip()
        if not selected or not image:
            self.status.set('Choose catalog compute and enter an image.')
            return
        try:
            minutes = int(self.deadline_minutes.get())
            if not 1 <= minutes <= 1440:
                raise ValueError()
        except ValueError:
            self.status.set('Deadline must be 1–1440 minutes.')
            return
        deadline = time.time() + minutes * 60
        if not messagebox.askyesno('Billable RunPod', f'Create this pod with a {minutes}-minute deadline? Automatic termination works only while this app is open with the API key entered. Charges can continue after it closes.'):
            return
        self.background(lambda c: Ownership(self.state, c).create(selected, image, deadline),
                        lambda _: self.status.set('Create submitted. Reconcile for status; charges may continue.'))

    def reconcile(self):
        self.background(lambda c: Ownership(self.state, c).reconcile(),
                        lambda _: self.status.set('Ownership status refreshed; verify unresolved or queued pods in the RunPod console.'))

    def close(self):
        if self.busy or self.deploying or self.enumerating:
            self.status.set('Wait for the current provider request or bounded deployment to finish before closing.')
            return
        if self.bridge:
            self.bridge.close()
        self.root.destroy()

    def forget(self):
        if self.busy:
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
        self.state.write([r for r in records if r.get('name') != record.get('name')])
        self.refresh()
        self.status.set('Local record removed. Provider resources were not changed.')

    def deploy_worker(self):
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
        known_hosts, identity = self.known_hosts.get(), self.identity.get().strip() or None
        profile = self.profile.get()
        secret = self.key.get().strip()
        def launch(pod):
            self.deploying = True
            self.status.set('Deploying and building the native worker; the deadline remains active. Pod charges continue.')
            def work():
                try:
                    receipt = deploy_selected(pod, record, known_hosts, identity, secret, profile=profile)
                    result = 'Worker build finished. GPU execution has not been qualified; pod charges continue.'
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
        self.background(lambda c: c.get_pod(record['id']), launch)

    def audio_settings(self):
        window = tk.Toplevel(self.root)
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
        idx = self.list.curselection()
        if not idx or not self.records[idx[0]].get('id'):
            self.status.set('Select an owned pod with a known ID.')
            return
        if self.bridge:
            self.status.set('A simulation bridge is already open; close and reopen the controller to replace it.')
            return
        record = dict(self.records[idx[0]])
        pod_id = record['id']
        known_hosts, identity, files = self.known_hosts.get(), self.identity.get(), self.files.get()
        profile = self.profile.get()
        settings = (self.audio_binary.get(), self.devices.get(self.audio_device.get(), self.audio_device.get()), self.capture_bytes.get(), self.playback_bytes.get(), self.capture_frames.get(), self.playback_prebuffer.get(), self.local_utc_uncertainty.get(), self.max_clock_offset.get(), self.remote_clock_drift_ppm.get())
        def done(pod):
            try:
                root = file_root(files)
                live = profile == 'live'
                command = ssh_command(pod, known_hosts, identity, simulation_gpu(record, pod), root is not None, live=live)
                if live:
                    command = live_command(command, *settings)
                from browser_bridge import start
                self.bridge = start(command, files=root, live=True) if live else start(command, files=root) if root else start(command)
                webbrowser.open(self.bridge.url)
                self.status.set('Live bridge opened. Local CPU fake-device holdout confirmed completion; physical audio and RunPod GPU remain unqualified.' if live else 'Simulation bridge opened. The remote worker must be deployed and its SSH host key preverified.')
            except Exception as exc:
                self.status.set(str(exc))
        self.background(lambda c: c.get_pod(pod_id), done)

    def details(self):
        idx = self.list.curselection()
        if not idx or not self.records[idx[0]].get('id'):
            self.status.set('Select an owned pod with a known ID.')
            return
        pod_id = self.records[idx[0]]['id']
        def show(pod):
            # Show provider fields as text; never execute a returned command.
            messagebox.showinfo('RunPod details', json.dumps(pod, indent=2))
            self.status.set('Pod details loaded. SSH host identity must be verified separately.')
        self.background(lambda c: c.get_pod(pod_id), show)

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
    import tkinter as tk
    from tkinter import ttk, messagebox
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
