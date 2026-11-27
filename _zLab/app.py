#!/usr/bin/env python3
"""Managed cloud laboratory controller for native modem workloads."""
from dataclasses import dataclass
import hashlib
import json
import math
import os
import re
import webbrowser
from pathlib import Path
import secrets
import select
import shlex
import signal
import stat
import subprocess
import sys
import tempfile
import threading
import time

from runpod import RunPodClient, RunPodError
from linode import LinodeClient

IMAGE = 'runpod/pytorch:1.0.2-cu1281-torch280-ubuntu2404'
LINODE_IMAGE = 'linode/ubuntu24.04'
POD_DEADLINE_LIMIT_MINUTES = 36 * 60
MIN_CPU_VCPUS = 32
MIN_CPU_MEMORY_GB = 128
GPU_MODELS = ('RTX PRO 6000', 'RTX 5090', 'RTX 4090')
RTX_PRO_6000 = 'NVIDIA RTX PRO 6000 Blackwell Server Edition'
RTX_5090 = 'NVIDIA GeForce RTX 5090'
PREFERRED_GPUS = {
    (RTX_PRO_6000, 2): 0,
    (RTX_PRO_6000, 1): 1,
    (RTX_5090, 3): 2,
    (RTX_5090, 2): 3,
    (RTX_5090, 1): 4,
}
REFRESH_ARROW = """
#define refresh_width 16
#define refresh_height 13
static unsigned char refresh_bits[] = {
  0x00, 0x00, 0xe0, 0x07, 0x38, 0xdc, 0x0c, 0xf0, 0x06, 0xf0,
  0x06, 0xfe, 0x06, 0xe0, 0x06, 0x00, 0x06, 0x00, 0x06, 0x30,
  0x0c, 0x18, 0xf8, 0x0f, 0xe0, 0x03
};
"""


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
    style.configure('Refresh.TButton', padding=(5, 3))
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


def compute_refresh_button(parent, command):
    icon = tk.BitmapImage(master=parent, data=REFRESH_ARROW,
                          foreground=ttk.Style(parent).lookup('TButton', 'foreground'))
    button = ttk.Button(parent, name='refresh_compute', text='Refresh compute', image=icon,
                        compound='image', style='Refresh.TButton', command=command)
    button.icon = icon
    hint = tk.Toplevel(button)
    hint.withdraw()
    hint.overrideredirect(True)
    ttk.Label(hint, text=button.cget('text'), padding=(6, 3)).pack()

    def show_hint(_event):
        x, y = button.winfo_rootx(), button.winfo_rooty() + button.winfo_height()
        hint.geometry(f'{x - hint.winfo_reqwidth() + button.winfo_width():+d}{y + 3:+d}')
        hint.deiconify()

    for event in ('<Enter>', '<FocusIn>'):
        button.bind(event, show_hint)
    for event in ('<Leave>', '<FocusOut>', '<ButtonPress>'):
        button.bind(event, lambda _event: hint.withdraw())
    return button


def gpu_count_ceiling(item, cloud):
    limits = item.get('maxCount')
    limit = limits.get(cloud.lower()) if isinstance(limits, dict) else None
    return limit if type(limit) is int and limit >= 0 else None


def valid_cpu_count(count):
    return type(count) is int and count >= MIN_CPU_VCPUS and (count & (count - 1)) == 0


def catalog_offer(item, kind, cloud, count=1):
    """Return the GUI label, create payload, stock and hourly price for one SKU and count."""
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
        if count != 1 and (identity, count) not in PREFERRED_GPUS:
            return None
        maximum = gpu_count_ceiling(item, cloud)
        if item.get(cloud.lower()) is False or (maximum is not None and count > maximum):
            stock = 'NONE'
        compute = {'gpu': {'id': identity, 'count': count}, 'cloud': cloud}
        label = f'GPU: {identity}'
        if count > 1:
            label += f' x{count}'
        per_gpu = prices.get(cloud.lower())
        price = per_gpu * count if type(per_gpu) in (int, float) else None
    else:
        if cloud != 'SECURE' or not valid_cpu_count(count):
            return None
        bounds = item.get('vcpu')
        if not isinstance(bounds, dict):
            return None
        minimum, maximum = bounds.get('min'), bounds.get('max')
        memory_per_vcpu = item.get('ramGbPerVcpu')
        if (type(minimum) is not int or type(maximum) is not int or not 1 <= minimum <= count <= maximum
                or type(memory_per_vcpu) not in (int, float)):
            return None
        try:
            memory = memory_per_vcpu * count
            if not math.isfinite(memory) or memory < MIN_CPU_MEMORY_GB:
                return None
        except OverflowError:
            return None
        compute = {'cpu': {'id': identity, 'vcpuCount': count}, 'cloud': cloud}
        label = f'CPU: {identity}, {count} vCPU / {memory:g} GB'
        per_vcpu = prices.get('securePerVcpu')
        price = per_vcpu * count if type(per_vcpu) in (int, float) else None
    label += f' · stock {stock.lower()}'
    if type(price) in (int, float) and math.isfinite(price) and price > 0:
        label += f' · ${price:g}/hr'
    else:
        label += ' · price unknown'
        price = None
    return label, compute, stock, price


def cpu_catalog_counts(cpus):
    """Find the exact stock queries supported by the flavor ranges and memory."""
    counts = set()
    for item in cpus:
        bounds = item.get('vcpu') if isinstance(item, dict) else None
        maximum = bounds.get('max') if isinstance(bounds, dict) else None
        if type(maximum) is not int or maximum < MIN_CPU_VCPUS:
            continue
        for power in range(MIN_CPU_VCPUS.bit_length() - 1, maximum.bit_length()):
            count = 1 << power
            if catalog_offer(item, 'cpu', 'SECURE', count):
                counts.add(count)
    return sorted(counts, reverse=True)


def options(gpus, cpus, cloud='SECURE', *, multi_gpu=None):
    offers = []
    for count, rows in ((1, gpus), *(multi_gpu or {}).items()):
        for item in rows:
            offer = catalog_offer(item, 'gpu', cloud, count)
            if offer and offer[2] != 'NONE' and (count == 1 or offer[2] != 'UNKNOWN'):
                offers.append(offer)
    offers.extend(offer for count, rows in cpus.items() for item in rows
                  if (offer := catalog_offer(item, 'cpu', cloud, count)) and offer[2] != 'NONE')

    def priority(offer):
        _, compute, stock, price = offer
        if 'cpu' in compute:
            cpu = compute['cpu']
            return (stock == 'UNKNOWN', 0, not cpu['id'].startswith('cpu5'), -cpu['vcpuCount'],
                    price if price is not None else math.inf, cpu['id'])
        gpu = compute['gpu']
        identity = gpu['id']
        preference = PREFERRED_GPUS.get((identity, gpu['count']), len(PREFERRED_GPUS))
        model = identity.upper()
        model_rank = next((i for i, name in enumerate(GPU_MODELS) if name in model), len(GPU_MODELS))
        return stock == 'UNKNOWN', 1, preference, 'MIG' in model.split(), model_rank, 'MAX-Q' in model

    return [(label, compute) for label, compute, _, _ in sorted(offers, key=priority)]


def creation_quote(client, compute):
    """Recheck the exact selected SKU and tier before confirming a paid request."""
    cloud = compute['cloud']
    kind = 'gpu' if 'gpu' in compute else 'cpu'
    count = compute['gpu']['count'] if kind == 'gpu' else compute['cpu']['vcpuCount']
    items = client.list_gpus(cloud, count=count) if kind == 'gpu' else client.list_cpus(count=count)
    matches = [item for item in items if isinstance(item, dict) and item.get('id') == compute[kind]['id']]
    offer = catalog_offer(matches[0], kind, cloud, count) if len(matches) == 1 else None
    if not offer or offer[1] != compute:
        raise ValueError('This compute option is no longer listed for the selected tier. Reload the catalog.')
    label, _, stock, _ = offer
    if stock == 'NONE':
        raise ValueError(f'{label} on {cloud.title()} Cloud. Reload the catalog for another placement.')
    if kind == 'gpu' and count > 1 and stock == 'UNKNOWN':
        raise ValueError(f'RunPod could not confirm stock for {label}. Reload the catalog and try again.')
    return label


