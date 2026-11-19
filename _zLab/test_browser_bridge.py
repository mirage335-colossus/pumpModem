#!/usr/bin/env python3
import json
from pathlib import Path
import struct
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request
from unittest import mock
from urllib.parse import urlsplit

import browser_bridge


def frame(kind, payload=b''):
    return b'DPW1' + struct.pack('<II', kind, len(payload)) + payload


PEER = r'''
import struct, sys

def frame(t,p=b''):
    return b'DPW1'+struct.pack('<II',t,len(p))+p
sys.stdout.buffer.write(frame(101,b'{"generation":"1","ack":"0"}'))
sys.stdout.buffer.flush()
while True:
    h=sys.stdin.buffer.read(12)
    if not h: break
    if len(h)!=12: sys.exit(2)
    magic,t,n=struct.unpack('<4sII',h)
    data=sys.stdin.buffer.read(n)
    if len(data)!=n: sys.exit(2)
    sys.stdout.buffer.write(frame(103,struct.pack('<I',0)))
    sys.stdout.buffer.flush()
'''


LIVE_PEER = r'''
import json,struct,sys

def frame(t,p=b''):
    return b'DPW1'+struct.pack('<II',t,len(p))+p
resource={'capture_cap_bytes':1000,'playback_cap_bytes':2000,
          'capture_workspace_bytes':None,'playback_workspace_bytes':None,
          'companion_shared_control_limit_bytes':524288,'transport_buffer_allowance_bytes':134217728,
          'browser_host_allowance_bytes':536870912,'companion_worker_stack_count':3,
          'companion_worker_stack_bytes':None,'workspace_includes_buffer_cap':True,
          'companion_control_included_in_browser_allowance':False,
          'workspace_receipts_are_conservative':True,'receipt_is_aggregate':False,
          'process_baseline_included':False}
sys.stdout.buffer.write(frame(190,json.dumps(resource).encode())+frame(101,b'{"generation":"1","ack":"0"}'))
sys.stdout.buffer.flush()
while True:
    h=sys.stdin.buffer.read(12)
    if not h: break
    magic,channel,n=struct.unpack('<4sII',h)
    packet=sys.stdin.buffer.read(n)
    if magic!=b'DPL1' or channel!=0 or packet[:4]!=b'DPW1': sys.exit(4)
    kind=struct.unpack_from('<I',packet,4)[0]
    sys.stdout.buffer.write(frame(108 if kind==15 else 103,b'0000'))
    sys.stdout.buffer.flush()
'''


class LiveTransportTests(unittest.TestCase):
    def test_live_channel_receipts_and_restrictions(self):
        worker = browser_bridge.SimulationWorker([sys.executable, '-u', '-c', LIVE_PEER], live=True)
        try:
            with worker.state:
                self.assertTrue(worker.state.wait_for(lambda: worker.snapshot is not None or worker.output.error, timeout=5))
                self.assertIsNone(worker.output.error)
                self.assertEqual(worker.resources['transport_buffer_allowance_bytes'], 134217728)
            self.assertEqual(struct.unpack_from('<I', worker.output.get(), 4)[0], 101)
            worker.input(frame(15, struct.pack('<Qd', 7, 0.0)))
            self.assertEqual(struct.unpack_from('<I', worker.output.get(), 4)[0], 108)
            with self.assertRaises(browser_bridge._preview.PreviewError):
                worker.input(frame(3))
            with self.assertRaises(browser_bridge._preview.PreviewError):
                worker.write(frame(10), trusted=True)
        finally:
            worker.close()

    def test_receipt_from_actual_transport_is_accepted(self):
        transport = Path(__file__).resolve().parent / 'audio' / 'transport.py'
        child = json.dumps([sys.executable, '-c', 'import time; time.sleep(30)'])
        command = [sys.executable, str(transport), '--max-clock-offset', '0.1',
                   '--local-utc-uncertainty', '0.1', '--remote-clock-drift-ppm', '1',
                   '--remote-json', child, '--local-json', child, '--report-resources']
        worker = browser_bridge.SimulationWorker(command, live=True)
        try:
            with worker.state:
                self.assertTrue(worker.state.wait_for(lambda: worker.resources is not None or worker.output.error, timeout=8))
                self.assertIsNone(worker.output.error)
                self.assertEqual(worker.resources['browser_host_allowance_bytes'], 536870912)
                self.assertEqual(worker.resources['companion_worker_stack_count'], 3)
                self.assertIsNone(worker.resources['companion_worker_stack_bytes'])
                self.assertTrue(worker.resources['workspace_includes_buffer_cap'])
                self.assertFalse(worker.resources['receipt_is_aggregate'])
        finally:
            worker.close()

    def test_invalid_resource_receipt_is_not_forwarded(self):
        script = ("import struct,sys,time; p=b'{\"danger\":\"<script>\"}'; "
                  "sys.stdout.buffer.write(b'DPW1'+struct.pack('<II',190,len(p))+p); "
                  "sys.stdout.buffer.flush(); time.sleep(1)")
        worker = browser_bridge.SimulationWorker([sys.executable, '-u', '-c', script], live=True)
        try:
            with worker.state:
                self.assertTrue(worker.state.wait_for(lambda: worker.output.error is not None, timeout=5))
                self.assertIn('Invalid local resource receipt', worker.output.error)
                self.assertIsNone(worker.resources)
            self.assertEqual(worker.output.size, 0)
        finally:
            worker.close()


