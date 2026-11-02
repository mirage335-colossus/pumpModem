#!/usr/bin/env python3
"""Stage SHA-bound evidence and derive explicit successful-job receipt views.

Neither operation changes original archives, extracted files, plans or runner
receipts. Derived success views are selections from successful attempts, never
a claim that an interrupted parent run succeeded as a whole.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import tempfile


class EvidenceError(ValueError):
    pass


def require(ok,message):
    if not ok:raise EvidenceError(message)


def sha(path):
    result=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):result.update(block)
    return result.hexdigest()


def load(path):
    doc=json.loads(Path(path).read_text())
    require(isinstance(doc,dict),'expected JSON object: '+str(path))
    return doc


def option(argv,key):
    require(isinstance(argv,list) and key in argv and argv.index(key)+1<len(argv),'command lacks '+key)
    return argv[argv.index(key)+1]


def relative_recorded(path,source_root):
    value=PurePosixPath(path);root=PurePosixPath(source_root)
    require(value.is_absolute() and '..' not in value.parts,'noncanonical recorded path')
    try:relative=value.relative_to(root)
    except ValueError as error:raise EvidenceError('recorded path is outside declared source root') from error
    require(relative.parts and all(x not in ('.','..') for x in relative.parts),'invalid relative evidence path')
    return relative.as_posix()


def _stream_metadata(archive,names):
    found={}
    with tarfile.open(archive,'r|gz') as stream:
        for member in stream:
            if member.name not in names:continue
            require(member.isfile() and member.name not in found,'duplicate/nonregular selected archive member')
            handle=stream.extractfile(member);require(handle is not None,'unreadable archive member')
            data=handle.read();found[member.name]=(data,hashlib.sha256(data).hexdigest())
    require(set(found)==set(names),'archive is missing selected metadata: '+str(sorted(set(names)-set(found))[:3]))
    return found


def stage_archive(plan_paths,archive,archive_sha256,source_root,code_root,source_manifest_path,output_root):
    archive=Path(archive);output_root=Path(output_root);code_root=Path(code_root)
    require(not output_root.exists(),'staging output already exists')
    require(sha(archive)==archive_sha256,'archive SHA256 mismatch')
    source_manifest=load(source_manifest_path)
    source_files={entry['path']:entry for entry in source_manifest.get('files',[])}
    require(source_files and len(source_files)==len(source_manifest['files']),'source manifest is missing/ambiguous')
    plans=[load(path) for path in plan_paths];jobs=[];score_jobs=[];planned={};score_outputs={}
    for plan in plans:
        source=plan.get('source',{})
        expected_archive=source.get('execution_source_archive_sha256')
        if expected_archive:require(expected_archive==source_manifest.get('source_archive_sha256'),'source archive binding mismatch')
        for job in plan.get('jobs',[]):
            output=option(job['argv'],'--output');require(output not in planned,'duplicate planned workspace')
            planned[output]=job;jobs.append(job)
        for job in plan.get('scoring_jobs',[]):
            output=option(job['argv'],'--output');require(output not in score_outputs,'duplicate planned score output')
            score_outputs[output]=job;score_jobs.append(job)
    require(jobs,'no workspaces requested')
    metadata_names={relative_recorded(output,source_root)+'/manifest.json' for output in planned}
    metadata_names.update(relative_recorded(output,source_root) for output in score_outputs)
    metadata=_stream_metadata(archive,metadata_names)
    selected=set(metadata_names);expected_hash={name:digest for name,(_,digest) in metadata.items()};expected_sizes={}
    score_source_hashes={};score_receipts=0
    for output,job in planned.items():
        prefix=relative_recorded(output,source_root);m=json.loads(metadata[prefix+'/manifest.json'][0])
        require(m.get('format')=='triad-block-stats-v1' and m.get('command_argv')==job['argv'],
                'manifest command/format does not match executed plan')
        sizes=m.get('file_sizes');require(isinstance(sizes,dict) and sizes,'manifest lacks array inventory')
        for name,size in sizes.items():
            require(PurePosixPath(name).name==name and type(size)is int and size>=0,'invalid array inventory entry')
            selected.add(prefix+'/'+name)
            expected_sizes[prefix+'/'+name]=size
    for output,job in score_outputs.items():
        name=relative_recorded(output,source_root);r=json.loads(metadata[name][0]);argv=job['argv']
        require(r.get('schema')=='triad-conditional-sweep-v1','unsupported score report')
        require(r.get('scores_file')==Path(output).with_suffix('.scores.npz').name,'score archive basename mismatch')
        npz=str(PurePosixPath(name).with_suffix('.scores.npz'));selected.add(npz);expected_hash[npz]=r['scores_sha256']
        require([row.get('input') for row in r.get('paths',[])]==argv[2:argv.index('--output')],
                'score input order differs from executed plan')
        source_name=relative_recorded(argv[1],source_root)
        require(source_name not in score_source_hashes or score_source_hashes[source_name]==r['source_sha256'],
                'inconsistent scorer script hashes')
        score_source_hashes[source_name]=r['source_sha256']
        for dep,digest in r.get('dependency_sha256',{}).items():
            require(PurePosixPath(dep).name==dep,'invalid scorer dependency name')
            relative=str(PurePosixPath(source_name).with_name(dep))
            require(relative not in score_source_hashes or score_source_hashes[relative]==digest,'inconsistent scorer source hashes')
            score_source_hashes[relative]=digest
        for row in r.get('paths',[]):
            if row.get('status')!='completed':continue
            hashes=row.get('input_hashes',row.get('sampled_input_hashes'))
            require(isinstance(hashes,dict) and hashes,'completed score has no input hash receipt')
            prefix=relative_recorded(row['input'],source_root)
            require(row['input'] in planned,'score input is not a requested workspace')
            for filename,digest in hashes.items():
                require(PurePosixPath(filename).name==filename,'invalid input hash filename')
                key=prefix+'/'+filename
                require(key in selected and (key not in expected_hash or expected_hash[key]==digest),'input hash receipts disagree')
                expected_hash[key]=digest
            score_receipts+=1
    # Include transitive Python tools, not only the scorer's explicitly named
    # dependencies. Every copied source file is bound to the verified source
    # archive manifest; local analysis helper changes are deliberately separate.
    tool_names={name for name in source_files if name.startswith('tools/triad') and name.endswith('.py')}
    require(set(score_source_hashes)<=tool_names,'source manifest omits a declared scorer dependency')
    for name in tool_names:
        origin=code_root/name;entry=source_files[name]
        require(origin.is_file() and origin.stat().st_size==entry['size'] and sha(origin)==entry['sha256'],
                'local code differs from verified remote source: '+name)
        if name in score_source_hashes:require(entry['sha256']==score_source_hashes[name],'scorer report/source manifest hash mismatch')
    output_root.parent.mkdir(parents=True,exist_ok=True)
    inventory=[]
    with tempfile.TemporaryDirectory(prefix='triad-evidence-stage-',dir=output_root.parent) as temporary:
        temporary=Path(temporary);found=set()
        with tarfile.open(archive,'r|gz') as stream:
            for member in stream:
                if member.name not in selected:continue
                require(member.isfile() and member.name not in found,'duplicate/nonregular selected archive member')
                found.add(member.name);target=temporary/member.name;target.parent.mkdir(parents=True,exist_ok=True)
                handle=stream.extractfile(member);require(handle is not None,'unreadable selected data')
                digest=hashlib.sha256();count=0
                with target.open('xb') as output:
                    for block in iter(lambda:handle.read(1024*1024),b''):
                        digest.update(block);count+=len(block);output.write(block)
                require(count==member.size,'archive member size mismatch')
                if member.name in expected_sizes:require(count==expected_sizes[member.name],'manifest array size mismatch')
                if member.name in expected_hash:require(digest.hexdigest()==expected_hash[member.name],'archive/input receipt hash mismatch: '+member.name)
                target.chmod(0o444)
                inventory.append(dict(path=member.name,size=count,sha256=digest.hexdigest(),origin='SHA-bound evidence archive'))
        require(found==selected,'archive is missing planned data: '+str(sorted(selected-found)[:3]))
        for name in sorted(tool_names):
            target=temporary/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(code_root/name,target)
            require(sha(target)==source_files[name]['sha256'],'source copy hash mismatch')
            target.chmod(0o444);inventory.append(dict(path=name,size=target.stat().st_size,sha256=sha(target),origin='verified source archive manifest'))
        require(sha(archive)==archive_sha256,'archive changed while staging')
        report=dict(schema='triad-staged-evidence-v1',adapter_sha256=sha(__file__),source_root=str(source_root),staged_root=str(output_root.resolve()),archive_sha256=archive_sha256,archive_size=archive.stat().st_size,source_manifest_sha256=sha(source_manifest_path),source_archive_sha256=source_manifest['source_archive_sha256'],plans=[dict(path=str(Path(p).resolve()),sha256=sha(p)) for p in plan_paths],workspaces=len(planned),score_reports=len(score_outputs),completed_score_input_receipts_checked=score_receipts,files=sorted(inventory,key=lambda x:x['path']),scope='Exact selected evidence bytes copied from the verified original archive into its recorded path layout; flat extraction and originals are untouched. This stages evidence, not receiver qualification.')
        (temporary/'stage-report.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
        temporary.rename(output_root)
    return report


def successful(row):
    runner=row.get('runner',{})
    return (row.get('returncode')==0 and runner.get('schema')=='triad-bounded-run-v1' and
        runner.get('outcome')=='completed' and runner.get('exit_code')==0 and
        runner.get('child_exit_code')==0 and runner.get('cancelled')is False and
        runner.get('timed_out')is False and runner.get('termination',{}).get('group_empty')is True)


def derive_success_view(plan_path,parent_paths,output,allow_excluded_failures=False):
    output=Path(output);require(not output.exists(),'receipt view output already exists')
    plan=load(plan_path);planned={j['name']:j['argv'] for j in plan.get('jobs',[])}
    require(planned and len(planned)==len(plan['jobs']),'empty/duplicate planned job inventory')
    selected={};excluded=[];parents=[]
    for path in parent_paths:
        parent=load(path);rows=parent.get('results');require(isinstance(rows,list),'parent receipt lacks results')
        all_success=all(successful(row) for row in rows)
        require(allow_excluded_failures or (parent.get('cancelled')is False and all_success),
                'parent was cancelled/failed; explicit completed-resumption selection is required')
        parent_hash=sha(path)
        parents.append(dict(path=str(Path(path).resolve()),sha256=parent_hash,cancelled=parent.get('cancelled'),records=len(rows),all_parent_records_successful=all_success))
        for index,row in enumerate(rows):
            name=row.get('name');wanted=name in planned
            if wanted and successful(row):
                require(row.get('command')==planned[name],'successful row command differs from requested plan')
                require(name not in selected,'multiple successful receipts for one planned job')
                selected[name]=copy.deepcopy(row)
            else:
                excluded.append(dict(parent_sha256=parent_hash,parent_record_index=index,reason='outside requested exact successful-job selection',record=copy.deepcopy(row)))
    missing=set(planned)-set(selected);require(not missing,'missing successful planned jobs: '+str(sorted(missing)[:3]))
    result=dict(schema='triad-derived-planned-success-view-v1',adapter_sha256=sha(__file__),plan=str(Path(plan_path).resolve()),plan_sha256=sha(plan_path),cancelled=False,results=[selected[j['name']] for j in plan['jobs']],derived_view=True,selection_scope='Exact successful attempts for plan.jobs only; original scoring and other jobs need their own receipts.',all_requested_jobs_successful=True,allow_excluded_failures=bool(allow_excluded_failures),parent_receipts=parents,excluded_records=excluded,excluded_unsuccessful_records=sum(not successful(x['record']) for x in excluded),interpretation='This is a derived selection, not a rewrite of any parent receipt and not a claim that cancelled or failed parent runs succeeded. Every excluded failure/cancellation/non-start record is preserved verbatim below.')
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n');return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='mode',required=True)
    stage=sub.add_parser('stage');stage.add_argument('--plan',type=Path,action='append',required=True);stage.add_argument('--archive',type=Path,required=True);stage.add_argument('--archive-sha256',required=True);stage.add_argument('--source-root',required=True);stage.add_argument('--code-root',type=Path,required=True);stage.add_argument('--source-manifest',type=Path,required=True);stage.add_argument('--output-root',type=Path,required=True)
    receipt=sub.add_parser('receipts');receipt.add_argument('--plan',type=Path,required=True);receipt.add_argument('--parent',type=Path,action='append',required=True);receipt.add_argument('--output',type=Path,required=True);receipt.add_argument('--allow-excluded-failures',action='store_true')
    args=parser.parse_args()
    if args.mode=='stage':result=stage_archive(args.plan,args.archive,args.archive_sha256,args.source_root,args.code_root,args.source_manifest,args.output_root);print(json.dumps(dict(workspaces=result['workspaces'],score_reports=result['score_reports'],files=len(result['files']),root=str(args.output_root))))
    else:result=derive_success_view(args.plan,args.parent,args.output,args.allow_excluded_failures);print(json.dumps(dict(selected=len(result['results']),excluded=len(result['excluded_records']),excluded_unsuccessful=result['excluded_unsuccessful_records'],output=str(args.output))))

if __name__=='__main__':main()
