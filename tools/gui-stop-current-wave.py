#!/usr/bin/env python3
"""Unmatched current-source Fake-EU critical-path capture; never qualification.

prepare/restore mutate only an explicitly supplied isolated checkout. run invokes
full original test_gui_controller (no arguments), optionally with a deliberately
partial preparation co-load. It does not build, dispatch CI, or alter deadlines.
"""
from __future__ import annotations
import argparse, difflib, hashlib, json, os, re, resource, runpy, signal
import subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
SUPPORT = HERE / 'gui-stop-current-wave'
HEADER = 'src/gui_stop_current_probe.hpp'
RUNTIME = 'a6c2bf11d255fd00f35ecc238a56e144e7605145'
PATHS = ('src/live.cpp', 'src/channel.cpp', 'src/pattern_code.cpp',
         'src/audio_trace.hpp', 'tests/test_gui_controller.cpp',
         'src/pattern_receiver.cpp', 'src/scheduled_work_profile.hpp', 'src/search_parallel.cpp',
         'src/pattern_fft_batch.cpp', 'src/search_fft.cpp')

def sha(value): return hashlib.sha256(value).hexdigest()
def write(path, value): path.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n')
def load(path): return json.loads(path.read_text())
def git(root, *args):
    return subprocess.check_output(['git', '-c', 'safe.directory='+str(root), *args], cwd=root, text=True).strip()
def support_hashes():
    return {str(p.relative_to(HERE)): sha(p.read_bytes()) for p in (Path(__file__).resolve(), SUPPORT/'probe.hpp', SUPPORT/'pins.json', SUPPORT/'README.md', SUPPORT/'native-substages.json')}
def replace_once(text, old, new):
    if text.count(old) != 1: raise ValueError('nonunique overlay anchor: '+old[:160])
    return text.replace(old, new)

