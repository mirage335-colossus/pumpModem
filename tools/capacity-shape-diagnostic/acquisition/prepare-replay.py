#!/usr/bin/env python3
"""Prepare the fixed-bank replay as a reversible private postimage."""
import argparse, difflib, hashlib, json, shutil, subprocess
from pathlib import Path

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[2]
PRODUCER = OUT / 'producer-output'
# main() resolves the explicit fixture directory before reading either input.
CAPTURE = PCM = SIDECAR = None
REVISION = '34fcedb04d00363943f9a4a7439bbbf180e3e1cd'
PCM_SHA = '3f1ecd6bb83ebfd9b1314d25b7b1fcc77f8bf282503171bb2e0e59c921e6ddd1'
SIDECAR_SHA = 'afe4936afe0241d23823384e3f0c4b3f7c6dcfe21587401996362e83cb126a9d'
ORIGIN = 1792421016.0003071
PREFIX = 154038
TOTAL = 154676

def sha(data): return hashlib.sha256(data).hexdigest()
def once(text, before, after, name):
    count = text.count(before)
    if count != 1: raise ValueError(f'{name}: expected one anchor, got {count}')
    return text.replace(before, after, 1)
def unified(before, after, name):
    return ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
        fromfile='producer/'+name, tofile='fixed-bank/'+name))

def checked_partitions():
    raw=SIDECAR.read_bytes()
    if sha(raw)!=SIDECAR_SHA: raise ValueError('sidecar hash mismatch')
    d=json.loads(raw)
    if d.get('schema')!=1 or d.get('float_format')!='IEEE754 binary32 little endian' or len(d.get('partitions',[]))!=234:
        raise ValueError('partition sidecar schema/extent changed')
    rows=d['partitions']; cursor=0
    for i,p in enumerate(rows):
        if set(p)!={'first','count','file_sample_offset','captured_count','case_id','case_sequence','noise','partial'}:
            raise ValueError(f'unexpected partition fields at {i}')
        expected=(0,0) if cursor<76 else (1,1) if cursor<7091 else (2,3) if cursor<14976 else (3,5)
        if p['first']!=cursor or p['file_sample_offset']!=cursor or p['count']<=0 or p['captured_count']!=p['count'] or p['partial'] or (p['case_id'],p['case_sequence'])!=expected:
            raise ValueError(f'partition is incomplete, noncontiguous, or from a different case at {i}')
        cursor+=p['count']
    if cursor!=TOTAL or rows[233]['first']!=PREFIX or rows[232]['first']+rows[232]['count']!=PREFIX:
        raise ValueError('154038 is not the exact 233-partition prefix')
    pcm=PCM.read_bytes()
    if sha(pcm)!=PCM_SHA or len(pcm)!=TOTAL*4: raise ValueError('PCM hash/extent mismatch')
    return rows, raw, pcm

HEADER_EDIT = r'''#include <functional>
'''
HEADER_AFTER = r'''#include <functional>
#include <filesystem>
'''
DECL_OLD='class Session {\n'
DECL_NEW='void diagnostic_fixed_bank_pcm_replay(const Settings&,const std::filesystem::path&);\nclass Session {\n'
FRIEND_OLD='    friend std::size_t receiver_control_margin(const modem::Config&,bool);\n'
FRIEND_NEW=FRIEND_OLD+'    friend void diagnostic_fixed_bank_pcm_replay(const Settings&,const std::filesystem::path&);\n'

