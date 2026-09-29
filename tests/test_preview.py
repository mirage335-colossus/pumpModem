#!/usr/bin/env python3
"""Development host tests. Optional --worker runs the real native integration."""
import argparse
import http.client
import importlib.util
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('preview', ROOT/'tools/preview.py')
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)
WORKER = None


def begin(target, size):
    return struct.pack('<Q', target) + preview.string('received.bin') + struct.pack('<Q', size)


class HostTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_frame_bounds_and_atomic_validation(self):
        data = preview.frame(15, b'1234')
        self.assertEqual(len(preview.frames(data+data)), 2)
        for malformed in (data[:-1], b'BAD!'+data[4:], b'DPW1'+struct.pack('<II', 1, preview.MAX_FRAME+1)):
            with self.assertRaises(preview.PreviewError):
                preview.frames(malformed)
        output = preview.OutputQueue(limit=len(data))
        output.put(data)
        with self.assertRaises(preview.PreviewError):
            output.put(data)
        self.assertEqual(output.get(), data)
        output.close('gone')
        with self.assertRaisesRegex(preview.PreviewError, 'gone'):
            output.get()

    @unittest.skipUnless(sys.platform.startswith('linux'), 'descriptor file policy belongs to Linux worker')
    def test_host_paths_and_pinned_input(self):
        directory = self.root/'allowed';directory.mkdir()
        (self.root/'outside').write_bytes(b'secret')
        (directory/'inside').write_bytes(b'original')
        (directory/'link').symlink_to(self.root/'outside')
        (directory/'dirlink').symlink_to(self.root, target_is_directory=True)
        os.mkfifo(directory/'fifo')
        scope = preview.HostFiles(directory)
        try:
            for name in ('../outside', str(self.root/'outside'), 'link', 'dirlink/outside', 'fifo'):
                with self.assertRaises((preview.PreviewError,OSError)):
                    scope.read(name)
            source, _, _ = scope.read('inside')
            (directory/'inside').unlink();(directory/'inside').symlink_to(self.root/'outside')
            with source:
                self.assertEqual(source.read(), b'original')
            # Rename the chosen parent after opening; a replacement symlink
            # cannot redirect operations performed through the held descriptor.
            (directory/'nested').mkdir();(directory/'nested/data').write_bytes(b'pinned')
            parent, leaf = scope.parent('nested/data')
            (directory/'nested').rename(directory/'moved')
            (directory/'nested').symlink_to(self.root, target_is_directory=True)
            try:
                fd=os.open(leaf,os.O_RDONLY|os.O_NOFOLLOW,dir_fd=parent)
                with os.fdopen(fd,'rb') as stream:self.assertEqual(stream.read(),b'pinned')
            finally:os.close(parent)
        finally:
            scope.close()

    @unittest.skipUnless(sys.platform.startswith('linux'), 'native worker file policy')
    def test_exports_complete_or_preserve_destination(self):
        scope = preview.HostFiles(self.root)
        try:
            job = scope.save('ok.bin',7)
            job.accept(105,begin(7,3));job.accept(106,struct.pack('<QQI',7,0,3)+b'abc')
            self.assertFalse((self.root/'ok.bin').exists())
            job.accept(107,struct.pack('<Q',7)+preview.string(''))
            self.assertIsNone(job.error);self.assertEqual((self.root/'ok.bin').read_bytes(),b'abc')
            with self.assertRaises(preview.PreviewError):scope.save('ok.bin',8)
            job = scope.save('failed.bin',9)
            job.accept(107,struct.pack('<Q',9)+preview.string('controller rejected save'))
            self.assertIn('rejected',job.error);self.assertFalse((self.root/'failed.bin').exists())
            job = scope.save('raced.bin',10)
            (self.root/'raced.bin').symlink_to(self.root/'ok.bin')
            job.accept(105,begin(10,0));job.accept(107,struct.pack('<Q',10)+preview.string(''))
            self.assertIsNotNone(job.error);self.assertEqual((self.root/'ok.bin').read_bytes(),b'abc')
            self.assertFalse(list(self.root.glob('*.partial')))
            job = scope.save('short.bin',11)
            job.accept(105,begin(11,10));job.accept(107,struct.pack('<Q',11)+preview.string(''))
            self.assertIn('Incomplete',job.error);self.assertFalse((self.root/'short.bin').exists())
        finally:scope.close()

    def test_missing_outputs_have_build_instructions(self):
        result=subprocess.run([sys.executable,'-B',str(ROOT/'tools/preview.py'),'wasm','--build-dir',str(self.root)],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0);self.assertIn('./build.sh --wasm-sdk',result.stderr)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'native worker file policy')
    def test_export_uses_open_file_and_always_signals_completion(self):
        scope=preview.HostFiles(self.root)
        try:
            job=scope.save('result',7)
            job.accept(105,begin(7,3));job.accept(106,struct.pack('<QQI',7,0,3)+b'abc')
            (self.root/job.temporary).rename(self.root/'moved')
            (self.root/job.temporary).symlink_to(self.root/'unrelated')
            job.finish()
            self.assertIsNone(job.error);self.assertEqual((self.root/'result').read_bytes(),b'abc')
            self.assertTrue((self.root/job.temporary).is_symlink())
            job=scope.save('failed-close',8)
            original=job.file.close
            def fail_close():
                original();raise OSError('injected close failure')
            with patch.object(job.file,'close',fail_close):job.finish('cancelled')
            self.assertTrue(job.done.is_set())
            with self.assertRaises(OSError):os.fstat(job.parent)
            self.assertFalse((self.root/job.temporary).exists())
        finally:scope.close()

    def test_wasm_serves_only_built_page(self):
        page=self.root/'datapump-wasm.html';page.write_bytes(b'<!doctype html><p>compiled fixture</p>')
        server=preview.PreviewServer(SimpleNamespace(mode='wasm',page=page))
        thread=threading.Thread(target=server.serve_forever);thread.start()
        try:
            for path,expected in [('/',200),('/datapump-wasm.html',200),('/../secret',404),('/web/style.css',404)]:
                connection=http.client.HTTPConnection('127.0.0.1',server.server_port,timeout=5)
                connection.request('GET',path);response=connection.getresponse();data=response.read();connection.close()
                self.assertEqual(response.status,expected)
                if expected==200:self.assertEqual(data,page.read_bytes())
        finally:server.shutdown();server.server_close();thread.join()


class NativeTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.server=preview.PreviewServer(SimpleNamespace(mode='worker',worker=WORKER,simulation=True,files=self.root))
        self.thread=threading.Thread(target=self.server.serve_forever);self.thread.start()
        self.maintenance=threading.Thread(target=self.server.maintain);self.maintenance.start()
        self.session='';self.snapshot=None
        code,data=self.request('/start',b'');self.assertEqual(code,200,data)
        self.session=json.loads(data)['session'];self.process=self.server.worker.process
        self.wait(lambda:self.snapshot is not None)

    def tearDown(self):
        self.server.stopping.set();self.server.stop_worker(self.session)
        self.server.shutdown();self.server.server_close();self.thread.join();self.maintenance.join()
        self.temp.cleanup()

    def request(self,path,data=None,token=True,extra=None):
        headers={'X-Preview-Session':self.session}
        if token:headers['X-Preview-Token']=self.server.token
        headers.update(extra or {})
        connection=http.client.HTTPConnection('127.0.0.1',self.server.server_port,timeout=65)
        connection.request('GET' if data is None else 'POST',path,data,headers)
        response=connection.getresponse();result=(response.status,response.read());connection.close();return result

    def read(self):
        code,data=self.request('/output');self.assertEqual(code,200,data)
        for kind,packet in preview.frames(data):
            if kind==101:self.snapshot=json.loads(packet[12:])
            elif kind==103:raise AssertionError(packet[16:].decode())
        return preview.frames(data)

    def wait(self,predicate,seconds=60):
        deadline=time.monotonic()+seconds
        while not predicate():
            self.assertLess(time.monotonic(),deadline,'native progress timeout');self.read()

    def control(self,label):
        return next(c for c in self.snapshot['controls'] if c['label']==label)

    def event(self,kind,target,value='',flags=0):
        sequence=int(self.snapshot['ack'])+1
        payload=struct.pack('<IQQQIII',1,int(self.snapshot['generation']),sequence,int(target),kind,flags,0)+preview.string(value)+preview.string('')
        code,data=self.request('/input',preview.frame(2,payload));self.assertEqual(code,200,data)
        self.wait(lambda:int(self.snapshot['ack'])>=sequence)

    def service(self,path):
        service=self.snapshot['service'];sequence=int(self.snapshot['ack'])+1
        event={'version':1,'kind':13,'generation':self.snapshot['generation'],'sequence':str(sequence),'target':service['id']}
        request=json.dumps({'event':event,'kind':service['kind'],'path':path})
        code,data=self.request('/service',request);self.assertEqual(code,200,data)
        self.assertGreaterEqual(int(self.server.worker.snapshot['ack']),sequence)
        self.wait(lambda:int(self.snapshot['ack'])>=sequence)
        self.assertEqual(self.request('/service',request)[0],400,'duplicate service must be rejected')

    def test_auth_frames_clock_and_tab_ownership(self):
        self.assertEqual(self.request('/start',b'',token=False)[0],403)
        self.assertEqual(self.request('/start',b'',extra={'Origin':'https://invalid.example'})[0],403)
        self.assertEqual(self.request('/start',b'',extra={'Host':'invalid.example'})[0],403)
        self.assertEqual(self.request('/start',b'')[0],409)
        self.assertEqual(self.request('/input',b'broken')[0],400)
        self.assertEqual(self.request('/input',preview.frame(10))[0],400)
        t0=time.time();self.assertEqual(self.request('/input',preview.frame(15,struct.pack('<Qd',17,t0)))[0],200)
        found=False
        while not found:
            for kind,packet in self.read():
                if kind==108:
                    nonce,client,received,sent=struct.unpack('<Qddd',packet[12:]);self.assertEqual((nonce,client),(17,t0));self.assertGreaterEqual(sent,received);found=True
        # Stale close beacon cannot terminate the current session.
        self.assertEqual(self.request('/close',json.dumps({'token':self.server.token,'session':'old'}),token=False)[0],200)
        self.assertIsNone(self.process.poll())
        self.assertEqual(self.request('/close',json.dumps({'token':self.server.token,'session':self.session}),token=False)[0],200)
        self.assertEqual(self.process.poll(),0)
        self.assertIsNone(self.server.worker)

    def test_native_file_round_trip_and_cancel(self):
        fixture=bytes([0,1,2,3,255,192,128,68,97,116,97,80,117,109,112,10])
        (self.root/'input.bin').write_bytes(fixture)
        self.event(4,self.control('Attach file')['id']);self.wait(lambda:bool(self.snapshot.get('service')))
        self.event(13,self.snapshot['service']['id'],flags=2)
        self.assertFalse(self.snapshot.get('service'))
        for label,value in [('Path loss','6 dB'),('Short ≤16 B target SNR (dB-Hz)','80'),('Long / file target SNR (dB-Hz)','80')]:self.event(1,self.control(label)['id'],value)
        self.event(4,self.control('Attach file')['id']);self.wait(lambda:bool(self.snapshot.get('service')))
        self.service('input.bin');self.wait(lambda:self.control('Transmit')['enabled'])
        self.event(4,self.control('Transmit')['id'])
        self.wait(lambda:bool(self.control('Files in memory').get('records')))
        files=self.control('Files in memory');self.event(7,files['id'],files['records'][0]['id'])
        self.wait(lambda:bool(self.snapshot.get('service')));self.service('received.bin')
        self.assertEqual((self.root/'received.bin').read_bytes(),fixture)
        worker=self.server.worker;self.assertEqual(self.request('/stop',b'')[0],200)
        self.assertEqual(self.process.poll(),0);self.assertFalse(worker.reader.is_alive())

    def test_expired_tab_reaps_worker(self):
        worker=self.server.worker;worker.touched=time.monotonic()-preview.LEASE-1
        deadline=time.monotonic()+8
        while self.server.worker is not None and time.monotonic()<deadline:time.sleep(.05)
        self.assertIsNone(self.server.worker);self.assertEqual(self.process.poll(),0)
        self.assertFalse(worker.reader.is_alive())

    def test_failed_import_can_be_retried(self):
        (self.root/'input.bin').write_bytes(b'abc')
        self.event(4,self.control('Attach file')['id']);self.wait(lambda:bool(self.snapshot.get('service')))
        service=self.snapshot['service'];sequence=int(self.snapshot['ack'])+1
        event={'version':1,'kind':13,'generation':self.snapshot['generation'],'sequence':str(sequence),'target':service['id']}
        scope=self.server.worker.file_scope;original=scope.read
        def truncated(path):
            source,name,info=original(path);source.seek(0,os.SEEK_END)
            return source,name,info
        with patch.object(scope,'read',truncated):
            code,data=self.request('/service',json.dumps({'event':event,'kind':service['kind'],'path':'input.bin'}))
        self.assertEqual(code,400);self.assertIn(b'truncated',data)
        self.wait(lambda:int(self.snapshot['ack'])>=sequence)
        self.assertFalse(self.snapshot.get('service'))
        self.event(4,self.control('Attach file')['id']);self.wait(lambda:bool(self.snapshot.get('service')))
        self.service('input.bin');self.wait(lambda:self.control('Transmit')['enabled'])


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--worker',type=Path)
    args,remaining=parser.parse_known_args();WORKER=args.worker
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(NativeTests if WORKER else HostTests)
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(not result.wasSuccessful())