def transform(original):
    d = dict(original)
    for name in ('src/live.cpp','src/channel.cpp','src/pattern_code.cpp'):
        d[name] = '#include "gui_stop_current_probe.hpp"\n'+d[name]
    p='src/audio_trace.hpp'
    d[p]='#include <cstdlib>\n#include <string_view>\n'+d[p]
    a='template<class F>void record(const char* event,F fields) noexcept {\n'
    d[p]=replace_once(d[p],a,a+'''    // PRIVATE capture filters before sequence assignment; footer is unchanged.
    static const bool gui_stop_only=[] {const auto* v=std::getenv("DATAPUMP_GUI_STOP_CURRENT");return v&&std::string_view(v)=="1";}();
    if(gui_stop_only && std::string_view(event)!="trace_begin" &&
       !std::string_view(event).starts_with("gui_stop_current_"))return;
''')
    p='tests/test_gui_controller.cpp'
    d[p]='#include "../src/gui_stop_current_probe.hpp"\n'+d[p]
    a='    for(const auto* preference:{"fake-fcc","fake-eu","fake-list"}) {\n    Controller controller({true,true});'
    d[p]=replace_once(d[p],a,'''    for(const auto* preference:{"fake-fcc","fake-eu","fake-list"}) {
    datapump::gui_stop_current::Case stop_capture(std::string_view(preference)=="fake-eu",fixture.path);
    Controller controller({true,true});''')
    a='do {controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));}'
    if d[p].count(a)!=2: raise ValueError('strict stop-loop anchors changed')
    d[p]=d[p].replace(a,'do {{datapump::gui_stop_current::Poll stop_poll;controller.poll();datapump::gui_stop_current::ack(controller.snapshot());}std::this_thread::sleep_for(std::chrono::milliseconds(5));}')
    a='    const auto stop_wall_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-stop_started).count();'
    d[p]=replace_once(d[p],a,a+'\n    datapump::gui_stop_current::ack(controller.snapshot(),true);\n    datapump::gui_stop_current::point("strict_stop_observation",{{"request",datapump::gui_stop_current::state.request_count.load()},{"transmitting",controller.snapshot().transmitting},{"finished",controller.snapshot().transmission_finished}},{{"stop_wall_seconds",stop_wall_seconds}});\n    datapump::gui_stop_current::cpu_ledger("strict_stop_observation");')
    p='src/live.cpp'
    d[p]=replace_once(d[p],'audio::trace::record("receiver_selection",[&](auto& f) {','datapump::gui_stop_current::detail("receiver_selection",[&](auto& f) {')
    a='                 std::uint64_t observation_id = 0, bool preserve_monitor = false) {\n'
    d[p]=replace_once(d[p],a,a+'        datapump::gui_stop_current::Interval stop_publication("publication_begin","publication_end",{{"serial",serial},{"generation",version},{"force",force}});\n')
    a='        std::array<std::complex<double>,plot_size> projected{};\n        const auto rotation='
    d[p]=replace_once(d[p],a,'        datapump::gui_stop_current::Feed stop_feed(samples,bank.samples,bank.revision,version,bank.source_quota.get());\n'+a)
    a='''void Session::finish_transmit() {
    {
        std::lock_guard lock(impl_->mutex);'''
    d[p]=replace_once(d[p],a,'''void Session::finish_transmit() {
    datapump::gui_stop_current::request_before();
    {
        std::lock_guard lock(impl_->mutex);
        datapump::gui_stop_current::point("request_lock_acquired",{{"serial",impl_->current.transmission_id},{"generation",impl_->generation},{"revision",impl_->receive_revision}});''')
    a='''            impl_->current.status="Finishing echo protection and pulse tails; cancel again to abort immediately";
            impl_->changed.notify_all();return;'''
    d[p]=replace_once(d[p],a,'''            impl_->current.status="Finishing echo protection and pulse tails; cancel again to abort immediately";
            datapump::gui_stop_current::point("request_installed",{{"serial",impl_->current.transmission_id},{"generation",impl_->generation},{"revision",impl_->receive_revision}},{{"published_source_seconds",impl_->current.transmission_seconds}});
            impl_->changed.notify_all();return;''')
    a='''        source_pcm_started=true;
        if(finish_requested && !wave.graceful) {
            wave.transmitter->request_finish();wave.graceful=true;
        }'''
    d[p]=replace_once(d[p],a,'''        source_pcm_started=true;
        datapump::gui_stop_current::serial=wave.serial;
        if(finish_requested && !wave.graceful) {
            datapump::gui_stop_current::point("endpoint_before",{{"serial",wave.serial},{"generated",wave.transmitter->samples_emitted()},{"transmitted",wave.transmitted_samples},{"endpoint",wave.transmitter->total_samples()}});
            wave.transmitter->request_finish();wave.graceful=true;
            datapump::gui_stop_current::point("endpoint_installed",{{"serial",wave.serial},{"generated",wave.transmitter->samples_emitted()},{"transmitted",wave.transmitted_samples},{"endpoint",wave.transmitter->total_samples()}});
        }''')
    a='''        changed.notify_all();
    }
    void progress(const Prepared& wave, const Settings& value) {'''
    d[p]=replace_once(d[p],a,'''        datapump::gui_stop_current::point("internal_completion",{{"serial",wave.serial},{"generation",wave.generation},{"transmitting",current.transmitting},{"finished",current.transmission_finished},{"failed",!error.empty()},{"generated",wave.transmitter?wave.transmitter->samples_emitted():0},{"transmitted",wave.transmitted_samples},{"endpoint",wave.transmitter?wave.transmitter->total_samples():0}});
        changed.notify_all();
    }
    void progress(const Prepared& wave, const Settings& value) {''')
    a='''                        simulation_channel=std::move(channel);simulation_origin=origin;'''
    d[p]=replace_once(d[p],a,a+'\n                        datapump::gui_stop_current::point("media_origin",{{"generation",version},{"source_sample",simulation_channel->received_samples()}},{{"anchor_epoch",origin}});')
    a='''                        simulation_bank = make_bank(value,std::move(initial));'''
    d[p]=replace_once(d[p],a,a+'\n                        datapump::gui_stop_current::config("receiver",value.transfer.modem);\n                        datapump::gui_stop_current::point("bank_created",{{"generation",version},{"revision",simulation_bank->revision},{"source_sample",simulation_channel->received_samples()},{"bank_sample",simulation_bank->samples},{"receiver_timestamp",value.transfer.timestamp},{"custom_epoch_clock",custom_epoch_clock}},{{"observed_epoch",simulation_bank->observed_epoch.value_or(0.)}});')
    a='''                            transmitter=std::make_unique<modem::StreamingTransmitter>(modem::Noise{},config,value.dsp_workspace_bytes/4);'''
    d[p]=replace_once(d[p],a,'''                            datapump::gui_stop_current::point("noise_factory_begin",{{"serial",wave.serial},{"stream_epoch",config.stream_epoch},{"stream_phase_samples",config.stream_phase_samples}});
                            datapump::gui_stop_current::config("tx_noise_factory_geometry",config);
                            transmitter=std::make_unique<modem::StreamingTransmitter>(modem::Noise{},config,value.dsp_workspace_bytes/4);
                            datapump::gui_stop_current::point("noise_factory_end",{{"serial",wave.serial},{"stream_epoch",config.stream_epoch},{"stream_phase_samples",config.stream_phase_samples}});''')
    p='src/channel.cpp'
    a='''std::size_t SampledSimulationChannel::read(StreamingTransmitter& source,std::span<float> output,std::stop_token stop) {
    auto& s=*impl_;'''
    d[p]=replace_once(d[p],a,a+'\n    datapump::gui_stop_current::Read stop_read(*this,&source,output);')
    a='''void SampledSimulationChannel::read_noise(std::span<float> output,std::stop_token stop) {
    auto& s=*impl_;'''
    d[p]=replace_once(d[p],a,a+'\n    datapump::gui_stop_current::Read stop_read(*this,nullptr,output);')
    a='''        burst_rotation=phase_rotation(tau*config.carrier_hz*rate*(static_cast<long double>(origin)+startup)/config.sample_rate);'''
    d[p]=replace_once(d[p],a,a+'''
        namespace GS=datapump::gui_stop_current;
        GS::detail("channel_begin",[&](auto& f){f.add("channel",reinterpret_cast<std::uintptr_t>(this));f.add("received",received);f.add("origin",origin);f.add("buffer_start",buffer_start);f.add("buffer_count",buffer_count);f.add("startup",startup);f.add("rate",rate);f.add("seed",channel.seed);f.add("snr_db",channel.snr_db);f.add("clock_error_ppm",channel.clock_error_ppm);f.add("frequency_offset_hz",channel.frequency_offset_hz);f.add("phase_noise",channel.phase_noise_degrees_per_sqrt_second);f.add("delay_samples",channel.delay_samples);f.add("oscillator_real",oscillator.real());f.add("oscillator_imag",oscillator.imag());f.add("rotation_real",burst_rotation.real());f.add("rotation_imag",burst_rotation.imag());});
        const auto witness=[&](const char* label,const auto& engine,const auto& distribution){GS::detail("channel_rng",[&](auto& f){std::ostringstream e,d;e<<engine;d<<distribution;f.add("channel",reinterpret_cast<std::uintptr_t>(this));f.add("origin",origin);f.add("engine",label);f.add("state",e.str());f.add("distribution",d.str());});};
        if(GS::active()){witness("startup",startup_random,0);witness("phase",phase_random,phase_normal);witness("noise",noise_random,noise_normal);}
''')
    p='src/pattern_code.cpp'
    start=d[p].index('Config NoiseTransmitter::prepare_config(Config config) {')
    end=d[p].index('\n}',start)
    block=d[p][start:end]
    block=replace_once(block,'    return config;','    datapump::gui_stop_current::config("prepared_noise",config);\n    return config;')
    d[p]=d[p][:start]+block+d[p][end:]
    spec=load(SUPPORT/'native-substages.json')
    if spec['schema']!=2:raise ValueError('unknown native substage patch schema')
    for item in spec['files']:
        name=item['path'];text=d[name]
        if sha(text.encode())!=item['source_sha256']:raise ValueError('stale substage source '+name)
        lines=text.splitlines(True)
        for edit in reversed(item['edits']):
            begin,end=edit['start_line0'],edit['end_line0']
            if lines[begin:end]!=edit['old']:raise ValueError('substage line mismatch '+name)
            lines[begin:end]=edit['new']
        d[name]=''.join(lines)
        if sha(d[name].encode())!=item['postimage_sha256']:raise ValueError('wrong substage postimage '+name)
    return d

