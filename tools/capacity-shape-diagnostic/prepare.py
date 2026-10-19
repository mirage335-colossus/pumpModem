#!/usr/bin/env python3
"""Prepare a private, reversible capacity-shape Live diagnostic overlay."""
import argparse, difflib, hashlib, importlib.util, json, sys
from pathlib import Path
sys.dont_write_bytecode = True
BASE_COMMIT='81c7da84121be60fe0ce1e7252e94cdf952773e8'
EXPECTED={
 'src/scheduled_work_profile.hpp':'87a701a4ed853685e4250805a8665fdd0f10b15347ab0f4f8be1fcc8148b7acf',
 'src/search_parallel.cpp':'4aa3270515e0e8aaf6f6f146588c40fbff83627af615c85f95897e1af9132f4b',
 'src/pattern_receiver.cpp':'e956fab251fa1e42981fada7f132d07f6beba53d36a8fc4ff5ace80b66a70f87',
 'src/live.cpp':'536c2005424ba5c973d6db15163ef60c6bf2fe8d3983a5046be931805b1bea9a',
 'src/audio_trace.hpp':'2e476953b87dbb555e5734357a7de68873709070a97986b6b48fa82397bc52d9',
 'tests/test_live.cpp':'4c38f29eff44f89f39170a2408b7d967f30194c32914302cd3b6ac4a88ee91b7',
}
HERE=Path(__file__).resolve().parent
SHAPE=HERE/'capacity_shape_probe.hpp'
CANONICAL='tools/owner-census-diagnostic'
TOOLS={
 'prepare.py':'d627abbccbddf9ead52279ff970e8a06c4a540c25936108f4af651d52bebedfd',
 'ledger_extension.py':'733194a043848d5a3492539b1fd8443b191f246c0f46edea29fdb3cfa7e141aa',
 'owner_census_probe.hpp':'5b22ccbd23aebdb9e7ca3296348c8d4c32b6607e3270fb0f2f0d7f96597068e6',
}

def sha(b): return hashlib.sha256(b).hexdigest()
def load(path,name):
    spec=importlib.util.spec_from_file_location(name,path);mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod
def once(text,old,new):
    n=text.count(old)
    if n!=1: raise ValueError(f'anchor must occur exactly once ({n}): {old[:120]!r}')
    return text.replace(old,new,1)
def edits_from(before,edits):
    after=before
    for old,new in edits: after=once(after,old,new)
    reverse=after
    for old,new in reversed(edits): reverse=once(reverse,new,old)
    if reverse!=before: raise ValueError('reverse reconstruction mismatch')
    return after,reverse

