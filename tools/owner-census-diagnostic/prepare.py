#!/usr/bin/env python3
"""Prepare a hash-gated, reversible private FIRST omission census overlay."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
BASE_COMMIT='a9b2d95a5fca8f7f007d84a9269cac3863da65c3'
EXPECTED={
 'src/pattern_receiver.cpp':'e956fab251fa1e42981fada7f132d07f6beba53d36a8fc4ff5ace80b66a70f87',
 'src/live.cpp':'536c2005424ba5c973d6db15163ef60c6bf2fe8d3983a5046be931805b1bea9a',
 'src/channel.cpp':'73b633723256cf8d8a2bb63a2edb003a94d91b40b2c3fcf7bdb4411ffab5951d',
 'src/live_simulation_clock.hpp':'c212cfe323561c0276ccaf5c594542ea7445b80cda9aace85ca7af1061628faa',
 'src/pattern_code.cpp':'3e837e010ee937136b0cca72e2200b2c3de938bec344e0292478d5e1cde05cd4',
 'src/audio_trace.hpp':'2e476953b87dbb555e5734357a7de68873709070a97986b6b48fa82397bc52d9',
 'tests/test_live.cpp':'4c38f29eff44f89f39170a2408b7d967f30194c32914302cd3b6ac4a88ee91b7',
}
NS='::datapump::diagnostic::owner_census::'

def once(text,old,new):
    if text.count(old)!=1:raise ValueError(f'anchor must occur once ({text.count(old)}): {old[:120]!r}')
    return text.replace(old,new,1)

def live_edits(source):
    edits=[]
    def add(old,new):edits.append((old,new))
    add('#include "audio_trace.hpp"\n','#include "audio_trace.hpp"\n#include "owner_census_probe.hpp"\n')
    old='        const auto unavailable=[&](const std::string& reason) {\n'
    add(old,'''        ::datapump::diagnostic::owner_census::Candidate raw_candidate;
        const auto raw_omission=[&](const auto& reason) noexcept {
            if(!::datapump::diagnostic::owner_census::enabled())return;
            try {::datapump::diagnostic::owner_census::capture(bank,value,raw_candidate,reason,
                observation_epoch(bank,value,bank.samples),bank_capacity(value));}catch(...){}
        };
'''+old+'            raw_omission(reason);\n')
    old='            for(const auto& profile:profiles) {\n'
    add(old,old+'            raw_candidate={};'+NS+'profile(raw_candidate,profile);raw_candidate.family=family;\n')
    old="                const auto tag = key ? key->mac(Bytes{'D','P','-','R','X','-','B','A','N','K'}) : Bytes{};\n"
    add(old,old+'                raw_candidate={};'+NS+'profile(raw_candidate,profile);raw_candidate.family=family;\n                raw_candidate.valid=true;raw_candidate.epoch=epoch;raw_candidate.origin=bank.samples;\n                '+NS+'key(raw_candidate,tag);\n')
    old='                if(bank.working_bytes>=capacity || capacity-bank.working_bytes<=control_margin) {\n'
    add(old,'                raw_candidate.capacity=capacity;raw_candidate.margin=control_margin;\n'+old+'                    raw_omission("receiver allocation refused");\n')
    old='                const auto config = transfer::seeded_config(receiver.options, epoch);\n'
    add(old,old+'                '+NS+'profile(raw_candidate,config);raw_candidate.config_known=true;\n')
    old='                    auto search=std::move(prepared.search);\n'
    add(old,old+'''                    raw_candidate.search_known=true;raw_candidate.start_known=search.start_offset_seconds.has_value();
                    if(search.start_offset_seconds)raw_candidate.start=*search.start_offset_seconds;
                    raw_candidate.radius=search.start_uncertainty_seconds;raw_candidate.initial=search.initial_stream_symbols;
                    raw_candidate.search_phases=search.search_stream_phases;raw_candidate.pairs=search.hypotheses.size();
''')
    old='                        const auto allocation=std::min(value.dsp_workspace_bytes/2,capacity-bank.working_bytes-control_margin);\n'
    add(old,old+'''                        raw_candidate.allocation_known=true;
                        raw_candidate.allocation=selection.backend==modem::ReceiverBackend::existing_complex && !(selection.scheduled && !packed_schedule)?allocation:
                            std::min(allocation,selection.selected_workspace_bytes);
''')
    # Replace the exact original predicate as one reversible anchor. Decision values
    # are journaled at original returns; no second retirement-policy evaluation.
    first='        const auto retired=std::erase_if(bank.receivers,[&](const auto& receiver){\n'
    end='        bank.retirements+=retired;\n'
    start=source.index(first);finish=source.index(end,start)
    old=source[start:finish]
    new=old.replace(first,'''        ::datapump::diagnostic::owner_census::refresh_begin(bank.revision,bank.samples,now);
'''+first+'''            bool raw_expiry_known=false,raw_expired=false,raw_frontier_queried=false;
            std::optional<std::uint64_t> raw_frontier;
            const auto raw_decision=[&](bool retire,const char* reason) noexcept {
                return ::datapump::diagnostic::owner_census::decision(receiver,retire,reason,
                    raw_expiry_known,raw_expired,raw_frontier_queried,raw_frontier);
            };
''',1)
    new=once(new,'if(receiver.modem->synchronized())return false;','if(receiver.modem->synchronized())return raw_decision(false,"synchronized");')
    new=once(new,'if(!receiver.modem->initial_search_complete())return false;','if(!receiver.modem->initial_search_complete())return raw_decision(false,"initial_search_incomplete");')
    new=once(new,'                const auto frontier=receiver.modem->delayed_processing_frontier();\n','                const auto frontier=receiver.modem->delayed_processing_frontier();\n                raw_frontier_queried=true;raw_frontier=frontier;\n')
    new=once(new,'if(!frontier || *frontier>=bank.samples-receiver.sample_origin)return true;','if(!frontier || *frontier>=bank.samples-receiver.sample_origin)return raw_decision(true,"burst_quiescent");')
    new=once(new,'\n                bank.retirement_pending=true;return false;','\n                bank.retirement_pending=true;return raw_decision(false,"burst_frontier_behind");')
    new=once(new,'            if(!expired)return false;\n','            raw_expiry_known=true;raw_expired=expired;\n            if(!expired)return raw_decision(false,"not_expired");\n')
    new=once(new,'            if(const auto committed=receiver.modem->delayed_processing_frontier()) {\n','            raw_frontier=receiver.modem->delayed_processing_frontier();raw_frontier_queried=true;\n            if(const auto committed=raw_frontier) {\n')
    new=once(new,'                    bank.retirement_pending=true;return false;','                    bank.retirement_pending=true;return raw_decision(false,"frontier_behind");')
    new=once(new,'            return true;\n','            return raw_decision(true,"expired_scored");\n')
    add(old,new)
    return edits

def pattern_edits(source):
    old='bool PatternReceiver::synchronized()const{return impl_->fallback?impl_->fallback->synchronized():std::any_of(impl_->tracks.begin(),impl_->tracks.end(),[](const auto& track){return track.admitted;});}'
    return [('#include "audio_trace.hpp"\n','#include "audio_trace.hpp"\n#include "owner_census_probe.hpp"\n'),
        (old,old.replace('{return','{::datapump::diagnostic::owner_census::inspect(*impl_);return',1))]

def transform(source,kind):
    edits=live_edits(source) if kind=='live' else pattern_edits(source)
    result=source
    for old,new in edits:result=once(result,old,new)
    reverse=result
    for old,new in reversed(edits):reverse=once(reverse,new,old)
    if reverse!=source:raise ValueError('reverse reconstruction differs')
    return result,reverse,edits

def sha(data):return hashlib.sha256(data).hexdigest()

def prepare(root,output):
    root=root.resolve(strict=True);output=output.resolve()
    if output==root or root/'src'==output:raise ValueError('requires fresh private output directory')
    actual={p:sha((root/p).read_bytes()) for p in EXPECTED}
    if actual!=EXPECTED:raise ValueError(f'input closure differs from {BASE_COMMIT}: {actual}')
    prepared={}
    for kind in ('live','pattern_receiver'):
        before=(root/f'src/{kind}.cpp').read_text();after,reverse,edits=transform(before,kind)
        prepared[kind]=(before,after,reverse,len(edits))
    support=Path(__file__).with_name('owner_census_probe.hpp').read_bytes()
    output.mkdir(parents=True,exist_ok=False)
    manifest={'schema':1,'status':'private_preparation_only','base_commit':BASE_COMMIT,
        'source_root':str(root),'output_dir':str(output),'source_closure':actual,'sources':{},
        'tool_sha256':sha(Path(__file__).read_bytes()),'support_sha256':sha(support),
        'environment':'DATAPUMP_OWNER_CENSUS_DIAGNOSTIC=0|1 (unset=0; other values disabled/incomplete)',
        'capture':'one FIRST omission, synchronous joined admission boundary; original default clock unchanged',
        'caps':{'owners':128,'tracks_per_owner':16,'rows':2400,'journal':128},
        'clock_scope':'census emission and owner queries only; excludes journal/marker/filter/summary; four explicit clock attempts plus trace clocks',
        'limitations':['not qualification','not a paired or fixed-work overhead comparison','fallback internal tracks explicitly unavailable',
            'non-final radius is readiness envelope, not scheduler execution proof','journal reports last actual refresh only',
            'strict footer/seal/drop validation required; any missing summary or capped/error state is incomplete']}
    for kind,(before,after,reverse,anchors) in prepared.items():
        for suffix,text in (('.cpp',after),('.reversed.cpp',reverse)):
            path=output/(kind+suffix);path.write_text(text)
            if path.read_text()!=text:raise ValueError('private output readback differs')
        (output/(kind+'.patch')).write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile=f'base/src/{kind}.cpp',tofile=f'private/src/{kind}.cpp')))
        manifest['sources'][kind]={'before_sha256':sha(before.encode()),'after_sha256':sha(after.encode()),'reverse_sha256':sha(reverse.encode()),'anchors':anchors}
    (output/'owner_census_probe.hpp').write_bytes(support)
    if (output/'owner_census_probe.hpp').read_bytes()!=support:raise ValueError('support readback differs')
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return manifest

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-root',required=True,type=Path);p.add_argument('--output-dir',required=True,type=Path)
    a=p.parse_args();print(json.dumps(prepare(a.source_root,a.output_dir),indent=2))