def prepare(root, report):
    pins=load(SUPPORT/'pins.json')
    for p,h in pins.items():
        if sha((root/p).read_bytes())!=h: raise ValueError('stale current-runtime read pin '+p)
    if report.exists(): raise ValueError('report must be fresh')
    if (root/HEADER).exists(): raise ValueError('private header already exists')
    original={p:(root/p).read_text() for p in PATHS}; changed=transform(original)
    report.mkdir(parents=True); (report/'originals').mkdir()
    for p in PATHS:
        q=report/'originals'/p;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes((root/p).read_bytes())
    source=git(root,'rev-parse','HEAD') if (root/'.git').exists() else None
    inventory={str(p.relative_to(root)):sha(p.read_bytes()) for p in root.rglob('*') if p.is_file() and not any(x in {'.git','.agent-work','build','__pycache__'} for x in p.relative_to(root).parts)}
    manifest={'schema':1,'assignment':'gui-stop-current-identity-wave','runtime':RUNTIME,'source_commit':source,'read_pins':pins,'originals':{p:sha((root/p).read_bytes()) for p in PATHS},'postimages':{p:sha(v.encode()) for p,v in changed.items()},'private_header':HEADER,'header_sha256':sha((SUPPORT/'probe.hpp').read_bytes()),'support':support_hashes(),'source_inventory':inventory,'qualification':False,'matched':False,'source_semantics':'original full noarg fixture, natural inputs/default clock/stop; coarse observation only'}
    write(report/'manifest.json',manifest)
    (report/'overlay.patch').write_text(''.join(''.join(difflib.unified_diff(original[p].splitlines(True),changed[p].splitlines(True),fromfile='a/'+p,tofile='b/'+p)) for p in PATHS))
    for p,v in changed.items(): (root/p).write_text(v)
    (root/HEADER).write_bytes((SUPPORT/'probe.hpp').read_bytes())
    check_prepared(root,report)
    return manifest

def check_prepared(root,report):
    m=load(report/'manifest.json')
    for p,h in m['postimages'].items():
        if sha((root/p).read_bytes())!=h: raise ValueError('changed prepared source '+p)
    for p,h in m['read_pins'].items():
        if p not in m['postimages'] and sha((root/p).read_bytes())!=h: raise ValueError('changed read input '+p)
    if sha((root/HEADER).read_bytes())!=m['header_sha256']: raise ValueError('changed private header')
    if support_hashes()!=m['support']: raise ValueError('tool/support changed after preparation')
    if m['source_commit'] and git(root,'rev-parse','HEAD')!=m['source_commit']: raise ValueError('checkout HEAD changed')
    return m

