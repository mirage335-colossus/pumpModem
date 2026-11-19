"""Real Tk widget checks with isolated fake provider and no cloud operations.

These tests create their own withdrawn window; a working Tk display is needed.
They never read the user's lab state or access an account, network, or audio.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import types
import unittest
from unittest.mock import patch

import app as lab
from runpod import RunPodClient, RunPodError

try:
    import tkinter as tk
    from tkinter import messagebox, ttk
except ImportError:
    tk = ttk = messagebox = None


class FakeProvider:
    def __init__(self):
        self.gpus = [{'id': 'NVIDIA GeForce RTX 4090'}]
        self.cpus = [{'id': 'test-cpu', 'vcpu': {'min': 2, 'max': 32}, 'ramGbPerVcpu': 4}]
        self.calls = []
        self.pods = []
        self.queued = []
        self.created = []
        self.cancelled = []
        self.deleted = []
        self.create_error = None

    def list_gpus(self):
        self.calls.append('gpus')
        if isinstance(self.gpus, Exception):
            raise self.gpus
        return self.gpus

    def list_cpus(self):
        self.calls.append('cpus')
        if isinstance(self.cpus, Exception):
            raise self.cpus
        return self.cpus

    def create_pod(self, payload):
        self.calls.append('create')
        self.created.append(payload)
        if self.create_error:
            raise self.create_error
        self.queued = [{'id': 'queue-1', 'name': payload['name'], 'status': 'QUEUED'}]
        return 202, {'id': 'queue-1', 'podId': None, 'status': 'QUEUED'}

    def list_pods(self):
        self.calls.append('pods')
        return self.pods

    def list_queued(self):
        self.calls.append('queued')
        return self.queued

    def cancel_queued(self, identifier):
        self.calls.append('cancel')
        self.cancelled.append(identifier)

    def delete_pod(self, identifier):
        self.calls.append('delete')
        self.deleted.append(identifier)

    def get_pod(self, identifier):
        self.calls.append('get')
        return next(p for p in self.pods if p['id'] == identifier)


@unittest.skipIf(tk is None, 'Python Tk is not installed')
class TkControllerIntegration(unittest.TestCase):
    def setUp(self):
        try:
            self.root = tk.Tk()
        except tk.TclError as error:
            self.skipTest('Tk display unavailable: ' + str(error))
        self.root.withdraw()
        self.tmp = tempfile.TemporaryDirectory(prefix='pump-lab-gui-test-')
        self.addCleanup(self.tmp.cleanup)
        self.addCleanup(self.close_root)
        self.provider = FakeProvider()
        self.patches = [patch.dict(os.environ, {'XDG_STATE_HOME': self.tmp.name}),
                        patch.object(lab, 'tk', tk, create=True),
                        patch.object(lab, 'ttk', ttk, create=True),
                        patch.object(lab, 'messagebox', messagebox, create=True),
                        patch.object(lab, 'RunPodClient', side_effect=lambda key: self.provider)]
        for item in self.patches:
            item.start()
            self.addCleanup(item.stop)
        self.controller = lab.App(self.root)
        self.root.update_idletasks()
        self.assertIn('RunPod Lab', self.root.title())

    def close_root(self):
        try:
            if self.root.winfo_exists():
                self.root.destroy()
        except tk.TclError:
            pass

    def descendants(self, root=None):
        root = root or self.root
        for child in root.winfo_children():
            yield child
            yield from self.descendants(child)

    def button(self, label, root=None):
        matches = [w for w in self.descendants(root) if isinstance(w, ttk.Button) and w.cget('text') == label]
        self.assertEqual(len(matches), 1, label)
        return matches[0]

    def entry_for(self, variable, root=None):
        matches = [w for w in self.descendants(root) if isinstance(w, (ttk.Entry, ttk.Combobox))
                   and w.cget('textvariable') == str(variable)]
        self.assertEqual(len(matches), 1)
        return matches[0]

    def set_entry(self, variable, value, root=None):
        entry = self.entry_for(variable, root)
        entry.delete(0, 'end')
        entry.insert(0, value)
        return entry

    def execute(self, action, done, timeout=4):
        """Run actual callbacks in Tk mainloop, allowing worker-thread after()."""
        end = time.monotonic() + timeout
        failure = []
        def start():
            try:
                action()
            except BaseException as error:
                failure.append(error)
                self.root.quit()
        def poll():
            try:
                if done():
                    self.root.quit()
                elif time.monotonic() >= end:
                    failure.append(AssertionError('GUI action did not finish: ' + self.controller.status.get()))
                    self.root.quit()
                else:
                    self.root.after(10, poll)
            except BaseException as error:
                failure.append(error)
                self.root.quit()
        self.root.after(0, start)
        self.root.after(10, poll)
        self.root.mainloop()
        if failure:
            raise failure[0]

    def load_catalog(self):
        self.set_entry(self.controller.key, 'dummy-session-key')
        self.execute(self.button('Load catalog').invoke,
                     lambda: not self.controller.busy and 'Catalog' in self.controller.status.get())

    def test_catalog_403_preserves_other_catalog_and_does_not_leak_key(self):
        self.provider.cpus = RunPodError('dummy-session-key hidden detail', status=403)
        entry = self.set_entry(self.controller.key, 'dummy-session-key')
        self.assertTrue(entry.cget('show'))
        self.execute(self.button('Load catalog').invoke,
                     lambda: not self.controller.busy and 'Catalog' in self.controller.status.get())
        self.assertEqual(self.provider.calls, ['gpus', 'cpus'])
        self.assertIn('RTX 4090', self.controller.compute.get())
        self.assertEqual(len(self.controller.dropdown['values']), 1)
        self.assertIn('403', self.controller.status.get())
        self.assertIn('catalog:read', self.controller.status.get())
        self.assertNotIn('dummy-session-key', self.controller.status.get())

    def test_dark_theme_readable_contrast(self):
        """Check actual dark-theme widget colors without accessing clipboard."""
        style = ttk.Style(self.root)
        def luminance(color):
            values = [value / 65535 for value in self.root.winfo_rgb(color)]
            linear = [value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4
                      for value in values]
            return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2]
        def contrast(foreground, background):
            first, second = sorted((luminance(foreground), luminance(background)))
            return (second + 0.05) / (first + 0.05)
        pairs = [
            (style.lookup('TLabel', 'foreground'), style.lookup('TLabel', 'background')),
            (style.lookup('Hint.TLabel', 'foreground'), style.lookup('Hint.TLabel', 'background')),
            (style.lookup('TButton', 'foreground'), style.lookup('TButton', 'background')),
            (style.lookup('TEntry', 'foreground'), style.lookup('TEntry', 'fieldbackground')),
            (style.lookup('TCombobox', 'foreground', ('readonly',)),
             style.lookup('TCombobox', 'fieldbackground', ('readonly',))),
            (self.controller.list.cget('foreground'), self.controller.list.cget('background')),
            (self.controller.list.cget('selectforeground'), self.controller.list.cget('selectbackground')),
        ]
        for foreground, background in pairs:
            self.assertGreaterEqual(contrast(foreground, background), 4.5, (foreground, background))

    @unittest.skipUnless(os.environ.get('LAB_ISOLATED_X11') == '1',
                         'set LAB_ISOLATED_X11=1 only inside a dedicated Xvfb display')
    def test_double_click_copies_visible_status(self):
        """Copy fabricated status in an explicitly isolated X11 clipboard."""
        self.provider.cpus = RunPodError('dummy-session-key hidden detail', status=403)
        self.load_catalog()
        displayed = self.controller.status.get()
        self.assertIn('403', displayed)
        self.assertNotIn('dummy-session-key', displayed)
        # Map the actual label so Tk dispatches two real single-button events as
        # a double-click; event_generate cannot itself synthesize a Double modifier.
        self.root.deiconify()
        self.root.update()
        label = self.controller.status_label
        for moment in (1000, 1100):
            label.event_generate('<ButtonPress-1>', x=4, y=4, time=moment, when='now')
            label.event_generate('<ButtonRelease-1>', x=4, y=4, time=moment+20, when='now')
        self.root.update()
        self.assertEqual(self.root.clipboard_get(), displayed)
        self.assertNotIn('dummy-session-key', self.root.clipboard_get())
        screenshot = os.environ.get('LAB_GUI_SCREENSHOT_XWD')
        if screenshot:
            with open(screenshot, 'wb') as output:
                subprocess.run(['xwd', '-id', hex(self.root.winfo_id()), '-silent'],
                               stdout=output, check=True, timeout=5)

    def test_real_client_fake_http_403_surfaces_affected_endpoint(self):
        """Exercise the real client, Tk callback and UI with a fake HTTP transport."""
        requests = []
        def transport(method, url, headers, body, timeout):
            requests.append((method, url, body))
            self.assertEqual(headers.get('Authorization'), 'Bearer dummy-session-key')
            if url.endswith('/catalog/gpus'):
                return 403, b'{"detail":"untrusted dummy-session-key"}'
            if url.endswith('/catalog/cpus'):
                return 200, json.dumps({'cpus': self.provider.cpus}).encode()
            raise AssertionError('unexpected fake HTTP request')
        client = RunPodClient('dummy-session-key', transport=transport)
        self.set_entry(self.controller.key, 'dummy-session-key')
        with patch.object(lab, 'RunPodClient', return_value=client):
            self.execute(self.button('Load catalog').invoke,
                         lambda: not self.controller.busy and 'Catalog' in self.controller.status.get())
        self.assertEqual([row[1].rsplit('/', 1)[-1] for row in requests], ['gpus', 'cpus'])
        self.assertIn('CPU:', self.controller.compute.get())
        self.assertIn('GPU catalog', self.controller.status.get())
        self.assertIn('403', self.controller.status.get())
        self.assertNotIn('dummy-session-key', self.controller.status.get())

    def test_deadline_validation_cancel_create_and_queue_cancel(self):
        self.load_catalog()
        create = self.button('Create billable pod')
        self.set_entry(self.controller.deadline_minutes, '0')
        self.execute(create.invoke, lambda: 'Deadline must' in self.controller.status.get())
        self.assertNotIn('create', self.provider.calls)
        self.set_entry(self.controller.deadline_minutes, '5')
        with patch.object(messagebox, 'askyesno', return_value=False):
            self.execute(create.invoke, lambda: not self.controller.busy)
        self.assertNotIn('create', self.provider.calls)
        with patch.object(messagebox, 'askyesno', return_value=True):
            self.execute(create.invoke, lambda: not self.controller.busy and 'Create submitted' in self.controller.status.get())
        self.assertEqual(len(self.provider.created), 1)
        saved = self.controller.state.read()
        self.assertEqual(saved[0]['queue_id'], 'queue-1')
        self.assertNotIn('id', saved[0])
        self.assertNotIn('dummy-session-key', self.controller.state.path.read_text())
        self.controller.list.selection_set(0)
        with patch.object(messagebox, 'askyesno', return_value=True):
            self.execute(self.button('Terminate selected pod').invoke,
                         lambda: not self.controller.busy and 'Termination requested' in self.controller.status.get())
        self.assertEqual(self.provider.cancelled, ['queue-1'])
        self.assertEqual(self.provider.deleted, [])

    def test_create_403_is_visible_and_does_not_leave_pending_job(self):
        self.load_catalog()
        self.provider.create_error = RunPodError(
            'POST /v2/pods: request failed (HTTP 403); check the pods:write scope and account or team access',
            status=403, operation='POST /v2/pods', required_scope='pods:write')
        with patch.object(messagebox, 'askyesno', return_value=True):
            self.execute(self.button('Create billable pod').invoke,
                         lambda: not self.controller.busy and '403' in self.controller.status.get())
        self.assertIn('pods:write', self.controller.status.get())
        self.assertEqual(self.controller.state.read(), [])
        self.assertNotIn('dummy-session-key', self.controller.status.get())

    def test_restart_reconciliation_and_expired_queue(self):
        now = time.time()
        self.controller.state.write([{'name': 'fixture-job', 'deadline': now - 1}])
        self.provider.queued = [{'name': 'fixture-job', 'id': 'q-test', 'status': 'QUEUED'}]
        self.controller.refresh()
        self.assertIn('fixture-job', self.controller.list.get(0))
        # Key entry triggers the same idle deadline check used after a restart.
        self.set_entry(self.controller.key, 'dummy-session-key')
        self.execute(lambda: None,
                     lambda: not self.controller.busy and self.provider.cancelled == ['q-test'])
        self.assertIn('Deadline check', self.controller.status.get())
        self.assertEqual(self.controller.state.read()[0]['deadline_action'], 'queue:q-test')
        # Reconstruct a fresh controller from only the isolated persisted state.
        other = tk.Toplevel(self.root)
        other.withdraw()
        try:
            restarted = lab.App(other)
            self.assertEqual(restarted.records[0]['queue_id'], 'q-test')
            self.assertIn('fixture-job', restarted.list.get(0))
            self.assertEqual(restarted.key.get(), '')
        finally:
            other.destroy()

    def test_console_fingerprint_cleared_when_its_selected_pod_changes(self):
        self.root.deiconify()
        self.root.update()
        self.controller.state.write([{'name': 'first', 'id': 'p1'}, {'name': 'second', 'id': 'p2'}])
        self.controller.refresh()
        self.controller.list.selection_set(0)
        self.set_entry(self.controller.known_hosts, 'SHA256:' + 'A' * 43)
        self.controller.refresh()
        self.assertTrue(self.controller.known_hosts.get().startswith('SHA256:'))
        self.provider.pods = [{'id': 'p1', 'name': 'first',
                               'ssh': {'direct': {'host': 'pod.example', 'port': 2222}}}]
        with patch.object(self.controller, 'background') as background, \
             patch('host_identity.verify_host_key', return_value=Path(self.tmp.name) / 'pin') as verify:
            self.button('Open selected worker').invoke()
            lookup, _ = background.call_args.args
            lookup(self.provider)
            verify.assert_called_once_with('p1', 'pod.example', 2222, 'SHA256:' + 'A' * 43,
                                           self.controller.state.path.parent)
        self.controller.list.selection_clear(0, 'end')
        self.controller.list.selection_set(1)
        self.controller.list.event_generate('<<ListboxSelect>>')
        self.root.update()
        self.assertEqual(self.controller.known_hosts.get(), '')
        self.assertIn('selected pod changed', self.controller.status.get())

    def test_deploy_button_uses_selected_pod_and_safe_profile(self):
        self.set_entry(self.controller.key, 'dummy-session-key')
        known = Path(self.tmp.name) / 'known_hosts'
        known.write_text('fixture')
        self.assertEqual(self.controller.known_hosts.get(), '')
        self.provider.pods = [{'id': 'pod-1', 'name': 'owned', 'status': 'RUNNING',
                               'gpu': {'id': 'NVIDIA GeForce RTX 4090', 'count': 1},
                               'ssh': {'direct': {'host': 'pod.example', 'username': 'root', 'port': 2222}}}]
        self.controller.state.write([{'name': 'owned', 'id': 'pod-1', 'status': 'RUNNING',
                                      'deadline': time.time()+1800, 'compute_kind': 'gpu',
                                      'compute_id': 'NVIDIA GeForce RTX 4090'}])
        self.controller.refresh()
        self.controller.list.selection_set(0)
        self.controller.profile.set('live')
        calls = []
        def deploy(*args, **kwargs):
            calls.append((args, kwargs))
            kwargs['on_output']('PUMP_LAB_MODE=vulkan\nPUMP_LAB_WORKER_SHA256=fixture')
        with patch.object(self.provider, 'get_pod_ssh_fingerprint', create=True, return_value='test-fingerprint') as logs, \
             patch('host_identity.existing_host_key', return_value=None), \
             patch('host_identity.verify_host_key', return_value=known) as verify, \
             patch('deploy.deploy', side_effect=deploy):
            self.execute(self.button('Deploy worker to selected pod').invoke,
                         lambda: not self.controller.busy and not self.controller.deploying
                         and 'Worker build finished' in self.controller.status.get())
        logs.assert_called_once_with('pod-1')
        verify.assert_called_once_with('pod-1', 'pod.example', 2222, 'test-fingerprint',
                                       self.controller.state.path.parent)
        self.assertEqual(len(calls), 1)
        self.assertEqual(calls[0][0][3], str(known))
        self.assertEqual(calls[0][1]['mode'], 'gpu')
        self.assertEqual(calls[0][1]['profile'], 'live')
        self.assertEqual(calls[0][1]['timeout'], 900)
        self.assertIn('PUMP_LAB_MODE=vulkan', self.controller.deployment_receipt.get())
        self.assertNotIn('dummy-session-key', self.controller.deployment_receipt.get())
        self.assertEqual(self.provider.deleted, [])

    def test_live_settings_widget_and_safe_worker_launch(self):
        self.set_entry(self.controller.key, 'dummy-session-key')
        path = Path(self.tmp.name)
        known = path / 'known_hosts'
        known.write_text('test only')
        audio = path / 'audio-helper'
        audio.write_text('')
        audio.chmod(0o700)
        self.provider.pods = [{'id': 'pod-1', 'name': 'owned', 'status': 'RUNNING',
                               'gpu': {'id': 'NVIDIA GeForce RTX 4090', 'count': 1},
                               'ssh': {'direct': {'host': 'pod.example', 'username': 'root', 'port': 2222}}}]
        self.controller.state.write([{'name': 'owned', 'id': 'pod-1', 'status': 'RUNNING',
                                      'deadline': time.time()+300, 'compute_kind': 'gpu',
                                      'compute_id': 'NVIDIA GeForce RTX 4090'}])
        self.controller.refresh()
        self.controller.list.selection_set(0)
        self.set_entry(self.controller.known_hosts, str(known))
        self.controller.profile.set('live')
        self.button('Configure live audio limits').invoke()
        window = next(w for w in self.root.winfo_children() if isinstance(w, tk.Toplevel))
        window.withdraw()
        try:
            self.set_entry(self.controller.audio_binary, str(audio), window)
            with patch.object(lab, 'local_devices', return_value={'test-audio-device — Test interface': 'test-audio-device'}):
                self.execute(self.button('Refresh devices', window).invoke,
                             lambda: not self.controller.enumerating and bool(self.controller.devices))
            device = self.entry_for(self.controller.audio_device, window)
            self.assertIn('test-audio-device — Test interface', device['values'])
            device.set('test-audio-device — Test interface')
            for variable, value in ((self.controller.capture_bytes, '65536'),
                                    (self.controller.playback_bytes, '65536'),
                                    (self.controller.capture_frames, '240000'),
                                    (self.controller.playback_prebuffer, '1024'),
                                    (self.controller.local_utc_uncertainty, '0.1'),
                                    (self.controller.max_clock_offset, '1'),
                                    (self.controller.remote_clock_drift_ppm, '5')):
                self.set_entry(variable, value, window)
            screenshot = os.environ.get('LAB_GUI_DIALOG_XWD')
            if screenshot and os.environ.get('LAB_ISOLATED_X11') == '1':
                window.deiconify()
                self.root.update()
                with open(screenshot, 'wb') as output:
                    subprocess.run(['xwd', '-id', hex(window.winfo_id()), '-silent'],
                                   stdout=output, check=True, timeout=5)
            self.button('Close', window).invoke()
        finally:
            if window.winfo_exists():
                window.destroy()
        observed = {}
        class Bridge:
            url = 'http://127.0.0.1/fixture'
            closed = False
            def close(self): self.closed = True
        def start(command, **kwargs):
            observed.update(command=command, kwargs=kwargs)
            return Bridge()
        browser = types.SimpleNamespace(start=start)
        with patch.dict('sys.modules', {'browser_bridge': browser}), patch.object(lab.webbrowser, 'open', return_value=True):
            self.execute(self.button('Open selected worker').invoke,
                         lambda: not self.controller.busy and self.controller.bridge is not None)
        command = observed['command']
        self.assertTrue(observed['kwargs']['live'])
        self.assertIn('--report-resources', command)
        local = json.loads(command[command.index('--local-json')+1])
        remote = json.loads(command[command.index('--remote-json')+1])
        self.assertIn('test-audio-device', local)
        self.assertIn('--require-gpu', remote[-1])
        self.assertIn('--live', remote[-1])
        self.assertNotIn('dummy-session-key', repr(observed))
        self.assertIn('unqualified', self.controller.status.get())
        bridge = self.controller.bridge
        self.execute(self.button('Close worker session').invoke,
                     lambda: self.controller.bridge is None and not self.controller.closing_bridge)
        self.assertTrue(bridge.closed)
        self.assertIn('pod remains active', self.controller.status.get())
        self.assertEqual(self.provider.deleted, [])
        # An ordinary close releases local resources and allows a fresh worker.
        with patch.dict('sys.modules', {'browser_bridge': browser}), patch.object(lab.webbrowser, 'open', return_value=True):
            self.execute(self.button('Open selected worker').invoke,
                         lambda: not self.controller.busy and self.controller.bridge is not None)


if __name__ == '__main__':
    unittest.main()