def load_catalog(client, cloud='SECURE'):
    """Load each independent catalog so one failure cannot hide the other.

    Do not include exception text in the GUI: even if the built-in client
    sanitizes its errors, a provider or injected transport can expose a key or
    other request data. Map status codes to fixed, actionable messages.
    """
    failures = []

    def fetch_catalog(kind, fetch):
        try:
            return fetch()
        except RunPodError as error:
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
            return []

    gpus = fetch_catalog('GPU', lambda: client.list_gpus(cloud))
    cpus = {}
    if cloud == 'SECURE':
        # The unexpanded catalog describes supported sizes. Availability at
        # one size cannot confirm stock at another.
        for count in cpu_catalog_counts(fetch_catalog('CPU', client.list_cpus)):
            cpus[count] = fetch_catalog(f'CPU {count} vCPU', lambda: client.list_cpus(count=count))
    multi_gpu = {}
    for count in (2, 3):
        # The hardware ceiling can skip impossible queries. Only availability
        # for the exact count establishes whether those GPUs are free.
        if any(isinstance(item, dict) and isinstance(item.get('id'), str)
               and (item['id'], count) in PREFERRED_GPUS
               and item.get(cloud.lower()) is not False
               and ((ceiling := gpu_count_ceiling(item, cloud)) is None or ceiling >= count)
               for item in gpus):
            multi_gpu[count] = fetch_catalog(f'GPU x{count}', lambda: client.list_gpus(cloud, count=count))
    return options(gpus, cpus, cloud, multi_gpu=multi_gpu), failures


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

    @staticmethod
    def _end_file(name, *, linode=False):
        if not isinstance(name, str) or not re.fullmatch(r'[ -~]{1,256}', name):
            raise ValueError('Invalid owned pod name for termination.')
        prefix = '.linode-end-approved-' if linode else '.end-request-'
        return prefix + hashlib.sha256(name.encode('ascii')).hexdigest()

    def _state_directory(self):
        from host_identity import _private_directory
        return _private_directory(self.path.parent.absolute())

    def queue_end(self, name):
        """Record an End click independently of an in-flight ownership write."""
        self._write_end(self._end_file(name), name)

    def queue_linode_end(self, record):
        """Publish approval for the instance, or its unique pending create name."""
        name, identity = record.get('name'), record.get('id')
        if (record.get('provider') != 'linode'
                or not self._valid_linode_identity(name, identity)):
            raise ValueError('Invalid Linode identity for termination approval.')
        self._write_end(self._end_file(name, linode=True), name + '\n' + (identity or ''))

    @staticmethod
    def _valid_linode_identity(name, identity):
        if identity is None:
            # Only this controller's unguessable, single-use create labels can
            # represent an approval before the provider assigns an instance ID.
            return isinstance(name, str) and re.fullmatch(r'pump-lab-[a-f0-9]{24}', name)
        return isinstance(identity, str) and re.fullmatch(r'[1-9][0-9]{0,31}', identity)

    def _write_end(self, filename, content):
        self.path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        directory = self._state_directory()
        temporary = '.end-writing-' + secrets.token_hex(16)
        try:
            descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                                 0o600, dir_fd=directory)
            try:
                with os.fdopen(descriptor, 'w', encoding='ascii') as handle:
                    handle.write(content)
                    handle.flush()
                    os.fsync(handle.fileno())
                os.replace(temporary, filename, src_dir_fd=directory, dst_dir_fd=directory)
                os.fsync(directory)
            finally:
                try:
                    os.unlink(temporary, dir_fd=directory)
                except FileNotFoundError:
                    pass
        finally:
            os.close(directory)

    def queued_ends(self):
        """Read complete, private termination markers published by this app."""
        return set(self._read_ends(linode=False)).union(self.queued_linode_ends())

    def queued_linode_ends(self):
        return self._read_ends(linode=True)

    def _read_ends(self, *, linode):
        pattern = r'\.linode-end-approved-[a-f0-9]{64}' if linode else r'\.end-request-[a-f0-9]{64}'
        maximum = 289 if linode else 256
        try:
            directory = self._state_directory()
        except FileNotFoundError:
            return {}
        try:
            requests = {}
            for filename in os.listdir(directory):
                if not re.fullmatch(pattern, filename):
                    continue
                try:
                    descriptor = os.open(filename, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK,
                                         dir_fd=directory)
                except FileNotFoundError:
                    continue
                try:
                    info = os.fstat(descriptor)
                    if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
                            or info.st_mode & 0o077 or not 0 < info.st_size <= maximum):
                        raise ValueError('Invalid saved termination request.')
                    content = os.read(descriptor, maximum + 1).decode('ascii')
                    name, separator, identity = content.partition('\n')
                    if (self._end_file(name, linode=linode) != filename
                            or (linode and (not separator or not self._valid_linode_identity(name, identity or None)))
                            or (not linode and separator)):
                        raise ValueError('Invalid saved termination request.')
                    requests[name] = identity or None
                finally:
                    os.close(descriptor)
            return requests
        finally:
            os.close(directory)

    def consume_end(self, name, *, linode=False):
        """Remove a marker only after publishing its effect to ownership state."""
        filename = self._end_file(name, linode=linode)
        try:
            directory = self._state_directory()
        except FileNotFoundError:
            return
        try:
            try:
                os.unlink(filename, dir_fd=directory)
            except FileNotFoundError:
                return
            os.fsync(directory)
        finally:
            os.close(directory)