def restore(root,report):
    m=load(report/'manifest.json'); states={}
    for p,h in m['originals'].items():
        h0=sha((report/'originals'/p).read_bytes())
        if h0!=h: raise ValueError('changed saved original '+p)
        actual=sha((root/p).read_bytes())
        if actual not in (h,m['postimages'][p]):raise ValueError('unknown restoration state '+p)
        states[p]='pre' if actual==h else 'post'
    q=root/HEADER
    if q.exists() and sha(q.read_bytes())!=m['header_sha256']: raise ValueError('unknown header state')
    for p in m['originals']:(root/p).write_bytes((report/'originals'/p).read_bytes())
    if q.exists():q.unlink()
    assert all(sha((root/p).read_bytes())==h for p,h in m['originals'].items())
    write(report/'restoration.json',{'restored':True,'states_before':states,'originals':m['originals'],'header_removed':True})

def proc_stat(path):
    text=path.read_text(); tail=text[text.rindex(')')+2:].split()
    return {'state':tail[0],'utime_ticks':int(tail[11]),'stime_ticks':int(tail[12]),'starttime_ticks':int(tail[19])}
def read_small(path,limit=16384):
    try:
        value=path.read_text()
        return value if len(value)<=limit else {'unavailable':'oversize'}
    except OSError as e:return {'unavailable':type(e).__name__}
def sample(pid):
    base=Path('/proc')/str(pid); out={'pid':pid,'tasks':[]}
    try:
        out['process']=proc_stat(base/'stat');out['status']=read_small(base/'status')
        for t in sorted((base/'task').iterdir(),key=lambda p:int(p.name))[:64]:
            try:out['tasks'].append({'tid':int(t.name),**proc_stat(t/'stat'),'schedstat':read_small(t/'schedstat'),'affinity':sorted(os.sched_getaffinity(int(t.name)))})
            except (OSError,ValueError):pass
        out['task_limit_exceeded']=len(list((base/'task').iterdir()))>64
        out['affinity']=sorted(os.sched_getaffinity(pid))
    except (OSError,ValueError) as e:out['unavailable']=type(e).__name__
    return out

def poll_child(p):
    if hasattr(p,'wave_usage'):return p.returncode
    pid,status,usage=os.wait4(p.pid,os.WNOHANG)
    if not pid:return None
    p.returncode=os.waitstatus_to_exitcode(status);p.wave_usage={'user_seconds':usage.ru_utime,'system_seconds':usage.ru_stime,'maxrss_kib':usage.ru_maxrss}
    return p.returncode

def group_members(pgid):
    out=[]
    for p in Path('/proc').iterdir():
        if not p.name.isdigit():continue
        try:
            pid=int(p.name)
            if os.getpgid(pid)==pgid:out.append({'pid':pid,**proc_stat(p/'stat')})
        except (OSError,ValueError):pass
    return out

def close_group(p):
    actions=[]
    for sig,seconds in [(signal.SIGTERM,5),(signal.SIGKILL,5)]:
        try:os.killpg(p.pid,sig);actions.append(sig.name)
        except ProcessLookupError:pass
        until=time.monotonic()+seconds
        while poll_child(p) is None and time.monotonic()<until:time.sleep(.05)
        if p.returncode is not None:break
    if poll_child(p) is None:raise RuntimeError('child could not be reaped')
    # Kill descendants even if their leader has already exited.
    try:os.killpg(p.pid,signal.SIGKILL);actions.append('descendant_SIGKILL')
    except ProcessLookupError:pass
    return {'actions':actions,'leader_reaped':True,'exit':p.returncode,'usage':p.wave_usage,'remaining_group_members':group_members(p.pid)}

def build_evidence(root,binary):
    cache=root/'build/CMakeCache.txt';commands=root/'build/compile_commands.json'
    if not cache.is_file() or not commands.is_file():raise ValueError('build cache and compile_commands.json required')
    values={}
    for line in cache.read_text().splitlines():
        if line and not line.startswith(('#','//')) and '=' in line and ':' in line.split('=',1)[0]:
            k,v=line.split('=',1);values[k.split(':',1)[0]]=v
    for key,want in {'CMAKE_BUILD_TYPE':'Debug','DATAPUMP_SANITIZERS':'ON','DATAPUMP_PORTABLE':'OFF'}.items():
        if values.get(key)!=want:raise ValueError('wrong diagnostic build '+key+'='+str(values.get(key)))
    selected=[c for c in load(commands) if Path(c['file']).name=='test_gui_controller.cpp']
    if len(selected)!=1:raise ValueError('expected one GUI compile command')
    command=selected[0].get('command') or ' '.join(selected[0]['arguments'])
    if '-DDATAPUMP_INSTRUMENTED_TEST=1' not in command or '-fsanitize=address,undefined' not in command:
        raise ValueError('GUI diagnostic requires actual instrumented ASan+UBSan compile command')
    return {'cache':str(cache),'cache_sha256':sha(cache.read_bytes()),'compile_commands':str(commands),'compile_commands_sha256':sha(commands.read_bytes()),'gui_compile':selected[0],'actual_binary':str(binary)}

