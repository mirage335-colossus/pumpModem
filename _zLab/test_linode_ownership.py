"""Managed CPU instance lifecycle using provider-normalized Linode responses."""
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import Mock

from app import Ownership, State, deploy_selected, owned_pod_connection, simulation_gpu
from runpod import RunPodError


class LinodeOwnershipTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.state = State(Path(directory.name) / 'jobs.json')
        self.client = Mock()
        self.client.list_queued.return_value = []
        self.client.list_pods.return_value = []
        self.owner = Ownership(self.state, self.client)
        self.name = self.owner.new_pod_name()
        self.deadline = time.time() + 60
        self.image = 'linode/ubuntu24.04'
        self.plan = {'provider': 'linode', 'region': 'us-ord',
                     'cpu': {'id': 'fixture-compute-optimized', 'vcpuCount': 256}, 'memory_gib': 512}
        self.details = {'id': '1234', 'name': self.name, 'provider': 'linode', 'status': 'RUNNING',
                        'region': self.plan['region'], 'cpu': dict(self.plan['cpu'], memory=512),
                        'ssh': {'direct': {'host': '192.0.2.12', 'port': 22, 'username': 'root'}}}
        self.client.create_pod.return_value = 201, self.details
        self.client.get_pod.return_value = self.details

    def create(self):
        return self.owner.create(self.plan, self.image, self.deadline, name=self.name)

    def test_managed_allocation_and_deployment(self):
        self.create()
        payload = self.client.create_pod.call_args.args[0]
        self.assertEqual({field: payload[field] for field in ('label', 'type', 'region', 'image', 'booted')},
                         {'label': self.name, 'type': self.plan['cpu']['id'], 'region': self.plan['region'],
                          'image': self.image, 'booted': True})
        self.assertIn('authorized_keys', payload)
        self.assertIn('user_data', payload['metadata'])
        record, = self.state.read()
        self.assertEqual((record['id'], record['requested_vcpu'], record['requested_memory_gib']),
                         ('1234', 256, 512))
        self.assertNotIn('cloud', record)
        self.assertNotIn(payload['metadata']['user_data'], self.state.path.read_text())
        known = self.state.path.parent / 'known_hosts'
        known.touch()
        worker = '/workspace/pump-lab/revisions/' + 'a' * 64 + '/build/lab/datapump-worker'
        deploy = Mock(return_value=worker)
        selection = deploy_selected(self.details, record, str(known), record['ssh_identity'], deploy_func=deploy)
        self.assertEqual(deploy.call_args.kwargs['mode'], 'native-cpu')
        self.assertEqual(selection.worker_path, worker)

    def test_lost_create_response_binds_approved_pending_name_before_deleting(self):
        def lost_response(payload):
            pending, = self.state.read()
            self.assertEqual(pending['name'], payload['label'])
            self.assertNotIn('id', pending)
            self.assertTrue(Path(pending['ssh_identity']).exists())
            raise RunPodError('Connection lost', uncertain=True)

        self.client.create_pod.side_effect = lost_response
        with self.assertRaises(RunPodError):
            self.create()
        with self.assertRaisesRegex(ValueError, 'unresolved'):
            self.owner.create(self.plan, self.image, self.deadline)
        self.client.create_pod.assert_called_once()
        pending, = self.state.read()
        self.state.queue_linode_end(pending)
        self.client.list_pods.return_value = [dict(self.details, name='unrelated', id='9999'), self.details]
        restarted = Ownership(State(self.state.path), self.client)
        restarted.apply_queued_ends('linode')
        identity = Path(pending['ssh_identity'])
        def check_approval(_):
            persisted, = self.state.read()
            self.assertEqual(persisted['id'], self.details['id'])
            self.assertTrue(Ownership.linode_approved(persisted))
        self.client.delete_pod.side_effect = check_approval
        self.assertEqual(restarted.enforce_deadlines(now=self.deadline), (1, []))
        self.client.delete_pod.assert_called_once_with(self.details['id'])
        self.assertTrue(identity.exists())
        self.client.get_pod.side_effect = RunPodError('No such instance', status=404)
        self.client.list_pods.return_value = []
        self.assertEqual(restarted.enforce_deadlines(now=self.deadline + 1), (0, []))
        self.assertFalse(identity.exists())
        self.assertEqual(self.state.read()[0]['status'], Ownership.TERMINATED)

    def test_identity_mismatch_blocks_deletion(self):
        self.create()
        record, = self.state.read()
        self.state.queue_linode_end(record)
        self.owner.apply_queued_ends('linode')
        for change in ({'provider': 'runpod'}, {'name': 'other'}, {'id': 'other'}):
            with self.subTest(change=change):
                self.client.get_pod.return_value = dict(self.details, **change)
                count, errors = self.owner.delete(record)
                self.assertEqual(count, 0)
                self.assertTrue(errors)
                self.client.delete_pod.assert_not_called()
                self.assertTrue(Path(record['ssh_identity']).exists())

    def test_expiry_legacy_end_and_direct_delete_require_fresh_approval(self):
        self.create()
        record, = self.state.read()
        self.state.queue_end(record['name'])  # Older releases also queued on window close.
        record['status'] = Ownership.TERMINATING
        record['termination_started'] = time.time()
        self.state.write([record])
        restarted = Ownership(State(self.state.path), self.client)
        restarted.apply_queued_ends('linode')
        self.assertTrue(restarted.linode_needs_confirmation(record, self.deadline))
        self.assertEqual(restarted.enforce_deadlines(now=self.deadline), (0, []))
        with self.assertRaisesRegex(ValueError, 'Confirm termination'):
            restarted.delete(record)
        with self.assertRaisesRegex(ValueError, 'Confirm termination'):
            restarted._submit_termination(record, 'id', self.client.delete_pod)
        renewed = time.time() + 600
        kept = restarted.keep_linode(record, deadline=renewed)
        self.assertFalse(restarted._has_termination_intent(kept))
        self.assertFalse(restarted.linode_needs_confirmation(kept, self.deadline))
        self.assertEqual(kept['deadline'], renewed)
        self.assertTrue(Path(kept['ssh_identity']).exists())
        self.assertEqual(self.state.queued_ends(), set())
        self.client.delete_pod.assert_not_called()

    def test_known_id_approval_is_not_transferable_and_survives_restart(self):
        self.create()
        record, = self.state.read()
        self.state.queue_linode_end(dict(record, id='9999'))
        with self.assertRaisesRegex(ValueError, 'ID changed'):
            self.owner.apply_queued_ends('linode')
        self.assertFalse(self.owner.linode_approved(self.state.read()[0]))
        self.assertEqual(self.owner.enforce_deadlines(now=self.deadline), (0, []))
        self.client.delete_pod.assert_not_called()
        self.state.queue_linode_end(record)
        with self.assertRaisesRegex(ValueError, 'underway'):
            self.owner.keep_linode(record)
        self.owner.apply_queued_ends('linode')
        self.assertEqual(self.state.queued_linode_ends(), {})
        self.client.delete_pod.assert_not_called()
        restarted = Ownership(State(self.state.path), self.client)
        self.assertEqual(restarted.enforce_deadlines(), (1, []))
        self.assertEqual(restarted.enforce_deadlines(), (0, []))
        self.client.delete_pod.assert_called_once_with(self.details['id'])

    def test_legacy_sent_receipt_verifies_absence_without_new_delete(self):
        self.create()
        record, = self.state.read()
        record.update(status=Ownership.TERMINATING, termination_started=time.time(),
                      termination_sent={'id': record['id']})
        self.state.write([record])
        self.assertFalse(self.owner.linode_needs_confirmation(record, self.deadline))
        self.assertEqual(self.owner.enforce_deadlines(), (0, []))
        self.state.queue_linode_end(record)
        self.owner.apply_queued_ends('linode')
        self.assertEqual(self.owner.enforce_deadlines(), (0, []))
        self.client.get_pod.side_effect = RunPodError('Absent', status=404)
        self.assertEqual(self.owner.enforce_deadlines(), (0, []))
        self.assertEqual(self.state.read()[0]['status'], Ownership.TERMINATED)
        self.client.delete_pod.assert_not_called()

    def test_exact_plan_and_region_before_ssh_or_deployment(self):
        self.create()
        record, = self.state.read()
        for change in ({'provider': 'runpod'}, {'region': 'other'}, {'gpu': {'count': 1}},
                       {'cpu': dict(self.details['cpu'], id='other')},
                       {'cpu': dict(self.details['cpu'], vcpuCount=128)},
                       {'cpu': dict(self.details['cpu'], memory=1024)}):
            with self.subTest(change=change):
                wrong = dict(self.details, **change)
                self.client.get_pod.return_value = wrong
                with self.assertRaises(ValueError):
                    owned_pod_connection(self.client, record, 'explicit_known_hosts', self.state.path.parent)
                with self.assertRaises(ValueError):
                    simulation_gpu(record, wrong)


if __name__ == '__main__':
    unittest.main()