class Ownership:
    TERMINATING = 'Terminating'
    TERMINATED = 'Terminated'
    TERMINATION_FAILED = 'Termination failed'
    NOT_VISIBLE = 'No longer visible'
    _QUEUE_CLOSED = ('CANCELLED', 'FAILED', 'EXPIRED')
    _TERMINATION_WAIT = 120

    def __init__(self, state, client):
        self.state, self.client = state, client

    @staticmethod
    def new_pod_name():
        return 'pump-lab-' + secrets.token_hex(12)

    @staticmethod
    def _create_fields(compute, name, image):
        """Bind the selected allocation to both the ownership record and API request."""
        provider = compute.get('provider', 'runpod')
        kind = 'gpu' if 'gpu' in compute else 'cpu' if 'cpu' in compute else None
        choice = compute.get(kind)
        if not kind or not isinstance(choice, dict) or not isinstance(choice.get('id'), str) or not choice['id']:
            raise ValueError('Select a valid catalog compute type')
        fields = {'compute_kind': kind, 'compute_id': choice['id']}
        if provider == 'linode':
            region, memory, vcpus = compute.get('region'), compute.get('memory_gib'), choice.get('vcpuCount')
            if (kind != 'cpu' or not isinstance(region, str) or not region
                    or type(vcpus) is not int or vcpus <= 0
                    or type(memory) not in (int, float) or not math.isfinite(memory) or memory <= 0):
                raise ValueError('Select a Linode CPU plan with a region and memory allocation')
            fields.update(provider=provider, region=region, requested_vcpu=vcpus, requested_memory_gib=memory)
            return fields, {'label': name, 'type': choice['id'], 'region': region, 'image': image, 'booted': True}
        if provider != 'runpod':
            raise ValueError('Unrecognized compute provider')
        cloud = compute.get('cloud', 'SECURE')
        if cloud not in ('SECURE', 'COMMUNITY') or (kind == 'cpu' and cloud != 'SECURE'):
            raise ValueError('Invalid cloud tier for the selected compute')
        fields['cloud'] = cloud
        if kind == 'cpu':
            vcpu = choice.get('vcpuCount')
            if not valid_cpu_count(vcpu):
                raise ValueError(f'CPU choice must request a power of two and at least {MIN_CPU_VCPUS} vCPUs')
            fields.update(requested_vcpu=vcpu, minimum_memory_gb=MIN_CPU_MEMORY_GB)
        else:
            count = choice.get('count', 1)
            if type(count) is not int or count < 1:
                raise ValueError('GPU choice must request a positive GPU count')
            fields['requested_gpu_count'] = count
            choice = dict(choice, count=count)
        return fields, {'name': name, 'image': image, 'disk': 50, 'startSsh': True,
                        'ports': ['22/tcp'], kind: choice, 'cloud': cloud}

    def create(self, compute, image, deadline=None, *, name=None):
        if name is None:
            name = self.new_pod_name()
        if not isinstance(name, str) or not re.fullmatch(r'pump-lab-[a-f0-9]{24}', name):
            raise ValueError('Invalid owned pod name for creation.')
        if name in self.state.queued_ends():
            return None
        records = self.state.read()
        # A missing id might be an accepted, queued or timed-out create.
        if any(not r.get('id') and r.get('status') != self.TERMINATED for r in records):
            raise ValueError('An earlier create is unresolved. Reconcile it before creating another pod.')
        if any(r.get('name') == name for r in records):
            raise ValueError('A saved request already uses this pod name.')
        if type(deadline) not in (int, float) or not math.isfinite(deadline) or deadline <= time.time():
            raise ValueError('A future job deadline is required')
        fields, payload = self._create_fields(compute, name, image)
        record = {'name': name, 'status': 'create pending', 'deadline': deadline, **fields}
        provider = record.get('provider', 'runpod')
        from bootstrap import prepare_bootstrap, forget_bootstrap
        bootstrap_record, bootstrap_payload = prepare_bootstrap(self.state.path.parent, name, image, provider=provider)
        record.update(bootstrap_record)
        records.append(record)
        try:
            self.state.write(records)
        except (OSError, ValueError):
            forget_bootstrap(record, self.state.path.parent)
            raise
        if name in self.state.queued_ends():
            records.remove(record)
            self.state.write(records)
            forget_bootstrap(record, self.state.path.parent)
            return None
        try:
            status, result = self.client.create_pod({**payload, **bootstrap_payload})
        except RunPodError as exc:
            if not exc.uncertain and exc.status and 400 <= exc.status < 500:
                records.remove(record)
                self.state.write(records)
                forget_bootstrap(record, self.state.path.parent)
            raise
        if provider == 'runpod' and (status == 202 or 'podId' in result or str(result.get('status', '')).upper() == 'QUEUED'):
            record['queue_id'] = result.get('id')
            record['status'] = 'queued; inspect and cancel in RunPod console'
        elif (status == 201 and isinstance(result.get('id'), str) and result['id']
              and result.get('name') == name and result.get('provider', 'runpod') == provider):
            record['id'] = result['id']
            record['status'] = str(result.get('status', 'created'))
        else:
            record['status'] = 'create response requires reconciliation'
        self.state.write(records)
        return result

    @staticmethod
    def _find_owned(items, identity, name, provider='runpod'):
        matches = [item for item in items if item.get('name') == name
                   or (identity and item.get('id') == identity)]
        if not matches:
            return None
        if (len(matches) != 1 or matches[0].get('name') != name
                or not isinstance(matches[0].get('id'), str) or not matches[0]['id']
                or (identity and matches[0]['id'] != identity)
                or matches[0].get('provider', 'runpod') != provider):
            raise ValueError('Provider inventory does not match the owned record.')
        return matches[0]

    def _identify(self, record, pods, queues):
        name = record.get('name')
        if not isinstance(name, str) or not name:
            raise ValueError('The ownership record has no pod name.')
        pod = self._find_owned(pods, record.get('id'), name, record.get('provider', 'runpod'))
        queue = self._find_owned(queues, record.get('queue_id'), name)
        if pod:
            if not record.get('id') and self.linode_approved(record):
                record['linode_termination_approval']['id'] = pod['id']
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
                    detail = self._get_owned(self.client.get_pod, record['id'], record['name'],
                                             record.get('provider', 'runpod'))
                    # An absent ID might belong to a different authenticated account.
                    record['status'] = str(detail.get('status', 'unknown')) if detail else self.NOT_VISIBLE
                else:
                    record['status'] = 'Waiting to confirm pod creation'
            except ValueError:
                if not self._has_termination_intent(record):
                    record['status'] = 'Provider identity mismatch'
        self.state.write(records)
        return records

    def _reconcile_failed_pod(self, record, records):
        try:
            if self._get_owned(self.client.get_pod, record['id'], record['name'], record.get('provider', 'runpod')):
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

    @staticmethod
    def linode_approved(record):
        """An approval belongs to exactly one managed Linode or pending creation."""
        name, identity = record.get('name'), record.get('id')
        return (record.get('provider') == 'linode' and bool(name)
                and bool(State._valid_linode_identity(name, identity))
                and record.get('linode_termination_approval') == {'name': name, 'id': identity})

    @classmethod
    def linode_needs_confirmation(cls, record, now):
        if (record.get('provider') != 'linode' or cls.linode_approved(record)
                or record.get('status') in (cls.TERMINATED, cls.NOT_VISIBLE)):
            return False
        if (record.get('status') == cls.TERMINATING and record.get('id')
                and cls._pod_termination_confirmed(record)):
            # This request was already sent. Observe it without sending another.
            return False
        deadline = record.get('deadline')
        return (cls._has_termination_intent(record)
                or (type(deadline) in (int, float) and math.isfinite(deadline) and deadline <= now))

    @classmethod
    def needs_poll(cls, record, now):
        if record.get('status') in (cls.TERMINATED, cls.TERMINATION_FAILED, cls.NOT_VISIBLE):
            return False
        if record.get('provider') == 'linode' and not cls.linode_approved(record):
            return bool(record.get('id') and cls._has_termination_intent(record)
                        and cls._pod_termination_confirmed(record))
        deadline = record.get('deadline')
        return (cls._has_termination_intent(record)
                or (type(deadline) in (int, float) and math.isfinite(deadline) and deadline <= now))

    @classmethod
    def _begin_termination(cls, record, now):
        record['status'] = cls.TERMINATING
        record['termination_started'] = now
        record.pop('termination_error', None)

    def apply_queued_ends(self, provider):
        """Transfer complete UI requests to the serialized ownership state."""
        if provider == 'linode':
            requests = self.state.queued_linode_ends()
        elif provider == 'runpod':
            requests = self.state._read_ends(linode=False)
        else:
            raise ValueError('Unknown termination provider.')
        for name, identity in requests.items():
            matches = [record for record in self.state.read() if record.get('name') == name]
            if len(matches) > 1 or (matches and matches[0].get('provider', 'runpod') != provider):
                raise ValueError('The selected instance changed. Review the instance list.')
            if matches and matches[0].get('status') != self.TERMINATED:
                if provider == 'linode' and identity is not None and matches[0].get('id') != identity:
                    raise ValueError('The instance ID changed after termination was approved.')
                self.request_termination(matches[0])
            # No row after a serialized create means no pending submission.
            self.state.consume_end(name, linode=(provider == 'linode'))
            if provider == 'linode':
                self.state.consume_end(name)  # Retire any superseded legacy marker.

    def request_termination(self, record):
        """Persist the intent before any provider work, using the selected identity."""
        records = self.state.read()
        matches = [item for item in records if item.get('name') == record.get('name')]
        if (not record.get('name') or len(matches) != 1 or
                any(matches[0].get(key) != record.get(key) for key in ('id', 'queue_id')) or
                matches[0].get('provider', 'runpod') != record.get('provider', 'runpod')):
            raise ValueError('The selected pod changed. Review the pod list and try again.')
        current = matches[0]
        approved_now = False
        if current.get('provider') == 'linode' and not self.linode_approved(current):
            pending = self.state.queued_linode_ends()
            name = current['name']
            if name not in pending or (pending[name] is not None and pending[name] != current.get('id')):
                raise ValueError('Confirm termination of this Linode first.')
            current['linode_termination_approval'] = {'name': name, 'id': current.get('id')}
            approved_now = True
        if (current.get('status') != self.TERMINATED
                and (approved_now or current.get('status') != self.TERMINATING)):
            if current.get('provider') == 'linode' and current.get('status') == self.TERMINATION_FAILED:
                # An explicit retry after the observation timeout starts a new
                # attempt. Until then, even an older receipt is observation-only.
                current.pop('termination_sent', None)
                current.pop('deadline_action', None)
            self._begin_termination(current, time.time())
            self.state.write(records)
        return current

    def delete(self, record):
        """Set the termination intent and advance it once."""
        self.request_termination(record)
        return self.enforce_deadlines()

    def keep_linode(self, record, *, deadline=None):
        """Keep a Linode and optionally set a fresh reminder for its next session."""
        if deadline is not None and (type(deadline) not in (int, float)
                                     or not math.isfinite(deadline) or deadline <= time.time()):
            raise ValueError('A future Linode deadline is required.')
        records = self.state.read()
        name = record.get('name')
        matches = [item for item in records if item.get('name') == name]
        if (not name or len(matches) != 1 or record.get('provider') != 'linode'
                or matches[0].get('provider') != 'linode' or matches[0].get('id') != record.get('id')):
            raise ValueError('The selected Linode changed. Review the instance list.')
        current = matches[0]
        if (current.get('status') == self.TERMINATED or self.linode_approved(current)
                or name in self.state.queued_linode_ends()
                or (current.get('id') and self._pod_termination_confirmed(current))):
            raise ValueError('Termination of this Linode is already underway.')
        if self._has_termination_intent(current):
            current['status'] = 'Kept'
            current.pop('termination_started', None)
            current.pop('termination_error', None)
            current.pop('deadline_action', None)
        if deadline is not None:
            current['deadline'] = deadline
        self.state.write(records)
        self.state.consume_end(name)
        return current

    @staticmethod
    def _get_owned(get, identity, name, provider='runpod'):
        try:
            detail = get(identity)
        except RunPodError as exc:
            if exc.status == 404:
                return None
            raise
        if (not isinstance(detail, dict) or detail.get('id') != identity or detail.get('name') != name
                or detail.get('provider', 'runpod') != provider):
            raise ValueError('Provider details do not match the owned resource.')
        return detail

    @classmethod
    def _submit_termination(cls, record, field, delete):
        identity = record[field]
        if record.get('provider') == 'linode':
            if field == 'id' and cls._pod_termination_confirmed(record):
                return 0
            if field != 'id' or not cls.linode_approved(record):
                raise ValueError('Confirm termination of this Linode first.')
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

    def _advance_termination(self, record, records):
        name = record['name']
        if not (record.get('id') or record.get('queue_id')):
            self._identify(record, self.client.list_pods(), self.client.list_queued())
            if not (record.get('id') or record.get('queue_id')):
                return 0
            if self.linode_approved(record):
                # Persist the first verified ID before sending DELETE so an
                # interrupted pending-name operation cannot target a replacement.
                self.state.write(records)

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
        pod = self._get_owned(self.client.get_pod, pod_id, name, record.get('provider', 'runpod')) if pod_id else None
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
            identity = record.get('ssh_identity')
            if identity and not any(item is not record and item.get('ssh_identity') == identity
                                    for item in records):
                forget_bootstrap(record, self.state.path.parent)
            record.pop('ssh_identity', None)
        except (OSError, ValueError):
            return 'Pod terminated. Could not remove its local SSH files.'
        return None

    def prune_terminated(self, keep_name=None):
        """Remove confirmed, closed jobs after retiring their managed SSH files."""
        records = self.state.read()
        errors = []
        removed = False
        for record in list(records):
            if (record.get('status') != self.TERMINATED
                    or (keep_name is not None and record.get('name') == keep_name)):
                continue
            error = self._retire_ssh(record, records)
            if error:
                errors.append(error)
                continue
            records.remove(record)
            removed = True
        if removed:
            self.state.write(records)
        return errors

    def forget_absent(self, record):
        """Retire a user-verified, invisible local job after fresh provider checks."""
        records = self.state.read()
        matches = [r for r in records if r.get('name') == record.get('name')]
        if (len(matches) != 1 or matches[0].get('status') != self.NOT_VISIBLE
                or matches[0].get('queue_id') or not matches[0].get('id')
                or matches[0].get('id') != record.get('id')):
            raise ValueError('The selected old pod record changed.')
        current = matches[0]
        name, pod_id = current['name'], current['id']
        provider = current.get('provider', 'runpod')
        if (self._get_owned(self.client.get_pod, pod_id, name, provider)
                or self._find_owned(self.client.list_pods(), pod_id, name, provider)
                or self._find_owned(self.client.list_queued(), None, name)):
            raise ValueError('The provider still lists this request.')
        error = self._retire_ssh(current, records)
        if error:
            raise ValueError('Could not remove the old pod’s managed SSH files.')
        records.remove(current)
        self.state.write(records)

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
            provider_label = 'Linode' if record.get('provider') == 'linode' else 'RunPod'
            try:
                count += self._advance_termination(record, records)
            except (OSError, ValueError, RunPodError) as exc:
                status = exc.status if isinstance(exc, RunPodError) else None
                if type(status) is int:
                    error = f'{provider_label} HTTP {status}.'
                elif isinstance(exc, ValueError):
                    error = str(exc)
                else:
                    error = f'{provider_label} could not confirm the request.'
                if isinstance(exc, RunPodError) and (exc.uncertain or status in (None, 429)
                                                    or (type(status) is int and status >= 500)):
                    # Retry temporary failures after rechecking ownership.
                    record['termination_error'] = error
                    error = None
            if record['status'] == self.TERMINATED:
                error = self._retire_ssh(record, records)
            elif record['status'] == self.TERMINATING and now - started >= self._TERMINATION_WAIT:
                error = error or f'{provider_label} did not confirm termination. Check the account and try again.'
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
    if (not isinstance(pod, dict) or pod.get('id') != pod_id or pod.get('name') != name
            or pod.get('provider', 'runpod') != record.get('provider', 'runpod')):
        raise ValueError('Provider details do not match the selected ownership record')
    return pod


def owned_pod_connection(client, record, known_hosts, state_dir):
    """Resolve the SSH trust file for this selection before opening a connection."""
    pod = owned_pod_details(client, record)
    if record.get('provider') == 'linode':
        simulation_gpu(record, pod)
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
            if record.get('provider') == 'linode':
                raise ValueError('The saved Linode instance is missing its managed SSH host fingerprint.')
            try:
                fingerprint = client.get_pod_ssh_fingerprint(pod_id)
            except RunPodError as error:
                raise ValueError(f'{error}. This older pod needs its existing SSH fingerprint. '
                                 'New pods created by this version configure SSH automatically.') from None
        path = verify_host_key(pod_id, host, port, fingerprint, state_dir)
    return pod, str(path)


def simulation_gpu(record, pod):
    provider = record.get('provider', 'runpod')
    if pod.get('provider', 'runpod') != provider or provider not in ('linode', 'runpod'):
        raise ValueError('The reported compute provider does not match the selected instance.')
    if provider == 'linode':
        cpu = pod.get('cpu')
        if (record.get('compute_kind') != 'cpu' or not isinstance(cpu, dict) or pod.get('gpu')
                or not record.get('region') or pod.get('region') != record['region']
                or not record.get('compute_id') or cpu.get('id') != record['compute_id']
                or type(cpu.get('vcpuCount')) is not int or cpu['vcpuCount'] <= 0
                or cpu['vcpuCount'] != record.get('requested_vcpu')
                or type(cpu.get('memory')) not in (int, float) or not math.isfinite(cpu['memory'])
                or cpu['memory'] <= 0 or cpu['memory'] != record.get('requested_memory_gib')):
            raise ValueError('Linode did not confirm the selected region, CPU plan, vCPU count and memory.')
        return False
    # Older ownership records have no requested tier. Every new record
    # records it, and GET /pods/{id} reports it for CPU and GPU pods.
    if 'cloud' in record and (record['cloud'] not in ('SECURE', 'COMMUNITY')
                              or pod.get('cloud') != record['cloud']):
        raise ValueError('RunPod did not confirm the requested cloud tier. Check the selected pod details.')
    kind, expected = record.get('compute_kind'), record.get('compute_id')
    if kind == 'gpu':
        actual, count = pod.get('gpu'), record.get('requested_gpu_count', 1)
        if (not isinstance(actual, dict) or actual.get('id') != expected or
                type(count) is not int or count < 1 or
                type(actual.get('count')) is not int or actual['count'] != count):
            raise ValueError('RunPod did not report the requested GPU type and count')
        return True
    if kind == 'cpu':
        actual = pod.get('cpu') or {}
        if actual.get('id') != expected or pod.get('gpu'):
            raise ValueError('Pod CPU identity does not match the requested CPU type')
        vcpu, memory = actual.get('vcpuCount'), actual.get('memory')
        if (type(vcpu) is not int or vcpu != record.get('requested_vcpu') or
                type(memory) not in (int, float) or not math.isfinite(memory) or
                memory < record.get('minimum_memory_gb', MIN_CPU_MEMORY_GB)):
            raise ValueError(f'RunPod did not report the requested CPU count and at least {MIN_CPU_MEMORY_GB} GB host memory')
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