def prepare(root,out):
    root=root.resolve(strict=True);out=out.resolve()
    if out==root or root/'src'==out or out.exists(): raise ValueError('output must be a fresh private directory')
    actual={p:sha((root/p).read_bytes()) for p in EXPECTED}
    if actual!=EXPECTED: raise ValueError(f'current source closure differs from 81c7da84121be60fe0ce1e7252e94cdf952773e8: {actual}')
    toolroot=root/CANONICAL
    tool_hashes={name:sha((toolroot/name).read_bytes()) for name in TOOLS}
    if tool_hashes!=TOOLS: raise ValueError(f'canonical schema-2 census tools changed: {tool_hashes}')
    base=load(toolroot/'prepare.py','canonical_owner_census_prepare')
    live=(root/'src/live.cpp').read_text();pattern=(root/'src/pattern_receiver.cpp').read_text()
    test=(root/'tests/test_live.cpp').read_text();audio=(root/'src/audio_trace.hpp').read_text()
    # Reuse the accepted schema-2 first-refusal/census instrumentation verbatim,
    # then add separate aggregate points. Its first-refusal boundary stays intact.
    live1,live_reverse,_=base.transform(live,'live')
    # Preserve the accepted schema-2 inspector at PatternReceiver's original
    # synchronized() hook so a refusal census can report tracks and state.
    pattern2,pattern_reverse,_=base.transform(pattern,'pattern_receiver')
    # transform() includes the accepted schema-2 producer extension.
    shape_edits=[
      ('#include "owner_census_probe.hpp"\n','#include "owner_census_probe.hpp"\n#include "capacity_shape_probe.hpp"\n'),
      ('        for(const auto& receiver:bank.receivers)bank.working_bytes+=receiver_workspace(receiver);\n',
       '        for(const auto& receiver:bank.receivers)bank.working_bytes+=receiver_workspace(receiver);\n'
       '        ::datapump::diagnostic::capacity_shape::observe(bank,value,[](const auto& receiver){return receiver_workspace(receiver);});\n'),
      ('                    if(wave && wave->simulation_channel_pending) {\n                        simulation_channel->begin_burst();wave->simulation_channel_pending=false;\n',
       '                    if(wave && wave->simulation_channel_pending) {\n                        simulation_channel->begin_burst();wave->simulation_channel_pending=false;\n'
       '                        if(media_clock && simulation_bank) ::datapump::diagnostic::capacity_shape::channel_origin("framed_fixture",value.transfer.modem,channel_config(value),*simulation_channel,\n'
       '                            simulation_channel->received_samples(),simulation_bank->samples,simulation_bank->revision,*simulation_bank->observed_epoch);\n'),
    ]
    live2,shape_reverse=edits_from(live1,shape_edits)
    if shape_reverse!=live1: raise ValueError('shape reverse does not restore schema-2 source')
    test_edits=[
      ('#include "datapump/live.hpp"\n','#include "datapump/live.hpp"\n#include "../src/capacity_shape_probe.hpp"\n'),
      ('    value.transfer.key=Crypto(material);\n    return value;\n',
       '    value.transfer.key=Crypto(material);\n'
       '    value.dsp_workspace_bytes=512ULL*1024*1024;value.transfer.dsp_workspace_bytes=512ULL*1024*1024;\n'
       '    return value;\n'),
      ('            fields.add("search_seconds",value.transfer.search_seconds);fields.add("dsp_budget",value.dsp_workspace_bytes);\n',
       '            fields.add("search_seconds",value.transfer.search_seconds);fields.add("dsp_budget",value.dsp_workspace_bytes);\n'
       '            fields.add("transfer_dsp_budget",value.transfer.dsp_workspace_bytes);\n'),
      ('void simulation_media_clock_transmissions(bool standard_geometry=false) {\n',
       'struct CapacityShapeCaseScope {\n'
       '    const char* label;live::Session& session;bool finished=false;\n'
       '    CapacityShapeCaseScope(const char* name,live::Session& value):label(name),session(value){::datapump::diagnostic::capacity_shape::begin_case(label);}\n'
       '    void finish() noexcept {if(finished)return;finished=true;try{::datapump::diagnostic::capacity_shape::end_case(label,session.snapshot());}catch(...){}}\n'
       '    ~CapacityShapeCaseScope(){finish();}\n'
       '};\n'
       'void simulation_media_clock_transmissions(bool standard_geometry=false) {\n'),
      ('        const auto* case_name=mode==0?"raw":mode==1?"short":"framed";\n',
       '        const auto* case_name=mode==0?"raw":mode==1?"short":"framed";\n'
       '        CapacityShapeCaseScope capacity_shape_case(case_name,session);\n'),
      ('        {std::lock_guard lock(script.mutex);check(script.epochs.size()==mode+1,"default provider missed source epoch handoff");epoch=script.epochs.back();origin=script.banks.front().epoch;}\n',
       '        std::uint64_t source_origin=0,bank_origin=0,bank_revision=0;double bank_epoch=0;\n'
       '        {std::lock_guard lock(script.mutex);check(script.epochs.size()==mode+1,"default provider missed source epoch handoff");epoch=script.epochs.back();origin=script.banks.front().epoch;\n'
       '         source_origin=script.banks.front().source;bank_origin=script.banks.front().bank;bank_revision=script.banks.front().revision;bank_epoch=script.banks.front().epoch;}\n'
       '        ::datapump::diagnostic::capacity_shape::transmission(case_name,calculated,source_origin,bank_origin,bank_revision,bank_epoch,epoch);\n'),
      ('        require(calculated.samples_received>64 && !completed.receiver_health.failed(),"media clock reset healthy reception");\n',
       '        require(calculated.samples_received>64 && !completed.receiver_health.failed(),"media clock reset healthy reception");\n'
       '        capacity_shape_case.finish();\n'),
    ]
    test2,test_reverse=edits_from(test,test_edits)
    trace_edits=[
      ('#include <cstdint>\n','#include <cstdint>\n#include <cstdlib>\n#include <cstring>\n'),
      ('inline std::atomic<std::uint64_t> next_identity{0};\n',
       '''inline bool capacity_shape_filter_enabled() noexcept {
    static const bool enabled=[] {const auto* p=std::getenv("DATAPUMP_CAPACITY_SHAPE_DIAGNOSTIC");return p&&std::strcmp(p,"1")==0;}();
    return enabled;
}
inline bool capacity_shape_event_allowed(const char* event) noexcept {
    if(!capacity_shape_filter_enabled())return true;
    return std::strcmp(event,"trace_begin")==0 || std::strcmp(event,"simulation_media_clock_case")==0 ||
        std::strncmp(event,"capacity_shape_",15)==0 || std::strncmp(event,"owner_census_",13)==0 ||
        std::strcmp(event,"receiver_bank_build")==0 || std::strcmp(event,"receiver_bank_admitted")==0 ||
        std::strcmp(event,"receiver_bank_retired")==0 || std::strcmp(event,"receiver_selection")==0 ||
        std::strcmp(event,"receiver_allocation_refused")==0 || std::strcmp(event,"receiver_profile_unavailable")==0 ||
        std::strcmp(event,"receiver_optional_scratch_reclaimed")==0 ||
        std::strcmp(event,"receiver_shared_temporal_plans_retained")==0 ||
        std::strcmp(event,"receiver_shared_capture_reserved")==0 ||
        std::strcmp(event,"receiver_shared_capture_unavailable")==0;
}
inline constexpr const char* capacity_shape_filter_policy="kept trace_begin, simulation_media_clock_case, capacity_shape_*, owner_census_*, receiver selection/admission/retirement/refusal and shared-capacity events; omitted per-bit confirmation/history and unrelated hot diagnostic events";
inline std::atomic<std::uint64_t> next_identity{0};
'''),
      ('template<class F>void record(const char* event,F fields) noexcept {\n    const auto state=active.load(std::memory_order_acquire);if(!state)return;\n',
       'template<class F>void record(const char* event,F fields) noexcept {\n    if(!capacity_shape_event_allowed(event))return;\n    const auto state=active.load(std::memory_order_acquire);if(!state)return;\n'),
      ('            f.add("receiver_probe",receiver_probe);});\n',
       '            f.add("receiver_probe",receiver_probe);f.add("capacity_shape_filter",capacity_shape_filter_enabled());\n'
       '            if(capacity_shape_filter_enabled())f.add("capacity_shape_filter_policy",capacity_shape_filter_policy);});\n'),
    ]
    audio2,audio_reverse=edits_from(audio,trace_edits)
    source_support=(toolroot/'owner_census_probe.hpp').read_text()
    source_cap='owner_cap=128,track_cap=16,row_cap=2400'
    private_cap='owner_cap=512,track_cap=16,row_cap=11000'
    if source_support.count(source_cap)!=1: raise ValueError('canonical owner cap anchor differs')
    support=source_support.replace(source_cap,private_cap,1).encode();shape=SHAPE.read_bytes()

    # Further private CPU partition. Keep accepted capacity/census postimages
    # as explicit intermediate preimages, and reverse this layer independently.
    profiler=(root/'src/scheduled_work_profile.hpp').read_text()
    parallel=(root/'src/search_parallel.cpp').read_text()
    cpu_edits={
      'live':[
        ('#include "capacity_shape_probe.hpp"\n','#include "capacity_shape_probe.hpp"\n#include "capacity_cpu_probe.hpp"\n'),
        ('        if(rx_cpu::global().mode!=rx_cpu::Mode::disabled)audio::trace::record("receiver_component_cpu",',
         '        ::datapump::diagnostic::capacity_cpu::finalize();\n        if(rx_cpu::global().mode!=rx_cpu::Mode::disabled)audio::trace::record("receiver_component_cpu",'),
        ('        ::datapump::diagnostic::capacity_shape::observe(bank,value,[](const auto& receiver){return receiver_workspace(receiver);});\n',
         '        ::datapump::diagnostic::capacity_shape::observe(bank,value,[](const auto& receiver){return receiver_workspace(receiver);});\n        ::datapump::diagnostic::capacity_cpu::work_point("joined_refresh_before_new_admissions");\n'),
      ],
      'pattern':[
        ('#include "owner_census_probe.hpp"\n','#include "owner_census_probe.hpp"\n#include "capacity_cpu_probe.hpp"\n'),
        ('namespace datapump::modem {\n','namespace datapump::modem {\nnamespace rx_cpu=::datapump::detail::cpu_profile;\n'),
        ('    Impl(Config c,std::size_t bytes,PatternSearch options)\n','    ~Impl(){::datapump::diagnostic::capacity_cpu::destroyed(*this);}\n    Impl(Config c,std::size_t bytes,PatternSearch options)\n'),
        ('        ~ScoringScope() {state.drop_scoring();state.release_idle_scratch();}\n',
         '        ~ScoringScope() {state.drop_scoring();state.release_idle_scratch();::datapump::diagnostic::capacity_cpu::observe(state);}\n'),
        ('                         std::uint64_t latest_start=std::numeric_limits<std::uint64_t>::max()) {\n',
         '                         std::uint64_t latest_start=std::numeric_limits<std::uint64_t>::max()) {\n        rx_cpu::Scope cpu_continuation(rx_cpu::Stage::ordinary_continuation);\n'),
        ('            cancelled(stop);\n            const auto observed=static_cast<std::size_t>(std::min<std::uint64_t>(hop,bins-length+1-next_start));\n',
         '            cancelled(stop);\n            rx_cpu::Scope cpu_acquisition(rx_cpu::Stage::ordinary_acquisition);\n            const auto observed=static_cast<std::size_t>(std::min<std::uint64_t>(hop,bins-length+1-next_start));\n'),
      ],
      'audio':[
        ('        std::strncmp(event,"capacity_shape_",15)==0 || std::strncmp(event,"owner_census_",13)==0 ||\n',
         '        std::strncmp(event,"capacity_shape_",15)==0 || std::strncmp(event,"owner_census_",13)==0 ||\n        std::strncmp(event,"capacity_cpu_",13)==0 || std::strcmp(event,"receiver_component_cpu")==0 ||\n'),
        ('kept trace_begin, simulation_media_clock_case, capacity_shape_*, owner_census_*, receiver selection/admission/retirement/refusal and shared-capacity events;',
         'kept trace_begin, simulation_media_clock_case, capacity_shape_*, owner_census_*, capacity_cpu_*, receiver_component_cpu, receiver selection/admission/retirement/refusal and shared-capacity events;'),
      ],
      'test':[
        ('#include "../src/capacity_shape_probe.hpp"\n','#include "../src/capacity_shape_probe.hpp"\n#include "../src/capacity_cpu_probe.hpp"\n'),
        ('::datapump::diagnostic::capacity_shape::begin_case(label);}',
         '::datapump::diagnostic::capacity_shape::begin_case(label);::datapump::diagnostic::capacity_cpu::case_marker(label,"begin");}'),
        ('::datapump::diagnostic::capacity_shape::end_case(label,session.snapshot());',
         '::datapump::diagnostic::capacity_shape::end_case(label,session.snapshot());::datapump::diagnostic::capacity_cpu::case_marker(label,"end");'),
      ],
      'profiler':[
        ('scheduled_candidate_residual, common_raw, count\n','scheduled_candidate_residual, common_raw, ordinary_acquisition, ordinary_continuation, ordinary_source_generation, count\n'),
        ('"scheduled_common_residual","scheduled_candidate_residual","common_raw"\n',
         '"scheduled_common_residual","scheduled_candidate_residual","common_raw","ordinary_acquisition","ordinary_continuation","ordinary_source_generation"\n'),
      ],
      'parallel':[
        ('            profile_owner_ = rx_cpu::current_collector();\n',
         '            profile_owner_ = rx_cpu::current_collector();\n            profile_stage_ = rx_cpu::active?rx_cpu::active->stage:rx_cpu::Stage::search_residual;\n'),
        ('        rx_cpu::Context cpu(profile_owner_,rx_cpu::Stage::search_residual);\n',
         '        rx_cpu::Context cpu(profile_owner_,profile_stage_);\n'),
        ('    rx_cpu::Collector* profile_owner_ = nullptr; // Immutable until all batch workers join.\n',
         '    rx_cpu::Collector* profile_owner_ = nullptr; // Immutable until all batch workers join.\n    rx_cpu::Stage profile_stage_=rx_cpu::Stage::search_residual; // Same joined submission lifetime.\n'),
      ],
    }
    for call in ['wave->prepare_simulation(*wave,observation_epoch(*simulation_bank,value,simulation_bank->samples));','prepare(*wave,observation_epoch(*simulation_bank,value,simulation_bank->samples));']:
        cpu_edits['live'].append((call,'::datapump::diagnostic::capacity_cpu::generate([&]{'+call+'});'))
    for old,new in [
      ('simulation_channel->read(', '::datapump::diagnostic::capacity_cpu::read(*simulation_channel,'),
      ('simulation_channel->read_noise(', '::datapump::diagnostic::capacity_cpu::read_noise(*simulation_channel,'),
    ]:
        if not live2.count(old):raise ValueError('missing source generation calls')
        cpu_edits['live'].append((live2,live2.replace(old,new)))
        live2=live2.replace(old,new)
    # The two call replacements above are already applied; retain their exact
    # inverse anchors separately from the other original postimage edits.
    calls_reverse=live2
    for old,new in reversed(cpu_edits['live'][5:]):calls_reverse=once(calls_reverse,new,old)
    cpu_edits['live']=cpu_edits['live'][:5]
    live3,live_cpu_reverse=edits_from(live2,cpu_edits['live'])
    # calls_reverse equals the accepted capacity-shape postimage.
    live_cpu_reverse=calls_reverse
    pattern3,pattern_cpu_reverse=edits_from(pattern2,cpu_edits['pattern'])
    audio3,audio_cpu_reverse=edits_from(audio2,cpu_edits['audio'])
    test3,test_cpu_reverse=edits_from(test2,cpu_edits['test'])
    profiler2,profiler_reverse=edits_from(profiler,cpu_edits['profiler'])
    parallel2,parallel_reverse=edits_from(parallel,cpu_edits['parallel'])
    cpu_intermediate={'src/live.cpp':live_cpu_reverse,'src/pattern_receiver.cpp':pattern_cpu_reverse,
        'src/audio_trace.hpp':audio_cpu_reverse,'tests/test_live.cpp':test_cpu_reverse}
    live2,pattern2,audio2,test2=live3,pattern3,audio3,test3
    cpu_support=(HERE/'capacity_cpu_probe.hpp').read_bytes()
    cpu_contract=json.loads((HERE/'cpu-contract.json').read_text())

    out.mkdir(parents=True)
    files={
      'prepared-source/src/scheduled_work_profile.hpp':profiler2,'prepared-source/src/search_parallel.cpp':parallel2,
      'scheduled_work_profile.reversed.hpp':profiler_reverse,'search_parallel.reversed.cpp':parallel_reverse,
      'prepared-source/src/live.cpp':live2,'prepared-source/src/pattern_receiver.cpp':pattern2,
      'prepared-source/src/audio_trace.hpp':audio2,'prepared-source/tests/test_live.cpp':test2,
      'live.reversed.cpp':live_reverse,'audio_trace.reversed.hpp':audio_reverse,'test_live.reversed.cpp':test_reverse,
      'pattern_receiver.reversed.cpp':pattern_reverse,
      'live.patch':'','pattern_receiver.patch':'','audio_trace.patch':'','test_live.patch':'',
    }
    for rel,content in files.items():
        p=out/rel;p.parent.mkdir(parents=True,exist_ok=True)
        if rel.endswith('.patch'):
            source=live if rel=='live.patch' else pattern if rel=='pattern_receiver.patch' else audio if rel=='audio_trace.patch' else test
            target=live2 if rel=='live.patch' else pattern2 if rel=='pattern_receiver.patch' else audio2 if rel=='audio_trace.patch' else test2
            name='src/live.cpp' if rel=='live.patch' else 'src/pattern_receiver.cpp' if rel=='pattern_receiver.patch' else 'src/audio_trace.hpp' if rel=='audio_trace.patch' else 'tests/test_live.cpp'
            content=''.join(difflib.unified_diff(source.splitlines(True),target.splitlines(True),fromfile='base/'+name,tofile='private/'+name))
        p.write_text(content)
        if p.read_text()!=content:raise ValueError(f'readback mismatch: {rel}')
    (out/'prepared-source/src/capacity_cpu_probe.hpp').write_bytes(cpu_support)
    for rel,content in cpu_intermediate.items():
        dest=out/'cpu-intermediate'/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(content)
    for name,before,after in [('scheduled_work_profile',profiler,profiler2),('search_parallel',parallel,parallel2)]:
        suffix='.hpp' if name=='scheduled_work_profile' else '.cpp'
        (out/(name+'.patch')).write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='base/src/'+name+suffix,tofile='private/src/'+name+suffix)))
    (out/'prepared-source/src/owner_census_probe.hpp').write_bytes(support)
    (out/'prepared-source/src/capacity_shape_probe.hpp').write_bytes(shape)
    manifest={
      'schema':1,'status':'private_preparation_only','base_commit':BASE_COMMIT,'source_root':str(root),'output_dir':str(out),
      'source_closure':actual,'sources':{
        'src/live.cpp':{'before_sha256':sha(live.encode()),'after_sha256':sha(live2.encode()),'reverse_sha256':sha(live_reverse.encode())},
        'src/pattern_receiver.cpp':{'before_sha256':sha(pattern.encode()),'after_sha256':sha(pattern2.encode()),'reverse_sha256':sha(pattern_reverse.encode())},
        'src/audio_trace.hpp':{'before_sha256':sha(audio.encode()),'after_sha256':sha(audio2.encode()),'reverse_sha256':sha(audio_reverse.encode())},
        'tests/test_live.cpp':{'before_sha256':sha(test.encode()),'after_sha256':sha(test2.encode()),'reverse_sha256':sha(test_reverse.encode())},
        'src/scheduled_work_profile.hpp':{'before_sha256':sha(profiler.encode()),'after_sha256':sha(profiler2.encode()),'reverse_sha256':sha(profiler_reverse.encode())},
        'src/search_parallel.cpp':{'before_sha256':sha(parallel.encode()),'after_sha256':sha(parallel2.encode()),'reverse_sha256':sha(parallel_reverse.encode())}},
      'launcher_baseline_commit':'f08dec051ea86b4e0356c53ad1bf16bd0c228ca2',
      'cpu_partition_contract':cpu_contract,'capacity_cpu_support_sha256':sha(cpu_support),'cpu_contract_sha256':sha((HERE/'cpu-contract.json').read_bytes()),
      'tool_sha256':sha(Path(__file__).read_bytes()),'canonical_tool_hashes':tool_hashes,'canonical_tool_root':CANONICAL,'owner_cap_derivation':'canonical cap changed 128->512 and row cap 2400->11000; same producer code',
      'owner_census_support_sha256':sha(support),'capacity_shape_support_sha256':sha(shape),
      'diagnostic_environment':{'DATAPUMP_CAPACITY_SHAPE_DIAGNOSTIC':'1','DATAPUMP_OWNER_CENSUS_DIAGNOSTIC':'1','DATAPUMP_CAPACITY_CPU_DIAGNOSTIC':'1','DATAPUMP_RX_CPU_PROFILE':'coarse'},
      'runner_argv':['test_live','--simulation-media-clock-trace','<trace-path>'],
      'settings':{'settings_dsp_workspace_bytes':536870912,'transfer_dsp_workspace_bytes':536870912},
      'fixture_invariants':['original natural epoch clock and key','original geometry/search/inputs/assertions','instrumented sampled-work budget remains 120s','generated samples are not transmitted samples'],
      'capture_contract':{'aggregate_event':'capacity_shape_point','summary_event':'capacity_shape_summary','first_refusal':'existing schema-2 owner_census producer, semantics unchanged','owner_cap':512,'journal_cap':512,'aggregate_event_cap':512,'signature_cap':32,'truncation_explicit':True,'peak_and_end':'owner/bytes peaks plus per-case endpoint'},
      'metrics':['joined-boundary bank sample, observed sample and epoch','resident/admitted/retired; exact monotonic joined-bank peak owner count and separately labeled sampled owner count','stored ledger and independently recomputed base/shared/receiver component sums','shared pool/deferred reservations exactly once','latest actual retirement-predicate journal counts when complete','windowed/local-fallback/config signatures; selected backend is preserved in existing receiver_selection events','reserved/working receiver bytes','byte maxima are sampled at joined refresh boundaries, not a global allocator peak','selected owner-local PatternReceiver bit limits are unavailable except in refusal census','public DSP bytes and health at case end'],
      'endpoint_contract':{'source':'Snapshot transmission_seconds and transmission_fraction at TX completion; based on transmitted samples','generated':'transmit_trace.generated_bits and total_wire_bits are separate source-generation counters','received':'samples_received is receiver progress; simulation_receiving_tail is post-source tail','channel_origin':'actual active case, receive sample origin, bank coordinates, seed/settings, startup offset and carrier phase'},
      'aggregate_sampling_scope':'refresh_bank joined refresh boundaries after its refresh work; pointwise byte maxima are sampled, exact owner peak is bank.peak_receivers and end public peak_receiver_instances',
      'timing_scope':'capacity-shape observer wall and calling-thread CPU only; census separately scoped; trace writer and whole-fixture overhead not measured',
      'trace_filter':{'gated_by':'DATAPUMP_CAPACITY_SHAPE_DIAGNOSTIC=1','retained':['trace_begin','simulation_media_clock_case','capacity_shape_*','owner_census_*','receiver_bank_build','receiver_bank_admitted','receiver_bank_retired','receiver_selection','receiver_allocation_refused','receiver_profile_unavailable','receiver_optional_scratch_reclaimed','receiver_shared_temporal_plans_retained','receiver_shared_capture_reserved','receiver_shared_capture_unavailable'],'omitted':'per-bit confirmation/history and unrelated hot diagnostic events; filter runs before sequence/attempt counters so intentional omissions are not trace drops'},
      'unmeasured_dimensions':['owner-local PatternReceiver candidate cap, phase/timing bit bounds, exact per-owner cache sizing when no first refusal census occurs; aggregate contains candidate_limited and actual reserved/working sums, and selected backend remains in existing receiver_selection trace events'],
      'interpretation_limit':'512MiB changes strategy/backend/cache/bit grants; natural-origin result is a diagnostic capacity-shape wave, not matched performance comparison or qualification',
      'preserved_dirty_file_sha256':sha((root/'docs/validation.md').read_bytes()) if (root/'docs/validation.md').exists() else None,
    }
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    if json.loads((out/'manifest.json').read_text())!=manifest:raise ValueError('manifest readback mismatch')
    return manifest
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source-root',required=True,type=Path);p.add_argument('--output-dir',required=True,type=Path)
    a=p.parse_args();print(json.dumps(prepare(a.source_root,a.output_dir),indent=2))
