import importlib.util,json,tempfile,unittest,subprocess,hashlib
from pathlib import Path
from unittest.mock import Mock,patch
spec=importlib.util.spec_from_file_location('capture',Path(__file__).with_name('capture.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Checks(unittest.TestCase):
 def test_exited_leader_still_terminates_owned_group(self):
  p=Mock(pid=123);p.returncode=0
  with patch.object(m.os,'killpg') as kill:m.terminate_group(p)
  self.assertEqual([c.args for c in kill.call_args_list],[(123,m.signal.SIGTERM),(123,m.signal.SIGKILL)])
  p.wait.assert_called_once_with(timeout=5)
 def test_timeout_escalates_with_bounded_waits(self):
  p=Mock(pid=123);p.wait.side_effect=[subprocess.TimeoutExpired('child',5),-9]
  with patch.object(m.os,'killpg') as kill:m.terminate_group(p)
  self.assertEqual([c.kwargs for c in p.wait.call_args_list],[{'timeout':5},{'timeout':10}])
  self.assertEqual(len(kill.call_args_list),3)
 def test_launch_failure_saved_without_pipe_thread(self):
  with tempfile.TemporaryDirectory() as t:
   root=Path(t)
   with patch.object(m.subprocess,'Popen',side_effect=OSError('controlled launch failure')):
    r=m.run_case(root/'missing',root,'baseline',False,root)
   self.assertEqual(r['actual_exit_code'],None);self.assertIn('controlled',r['launch_error'])
   self.assertEqual(r['process_result'],'failed_or_incomplete');self.assertTrue((root/'baseline/result.json').is_file())
 def test_capped_file_retains_prefix_and_observed_hash(self):
  with tempfile.TemporaryDirectory() as t:
   p=Path(t)/'output';p.write_bytes(b'abcdef');r=m.sealed_file(p,3)
   self.assertEqual(p.read_bytes(),b'abc');self.assertTrue(r['truncated']);self.assertEqual(r['sha256_observed'],hashlib.sha256(b'abcdef').hexdigest())
 def test_matched_capture_and_origin_binding(self):
  with tempfile.TemporaryDirectory() as t:
   p=Path(t);source=p/'archive';source.mkdir();(source/'fixture').write_text('baseline')
   compiled=p/'compiled';(compiled/'source').mkdir(parents=True);(compiled/'evidence').mkdir();(compiled/'source/input').write_text('postimage')
   manifest={'schema':1,'baseline_source_commit':'4e9048748e8d34ca4f90ef1dd01bd8d4588759b2','fixed_origin':1792561396.3857498,'applied':True,'files':{'input':{'after_sha256':m.sha_file(compiled/'source/input')}}}
   (compiled/'evidence/manifest.json').write_text(json.dumps(manifest));binary=p/'build/test_live';binary.parent.mkdir();binary.write_text('ELFmock');(binary.parent/'CMakeCache.txt').write_text('CMAKE_HOME_DIRECTORY:INTERNAL='+str(compiled/'source')+'\nCMAKE_BUILD_TYPE:STRING=Debug\nDATAPUMP_SANITIZERS:BOOL=ON\nDATAPUMP_PORTABLE:BOOL=OFF\n');adapter=p/'prepare.py';adapter.write_text('adapter')
   with patch.object(m,'run_case',side_effect=[{'observer':'off','process_result':'failed_or_incomplete'},{'observer':'on','process_result':'failed_or_incomplete'}]) as run:
    self.assertEqual(m.capture(p,source,compiled,adapter,binary,p/'report'),1)
   self.assertEqual([c.args[3] for c in run.call_args_list],[False,True]);d=json.loads((p/'report/capture-report.json').read_text());self.assertTrue(d['input_origin']['adapter_manifest_verified']);self.assertFalse(d['qualification_claim'])
   manifest['fixed_origin']=1;(compiled/'evidence/manifest.json').write_text(json.dumps(manifest))
   with patch.object(m,'run_case') as run:
    with self.assertRaisesRegex(ValueError,'origin'):m.capture(p,source,compiled,adapter,binary,p/'badreport')
    run.assert_not_called()
if __name__=='__main__':unittest.main()
