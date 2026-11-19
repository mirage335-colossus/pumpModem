"""Bounded transport + real companion composition, using test-only audio.

This is neither a physical device test nor a receiver/GPU qualification. It
holds remote credit until a finite synthetic capture has stopped, checks each
sample and timestamp while draining, and keeps the control path active.
"""
import json
import os
from pathlib import Path
import select
import struct
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
AUDIO = ROOT / '_zLab' / 'audio'
sys.path.insert(0, str(AUDIO))
import transport

REMOTE = r'''
import json, math, os, select, struct, time
FMT = '<QQQdddIddIddI'
FMT_SIZE = struct.calcsize(FMT)
rate, cutoff = 1500, 3000
expect_eof = bool(int(os.environ.get('LAB_AUDIO_EXPECT_EOF','0')))
def out(kind,payload=b''):
 data=struct.pack('<4sII',b'DPW1',kind,len(payload))+payload
 while data:
  n=os.write(1,data);data=data[n:]
def readn(n):
 outbuf=bytearray()
 while len(outbuf)<n:
  data=os.read(0,n-len(outbuf))
  if not data:raise EOFError('transport closed')
  outbuf.extend(data)
 return bytes(outbuf)
def receive():
 magic,channel,size=struct.unpack('<4sII',readn(12));assert magic==b'DPL1' and channel in (0,1)
 raw=readn(size);magic,kind,length=struct.unpack_from('<4sII',raw);assert magic==b'DPW1' and length==len(raw)-12
 return kind,raw[12:]
def credit():out(207,struct.pack('<QI',1,25))
out(200,struct.pack('<QIIddd',1,rate,0,.0001,0.,0.))
ack_at=None;credit_sent=False;before_credit=0;beats=0;received=0;noise=0x123456789abcdef;mask=(1<<64)-1
origin=None;stopped=False;backlog_notified=False;gui_clock_seen=False
timing_valid=True;payload_valid=True;clock_probes=0;done=False
next_beat=time.monotonic()
while True:
 now=time.monotonic()
 if ack_at is not None and not stopped:
  with open(os.environ['LAB_AUDIO_FIXTURE_TRACE_PATH'], encoding='utf-8') as trace:
   events=[json.loads(line) for line in trace if line.endswith('\n')]
  origins=[event for event in events if event['event']=='origin']
  ends=[event for event in events if event['event']=='stopped']
  if origins:
   assert len(origins)==1 and origins[0]['rate']==rate and origins[0]['quality']==1 and origins[0]['uncertainty']==.001
   origin=origins[0]['utc']
  if ends:
   assert origin is not None and len(ends)==1
   assert ends[0]['frames']==(1500 if expect_eof else cutoff)
   assert ends[0]['reason']==('early_eof' if expect_eof else 'callback_stop')
   stopped=True;backlog_notified=True;out(101,json.dumps({'backlog_ready':True}).encode())
 if not done and now>=next_beat:
  out(101,json.dumps({'beat':beats,'backlogged':stopped and not credit_sent,'draining':credit_sent}).encode());beats+=1;next_beat=now+.1
 if stopped and gui_clock_seen and not credit_sent:
  credit_sent=True;credit()
 ready,_,_=select.select([0],[],[],.05)
 if not ready:continue
 kind,p=receive()
 if kind==15:
  nonce,echo=struct.unpack('<Qd',p);wall=time.time();out(108,struct.pack('<Qddd',nonce,echo,wall,wall));clock_probes+=1
  if nonce==0x1a2b3c4d:
   assert stopped and backlog_notified and not credit_sent
   gui_clock_seen=True
 elif kind==220:
  stream,logical,hardware=struct.unpack_from('<QII',p);assert stream==1 and logical==rate and hardware==rate
  out(208,struct.pack('<QQ',1,cutoff))
 elif kind==229:
  stream,status,n=struct.unpack_from('<QII',p);assert stream==1 and status==1 and len(p)==16+n
  ack_at=time.monotonic()
 elif kind==221:
  if not credit_sent:before_credit+=1
  stream,first,seconds,fraction,unc,spf,quality,rate_unc,extra,declared,phase,floor,count=struct.unpack_from(FMT,p)
  assert stream==1 and first==received and len(p)==FMT_SIZE+4*count and quality!=0 and unc>=0 and abs(spf-1/rate)<1e-12
  assert origin is not None and quality==1 and rate_unc==0 and declared==0 and phase==0 and floor==0
  assert abs(extra-(first%75)/rate)<1e-12
  # The fixture supplies .001 s; the transport must add its declared .5 s
  # inter-host and .001 s local-UTC radii, preserving sample dates.
  assert .502 <= unc <= .50200001
  stamp=seconds+fraction
  timing_valid &= abs(stamp-(origin+first/rate))<2e-6
  for (sample,) in struct.iter_unpack('<f',p[FMT_SIZE:]):
   noise ^= (noise<<13)&mask;noise ^= noise>>7;noise ^= (noise<<17)&mask;noise &= mask
   want=(float(noise&0xffff)/32767.5-1.)*1e-4
   want=struct.unpack('<f',struct.pack('<f',want))[0]
   payload_valid &= sample==want
  received+=count;credit()
 elif kind==222:
  stream,status,n=struct.unpack_from('<QII',p);assert stream==1 and len(p)==16+n
  expected=1500 if expect_eof else cutoff
  assert received==expected and status==(1 if expect_eof else 0)
  assert before_credit==0 and beats>=3 and clock_probes>=2 and gui_clock_seen and timing_valid and payload_valid
  out(201,struct.pack('<Q',1));out(101,json.dumps({'result':True,'frames':received,'end_status':status,'beats':beats,'clock_probes':clock_probes}).encode())
  out(104,struct.pack('<I',0));done=True
 elif kind in (1,2):pass
 else:raise AssertionError('unexpected frame '+str(kind))
'''