def ssh_endpoint(pod, known_hosts, identity=None):
    from deploy import _connection
    direct = (pod.get('ssh') or {}).get('direct') or {}
    port = direct.get('port')
    host, user, options = _connection(direct.get('host'), direct.get('username'),
                                      port, known_hosts, identity or None)
    return ['ssh', '-F', '/dev/null', '-p', str(port), '-l', user, *options, host]


def ssh_command(pod, known_hosts, identity=None, require_gpu=False, file_channel=False, live=False,
                prefer_gpu=False):
    flag = ((' --require-gpu' if require_gpu else '') +
            (' --prefer-gpu' if prefer_gpu and not require_gpu else '') +
            (' --file-channel' if file_channel or live else '') +
            (' --live' if live else ''))
    worker = 'datapump-lab-worker' if live else 'datapump-worker'
    remote_dir = '/workspace/pump-lab-live' if live else '/workspace/pump-lab'
    endpoint = ssh_endpoint(pod, known_hosts, identity)
    return [endpoint[0], '-T', *endpoint[1:],
            'python3 ' + remote_dir + '/_zLab/worker_relay.py' + flag + ' ' + remote_dir + '/build/lab/' + worker]


def deploy_selected(pod, record, known_hosts, identity, secret='', deploy_func=None, profile='simulation',
                    *, on_progress=None, cancelled=None):
    """Deploy an admitted selection and retain only bounded, redacted receipts."""
    from deploy import deploy, _connection
    simulation_gpu(record, pod)
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
    progress = {} if on_progress is None else {'on_progress': on_progress}
    if cancelled is not None:
        progress['cancelled'] = cancelled
    mode = 'native-cpu' if record.get('provider') == 'linode' else 'gpu'
    (deploy_func or deploy)(host, user, port, known_hosts, identity=identity,
                            mode=mode, timeout=min(remaining, 900), profile=profile,
                            on_output=output.append, **progress)
    lines = []
    for line in '\n'.join(output).splitlines():
        if line.startswith(('PUMP_LAB_PROFILE=', 'PUMP_LAB_MODE=', 'PUMP_LAB_DEVICE=', 'PUMP_LAB_TORCH=',
                            'PUMP_LAB_CUDA=', 'PUMP_LAB_WORKER_SHA256=', 'PUMP_LAB_PROBE_SHA256=', 'PUMP_LAB_CPU_MODEL=',
                            'PUMP_LAB_CPU_AFFINITY_LOGICAL=', 'PUMP_LAB_CGROUP_MEMORY_LIMIT_BYTES=',
                            'PUMP_LAB_CGROUP_CPU_QUOTA_CORES=', 'PUMP_LAB_CPU_MODEL_SCOPE=',
                            'PUMP_LAB_CGROUP_SCOPE=', 'PUMP_LAB_KERNEL_MEMORY_TOTAL_BYTES=',
                            'PUMP_LAB_RECEIPT_SCOPE=', 'PUMP_LAB_GPU_SCOPE=',
                            'PUMP_LAB_GPU_VISIBLE_MEMORY_BYTES=', 'PUMP_LAB_GPU_VISIBLE_UUID=',
                            'PUMP_LAB_GPU_PARTITION_STATUS=', 'PUMP_LAB_GPU_INVENTORY=',
                            'PUMP_LAB_BUILD_JOBS=', 'PUMP_LAB_BUILD_GENERATOR=', 'PUMP_LAB_MEASURE=',
                            'PUMP_LAB_CXX_COMPILER=', 'PUMP_LAB_CXX_COMPILER_ID=',
                            'PUMP_LAB_CXX_COMPILER_VERSION=', 'PUMP_LAB_COMPILER_CACHE=')):
            clean = ''.join(ch if 32 <= ord(ch) < 127 else '?' for ch in line[:250])
            if secret:
                clean = clean.replace(secret, '[redacted]')
            if clean not in lines:
                lines.append(clean)
    return '\n'.join(lines[:28]) or 'Build succeeded; structured device and binary receipt was unavailable.'


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


@dataclass
class LabAccount:
    name: str
    state: State
    key: object
    store: object
    entry: object = None
    saved_key: str | None = None

    def client(self, key):
        return (LinodeClient if self.name == 'Linode' else RunPodClient)(key)


def job_identity(record):
    return record.get('provider', 'runpod'), record.get('name')


def redact_keys(message, keys):
    for key in keys:
        message = message.replace(key, '[redacted]')
    return message


