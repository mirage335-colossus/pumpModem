import tempfile
import time
import unittest
from unittest.mock import Mock, patch
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
        status, result = self.result(payload)
        if status == 201:
            self.pods.append(result)
        elif status == 202:
            self.queued = [dict(result, name=payload['name'])]
        return status, result
    @staticmethod
    def _get(rows, identity):
        for row in rows:
            if row['id'] == identity:
                return row
        raise RunPodError('not found', status=404)
    def get_pod(self, identity):
        return self._get(self.pods, identity)
    def get_queued(self, identity):
        return self._get(self.list_queued(), identity)
    def list_pods(self):
        return self.pods
    def list_queued(self):
        return getattr(self, 'queued', [])
    def cancel_queued(self, queue_id):
        self.deleted.append('queue:'+queue_id)
    def delete_pod(self, pod_id):
        self.deleted.append(pod_id)


class Tests(unittest.TestCase):
    def test_failed_initial_ownership_save_removes_bootstrap_without_request(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td) / 'jobs.json')
            client = Mock()
            with patch.object(state, 'write', side_effect=OSError('disk full')):
                with self.assertRaisesRegex(OSError, 'disk full'):
                    Ownership(state, client).create({'gpu': {'id': 'RTX 5090', 'count': 1}}, 'image', time.time() + 60)
            client.create_pod.assert_not_called()
            self.assertEqual(list(Path(td).iterdir()), [])

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

    def test_owned_pod_enrollment_stops_on_mismatch_and_honors_explicit_file(self):
        client = Mock()
        record = {'id': 'pod-2', 'name': 'second'}
        detail = {'id': 'pod-2', 'name': 'second', 'ssh': {'direct': {'host': 'fresh.example', 'port': 3322}}}
        client.get_pod.return_value = detail
        client.get_pod_ssh_fingerprint.return_value = 'expected-fingerprint'
        state = Path('/private/state')
        with patch('host_identity.existing_host_key', return_value=None) as pin, \
             patch('host_identity.verify_host_key', side_effect=ValueError('fingerprint mismatch')) as verify:
            self.assertEqual(app_module.owned_pod_connection(client, record, 'my_known_hosts', state),
                             (detail, 'my_known_hosts'))
            client.get_pod_ssh_fingerprint.assert_not_called()
            pin.assert_not_called()
            with self.assertRaisesRegex(ValueError, 'fingerprint mismatch'):
                app_module.owned_pod_connection(client, record, '', state)
            verify.assert_called_once_with('pod-2', 'fresh.example', 3322, 'expected-fingerprint', state)
            client.get_pod_ssh_fingerprint.assert_called_once_with('pod-2')
            client.get_pod.return_value = dict(detail, id='different-pod')
            with self.assertRaisesRegex(ValueError, 'do not match'):
                app_module.owned_pod_connection(client, record, '', state)
            self.assertEqual(pin.call_count, 1)

    def test_console_fingerprint_uses_same_verifier_even_with_prior_pin(self):
        record = {'id': 'owned-id', 'name': 'lab'}
        detail = dict(record, ssh={'direct': {'host': 'host.example', 'port': 13000}})
        client = Mock()
        client.get_pod.return_value = detail
        fingerprint = 'SHA256:' + 'A' * 43
        state = Path('/private/state')
        with patch('host_identity.existing_host_key', return_value=state / 'old') as cached, \
             patch('host_identity.verify_host_key', return_value=state / 'pin') as verify:
            self.assertEqual(app_module.owned_pod_connection(client, record, fingerprint, state),
                             (detail, str(state / 'pin')))
            verify.assert_called_once_with('owned-id', 'host.example', 13000, fingerprint, state)
            verify.side_effect = ValueError('different SSH host key already exists')
            with self.assertRaisesRegex(ValueError, 'different SSH host key'):
                app_module.owned_pod_connection(client, record, fingerprint, state)
            cached.assert_not_called()
            client.get_pod_ssh_fingerprint.assert_not_called()

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
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATING)
            self.assertEqual(owner.enforce_deadlines(now=151), (0, []))
            self.assertEqual(client.deleted, ['p1'])
            client.pods.pop(0)
            self.assertEqual(Ownership(state, client).enforce_deadlines(now=152), (0, []))
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATED)
            self.assertFalse(Ownership.needs_poll(state.read()[0], 15000))
            self.assertEqual(state.read()[1], {'name':'future', 'id':'p2', 'deadline':200})

    def test_deadline_cancels_queue_then_terminates_if_queue_became_pod(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            state.write([{'name':'expired', 'deadline':100}])
            client = Fake()
            queue = {'name':'expired','id':'q1','status':'QUEUED'}
            client.queued = [queue]
            owner = Ownership(state, client)
            self.assertEqual(owner.enforce_deadlines(now=150), (1, []))
            queue.update(status='DEPLOYED', podId='p1')
            # The queue can report deployment before its pod is visible.
            self.assertEqual(owner.enforce_deadlines(now=160), (0, []))
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATING)
            self.assertEqual(state.read()[0]['id'], 'p1')
            client.pods = [{'name':'expired', 'id':'p1', 'status':'RUNNING'}]
            self.assertEqual(owner.enforce_deadlines(now=161), (1, []))
            self.assertEqual(client.deleted, ['queue:q1', 'p1'])
            client.pods.clear()
            self.assertEqual(owner.enforce_deadlines(now=162), (0, []))
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATED)

    def test_deadline_missing_pod_does_not_block_later_jobs(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            state.write([{'name':'missing', 'id':'p1', 'deadline':100},
                         {'name':'running', 'id':'p2', 'deadline':100}])
            client = Fake()
            client.pods = [{'name':'running','id':'p2','status':'RUNNING'}]
            owner = Ownership(state, client)
            self.assertEqual(owner.enforce_deadlines(now=150), (1, []))
            self.assertEqual(client.deleted, ['p2'])
            self.assertEqual(state.read()[0]['status'], Ownership.NOT_VISIBLE)
            client.pods.clear()
            self.assertEqual(owner.enforce_deadlines(now=271), (0, []))
            self.assertEqual(client.deleted, ['p2'])
            self.assertEqual(state.read()[0]['status'], Ownership.NOT_VISIBLE)
            self.assertEqual(state.read()[1]['status'], Ownership.TERMINATED)

    def test_stale_termination_of_absent_pod_settles_and_resumes_on_reappearance(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            record = {'name': 'owned', 'id': 'p1', 'deadline': 100,
                      'status': Ownership.TERMINATING, 'termination_started': 50,
                      'ssh_identity': str(Path(td)/'private-key')}
            state.write([record])
            client = Fake()
            owner = Ownership(state, client)
            with patch('host_identity.forget_host_key') as forget_pin, \
                    patch('bootstrap.forget_bootstrap') as forget_identity:
                self.assertEqual(owner.enforce_deadlines(now=250), (0, []))
                hidden = dict(record, status=Ownership.NOT_VISIBLE)
                self.assertEqual(state.read(), [hidden])
                self.assertFalse(Ownership.needs_poll(hidden, 300))
                self.assertEqual(owner.reconcile(), [hidden])
                forget_pin.assert_not_called()
                forget_identity.assert_not_called()
            self.assertEqual(client.deleted, [])
            client.pods = [{'name': 'owned', 'id': 'p1', 'status': 'RUNNING'}]
            recovered = owner.reconcile()[0]
            self.assertEqual(recovered['status'], Ownership.TERMINATING)
            self.assertEqual(owner.enforce_deadlines(), (1, []))
            self.assertEqual(client.deleted, ['p1'])

    def test_reconcile_already_failed_termination_of_absent_pod(self):
        cases = (({}, Ownership.NOT_VISIBLE), ({'id': 'p1'}, Ownership.TERMINATED))
        for receipt, expected in cases:
            with self.subTest(receipt=receipt), tempfile.TemporaryDirectory() as td:
                state = State(Path(td)/'jobs.json')
                state.write([{'name': 'owned', 'id': 'p1', 'status': Ownership.TERMINATION_FAILED,
                              'termination_error': 'Old error', 'termination_sent': receipt}])
                client = Fake()
                owner = Ownership(state, client)
                recovered = owner.reconcile()[0]
                self.assertEqual(recovered['status'], expected)
                self.assertNotIn('termination_error', recovered)
                self.assertEqual(client.deleted, [])
                if expected == Ownership.NOT_VISIBLE:
                    client.pods = [{'name': 'owned', 'id': 'p1', 'status': 'RUNNING'}]
                    self.assertEqual(owner.reconcile()[0]['status'], Ownership.TERMINATING)
                    self.assertEqual(owner.enforce_deadlines(), (1, []))
                    self.assertEqual(client.deleted, ['p1'])

    def test_termination_does_not_settle_when_provider_cannot_confirm_absence(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            state.write([{'name': 'owned', 'id': 'p1', 'status': Ownership.TERMINATING,
                          'termination_started': 100}])
            client = Fake()
            client.pods = [{'name': 'owned', 'id': 'p1', 'status': 'RUNNING'}]
            with patch.object(client, 'get_pod', side_effect=RunPodError('not found', status=404)):
                self.assertEqual(Ownership(state, client).enforce_deadlines(now=110), (0, []))
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATING)
            client.pods.clear()
            with patch.object(client, 'get_pod', side_effect=RunPodError('forbidden', status=403)):
                self.assertEqual(Ownership(state, client).enforce_deadlines(now=115), (0, ['RunPod HTTP 403.']))
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATION_FAILED)
            self.assertEqual(client.deleted, [])

    def test_termination_checks_exact_identity_and_keeps_failure_for_retry(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            record = {'name':'owned', 'id':'p1', 'deadline':time.time()+60}
            state.write([record])
            client = Fake()
            client.pods = [{'name':'different', 'id':'p1', 'status':'RUNNING'}]
            owner = Ownership(state, client)
            with self.assertRaisesRegex(ValueError, 'selected pod changed'):
                owner.request_termination(dict(record, id='p2'))
            owner.request_termination(record)
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATING)
            self.assertEqual(client.deleted, [])
            count, errors = owner.enforce_deadlines()
            self.assertEqual(count, 0)
            self.assertTrue(errors)
            self.assertEqual(client.deleted, [])
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATION_FAILED)
            owner.reconcile()
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATION_FAILED)
            client.pods[0]['name'] = 'owned'
            owner.request_termination(record)
            self.assertEqual(owner.enforce_deadlines(), (1, []))
            self.assertEqual(client.deleted, ['p1'])

    def test_retry_confirms_late_deletion_without_losing_previous_receipt(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            client = Fake()
            client.pods = [{'name':'owned', 'id':'p1', 'status':'RUNNING'}]
            state.write(client.pods)
            owner = Ownership(state, client)
            started = owner.request_termination(state.read()[0])['termination_started']
            self.assertEqual(owner.enforce_deadlines(now=started), (1, []))
            owner.enforce_deadlines(now=started+121)
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATION_FAILED)
            client.pods.clear()
            owner.request_termination(state.read()[0])
            self.assertEqual(owner.enforce_deadlines(), (0, []))
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATED)
            self.assertEqual(client.deleted, ['p1'])

    def test_missing_existing_pod_is_visible_as_unconfirmed(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            state.write([{'name':'owned', 'id':'p1', 'status':'RUNNING'}])
            client = Fake()
            records = Ownership(state, client).reconcile()
            self.assertEqual(records[0]['status'], Ownership.NOT_VISIBLE)
            self.assertEqual(client.deleted, [])

    def test_create_requires_future_deadline_before_provider_call(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            client = Fake()
            with self.assertRaises(ValueError): Ownership(state, client).create({}, 'image', 1)
            self.assertFalse(hasattr(client, 'payload'))
            self.assertEqual(state.read(), [])

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
            record = {'compute_kind':'gpu','compute_id':'g1','deadline':time.time()+60, 'cloud':'SECURE'}
            for tier in (None, 'COMMUNITY'):
                if tier:
                    pod['cloud'] = tier
                with self.subTest(tier=tier), self.assertRaisesRegex(ValueError, 'cloud tier'):
                    deploy_selected(pod, record, str(known), None, deploy_func=fake)
            self.assertEqual(calls, [])
            pod['cloud'] = 'SECURE'
            receipt = deploy_selected(pod, record, str(known), None, 'secretTOKEN', fake)
            self.assertEqual(calls[0][1]['mode'], 'gpu')
            self.assertTrue(0 < calls[0][1]['timeout'] <= 60)
            self.assertIn('PUMP_LAB_MODE=cuda', receipt)
            self.assertNotIn('sensitive build log', receipt)
            self.assertNotIn('secretTOKEN', receipt)
            self.assertIn('[redacted]', receipt)
            self.assertIn('PUMP_LAB_WORKER_SHA256=abcd', receipt)
            pod.pop('gpu'); pod['cpu'] = {'id':'c1', 'vcpuCount':32, 'memory':128}
            record.update(compute_kind='cpu', compute_id='c1', requested_vcpu=32, minimum_memory_gb=128,
                          deadline=time.time()+1800)
            deploy_selected(pod, record, str(known), None, deploy_func=fake)
            self.assertEqual(calls[1][1]['mode'], 'cpu')
            self.assertEqual(calls[1][1]['timeout'], 300)
            deploy_selected(pod, record, str(known), None, deploy_func=fake, profile='live')
            self.assertEqual(calls[-1][1]['profile'], 'live')
            with self.assertRaises(ValueError):
                deploy_selected(pod, {'compute_kind':'gpu','compute_id':'g1','deadline':time.time()+60}, str(known), None, deploy_func=fake)
            for deadline in (0, None, float('inf'), float('nan')):
                with self.subTest(deadline=deadline), self.assertRaisesRegex(ValueError, 'deadline'):
                    deploy_selected(pod, dict(record, deadline=deadline), str(known), None, deploy_func=fake)
            self.assertEqual(len(calls), 3)

    def test_one_gpu_allocation_required_on_each_tier(self):
        for tier in ('SECURE', 'COMMUNITY'):
            record = {'compute_kind': 'gpu', 'compute_id': 'g1', 'cloud': tier}
            pod = {'gpu': {'id': 'g1', 'count': 1}, 'cloud': tier}
            self.assertTrue(simulation_gpu(record, pod))
            for count in (0, 2, True, 1.0, None):
                pod['gpu']['count'] = count
                with self.subTest(tier=tier, count=count), self.assertRaisesRegex(ValueError, 'one GPU'):
                    simulation_gpu(record, pod)

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
        self.assertEqual([i[1] for i in choices], [{'gpu':{'id':'RTX 5090','count':1}, 'cloud':'SECURE'},
                         {'gpu':{'id':'NVIDIA GeForce RTX 4090','count':1}, 'cloud':'SECURE'},
                         {'cpu':{'id':'large','vcpuCount':32}, 'cloud':'SECURE'}])

    def test_catalog_defaults_to_available_full_gpus_before_partitions(self):
        models = ['NVIDIA RTX PRO 6000 Blackwell Server Edition MIG 1g.24gb',
                  'NVIDIA GeForce RTX 5090', 'NVIDIA RTX PRO 6000 Blackwell Server Edition']
        choices = options([{'id': model, 'availability': 'LOW'} for model in models], [])
        self.assertEqual([value['gpu']['id'] for _, value in choices],
                         [models[2], models[1], models[0]])

    def test_catalog_hides_unavailable_gpu_and_cpu_but_keeps_unknown_stock(self):
        gpus = [{'id': 'RTX 5090', 'availability': 'NONE'},
                {'id': 'RTX 4090', 'availability': 'HIGH', 'secure': False},
                {'id': 'RTX PRO 6000 Blackwell Server Edition'}]
        cpus = [{'id': 'cpu', 'availability': 'NONE', 'vcpu': {'min': 2, 'max': 32}, 'ramGbPerVcpu': 4}]
        choices = options(gpus, cpus)
        self.assertEqual(len(choices), 1)
        self.assertEqual(choices[0][1]['gpu']['id'], gpus[2]['id'])
        self.assertIn('stock unknown', choices[0][0])

    def test_quote_requires_exact_current_sku_and_tier(self):
        client = Mock()
        select = {'gpu': {'id': 'RTX 5090', 'count': 1}, 'cloud': 'COMMUNITY'}
        gpu = {'id': 'RTX 5090', 'availability': 'HIGH',
               'price': {'secure': 1.32, 'community': 0.87}}
        client.list_gpus.return_value = [gpu]
        self.assertIn('stock high · $0.87/hr', app_module.creation_quote(client, select))
        client.list_gpus.assert_called_once_with('COMMUNITY')
        for reason in ({'availability': 'NONE'}, {'community': False}):
            client.list_gpus.return_value = [dict(gpu, **reason)]
            with self.assertRaisesRegex(ValueError, 'stock none'):
                app_module.creation_quote(client, select)
        for rows in ([gpu, gpu], [dict(gpu, id='RTX 4090')], []):
            client.list_gpus.return_value = rows
            with self.assertRaisesRegex(ValueError, 'no longer listed'):
                app_module.creation_quote(client, select)
        client.list_gpus.return_value = [{'id': 'RTX 5090', 'availability': 'novel'}]
        self.assertIn('stock unknown · price unknown', app_module.creation_quote(client, select))
        client.list_cpus.assert_not_called()

    def test_32_vcpu_quote_and_memory_requirement(self):
        client = Mock()
        cpu = {'id': 'general', 'vcpu': {'min': 2, 'max': 32}, 'ramGbPerVcpu': 4,
               'price': {'securePerVcpu': 0.025}, 'availability': 'MEDIUM'}
        select = {'cpu': {'id': 'general', 'vcpuCount': 32}, 'cloud': 'SECURE'}
        client.list_cpus.return_value = [cpu]
        self.assertIn('128 GB · stock medium · $0.8/hr', app_module.creation_quote(client, select))
        client.list_cpus.return_value = [dict(cpu, ramGbPerVcpu=2)]
        with self.assertRaisesRegex(ValueError, 'no longer listed'):
            app_module.creation_quote(client, select)
        client.list_gpus.assert_not_called()

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
            def list_gpus(self, cloud): return self.fetch('gpu')
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
            def list_gpus(self, cloud): raise RunPodError('secret', status=401)
            def list_cpus(self): raise RunPodError('secret', status=429)
        choices, warnings = app_module.load_catalog(Catalog())
        self.assertEqual(choices, [])
        self.assertEqual(len(warnings), 2)
        self.assertIn('HTTP 401', warnings[0])
        self.assertIn('HTTP 429', warnings[1])
        self.assertNotIn('secret', repr(warnings))
        class Empty:
            def list_gpus(self, cloud): return []
            def list_cpus(self): return []
        self.assertEqual(app_module.load_catalog(Empty()), ([], []))

    def test_catalog_gui_shows_partial_failure_and_usable_choice(self):
        class Value:
            def get(self): return self.value
            def set(self, value): self.value = value
        class Catalog:
            def list_gpus(self, cloud): return [{'id':'RTX 5090'}]
            def list_cpus(self): raise RunPodError('sensitive detail', status=403)
        controller = App.__new__(App)
        controller.dropdown, controller.compute, controller.status = {}, Value(), Value()
        controller.choices, controller.cloud, controller.preferred_compute = [], Value(), None
        controller.key = Value()
        controller.exit_after = None
        controller.key.set('test-key')
        controller.cloud.set('SECURE')
        controller.compute.set('')
        controller.background = lambda work, done: done(work(Catalog()))
        controller.catalog()
        self.assertEqual(len(controller.choices), 1)
        self.assertEqual(controller.compute.value, controller.dropdown['values'][0])
        self.assertIn('partially loaded', controller.status.value)
        self.assertIn('CPU catalog', controller.status.value)
        self.assertIn('catalog:read', controller.status.value)
        self.assertNotIn('sensitive detail', controller.status.value)

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
        controller.session_record = None
        controller.key_entry = Mock()
        controller.records = [{'name':'first'}, {'name':'selected'}]
        controller.known_hosts = Mock()
        controller.known_hosts.get.return_value = ''
        controller.state = Records([{'name':'selected'}, {'name':'first'}])
        controller.refresh()
        self.assertEqual(controller.list.curselection(), (0,))
        self.assertEqual(controller.records[0]['name'], 'selected')
        controller.state.records = [{'name':'first'}]
        controller.refresh()
        self.assertEqual(controller.list.curselection(), ())

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
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATING)
            identity = Path(state.read()[0]['ssh_identity'])
            self.assertTrue(identity.exists())
            # Pending provider deletion preserves the pin and the generated identity.
            with patch('host_identity.forget_host_key') as forget:
                owner.enforce_deadlines()
                forget.assert_not_called()
                client.pods.clear()
                owner.enforce_deadlines()
                forget.assert_called_once_with('pod1', Path(td))
            self.assertFalse(identity.exists())
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATED)

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
            owner.client.queued[0]['status'] = 'CANCELLED'
            owner.enforce_deadlines()
            self.assertEqual(state.read()[0]['status'], Ownership.TERMINATED)
            owner.create({'cpu':{'id':'c1','vcpuCount':32}}, 'image', time.time()+60)
            self.assertEqual(len(state.read()), 2)

    def test_known_create_rejection_clears_pending(self):
        with tempfile.TemporaryDirectory() as td:
            state = State(Path(td)/'jobs.json')
            owner = Ownership(state, Fake(error=RunPodError('denied', status=403)))
            with self.assertRaises(RunPodError): owner.create({'cpu':{'id':'c1','vcpuCount':32}}, 'image', time.time()+60)
            self.assertEqual(state.read(), [])

if __name__ == '__main__': unittest.main()
