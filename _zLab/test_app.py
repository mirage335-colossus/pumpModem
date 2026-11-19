import tempfile
import time
import unittest
from unittest.mock import patch
from pathlib import Path

import app as app_module
from app import App, options, Ownership, State, ssh_command, simulation_gpu, owned_pod_details, file_root, deploy_selected, clock_bounds, live_command, local_devices, device_query
from runpod import RunPodError


class Fake:
    def __init__(self, result=None, error=None):
        self.result, self.error, self.pods, self.deleted = result, error, [], []
    def create_pod(self, payload):
        self.payload = payload
        if self.error:
            raise self.error
        return self.result(payload)
    def list_pods(self):
        return self.pods
    def list_queued(self):
        return getattr(self, 'queued', [])
    def cancel_queued(self, queue_id):
        self.deleted.append('queue:'+queue_id)
    def delete_pod(self, pod_id):
        self.deleted.append(pod_id)


class Tests(unittest.TestCase):
    def test_owned_pod_connection_details_use_exact_individual_response(self):
        record = {'id': 'p1', 'name': 'owned'}
        detail = {'id': 'p1', 'name': 'owned', 'ssh': {'direct': {'host': 'pod.example'}}}
        class Client:
            def __init__(self): self.detail, self.requests = detail, []
            def list_pods(self): return [{'id': 'p1', 'name': 'owned', 'ssh': None}]
            def get_pod(self, identity):
                self.requests.append(identity)
                return self.detail
        client = Client()
        self.assertIsNone(client.list_pods()[0]['ssh'])
        self.assertIs(owned_pod_details(client, record), detail)
        self.assertEqual(client.requests, ['p1'])
        for bad in ({'id': 'other', 'name': 'owned'}, {'id': 'p1', 'name': 'other'}, None):
            with self.subTest(bad=bad), self.assertRaisesRegex(ValueError, 'do not match'):
                client.detail = bad
                owned_pod_details(client, record)
        for incomplete in ({'name': 'owned'}, {'id': 'p1'}):
            with self.subTest(incomplete=incomplete), self.assertRaisesRegex(ValueError, 'missing'):
                owned_pod_details(client, incomplete)

    def test_connection_actions_fetch_selected_individual_pod(self):
        class Var:
            def get(self): return ''
            def set(self, _): pass
        class List:
            def curselection(self): return (0,)
        class Client:
            def __init__(self): self.requests = []
            def get_pod(self, pod_id):
                self.requests.append(pod_id)
                return {'id': pod_id, 'name': 'owned', 'ssh': {'direct': {}}}
        controller = App.__new__(App)
        controller.list = List()
        controller.records = [{'id': 'p1', 'name': 'owned', 'deadline': time.time() + 60}]
        controller.deploying = controller.closing_bridge = False
        controller.bridge = None
        controller.devices = {}
        for field in ('known_hosts', 'identity', 'profile', 'key', 'files', 'audio_binary',
                      'audio_device', 'capture_bytes', 'playback_bytes', 'capture_frames',
                      'playback_prebuffer', 'local_utc_uncertainty', 'max_clock_offset',
                      'remote_clock_drift_ppm', 'status'):
            setattr(controller, field, Var())
        actions = []
        controller.background = lambda request, done: actions.append((request, done))
        controller.deploy_worker()
        controller.open_simulation()
        controller.details()
        self.assertEqual(len(actions), 3)
        client = Client()
        for request, _ in actions:
            self.assertEqual(request(client)['id'], 'p1')
        self.assertEqual(client.requests, ['p1', 'p1', 'p1'])

    def test_copy_status_copies_only_visible_message(self):
        class Root:
            def __init__(self): self.clipboard = 'old'
            def clipboard_clear(self): self.clipboard = ''
            def clipboard_append(self, value): self.clipboard += value
        class Status:
            def __init__(self, value): self.value = value
            def get(self): return self.value
            def set(self, value): self.value = value
        controller = app_module.App.__new__(app_module.App)
        controller.root, controller.status = Root(), Status('GPU catalog: HTTP 403\nCheck permissions.')
        # Other form values must never be copied along with the status.
        controller.key = Status('private-api-key')
        controller.copy_status()
        self.assertEqual(controller.root.clipboard, 'GPU catalog: HTTP 403\nCheck permissions.')
        self.assertEqual(controller.status.value, controller.root.clipboard)
        controller.status.value = ''
        controller.copy_status()
        self.assertEqual(controller.root.clipboard, 'GPU catalog: HTTP 403\nCheck permissions.')

    def test_missing_tk_has_actionable_startup_message(self):
        import builtins
        original = builtins.__import__

        for missing in ('tkinter', '_tkinter'):
            def importing(name, *args, **kwargs):
                if name == 'tkinter':
                    raise ModuleNotFoundError(f'No module named {missing!r}', name=missing)
                return original(name, *args, **kwargs)
            with self.subTest(missing=missing), patch('builtins.__import__', side_effect=importing):
                with self.assertRaisesRegex(SystemExit, 'sudo apt install python3-tk'):
                    app_module.main()

    def test_unrelated_import_failure_is_not_hidden(self):
        import builtins
        original = builtins.__import__

        def importing(name, *args, **kwargs):
            if name == 'tkinter':
                raise ModuleNotFoundError('No module named dependency', name='dependency')
            return original(name, *args, **kwargs)
        with patch('builtins.__import__', side_effect=importing):
            with self.assertRaisesRegex(ModuleNotFoundError, 'dependency'):
                app_module.main()

    def test_ssh_command_validates_and_ignores_provider_command(self):
        with tempfile.TemporaryDirectory() as td:
            known = Path(td)/'known_hosts'
            identity = Path(td)/'identity'
            known.write_text('')
            identity.write_text('')
            pod = {'ssh': {'direct': {'host':'pod.example', 'username':'root', 'port':2222, 'command':'unsafe'}}}
            cmd = ssh_command(pod, str(known), str(identity))
            self.assertIn('StrictHostKeyChecking=yes', cmd)
            self.assertIn('UserKnownHostsFile='+str(known), cmd)
            self.assertIn('-F', cmd)
            self.assertIn('/dev/null', cmd)
            self.assertIn(str(identity), cmd)
            self.assertNotIn('unsafe', cmd)
            self.assertNotIn('--require-gpu', cmd[-1])
            self.assertIn('--require-gpu', ssh_command(pod, str(known), str(identity), True)[-1])
            self.assertIn('--file-channel', ssh_command(pod, str(known), file_channel=True)[-1])
            live_ssh = ssh_command(pod, str(known), require_gpu=True, live=True)[-1]
            self.assertIn('--live', live_ssh)
            self.assertIn('--file-channel', live_ssh)
            self.assertIn('--require-gpu', live_ssh)
            self.assertEqual(live_ssh, 'python3 /workspace/pump-lab-live/_zLab/worker_relay.py --require-gpu --file-channel --live /workspace/pump-lab-live/build/lab/datapump-lab-worker')
            self.assertNotIn('--file-channel', cmd[-1])
            self.assertIsNone(file_root(''))
            self.assertEqual(file_root(td), Path(td).resolve())
            with self.assertRaises(ValueError): file_root(str(known))
            with self.assertRaises(ValueError):
                ssh_command({'ssh': {'direct': {'host':'-oProxyCommand=bad', 'username':'root','port':22}}}, str(known))
            with self.assertRaises(ValueError):
                ssh_command({'ssh': {'direct': {'host':'host', 'username':'root', 'port':'22'}}}, str(known))
            bad = Path(td)/'bad%h'
            bad.write_text('')
            with self.assertRaises(ValueError): ssh_command(pod, str(bad))

    def test_forget_does_not_mutate_state_while_provider_call_is_active(self):
        class Status:
            def set(self, value): self.value = value
        app = App.__new__(App)
        app.busy = True
        app.status = Status()
        # Missing list and state deliberately prove the guard runs before reading them.
        app.forget()
        self.assertIn('Wait', app.status.value)

    def test_deadline_terminates_pod_once_and_preserves_future_pod(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            state.write([{'name':'expired', 'id':'p1', 'deadline':100},
                         {'name':'future', 'id':'p2', 'deadline':200}])
            client = Fake()
            client.pods = [{'name':'expired','id':'p1','status':'RUNNING'},
                           {'name':'future','id':'p2','status':'RUNNING'}]
            owner = Ownership(state, client)
            self.assertEqual(owner.enforce_deadlines(now=150), (1, []))
            count, warnings = owner.enforce_deadlines(now=150)
            self.assertEqual(count, 0)
            self.assertIn('provider status RUNNING', warnings[0])
            self.assertEqual(client.deleted, ['p1'])
            self.assertEqual(state.read()[0]['deadline_action'], 'pod:p1')

    def test_deadline_cancels_queue_then_terminates_if_queue_became_pod(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            state.write([{'name':'expired', 'deadline':100}])
            client = Fake()
            client.queued = [{'name':'expired','id':'q1','status':'QUEUED'}]
            owner = Ownership(state, client)
            self.assertEqual(owner.enforce_deadlines(now=150), (1, []))
            client.queued = []
            client.pods = [{'name':'expired', 'id':'p1', 'status':'RUNNING'}]
            self.assertEqual(owner.enforce_deadlines(now=151), (1, []))
            self.assertEqual(client.deleted, ['queue:q1', 'p1'])

    def test_deadline_missing_pod_does_not_block_later_jobs(self):
        class MissingFirst(Fake):
            def delete_pod(self, pod_id):
                self.deleted.append(pod_id)
                if pod_id == 'p1':
                    raise RunPodError('not found', status=404)
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            state.write([{'name':'missing', 'id':'p1', 'deadline':100},
                         {'name':'running', 'id':'p2', 'deadline':100}])
            client = MissingFirst()
            client.pods = [{'name':'running','id':'p2','status':'RUNNING'}]
            owner = Ownership(state, client)
            count, errors = owner.enforce_deadlines(now=150)
            self.assertEqual(count, 1)
            self.assertIn('HTTP 404', errors[0])
            self.assertEqual(client.deleted, ['p1', 'p2'])
            self.assertEqual(state.read()[0]['deadline_absent_pod'], 'p1')
            owner.enforce_deadlines(now=151)
            self.assertEqual(client.deleted, ['p1', 'p2'])

    def test_create_requires_future_deadline_before_provider_call(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            client = Fake()
            with self.assertRaises(ValueError): Ownership(state, client).create({}, 'image', 1)
            self.assertFalse(hasattr(client, 'payload'))
            self.assertEqual(state.read(), [])

    def test_forget_rechecks_busy_after_modal_callback(self):
        from unittest.mock import patch
        class Status:
            def set(self, value): self.value = value
        class List:
            def curselection(self): return (0,)
        controller = App.__new__(App)
        controller.busy, controller.status, controller.list = False, Status(), List()
        controller.records = [{'name':'one'}]
        class MessageBox:
            @staticmethod
            def askyesno(*_):
                controller.busy = True
                return True
        with patch.object(app_module, 'messagebox', MessageBox, create=True):
            controller.forget()  # no state object: state access would fail
        self.assertIn('Wait', controller.status.value)

    def test_remote_compute_must_match_recorded_catalog_choice(self):
        self.assertTrue(simulation_gpu({'compute_kind':'gpu','compute_id':'g1'},
                                       {'gpu':{'id':'g1','count':1}}))
        self.assertFalse(simulation_gpu({'compute_kind':'cpu','compute_id':'c1','requested_vcpu':32,'minimum_memory_gb':128},
                                        {'cpu':{'id':'c1','vcpuCount':32,'memory':128}}))
        with self.assertRaises(ValueError):
            simulation_gpu({'compute_kind':'cpu','compute_id':'c1','requested_vcpu':32,'minimum_memory_gb':128},
                           {'cpu':{'id':'c1','vcpuCount':32,'memory':64}})
        with self.assertRaises(ValueError):
            simulation_gpu({'compute_kind':'gpu','compute_id':'g1'}, {'gpu':{'id':'g2','count':1}})
        with self.assertRaises(ValueError):
            simulation_gpu({}, {'gpu':{'id':'g1','count':1}})

    def test_deployment_mode_and_receipt_redaction(self):
        with tempfile.TemporaryDirectory() as td:
            known = Path(td)/'known_hosts'
            known.write_text('')
            pod = {'gpu': {'id':'g1','count':1},
                   'ssh': {'direct': {'host':'pod.example','username':'root','port':22}}}
            calls = []
            def fake(*args, **kwargs):
                calls.append((args, kwargs))
                kwargs['on_output']('sensitive build log\nPUMP_LAB_MODE=cuda\nPUMP_LAB_DEVICE=secretTOKEN\nPUMP_LAB_WORKER_SHA256=abcd')
            record = {'compute_kind':'gpu','compute_id':'g1','deadline':time.time()+60}
            receipt = deploy_selected(pod, record, str(known), None, 'secretTOKEN', fake)
            self.assertEqual(calls[0][1]['mode'], 'cuda')
            self.assertNotIn('sensitive build log', receipt)
            self.assertNotIn('secretTOKEN', receipt)
            self.assertIn('[redacted]', receipt)
            self.assertIn('PUMP_LAB_WORKER_SHA256=abcd', receipt)
            pod.pop('gpu'); pod['cpu'] = {'id':'c1', 'vcpuCount':32, 'memory':128}
            record.update(compute_kind='cpu', compute_id='c1', requested_vcpu=32, minimum_memory_gb=128)
            deploy_selected(pod, record, str(known), None, deploy_func=fake)
            self.assertEqual(calls[1][1]['mode'], 'cpu')
            deploy_selected(pod, record, str(known), None, deploy_func=fake, profile='live')
            self.assertEqual(calls[-1][1]['profile'], 'live')
            with self.assertRaises(ValueError):
                deploy_selected(pod, {'compute_kind':'gpu','compute_id':'g1','deadline':time.time()+60}, str(known), None, deploy_func=fake)
            self.assertEqual(len(calls), 3)

    def test_clock_bounds_are_explicit_positive_and_finite(self):
        self.assertEqual(clock_bounds('0.25', '2', '10'), (0.25, 2, 10))
        for bad in ('', '0', '-1', 'nan', 'inf'):
            with self.assertRaises(ValueError): clock_bounds(bad, '1', '1')
            with self.assertRaises(ValueError): clock_bounds('1', bad, '1')
            with self.assertRaises(ValueError): clock_bounds('1', '1', bad)

    def test_live_argv_is_bounded_and_never_shell_interpolates(self):
        with tempfile.TemporaryDirectory() as td:
            binary = Path(td)/'audio helper'
            binary.write_text('')
            binary.chmod(0o700)
            command = live_command(['ssh','host','fixed remote'], str(binary), 'device;literal',
                                   '65536', '65536', '240000', '1024', '0.1', '1', '5')
            self.assertIn('--report-resources', command)
            local = __import__('json').loads(command[command.index('--local-json')+1])
            self.assertIn('device;literal', local)
            remote = __import__('json').loads(command[command.index('--remote-json')+1])
            self.assertEqual(remote, ['ssh','host','fixed remote'])
            for args in [('0', '65536', '240000', '1024'),
                         ('65536', '65536', '240000', '999999'),
                         ('65536', '65536', '0', '1024')]:
                with self.assertRaises(ValueError):
                    live_command(['ssh'], str(binary), 'default', *args, '0.1', '1', '5')

    def test_local_device_discovery_bounded_and_literal(self):
        with tempfile.TemporaryDirectory() as td:
            binary = Path(td)/'audio helper'
            binary.write_text('')
            binary.chmod(0o700)
            def runner(argv, data, timeout):
                self.assertEqual(argv, [str(binary), '--list-devices'])
                self.assertIsNone(data)
                self.assertEqual(timeout, 10)
                return 0, b'[{"id":"hw:1","description":"Device 1"}]'
            self.assertEqual(local_devices(str(binary), runner), {'hw:1 — Device 1':'hw:1'})
            with self.assertRaises(ValueError):
                local_devices(str(binary), lambda *_: (0, b'X' * 4097))
            with self.assertRaises(ValueError):
                local_devices(str(binary), lambda *_: (0, b'[{"id":"x\\n","description":"a"}]'))

    def test_device_query_detects_overflow_and_timeout(self):
        import sys
        with self.assertRaisesRegex(ValueError, 'exceeds'):
            device_query([sys.executable, '-c', "print('x'*5000)"], None, 2)
        with self.assertRaisesRegex(ValueError, 'timed out'):
            device_query([sys.executable, '-c', 'import time; time.sleep(5)'], None, .05)
        code, value = device_query([sys.executable, '-c', "print('[]')"], None, 2)
        self.assertEqual((code, value), (0, b'[]\n'))

    def test_catalog(self):
        choices = options([{'id': 'NVIDIA GeForce RTX 4090'}, {'id':'RTX 5090'}],
                          [{'id': 'small', 'vcpu': {'min': 1, 'max': 32}, 'ramGbPerVcpu': 2},
                           {'id': 'large', 'vcpu': {'min': 2, 'max': 32}, 'ramGbPerVcpu': 4}])
        self.assertEqual([i[1] for i in choices], [{'gpu':{'id':'RTX 5090','count':1}},
                         {'gpu':{'id':'NVIDIA GeForce RTX 4090','count':1}},
                         {'cpu':{'id':'large','vcpuCount':32}}])

    def test_catalog_partial_failures_keep_choices_and_hide_exception_text(self):
        class Catalog:
            def __init__(self, gpu, cpu):
                self.gpu, self.cpu, self.calls = gpu, cpu, []
            def fetch(self, name):
                self.calls.append(name)
                value = getattr(self, name)
                if isinstance(value, Exception):
                    raise value
                return value
            def list_gpus(self): return self.fetch('gpu')
            def list_cpus(self): return self.fetch('cpu')
        gpu = [{'id': 'NVIDIA GeForce RTX 4090'}]
        cpu = [{'id': 'cpu-128', 'vcpu': {'min': 2, 'max': 32}, 'ramGbPerVcpu': 4}]
        secret = 'token-that-must-not-be-displayed'
        for failed_kind in ('GPU', 'CPU'):
            with self.subTest(failed=failed_kind):
                failure = RunPodError(secret, status=403)
                client = Catalog(failure if failed_kind == 'GPU' else gpu,
                                 failure if failed_kind == 'CPU' else cpu)
                choices, warnings = app_module.load_catalog(client)
                self.assertEqual(client.calls, ['gpu', 'cpu'])
                self.assertEqual(len(choices), 1)
                self.assertEqual(len(warnings), 1)
                self.assertIn(failed_kind + ' catalog', warnings[0])
                self.assertIn('HTTP 403', warnings[0])
                self.assertIn('catalog:read', warnings[0])
                self.assertNotIn(secret, repr((choices, warnings)))

    def test_catalog_both_fail_or_are_empty(self):
        class Catalog:
            def list_gpus(self): raise RunPodError('secret', status=401)
            def list_cpus(self): raise RunPodError('secret', status=429)
        choices, warnings = app_module.load_catalog(Catalog())
        self.assertEqual(choices, [])
        self.assertEqual(len(warnings), 2)
        self.assertIn('HTTP 401', warnings[0])
        self.assertIn('HTTP 429', warnings[1])
        self.assertNotIn('secret', repr(warnings))
        class Empty:
            def list_gpus(self): return []
            def list_cpus(self): return []
        self.assertEqual(app_module.load_catalog(Empty()), ([], []))

    def test_catalog_gui_shows_partial_failure_and_usable_choice(self):
        class Value:
            def set(self, value): self.value = value
        class Catalog:
            def list_gpus(self): return [{'id':'RTX 5090'}]
            def list_cpus(self): raise RunPodError('sensitive detail', status=403)
        controller = App.__new__(App)
        controller.dropdown, controller.compute, controller.status = {}, Value(), Value()
        controller.background = lambda work, done: done(work(Catalog()))
        controller.catalog()
        self.assertEqual(len(controller.choices), 1)
        self.assertEqual(controller.compute.value, controller.dropdown['values'][0])
        self.assertIn('partially loaded', controller.status.value)
        self.assertIn('CPU catalog', controller.status.value)
        self.assertIn('catalog:read', controller.status.value)
        self.assertNotIn('sensitive detail', controller.status.value)

    def test_close_worker_session_allows_reopening_without_touching_pod(self):
        class Status:
            def set(self, value): self.value = value
        class Root:
            def after(self, _, callback): callback()
        class Bridge:
            closed = 0
            def close(self): self.closed += 1
        class ImmediateThread:
            def __init__(self, target, daemon): self.target = target
            def start(self): self.target()
        controller = App.__new__(App)
        controller.busy = controller.closing_bridge = False
        controller.status, controller.root = Status(), Root()
        bridge = controller.bridge = Bridge()
        with patch.object(app_module.threading, 'Thread', ImmediateThread):
            controller.close_bridge()
        self.assertEqual(bridge.closed, 1)
        self.assertIsNone(controller.bridge)
        self.assertFalse(controller.closing_bridge)
        self.assertIn('pod remains active', controller.status.value)

    def test_refresh_preserves_selected_record_by_name_when_reordered(self):
        class List:
            def __init__(self): self.selected, self.rows = (1,), []
            def curselection(self): return self.selected
            def delete(self, *_): self.selected, self.rows = (), []
            def insert(self, _, row): self.rows.append(row)
            def selection_set(self, index): self.selected = (index,)
            def activate(self, index): self.active = index
            def see(self, index): self.visible = index
        class Records:
            def __init__(self, records): self.records = records
            def read(self): return self.records
        controller = App.__new__(App)
        controller.list = List()
        controller.records = [{'name':'first'}, {'name':'selected'}]
        controller.state = Records([{'name':'selected'}, {'name':'first'}])
        controller.refresh()
        self.assertEqual(controller.list.curselection(), (0,))
        self.assertEqual(controller.records[0]['name'], 'selected')
        controller.state.records = [{'name':'first'}]
        controller.refresh()
        self.assertEqual(controller.list.curselection(), ())

    def test_failed_or_busy_session_close_is_safe_and_retryable(self):
        class Status:
            def set(self, value): self.value = value
        class Root:
            def after(self, _, callback): callback()
        class Bridge:
            def close(self): raise OSError('sensitive detail')
        class ImmediateThread:
            def __init__(self, target, daemon): self.target = target
            def start(self): self.target()
        controller = App.__new__(App)
        controller.busy, controller.closing_bridge = True, False
        controller.status, controller.root = Status(), Root()
        bridge = controller.bridge = Bridge()
        controller.close_bridge()
        self.assertIs(controller.bridge, bridge)
        self.assertIn('Wait', controller.status.value)
        controller.busy = False
        with patch.object(app_module.threading, 'Thread', ImmediateThread):
            controller.close_bridge()
        self.assertIs(controller.bridge, bridge)
        self.assertFalse(controller.closing_bridge)
        self.assertIn('Could not close', controller.status.value)
        self.assertNotIn('sensitive detail', controller.status.value)

    def test_create_and_delete(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            client = Fake(lambda p: (201, {'id':'pod1','name':p['name'], 'status':'PROVISIONING'}))
            owner = Ownership(state, client)
            owner.create({'gpu':{'id':'RTX 5090','count':1}}, 'image', time.time()+60)
            self.assertEqual(state.read()[0]['id'], 'pod1')
            self.assertTrue(client.payload['startSsh'])
            self.assertEqual(client.payload['ports'], ['22/tcp'])
            self.assertEqual(state.path.stat().st_mode & 0o777, 0o600)
            owner.delete(state.read()[0])
            self.assertEqual(client.deleted, ['pod1'])
            self.assertIn('termination requested', state.read()[0]['status'])

    def test_uncertain_create_reconciles_without_retry(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            client = Fake(error=RunPodError('timeout', uncertain=True))
            owner = Ownership(state, client)
            with self.assertRaises(RunPodError): owner.create({'cpu':{'id':'c1','vcpuCount':32}}, 'image', time.time()+60)
            with self.assertRaises(ValueError): owner.create({'cpu':{'id':'c1','vcpuCount':32}}, 'image', time.time()+60)
            name = state.read()[0]['name']
            client.pods = [{'name': name, 'id':'one', 'status':'RUNNING'}]
            owner.reconcile()
            self.assertEqual(state.read()[0]['id'], 'one')

    def test_queue_id_is_never_a_pod_id(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            owner = Ownership(state, Fake(lambda p: (202, {'id':'queue1','podId':None,'status':'QUEUED'})))
            owner.create({'cpu':{'id':'c1','vcpuCount':32}}, 'image', time.time()+60)
            self.assertNotIn('id', state.read()[0])
            self.assertEqual(state.read()[0]['queue_id'], 'queue1')
            owner.delete(state.read()[0])
            self.assertEqual(owner.client.deleted, ['queue:queue1'])
            with self.assertRaises(ValueError): owner.create({'cpu':{'id':'c1','vcpuCount':32}}, 'image', time.time()+60)

    def test_known_create_rejection_clears_pending(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            owner = Ownership(state, Fake(error=RunPodError('denied', status=403)))
            with self.assertRaises(RunPodError): owner.create({'cpu':{'id':'c1','vcpuCount':32}}, 'image', time.time()+60)
            self.assertEqual(state.read(), [])

if __name__ == '__main__': unittest.main()
