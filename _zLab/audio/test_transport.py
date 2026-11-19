import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
import unittest
import signal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import transport


class ClockTests(unittest.TestCase):
    def test_relative_bound_and_clock_step(self):
        gate = transport.ClockGate(.1, .01, 10)
        request = gate.request(wall=1000.0, mono=10.0)
        nonce, stamp = struct.unpack('<Qd', request[12:])
        reply = struct.pack('<Qddd', nonce, stamp, 1000.02, 1000.03)
        self.assertTrue(gate.response(reply, wall=1000.06, mono=10.06))
        self.assertLessEqual(gate.bound, .1)
        gate.request(wall=1001.0, mono=11.0)
        nonce, stamp = gate.pending[:2]
        with self.assertRaisesRegex(transport.TransportError, 'stepped'):
            gate.response(struct.pack('<Qddd', nonce, stamp, 1001.02, 1001.03),
                          wall=1001.07, mono=11.06)

    def test_large_offset_timeout_and_invalid_bounds_fail(self):
        for value in (0, -1, float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                transport.ClockGate(value, .01, 10)
        gate = transport.ClockGate(.01, .01, 10)
        gate.request(wall=1.0, mono=1.0)
        nonce, stamp = gate.pending[:2]
        with self.assertRaisesRegex(transport.TransportError, 'exceeds'):
            gate.response(struct.pack('<Qddd', nonce, stamp, 1.3, 1.3), wall=1.1, mono=1.1)
        with self.assertRaisesRegex(transport.TransportError, 'timed out'):
            gate.check_pending(12.0)

    def test_uncertainty_is_charged_and_playback_budget_reduced(self):
        gate = transport.ClockGate(.1, .05, 10)
        pcm = transport.frame(221, struct.pack('<QQQdd', 1, 0, 10, .5, .02))
        charged = gate.capture_pcm(pcm)
        self.assertGreaterEqual(struct.unpack_from('<d', charged, 44)[0], .17)
        opening = transport.frame(202, struct.pack('<QIIIdd', 1, 48000, 0, 0, 1., .4))
        reduced = gate.playback_open(opening)
        self.assertAlmostEqual(struct.unpack_from('<d', reduced, 40)[0], .25)
        with self.assertRaisesRegex(transport.TransportError, 'allowance'):
            gate.playback_open(transport.frame(202, struct.pack('<QIIIdd', 1, 1, 0, 0, 1., .1)))

    def test_clock_step_between_probes_and_remote_drift_fail(self):
        gate = transport.ClockGate(.1, .01, 100)
        request = gate.request(wall=1000.0, mono=10.0)
        nonce, stamp = struct.unpack('<Qd', request[12:])
        gate.response(struct.pack('<Qddd', nonce, stamp, 1000.02, 1000.03),
                      wall=1000.06, mono=10.06)
        gate.check_continuity(wall=1001.06, mono=11.06)
        with self.assertRaisesRegex(transport.TransportError, 'between clock exchanges'):
            gate.check_continuity(wall=1001.08, mono=11.06)
        with self.assertRaisesRegex(transport.TransportError, 'drift exhausts'):
            gate.check_continuity(wall=2000.06, mono=1010.06)
        # A new probe must not silently replace the old anchor after a step.
        with self.assertRaisesRegex(transport.TransportError, 'between clock exchanges'):
            gate.request(wall=1005.0, mono=11.0)
        # A step after request but before response must also be rejected before
        # the new response can establish another anchor.
        request = gate.request(wall=1001.06, mono=11.06)
        nonce, stamp = struct.unpack('<Qd', request[12:])
        with self.assertRaisesRegex(transport.TransportError, 'between clock exchanges'):
            gate.response(struct.pack('<Qddd', nonce, stamp, 1001.08, 1001.09),
                          wall=1005.12, mono=11.12)

    def test_remote_drift_is_charged_during_probe(self):
        gate = transport.ClockGate(.12, .01, 3600)
        request = gate.request(wall=1000., mono=0.)
        nonce, stamp = struct.unpack('<Qd', request[12:])
        # Endpoints alone lie within ±.1 s, but the 10 s probe can include
        # 36 ms of declared pod-clock drift and exceeds the .12 s bound.
        with self.assertRaisesRegex(transport.TransportError, 'exceeds'):
            gate.response(struct.pack('<Qddd', nonce, stamp, 1000.1, 1009.9),
                          wall=1010., mono=10.)

    def test_bounded_queue_and_large_envelope(self):
        output = transport.Output(1)
        output.put(b'x' * transport.QUEUE_LIMIT)
        with self.assertRaisesRegex(transport.TransportError, 'bound'):
            output.put(b'x')
        inner = transport.frame(2, b'x' * transport.MAX_PAYLOAD)
        kind, block = transport.frame_info(transport.envelope(1, inner), b'DPL1')
        self.assertEqual(kind, 1)
        self.assertEqual(transport.frame_info(block)[0], 2)
        small = transport.Output(1)
        for _ in range(transport.QUEUE_ITEMS):
            small.put(b'x')
        with self.assertRaisesRegex(transport.TransportError, 'bound'):
            small.put(b'x')

    def test_fatal_status_uses_supplied_output_descriptor(self):
        reader, writer = os.pipe()
        try:
            transport._fatal_status(ValueError('private details'), writer)
            block = os.read(reader, 4096)
            kind, payload = transport.frame_info(block)
            self.assertEqual(kind, 191)
            self.assertEqual(json.loads(payload), {'code': 'configuration'})
            self.assertNotIn(b'private details', block)
        finally:
            os.close(reader)
            os.close(writer)


REMOTE = r'''
import json,os,struct,time
def out(k,p=b''): os.write(1,struct.pack('<4sII',b'DPW1',k,len(p))+p)
def readn(n):
 b=b''
 while len(b)<n:
  x=os.read(0,n-len(b))
  if not x: raise EOFError()
  b+=x
 return b
out(200,struct.pack('<QIIddd',1,48000,0,0.0001,0.,0.))
cap_done=False
while True:
 h=readn(12); magic,chan,n=struct.unpack('<4sII',h); raw=readn(n)
 assert magic==b'DPL1' and chan==0
 _,k,size=struct.unpack('<4sII',raw[:12]); p=raw[12:]; assert len(p)==size
 if k==15:
  nonce,echo=struct.unpack('<Qd',p); now=time.time()
  out(108,struct.pack('<Qddd',nonce,echo,now,now))
 elif k==221:
  uncertainty=struct.unpack_from('<d',p,32)[0]
  out(101,json.dumps({'capture_uncertainty':uncertainty}).encode())
  out(202,struct.pack('<QIIIdddddd',2,48000,0,0,1.,1.,0.,.0001,0.,0.))
  out(203,struct.pack('<Qd',2,time.time()+10))
 elif k==222:
  cap_done=True; out(201,struct.pack('<Q',1))
 elif k==224:
  assert cap_done
  out(104,struct.pack('<I',0))
'''

LOCAL = r'''
import os,struct
def out(k,p=b''): os.write(1,struct.pack('<4sII',b'DPW1',k,len(p))+p)
def readn(n):
 b=b''
 while len(b)<n:
  x=os.read(0,n-len(b))
  if not x: raise EOFError()
  b+=x
 return b
scheduled=False
stopped=False
while True:
 h=readn(12); magic,k,n=struct.unpack('<4sII',h); p=readn(n); assert magic==b'DPW1'
 if k==200:
  out(220,struct.pack('<QIIdQId',1,48000,48000,20000.,100000,2,.001))
  out(221,struct.pack('<QQQdd',1,0,100,.5,.02))
  out(222,struct.pack('<QI',1,0))
 elif k==202:
  remaining=struct.unpack_from('<d',p,28)[0]; assert .6<remaining<.8
 elif k==201: stopped=True
 elif k==203: scheduled=True
 if stopped and scheduled:
  out(224,struct.pack('<QI',2,1)); scheduled=False
'''


class IntegrationTests(unittest.TestCase):
    def start(self, remote=REMOTE, local=LOCAL, report=False, debug=False, summary=False):
        command = [sys.executable, str(Path(transport.__file__)), '--max-clock-offset', '.2',
                   '--local-utc-uncertainty', '.05', '--remote-clock-drift-ppm', '10', '--remote-json',
                   json.dumps([sys.executable, '-u', '-c', remote]), '--local-json',
                   json.dumps([sys.executable, '-u', '-c', local])]
        if report:
            command.append('--report-resources')
        if debug:
            command.append('--debug-child-stderr')
        if summary:
            command.append('--debug-audio-summary')
        return subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, start_new_session=True)

    def readframe(self, proc):
        import select
        parts = bytearray()
        deadline = time.monotonic() + 8
        while len(parts) < 12:
            ready, _, _ = select.select([proc.stdout], [], [], max(0, deadline-time.monotonic()))
            self.assertTrue(ready, 'timed out waiting for output')
            data = os.read(proc.stdout.fileno(), 12-len(parts))
            self.assertTrue(data, 'transport closed before output')
            parts.extend(data)
        _, kind, size = struct.unpack('<4sII', parts)
        while len(parts) < 12+size:
            data = os.read(proc.stdout.fileno(), 12+size-len(parts))
            self.assertTrue(data)
            parts.extend(data)
        return kind, bytes(parts[12:])

    def test_routing_gates_uncertainty_and_schedule(self):
        proc = self.start()
        try:
            kind, payload = self.readframe(proc)
            self.assertEqual(kind, 101)
            self.assertGreaterEqual(json.loads(payload)['capture_uncertainty'], .27)
            kind, _ = self.readframe(proc)
            self.assertEqual(kind, 104)
            proc.stdin.close()
            self.assertEqual(proc.wait(timeout=6), 0, proc.stderr.read().decode())
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdout.close()
            proc.stderr.close()

    def test_resource_status_reports_actual_format_separately(self):
        proc = self.start(report=True)
        try:
            kind, payload = self.readframe(proc)
            self.assertEqual(kind, 190)
            status = json.loads(payload)
            self.assertIsNone(status['capture_cap_bytes'])
            self.assertIsNone(status['capture_workspace_bytes'])
            self.assertFalse(status['process_baseline_included'])
            kind, payload = self.readframe(proc)
            self.assertEqual(kind, 190)
            status = json.loads(payload)
            self.assertEqual(status['capture_workspace_bytes'], 100000)
            self.assertEqual(status['companion_shared_control_limit_bytes'], 512 * 1024)
            self.assertEqual(status['browser_host_allowance_bytes'], 512 * 1024 * 1024)
            self.assertEqual(status['companion_worker_stack_count'], 3)
            self.assertIsNone(status['companion_worker_stack_bytes'])
            self.assertTrue(status['workspace_includes_buffer_cap'])
            self.assertFalse(status['companion_control_included_in_browser_allowance'])
            self.assertEqual(self.readframe(proc)[0], 101)
            self.assertEqual(self.readframe(proc)[0], 104)
            proc.stdin.close()
            self.assertEqual(proc.wait(timeout=6), 0, proc.stderr.read().decode())
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdout.close()
            proc.stderr.close()

    def test_audio_summary_counts_received_and_forwarded_frames(self):
        proc = self.start(summary=True)
        try:
            self.assertEqual(self.readframe(proc)[0], 101)
            self.assertEqual(self.readframe(proc)[0], 104)
            proc.stdin.close()
            self.assertEqual(proc.wait(timeout=6), 0)
            lines = proc.stderr.read().decode().splitlines()
            prefix = 'lab audio frame counts: '
            summary = json.loads(next(line[len(prefix):] for line in lines if line.startswith(prefix)))
            self.assertEqual(summary['counts']['local_221']['count'], 1)
            self.assertEqual(summary['counts']['to_remote_221']['count'], 1)
            self.assertEqual(summary['counts']['remote_203']['count'], 1)
            self.assertEqual(summary['counts']['to_local_203']['count'], 1)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdout.close()
            proc.stderr.close()

    def test_local_early_eof_is_failure_not_completion(self):
        proc = self.start(local='pass')
        try:
            self.assertNotEqual(proc.wait(timeout=6), 0)
            self.assertIn(b'local child ended before receiver completion', proc.stderr.read())
            self.assertEqual(proc.stdout.read(), b'')
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()

    def test_truncated_frame_identifies_source_and_diagnostic_is_bounded(self):
        local = ("import os; os.write(2,b'x'*10000+b'END-DIAGNOSTIC'); "
                 "os.write(1,b'DPW1'+(221).to_bytes(4,'little')+(100).to_bytes(4,'little')+b'x')")
        proc = self.start(remote='import time; time.sleep(10)', local=local, debug=True)
        try:
            self.assertNotEqual(proc.wait(timeout=6), 0)
            diagnostic = proc.stderr.read()
            self.assertIn(b'local input: truncated input at EOF', diagnostic)
            self.assertIn(b'END-DIAGNOSTIC', diagnostic)
            self.assertLess(len(diagnostic), 4500)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()

    def test_gui_receives_only_fixed_fatal_category(self):
        proc = self.start(remote='import time; time.sleep(10)',
                          local="import sys; print('FAKE-SECRET',file=sys.stderr)", report=True)
        try:
            status = []
            while True:
                kind, payload = self.readframe(proc)
                if kind == 191:
                    status.append(payload)
                    break
                self.assertEqual(kind, 190)
            self.assertEqual(json.loads(status[0]), {'code': 'local_audio'})
            self.assertNotEqual(proc.wait(timeout=6), 0)
            self.assertNotIn(b'FAKE-SECRET', proc.stderr.read())
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()

    def test_invalid_configuration_emits_fixed_status(self):
        cmd = [sys.executable, str(Path(transport.__file__)), '--max-clock-offset', '0',
               '--local-utc-uncertainty', '.05', '--remote-clock-drift-ppm', '10',
               '--remote-json', '[]', '--local-json', '[]', '--report-resources']
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            kind, payload = self.readframe(proc)
            self.assertEqual(kind, 191)
            self.assertEqual(json.loads(payload), {'code': 'configuration'})
            self.assertNotEqual(proc.wait(timeout=6), 0)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()

    def test_fatal_status_never_splices_into_partly_sent_gui_frame(self):
        remote = ("import os,struct,time; p=b'x'*1048576; "
                  "os.write(1,struct.pack('<4sII',b'DPW1',101,len(p))+p); time.sleep(10)")
        local = 'import time; time.sleep(.4)'
        proc = self.start(remote=remote, local=local, report=True)
        try:
            # Drain the initial resource receipt, then deliberately stop
            # reading while the large worker snapshot fills the pipe.
            self.assertEqual(self.readframe(proc)[0], 190)
            self.assertNotEqual(proc.wait(timeout=6), 0)
            remaining = proc.stdout.read()
            self.assertTrue(remaining.startswith(b'DPW1'))
            self.assertNotIn(struct.pack('<4sI', b'DPW1', 191), remaining)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()

    def test_unknown_local_stream_fails(self):
        remote = 'import time; time.sleep(10)'
        local = "import os,struct,time; p=struct.pack('<Q',123); os.write(1,struct.pack('<4sII',b'DPW1',221,len(p))+p); time.sleep(10)"
        proc = self.start(remote=remote, local=local)
        try:
            self.assertNotEqual(proc.wait(timeout=6), 0)
            self.assertIn(b'unknown local capture stream', proc.stderr.read())
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()

    def test_waveform_before_accepted_cutoff_fails(self):
        remote = REMOTE.replace("out(203,struct.pack('<Qd',2,time.time()+10))",
                                "out(204,struct.pack('<QQIf',2,0,1,0.0))")
        proc = self.start(remote=remote)
        try:
            self.assertNotEqual(proc.wait(timeout=6), 0)
            self.assertIn(b'playback PCM before an accepted live capture cutoff', proc.stderr.read())
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()

    def test_gui_cannot_inject_audio_or_trusted_file_on_ui_channel(self):
        for kind in (11, 221):
            proc = self.start(remote='import time; time.sleep(10)', local='import time; time.sleep(10)')
            try:
                proc.stdin.write(transport.envelope(0, transport.frame(kind, b'')))
                proc.stdin.flush()
                self.assertNotEqual(proc.wait(timeout=6), 0)
                self.assertIn(b'invalid GUI authority channel', proc.stderr.read())
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
                proc.stdin.close()
                proc.stdout.close()
                proc.stderr.close()

    def test_trusted_capture_cutoff_requires_ack(self):
        remote = r'''
import os,struct,time
def out(k,p=b''): os.write(1,struct.pack('<4sII',b'DPW1',k,len(p))+p)
def readn(n):
 b=b''
 while len(b)<n:
  x=os.read(0,n-len(b))
  if not x: raise EOFError()
  b+=x
 return b
out(200,struct.pack('<QIIddd',1,48000,0,0.0001,0.,0.))
while True:
 h=readn(12); _,_,n=struct.unpack('<4sII',h); raw=readn(n); _,k,_=struct.unpack('<4sII',raw[:12]); p=raw[12:]
 if k==15:
  nonce,echo=struct.unpack('<Qd',p); now=time.time(); out(108,struct.pack('<Qddd',nonce,echo,now,now))
 elif k==220: out(208,struct.pack('<QQ',1,500))
 elif k==229:
  assert struct.unpack('<QII',p)==(1,1,0); out(104,struct.pack('<I',0))
'''
        local = r'''
import os,struct
def out(k,p=b''): os.write(1,struct.pack('<4sII',b'DPW1',k,len(p))+p)
def readn(n):
 b=b''
 while len(b)<n:
  x=os.read(0,n-len(b))
  if not x: raise EOFError()
  b+=x
 return b
while True:
 h=readn(12); _,k,n=struct.unpack('<4sII',h); p=readn(n)
 if k==200: out(220,struct.pack('<QIIdQId',1,48000,48000,20000.,100000,2,.001))
 elif k==208:
  assert struct.unpack('<QQ',p)==(1,500); out(229,struct.pack('<QII',1,1,0))
'''
        proc = self.start(remote=remote, local=local)
        try:
            kind, _ = self.readframe(proc)
            self.assertEqual(kind, 104)
            proc.stdin.close()
            self.assertEqual(proc.wait(timeout=6), 0, proc.stderr.read().decode())
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdout.close()
            proc.stderr.close()

    def test_signal_cancels_children(self):
        proc = self.start(remote='import time; time.sleep(10)', local='import time; time.sleep(10)')
        try:
            time.sleep(.1)
            proc.send_signal(signal.SIGTERM)
            self.assertEqual(proc.wait(timeout=6), 0)
            self.assertEqual(proc.stdout.read(), b'')
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()

    def test_stalled_gui_output_fails_instead_of_growing_without_bound(self):
        remote = ("import os,struct,time; p=b'x'*131072; "
                  "f=struct.pack('<4sII',b'DPW1',101,len(p))+p; "
                  "\nwhile True: os.write(1,f)")
        local = 'import time; time.sleep(10)'
        wrapper = ("import sys;sys.path.insert(0,sys.argv[1]);import transport;"
                   "transport.run([sys.executable,'-u','-c',sys.argv[2]],"
                   "[sys.executable,'-u','-c',sys.argv[3]],.2,.05,10,stall_timeout=.25)")
        proc = subprocess.Popen([sys.executable, '-c', wrapper, str(Path(transport.__file__).parent),
                                 remote, local], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, start_new_session=True)
        try:
            self.assertNotEqual(proc.wait(timeout=6), 0)
            self.assertIn(b'transport output stalled', proc.stderr.read())
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdin.close()
            proc.stdout.close()
            proc.stderr.close()

    @unittest.skipUnless(os.environ.get('LAB_AUDIO_WORKER'), 'set LAB_AUDIO_WORKER to a built lab worker')
    def test_native_worker_snapshot_through_live_relay(self):
        worker = Path(os.environ['LAB_AUDIO_WORKER'])
        relay = Path(transport.__file__).resolve().parents[1] / 'worker_relay.py'
        command = [sys.executable, str(Path(transport.__file__)), '--max-clock-offset', '.2',
                   '--local-utc-uncertainty', '.05', '--remote-clock-drift-ppm', '10', '--remote-json',
                   json.dumps([sys.executable, str(relay), '--live', '--file-channel', str(worker)]),
                   '--local-json', json.dumps([sys.executable, '-u', '-c', 'import time; time.sleep(20)'])]
        proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, start_new_session=True)
        try:
            proc.stdin.write(transport.envelope(0, transport.frame(1, struct.pack('<II', 1024, 768))))
            proc.stdin.flush()
            kind, payload = self.readframe(proc)
            self.assertEqual(kind, 101)
            self.assertIsInstance(json.loads(payload), dict)
            proc.stdin.close()
            self.assertEqual(proc.wait(timeout=8), 0, proc.stderr.read().decode())
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdout.close()
            proc.stderr.close()


if __name__ == '__main__':
    unittest.main()
