#!/usr/bin/env python3
"""Cheap native comparison for the optional coarse public timing provider.

This validates candidate synthesis, not long noisy timing-state reception.
"""
import argparse
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

import numpy as np

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import triad_multiday_tracker_check as checked
from triad_multiday_tracker_check import NativePublicTimingBasis
from triad_timing_forward import TimingState


def main(binary):
    with tempfile.TemporaryDirectory(prefix='triad-public-timing-') as temp:
        root=Path(temp)
        common=[binary,'--rate','.001','--sample-rate','1','--sample-rate-denominator','64',
            '--days','38','--data-if','half-rate','--clock-profile','off',
            '--seed','12017','--key-seed','13219']
        subprocess.run(common+['--template-ppm','0','--output',str(root/'complete')],
                       check=True,stdout=subprocess.DEVNULL,timeout=15)
        manifest=json.loads((root/'complete'/'manifest.json').read_text())
        errors=[]
        with NativePublicTimingBasis(binary,manifest) as provider:
            for anchor in (1_000_000,3_000_000):
                target=root/str(anchor)
                ppms=(-200,-100,0,100,200)
                subprocess.run(common+['--template-ppm',','.join(map(str,ppms)),
                    '--source-window',str(anchor),'4096','--output',str(target)],
                    check=True,stdout=subprocess.DEVNULL,timeout=15)
                window=json.loads((target/'manifest.json').read_text())
                first=window['first_sample'];count=window['samples']
                states=[TimingState(0,round(ppm*1e-6*anchor/10),ppm//100) for ppm in ppms]
                values=provider(states,anchor,first,first+count,first,np.arange(count))
                assert np.all(values[0]==0)
                split=count//2
                joined=np.concatenate((provider(states,anchor,first,first+split,first,np.arange(split)),
                    provider(states,anchor,first+split,first+count,first+split,np.arange(count-split))),axis=-1)
                np.testing.assert_allclose(values,joined,rtol=0,atol=1e-12)
                for bit in (0,1):
                    for index,ppm in enumerate(ppms):
                        exact=np.fromfile(target/f'basis_bit{bit}_ppm{index}.c64',dtype='<c8')
                        error=float(np.linalg.norm(values[bit+1,index,0]-exact)/np.linalg.norm(exact))
                        assert error<1e-5,(anchor,bit,ppm,error)
                        errors.append(error)
            # Exact native support rejects neither valid tails nor zero
            # extension; negative coordinates must never wrap cached samples.
            zero=provider([TimingState(0,-10000,0)],0,0,16,0,np.arange(16))
            assert np.all(zero==0)
            first=manifest['samples']+1024
            zero=provider([TimingState(0,0,0)],first*64,first,first+16,first,np.arange(16))
            assert np.all(zero==0)
            try:
                provider([TimingState(0,0,0)],0,0,2,0,np.array([0.,1.01]))
            except ValueError:
                pass
            else:
                raise AssertionError('nonuniform physical input silently accepted')
        receipt=provider.report()
        assert receipt['exit_code']==0
        # Format changes must fail before any candidate is requested and still
        # close/reap the persistent child. These are our own tiny protocol stubs.
        for field,value in [('key_seed',-1),('pattern_revision','different-format')]:
            altered=dict(receipt['startup']);altered[field]=value
            marker=root/f'closed-{field}'
            fake=root/f'fake-{field}'
            fake.write_text('#!'+sys.executable+'\nimport sys\nfrom pathlib import Path\n'+
                'print('+repr(json.dumps(altered))+',flush=True)\n'+
                'sys.stdin.buffer.read()\nPath('+repr(str(marker))+').write_text("closed")\n')
            fake.chmod(0o700)
            try:
                NativePublicTimingBasis(fake,manifest)
            except RuntimeError as error:
                assert 'format differs from capture: '+field in str(error)
            else:
                raise AssertionError('mismatched public source format accepted')
            assert marker.read_text()=='closed'
        # Exercise final qualification guards without editing repository code
        # or rerunning any full campaign. The altered digest simulates a file
        # changing after its start hash; the result must be retained as invalid.
        arguments=dict(input=str(root/'complete'),cn0=-50.,true_bit='0',seed=173,
            alpha=1e-4,chunk=64,limit_samples=128,segment_seconds=4096.,
            coherence_seconds=float('inf'),dynamic_timing=False,basis_exporter=None,
            timing_states=8,timing_proposals=32)
        for changed in (None,'triad_dynamic_joint.py','basis_bit0_ppm0.c64'):
            output=root/('guard-'+str(changed)+'.json')
            real_sha=checked.sha;counts={}
            def observed_sha(path):
                name=Path(path).name;counts[name]=counts.get(name,0)+1
                value=real_sha(path)
                return 'changed-after-start' if name==changed and counts[name]>=2 else value
            with patch.object(checked,'sha',side_effect=observed_sha),redirect_stdout(io.StringIO()):
                try:
                    checked.main(argparse.Namespace(output=str(output),**arguments))
                except RuntimeError as error:
                    assert changed is not None and 'not source-qualified' in str(error)
                else:
                    assert changed is None
            report=json.loads(output.read_text())
            assert 'triad_dynamic_joint.py' in report['source_sha256_at_start']
            assert report['runtime_dependencies_unchanged']==(changed is None)
            assert report['input_samples_unchanged']==(changed!='basis_bit0_ppm0.c64')
        print(json.dumps(dict(checks=28,maximum_relative_complex_rms=max(errors),native_public_basis=receipt,

            source_calendar=manifest['source_calendar'],scope='Exact native public candidate basis, including preserved legacy-RRC late-boundary failure case; two absolute-time windows/five rates; not dynamic HMM sensitivity')))


if __name__=='__main__':
    if len(sys.argv)!=2:
        raise SystemExit('usage: test_triad_multiday_tracker_check.py BUILT_EXPORTER')
    main(sys.argv[1])