class ActualCompanionComposition(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='pump-lab-composition-')
        cls.fixture = str(Path(cls.temp.name) / 'fixture')
        command = ['g++', '-std=c++20', '-O2', '-DNDEBUG', '-pthread', '-Iinclude',
                   '-I_zLab/audio', '_zLab/audio/local.cpp', '_zLab/audio/local_fixture.cpp',
                   '_zLab/audio/local_fixture_resources.cpp', '_zLab/audio/local_main.cpp',
                   'src/host/protocol.cpp', '-o', cls.fixture]
        try:
            subprocess.run(command, cwd=ROOT, check=True, capture_output=True, timeout=120)
        except FileNotFoundError as error:
            cls.temp.cleanup()
            raise unittest.SkipTest('C++ compiler unavailable') from error
        except BaseException:
            cls.temp.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_episode(self, early_eof):
        local = [self.fixture, '--device', 'fixture', '--capture-bytes', '1048576',
                 '--playback-bytes', '65536', '--capture-frames', '6000',
                 '--playback-prebuffer-frames', '4096']
        command = [sys.executable, str(AUDIO / 'transport.py'), '--max-clock-offset', '.5',
                   '--local-utc-uncertainty', '.001', '--remote-clock-drift-ppm', '10',
                   '--remote-json', json.dumps([sys.executable, '-u', '-c', REMOTE]),
                   '--local-json', json.dumps(local), '--debug-child-stderr']
        trace_path = Path(self.temp.name) / ('early-eof.jsonl' if early_eof else 'cutoff.jsonl')
        trace_path.write_text('')
        trace_path.chmod(0o600)
        env = dict(os.environ, LAB_AUDIO_EXPECT_EOF='1' if early_eof else '0',
                   LAB_AUDIO_FIXTURE_TRACE_PATH=str(trace_path),
                   LAB_AUDIO_FIXTURE_PREHISTORY_SECONDS='0')
        if early_eof:
            env['LAB_AUDIO_FIXTURE_EOF_FRAMES'] = '1500'
        else:
            env.pop('LAB_AUDIO_FIXTURE_EOF_FRAMES', None)
        proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=env, cwd=ROOT, bufsize=0)
        try:
            nonce = 0x1a2b3c4d
            deadline = time.monotonic() + 15
            result = None
            backlog_ready = False
            backlog_beats = 0
            got_clock = False
            ping_sent = False

            def read_exact(count):
                result = bytearray()
                while len(result) < count:
                    remaining = deadline-time.monotonic()
                    self.assertGreater(remaining, 0, 'composition did not finish')
                    ready, _, _ = select.select([proc.stdout], [], [], remaining)
                    self.assertTrue(ready, 'composition did not finish')
                    data = os.read(proc.stdout.fileno(), count-len(result))
                    self.assertTrue(data, 'transport exited unexpectedly')
                    result.extend(data)
                return bytes(result)

            while time.monotonic() < deadline:
                header = read_exact(12)
                magic, kind, size = struct.unpack('<4sII', header)
                self.assertEqual(magic, b'DPW1')
                self.assertLessEqual(size, transport.MAX_PAYLOAD)
                payload = read_exact(size)
                if kind == 101:
                    snapshot = json.loads(payload)
                    if snapshot.get('backlog_ready'):
                        backlog_ready = True
                    if snapshot.get('backlogged'):
                        self.assertTrue(backlog_ready)
                        backlog_beats += 1
                        if backlog_beats == 3:
                            # This clock probe is deliberately issued only once
                            # physical capture has stopped with credit withheld.
                            ping = transport.envelope(0, transport.frame(15, struct.pack('<Qd', nonce, time.time())))
                            proc.stdin.write(ping)
                            proc.stdin.flush()
                            ping_sent = True
                    if snapshot.get('result'):
                        result = snapshot
                elif kind == 108 and len(payload) == 32:
                    got_clock |= struct.unpack_from('<Q', payload)[0] == nonce
                elif kind == 191:
                    self.fail('transport fatal status: ' + payload.decode())
                elif kind == 104:
                    break
            self.assertIsNotNone(result)
            self.assertGreaterEqual(backlog_beats, 3)
            self.assertTrue(ping_sent)
            self.assertTrue(got_clock, 'GUI clock probe stalled during withheld capture credit')
            self.assertEqual(result['frames'], 1500 if early_eof else 3000)
            self.assertEqual(result['end_status'], 1 if early_eof else 0)
            proc.stdin.close()
            self.assertEqual(proc.wait(timeout=6), 0, proc.stderr.read().decode())
        except BaseException as error:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            diagnostic = proc.stderr.read(8192).decode('utf-8', 'replace')
            raise AssertionError(f'{error}\nBounded child diagnostic: {diagnostic}') from error
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdout.close()
            proc.stderr.close()

    def test_cutoff_then_drain_without_losing_samples(self):
        self.run_episode(False)

    def test_early_eof_remains_incomplete(self):
        self.run_episode(True)


if __name__ == '__main__':
    unittest.main()
