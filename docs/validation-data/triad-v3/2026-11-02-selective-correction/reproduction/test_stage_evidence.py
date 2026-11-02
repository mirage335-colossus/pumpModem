#!/usr/bin/env python3
"""Small provenance/layout tests; no receiver simulation or cloud operations."""
from pathlib import Path
import copy
import hashlib
import importlib.util
import io
import json
import tarfile
import tempfile
import unittest

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('stage_evidence_under_test',HERE/'stage_evidence.py')
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)


def write(path,value):
    path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(value)+'\n');return path


def success(name,command):
    return dict(name=name,command=command,returncode=0,runner=dict(schema='triad-bounded-run-v1',outcome='completed',exit_code=0,child_exit_code=0,cancelled=False,timed_out=False,termination=dict(group_empty=True)))


class ReceiptViews(unittest.TestCase):
    def test_subset_keeps_unplanned_success_and_original(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);plan=write(root/'plan.json',dict(jobs=[dict(name='a',argv=['binary','a'])]))
            original=dict(cancelled=False,results=[success('a',['binary','a']),success('extra',['binary','extra'])]);parent=write(root/'parent.json',original);before=parent.read_bytes()
            view=s.derive_success_view(plan,[parent],root/'view.json')
            self.assertEqual(parent.read_bytes(),before);self.assertEqual(view['results'],original['results'][:1]);self.assertEqual(view['excluded_records'][0]['record'],original['results'][1]);self.assertTrue(view['parent_receipts'][0]['all_parent_records_successful'])

    def test_completed_resumption_preserves_cancellation(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);plan=write(root/'plan.json',dict(jobs=[dict(name='a',argv=['binary','a']),dict(name='b-resume',argv=['binary','b-new'])]))
            failed=success('b',['binary','b-old']);failed['returncode']=143;failed['runner']['cancelled']=True
            first=write(root/'first.json',dict(cancelled=True,results=[success('a',['binary','a']),failed,dict(name='c',outcome='not_started_cancelled')]))
            resume=write(root/'resume.json',dict(cancelled=False,results=[success('b-resume',['binary','b-new'])]))
            with self.assertRaises(s.EvidenceError):s.derive_success_view(plan,[first,resume],root/'bad.json')
            view=s.derive_success_view(plan,[first,resume],root/'view.json',True)
            self.assertEqual(len(view['results']),2);self.assertEqual(view['excluded_unsuccessful_records'],2);self.assertTrue(view['parent_receipts'][0]['cancelled']);self.assertTrue(json.loads(first.read_text())['cancelled'])

    def test_missing_error_and_command_mismatch_fail_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);plan=write(root/'plan.json',dict(jobs=[dict(name='a',argv=['binary','a'])]))
            for label,row in [('missing',success('other',['binary','other'])),('command',success('a',['binary','wrong'])),('unreaped',success('a',['binary','a']))]:
                if label=='unreaped':row['runner']['termination']['group_empty']=False
                parent=write(root/(label+'.json'),dict(cancelled=False,results=[row]));output=root/(label+'-view.json')
                with self.assertRaises(s.EvidenceError):s.derive_success_view(plan,[parent],output)
                self.assertFalse(output.exists())


class Staging(unittest.TestCase):
    def fixture(self,root,omit=None,bad_input_hash=False):
        prefix='local-evidence/sample/path-0';source='/workspace/triad/'+prefix
        argv=['/workspace/triad/build/triad_long','--mode','export-stats','--output',source]
        output='/workspace/triad/local-evidence/scores/result.json'
        scorer=['/workspace/venv/python','/workspace/triad/tools/triad-conditional-sweep.py',source,'--output',output]
        plan=write(root/'plan.json',dict(jobs=[dict(name='sample',argv=argv)],scoring_jobs=[dict(name='score',argv=scorer)],source=dict(execution_source_archive_sha256='1'*64)))
        data={prefix+'/gram.c128':b'g'*16,prefix+'/mean.c128':b'm'*16}
        manifest=dict(format='triad-block-stats-v1',command_argv=argv,file_sizes={'gram.c128':16,'mean.c128':16})
        data[prefix+'/manifest.json']=json.dumps(manifest).encode()
        code=root/'code';main=b'# scorer source\n';dep=b'# dependency\n'
        (code/'tools').mkdir(parents=True);(code/'tools/triad-conditional-sweep.py').write_bytes(main);(code/'tools/triad_dummy.py').write_bytes(dep)
        source_manifest=write(root/'source-manifest.json',dict(source_archive_sha256='1'*64,files=[dict(path='tools/triad-conditional-sweep.py',size=len(main),sha256=hashlib.sha256(main).hexdigest()),dict(path='tools/triad_dummy.py',size=len(dep),sha256=hashlib.sha256(dep).hexdigest())]))
        npz=b'fixture score archive';data['local-evidence/scores/result.scores.npz']=npz
        hashes={name:hashlib.sha256(data[prefix+'/'+name]).hexdigest() for name in ('manifest.json','gram.c128','mean.c128')}
        if bad_input_hash:hashes['mean.c128']='0'*64
        report=dict(schema='triad-conditional-sweep-v1',scores_file='result.scores.npz',scores_sha256=hashlib.sha256(npz).hexdigest(),source_sha256=hashlib.sha256(main).hexdigest(),dependency_sha256={'triad_dummy.py':hashlib.sha256(dep).hexdigest()},paths=[dict(input=source,status='completed',input_hashes=hashes)])
        data['local-evidence/scores/result.json']=json.dumps(report).encode();archive=root/'evidence.tar.gz'
        with tarfile.open(archive,'w:gz') as stream:
            for name,content in data.items():
                if name==omit:continue
                member=tarfile.TarInfo(name);member.size=len(content);stream.addfile(member,io.BytesIO(content))
        return plan,archive,code,source_manifest,data

    def test_complete_stage_layout_and_original_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);plan,archive,code,manifest,data=self.fixture(root);before=s.sha(archive)
            result=s.stage_archive([plan],archive,before,'/workspace/triad',code,manifest,root/'staged')
            self.assertEqual(result['workspaces'],1);self.assertEqual(result['score_reports'],1);self.assertEqual(s.sha(archive),before)
            for name,value in data.items():self.assertEqual((root/'staged'/name).read_bytes(),value)
            self.assertEqual((root/'staged/tools/triad_dummy.py').read_bytes(),b'# dependency\n')

    def test_archive_hash_missing_data_and_input_hash_failure(self):
        for label in ('archive-hash','missing','input-hash','source-hash'):
            with self.subTest(label=label),tempfile.TemporaryDirectory() as temp:
                root=Path(temp);plan,archive,code,manifest,_=self.fixture(root,omit='local-evidence/sample/path-0/mean.c128' if label=='missing' else None,bad_input_hash=label=='input-hash')
                if label=='source-hash':(code/'tools/triad_dummy.py').write_bytes(b'changed')
                with self.assertRaises(s.EvidenceError):s.stage_archive([plan],archive,'0'*64 if label=='archive-hash' else s.sha(archive),'/workspace/triad',code,manifest,root/'staged')
                self.assertFalse((root/'staged').exists())


if __name__=='__main__':unittest.main()