HELPER = r'''void diagnostic_fixed_bank_pcm_replay(const Settings& requested,const std::filesystem::path& pcm_path) {
    constexpr std::uint64_t target=154038,total=154676;
    constexpr double origin=1792421016.0003071;
    if(std::endian::native!=std::endian::little)throw Error("fixed-bank replay requires little-endian IEEE754 host");
    std::int64_t replay_milliseconds=0;
    auto impl=std::make_unique<Session::Impl>(Session::EpochClock{},[&]{return Clock::time_point{}+std::chrono::milliseconds(replay_milliseconds);},false);
    impl->configure(requested);
    const auto value=impl->settings;const auto version=impl->generation;
    if(!value.simulation||value.transfer.modem.sample_rate!=512||value.transfer.modem.carrier_hz!=128||
       value.transfer.modem.bandwidth_hz!=128||value.transfer.modem.spreading_factor!=16||
       !value.transfer.modem.scramble||value.transfer.modem.dsss||!value.transfer.key||
       value.transfer.search_seconds!=1||value.dsp_workspace_bytes!=512ULL*1024*1024||
       value.transfer.dsp_workspace_bytes!=512ULL*1024*1024)
        throw Error("fixed-bank replay requires exact A3 settings and 512 MiB DSP limits");
    std::ifstream input(pcm_path,std::ios::binary);if(!input)throw Error("cannot open fixed-bank PCM");
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
    if(bytes.size()!=total*sizeof(float))throw Error("fixed-bank PCM extent changed");
    std::vector<float> pcm(total);std::memcpy(pcm.data(),bytes.data(),bytes.size());
    for(const auto x:pcm)if(!std::isfinite(x))throw Error("fixed-bank PCM has a nonfinite sample");
    std::ifstream sidecar_input(pcm_path.string()+".partitions.json",std::ios::binary);
    if(!sidecar_input)throw Error("cannot open fixed-bank partition sidecar");
    const std::string sidecar((std::istreambuf_iterator<char>(sidecar_input)),{});
    const auto start=sidecar.find("\"partitions\":[");if(start==std::string::npos)throw Error("missing partitions array");
    struct Part {std::uint64_t first=0,count=0,offset=0,captured=0,case_id=0,sequence=0;bool noise=false,partial=false;};
    std::vector<Part> parts;std::size_t pos=start+14;
    const auto number=[](std::string_view row,std::size_t& at,const char* key) {
        const std::string prefix=std::string("\"")+key+"\":";
        if(row.substr(at,prefix.size())!=prefix)throw Error("partition key order/format changed");at+=prefix.size();
        std::uint64_t value=0;const auto result=std::from_chars(row.data()+at,row.data()+row.size(),value);
        if(result.ec!=std::errc{}||result.ptr==row.data()+at)throw Error("invalid partition integer");
        at=static_cast<std::size_t>(result.ptr-row.data());return value;
    };
    for(;;) {
        while(pos<sidecar.size()&&(sidecar[pos]==','||sidecar[pos]==' '||sidecar[pos]=='\n'))++pos;
        if(pos>=sidecar.size())throw Error("unterminated partitions array");if(sidecar[pos]==']')break;
        if(sidecar[pos]!='{')throw Error("invalid partition object");const auto end=sidecar.find('}',pos);
        if(end==std::string::npos)throw Error("unterminated partition object");
        const auto row=std::string_view(sidecar).substr(pos+1,end-pos-1);std::size_t at=0;Part p;
        p.first=number(row,at,"first");if(row[at++]!=',')throw Error("partition separator missing");
        p.count=number(row,at,"count");if(row[at++]!=',')throw Error("partition separator missing");
        p.offset=number(row,at,"file_sample_offset");if(row[at++]!=',')throw Error("partition separator missing");
        p.captured=number(row,at,"captured_count");if(row[at++]!=',')throw Error("partition separator missing");
        p.case_id=number(row,at,"case_id");if(row[at++]!=',')throw Error("partition separator missing");
        p.sequence=number(row,at,"case_sequence");if(row[at++]!=',')throw Error("partition separator missing");
        if(row.substr(at,8)!="\"noise\":")throw Error("partition noise field differs");at+=8;
        if(row.substr(at,4)=="true"){p.noise=true;at+=4;}else if(row.substr(at,5)=="false")at+=5;else throw Error("bad noise value");
        if(row[at++]!=',')throw Error("partition separator missing");
        if(row.substr(at,10)!="\"partial\":")throw Error("partition partial field differs");at+=10;
        if(row.substr(at,4)=="true"){p.partial=true;at+=4;}else if(row.substr(at,5)=="false")at+=5;else throw Error("bad partial value");
        if(at!=row.size())throw Error("unexpected partition fields");parts.push_back(p);pos=end+1;
    }
    if(parts.size()!=234)throw Error("partition count changed");std::uint64_t next=0;
    for(const auto& p:parts) {
        const std::pair<std::uint64_t,std::uint64_t> expected=next<76?std::pair<std::uint64_t,std::uint64_t>{0,0}:
          next<7091?std::pair<std::uint64_t,std::uint64_t>{1,1}:next<14976?std::pair<std::uint64_t,std::uint64_t>{2,3}:std::pair<std::uint64_t,std::uint64_t>{3,5};
        if(p.first!=next||p.offset!=next||!p.count||p.captured!=p.count||p.partial||std::pair{p.case_id,p.sequence}!=expected||p.count>total-next)
            throw Error("partition continuity or case provenance changed");next+=p.count;
    }
    if(next!=total||parts[233].first!=target||parts[232].first+parts[232].count!=target)
        throw Error("fixed replay boundary is not 154038 after 233 full partitions");
    Session::Impl::Bank initial;initial.observed_epoch=origin;
    auto bank=impl->make_bank(value,std::move(initial));
    if(bank.samples||bank.revision!=impl->receive_revision||!bank.observed_epoch)throw Error("initial bank coordinates changed");
    std::unique_ptr<Session::Impl::Prepared> wave;std::uint64_t wave_case=0;
    const auto create_wave=[&](std::uint64_t id) {
        auto w=std::make_unique<Session::Impl::Prepared>();w->modem=value.transfer.modem;w->binary=id==1;w->generation=version;
        auto options=value.transfer;options.timestamp=id==1?1792421016ULL:id==2?1792421029ULL:1792421045ULL;
        if(id==1)w->transmitter=transfer::binary_transmitter(Bytes{0,0,1},options);
        else {Message message;message.data=id==2?Bytes{'h','i'}:Bytes(17,'x');w->transmitter=transfer::message_transmitter(message,options);}
        const auto reserved=replay_workspace(value);if(reserved<=replay_result_workspace)throw Error("original replay storage unavailable");
        w->point_limit=std::max<std::size_t>(32,detail::SignalWindow::constellation_capacity(value.transfer.modem));
        w->replay_count=std::min(replay_frames,(reserved-replay_result_workspace)/
            (replay_frame_base+w->point_limit*sizeof(std::complex<float>)));
        if(w->replay_count<2)throw Error("original replay slot geometry invalid");
        w->replay.reserve(w->replay_count);w->signals.resize(w->replay_count);w->serial=id;wave_case=id;wave=std::move(w);
    };
    const auto add_signal=[](audio::trace::Fields& f,const SignalUpdate* s) {
        f.add("accepted_signal",s!=nullptr);if(!s)return;
        f.add("signal_id",s->id);f.add("signal_revision",s->revision);f.add("received_bits",s->received_bits);
        f.add("missing_symbols",s->missing_symbols);f.add("physical_complete",s->complete);f.add("validated",s->validated);
        f.add("binary",s->binary);f.add("text_prefix",s->text.substr(0,64));f.add("raw_bits_prefix",s->raw_bits.substr(0,64));
    };
    std::stop_source replay_stop;
    for(std::size_t i=0;i<233;++i) {
        const auto& p=parts[i];if(p.first!=bank.samples)throw Error("bank/input sample positions diverged");
        if(p.case_id==0)wave.reset();else if(wave_case!=p.case_id){replay_milliseconds=(p.case_id-1)*3000;create_wave(p.case_id);}
        auto* staged=p.case_id?wave.get():nullptr;impl->account(p.count,version);
        impl->feed_samples(bank,std::span<const float>(pcm).subspan(p.first,p.count),value,version,replay_stop.get_token(),staged);
        if(bank.samples!=p.first+p.count)throw Error("feed_samples did not commit full partition");
        const auto& health=impl->current.receiver_health;
        if(health.failed()||health.dropped_samples||!impl->current.error.empty()||!impl->current.receiver_error.empty())
            throw Error("fixed-bank receiver health/error at committed sample "+std::to_string(bank.samples)+
                ": "+impl->current.error+"; "+impl->current.receiver_error);

        const SignalUpdate* event=nullptr;
        if(staged){if(wave->verified)event=&*wave->verified;else for(auto it=wave->signals.rbegin();it!=wave->signals.rend();++it)if(*it){event=&**it;break;}}
        audio::trace::record("capacity_shape_fixed_bank_partition",[&](auto& f) {
            f.add("schema",1);f.add("partition_index",i);f.add("case_id",p.case_id);f.add("case_sequence",p.sequence);
            f.add("noise",p.noise);f.add("first",p.first);f.add("count",p.count);f.add("bank_sample",bank.samples);
            f.add("bank_epoch",origin+static_cast<double>(bank.samples)/value.transfer.modem.sample_rate);f.add("revision",bank.revision);
            f.add("resident",bank.receivers.size());f.add("admissions",bank.admissions);f.add("retirements",bank.retirements);
            f.add("builds",bank.builds);f.add("working_bytes",bank.working_bytes);f.add("coverage_limited",bank.coverage_limited);
            f.add("receiver_limit",bank.receiver_limit);if(staged){f.add("prepared_case",wave_case);f.add("replay_slots",wave->replay_count);f.add("replay_slots_initialized",wave->signals.size());}
            add_signal(f,event);
        });
    }
    if(bank.samples!=target)throw Error("replay did not stop at committed prefix");
    // Mirrors the next captured feed's admission fence, but returns at the
    // existing joined-refresh/before-admission work point without feeding tail.
    impl->refresh_bank(bank,value,true);
    const SignalUpdate* event=nullptr;
    if(wave){if(wave->verified)event=&*wave->verified;else for(auto it=wave->signals.rbegin();it!=wave->signals.rend();++it)if(*it){event=&**it;break;}}
    audio::trace::record("capacity_shape_fixed_bank_end",[&](auto& f) {
        f.add("schema",1);f.add("input_boundary",target);f.add("committed_partitions",233);f.add("unfed_suffix_samples",total-target);
        f.add("eof_injected",false);f.add("absence_injected",false);f.add("bank_sample",bank.samples);
        f.add("bank_epoch",origin+static_cast<double>(bank.samples)/value.transfer.modem.sample_rate);f.add("revision",bank.revision);
        f.add("resident",bank.receivers.size());f.add("peak_resident",bank.peak_receivers);f.add("admissions",bank.admissions);
        f.add("retirements",bank.retirements);f.add("builds",bank.builds);f.add("working_bytes",bank.working_bytes);
        f.add("limited",bank.limited);f.add("coverage_limited",bank.coverage_limited);f.add("receiver_limit",bank.receiver_limit);
        const auto& health=impl->current.receiver_health;
        f.add("health_input_overruns",health.input_overruns);f.add("health_dropped_samples",health.dropped_samples);
        f.add("health_input_interrupted",health.input_interrupted);f.add("health_search_limited",health.search_limited);
        f.add("health_receiver_reset",health.receiver_reset);f.add("session_error",impl->current.error);
        f.add("session_receiver_error",impl->current.receiver_error);

        f.add("session_samples_received",impl->current.samples_received);f.add("prepared_case",wave_case);
        f.add("replay_slots",wave?wave->replay_count:0);f.add("replay_slots_initialized",wave?wave->signals.size():0);add_signal(f,event);
    });
}

'''