class StartupCleanupTests(unittest.TestCase):
    def test_child_is_reaped_if_pipe_setup_fails(self):
        real_popen = browser_bridge.subprocess.Popen
        children = []

        def capture(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            children.append(child)
            return child

        with mock.patch.object(browser_bridge.subprocess, 'Popen', side_effect=capture), \
             mock.patch.object(browser_bridge.os, 'set_blocking', side_effect=OSError('test setup failure')):
            with self.assertRaisesRegex(OSError, 'test setup failure'):
                browser_bridge.SimulationWorker([sys.executable, '-u', '-c', PEER])
        self.assertEqual(len(children), 1)
        self.assertIsNotNone(children[0].poll())
        self.assertTrue(children[0].stdin.closed)
        self.assertTrue(children[0].stdout.closed)


class FileImportTests(unittest.TestCase):
    def test_real_worker_import_is_scoped_and_authorized(self):
        root = Path(__file__).resolve().parent.parent
        binary = root / 'build/dev-cli-web/datapump-worker'
        if not binary.is_file():
            self.skipTest('native worker not built')
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            (home / 'keyfile').write_text('invalid fixture keyfile')
            (home / 'link').symlink_to('/etc/passwd')
            worker = browser_bridge.SimulationWorker(
                [sys.executable, str(root / '_zLab/worker_relay.py'),
                 '--file-channel', str(binary)], files=home)
            try:
                with worker.state:
                    self.assertTrue(worker.state.wait_for(
                        lambda: worker.snapshot is not None or worker.output.error, timeout=10))
                    self.assertIsNone(worker.output.error)
                    snapshot = worker.snapshot
                    menu = next(c for c in snapshot['controls'] if c.get('label') == 'Keyfile')
                    target = next(o['id'] for o in menu['options'] if o['label'] == 'Open keyfile')
                    seq, generation = int(snapshot['ack']) + 1, int(snapshot['generation'])
                payload = (struct.pack('<IQQQIII', 1, generation, seq, int(target), 4, 0, 0)
                           + browser_bridge._preview.string('') + browser_bridge._preview.string(''))
                worker.input(frame(2, payload))
                with worker.state:
                    self.assertTrue(worker.state.wait_for(
                        lambda: (worker.snapshot or {}).get('service') is not None or worker.output.error,
                        timeout=10))
                    self.assertIsNone(worker.output.error)
                    snapshot = worker.snapshot
                    service = snapshot['service']
                    event = {'version': 1, 'kind': 13, 'generation': snapshot['generation'],
                             'sequence': str(int(snapshot['ack']) + 1), 'target': service['id']}
                request = {'kind': 'open_file', 'event': event, 'path': '../outside'}
                with self.assertRaises(browser_bridge._preview.PreviewError):
                    worker.service(request)
                request['path'] = 'link'
                with self.assertRaises(OSError):
                    worker.service(request)
                # Ordinary browser messages cannot submit trusted file frames,
                # even when the local user enabled a file root.
                with self.assertRaises(browser_bridge._preview.PreviewError):
                    worker.input(frame(10, b''))
                with self.assertRaises(browser_bridge._preview.PreviewError):
                    worker.input(b'DPL1' + struct.pack('<II', 1, 12) + frame(12))
                request['path'] = 'keyfile'
                worker.service(request)
                with worker.state:
                    self.assertGreaterEqual(int(worker.snapshot['ack']), int(event['sequence']))
                with self.assertRaises(browser_bridge._preview.PreviewError):
                    worker.service(request)
            finally:
                worker.close()



class BrowserBridgeTests(unittest.TestCase):
    def setUp(self):
        self.bridge = browser_bridge.start([sys.executable, '-u', '-c', PEER])
        parts = urlsplit(self.bridge.url)
        self.base = f'{parts.scheme}://{parts.netloc}'
        self.token = parts.fragment
        self.session = ''

    def tearDown(self):
        self.bridge.close()
        self.bridge.close()

    def request(self, path, body=None, token=None, **headers):
        headers['X-Preview-Token'] = self.token if token is None else token
        headers['X-Preview-Session'] = self.session
        req = urllib.request.Request(self.base + path, data=body, headers=headers)
        with urllib.request.urlopen(req, timeout=5) as response:
            return response.read()

    def start_peer(self):
        self.session = json.loads(self.request('/start', b''))['session']
        return self.bridge.server.worker

    def test_loopback_ui_and_simulation_peer(self):
        self.assertTrue(self.base.startswith('http://127.0.0.1:'))
        self.assertIn(b'RunPod Robust simulation', self.request('/'))
        script = self.request('/tools/preview.mjs')
        self.assertIn(b'audio.disabled=true;audio.title=', script)
        worker = self.start_peer()
        snapshot = self.request('/output')
        self.assertEqual(snapshot[:4], b'DPW1')
        self.assertEqual(struct.unpack_from('<I', snapshot, 4)[0], 101)
        self.request('/input', frame(1, struct.pack('<II', 800, 600)))
        self.assertEqual(struct.unpack_from('<I', self.request('/output'), 4)[0], 103)
        self.bridge.close()
        self.assertIsNotNone(worker.process.poll())

    def test_authorization_and_disabled_audio(self):
        with self.assertRaises(urllib.error.HTTPError) as unauthorized:
            self.request('/start', b'', token='wrong')
        self.assertEqual(unauthorized.exception.code, 403)
        self.start_peer()
        with self.assertRaises(urllib.error.HTTPError) as audio:
            self.request('/input', frame(3))
        self.assertEqual(audio.exception.code, 400)
        with self.assertRaises(urllib.error.HTTPError) as mixed:
            self.request('/input', frame(1, b'12345678') + frame(4))
        self.assertEqual(mixed.exception.code, 400)
        with self.assertRaises(urllib.error.HTTPError) as service:
            self.request('/service', b'{}')
        self.assertEqual(service.exception.code, 400)


    def test_existing_native_worker_snapshot(self):
        binary = Path(__file__).resolve().parent.parent / 'build/dev-cli-web/datapump-worker'
        if not binary.is_file():
            self.skipTest('native worker not built')
        self.bridge.close()
        self.bridge = browser_bridge.start([str(binary), '--simulation'])
        parts = urlsplit(self.bridge.url)
        self.base, self.token = f'{parts.scheme}://{parts.netloc}', parts.fragment
        self.start_peer()
        self.request('/input', frame(1, struct.pack('<II', 800, 600)))
        data = self.request('/output')
        self.assertEqual(data[:4], b'DPW1')
        self.assertEqual(struct.unpack_from('<I', data, 4)[0], 101)
        snapshot = json.loads(data[12:])
        self.assertIn('generation', snapshot)

    def test_browser_file_choice_import_and_stale_cancellation(self):
        binary = Path(__file__).resolve().parent.parent / 'build/dev-cli-web/datapump-worker'
        if not binary.is_file():
            self.skipTest('native worker not built')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'keyfile').write_text('invalid fixture keyfile')
            self.bridge.close()
            relay = Path(__file__).resolve().parent / 'worker_relay.py'
            self.bridge = browser_bridge.start(
                [sys.executable, str(relay), '--file-channel', str(binary)], files=path)
            parts = urlsplit(self.bridge.url)
            self.base, self.token = f'{parts.scheme}://{parts.netloc}', parts.fragment
            reply = json.loads(self.request('/start', b''))
            self.session = reply['session']
            self.assertEqual(reply['files'], str(path))
            script = self.request('/tools/preview.mjs')
            self.assertIn(b"if(!files)throw", script)
            self.assertIn(b"path=await choose(service,signal)", script)
            snapshot = None
            def wait(predicate):
                nonlocal snapshot
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    for kind, packet in browser_bridge._preview.frames(self.request('/output')):
                        if kind == 101:
                            snapshot = json.loads(packet[12:])
                    if snapshot and predicate(snapshot):
                        return snapshot
                self.fail('native GUI snapshot deadline')
            def event(kind, target, flags=0):
                seq = int(snapshot['ack']) + 1
                payload = (struct.pack('<IQQQIII', 1, int(snapshot['generation']), seq,
                                       int(target), kind, flags, 0)
                           + browser_bridge._preview.string('') + browser_bridge._preview.string(''))
                self.request('/input', frame(2, payload))
                wait(lambda s: int(s['ack']) >= seq)
            wait(lambda s: any(c.get('label') == 'Keyfile' for c in s['controls']))
            def open_choice():
                menu = next(c for c in snapshot['controls'] if c.get('label') == 'Keyfile')
                target = next(o['id'] for o in menu['options'] if o['label'] == 'Open keyfile')
                event(4, target)
                wait(lambda s: bool(s.get('service')))
                service = snapshot['service']
                return {'kind': 'open_file', 'path': 'keyfile', 'event': {
                    'version': 1, 'kind': 13, 'generation': snapshot['generation'],
                    'sequence': str(int(snapshot['ack']) + 1), 'target': service['id']}}
            # Cancel one chooser; its old capability cannot authorize an import.
            cancelled = open_choice()
            event(13, cancelled['event']['target'], flags=2)
            with self.assertRaises(urllib.error.HTTPError) as denied:
                self.request('/service', json.dumps(cancelled).encode())
            self.assertEqual(denied.exception.code, 400)
            request = open_choice()
            encoded = json.dumps(request).encode()
            self.request('/service', encoded)
            with self.assertRaises(urllib.error.HTTPError) as stale:
                self.request('/service', encoded)
            self.assertEqual(stale.exception.code, 400)
            wait(lambda s: int(s['ack']) >= int(request['event']['sequence']))

    def test_live_receipt_endpoint_is_authenticated(self):
        self.bridge.close()
        self.bridge = browser_bridge.start([sys.executable, '-u', '-c', LIVE_PEER], live=True)
        parts = urlsplit(self.bridge.url)
        self.base, self.token = f'{parts.scheme}://{parts.netloc}', parts.fragment
        self.assertIn(b'local companion', self.request('/'))
        script = self.request('/tools/preview.mjs')
        self.assertIn(b'/lab-resources', script)
        self.assertIn(b'if(!stopped)report(error)', script)
        self.start_peer()
        self.request('/output')
        result = json.loads(self.request('/lab-resources'))
        self.assertEqual(result['capture_cap_bytes'], 1000)
        self.assertFalse(result['process_baseline_included'])
        with self.assertRaises(urllib.error.HTTPError) as forbidden:
            self.request('/lab-resources', token='wrong')
        self.assertEqual(forbidden.exception.code, 403)
        self.request('/input', frame(1, struct.pack('<II', 800, 600)))
        self.assertEqual(struct.unpack_from('<I', self.request('/output'), 4)[0], 103)

    def test_live_fatal_status_survives_maintenance_until_seen(self):
        # The child ignores EOF; the failed session must reap it even when a
        # browser keeps sending heartbeat requests and never polls output.
        script = ("import struct,sys,time; f=lambda t,p:b'DPW1'+struct.pack('<II',t,len(p))+p; "
                  "sys.stdout.buffer.write(f(101,b'{\"generation\":\"1\",\"ack\":\"0\"}')); "
                  "sys.stdout.buffer.flush(); time.sleep(.1); "
                  "sys.stdout.buffer.write(f(191,b'{\"code\":\"clock\"}')); sys.stdout.buffer.flush(); time.sleep(30)")
        self.bridge.close()
        with mock.patch.object(browser_bridge._preview, 'LEASE', 1.5):
            self.bridge = browser_bridge.start([sys.executable, '-u', '-c', script], live=True)
            parts = urlsplit(self.bridge.url)
            self.base, self.token = f'{parts.scheme}://{parts.netloc}', parts.fragment
            worker = self.start_peer()
            self.request('/output')
            with worker.state:
                self.assertTrue(worker.state.wait_for(lambda: worker.output.error is not None, timeout=5))
            time.sleep(.7)  # At least one maintenance sweep has occurred.
            self.assertIsNotNone(worker.process.poll())
            self.request('/heartbeat', b'')
            with self.assertRaises(urllib.error.HTTPError) as error:
                self.request('/output')
            self.assertEqual(error.exception.code, 410)
            self.assertIn(b'clock qualification failed', error.exception.read())
            for _ in range(4):
                time.sleep(.3)
                try:
                    self.request('/heartbeat', b'')
                except urllib.error.HTTPError:
                    break
            time.sleep(.5)
            self.assertIsNone(self.bridge.server.worker)

    def test_reject_untrusted_host(self):
        with self.assertRaises(urllib.error.HTTPError) as forbidden:
            self.request('/', Host='evil.example')
        self.assertEqual(forbidden.exception.code, 403)

    def test_arguments_rejected(self):
        with self.assertRaises(ValueError):
            browser_bridge.start('ssh example')
        with self.assertRaises(ValueError):
            browser_bridge.start([])


if __name__ == '__main__':
    unittest.main()
