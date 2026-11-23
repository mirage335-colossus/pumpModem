"""Real Tk widget checks with isolated fake provider and no cloud operations.

These tests create their own withdrawn window; a working Tk display is needed.
They never read the user's lab state or access an account, network, or audio.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import types
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs, urlsplit

import app as lab
from runpod import RunPodClient, RunPodError

try:
    import tkinter as tk
    from tkinter import font, messagebox, ttk
except ImportError:
    tk = ttk = messagebox = None


class FakeProvider:
    def __init__(self):
        self.gpus = [{'id': 'NVIDIA GeForce RTX 4090', 'availability': 'HIGH',
                      'price': {'secure': 1.25, 'community': 0.75}}]
        self.multi_gpu = {}
        self.cpus = [{'id': 'test-cpu', 'vcpu': {'min': 2, 'max': 32}, 'ramGbPerVcpu': 4}]
        self.cpus_by_count = {}
        self.calls = []
        self.cpu_counts = []
        self.gpu_tiers = []
        self.gpu_counts = []
        self.pods = []
        self.queued = []
        self.created = []
        self.cancelled = []
        self.deleted = []
        self.create_error = None

    def list_gpus(self, cloud, *, count=1):
        self.calls.append('gpus')
        self.gpu_tiers.append(cloud)
        self.gpu_counts.append(count)
        rows = self.gpus if count == 1 else self.multi_gpu.get(count, [])
        if isinstance(rows, Exception):
            raise rows
        return rows

    def list_cpus(self, count=None):
        self.calls.append('cpus')
        self.cpu_counts.append(count)
        rows = self.cpus_by_count.get(count, self.cpus)
        if isinstance(rows, Exception):
            raise rows
        return rows

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
        for pod in self.pods:
            if pod['id'] == identifier:
                return pod
        raise RunPodError('Pod not found', status=404)

    def get_queued(self, identifier):
        self.calls.append('get-queued')
        for queued in self.queued:
            if queued['id'] == identifier:
                return queued
        raise RunPodError('Queue not found', status=404)


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
        self.controller = lab.App(self.root, repository_directory=self.tmp.name)
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
        self.execute(self.button('Refresh compute').invoke,
                     lambda: not self.controller.busy)

    def run_close(self, action=None):
        errors = []

        def callback_error(_kind, error, _traceback):
            errors.append(error)
            self.root.quit()

        self.root.report_callback_exception = callback_error
        self.root.after(0, action or (lambda: self.root.tk.call(self.root.protocol('WM_DELETE_WINDOW'))))
        watchdog = self.root.after(2000, self.root.quit)
        self.root.bind('<Destroy>', lambda event: self.root.after_cancel(watchdog)
                       if event.widget is self.root else None, add='+')
        self.root.mainloop()
        if errors:
            raise errors[0]
        if not self.controller.window_closed.is_set():
            self.root.after_cancel(watchdog)
            self.fail('The lab window did not close.')

    def test_close_reconciles_legacy_terminating_pod_that_runpod_cannot_see(self):
        self.load_catalog()
        row = {'id': 'gone-pod', 'name': 'owned-gone', 'status': lab.Ownership.TERMINATING,
               'termination_started': time.time(), 'deadline': time.time() + 120}
        self.controller.state.write([row])
        self.controller.refresh()
        with patch.object(messagebox, 'askyesno') as confirm:
            self.run_close()
        self.assertEqual(self.controller.state.read()[0]['status'], lab.Ownership.NOT_VISIBLE)
        self.assertIn('get', self.provider.calls)
        self.assertIn('pods', self.provider.calls)
        self.assertIn('queued', self.provider.calls)
        self.assertEqual(self.provider.deleted, [])
        confirm.assert_not_called()

    def test_close_deadline_does_not_reset_during_a_stalled_runpod_request(self):
        self.load_catalog()
        row = {'id': 'pending-pod', 'name': 'owned-pending', 'status': lab.Ownership.TERMINATING,
               'termination_started': time.time(), 'deadline': time.time() + 120}
        self.controller.state.write([row])
        self.controller.refresh()
        entered, unblock, returned = threading.Event(), threading.Event(), threading.Event()
        observed = []

        def stalled(_identifier):
            entered.set()
            unblock.wait(2)
            raise RuntimeError('Injected provider stall')

        def close_again(deadline):
            observed.append((self.controller.busy, self.controller.exit_after == deadline))
            self.controller.close()
            observed.append((self.controller.busy, self.controller.exit_after == deadline))

        def close():
            self.controller.close()
            deadline = self.controller.exit_after
            self.root.after(80, lambda: close_again(deadline))

        post = self.controller.post_to_window

        def capture_post(callback):
            try:
                return post(callback)
            finally:
                returned.set()

        try:
            with patch.object(self.controller, '_CLOSE_GRACE', .25), \
                 patch.object(self.provider, 'get_pod', side_effect=stalled), \
                 patch.object(self.controller, 'post_to_window', side_effect=capture_post), \
                 patch.object(messagebox, 'askyesno', return_value=True) as confirm:
                self.run_close(close)
                self.assertTrue(entered.is_set())
                self.assertEqual(observed, [(True, True), (True, True)])
                self.assertEqual(self.controller.state.read(), [row])
                confirm.assert_called_once()
                unblock.set()
                self.assertTrue(returned.wait(1))
        finally:
            unblock.set()

    def test_close_offers_to_exit_while_local_bridge_is_still_stopping(self):
        entered, unblock, returned = threading.Event(), threading.Event(), threading.Event()

        def close_bridge():
            entered.set()
            unblock.wait(2)

        post = self.controller.post_to_window

        def capture_post(callback):
            try:
                return post(callback)
            finally:
                returned.set()

        self.controller.bridge = types.SimpleNamespace(close=close_bridge)
        try:
            with patch.object(self.controller, '_CLOSE_GRACE', .2), \
                 patch.object(self.controller, 'post_to_window', side_effect=capture_post), \
                 patch.object(messagebox, 'askyesno', return_value=True) as confirm:
                self.run_close()
                self.assertTrue(entered.is_set())
                confirm.assert_called_once()
                unblock.set()
                self.assertTrue(returned.wait(1))
        finally:
            unblock.set()

    def test_close_during_billing_confirmation_cannot_create_a_pod(self):
        self.load_catalog()

        def confirm(title, _message, **_options):
            self.assertEqual(title, 'Billable RunPod')
            self.controller.close()
            return True

        with patch.object(messagebox, 'askyesno', side_effect=confirm):
            self.run_close(self.button('Start simulation').invoke)
        self.assertEqual(self.provider.created, [])
        self.assertEqual(self.controller.state.read(), [])

    def test_keep_open_does_not_restore_an_interrupted_billing_confirmation(self):
        self.load_catalog()
        close_answered = tk.BooleanVar(self.root, value=False)

        def confirm(title, _message, **_options):
            if title == 'Close RunPod Lab?':
                close_answered.set(True)
                return False
            self.assertEqual(title, 'Billable RunPod')
            self.controller.close()
            self.root.wait_variable(close_answered)
            return True

        with patch.object(messagebox, 'askyesno', side_effect=confirm), \
             patch.object(self.controller, '_CLOSE_GRACE', .05):
            self.execute(self.button('Start simulation').invoke,
                         lambda: close_answered.get() and not self.controller.busy
                         and not self.controller.confirming_create)
        self.assertFalse(self.controller.window_closed.is_set())
        self.assertIsNone(self.controller.exit_after)
        self.assertEqual(self.provider.created, [])

    def test_catalog_403_preserves_other_catalog_and_does_not_leak_key(self):
        self.provider.cpus = RunPodError('dummy-session-key hidden detail', status=403)
        entry = self.set_entry(self.controller.key, 'dummy-session-key')
        self.assertTrue(entry.cget('show'))
        self.execute(self.button('Refresh compute').invoke,
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

    def test_default_window_displays_full_compute_quotes_and_wrapped_errors(self):
        self.provider.gpus = [
            {'id': 'NVIDIA GeForce RTX 5090', 'availability': 'LOW', 'price': {'secure': 1.32}},
            {'id': 'NVIDIA RTX PRO 6000 Blackwell Server Edition', 'availability': 'HIGH',
             'price': {'secure': 2.09}},
            {'id': 'NVIDIA RTX PRO 6000 Blackwell Max-Q Workstation Edition', 'availability': 'LOW',
             'price': {'secure': 2.09}},
        ]
        self.load_catalog()
        self.root.deiconify()
        self.root.update()
        self.assertEqual((self.root.winfo_width(), self.root.winfo_height()), (920, 420))
        self.assertEqual(
            {widget.cget('text') for widget in self.descendants()
             if isinstance(widget, ttk.Button) and widget.winfo_ismapped()},
            {'Start simulation', 'End simulation', 'Advanced', 'Refresh compute'},
        )
        combo = self.controller.dropdown
        refresh = self.button('Refresh compute')
        self.assertEqual(str(refresh.cget('compound')), 'image')
        self.assertGreaterEqual(refresh.winfo_rootx(), combo.winfo_rootx() + combo.winfo_width())
        font_used = font.Font(self.root, font=combo['font'])
        for label in combo['values']:
            with self.subTest(label=label):
                combo.set(label)
                self.assertLessEqual(font_used.measure(label), combo.winfo_width() - 26)
        self.provider.gpus = self.provider.cpus = RunPodError('dummy failure', status=403)
        self.load_catalog()
        status = self.controller.status_label
        self.assertIn('GPU catalog', self.controller.status.get())
        self.assertIn('CPU catalog', self.controller.status.get())
        self.assertGreater(status.winfo_reqheight(), 2 * font_used.metrics('ascent'))
        self.assertEqual(status.winfo_height(), status.winfo_reqheight())
        bottom = self.root.winfo_rooty() + self.root.winfo_height()
        for widget in (status, self.button('Start simulation'), self.button('End simulation'), self.button('Advanced')):
            self.assertLessEqual(widget.winfo_rooty() + widget.winfo_height(), bottom)

    def test_create_rechecks_stock_without_substituting_or_requesting_a_pod(self):
        self.provider.gpus.append({'id': 'RTX PRO 6000 Blackwell Max-Q', 'availability': 'NONE'})
        self.provider.gpus.append({'id': 'NVIDIA GeForce RTX 5090', 'availability': 'HIGH',
                                   'price': {'secure': 1.32}, 'maxCount': {'secure': 1}})
        self.load_catalog()
        self.assertIn('RTX 5090 · stock high', self.controller.compute.get())
        self.controller.compute_selected()
        self.provider.gpus[-1]['availability'] = 'NONE'
        with patch.object(messagebox, 'askyesno') as ask:
            self.execute(self.button('Start simulation').invoke, lambda: not self.controller.busy)
            ask.assert_not_called()
        self.assertIn('RTX 5090 · stock none', self.controller.status.get())
        self.assertIn('Secure Cloud', self.controller.status.get())
        self.assertEqual(self.provider.calls, ['gpus', 'cpus', 'cpus', 'gpus'])
        self.assertEqual(self.provider.gpu_tiers, ['SECURE', 'SECURE'])
        self.assertEqual(self.controller.state.read(), [])
        self.load_catalog()
        self.assertEqual(self.controller.compute.get(), '')
        self.assertFalse(any('stock none' in label for label in self.controller.dropdown['values']))
        self.assertTrue(any('RTX 4090' in label for label in self.controller.dropdown['values']))
        self.provider.gpus.pop()
        self.load_catalog()
        self.assertEqual(self.controller.compute.get(), '')
        self.assertIn('Choose a compute option', self.controller.status.get())

    def test_explicit_tier_change_refresh_and_paid_confirmation(self):
        self.provider.gpus.append({'id': 'NVIDIA GeForce RTX 5090', 'availability': 'HIGH',
                                   'maxCount': {'secure': 1, 'community': 1}})
        self.load_catalog()
        self.assertEqual(self.controller.cloud.get(), 'SECURE')
        chosen = next(label for label in self.controller.dropdown['values'] if 'RTX 4090' in label)
        self.controller.dropdown.set(chosen)
        self.controller.compute_selected()
        self.entry_for(self.controller.cloud).set('COMMUNITY')
        self.assertFalse(self.controller.choices)
        self.assertFalse(self.controller.compute.get())
        self.button('Start simulation').invoke()
        self.assertEqual(self.provider.calls, ['gpus', 'cpus', 'cpus'])
        self.execute(lambda: None, lambda: not self.controller.busy and bool(self.controller.choices))
        self.assertEqual(self.provider.calls, ['gpus', 'cpus', 'cpus', 'gpus'])
        self.assertEqual(self.provider.cpu_counts, [None, 32])
        self.assertFalse(any('CPU:' in label for label in self.controller.dropdown['values']))
        self.assertIn('RTX 4090', self.controller.compute.get())
        self.provider.gpus[0].update(availability='MEDIUM', price={'community': 0.81})
        self.load_catalog()
        self.assertIn('RTX 4090 · stock medium · $0.81/hr', self.controller.compute.get())
        self.provider.gpus[0]['price']['community'] = 0.84
        with patch.object(messagebox, 'askyesno', return_value=True) as ask:
            self.execute(self.button('Start simulation').invoke,
                         lambda: not self.controller.busy and self.controller.setting_up)
        prompt = ask.call_args.args[1]
        self.assertIn('RTX 4090 · stock medium · $0.84/hr on Community Cloud', prompt)
        self.assertIn(lab.IMAGE, prompt)
        self.assertIn('public IP', prompt)
        self.assertIn('catalog compute list price', prompt)
        self.assertEqual(self.provider.gpu_tiers, ['SECURE', 'COMMUNITY', 'COMMUNITY', 'COMMUNITY'])
        self.assertEqual(len(self.provider.created), 1)
        self.assertEqual(self.provider.created[0]['gpu'], {'id': 'NVIDIA GeForce RTX 4090', 'count': 1})
        self.assertEqual(self.provider.created[0]['cloud'], 'COMMUNITY')
        self.assertEqual(self.controller.state.read()[0]['cloud'], 'COMMUNITY')

    def test_default_cpu_selection_prefers_cpu5_and_creates_with_36_hour_deadline(self):
        cpu5 = {'id': 'cpu5g', 'vcpu': {'min': 2, 'max': 32}, 'ramGbPerVcpu': 4,
                'price': {'securePerVcpu': 0.10}}
        cpu3 = {'id': 'cpu3g', 'vcpu': {'min': 2, 'max': 64}, 'ramGbPerVcpu': 4,
                'price': {'securePerVcpu': 0.0625}}
        self.provider.cpus = [cpu3, cpu5]
        self.provider.cpus_by_count = {
            32: [dict(cpu3, availability='HIGH'), dict(cpu5, availability='LOW')],
            64: [dict(cpu3, availability='LOW')],
        }
        self.load_catalog()
        self.assertIsNone(self.provider.cpu_counts[0])
        self.assertCountEqual(self.provider.cpu_counts[1:], [32, 64])
        cpu_label = self.controller.compute.get()
        self.assertIn('CPU: cpu5g, 32 vCPU / 128 GB · stock low · $3.2/hr', cpu_label)
        self.assertTrue(any('CPU: cpu3g, 64 vCPU' in label for label in self.controller.dropdown['values']))
        self.assertEqual(self.controller.deadline_minutes.get(), '2160')
        gpu_label = next(label for label in self.controller.dropdown['values'] if 'RTX 4090' in label)
        self.controller.dropdown.set(gpu_label)
        self.controller.compute_selected()
        self.load_catalog()
        self.assertEqual(self.controller.compute.get(), gpu_label)

        self.controller.dropdown.set(cpu_label)
        self.controller.compute_selected()
        self.provider.cpus_by_count[32][1]['price'] = {'securePerVcpu': 0.125}
        started = time.time()
        with patch.object(messagebox, 'askyesno', return_value=True) as ask:
            self.execute(self.button('Start simulation').invoke,
                         lambda: not self.controller.busy and self.controller.setting_up)
        self.assertEqual(self.provider.cpu_counts[-1], 32)
        self.assertIn('CPU: cpu5g, 32 vCPU / 128 GB · stock low · $4/hr on Secure Cloud',
                      ask.call_args.args[1])
        self.assertIn('36-hour deadline', ask.call_args.args[1])
        self.assertEqual(len(self.provider.created), 1)
        self.assertEqual(self.provider.created[0]['cpu'], {'id': 'cpu5g', 'vcpuCount': 32})
        record = self.controller.state.read()[0]
        self.assertEqual(record['requested_vcpu'], 32)
        self.assertGreaterEqual(record['deadline'], started + 129600)
        self.assertLess(record['deadline'], time.time() + 129600)

    def test_refresh_updates_default_until_user_selects_and_creates_exact_count(self):
        pro = {'id': lab.RTX_PRO_6000, 'availability': 'HIGH', 'price': {'secure': 2.09},
               'maxCount': {'secure': 2}}
        self.provider.gpus.append(pro)
        self.load_catalog()
        self.assertIn('Server Edition · stock high', self.controller.compute.get())
        self.provider.multi_gpu[2] = [dict(pro, availability='LOW')]
        self.load_catalog()
        self.assertIn('Server Edition x2 · stock low · $4.18/hr', self.controller.compute.get())
        self.root.deiconify()
        self.root.update()
        self.controller.dropdown.set(next(label for label in self.controller.dropdown['values']
                                          if 'RTX 4090' in label))
        self.controller.dropdown.event_generate('<<ComboboxSelected>>')
        self.provider.gpus[0]['price']['secure'] = .99
        self.load_catalog()
        self.assertIn('RTX 4090 · stock high · $0.99/hr', self.controller.compute.get())
        self.controller.dropdown.set(next(label for label in self.controller.dropdown['values'] if 'x2' in label))
        self.controller.dropdown.event_generate('<<ComboboxSelected>>')
        with patch.object(messagebox, 'askyesno', return_value=True) as ask:
            self.execute(self.button('Start simulation').invoke,
                         lambda: not self.controller.busy and self.controller.setting_up)
        self.assertIn('x2 · stock low · $4.18/hr', ask.call_args.args[1])
        self.assertIn('receiver currently uses one GPU per session', ask.call_args.args[1])
        self.assertEqual(self.provider.gpu_counts[-1], 2)
        self.assertEqual(self.provider.created[0]['gpu'], {'id': pro['id'], 'count': 2})
        self.assertEqual(self.controller.state.read()[0]['requested_gpu_count'], 2)

    def test_deadline_during_paid_confirmation_preserves_expiry_and_reports_unsent_create(self):
        self.load_catalog()
        def confirm(*_):
            self.controller.state.write([{'id': 'expired-1', 'name': 'prior-fixture',
                                          'deadline': time.time() - 1}])
            self.provider.pods = [{'id': 'expired-1', 'name': 'prior-fixture', 'status': 'RUNNING'}]
            self.controller.enforce_deadlines()
            self.assertTrue(self.controller.busy)
            return True
        with patch.object(messagebox, 'askyesno', side_effect=confirm):
            self.execute(self.button('Start simulation').invoke, lambda: not self.controller.busy)
        self.assertEqual(self.provider.deleted, ['expired-1'])
        self.assertEqual(self.provider.created, [])
        self.assertIn('Pod creation was interrupted', self.controller.status.get())
        self.assertIn('Terminating', self.controller.list.get(0))

    def test_key_edit_during_paid_confirmation_cancels_create(self):
        self.load_catalog()
        def confirm(*_):
            self.controller.key.set('dummy-replacement-key')
            return True
        with patch.object(messagebox, 'askyesno', side_effect=confirm):
            self.execute(self.button('Start simulation').invoke, lambda: not self.controller.busy)
        self.assertEqual(self.provider.created, [])
        self.assertIn('settings changed', self.controller.status.get())

    def test_edit_during_preflight_cancels_paid_confirmation(self):
        self.load_catalog()
        callbacks = []
        with patch.object(self.controller, 'background',
                          side_effect=lambda fn, done, _failed: callbacks.append((fn, done))):
            self.button('Start simulation').invoke()
            fetch, finish = callbacks.pop()
            quote = fetch(self.provider)
            self.set_entry(self.controller.image, 'different/image')
            with patch.object(messagebox, 'askyesno') as ask:
                finish(quote)
                ask.assert_not_called()
        self.assertIn('settings changed', self.controller.status.get())
        self.assertFalse(callbacks)
        self.assertEqual(self.controller.state.read(), [])

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
            if urlsplit(url).path.endswith('/catalog/gpus'):
                return 403, b'{"detail":"untrusted dummy-session-key"}'
            if urlsplit(url).path.endswith('/catalog/cpus'):
                return 200, json.dumps({'cpus': self.provider.cpus}).encode()
            raise AssertionError('unexpected fake HTTP request')
        client = RunPodClient('dummy-session-key', transport=transport)
        self.set_entry(self.controller.key, 'dummy-session-key')
        with patch.object(lab, 'RunPodClient', return_value=client):
            self.execute(self.button('Refresh compute').invoke,
                         lambda: not self.controller.busy and 'Catalog' in self.controller.status.get())
        self.assertEqual([urlsplit(row[1]).path.rsplit('/', 1)[-1] for row in requests],
                         ['gpus', 'cpus', 'cpus'])
        self.assertFalse(urlsplit(requests[1][1]).query)
        self.assertEqual(parse_qs(urlsplit(requests[2][1]).query),
                         {'include': ['AVAILABILITY'], 'product': ['POD'], 'vcpuCount': ['32']})
        self.assertIn('CPU:', self.controller.compute.get())
        self.assertIn('GPU catalog', self.controller.status.get())
        self.assertIn('403', self.controller.status.get())
        self.assertNotIn('dummy-session-key', self.controller.status.get())

    def test_stock_positive_then_http_400_displays_placement_error_and_clears_intent(self):
        requests = []
        def transport(method, url, headers, body, timeout):
            path = urlsplit(url).path
            requests.append((method, path))
            if (method, path) == ('GET', '/v2/catalog/gpus'):
                return 200, json.dumps({'gpus': [{'id': 'NVIDIA GeForce RTX 5090', 'availability': 'LOW',
                                                'price': {'secure': 1.32},
                                                'maxCount': {'secure': 1}}]}).encode()
            if (method, path) == ('GET', '/v2/catalog/cpus'):
                return 200, b'{"cpus":[]}'
            if (method, path) == ('POST', '/v2/pods'):
                payload = json.loads(body)
                self.assertEqual(payload['gpu'], {'id': 'NVIDIA GeForce RTX 5090', 'count': 1})
                self.assertEqual(payload['cloud'], 'SECURE')
                return 400, b'{"status":400,"detail":"No available instances. private-provider-detail"}'
            raise AssertionError('unexpected fake HTTP request')
        client = RunPodClient('dummy-session-key', transport=transport)
        with patch.object(lab, 'RunPodClient', return_value=client):
            self.load_catalog()
            with patch.object(messagebox, 'askyesno', return_value=True) as ask:
                self.execute(self.button('Start simulation').invoke, lambda: not self.controller.busy)
        self.assertIn('stock low', ask.call_args.args[1])
        self.assertEqual(requests, [('GET', '/v2/catalog/gpus'), ('GET', '/v2/catalog/cpus'),
                                    ('GET', '/v2/catalog/gpus'), ('POST', '/v2/pods')])
        status = self.controller.status.get()
        self.assertIn('POST /v2/pods', status)
        self.assertIn('HTTP 400', status)
        self.assertIn('Reload the catalog', status)
        self.assertNotIn('private-provider-detail', status)
        self.assertNotIn('dummy-session-key', status)
        self.assertEqual(self.controller.state.read(), [])

    def test_deadline_validation_cancel_create_and_queue_cancel(self):
        self.load_catalog()
        create = self.button('Start simulation')
        self.set_entry(self.controller.deadline_minutes, '0')
        self.execute(create.invoke, lambda: 'Deadline must' in self.controller.status.get())
        self.assertNotIn('create', self.provider.calls)
        self.set_entry(self.controller.deadline_minutes, '5')
        with patch.object(messagebox, 'askyesno', return_value=False):
            self.execute(create.invoke, lambda: not self.controller.busy)
        self.assertNotIn('create', self.provider.calls)
        with patch.object(messagebox, 'askyesno', return_value=True):
            self.execute(create.invoke, lambda: not self.controller.busy and self.controller.setting_up)
        self.assertEqual(len(self.provider.created), 1)
        saved = self.controller.state.read()
        self.assertEqual(saved[0]['queue_id'], 'queue-1')
        self.assertNotIn('id', saved[0])
        self.assertNotIn('dummy-session-key', self.controller.state.path.read_text())
        self.controller.list.selection_set(0)
        with patch.object(messagebox, 'askyesno', return_value=True):
            self.execute(self.button('End simulation').invoke,
                         lambda: not self.controller.busy and 'Terminating' in self.controller.status.get())
        self.assertEqual(self.provider.cancelled, ['queue-1'])
        self.assertEqual(self.provider.deleted, [])

    def test_create_403_is_visible_and_does_not_leave_pending_job(self):
        self.load_catalog()
        self.provider.create_error = RunPodError(
            'POST /v2/pods: request failed (HTTP 403); check the pods:write scope and account or team access',
            status=403, operation='POST /v2/pods', required_scope='pods:write')
        with patch.object(messagebox, 'askyesno', return_value=True):
            self.execute(self.button('Start simulation').invoke,
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
        self.set_entry(self.controller.key, 'dummy-session-key')
        self.execute(self.controller.enforce_deadlines,
                     lambda: not self.controller.busy and self.provider.cancelled == ['q-test'])
        self.assertIn('Terminating', self.controller.list.get(0))
        # Reconstruct a fresh controller from only the isolated persisted state.
        other = tk.Toplevel(self.root)
        other.withdraw()
        try:
            restarted = lab.App(other, repository_directory=self.tmp.name)
            self.assertEqual(restarted.records[0]['queue_id'], 'q-test')
            self.assertIn('fixture-job', restarted.list.get(0))
            self.assertEqual(restarted.key.get(), 'dummy-session-key')
            self.execute(lambda: None, lambda: bool(restarted.choices) and not restarted.busy)
            self.assertEqual(self.provider.cancelled, ['q-test'])
        finally:
            other.destroy()

    def test_closing_cancels_a_pending_catalog_refresh(self):
        self.controller.key.set('dummy-session-key')
        self.controller.tier_changed()
        scheduled = self.controller.catalog_after
        self.assertIsNotNone(scheduled)
        self.controller.close()
        self.assertTrue(self.controller.window_closed.is_set())
        self.assertNotIn(scheduled, self.root.tk.call('after', 'info'))
        self.assertEqual(self.provider.calls, [])

    def test_saved_key_loads_catalog_and_clear_removes_file(self):
        self.set_entry(self.controller.key, 'saved-fixture-key')
        self.assertTrue(self.controller.save_key())
        path = self.controller.key_store.path
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)
        other = tk.Toplevel(self.root)
        other.withdraw()
        try:
            restarted = lab.App(other, repository_directory=self.tmp.name)
            self.assertEqual(restarted.key.get(), 'saved-fixture-key')
            self.execute(lambda: None, lambda: bool(restarted.choices) and not restarted.busy)
            self.assertIn('RTX 4090', restarted.compute.get())
            restarted.clear_key()
            self.assertEqual(restarted.key.get(), '')
            self.assertFalse(path.exists())
            self.root.update_idletasks()
        finally:
            other.destroy()

    def test_saved_key_loads_and_selects_compute_before_reconciling_old_records(self):
        self.set_entry(self.controller.key, 'saved-fixture-key')
        self.assertTrue(self.controller.save_key())
        self.controller.state.write([{'name': 'stale-fixture', 'status': 'RUNNING',
                                      'deadline': time.time() + 3600}])
        other = tk.Toplevel(self.root)
        other.withdraw()
        try:
            restarted = lab.App(other, repository_directory=self.tmp.name)
            reconciliation = []
            with patch.object(restarted, 'poll_owned', side_effect=lambda **kwargs: reconciliation.append(kwargs)):
                self.execute(lambda: None,
                             lambda: bool(restarted.choices) and bool(restarted.compute.get()) and not restarted.busy)
            self.assertEqual(restarted.compute.get(), 'GPU: ' + self.provider.gpus[-1]['id'] +
                             ' · stock high · $1.25/hr')
            self.assertEqual(len(reconciliation), 1)
            self.assertTrue(reconciliation[0]['reconcile'])
            self.assertFalse(restarted.busy)
            self.assertEqual(restarted.key.get(), 'saved-fixture-key')
        finally:
            other.destroy()

    def test_start_simulation_creates_deploys_and_opens_with_generated_trust(self):
        self.load_catalog()
        def create(payload):
            self.provider.created.append(payload)
            pod = {'id': 'auto-pod', 'name': payload['name'], 'status': 'RUNNING',
                   'gpu': payload['gpu'], 'cloud': payload['cloud'],
                   'ssh': {'direct': {'host': 'pod.example', 'port': 2222, 'username': 'root'}}}
            self.provider.pods = [pod]
            return 201, pod
        pin = Path(self.tmp.name) / 'known_hosts'
        pin.write_text('fixture')
        observed = {'state': 'active', 'closed': False}
        def close_bridge():
            observed['closed'] = True
        bridge = types.SimpleNamespace(url='http://127.0.0.1:1/#fixture', close=close_bridge,
                                       status=lambda: (observed['state'], ''))
        with patch.object(self.provider, 'create_pod', side_effect=create), \
             patch.object(messagebox, 'askyesno', return_value=True) as confirm, \
             patch('host_identity.existing_host_key', return_value=None), \
             patch('host_identity.verify_host_key', return_value=pin) as verify, \
             patch.object(self.provider, 'get_pod_ssh_fingerprint', create=True,
                          side_effect=RunPodError('logs denied', status=403)) as logs, \
             patch.object(lab, 'deploy_selected', return_value='PUMP_LAB_MODE=vulkan') as deploy, \
             patch('browser_bridge.start', return_value=bridge) as start, \
             patch.object(lab.webbrowser, 'open', return_value=True) as browser:
            self.execute(self.button('Start simulation').invoke,
                         lambda: self.controller.bridge is bridge and not self.controller.setting_up)
            self.button('Start simulation').invoke()
        confirm.assert_called_once()
        self.assertEqual(len(self.provider.created), 1)
        record = self.controller.state.read()[0]
        self.assertEqual(record['deployments']['simulation'], 'PUMP_LAB_MODE=vulkan')
        self.assertTrue(record['ssh_fingerprint'].startswith('SHA256:'))
        self.assertIn('PUMP_LAB_HOST_KEY_B64', self.provider.created[0]['env'])
        verify.assert_called_once_with('auto-pod', 'pod.example', 2222,
                                       record['ssh_fingerprint'], self.controller.state.path.parent)
        self.assertEqual(deploy.call_args.args[3], record['ssh_identity'])
        self.assertIn(record['ssh_identity'], start.call_args.args[0])
        logs.assert_not_called()
        self.assertEqual(browser.call_count, 1)
        browser.assert_called_with(bridge.url)

        # Closing the browser must end the attached pod, even if another row
        # has been selected while the simulation was open.
        other = {'id': 'other-pod', 'name': 'other-fixture', 'status': 'RUNNING',
                 'deadline': time.time() + 120}
        self.controller.state.write([record, other])
        self.provider.pods.append(other)
        self.controller.select_record(other)
        self.controller.clear_key()
        self.assertEqual(self.controller.key.get(), 'dummy-session-key')
        observed['state'] = 'ended'
        self.execute(lambda: None, lambda: not self.controller.busy and observed['closed']
                     and self.provider.deleted == ['auto-pod'])
        self.assertTrue(self.controller.list.get(0).startswith('Terminating'))
        self.provider.pods.pop(0)
        self.execute(self.controller.enforce_deadlines, lambda: not self.controller.busy
                     and self.controller.list.size() == 1 and 'Pod terminated.' in self.controller.status.get())
        self.assertEqual(self.provider.pods, [other])
        self.assertEqual(self.controller.list.curselection(), (0,))
        self.assertEqual(self.controller.selected_record()['id'], 'other-pod')

    def test_new_pod_is_terminated_when_native_worker_cannot_open(self):
        self.load_catalog()
        def create(payload):
            pod = {'id': 'failed-pod', 'name': payload['name'], 'status': 'RUNNING',
                   'gpu': payload['gpu'], 'cloud': payload['cloud'],
                   'ssh': {'direct': {'host': 'pod.example', 'port': 2222, 'username': 'root'}}}
            self.provider.pods = [pod]
            return 201, pod
        pin = Path(self.tmp.name) / 'known_hosts'
        pin.write_text('fixture')
        with patch.object(self.provider, 'create_pod', side_effect=create), \
             patch.object(messagebox, 'askyesno', return_value=True), \
             patch('host_identity.existing_host_key', return_value=pin), \
             patch.object(lab, 'deploy_selected', return_value='PUMP_LAB_MODE=vulkan'), \
             patch('browser_bridge.start', side_effect=RuntimeError('The native worker disconnected.')), \
             patch.object(lab.webbrowser, 'open') as browser:
            self.execute(self.button('Start simulation').invoke, lambda: not self.controller.busy
                         and self.provider.deleted == ['failed-pod'])
        browser.assert_not_called()
        self.assertTrue(self.controller.list.get(0).startswith('Terminating'))
        self.provider.pods = []
        self.execute(self.controller.enforce_deadlines, lambda: not self.controller.busy
                     and self.controller.list.size() == 0 and 'Pod terminated.' in self.controller.status.get())
        self.assertIn('The native worker disconnected.', self.controller.status.get())
        self.assertIn('Pod terminated.', self.controller.status.get())

    def test_end_during_create_keeps_intent_and_terminates_late_result(self):
        self.load_catalog()
        entered, release = threading.Event(), threading.Event()

        def create(payload):
            entered.set()
            self.assertTrue(release.wait(3))
            pod = {'id': 'late-pod', 'name': payload['name'], 'status': 'RUNNING'}
            self.provider.pods = [pod]
            return 201, pod

        try:
            with patch.object(self.provider, 'create_pod', side_effect=create), \
                 patch.object(messagebox, 'askyesno', return_value=True) as confirm, \
                 patch.object(lab, 'deploy_selected') as deploy, patch.object(lab.webbrowser, 'open') as browser:
                self.execute(self.button('Start simulation').invoke, entered.is_set)
                name = self.controller.state.read()[0]['name']
                self.button('End simulation').invoke()
                self.assertTrue(self.controller.busy)
                self.assertEqual(self.controller.state.queued_ends(), {name})
                self.assertTrue(self.controller.list.get(0).startswith('Terminating'))
                self.assertIn('Ending simulation', self.controller.status.get())
                release.set()
                self.execute(lambda: None, lambda: not self.controller.busy
                             and self.provider.deleted == ['late-pod'])
            confirm.assert_called_once()
            deploy.assert_not_called()
            browser.assert_not_called()
            self.assertEqual(self.controller.state.queued_ends(), set())
        finally:
            release.set()

    def test_end_cancels_deployment_and_does_not_open_late_worker(self):
        self.load_catalog()
        pin = Path(self.tmp.name) / 'known_hosts'
        pin.write_text('fixture')
        gpu = 'NVIDIA GeForce RTX 4090'
        record = {'id': 'compiling-pod', 'name': 'owned-compiling', 'status': 'RUNNING',
                  'deadline': time.time() + 120, 'cloud': 'SECURE', 'compute_kind': 'gpu',
                  'compute_id': gpu}
        self.controller.state.write([record])
        self.controller.select_record(record)
        self.provider.pods = [
            dict(record, gpu={'id': gpu, 'count': 1},
                 ssh={'direct': {'host': 'pod.example', 'port': 2222, 'username': 'root'}}),
        ]
        cancelled_in_build = threading.Event()

        def deploy(*_args, cancelled, on_progress, **_kwargs):
            on_progress('Compiling the worker…')
            self.assertTrue(cancelled.wait(3))
            cancelled_in_build.set()
            return 'PUMP_LAB_MODE=vulkan'

        with patch('host_identity.existing_host_key', return_value=pin), \
             patch.object(lab, 'deploy_selected', side_effect=deploy), \
             patch('browser_bridge.start') as start, patch.object(lab.webbrowser, 'open') as browser:
            self.execute(self.button('Start simulation').invoke,
                         lambda: 'Compiling the worker… (0:' in self.controller.status.get())
            self.execute(self.button('End simulation').invoke,
                         lambda: cancelled_in_build.is_set() and not self.controller.deploying
                         and not self.controller.busy and self.provider.deleted == ['compiling-pod'])
            start.assert_not_called()
            browser.assert_not_called()

    def test_stale_history_does_not_hide_active_pod_from_end(self):
        self.load_catalog()
        stale = {'id': 'gone', 'name': 'previous', 'status': lab.Ownership.NOT_VISIBLE}
        finished = {'id': 'ended', 'name': 'finished', 'status': lab.Ownership.TERMINATED}
        active = {'id': 'active', 'name': 'running', 'status': 'RUNNING'}
        self.controller.state.write([stale, finished, active])
        self.provider.pods = [active]
        self.controller.refresh()
        self.assertEqual(self.controller.list.size(), 1)
        self.assertEqual(self.controller.list.curselection(), ())
        with patch.object(messagebox, 'askyesno', return_value=True):
            self.execute(self.button('End simulation').invoke,
                         lambda: self.provider.deleted == ['active'] and not self.controller.busy)
        self.assertEqual(self.controller.state.read()[0], stale)
        self.assertFalse(any(row['id'] == 'ended' for row in self.controller.state.read()))

    def test_setup_failure_can_retry_same_owned_pod(self):
        self.load_catalog()
        record = {'id': 'retry-pod', 'name': 'owned-retry', 'deadline': time.time() + 120,
                  'cloud': 'SECURE', 'compute_kind': 'gpu', 'compute_id': 'NVIDIA GeForce RTX 4090'}
        self.controller.state.write([record])
        self.controller.select_record(record)
        pod = dict(record, status='RUNNING', gpu={'id': record['compute_id'], 'count': 1},
                   ssh={'direct': {'host': 'pod.example', 'port': 2222, 'username': 'root'}})
        self.provider.pods = [pod]
        pin = Path(self.tmp.name) / 'known_hosts'
        pin.write_text('fixture')
        bridge = types.SimpleNamespace(url='http://127.0.0.1:1/#fixture', close=lambda: None, status=lambda: ('active', ''))
        with patch('host_identity.existing_host_key', return_value=pin), \
             patch.object(lab, 'deploy_selected', side_effect=[ValueError('interrupted build'), 'receipt']) as deploy, \
             patch('browser_bridge.start', return_value=bridge), \
             patch.object(lab.webbrowser, 'open', return_value=True), patch.object(messagebox, 'askyesno') as confirm:
            self.execute(self.button('Start simulation').invoke,
                         lambda: not self.controller.setting_up and 'Setup stopped' in self.controller.status.get())
            self.assertIn('interrupted build', self.controller.status.get())
            self.execute(self.button('Start simulation').invoke,
                         lambda: self.controller.bridge is bridge and not self.controller.setting_up)
        self.assertEqual(deploy.call_count, 2)
        confirm.assert_not_called()
        self.assertFalse(self.provider.created)

    def test_dismiss_missing_request_rechecks_provider_before_clearing_local_record(self):
        self.load_catalog()
        record = {'name': 'uncertain-request', 'status': lab.Ownership.TERMINATION_FAILED}
        self.controller.state.write([record])
        self.controller.select_record(record)
        self.provider.queued = [{'name': record['name'], 'id': 'late-queue'}]
        dismiss = self.button('Dismiss missing request')
        with patch.object(messagebox, 'askyesno', return_value=True) as confirm:
            self.execute(dismiss.invoke, lambda: not self.controller.busy)
            self.assertIn('RunPod still lists this request', self.controller.status.get())
            self.assertEqual(self.controller.state.read(), [record])
            self.provider.queued = []
            self.execute(dismiss.invoke, lambda: not self.controller.busy)
        self.assertEqual(confirm.call_count, 2)
        self.assertEqual(self.controller.state.read(), [])
        self.assertIn('Missing request dismissed', self.controller.status.get())
        self.assertEqual(self.provider.deleted, [])
        self.assertEqual(self.provider.cancelled, [])

    def test_console_fingerprint_cleared_when_its_selected_pod_changes(self):
        self.root.deiconify()
        self.root.update()
        self.controller.state.write([{'name': 'first', 'id': 'p1'}, {'name': 'second', 'id': 'p2'}])
        self.controller.refresh()
        self.controller.list.selection_set(0)
        self.set_entry(self.controller.known_hosts, 'SHA256:' + 'A' * 43)
        self.controller.refresh()
        self.assertTrue(self.controller.known_hosts.get().startswith('SHA256:'))
        self.controller.list.selection_clear(0, 'end')
        self.controller.list.selection_set(1)
        self.controller.list.event_generate('<<ListboxSelect>>')
        self.root.update()
        self.assertEqual(self.controller.known_hosts.get(), '')
        self.assertIn('selected pod changed', self.controller.status.get())

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
            def status(self): return 'active', ''
        def start(command, **kwargs):
            observed.update(command=command, kwargs=kwargs)
            return Bridge()
        browser = types.SimpleNamespace(start=start)
        with patch.dict('sys.modules', {'browser_bridge': browser}), patch.object(lab.webbrowser, 'open', return_value=True), \
             patch.object(lab, 'deploy_selected', return_value='PUMP_LAB_MODE=vulkan'):
            self.execute(self.button('Start simulation').invoke,
                         lambda: not self.controller.busy and self.controller.bridge is not None)
        command = observed['command']
        self.assertTrue(observed['kwargs']['live'])
        self.assertIn('--report-resources', command)
        local = json.loads(command[command.index('--local-json')+1])
        remote = json.loads(command[command.index('--remote-json')+1])
        self.assertIn('test-audio-device', local)
        self.assertNotIn('--require-gpu', remote[-1])
        self.assertIn('--prefer-gpu', remote[-1])
        self.assertIn('--live', remote[-1])
        self.assertNotIn('dummy-session-key', repr(observed))
        self.assertIn('Experimental Live/Duplex opened', self.controller.status.get())
        self.assertTrue(observed['kwargs']['managed'])
        bridge = self.controller.bridge
        self.execute(self.button('End simulation').invoke,
                     lambda: self.controller.bridge is None and not self.controller.busy
                     and self.provider.deleted == ['pod-1'])
        self.assertTrue(bridge.closed)
        self.assertIn('Terminating', self.controller.list.get(0))
        self.provider.pods = []
        self.execute(self.controller.enforce_deadlines,
                     lambda: not self.controller.busy and self.controller.list.size() == 0
                     and 'Pod terminated.' in self.controller.status.get())
        self.assertIn('Pod terminated', self.controller.status.get())
        self.assertEqual(self.provider.deleted, ['pod-1'])


if __name__ == '__main__':
    unittest.main()