def capture_readback(out):
    rows=[json.loads(line) for line in (out/'trace.jsonl').read_text().splitlines()]
    summaries=[x for x in rows if x.get('event')=='gui_stop_current_summary']
    if len(summaries)!=1:raise ValueError('expected one capture summary')
    summary=summaries[0]
    if summary.get('schema')!=3 or summary.get('float_bytes')!=4 or summary.get('unmatched')!=1 or summary.get('qualification')!=0:raise ValueError('unexpected producer contract')
    for name in ('record_overflow','detail_overflow','cpu_ledger_overflow','partition_overflow','errors','clock_errors','observer_invalid','poll_invalid','digest_error'):
        if summary.get(name)!=0:raise ValueError('incomplete capture '+name)
    observed=[x for x in rows if x.get('event','').startswith('gui_stop_current_') and 'capture_index' in x]
    if [x['capture_index'] for x in observed]!=list(range(summary['records'])):raise ValueError('capture index gap/duplicate')
    if sum(x.get('event')=='gui_stop_current_detail' for x in rows)!=summary['details']:raise ValueError('detail count')
    if sum(x.get('event')=='gui_stop_current_case_joined' for x in observed)!=1:raise ValueError('missing joined target boundary')
    parts=[x for x in rows if x.get('event')=='gui_stop_current_pcm_partition']
    if [x['ordinal'] for x in parts]!=list(range(summary['partitions'])):raise ValueError('partition gap/duplicate')
    committed=0;stored=0;uncertain=0
    for x in parts:
        if x['sample_offset']!=committed:raise ValueError('partition offset not committed prefix')
        if x['committed']:
            if x.get('read_id',1)==0:raise ValueError('committed feed lacks source read identity')
            if 'source_first' in x and x['source_end']-x['source_first']!=x['count']:raise ValueError('committed source/read count mismatch')
            if x['unwinding'] or x['bank_after']-x['bank_before']!=x['count']:raise ValueError('committed partition contradiction')
            committed+=x['count'];stored+=x['stored_count']
        else:uncertain+=1
    if (committed,stored,uncertain)!=(summary['committed_samples'],summary['pcm_stored_samples'],summary['uncertain_feeds']):raise ValueError('PCM partition totals')
    pcm=(out/'committed-input.f32').read_bytes()
    if len(pcm)!=stored*summary['float_bytes']:raise ValueError('PCM file length')
    if not summary['pcm_truncated'] and sha(pcm)!=summary['committed_pcm_sha256']:raise ValueError('PCM full committed hash')
    statuses=[x for x in observed if x.get('event')=='gui_stop_current_snapshot_status']
    if len(statuses)!=summary['snapshot_status_count']:raise ValueError('snapshot status count')
    if sum(x['final'] for x in statuses)!=summary['snapshot_status_final_count']:raise ValueError('snapshot final count')
    if summary['snapshot_status_period_ns']!=200000000:raise ValueError('snapshot status period')
    if statuses and ([x['first'] for x in statuses]!=[1]+[0]*(len(statuses)-1)):raise ValueError('snapshot first flags')
    for i,x in enumerate(statuses):
        for flag in ('simulation','first','final','hardware_frontier_applicable','hardware_queues_applicable','bank_processed_frontier_available'):
            if x[flag] not in (0,1):raise ValueError('snapshot applicability flag')
        if x['hardware_frontier_applicable']!=1-x['simulation'] or x['hardware_queues_applicable']!=1-x['simulation'] or x['bank_processed_frontier_available']!=0:raise ValueError('snapshot applicability')
        if i and (x['status_hook_wall_ns']<statuses[i-1]['status_hook_wall_ns'] or (not x['final'] and x['status_hook_wall_ns']-statuses[i-1]['status_hook_wall_ns']<200000000)):raise ValueError('snapshot status rate')
    details=[json.loads(x['detail_json']) for x in rows if x.get('event')=='gui_stop_current_detail']
    ledgers=[x for x in details if x.get('event')=='gui_stop_current_cpu_ledger']
    boundaries=[x.get('boundary') for x in ledgers]
    keys=[(x.get('boundary'),x.get('request_ordinal')) for x in ledgers]
    if len(keys)!=len(set(keys)) or boundaries.count('case_begin')!=1 or boundaries.count('case_joined')!=1:raise ValueError('missing/duplicate CPU ledger boundary')
    allowed={'case_begin','request','strict_stop_observation','case_joined'}
    if not set(boundaries)<=allowed:raise ValueError('unknown CPU ledger boundary')
    if len(ledgers)!=summary.get('cpu_ledger_records') or len(details)-len(ledgers)!=summary.get('generic_details'):raise ValueError('CPU/generic detail count')
    if summary.get('detail_limit')!=256 or summary.get('cpu_ledger_limit')!=16 or any(summary.get(k)!=0 for k in ('detail_first_overflow_wall_ns','cpu_ledger_first_overflow_wall_ns')):raise ValueError('detail storage contract')
    requests_before=[x for x in observed if x.get('event')=='gui_stop_current_request_before_lock']
    ordinals=[x['request'] for x in requests_before]
    if ordinals!=list(range(1,len(ordinals)+1)):raise ValueError('request ordinal sequence')
    if sorted(x['request_ordinal'] for x in ledgers if x['boundary']=='request')!=ordinals:raise ValueError('request CPU ledger identity')
    for x in ledgers:
        ordinal=x.get('request_ordinal')
        if type(ordinal) is not int or ordinal<0:raise ValueError('invalid CPU ledger request ordinal')
        if x['boundary']=='case_begin' and ordinal!=0:raise ValueError('case begin CPU ledger identity')
        if x['boundary']=='case_joined' and ordinal!=len(ordinals):raise ValueError('case joined CPU ledger identity')
        if x['boundary']=='strict_stop_observation' and ordinal not in ordinals:raise ValueError('strict CPU ledger identity')
    strict_observed=[x for x in observed if x.get('event')=='gui_stop_current_strict_stop_observation']
    if sorted(x['request_ordinal'] for x in ledgers if x['boundary']=='strict_stop_observation')!=sorted(x['request'] for x in strict_observed):raise ValueError('strict observation CPU ledger identity')
    for x in ledgers:
        if x.get('cpu_profile_mode')!='exact_coarse' or x.get('cpu_profile_native_clock_supported')!=1:raise ValueError('CPU profile mode/clock')
        if any(x.get(k)!=0 for k in ('cpu_profile_clock_failures','cpu_profile_nesting_failures','cpu_profile_overflows')):raise ValueError('CPU profile accounting failure')
        for stage in ('native_acquired_batch','native_continuation','native_template_values','native_template_fft','native_fft_search','fallback_dispatch','native_frequency_axis','native_bounded_search','native_whole_search','native_fft_validate','native_fft_template_setup','native_fft_template_values','native_fft_forward','native_fft_product','native_fft_inverse','native_fft_scores','native_fft_drift','borrowed_fft_checks','borrowed_fft_permutation','borrowed_fft_butterflies','borrowed_fft_normalization','legacy_fft_setup','legacy_fft_permutation','legacy_fft_butterflies','legacy_fft_normalization'):
            for suffix in ('ns','calls'):
                value=x.get('cpu_'+stage+'_'+suffix)
                if type(value) is not int or value<0:raise ValueError('missing/invalid CPU stage '+stage)
        if x['boundary']=='case_joined' and (x.get('cpu_profile_complete')!=1 or x.get('cpu_profile_active_contexts')!=0):raise ValueError('joined CPU ledger not complete/quiescent')
    if any(x.get('event')=='gui_stop_current_request_before_lock' for x in observed) and 'request' not in boundaries:raise ValueError('missing request CPU ledger')
    if any(x.get('event')=='gui_stop_current_strict_stop_observation' for x in observed) and 'strict_stop_observation' not in boundaries:raise ValueError('missing strict observation CPU ledger')
    fft_shapes=[x for x in details if x.get('event')=='gui_stop_current_fft_shape']
    for x in fft_shapes:
        n=x.get('transform');kind=x.get('kind')
        if type(n) is not int or n<1 or n&(n-1) or type(kind) is not int or kind not in (0,1,2) or x.get('inverse')!=1 or x.get('vectorized') not in (0,1) or x.get('sample_period')!=257 or x.get('request_ordinal') not in ordinals:raise ValueError('invalid FFT shape')
        if x['vectorized'] and (kind!=1 or not 4<=n<=8192):raise ValueError('invalid vectorized FFT shape')
    requests=[x for x in observed if x.get('event')=='gui_stop_current_request_installed']
    primary=requests[0]['serial'] if requests else None
    def saw(event):return primary is not None and any(x.get('event')=='gui_stop_current_'+event and x.get('serial')==primary for x in observed)
    return {'capture_valid':True,'summary':summary,'cpu_ledgers':ledgers,'receiver_selections':[x for x in details if x.get('event')=='gui_stop_current_receiver_selection'],'fft_shapes':fft_shapes,'fft_leaf_sample_period':257,'snapshot_status_count':len(statuses),'simulation_frontier_note':'bank feed counts are ingress/commit witnesses; hardware frontier and queues are inapplicable in Simulation, so zero is not no backlog','primary_request_serial':primary,'pcm_file_sha256':sha(pcm),'stored_pcm_complete':not bool(summary['pcm_truncated']),'natural_eof_observed':saw('natural_eof'),'internal_completion_observed':saw('internal_completion'),'gui_ack_observed':saw('gui_ack'),'qualification':False,'matched':False,'limits':['capture is complete only through original teardown; failure may interrupt a feed or prevent EOF/GUI ack','thread CPU for straddling intervals includes work before request','request published source witness is coherent but may predate an in-flight read','snapshot hardware disposition fields are inapplicable in Simulation; no true simulation processed frontier captured']}