class App:
    _CLOSE_GRACE = 6

    def __init__(self, root, *, repository_directory=None):
        from credentials import LinodeRepositoryApiKeyStore, RepositoryApiKeyStore
        self.root, self.choices = root, []
        runpod_state = State()
        linode_state = State(runpod_state.path.parents[1] / 'linode-lab' / 'jobs.json')
        self.accounts = {
            'linode': LabAccount('Linode', linode_state, tk.StringVar(root),
                                 LinodeRepositoryApiKeyStore(repository_directory)),
            'runpod': LabAccount('RunPod', runpod_state, tk.StringVar(root),
                                 RepositoryApiKeyStore(runpod_state.path.parent,
                                                       repository_directory=repository_directory)),
        }
        configure_dark_theme(root)
        self.busy = False
        self.create_interrupted = False
        self.preferred_compute = None
        self.catalog_after = None
        self.bridge = None
        self.shell_command = ''
        self.bridge_close_failed = False
        self.browser_attached = False
        self.session_record = None
        self.session_is_new = False
        self.creating_name = None
        self.creating_provider = None
        self.ending_names = self.queued_ends()
        self.confirming_linode_end = False
        self.kept_linode_deadlines = set()
        self.end_generation = 0
        self.end_notice = ''
        self.setup_cancelled = None
        self.setup_started = None
        self.setup_stage = ''
        self.exit_after = None
        self.close_after = None
        self.close_attempted = set()
        self.close_linode_ends = set()
        self.close_generation = 0
        self.quoting_create = False
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
        root.title('Robust zLab · Linode and RunPod')
        root.geometry('920x420')
        root.minsize(640, 320)
        box = ttk.Frame(root, padding=12)
        box.pack(fill='both', expand=True)
        self.compute = tk.StringVar()
        self.cloud = tk.StringVar(value='SECURE')
        self.image = tk.StringVar(value=IMAGE)
        self.deadline_minutes = tk.StringVar(value=str(POD_DEADLINE_LIMIT_MINUTES))
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
        self.status = tk.StringVar(value='Enter a Linode token or RunPod API key to load available compute.')
        for row, account in enumerate(self.accounts.values()):
            label = 'Linode API token' if account.name == 'Linode' else 'RunPod API key'
            ttk.Label(box, text=label).grid(row=row, column=0, sticky='w', padx=(0, 12))
            account.entry = ttk.Entry(box, textvariable=account.key, show='•')
            account.entry.grid(row=row, column=1, sticky='ew')
            account.entry.bind('<FocusOut>', self.key_edited)
            account.entry.bind('<Return>', self.key_edited)
        ttk.Label(box, text='Compute').grid(row=2, column=0, sticky='w', padx=(0, 12), pady=(8, 0))
        compute_box = ttk.Frame(box)
        compute_box.grid(row=2, column=1, sticky='ew', pady=(8, 0))
        self.refresh_compute = compute_refresh_button(compute_box, self.refresh_catalog)
        self.refresh_compute.pack(side='right', padx=(4, 0))
        self.dropdown = ttk.Combobox(compute_box, textvariable=self.compute, state='readonly')
        self.dropdown.pack(side='left', fill='x', expand=True)
        self.dropdown.bind('<<ComboboxSelected>>', self.compute_selected)
        actions = ttk.Frame(box)
        actions.grid(row=3, column=0, columnspan=2, sticky='ew', pady=12)
        ttk.Button(actions, text='Start simulation', command=self.start_simulation).pack(side='left')
        ttk.Button(actions, text='End simulation', command=self.end_simulation).pack(side='left', padx=8)
        self.restart_button = ttk.Button(actions, text='Restart simulation', command=self.restart_simulation)
        ttk.Button(actions, text='Advanced', command=self.toggle_advanced).pack(side='right')
        ttk.Label(box, text='Managed instances').grid(row=4, column=0, sticky='w')
        self.list = tk.Listbox(box, height=6, exportselection=False,
                               background='#171b22', foreground='#f0f2f5',
                               selectbackground='#365f91', selectforeground='#f0f2f5',
                               highlightbackground='#697584', highlightcolor='#365f91')
        self.list.grid(row=5, column=0, columnspan=2, sticky='nsew', pady=(4, 8))
        self.fingerprint_pod = None
        self.known_hosts.trace_add('write', self.bind_fingerprint)
        self.list.bind('<<ListboxSelect>>', self.clear_stale_fingerprint)
        self.list.bind('<<ListboxSelect>>', self.update_restart_button, add='+')
        status_box = ttk.Frame(box)
        status_box.grid(row=6, column=0, columnspan=2, sticky='ew')
        self.status_label = ttk.Label(status_box, textvariable=self.status, wraplength=760)
        self.status_label.pack(anchor='w')
        self.status_label.bind('<Double-Button-1>', self.copy_status)
        self.advanced = ttk.LabelFrame(box, text='Advanced', padding=10)
        panel = self.advanced
        panel.grid(row=7, column=0, columnspan=2, sticky='ew', pady=(10, 0))
        panel.columnconfigure(1, weight=1)
        for row, (label, field) in enumerate((
            ('RunPod container image', self.image), ('Deadline (minutes)', self.deadline_minutes),
            ('SSH trust', self.known_hosts), ('SSH identity', self.identity),
            ('Local file root', self.files),
        )):
            ttk.Label(panel, text=label).grid(row=row, column=0, sticky='w', padx=(0, 12))
            ttk.Entry(panel, textvariable=field).grid(row=row, column=1, sticky='ew')
        options_row = ttk.Frame(panel)
        options_row.grid(row=5, column=0, columnspan=2, sticky='w', pady=6)
        ttk.Label(options_row, text='RunPod cloud tier').pack(side='left')
        ttk.Combobox(options_row, textvariable=self.cloud, values=('SECURE', 'COMMUNITY'),
                     width=12, state='readonly').pack(side='left', padx=(4, 12))
        ttk.Label(options_row, text='Worker profile').pack(side='left')
        ttk.Combobox(options_row, textvariable=self.profile, values=('simulation', 'live'),
                     width=12, state='readonly').pack(side='left', padx=(4, 0))
        self.cloud.trace_add('write', self.tier_changed)
        advanced_actions = ttk.Frame(panel)
        advanced_actions.grid(row=6, column=0, columnspan=2, sticky='w', pady=(6, 0))
        ttk.Button(advanced_actions, text='Deployment receipt', command=self.show_receipt).pack(side='left')
        self.copy_ssh_button = ttk.Button(advanced_actions, text='Copy SSH command', command=self.copy_ssh)
        self.copy_ssh_button.pack(side='left', padx=4)
        ttk.Button(advanced_actions, text='Configure live audio limits', command=self.audio_settings).pack(side='left')
        account_actions = ttk.Frame(panel)
        account_actions.grid(row=7, column=0, columnspan=2, sticky='w', pady=(6, 0))
        for provider, label in (('linode', 'Clear Linode token'), ('runpod', 'Clear RunPod key')):
            ttk.Button(account_actions, text=label,
                       command=lambda p=provider: self.clear_key(p)).pack(side='left', padx=4)
        ttk.Button(account_actions, text='Dismiss missing request', command=self.dismiss_missing).pack(side='left')
        self.advanced.grid_remove()
        box.columnconfigure(1, weight=1)
        box.rowconfigure(5, weight=1)
        self.refresh()
        self.lifecycle_after = self.root.after(1000, self.lifecycle_tick)
        self.root.bind('<Destroy>', self.window_destroyed, add='+')
        self.catalog_key = None
        for account in self.accounts.values():
            try:
                account.saved_key = account.store.load()
                account.key.set(account.saved_key)
            except (OSError, ValueError):
                self.status.set('Could not read the saved API key. Check ' + str(account.store.path))
        if self.credentials():
            self.schedule_catalog(self.initialize_saved_session)

    def credentials(self):
        return {provider: key for provider, account in self.accounts.items()
                if (key := account.key.get().strip())}

    def account_for(self, record):
        return self.accounts[record.get('provider', 'runpod')]

    def queued_ends(self, providers=None):
        return {(provider, name) for provider, account in self.accounts.items()
                if providers is None or provider in providers
                for name in (account.state.queued_linode_ends() if provider == 'linode'
                             else account.state.queued_ends())}

    def initialize_saved_session(self):
        if self.records or self.ending_names:
            self.startup_catalog(after=lambda: self.poll_owned(reconcile=True))
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
            for attribute in ('close_after', 'lifecycle_after', 'setup_after', 'catalog_after'):
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
            self.status.set('Wait for the current cloud operation to finish.')
            return
        record = self.selected_record()
        if (not record or record.get('status') != Ownership.TERMINATION_FAILED
                or record.get('id') or record.get('queue_id')):
            self.status.set('Select a failed request with no confirmed instance ID.')
            return
        account = self.account_for(record)
        key = account.key.get().strip()
        resource = 'instance' if account.name == 'Linode' else 'pod or queued request'
        if not messagebox.askyesno(
            'Dismiss missing request',
            f'In the {account.name} account for this API key, check that no {resource} is named {record["name"]}. '
            'Have you verified that this request is missing?',
        ):
            return
        if self.busy or self.session_record or account.key.get().strip() != key:
            self.status.set('The provider session changed. Review the request and try again.')
            return

        def verify(client):
            name = record['name']
            pods, queued = client.list_pods(), client.list_queued()
            if any(row.get('name') == name for row in (*pods, *queued)):
                raise ValueError(f'{account.name} still lists this request. Use End simulation to retry cleanup.')

        def discard(_result):
            records = account.state.read()
            if record not in records:
                self.status.set('The selected request changed. Review the instance list.')
                return
            from bootstrap import forget_bootstrap
            try:
                forget_bootstrap(record, account.state.path.parent)
                records.remove(record)
                account.state.write(records)
            except (OSError, ValueError):
                self.status.set('Could not dismiss the request. Check the lab state directory.')
                return
            self.refresh()
            self.status.set('Missing request dismissed.')

        self.background(verify, discard, account=account)

    def key_edited(self, _event=None):
        if self.busy:
            return
        account = next((a for a in self.accounts.values() if _event and a.entry is _event.widget), None)
        if self.save_key(account) and self.credentials() != self.catalog_key:
            self.tier_changed()
            if not self.credentials():
                self.status.set('Enter a Linode token or RunPod API key to load available compute.')

    def schedule_catalog(self, callback, delay=0):
        if self.catalog_after is not None or self.exit_after is not None:
            return

        def run():
            self.catalog_after = None
            callback()

        self.catalog_after = self.root.after(delay, run) if delay else self.root.after_idle(run)

    def startup_catalog(self, after=None):
        if self.exit_after is not None:
            return
        if self.busy:
            self.schedule_catalog(lambda: self.startup_catalog(after), delay=100)
        elif self.credentials():
            self.catalog(after=after)

    def save_key(self, account=None):
        for candidate in ((account,) if account else self.accounts.values()):
            key = candidate.key.get().strip()
            if key == (candidate.saved_key or ''):
                continue
            try:
                candidate.store.save(key)
                candidate.saved_key = key
            except ValueError as error:
                self.status.set(f'Check the {candidate.name} credential. {error}')
                return False
            except OSError:
                self.status.set(f'Could not save the {candidate.name} credential. Check {candidate.store.path}')
                return False
        return True

    def clear_key(self, provider='runpod'):
        if self.busy or self.account_in_use(provider):
            self.status.set('Wait for the simulation and pending termination before clearing the API key.')
            return
        account = self.accounts[provider]
        account.key.set('')
        if self.save_key(account):
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

    def copy_ssh(self):
        """Copy verified shell access for the open managed session."""
        if self.shell_command:
            try:
                self.root.clipboard_clear()
                self.root.clipboard_append(self.shell_command)
                self.status.set('SSH command copied. Keep the simulation open while using the instance.')
            except tk.TclError:
                self.status.set('Could not copy the SSH command to the clipboard.')

    def selected_pod_id(self):
        record = self.selected_record()
        return (record.get('provider', 'runpod'), record.get('id'), record.get('name')) if record else None

    def bind_fingerprint(self, *_):
        self.fingerprint_pod = (self.selected_pod_id()
                                if self.known_hosts.get().strip().startswith('SHA256:') else None)

    def clear_stale_fingerprint(self, _event=None):
        if self.known_hosts.get().strip().startswith('SHA256:') and self.fingerprint_pod != self.selected_pod_id():
            self.known_hosts.set('')
            self.status.set('The selected instance changed. Its console fingerprint can be entered in the SSH trust field.')
            return True
        return False

    def lifecycle_tick(self):
        if self.setup_stage and self.exit_after is None:
            self.update_setup_status()
        record = self.active_record()
        if (self.exit_after is None and not self.confirming_linode_end
                and record and self.bridge and not self.closing_bridge
                and not self.bridge_close_failed):
            state, detail = self.bridge.status()
            if state == 'active':
                self.browser_attached = True
            elif state == 'ended' or (state == 'failed' and self.browser_attached):
                if record.get('provider') == 'linode':
                    self.detach_linode_session(detail if state == 'failed' else 'Browser session closed.')
                else:
                    self.request_end(record, notice=detail if state == 'failed' else '')
            elif state == 'failed':
                self.setup_failed(detail or 'The browser session failed.')
        if time.monotonic() >= self.next_provider_poll:
            self.enforce_deadlines()
        self.finish_session_end()
        self.lifecycle_after = self.root.after(1000, self.lifecycle_tick)

    def enforce_deadlines(self):
        credentials = self.credentials()
        if self.busy or not credentials:
            return
        now = time.time()
        states = [self.accounts[p].state for p in credentials]
        records = [record for state in states for record in state.read()]
        if ('linode' in credentials and self.exit_after is None and not self.confirming_linode_end
                and not self.confirming_create):
            state = self.accounts['linode'].state
            approved = state.queued_linode_ends()
            legacy = state.queued_ends() - approved.keys()
            for record in records:
                name = record.get('name')
                if (record.get('provider') != 'linode' or name in approved
                        or record.get('status') in (Ownership.TERMINATED, Ownership.NOT_VISIBLE)
                        or (name, record.get('deadline')) in self.kept_linode_deadlines):
                    continue
                if Ownership.linode_needs_confirmation(record, now) or name in legacy:
                    reason = ('A saved termination request needs your confirmation.'
                              if name in legacy or Ownership._has_termination_intent(record)
                              else 'The instance deadline has been reached.')
                    self.confirm_linode_termination(record, reason)
                    if self.busy or self.exit_after is not None or self.window_closed.is_set():
                        return
        due = any(Ownership.needs_poll(record, now) for record in records)
        queued = bool(self.queued_ends(credentials))
        active = job_identity(self.session_record) if self.session_record else None
        prune = any(record.get('status') == Ownership.TERMINATED and job_identity(record) != active
                    for record in records)
        unresolved = any(
            (not record.get('id')
             and record.get('status') not in (Ownership.TERMINATED, Ownership.TERMINATION_FAILED))
            or (record.get('id') and record.get('status') in (Ownership.NOT_VISIBLE, Ownership.TERMINATION_FAILED))
            or (record.get('provider') == 'linode' and record.get('id')
                and str(record.get('status')).lower() in ('provisioning', 'booting', 'rebooting'))
            for record in records
        )
        self.next_provider_poll = time.monotonic() + (2 if due or queued else 15)
        if due or queued or prune or (unresolved and not self.setting_up):
            self.poll_owned(reconcile=unresolved and not due)

    def poll_owned(self, *, reconcile=False, after=None):
        active = job_identity(self.session_record) if self.session_record else None
        keys = self.credentials().values()

        def poll(state, client, keep_name, provider):
            owner = Ownership(state, client)
            owner.apply_queued_ends(provider)
            if reconcile:
                owner.reconcile()
            count, errors = owner.enforce_deadlines()
            return count, errors + owner.prune_terminated(keep_name=keep_name)

        def work(clients):
            count, errors = 0, []
            for provider, client in clients.items():
                account = self.accounts[provider]
                try:
                    keep_name = active[1] if active and active[0] == provider else None
                    sent, failures = poll(account.state, client, keep_name, provider)
                except (OSError, ValueError, RunPodError) as error:
                    errors.append(f'{account.name} cleanup check failed. ' + str(error))
                    continue
                count += sent
                errors.extend(failures)
            return count, [redact_keys(message, keys) for message in errors]

        def done(result):
            self.ending_names = self.queued_ends()
            self.refresh()
            if result[1]:
                self.status.set(result[1][0])
            elif result[0]:
                record = self.active_record()
                if record and record.get('status') == Ownership.TERMINATING:
                    self.status.set(' '.join(filter(None, (self.end_notice, 'Terminating the instance…'))))
            self.finish_session_end()
            if after:
                after()

        def failed(_message):
            if after:
                after()

        self.background(work, done, failed, show_progress=False)

    def refresh(self):
        selected = self.list.curselection()
        previous = getattr(self, 'visible_records', getattr(self, 'records', []))
        selected_job = (job_identity(previous[selected[0]])
                        if selected and selected[0] < len(previous) else None)
        self.records = [r for account in self.accounts.values() for r in account.state.read()]
        self.visible_records = [record for record in self.records
                                if record.get('status') not in (Ownership.TERMINATED, Ownership.NOT_VISIBLE)]
        self.list.delete(0, 'end')
        ending = self.ending_names
        for index, r in enumerate(self.visible_records):
            deadline = r.get('deadline')
            when = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(deadline)) if isinstance(deadline, (int, float)) else 'not set'
            status = r.get('status', 'Unknown')
            if job_identity(r) in ending:
                status = Ownership.TERMINATING
            compute = r.get('compute_id') or r.get('name')
            count = r.get('requested_gpu_count')
            if type(count) is int and count > 1:
                compute = f'{compute} x{count}'
            elif type(vcpu := r.get('requested_vcpu')) is int:
                compute = f'{compute}, {vcpu} vCPU'
            identity = r.get('id') or r.get('queue_id') or 'ID pending'
            account = self.account_for(r)
            placement = f'{account.name} {r["region"]}' if r.get('region') else account.name
            self.list.insert('end', f'{status}  |  {placement}  |  {compute}  |  {identity}  |  deadline {when}')
            if job_identity(r) == selected_job:
                self.list.selection_set(index)
                self.list.activate(index)
                self.list.see(index)
        self.clear_stale_fingerprint()
        for provider, account in self.accounts.items():
            locked = self.account_in_use(provider) and account.saved_key and account.key.get().strip() == account.saved_key
            account.entry.state(['disabled' if locked else '!disabled'])
        self.copy_ssh_button.state(['!disabled' if self.shell_command else 'disabled'])
        self.update_restart_button()

    def account_in_use(self, provider):
        return ((self.session_record and job_identity(self.session_record)[0] == provider)
                or (self.creating_name and self.creating_provider == provider)
                or any(p == provider for p, _ in self.ending_names)
                or any(job_identity(r)[0] == provider and r.get('status') == Ownership.TERMINATING
                       for r in self.records))

    def background(self, fn, done, failed=None, *, account=None, show_progress=True):
        if self.busy:
            return False
        keys = self.credentials()
        if not keys or (account and account.name.lower() not in keys):
            self.status.set('Enter an API key first.')
            return False
        clients, key_errors = {}, []
        for provider, key in keys.items():
            candidate = self.accounts[provider]
            if account and candidate is not account:
                continue
            if not self.save_key(candidate):
                key_errors.append(self.status.get())
                continue
            try:
                clients[provider] = candidate.client(key)
            except ValueError:
                key_errors.append(f'Check the {candidate.name} credential.')
        if not clients:
            self.status.set(' '.join(key_errors))
            return False
        self.busy = True
        if show_progress:
            self.status.set(f'Contacting {account.name if account else "cloud providers"}…')
        def work():
            error = None
            try:
                if account:
                    result = fn(clients[account.name.lower()])
                else:
                    result = fn(clients)
            except Exception as exc:
                # Avoid exception text that could include user-entered secrets.
                result, error = None, redact_keys(str(exc), keys.values())
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
                if key_errors:
                    self.status.set(self.status.get() + ' ' + ' '.join(key_errors))
            if self.create_interrupted and not self.busy:
                self.create_interrupted = False
                self.status.set(self.status.get() + ' Instance creation was interrupted. Click Start simulation again to review a fresh quote.')
        threading.Thread(target=work, daemon=True).start()
        return True

    def tier_changed(self, *_):
        self.choices = []
        self.compute.set('')
        self.dropdown['values'] = []
        if self.credentials():
            self.schedule_catalog(self.startup_catalog)

    def refresh_catalog(self):
        """Reload saved credentials only for untouched, empty account fields."""
        if self.exit_after is not None:
            return

        failures = []
        for account in self.accounts.values():
            if account.key.get().strip() or account.saved_key:
                continue
            try:
                saved_key = account.store.load()
            except (OSError, ValueError):
                failures.append(
                    f'Could not read the {account.name} credential. Check {account.store.path}.')
                continue
            if saved_key:
                account.saved_key = saved_key
                account.key.set(saved_key)

        # Let a running catalog retry with the new credentials and keep any
        # scheduled startup or reconciliation callback intact.
        if self.busy or self.catalog_after is not None:
            return

        if not self.credentials() and failures:
            self.status.set(' '.join(failures))
            return

        def report_key_errors():
            if failures:
                self.status.set(' '.join(filter(None, (self.status.get(), *failures))))

        self.catalog(after=report_key_errors if failures else None)

    def selected_compute(self):
        selected = self.compute.get()
        return next((value for label, value in self.choices if label == selected), None)

    def compute_selected(self, _event=None):
        self.preferred_compute = self.selected_compute()

    def catalog(self, after=None):
        if self.exit_after is not None:
            return
        cloud = self.cloud.get()
        keys = self.credentials()

        def retry():
            self.startup_catalog(after=after)

        def done(result):
            if self.exit_after is not None:
                return
            if self.cloud.get() != cloud or self.credentials() != keys:
                self.schedule_catalog(retry)
                return
            self.catalog_key = keys
            previous = self.preferred_compute
            if previous and previous.get('provider', 'runpod') == 'runpod':
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
            if after:
                after()

        def failed(_error):
            if after and self.exit_after is None:
                after()

        def fetch(clients):
            choices, failures = [], []
            if 'runpod' in clients:
                offers, failures = load_catalog(clients['runpod'], cloud)
                choices.extend((f'RunPod {label}', value) for label, value in offers)
            if 'linode' in clients:
                try:
                    offers, issues = clients['linode'].catalog()
                    choices.extend(offers)
                    failures.extend(issues)
                except (OSError, ValueError, RunPodError) as error:
                    failures.append('Linode catalog unavailable. ' + str(error))
            return choices, [redact_keys(issue, keys.values()) for issue in failures]

        self.background(fetch, done, failed)

    def create(self, settings):
        if self.exit_after is not None or self.window_closed.is_set():
            return
        if self.setting_up or self.deploying:
            self.status.set('Wait for the current setup to finish.')
            return
        selected = self.selected_compute()
        if not selected:
            self.status.set('Choose catalog compute and enter an image.')
            return
        account = self.account_for(selected)
        linode = account.name == 'Linode'
        image = LINODE_IMAGE if linode else self.image.get().strip()
        if not image:
            self.status.set('Choose catalog compute and enter an image.')
            return
        deadline_text = self.deadline_minutes.get()
        try:
            minutes = self.configured_deadline_minutes()
        except ValueError as error:
            self.status.set(str(error))
            return
        key = account.key.get().strip()
        close_generation = self.close_generation
        end_generation = self.end_generation
        def interrupted():
            return (self.close_generation != close_generation or self.end_generation != end_generation
                    or self.window_closed.is_set())
        def settings_changed():
            return (self.selected_compute() != selected or account.key.get().strip() != key
                    or (not linode and self.image.get().strip() != image)
                    or self.deadline_minutes.get() != deadline_text)
        def confirm(quote):
            if interrupted():
                self.quoting_create = False
                return
            if self.confirming_linode_end:
                self.root.after(100, lambda: confirm(quote))
                return
            self.quoting_create = False
            if settings_changed():
                self.status.set('The instance settings changed. Review the selection and start again.')
                return
            duration = f'{minutes // 60}-hour' if minutes % 60 == 0 else f'{minutes}-minute'
            placement = quote if linode else f'{quote} on {selected["cloud"].title()} Cloud'
            billing = ('Usage is billed in hourly increments. Outbound transfer and account-enabled backups may add to the charge.'
                       if linode else 'The hourly rate is the catalog compute list price. Storage may add to the charge.')
            resource = 'Linode instance' if linode else 'pod'
            deadline_notice = ('At the deadline, zLab asks before deleting the Linode. '
                               if linode else 'Automatic termination works only while this app is open with the API key entered. ')
            prompt = (f'{placement}\n{billing}\n'
                      f'Image: {image}\n'
                      f'Create one {resource} with a {duration} deadline? '
                      f'{deadline_notice}'
                      'Charges can continue after it closes.')
            count = selected.get('gpu', {}).get('count', 1)
            if count > 1:
                prompt += f'\n\nThis pod allocates {count} GPUs. The receiver currently uses one GPU per session.'
            if selected.get('cloud') == 'COMMUNITY':
                prompt += ('\n\nThe worker needs direct SSH via a public IP. '
                           'The Community Cloud stock estimate does not verify public IP availability.')
            self.confirming_create = True
            try:
                approved = messagebox.askyesno(f'Billable {account.name}', prompt)
            except tk.TclError:
                if self.window_closed.is_set():
                    return
                raise
            finally:
                self.confirming_create = False
            if interrupted():
                return
            if not approved:
                self.status.set('Instance creation cancelled.')
                return
            # Tk services events while its modal confirmation is open. An
            # expired pod still needs immediate attention during that time.
            if self.busy:
                self.create_interrupted = True
                self.status.set('Another provider operation is running. Instance creation was interrupted.')
                return
            if settings_changed():
                self.status.set('The instance settings changed. Review the selection and start again.')
                return
            deadline = time.time() + minutes * 60
            name = Ownership.new_pod_name()
            def created(_result):
                # A Tk modal dialog can deliver the create response while the
                # user is still deciding whether to keep the new instance.
                if self.confirming_linode_end:
                    self.root.after(100, lambda: created(_result))
                    return
                self.creating_name = None
                record = next((r for r in self.records if job_identity(r) == (account.name.lower(), name)), None)
                if linode and self.exit_after is not None:
                    # close() recorded its decision by the pending create name.
                    # Its poll will handle approval after the provider ID arrives.
                    self.setup_stage = ''
                    self.setup_started = None
                    return
                if record:
                    self.select_record(record)
                    if job_identity(record) in self.ending_names or self.exit_after is not None:
                        self.request_end(record)
                    else:
                        self.prepare_worker(record, settings, new=True)
                else:
                    self.setup_stage = ''
                    self.setup_started = None
                    self.refresh()
                    if (account.name.lower(), name) in self.ending_names:
                        self.status.set('Instance creation cancelled.')
                        self.enforce_deadlines()

            def failed(_message):
                # A timed-out POST can leave a real resource under the saved intent.
                # Recover only that intent, never send another create.
                created(None)

            self.creating_name = name
            self.creating_provider = account.name.lower()
            self.setup_started = time.monotonic()
            self.set_setup_stage(f'Creating the {account.name} instance…')
            self.refresh()
            if not self.background(lambda c: Ownership(account.state, c).create(selected, image, deadline, name=name),
                                   created, failed, account=account, show_progress=False):
                self.creating_name = None
                self.setup_stage = ''
                self.setup_started = None
                self.refresh()
        def quote_failed(_error):
            self.quoting_create = False
            if interrupted():
                self.status.set('Instance creation cancelled.')

        def quote(client):
            return client.creation_quote(selected) if linode else creation_quote(client, selected)

        self.quoting_create = self.background(quote, confirm, quote_failed, account=account)

    def select_record(self, record):
        self.refresh()
        self.list.selection_clear(0, 'end')
        for index, item in enumerate(self.visible_records):
            if job_identity(item) == job_identity(record):
                self.list.selection_set(index)
                self.list.activate(index)
                self.list.see(index)
                break
        self.update_restart_button()

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
        return self.visible_records[selected[0]] if selected else None

    def active_record(self):
        if self.session_record:
            active = job_identity(self.session_record)
            return next((record for record in self.records if job_identity(record) == active), None)
        return None

    def restartable_linodes(self):
        return [r for r in self.visible_records if r.get('provider') == 'linode'
                and r.get('id') and str(r.get('status')).lower() in ('running', 'kept')
                and job_identity(r) not in self.ending_names and not Ownership.linode_approved(r)]

    def restart_target(self, candidates):
        record = self.selected_record() or self.active_record()
        if record is None and len(candidates) == 1:
            record = candidates[0]
        if (record not in candidates or self.exit_after is not None or self.confirming_linode_end
                or self.busy or self.setting_up or self.deploying or self.creating_name
                or self.closing_bridge or self.quoting_create or self.confirming_create):
            return None
        if self.session_record and job_identity(self.session_record) != job_identity(record):
            return None
        return record

    def update_restart_button(self, _event=None):
        candidates = self.restartable_linodes()
        if candidates:
            self.restart_button.pack(side='left')
            self.restart_button.state(['!disabled' if self.restart_target(candidates) else 'disabled'])
        else:
            self.restart_button.pack_forget()
            self.restart_button.state(['disabled'])

    def restart_simulation(self):
        record = self.restart_target(self.restartable_linodes())
        if record is None:
            self.status.set('Select a running Linode to restart its simulation.')
            return
        try:
            settings = self.session_settings()
        except (OSError, ValueError) as error:
            self.status.set(str(error))
            return
        if not self.bridge:
            self.resume_owned_worker(record, settings)
            return

        # Close the selected Linode's relay (or retry a failed local detach)
        # before starting the replacement session.
        active = self.active_record()
        if ((active and (job_identity(active) != job_identity(record) or active.get('id') != record['id']))
                or (not active and not self.bridge_close_failed)):
            self.status.set('Select the Linode that owns the open simulation.')
            return
        self.cancel_setup()
        cancelled = self.setup_cancelled = threading.Event()

        def reopen():
            self.setup_after = None
            if cancelled.is_set() or self.exit_after is not None or self.window_closed.is_set():
                return
            if self.busy or self.confirming_linode_end:
                self.setting_up = True
                self.setup_after = self.root.after(100, reopen)
                return
            self.setting_up = False
            self.session_record = None
            self.refresh()
            current = next((r for r in self.restartable_linodes()
                            if r['id'] == record['id'] and r['name'] == record['name']), None)
            if current is None:
                self.status.set('The selected Linode is no longer available for restart.')
                return
            self.resume_owned_worker(current, settings)

        self.status.set('Closing the previous simulation connection before restarting…')
        self.release_bridge(on_closed=reopen)
        self.update_restart_button()

    def resume_owned_worker(self, record, settings):
        if record.get('provider') == 'linode':
            account = self.accounts['linode']
            if not account.key.get().strip():
                self.status.set('Enter the Linode API token to restart its simulation.')
                return
            try:
                deadline = record.get('deadline')
                expired = type(deadline) not in (int, float) or not math.isfinite(deadline) or deadline <= time.time()
                if Ownership.linode_approved(record) or record['name'] in account.state.queued_linode_ends():
                    raise ValueError('Termination of this Linode is already underway.')
                if expired or Ownership._has_termination_intent(record):
                    new_deadline = None
                    if expired:
                        new_deadline = time.time() + self.configured_deadline_minutes() * 60
                    record = Ownership(account.state, None).keep_linode(record, deadline=new_deadline)
            except (OSError, ValueError) as error:
                self.status.set(str(error))
                return
        if Ownership._has_termination_intent(record) or Ownership.needs_poll(record, time.time()):
            self.status.set('This instance is ending. Select another instance or wait for termination.')
            return
        self.prepare_worker(record, settings)

    def configured_deadline_minutes(self):
        try:
            minutes = int(self.deadline_minutes.get())
        except ValueError:
            minutes = 0
        if not 1 <= minutes <= POD_DEADLINE_LIMIT_MINUTES:
            raise ValueError(f'Deadline must be 1–{POD_DEADLINE_LIMIT_MINUTES} minutes.')
        return minutes

    def set_setup_stage(self, stage):
        self.setup_stage = stage
        self.update_setup_status()

    def update_setup_status(self):
        elapsed = max(0, int(time.monotonic() - self.setup_started)) if self.setup_started is not None else 0
        minutes, seconds = divmod(elapsed, 60)
        self.status.set(f'{self.setup_stage} ({minutes}:{seconds:02d})')

    def start_simulation(self):
        if self.exit_after is not None:
            return
        if self.bridge:
            self.status.set('Choose End simulation to retry closing the previous connection.' if self.bridge_close_failed
                            else 'The simulation is already open in your browser.')
            return
        record = self.selected_record()
        selected = record or self.selected_compute() or {}
        provider = selected.get('provider', 'runpod')
        if (self.session_record or self.busy or self.setting_up or self.deploying or self.closing_bridge
                or any(p == provider for p, _ in self.ending_names)):
            self.status.set('Wait for the current setup or termination to finish.')
            return
        try:
            settings = self.session_settings()
        except (OSError, ValueError) as error:
            self.status.set(str(error))
            return
        if record and record.get('status') not in (Ownership.TERMINATED, Ownership.NOT_VISIBLE):
            self.resume_owned_worker(record, settings)
        elif self.credentials() != self.catalog_key:
            self.catalog()
        else:
            self.create(settings)

    def prepare_worker(self, record, settings, *, new=False):
        deadline = record.get('deadline')
        if type(deadline) not in (int, float) or not math.isfinite(deadline) or deadline <= time.time():
            self.status.set('The selected instance deadline is missing or expired.')
            self.enforce_deadlines()
            return
        account = self.account_for(record)
        key = account.key.get().strip()
        if not key or not self.save_key(account):
            self.status.set(f'Enter a {account.name} API key to start the simulation.')
            return
        self.session_record = record
        self.session_is_new = new
        if self.setup_started is None:
            self.setup_started = time.monotonic()
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
                records = Ownership(account.state, client).reconcile()
                current = next((item for item in records if item.get('name') == name), None)
                if not current or not current.get('id'):
                    return None
            if record.get('id') and current['id'] != record['id']:
                raise ValueError('The selected instance identity changed.')
            pod = owned_pod_details(client, current)
            if (pod.get('status') in ('ERROR', 'EXITED', 'TERMINATED', 'FAILED', 'STOPPED', 'DELETING')
                    or (pod.get('status') == 'OFFLINE' and not new)):
                raise ValueError('The selected instance is ' + pod['status'] + '.')
            if pod.get('status') != 'RUNNING' or not (pod.get('ssh') or {}).get('direct'):
                return None
            simulation_gpu(current, pod)
            try:
                return current, *owned_pod_connection(client, current, known_hosts, account.state.path.parent)
            except ValueError as error:
                if str(error).startswith(('Could not read a bounded SSH host key', 'Direct SSH is not ready')):
                    return None
                # Ubuntu can initially expose its image key while cloud-init
                # installs the managed key. Retry the same pin until the deadline.
                if (new and account.name == 'Linode' and not known_hosts and current.get('ssh_fingerprint')
                        and str(error).startswith('SSH host key mismatch with the expected key')):
                    return None
                raise

        def check_ready():
            self.setup_after = None
            if cancelled.is_set():
                return
            if account.key.get().strip() != key:
                self.setup_failed('The API key changed during setup.')
                return
            if time.time() >= ready_until:
                self.setup_failed('Timed out waiting for the instance and its authenticated SSH endpoint.')
                self.enforce_deadlines()
                return
            if self.busy:
                self.setup_after = self.root.after(250, check_ready)
                return
            def failed(message):
                if not cancelled.is_set():
                    self.setup_failed(message)
            self.background(connect, connected, failed, account=account, show_progress=False)

        def connected(connection):
            if cancelled.is_set():
                return
            if connection is None:
                self.set_setup_stage(f'Waiting for {account.name} startup and SSH…')
                self.setup_after = self.root.after(2000, check_ready)
                return
            current, pod, pin = connection
            self.session_record = current
            self.deploying = True
            self.set_setup_stage(f'Connecting to {account.name} to deploy the worker…')

            def set_progress(stage):
                def show():
                    if not cancelled.is_set():
                        self.set_setup_stage(stage)
                self.post_to_window(show)

            def work():
                bridge = None
                receipt = ''
                error = ''
                shell_command = ''
                try:
                    receipt = deploy_selected(pod, current, pin, identity, key, profile=profile,
                                              on_progress=set_progress, cancelled=cancelled)
                    if not cancelled.is_set():
                        set_progress('Starting the simulation worker…')
                        command = ssh_command(pod, pin, identity,
                                              file_channel=files is not None,
                                              live=profile == 'live',
                                              prefer_gpu=simulation_gpu(current, pod))
                        shell_command = shlex.join(ssh_endpoint(pod, pin, identity))
                        if profile == 'live':
                            command = live_command(command, *audio)
                        from browser_bridge import start
                        remaining = deadline - time.time()
                        if remaining <= 0:
                            raise ValueError('The instance deadline expired during setup.')
                        bridge = start(command, files=files, live=profile == 'live', managed=True,
                                       ready_timeout=min(30, remaining))
                except Exception as exc:
                    error = str(exc).replace(key, '[redacted]')[:1000]
                posted = self.post_to_window(lambda: deployed(current, receipt, bridge, error, shell_command))
                if not posted and bridge is not None:
                    try:
                        bridge.close()
                    except Exception:
                        pass

            threading.Thread(target=work, daemon=True).start()

        def deployed(current, receipt, bridge, error, shell_command):
            if self.busy and not cancelled.is_set():
                self.root.after(100, lambda: deployed(current, receipt, bridge, error, shell_command))
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
                records = account.state.read()
                row = next(item for item in records if item.get('id') == current['id'] and item.get('name') == name)
                if Ownership._has_termination_intent(row) or time.time() >= deadline:
                    raise ValueError('The instance deadline expired during setup.')
                row.setdefault('deployments', {})[profile] = receipt
                row['status'] = 'Running'
                account.state.write(records)
                self.deployment_receipt.set(receipt)
                if not webbrowser.open(bridge.url):
                    raise ValueError('Could not open the simulation in your browser.')
                self.shell_command = shell_command
                self.setting_up = False
                self.setup_stage = ''
                self.setup_started = None
                session = 'Simulation' if profile == 'simulation' else 'Experimental Live/Duplex'
                on_close = ('Closing the browser keeps the Linode running.' if account.name == 'Linode'
                            else 'Closing the browser ends its pod.')
                self.status.set(f'{session} opened. {on_close}')
                self.refresh()
            except Exception as exc:
                self.setup_failed(str(exc).replace(key, '[redacted]')[:1000])

        self.set_setup_stage(f'Waiting for {account.name} startup and SSH…')
        check_ready()

    def cancel_setup(self):
        self.shell_command = ''
        self.copy_ssh_button.state(['disabled'])
        self.setting_up = False
        self.setup_stage = ''
        self.setup_started = None
        if self.setup_cancelled:
            self.setup_cancelled.set()
        if self.setup_after:
            self.root.after_cancel(self.setup_after)
            self.setup_after = None

    def setup_failed(self, message):
        record = self.active_record()
        if record and record.get('provider') == 'linode':
            if self.exit_after is None:
                self.detach_linode_session(f'Setup stopped. {message}')
            return
        self.cancel_setup()
        self.release_bridge()
        if self.exit_after is not None:
            return
        record = self.active_record()
        if record and self.session_is_new:
            self.request_end(record, notice=f'Setup stopped. {message}')
        else:
            self.session_record = None
            self.status.set(f'Setup stopped. {message} Start again to retry the selected instance or end it.')
            self.refresh()

    def release_bridge(self, on_closed=None):
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
            elif on_closed:
                on_closed()
            self.update_restart_button()

        threading.Thread(target=work, daemon=True).start()

    def end_simulation(self):
        if self.confirming_linode_end or self.exit_after is not None:
            return
        if self.quoting_create or self.confirming_create:
            self.end_generation += 1
            self.status.set('Instance creation cancelled.')
            return
        active = self.active_record()
        if self.creating_name:
            active = {'name': self.creating_name, 'provider': self.creating_provider}
        record = active or self.selected_record()
        if record is None and len(self.visible_records) == 1:
            record = self.visible_records[0]
        if not record:
            if self.bridge_close_failed:
                self.release_bridge()
                return
            self.end_generation += 1
            self.status.set('Select an instance to end its simulation.' if self.visible_records
                            else 'No instance is running.')
            return
        if (record.get('provider') != 'linode' and not active
                and not messagebox.askyesno('End simulation', 'Terminate the selected instance and discard its storage?')):
            return
        self.request_end(record)

    def detach_linode_session(self, notice):
        self.cancel_setup()
        self.release_bridge()
        self.session_record = None
        self.browser_attached = False
        self.refresh()
        self.status.set(f'{notice} The Linode is kept. Choose Restart simulation to reconnect.')

    def confirm_linode_termination(self, record, reason):
        """Publish consent for the named instance before any termination work."""
        state = self.accounts['linode'].state
        name = record['name']
        identity = record.get('id')
        try:
            queued = state.queued_linode_ends()
            if name in queued and queued[name] in (None, identity):
                return True
            if not Ownership.linode_approved(record):
                if self.confirming_linode_end or self.exit_after is not None:
                    return False
                self.confirming_linode_end = True
                try:
                    target = f'{name} (ID {identity})' if identity else name
                    approved = messagebox.askyesno(
                        'Terminate Linode?', f'{reason}\n\nTerminate {target} and delete its disk data? '
                        'Linode rounds usage up to whole hours. Choosing No keeps the instance running and billable.',
                        default=messagebox.NO, parent=self.root)
                finally:
                    self.confirming_linode_end = False
                if not approved:
                    if Ownership.linode_needs_confirmation(record, time.time()) or name in state.queued_ends():
                        self.kept_linode_deadlines.add((name, record.get('deadline')))
                        if not self.busy:
                            Ownership(state, None).keep_linode(record)
                    self.status.set('Linode kept. The instance remains billable.')
                    return False
            state.queue_linode_end(record)
            self.ending_names.add(job_identity(record))
            return True
        except (OSError, ValueError, tk.TclError) as error:
            self.status.set(f'Could not confirm Linode termination. {error}')
            return False

    def request_end(self, record, *, notice=''):
        name = record['name']
        account = self.account_for(record)
        if account.name == 'Linode':
            if not self.confirm_linode_termination(record, notice or 'End simulation was requested.'):
                return
        else:
            try:
                account.state.queue_end(name)
            except (OSError, ValueError) as error:
                self.status.set(str(error))
                return
        self.end_generation += 1
        self.cancel_setup()
        self.release_bridge()
        self.ending_names.add(job_identity(record))
        self.session_record = record
        self.end_notice = notice
        ending = ('Ending simulation…' if account.key.get().strip()
                  else f'Ending simulation… Enter a {account.name} API key to finish stopping the instance.')
        self.status.set(' '.join(filter(None, (notice, ending))))
        self.next_provider_poll = 0
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
            if job_identity(record) in self.ending_names:
                return
            if self.exit_after is None:
                self.session_record = None
                self.refresh()
            if status == Ownership.TERMINATION_FAILED:
                error = record.get('termination_error', '')
                self.status.set(f'Termination failed. {error} Select the instance and choose End simulation to retry.')
            elif status == Ownership.NOT_VISIBLE:
                self.status.set(f'The instance is no longer visible to this {self.account_for(record).name} API key.')
            else:
                result = 'Linode instance terminated.' if record.get('provider') == 'linode' else 'Pod terminated.'
                self.status.set(' '.join(filter(None, (getattr(self, 'end_notice', ''), result))))
        elif (self.session_record and not record and self.creating_name is None and not self.busy
              and job_identity(self.session_record) not in self.ending_names):
            self.session_record = None
            self.refresh()

    def close(self):
        if self.exit_after is not None or self.window_closed.is_set() or self.confirming_linode_end:
            return
        linode_state = self.accounts['linode'].state
        settled = (Ownership.TERMINATED, Ownership.NOT_VISIBLE)
        try:
            linodes = {r['name']: r for r in linode_state.read()
                       if r.get('provider') == 'linode' and r.get('status') not in settled}
            queued = linode_state.queued_linode_ends()
        except (OSError, ValueError) as error:
            self.status.set(f'Could not check saved Linodes before closing. {error}')
            return
        if self.creating_name and self.creating_provider == 'linode':
            linodes.setdefault(self.creating_name, {'provider': 'linode', 'name': self.creating_name})
        ending = {name for name, r in linodes.items() if Ownership.linode_approved(r)
                  or (name in queued and queued[name] in (None, r.get('id')))
                  or (r.get('status') == Ownership.TERMINATING and r.get('id')
                      and Ownership._pod_termination_confirmed(r))}
        undecided = [r for name, r in linodes.items() if name not in ending]
        if undecided:
            instances = '\n'.join(
                f"{r['name']}  ({r.get('region') or 'region pending'}, ID {r.get('id') or 'pending'})"
                for r in undecided)
            self.confirming_linode_end = True
            try:
                answer = messagebox.askyesnocancel(
                    'Terminate Linodes and close zLab?',
                    'Linode bills in full-hour increments.\n\n' + instances +
                    '\n\nYes — terminate the listed Linodes, delete their storage, and close zLab.'
                    '\nNo — keep them running and close zLab. Billing continues.'
                    '\nCancel — stay in zLab.',
                    default=messagebox.CANCEL, parent=self.root)
            except tk.TclError:
                return
            finally:
                self.confirming_linode_end = False
            if answer is None:
                return
            if answer:
                try:
                    for record in undecided:
                        linode_state.queue_linode_end(record)
                        ending.add(record['name'])
                except (OSError, ValueError) as error:
                    self.status.set(f'Could not save all Linode termination requests. {error}')
                    return

        self.close_linode_ends = ending
        self.ending_names = self.queued_ends()
        self.refresh()
        self.close_generation += 1
        self.exit_after = time.monotonic() + self._CLOSE_GRACE
        self.close_attempted.clear()
        self.cancel_setup()
        if self.bridge:
            self.release_bridge()
        self.status.set('Closing zLab…')
        self.next_provider_poll = 0
        record = ({'name': self.creating_name, 'provider': self.creating_provider}
                  if self.creating_name else self.active_record())
        if (record and record.get('provider') != 'linode' and record.get('status') not in settled
                and job_identity(record) not in self.ending_names):
            self.close_attempted.add(job_identity(record))
            self.request_end(record)
        self.check_close()

    def check_close(self):
        self.close_after = None
        record = self.active_record()
        settled = (Ownership.TERMINATED, Ownership.NOT_VISIBLE)
        if (not record or record.get('status') in settled
                or (record.get('provider') == 'linode' and record['name'] not in self.close_linode_ends)):
            record = next((item for item in self.records if item.get('status') not in settled and
                           (item['name'] in self.close_linode_ends if item.get('provider') == 'linode'
                            else item.get('status') == Ownership.TERMINATING)), None)
        if record:
            self.session_record = record
            status = record.get('status')
            if record.get('provider') == 'linode' or status == Ownership.TERMINATING:
                if time.monotonic() >= self.next_provider_poll:
                    self.enforce_deadlines()
            elif (status != Ownership.TERMINATION_FAILED and job_identity(record) not in self.close_attempted
                  and job_identity(record) not in self.ending_names):
                self.close_attempted.add(job_identity(record))
                self.request_end(record)
        elif not (self.bridge or self.busy or self.deploying or self.enumerating
                  or self.quoting_create or self.confirming_create or self.creating_name):
            if self.save_key():
                self.root.destroy()
                return

        if time.monotonic() < self.exit_after:
            self.close_after = self.root.after(100, self.check_close)
            return
        if messagebox.askyesno(
            'Close zLab?',
            'A cloud or local operation is still unresolved. Close the lab anyway? '
            'Saved instance records will be checked again when you reopen the lab. '
            'Check your cloud account for any instance that may still be running.',
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
            'zLab requires Python Tk. On Debian or Ubuntu, install it with '
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
        raise SystemExit('Another zLab controller is already running')
    root = tk.Tk()
    App(root)
    root.mainloop()
    lock.close()


if __name__ == '__main__':
    main()