def main():
    global ROOT, PRODUCER, CAPTURE, PCM, SIDECAR, OUT
    parser=argparse.ArgumentParser()
    parser.add_argument('--source-root',type=Path,default=ROOT)
    parser.add_argument('--first-layer',type=Path,default=PRODUCER)
    parser.add_argument('--output-dir',type=Path,default=OUT/'prepared')
    parser.add_argument('--fixture-dir',type=Path,default=Path(__file__).resolve().parent/'fixture')
    args=parser.parse_args()
    ROOT=args.source_root.resolve(strict=True);PRODUCER=args.first_layer.resolve(strict=True)
    OUT=args.output_dir.resolve()
    CAPTURE=args.fixture_dir.resolve(strict=True)
    PCM=CAPTURE/'a3-source-input.f32';SIDECAR=CAPTURE/'a3-source-input.f32.partitions.json'
    if OUT.exists(): raise ValueError('output-dir must be fresh')
    OUT.mkdir(parents=True)
    subprocess.run(['git','-c','safe.directory='+str(ROOT),'-C',str(ROOT),'merge-base','--is-ancestor',REVISION,'HEAD'],check=True)
    first=json.loads((PRODUCER/'manifest.json').read_text())
    for rel,digest in first['source_closure'].items():
        if sha((ROOT/rel).read_bytes())!=digest: raise ValueError(f'first-layer source hash mismatch: {rel}')
    if sha((ROOT/'include/datapump/live.hpp').read_bytes())!='820ad7fc4346edaed29cb21e974d539b13f17777bc34f87a4c43c94b86a360bd': raise ValueError('public Session header preimage changed')
    rows,sidecar,pcm=checked_partitions()
    post=OUT/'prepared-source'
    shutil.copytree(PRODUCER/'prepared-source',post)
    (post/'include/datapump').mkdir(parents=True,exist_ok=True)
    shutil.copy2(ROOT/'include/datapump/live.hpp',post/'include/datapump/live.hpp')
    files=['include/datapump/live.hpp','src/live.cpp','tests/test_live.cpp']
    before={p:(post/p).read_text() for p in files}
    h=before[files[0]]
    h=once(h,HEADER_EDIT,HEADER_AFTER,'filesystem include')
    h=once(h,DECL_OLD,DECL_NEW,'diagnostic declaration')
    h=once(h,FRIEND_OLD,FRIEND_NEW,'diagnostic friend')
    l=before[files[1]]
    l=once(l,'#include <charconv>\n','#include <charconv>\n#include <bit>\n#include <cstring>\n#include <fstream>\n#include <iterator>\n','live includes')
    l=once(l,'    execution::Task source, capture_worker, encoder, decoder, recovery_worker;\n','    execution::Task source, capture_worker, encoder, decoder, recovery_worker;\n    bool tasks_started=false;\n','task guard')
    old='''    explicit Impl(EpochClock clock, ReplayClock presentation_clock)
        : custom_epoch_clock(bool(clock)),
          epoch_clock(clock ? std::move(clock) : EpochClock(epoch_now)),
          replay_clock(presentation_clock ? std::move(presentation_clock) : ReplayClock(Clock::now)) {
'''
    new='''    explicit Impl(EpochClock clock, ReplayClock presentation_clock,bool start_tasks=true)
        : custom_epoch_clock(bool(clock)),
          epoch_clock(clock ? std::move(clock) : EpochClock(epoch_now)),
          replay_clock(presentation_clock ? std::move(presentation_clock) : ReplayClock(Clock::now)) {
'''
    l=once(l,old,new,'optional task constructor')
    old='''        source = execution::Task([this](std::stop_token stop) { source_loop(stop); });
        capture_worker = execution::Task([this](std::stop_token stop) { capture_loop(stop); });
        encoder = execution::Task([this](std::stop_token stop) { encode_loop(stop); });
        decoder = execution::Task([this](std::stop_token stop) { decode_loop(stop); });
        recovery_worker = execution::Task([this](std::stop_token stop) { recovery_loop(stop); });
    }
    ~Impl() {
        halt(); source.request_stop(); capture_worker.request_stop(); encoder.request_stop(); decoder.request_stop(); recovery_worker.request_stop(); changed.notify_all();
        source.join(); capture_worker.join(); encoder.join(); decoder.join(); recovery_worker.join();
'''
    new='''        if(start_tasks) {
            source = execution::Task([this](std::stop_token stop) { source_loop(stop); });
            capture_worker = execution::Task([this](std::stop_token stop) { capture_loop(stop); });
            encoder = execution::Task([this](std::stop_token stop) { encode_loop(stop); });
            decoder = execution::Task([this](std::stop_token stop) { decode_loop(stop); });
            recovery_worker = execution::Task([this](std::stop_token stop) { recovery_loop(stop); });
            tasks_started=true;
        }
    }
    ~Impl() {
        if(tasks_started) {
            halt(); source.request_stop(); capture_worker.request_stop(); encoder.request_stop(); decoder.request_stop(); recovery_worker.request_stop(); changed.notify_all();
            source.join(); capture_worker.join(); encoder.join(); decoder.join(); recovery_worker.join();
        }
'''
    l=once(l,old,new,'guarded destructor')
    l=once(l,'    void refresh_bank(Bank& bank, const Settings& value) {\n','    void refresh_bank(Bank& bank, const Settings& value,bool diagnostic_checkpoint_only=false) {\n','refresh argument')
    l=once(l,'        ::datapump::diagnostic::capacity_cpu::work_point("joined_refresh_before_new_admissions");\n        if(new_epoch && !bank.burst_admission_frozen) {\n','        ::datapump::diagnostic::capacity_cpu::work_point("joined_refresh_before_new_admissions");\n        if(diagnostic_checkpoint_only)return;\n        if(new_epoch && !bank.burst_admission_frozen) {\n','checkpoint gate')
    l=once(l,'Session::Session(EpochClock epoch_clock, ReplayClock replay_clock)\n',HELPER+'Session::Session(EpochClock epoch_clock, ReplayClock replay_clock)\n','diagnostic helper')
    t=before[files[2]]
    old='        if(argc>1 && std::string_view(argv[1])=="--simulation-media-clock-trace") {\n'
    new='''        if(argc>1 && std::string_view(argv[1])=="--capacity-fixed-bank-replay") {
            check(argc==4,"usage: test_live --capacity-fixed-bank-replay <pcm-path> <trace-path>");
            const auto read_bytes=[](const std::filesystem::path& path) {
                std::ifstream in(path,std::ios::binary);check(bool(in),"cannot open fixed-bank input for hash check");
                return std::vector<char>((std::istreambuf_iterator<char>(in)),{});
            };
            const auto pcm_bytes=read_bytes(argv[2]);
            const auto sidecar_bytes=read_bytes(std::string(argv[2])+".partitions.json");
            check(component_sha256(pcm_bytes.data(),pcm_bytes.size())=="3f1ecd6bb83ebfd9b1314d25b7b1fcc77f8bf282503171bb2e0e59c921e6ddd1","fixed-bank PCM hash mismatch");
            check(component_sha256(sidecar_bytes.data(),sidecar_bytes.size())=="afe4936afe0241d23823384e3f0c4b3f7c6dcfe21587401996362e83cb126a9d","fixed-bank sidecar hash mismatch");
            auto value=media_clock_settings();value.dsp_workspace_bytes=512ULL*1024*1024;value.transfer.dsp_workspace_bytes=512ULL*1024*1024;
            audio::trace::Scope trace(argv[3]);live::diagnostic_fixed_bank_pcm_replay(value,argv[2]);
            std::cout<<"fixed-bank committed-prefix replay passed\\n";return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--simulation-media-clock-trace") {
'''
    t=once(t,old,new,'test fixture CLI branch')
    final={files[0]:h,files[1]:l,files[2]:t}
    reverse=OUT/'reverse';reverse.mkdir()
    for rel in files:
        (post/rel).parent.mkdir(parents=True,exist_ok=True);(post/rel).write_text(final[rel])
        suffix=Path(rel).name
        rev=reverse/(suffix+'.pre-fixed-bank');rev.write_text(before[rel])
        (OUT/(suffix+'.fixed-bank.patch')).write_text(unified(before[rel],final[rel],rel))
        if rev.read_text()!=before[rel] or (post/rel).read_text()!=final[rel]: raise ValueError(f'reversible readback mismatch: {rel}')
    source_hashes={rel:{'before_sha256':sha(before[rel].encode()),'after_sha256':sha(final[rel].encode()),
                        'reverse_sha256':sha((reverse/(Path(rel).name+'.pre-fixed-bank')).read_bytes())} for rel in files}
    descriptor={
      'schema':1,'status':'private diagnostic input descriptor; not qualification','source_revision':REVISION,
      'producer_source_closure':first['source_closure'],'producer_manifest_base_commit_label':first['base_commit'],
      'producer_manifest_sha256':sha((PRODUCER/'manifest.json').read_bytes()),
      'pcm_path':str(PCM),'pcm_sha256':sha(pcm),'pcm_format':'IEEE754 binary32 little endian','total_samples':TOTAL,
      'partition_path':str(SIDECAR),'partition_sha256':sha(sidecar),'full_partition_count':len(rows),
      'target':{'prefix_end_exclusive':PREFIX,'committed_partition_count':233,'unfed_suffix_first':PREFIX,'unfed_suffix_samples':TOTAL-PREFIX,
        'oracle_boundary':'capacity_shape_point sequence 1008 sample 154038; capacity_cpu_work sequence 1009 joined_refresh_before_new_admissions',
        'unfed_suffix_is_not_asserted_committed':True},
      'bank':{'origin_epoch':ORIGIN,'revision':1,'sample_origin':0,'epoch_policy':'default EpochClock, captured observed_epoch; sample-driven media epoch',
        'settings_factory':'test_live.cpp::media_clock_settings() first-layer postimage','geometry':{'sample_rate':512,'carrier_hz':128,'bandwidth_hz':128,
        'spreading_factor':16,'search_seconds':1,'scramble':True,'dsss':False,'snr_db':30,'clock_error_ppm':0,
        'phase_noise_degrees_per_sqrt_second':0,'settings_dsp_workspace_bytes':536870912,'transfer_dsp_workspace_bytes':536870912},
        'key_provenance':'media_clock_settings(): 32 bytes 0x61; configure() applies original normalized settings'},
      'cases':{'1':{'label':'raw','tx_epoch':1792421016.1487446,'floored_timestamp':1792421016,'source':'raw bits 001'},
        '2':{'label':'short','tx_epoch':1792421029.8499165,'floored_timestamp':1792421029,'source':'text hi'},
        '3':{'label':'framed','tx_epoch':1792421045.2503071,'floored_timestamp':1792421045,'source':'text 17 x bytes'}},
      'staging':'same case source transmitter and original replay_count/point_limit/signals vector geometry; no replay frames or TX progress generated',
      'completion':'no EOF, trailing PCM, or absence is injected; completion can only result from scored captured symbols',
      'scope':'exact ReceiverBank prefix replay through original make_bank/feed_samples; not full Live, GUI, or sensitivity qualification'}
    descriptor_path=OUT/'fixture.json';descriptor_path.write_text(json.dumps(descriptor,indent=2,sort_keys=True)+'\n')
    manifest={'schema':1,'status':'private reversible overlay prepared','assignment':'fixed-bank-pcm-replay-20261019',
      'source_revision':REVISION,'producer_output':str(PRODUCER),'producer_manifest_sha256':sha((PRODUCER/'manifest.json').read_bytes()),
      'postimage':str(post),'reverse_preimages':str(reverse),'fixture_descriptor':str(descriptor_path),'fixture_sha256':sha(descriptor_path.read_bytes()),
      'patches':[str(OUT/(Path(p).name+'.fixed-bank.patch')) for p in files],
      'source_closure':first['source_closure'],'source_hashes':source_hashes,
      'input_sha256':{'pcm':sha(pcm),'partitions':sha(sidecar)},'reversible':True}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n')
    print(json.dumps({'status':manifest['status'],'revision':REVISION,'prefix':PREFIX,'partitions':233,
      'postimage':str(post),'fixture_sha256':manifest['fixture_sha256']},indent=2))

if __name__=='__main__': main()
