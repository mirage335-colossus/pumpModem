#!/usr/bin/env python3
"""Read-only integrity verification; does not import measured code."""
from pathlib import Path
import ast,gzip,hashlib,io,json,tarfile
HERE=Path(__file__).resolve().parent
def sha(b):return hashlib.sha256(b).hexdigest()
def render(v):return (json.dumps(v,indent=2,allow_nan=False)+'\n').encode()
def main():
    for line in (HERE/'SHA256SUMS').read_text().splitlines():
        digest,name=line.split('  ',1);assert sha((HERE/name).read_bytes())==digest,name
    packed={};documents={}
    def raw(name):
        if name not in packed:
            p=(HERE/name).read_bytes();assert p[:3]==b'\x1f\x8b\x08' and p[3:8]==bytes(5);packed[name]=gzip.decompress(p)
        return packed[name]
    def document(name):
        if name not in documents:documents[name]=json.loads(raw(name))
        return documents[name]
    rows=[]
    for n in ['preservation.json','supporting-preservation.json']:
        if (HERE/n).exists():rows+=json.loads((HERE/n).read_text())['records']
    originals={}
    for row in rows:
        if 'embedded_in' in row:
            value=document(row['embedded_in'])
            for key in row['json_path']:value=value[key]
            data=render(value)
        elif row.get('storage')=='compact_json_reformatted_indent2_newline':data=render(document(row['archive']))
        else:data=raw(row['archive'])
        assert len(data)==row['raw_bytes'] and sha(data)==row['raw_sha256'],row['input_path'];originals[row['input_path']]=data
    index=document('source-index.json.gz')
    with tarfile.open(fileobj=io.BytesIO(raw(index['archive'])),mode='r:') as tar:members={m.name:tar.extractfile(m).read() for m in tar.getmembers() if m.isfile()}
    assert len(members)==len(index['members'])
    for row in index['members']:assert len(members[row['member']])==row['bytes'] and sha(members[row['member']])==row['sha256']
    source_docs={}
    for row in index['bindings']:
        if row['record'] not in source_docs:source_docs[row['record']]=json.loads(originals[row['record']])
        value=source_docs[row['record']]
        for key in row['json_path']:value=value[key]
        assert value==row['sha256']==sha(members[row['member']])
    assert not index.get('unavailable',[])
    prefix='local-evidence/triad-performance-broader/'
    if HERE.name=='scientific-cpu-complete-001':
        summary=json.loads(originals[prefix+'cpu-complete-001/summary.json']);assert summary['status']=='incomplete' and summary['exit_code']==2 and summary['errors']==[]
        assert len(summary['functional_commands'])==17 and len(summary['applicable_sampled_cases'])==9
        assert summary['outcomes']['performance_regression']['regressions']==[]
        assert set(summary['outcomes']['performance_regression']['unresolved'])=={'broad-hf-timing'}
        assert len(index['reconstructed_sources'])==2
        for row in index['reconstructed_sources']:
            proof=json.loads(originals[row['proof_record']]);assert not row['original_snapshot']
            assert proof.get('reconstructed_sha256')==row['sha256']
        post=json.loads((HERE/'post-run-classification.json').read_text());data=originals[post['receipt']];assert sha(data)==post['receipt_sha256'];derived=json.loads(data)
        assert derived['review_status']=='accepted_independent_delta_review'
        assert not derived['new_receiver_measurements'] and not derived['raw_files_rewritten'] and not derived['new_model_qualification']
        assert derived['validation']['valid'] and derived['validation']['status']=='passed'
        assert derived['validation']['outcomes']['sustained_stability']=='review_required'
        checked=set()
        for name,digest in derived['original_record_sha256'].items():assert sha(originals[name])==digest;checked.add(name)
        original=derived['original_complete_report'];name=original['path'];assert sha(originals[name])==original['sha256'];checked.add(name);assert len(checked)==12
        with tarfile.open(fileobj=io.BytesIO(raw(post['archive'])),mode='r:') as tar:post_sources={m.name:tar.extractfile(m).read() for m in tar.getmembers() if m.isfile()}
        for row in post['sources']:assert len(post_sources[row['path']])==row['bytes'] and sha(post_sources[row['path']])==row['sha256']
        assert sha(post_sources['tools/triad_perf_lifecycle.py'])==derived['classification_source_sha256']
        assert sha(post_sources['tests/test_triad_perf_lifecycle.py'])==derived['test_source_sha256']
        assert sha(post_sources['tools/triad-performance-lifecycle.json'])==derived['manifest_sha256']
    elif HERE.name=='scientific-blackwell-identical-cost-001':
        base=prefix+'gpu/final-routine/blackwell-identical-cost-001/'
        summary=json.loads(originals[base+'summary.json']);plan=json.loads(originals[base+'plan.json'])
        assert summary['status']=='comparison_measured_both_pass' and not summary['complete_suite_claim']
        assert summary['qualification']['prediction_valid'] and summary['qualification']['gate']=='inconclusive'
        assert all(v['gate']=='pass' and v['prediction_valid'] for v in summary['arms'].values())
        assert summary['savings']['same_policy_supported'] and not summary['savings']['campaign_cost_ratio_confidence_qualified']
        assert summary['savings']['first_use_seconds_saved_after_new_qualification']<0
        for arm in ['qualification','direct','abbreviated']:
            result=json.loads(originals[base+arm+'/measurement/result.json']);row=summary['qualification'] if arm=='qualification' else summary['arms'][arm]
            assert result['record_sha256']==row['record_sha256'] and result['identity_sha256']==row['identity_sha256']
        assert sha(originals[prefix+'blackwell-identical-cost-001/run.py'])==plan['wrapper_sha256']
        diff=json.loads(originals[prefix+'blackwell-identical-cost-001/manifest-diff.json'])
        for case in diff['cases'].values():
            for item in case.values():
                for side in ['old','new']:assert sha(originals[item[side+'_path']])==item[side+'_sha256']
    elif HERE.name=='scientific-fhss-derived-ci-001':
        receipt=json.loads(next(iter(originals.values())));context=json.loads((HERE/'source-context.json').read_text())
        report_raw=render(json.loads(gzip.decompress((HERE/context['binding_source']).read_bytes())))
        assert sha(report_raw)==context['source_report_sha256']==receipt['source_report_sha256'];report=json.loads(report_raw)
        row=next(c for c in report['cases'] if c['id']==receipt['case_id'])
        assert row['regression_decision']['expanded_full_prediction_ratio_interval']==receipt['observed_normal_ratio_interval']
        assert row['regression_decision']['gate']=='inconclusive' and receipt['source_status']=='inconclusive'
        assert receipt['new_measurements']==0 and not receipt['normal_decision_overwritten']
        assert receipt['observed_normal_ratio_interval'][1]<receipt['predeclared_ci_threshold']
        assert receipt['normal_pass_error_budget']<=receipt['predeclared_ci_pass_error_budget']
        assert report['harness_sha256']['triad_perf_adaptive.py']==context['policy_sha256']
        source=next(members[m['member']] for m in index['members'] if m['path']=='tools/triad_perf_adaptive.py');assert sha(source)==context['policy_sha256']
        function=next(n for n in ast.parse(source).body if isinstance(n,ast.FunctionDef) and n.name=='regression_profile')
        assignment=next(n for n in function.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='profiles' for t in n.targets));profiles=ast.literal_eval(assignment.value)
        assert profiles['normal']==(receipt['normal_pass_error_budget'],receipt['normal_threshold']) and profiles['ci']==(receipt['predeclared_ci_pass_error_budget'],receipt['predeclared_ci_threshold'])
    print(f"Verified {len(originals)} exact records, {len(members)} source versions, {len(index['bindings'])} source bindings, and preserved outcome scope.")
if __name__=='__main__':main()
