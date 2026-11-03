#!/usr/bin/env python3
"""Predeclare paired C/N0 sweeps of two complete public timing banks.

Only noise density changes in each native physical capture. Identical seeds
across C/N0 are intentional paired paths, not independent validation trials.
Short-window and global fits subsequently reuse the exact same exported data.
"""
import argparse
import copy
import importlib.util
import json
from pathlib import Path

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('sensitivity_plan_helpers',HERE/'prepare.py')
helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)
SCRIPT='docs/validation-data/triad-v3/2026-11-03-broad/acquisition/sensitivity.py'
CASES=('spread-hf-worst','timing-margin-default-1')
LEVELS=(-6,0,6,12)


def build(parent):
    result=copy.deepcopy(parent);result['cells']=[];result['jobs']=[]
    for case in CASES:
        selected=[j for j in parent['jobs'] if j['case']==case]
        if len(selected)!=9:raise ValueError('expected three captures/configure/export/acquire per case')
        for cn0 in LEVELS:
            newcase=case+'-cn'+str(cn0)
            cell=copy.deepcopy(next(c for c in parent['cells'] if c['case']==case))
            cell.update(case=newcase,parent_case=case,cn0_db_hz=cn0)
            result['cells'].append(cell)
            ids={j['id']:j['id'].replace(case,newcase) for j in selected}
            for old in selected:
                j=copy.deepcopy(old)
                j.update(id=ids[old['id']],case=newcase,parent_case=case,cn0_db_hz=cn0,
                    depends_on=[ids[x] for x in old['depends_on']],
                    parent_acquisition_definition_sha256=old['definition_sha256'])
                for key in ('workspace','acquisition_report','score_archive'):
                    if key in j:j[key]=j[key].replace(case,newcase)
                j['argv']=[x.replace('/'+case+'-', '/'+newcase+'-')
                           .replace('/acquire-'+case+'-', '/acquire-'+newcase+'-') for x in j['argv']]
                if j['stage'] in ('configure','export'):
                    helpers.replace(j['argv'],'--cn0',cn0)
                    before=list(old['argv']);after=list(j['argv'])
                    for flag in ('--cn0','--output'):
                        helpers.replace(after,flag,helpers.option(before,flag))
                    if before!=after:raise ValueError('unexpected physical change')
                if j['stage']=='export':j['timeout_seconds']=60
                j['permitted_changed_options']=sorted(set(j.get('permitted_changed_options',[]))|{'--cn0'})
                j['definition_sha256']=helpers.digest({k:v for k,v in j.items() if k!='definition_sha256'})
                result['jobs'].append(j)
    if len(result['jobs'])!=72 or len({j['id'] for j in result['jobs']})!=72:raise ValueError('bad sweep size')
    result['selection_reason']='Selected after complete-bank20dB-Hz recovery; frozen four-level paired sweep before observing lower-C/N0 outcomes. Not independent reliability qualification.'
    result['limitations']+=['Same physical/key/channel/noise addresses across levels intentionally pair the comparisons. Only noise density changes; modulation energy is preserved.',
        'The global receiver comparison changes channel coherence assumptions, not timing coverage. It cannot be generalized to renewed channels or uncontrolled phase drift.']
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--parent',type=Path,required=True)
    p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error('output must be fresh')
    parent=json.loads(a.parent.read_text());result=build(parent)
    result['sensitivity_parent_plan_sha256']=helpers.sha(a.parent)
    result['source_file_sha256'][SCRIPT]=helpers.sha(a.root/SCRIPT)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps(dict(plan=str(a.output),sha256=helpers.sha(a.output),jobs=len(result['jobs']))))