def run(root,report,binary,load_binary,condition,capture,timeout):
    m=check_prepared(root,report);out=report/'run';out.mkdir(exist_ok=False)
    if timeout<30 or timeout>900:raise ValueError('GUI external timeout must be30..900 seconds')
    if condition=='preparation' and (not load_binary or not load_binary.is_file()):raise ValueError('missing preparation binary')
    binary=binary.resolve();build=build_evidence(root,binary);env=dict(os.environ,DATAPUMP_GUI_STOP_CURRENT='1' if capture else '0',DATAPUMP_GUI_STOP_CURRENT_OUT=str(out),DATAPUMP_RX_CPU_PROFILE='coarse')
    bound={str(binary):sha(binary.read_bytes())}
    bound[build['cache']]=build['cache_sha256'];bound[build['compile_commands']]=build['compile_commands_sha256']
    bound.update({str(root/p):h for p,h in m['read_pins'].items() if p not in m['postimages']})
    bound.update({str(root/p):h for p,h in m['postimages'].items()});bound[str(root/HEADER)]=m['header_sha256']
    if load_binary:bound[str(load_binary.resolve())]=sha(load_binary.read_bytes())
    launch={'schema':1,'command':[str(binary)],'condition':condition,'capture':capture,'source_commit':m['source_commit'],'runtime':m['runtime'],'binding':bound,'support':m['support'],'build':build,'launcher_python':sys.version,'environment':{k:env[k] for k in ('DATAPUMP_GUI_STOP_CURRENT','DATAPUMP_GUI_STOP_CURRENT_OUT','DATAPUMP_RX_CPU_PROFILE')},'external_timeout_seconds':timeout,'qualification':False,'matched':False,'claims_forbidden':['ON/OFF overhead','load causation','failed-core waveform reconstruction']}
    write(out/'launch.json',launch)
    load_p=None;gui=None;handles=[];samples=[];sample_overflow=False;load_phase=False;load_early_exit=False;timed_out=False;closure={};started=time.monotonic();sampler_wall=0.;sampler_cpu=0.;error=None;load_log_before_gui='';load_log_at_gui_end=''
    previous_handlers={sig:signal.getsignal(sig) for sig in (signal.SIGTERM,signal.SIGINT)}
    def interrupted(signum,frame):raise InterruptedError('received signal '+str(signum))
    for sig in previous_handlers:signal.signal(sig,interrupted)
    try:
        if condition=='preparation':
            lo=(out/'preparation.stdout').open('wb');le=(out/'preparation.stderr').open('wb');handles.extend([lo,le]);load_env=dict(os.environ);load_env.pop('DATAPUMP_GUI_STOP_CURRENT',None)
            load_p=subprocess.Popen([str(load_binary.resolve())],cwd=root,env=load_env,stdout=lo,stderr=le,start_new_session=True)
            deadline=time.monotonic()+120
            while time.monotonic()<deadline:
                if poll_child(load_p) is not None:raise RuntimeError('preparation exited before arithmetic.serial start')
                text=(out/'preparation.stderr').read_text(errors='replace')
                if re.search(r'preparation_timing .*phase=arithmetic\.serial event=begin\b',text):load_phase=True;break
                time.sleep(.05)
            if not load_phase:raise RuntimeError('preparation phase start not observed within120s')
        if load_p:
            load_log_before_gui=(out/'preparation.stderr').read_text(errors='replace')
            if 'phase=arithmetic.serial event=end' in load_log_before_gui:raise RuntimeError('target preparation serial phase already ended before GUI launch')
        go=(out/'stdout.log').open('wb');ge=(out/'stderr.log').open('wb');handles.extend([go,ge]);gui=subprocess.Popen([str(binary)],cwd=root,env=env,stdout=go,stderr=ge,start_new_session=True)
        write(out/'process.json',{'gui_pid':gui.pid,'load_pid':load_p.pid if load_p else None,'gui_command':[str(binary)],'load_command':[str(load_binary)] if load_p else None,'proc_initial':sample(gui.pid),'clk_tck':os.sysconf('SC_CLK_TCK'),'sched_schedstats':read_small(Path('/proc/sys/kernel/sched_schedstats')),'cgroup':read_small(Path('/proc')/str(gui.pid)/'cgroup')})
        gui_started=time.monotonic()
        while poll_child(gui) is None:
            if time.monotonic()-gui_started>timeout:timed_out=True;break
            sw=time.monotonic();sc=time.process_time()
            row={'elapsed_seconds':time.monotonic()-gui_started,'wall_monotonic_ns':time.monotonic_ns(),'gui':sample(gui.pid),'cpu_pressure':read_small(Path('/proc/pressure/cpu')),'cgroup_cpu_stat':read_small(Path('/sys/fs/cgroup/cpu.stat')),'cgroup_cpu_pressure':read_small(Path('/sys/fs/cgroup/cpu.pressure')),'memory_current':read_small(Path('/sys/fs/cgroup/memory.current')),'memory_max':read_small(Path('/sys/fs/cgroup/memory.max'))}
            if load_p:
                if poll_child(load_p) is not None:load_early_exit=True
                row['load']=sample(load_p.pid)
            if len(samples)<4501:samples.append(row)
            else:sample_overflow=True
            sampler_wall+=time.monotonic()-sw;sampler_cpu+=time.process_time()-sc
            time.sleep(.2)
        if load_p:
            load_log_at_gui_end=(out/'preparation.stderr').read_text(errors='replace')
            if poll_child(load_p) is not None:load_early_exit=True
        if timed_out:closure['gui']=close_group(gui)
        else:closure['gui']={'reaped':True,'exit':gui.returncode,'usage':gui.wave_usage}
    except BaseException as e:error=type(e).__name__+': '+str(e)
    finally:
        try:
            if gui:
                if poll_child(gui) is None:closure['gui']=close_group(gui)
                elif group_members(gui.pid):closure['gui_descendant_cleanup']=close_group(gui)
        except Exception as e:closure['gui_cleanup_error']=type(e).__name__+': '+str(e)
        try:
            if load_p:closure['preparation']=close_group(load_p)
        except Exception as e:closure['preparation_cleanup_error']=type(e).__name__+': '+str(e)
        for f in handles:f.close()
        for sig,handler in previous_handlers.items():signal.signal(sig,handler)
    result={'schema':1,'source_commit':m['source_commit'],'runtime':m['runtime'],'actual_exit':gui.returncode if gui else None,'external_timeout':timed_out,'setup_error':error,'closure':closure,'whole_wrapper_wall_seconds':time.monotonic()-started,'load_phase_begin_observed':load_phase,'load_exited_before_gui_end':load_early_exit,'sampler_wall_seconds':sampler_wall,'sampler_cpu_seconds':sampler_cpu,'preparation_manifest_sha256':sha((report/'manifest.json').read_bytes()),'sample_count':len(samples),'sample_overflow':sample_overflow,'sample_interval_seconds':.2,'sample_limit':4501,'per_process_thread_limit':64,'sampler_scope':'proc reads only; excludes final JSON serialization and changed scheduling; not matched instrumentation overhead','qualification':False,'matched':False,'strict_assertions_modified':False,'source_generated_is_transmitted':False}
    result['process_closure_complete']=not any(k.endswith('_error') for k in closure) and all(not v.get('remaining_group_members') and (v.get('leader_reaped') or v.get('reaped')) for v in closure.values() if isinstance(v,dict))
    result['preparation_events_before_gui']=[line for line in load_log_before_gui.splitlines() if line.startswith('preparation_timing ')]
    result['preparation_events_through_gui_end']=[line for line in load_log_at_gui_end.splitlines() if line.startswith('preparation_timing ')]
    result['preparation_serial_phase_ended_during_gui']='phase=arithmetic.serial event=end' in load_log_at_gui_end
    result['preparation_partial_diagnostic_not_test_pass']=bool(load_p)
    result['load_condition_complete']=not load_p or (load_phase and not load_early_exit and not result['preparation_serial_phase_ended_during_gui'])
    write(out/'proc-samples.json',samples)
    result['binding_unchanged']=support_hashes()==m['support'] and all(Path(p).is_file() and sha(Path(p).read_bytes())==h for p,h in bound.items())
    if capture and (out/'trace.jsonl').exists():
        try:
            seal=runpy.run_path(str(root/'tools/gui-stop-differential.py'))['trace_seal'](out/'trace.jsonl',(out/'stderr.log').read_text(errors='replace'));result['strict_trace_seal']=seal;result['capture']=capture_readback(out)
        except Exception as e:result['trace_error']=type(e).__name__+': '+str(e)
    elif capture:result['trace_error']='capture target not reached or trace missing'
    result['files']={str(p.relative_to(out)):sha(p.read_bytes()) for p in out.iterdir() if p.is_file()}
    write(out/'run.json',result)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['prepare','restore','run']);p.add_argument('--root',type=Path,required=True);p.add_argument('--report',type=Path,required=True);p.add_argument('--binary',type=Path);p.add_argument('--load-binary',type=Path);p.add_argument('--condition',choices=['none','preparation'],default='preparation');p.add_argument('--capture',choices=['on','off'],default='on');p.add_argument('--timeout',type=int,default=600);a=p.parse_args();root=a.root.resolve();report=a.report.resolve()
    if a.action=='prepare':prepare(root,report)
    elif a.action=='restore':restore(root,report)
    else:
        if not a.binary:p.error('--binary required for run')
        had_run=(report/'run').exists()
        try:result=run(root,report,a.binary,a.load_binary,a.condition,a.capture=='on',a.timeout)
        except Exception as e:
            failure={'action':'run','error':type(e).__name__+': '+str(e),'application_launch_observed':(report/'run/process.json').exists(),'actual_exit':None,'qualification':False,'matched':False}
            if not had_run and (report/'run').is_dir():write(report/'run/operation-error.json',failure)
            print(json.dumps(failure),file=sys.stderr);return 1
        return 1 if result['setup_error'] or result['external_timeout'] or not result['binding_unchanged'] or result.get('trace_error') or result['actual_exit'] or not result['load_condition_complete'] or not result['process_closure_complete'] or any(k.endswith('_error') for k in result['closure']) else 0
    return 0
if __name__=='__main__':sys.exit(main())
