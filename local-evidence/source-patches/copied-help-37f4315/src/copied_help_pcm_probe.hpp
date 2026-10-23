#pragma once
// Private diagnostic only. No decoder decisions, cursor movement or runtime limits change.
#include "datapump/pattern_receiver.hpp"
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <openssl/evp.h>
namespace datapump::diagnostic::copied_help_pcm {
constexpr std::size_t sample_cap=1048576, descriptor_cap=8192, metadata_cap=8*1024*1024;
struct Descriptor {
    std::uint64_t ordinal=0,session=0,bank=0,generation=0,revision=0,first=0,count=0,offset=0,end=0,final_call=0;
    std::uint8_t status=0; // 0 entered, 1 committed, 2 scope left without commit
};
constexpr std::size_t record_cap=metadata_cap-sizeof(Descriptor)*descriptor_cap-65536;
struct Bank {
    std::uint64_t session=0,identity=0,generation=0,revision=0,next=0;
    bool prefix=true,config_complete=true,context_seen=false; std::size_t admissions=0;
};
struct State {
    std::mutex mutex;
    std::array<float,sample_cap> pcm{};
    std::array<Descriptor,descriptor_cap> descriptors{};
    std::array<char,record_cap> metadata{};
    std::array<Bank,16> banks{};
    std::array<std::uint64_t,16> sessions{},closed{};
    std::size_t samples=0,attempts=0,bytes=0,bank_count=0,session_count=0,records=0;
    std::uint64_t calls=0;
    bool sample_overflow=false,descriptor_overflow=false,metadata_overflow=false,identity_overflow=false;
    bool config_incomplete=false,input_incomplete=false,flushed=false;
    std::string directory;
};
static_assert(sizeof(State)-sizeof(std::array<float,sample_cap>)<=metadata_cap);
inline std::atomic<State*> active{nullptr};
inline thread_local std::uint64_t source_session=0,source_version=0;
inline void source_generation(const void* session,std::uint64_t version) noexcept {source_session=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(session));source_version=version;}
inline std::uint64_t source_generation(const void* session) noexcept {return source_session==static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(session))?source_version:0;}
inline std::mutex arm_mutex;
inline std::unique_ptr<State> storage;
inline std::uint64_t identity(const void* value) noexcept {return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value));}
struct Record {
    State& s;std::size_t start;bool good=true;
    explicit Record(State& value,std::string_view event):s(value),start(s.bytes){put("{\"event\":");quote(event);}
    void put(std::string_view v) noexcept {
        if(!good)return;
        if(v.size()>record_cap-s.bytes){good=false;s.metadata_overflow=true;return;}
        std::memcpy(s.metadata.data()+s.bytes,v.data(),v.size());s.bytes+=v.size();
    }
    void quote(std::string_view v) noexcept {
        put("\"");for(unsigned char c:v){if(c=='"'||c=='\\'){char b[2]={'\\',static_cast<char>(c)};put({b,2});}
        else if(c<32){char b[7];std::snprintf(b,sizeof b,"\\u%04x",static_cast<unsigned>(c));put({b,6});}else{char b=static_cast<char>(c);put({&b,1});}}put("\"");
    }
    template<class T> void value(T v) noexcept requires(std::is_integral_v<T>||std::is_enum_v<T>) {
        if constexpr(std::is_same_v<T,bool>)put(v?"true":"false");
        else if constexpr(std::is_enum_v<T>)value(static_cast<std::underlying_type_t<T>>(v));
        else {char b[32];const auto r=std::to_chars(b,b+sizeof b,v);if(r.ec==std::errc{})put({b,static_cast<std::size_t>(r.ptr-b)});else{good=false;s.metadata_overflow=true;}}
    }
    template<class T> void value(T v) noexcept requires(std::is_floating_point_v<T>) {
        if(!std::isfinite(v)){put("null");s.config_incomplete=true;return;}
        char b[128];const auto r=std::to_chars(b,b+sizeof b,v,std::chars_format::general,std::numeric_limits<T>::max_digits10);
        if(r.ec==std::errc{})put({b,static_cast<std::size_t>(r.ptr-b)});else{good=false;s.metadata_overflow=true;}
    }
    template<class T> void value(const std::optional<T>& v) noexcept {if(v)value(*v);else put("null");}
    void value(const modem::PatternStartWindow& v) noexcept {put("[");value(v.epoch_origin_samples);put(",");value(v.phase_scale);put(",");value(v.half_width_samples);put("]");}
    template<class T> void field(std::string_view k,const T& v) noexcept {put(",");quote(k);put(":");value(v);}
    void text(std::string_view k,std::string_view v) noexcept {put(",");quote(k);put(":");quote(v);}
    template<class Range> void hex(std::string_view k,const Range& range) noexcept {put(",");quote(k);put(":\"");constexpr char d[]="0123456789abcdef";for(auto x:range){char b[2]={d[(x>>4)&15],d[x&15]};put({b,2});}put("\"");}
    template<class Range> void array(std::string_view k,const Range& range) noexcept {put(",");quote(k);put(":[");bool first=true;for(const auto& v:range){if(!first)put(",");first=false;value(v);}put("]");}
    ~Record(){put("}\n");if(!good)s.bytes=start;else ++s.records;}
};
inline Bank* bank(State& s,std::uint64_t owner,std::uint64_t id,std::uint64_t gen,std::uint64_t rev,std::uint64_t first) noexcept {
    for(std::size_t i=0;i<s.bank_count;++i){auto& b=s.banks[i];if(b.session==owner&&b.identity==id&&b.generation==gen&&b.revision==rev)return &b;}
    if(s.bank_count==s.banks.size()){s.identity_overflow=true;return nullptr;}
    auto& b=s.banks[s.bank_count++];b={owner,id,gen,rev,first,first==0,true,false,0};if(first)s.input_incomplete=true;
    bool seen=false;for(std::size_t i=0;i<s.session_count;++i)seen|=s.sessions[i]==owner;
    if(!seen){if(s.session_count==s.sessions.size())s.identity_overflow=true;else s.sessions[s.session_count++]=owner;}
    return &b;
}
struct Feed {
    State* state=nullptr;std::size_t index=0;
    Feed()=default;Feed(State* s,std::size_t i):state(s),index(i){}
    Feed(const Feed&)=delete;Feed& operator=(const Feed&)=delete;
    Feed(Feed&& x) noexcept:state(x.state),index(x.index){x.state=nullptr;}
    void commit(std::uint64_t end) noexcept {
        if(!state)return;std::lock_guard lock(state->mutex);auto& d=state->descriptors[index];
        d.end=end;d.status=1;d.final_call=++state->calls;if(d.first>UINT64_MAX-d.count||end!=d.first+d.count)state->input_incomplete=true;
        auto* b=bank(*state,d.session,d.bank,d.generation,d.revision,d.first);
        if(b){if(b->next!=d.first){b->prefix=false;state->input_incomplete=true;}b->next=end;}
        state=nullptr;
    }
    ~Feed(){if(state){std::lock_guard lock(state->mutex);auto& d=state->descriptors[index];d.status=2;d.final_call=++state->calls;state->input_incomplete=true;
        if(auto* b=bank(*state,d.session,d.bank,d.generation,d.revision,d.first))b->prefix=false;}}
};
inline void projection(const void* session,const void* owner,std::uint64_t gen,std::uint64_t rev,std::uint64_t first,std::size_t count,std::complex<double> mixer) noexcept {
    auto* s=active.load(std::memory_order_acquire);if(!s)return;std::lock_guard lock(s->mutex);
    Record r(*s,"projection_begin");r.field("call",++s->calls);r.field("session",identity(session));r.field("bank",identity(owner));r.field("generation",gen);r.field("revision",rev);r.field("first",first);r.field("count",count);r.field("mixer_real",mixer.real());r.field("mixer_imag",mixer.imag());
}
template<class Receivers>
inline Feed feed(const void* session,const void* owner,std::uint64_t gen,std::uint64_t rev,std::uint64_t first,
                 std::span<const float> samples,std::complex<double> mixer,const Receivers& receivers,
                 bool allow_admission,bool account_frontier,std::optional<std::uint64_t> coordinate_count) noexcept {
    auto* s=active.load(std::memory_order_acquire);if(!s)return {};
    std::lock_guard lock(s->mutex);++s->calls;
    if(s->attempts==descriptor_cap){s->descriptor_overflow=true;return {};}
    if(samples.size()>sample_cap-s->samples){s->sample_overflow=true;return {};}
    const auto index=s->attempts++;auto& d=s->descriptors[index];
    d={s->calls,identity(session),identity(owner),gen,rev,first,samples.size(),s->samples,first,0,0};
    if(!samples.empty())std::memcpy(s->pcm.data()+s->samples,samples.data(),samples.size_bytes());s->samples+=samples.size();
    auto* b=bank(*s,d.session,d.bank,gen,rev,first);
    Record r(*s,"feed_attempt");r.field("ordinal",d.ordinal);r.field("session",d.session);r.field("bank",d.bank);
    r.field("generation",gen);r.field("revision",rev);r.field("first",first);r.field("count",samples.size());r.field("pcm_offset",d.offset);
    r.field("mixer_real_after_projection",mixer.real());r.field("mixer_imag_after_projection",mixer.imag());
    r.field("allow_admission",allow_admission);r.field("account_frontier",account_frontier);r.field("coordinate_count",coordinate_count);
    r.put(",\"resident_receiver_ids_before_refresh\":[");bool comma=false;std::size_t resident=0;
    for(const auto& receiver:receivers){if(comma)r.put(",");comma=true;r.value(receiver.point_consumer);++resident;}r.put("]");
    if(b&&first==0&&resident>b->admissions){b->config_complete=false;s->config_incomplete=true;}
    return Feed{s,index};
}
inline void session_closed(const void* session) noexcept {auto* s=active.load();if(!s)return;std::lock_guard lock(s->mutex);for(std::size_t i=0;i<s->session_count;++i)if(s->sessions[i]==identity(session))s->closed[i]=1;}
inline void config_fields(Record& r,const modem::Config& c) noexcept {
    r.field("pattern_symbols",c.pattern_symbols);
    r.field("search_arithmetic",c.search_arithmetic);
    r.field("receiver_strategy",c.receiver_strategy);
    r.field("outer_dsss_version",c.outer_dsss_version);
    r.field("spreading_factor",c.spreading_factor);
    r.field("stream_epoch",c.stream_epoch);
    r.field("stream_phase_samples",c.stream_phase_samples);
    r.field("sample_rate",c.sample_rate);
    r.field("constellation_bits",c.constellation_bits);
    r.field("carrier_hz",c.carrier_hz);
    r.field("oscillator_search_present",c.oscillator_search.has_value());if(c.oscillator_search){const auto& o=*c.oscillator_search;
    r.field("oscillator_lf_accuracy_ppm",o.lf.accuracy_ppm);
    r.field("oscillator_lf_phase_noise_degrees_per_sqrt_second",o.lf.phase_noise_degrees_per_sqrt_second);
    r.field("oscillator_rf_accuracy_ppm",o.rf.accuracy_ppm);
    r.field("oscillator_rf_phase_noise_degrees_per_sqrt_second",o.rf.phase_noise_degrees_per_sqrt_second);
    r.field("oscillator_rf_shift_hz",o.rf_shift_hz);
    r.field("oscillator_margin",o.margin);
    r.field("oscillator_reference",o.reference);
    r.field("oscillator_sideband",o.sideband);
    }
    r.field("frequency_search_multiplier",c.frequency_search_multiplier);
    r.field("bandwidth_hz",c.bandwidth_hz);
    r.field("training_seconds",c.training_seconds);
    r.field("integration_seconds",c.integration_seconds);
    r.field("spreading_mode",c.spreading_mode);
    r.field("pulse_shaping",c.pulse_shaping);
    r.field("scramble",c.scramble);
    r.field("dsss",c.dsss);
    r.field("dsss_factor",c.dsss_factor);
    r.field("receiver_cpu_budget",c.receiver_cpu_budget);
    r.field("config_replay_supported",!c.fake_hopping&&!c.scheduled_hopping&&!c.symbol_burst.enabled());
    if(!c.data_key){r.hex("spreading_seed",c.spreading_seed);r.hex("dsss_seed",c.dsss_seed);}
    else r.text("seed_derivation","seeded_config from retained key joined by HMAC tag; no secret roots logged");
    r.field("data_key_present",c.data_key.has_value());
    r.field("fake_hopping_present",bool(c.fake_hopping));if(c.fake_hopping)r.s.config_incomplete=true;
    r.field("scheduled_hopping_present",bool(c.scheduled_hopping));if(c.scheduled_hopping)r.s.config_incomplete=true;
    r.field("burst_wait_symbols",c.symbol_burst.wait_symbols);r.field("burst_random_limit",c.symbol_burst.random_limit);if(c.symbol_burst.enabled())r.s.config_incomplete=true;
    r.field("memory_limit",c.memory_limit);
}
inline void search_fields(Record& r,const modem::PatternSearch& q) noexcept {
    r.field("admitted_frequency_half_width_hz",q.admitted_frequency_half_width_hz);
    r.field("admitted_clock_half_width_ppm",q.admitted_clock_half_width_ppm);
    r.field("complex_frequency_coverage",q.complex_frequency_coverage);
    r.field("complex_clock_coverage",q.complex_clock_coverage);
    r.field("track_limit",q.track_limit);r.field("bit_limit",q.bit_limit);
    r.field("receiver_bank_count",q.receiver_bank_count);
    r.field("amplitude_receiver_bank_count",q.amplitude_receiver_bank_count);
    r.field("amplitude_strategy_workspace_bytes",q.amplitude_strategy_workspace_bytes);
    r.field("receiver_schedule_key_count",q.receiver_schedule_key_count);
    r.field("receiver_strategy_workspace_bytes",q.receiver_strategy_workspace_bytes);
    r.field("amplitude_channel_limit",q.amplitude_channel_limit);
    r.field("input_sample_origin",q.input_sample_origin);
    r.field("scheduled_capture_present",bool(q.scheduled_capture));if(q.scheduled_capture)r.s.config_incomplete=true;
    r.field("drift_tolerant",q.drift_tolerant);
    r.field("differential_window_seconds",q.differential_window_seconds);
    r.array("frequency_offsets_hz",q.frequency_offsets_hz);
    r.put(",\"hypotheses\":[");bool comma=false;for(const auto& h:q.hypotheses){if(comma)r.put(",");comma=true;r.put("[");r.value(h.frequency_offset_hz);r.put(",");r.value(h.clock_error_ppm);r.put("]");}r.put("]");
    r.field("frequency_rate_competition",q.frequency_rate_competition);
    r.field("expand_clock_search",q.expand_clock_search);
    r.field("prefer_streamed_templates",q.prefer_streamed_templates);
    r.field("reuse_deferred_templates",q.reuse_deferred_templates);
    r.field("allow_local_clock_fallback",q.allow_local_clock_fallback);
    r.field("couple_clock_to_carrier",q.couple_clock_to_carrier);
    r.field("uncoupled_frequency_count",q.uncoupled_frequency_count);
    r.field("false_alarm_probability",q.false_alarm_probability);
    r.field("retain_score",q.retain_score);
    r.field("chunk_bits",q.chunk_bits);
    r.field("candidate_limit",q.candidate_limit);
    r.field("initial_stream_symbols",q.initial_stream_symbols);
    r.field("search_stream_phases",q.search_stream_phases);
    r.field("compact_clock_search",q.compact_clock_search);
    r.field("start_offset_seconds",q.start_offset_seconds);
    r.field("start_uncertainty_seconds",q.start_uncertainty_seconds);
    r.field("qualified_start_window",q.qualified_start_window);
    r.field("amplitude_start_window",q.amplitude_start_window);
    r.field("amplitude_start_offset_seconds",q.amplitude_start_offset_seconds);
    r.field("amplitude_start_uncertainty_seconds",q.amplitude_start_uncertainty_seconds);
    r.field("amplitude_epoch_stride_seconds",q.amplitude_epoch_stride_seconds);
    r.field("amplitude_lookahead_epochs",q.amplitude_lookahead_epochs);
    r.field("restricted_timing_search",q.restricted_timing_search);
    r.field("restricted_direct_search",q.restricted_direct_search);
    r.field("partitioned_timing_search",q.partitioned_timing_search);
    r.field("early_qualified_fft",q.early_qualified_fft);
    r.field("component_qualified_search",q.component_qualified_search);
    r.field("release_idle_fft_scratch",q.release_idle_fft_scratch);
    r.array("clock_errors_ppm",q.clock_errors_ppm);
    r.field("worker_threads",q.worker_threads);
    r.field("outer_presence_guard",q.outer_presence_guard);
}
template<class KeyTag>
inline void constructed(const void* session,const void* owner,std::uint64_t gen,std::uint64_t rev,std::uint64_t sample,
                     std::uint64_t receiver_id,std::uint64_t epoch,std::uint64_t family,std::uint64_t origin,
                     std::size_t grant,const KeyTag& tag,const modem::Config& c,const modem::PatternSearch& search) noexcept {
    auto* s=active.load(std::memory_order_acquire);if(!s)return;std::lock_guard lock(s->mutex);
    if(auto* b=bank(*s,identity(session),identity(owner),gen,rev,sample)){if(!gen||c.fake_hopping||c.scheduled_hopping||c.symbol_burst.enabled()||search.scheduled_capture)b->config_complete=false;}if(!gen)s->config_incomplete=true;
    Record r(*s,"receiver_constructed");r.field("call",++s->calls);r.field("session",identity(session));r.field("bank",identity(owner));
    r.field("generation",gen);r.field("revision",rev);r.field("sample",sample);r.field("receiver_id",receiver_id);
    r.field("epoch",epoch);r.field("family",family);r.field("input_origin",origin);r.field("actual_constructor_grant",grant);
    r.hex("key_tag_hmac_dp_rx_bank",tag);config_fields(r,c);search_fields(r,search);
}
inline void admitted(const void* session,const void* owner,std::uint64_t gen,std::uint64_t rev,std::uint64_t sample,std::uint64_t receiver_id) noexcept {
    auto* s=active.load(std::memory_order_acquire);if(!s)return;std::lock_guard lock(s->mutex);
    if(auto* b=bank(*s,identity(session),identity(owner),gen,rev,sample))++b->admissions;if(!gen)s->config_incomplete=true;
    Record r(*s,"receiver_admitted");r.field("call",++s->calls);r.field("session",identity(session));r.field("bank",identity(owner));
    r.field("generation",gen);r.field("revision",rev);r.field("sample",sample);r.field("receiver_id",receiver_id);
}
template<class Settings>
inline void context(const void* session,const void* owner,std::uint64_t gen,std::uint64_t rev,
                    std::uint64_t source_sample,std::uint64_t bank_sample,std::optional<double> origin,const Settings& v,std::uint64_t transmission_id) noexcept {
    auto* s=active.load(std::memory_order_acquire);if(!s)return;std::lock_guard lock(s->mutex);
    // Each read supplies its exact source/media relationship; storage remains bounded.
    if(auto* b=bank(*s,identity(session),identity(owner),gen,rev,bank_sample)){b->context_seen=origin.has_value();if(!origin){b->config_complete=false;s->config_incomplete=true;}}
    Record r(*s,"source_context");r.field("call",++s->calls);r.field("session",identity(session));r.field("bank",identity(owner));
    r.field("generation",gen);r.field("revision",rev);r.field("source_sample",source_sample);r.field("bank_sample",bank_sample);r.field("media_origin",origin);r.field("transmission_id",transmission_id);
    r.field("simulation",v.simulation);r.field("seed",v.simulation_seed);r.field("snr_db",v.simulation_snr_db);
    r.field("clock_ppm",v.simulation_clock_error_ppm);r.field("frequency_offset_hz",v.simulation_frequency_offset_hz);
    r.field("phase_noise",v.simulation_phase_noise_degrees_per_sqrt_second);r.field("dsp_workspace",v.dsp_workspace_bytes);r.field("content_limit",v.content_limit);
}
inline std::string sha256(const void* bytes,std::size_t count) {
    std::array<unsigned char,EVP_MAX_MD_SIZE> digest{};unsigned length=0;
    if(EVP_Digest(bytes,count,digest.data(),&length,EVP_sha256(),nullptr)!=1||length!=32)return {};
    constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
    for(unsigned i=0;i<length;++i){result+=hex[digest[i]>>4];result+=hex[digest[i]&15];}return result;
}
inline std::string file_sha256(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);if(!input)return {};
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    if(!ctx||EVP_DigestInit_ex(ctx.get(),EVP_sha256(),nullptr)!=1)return {};
    std::array<char,65536> buffer{};
    while(input){input.read(buffer.data(),buffer.size());const auto count=input.gcount();if(count>0&&EVP_DigestUpdate(ctx.get(),buffer.data(),static_cast<std::size_t>(count))!=1)return {};}
    if(!input.eof())return {};
    std::array<unsigned char,EVP_MAX_MD_SIZE> digest{};unsigned length=0;
    if(EVP_DigestFinal_ex(ctx.get(),digest.data(),&length)!=1||length!=32)return {};
    constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
    for(unsigned i=0;i<length;++i){result+=hex[digest[i]>>4];result+=hex[digest[i]&15];}return result;
}
inline bool exclusive_write(const std::filesystem::path& path,const void* bytes,std::size_t count) {
    auto* file=std::fopen(path.string().c_str(),"wbx");if(!file)return false;
    const bool wrote=std::fwrite(bytes,1,count,file)==count;
    const bool flushed=std::fflush(file)==0;const bool closed=std::fclose(file)==0;
    return wrote&&flushed&&closed;
}
inline void flush() noexcept {
    auto* s=active.load();if(!s)return;
    try {
        std::lock_guard lock(s->mutex);if(s->flushed)return;s->flushed=true;
        bool writers_closed=s->session_count>0;for(std::size_t i=0;i<s->session_count;++i)writers_closed&=s->closed[i]!=0;
        bool committed=true;for(std::size_t i=0;i<s->attempts;++i)committed&=s->descriptors[i].status==1;
        bool contexts=s->bank_count>0;for(std::size_t i=0;i<s->bank_count;++i)contexts&=s->banks[i].context_seen;
        const auto dir=std::filesystem::path(s->directory);
        // arm() exclusively created this directory. Never replace output files.
        for(const auto* name:{"input.f32","events.jsonl","feeds.jsonl","seal.json"})if(std::filesystem::exists(dir/name))return;
        const bool pcm_ok=exclusive_write(dir/"input.f32",s->pcm.data(),s->samples*sizeof(float));
        const bool events_ok=exclusive_write(dir/"events.jsonl",s->metadata.data(),s->bytes);
        std::ostringstream feeds;
        for(std::size_t i=0;i<s->attempts;++i){const auto& d=s->descriptors[i];feeds<<"{\"ordinal\":"<<d.ordinal<<",\"session\":"<<d.session<<",\"bank\":"<<d.bank<<",\"generation\":"<<d.generation<<",\"revision\":"<<d.revision<<",\"first\":"<<d.first<<",\"count\":"<<d.count<<",\"pcm_offset\":"<<d.offset<<",\"end\":"<<d.end<<",\"final_call\":"<<d.final_call<<",\"status\":"<<unsigned(d.status)<<"}\n";}const auto feed_text=feeds.str();const bool feeds_ok=exclusive_write(dir/"feeds.jsonl",feed_text.data(),feed_text.size());
        const auto pcm_hash=sha256(s->pcm.data(),s->samples*sizeof(float)),event_hash=sha256(s->metadata.data(),s->bytes);
        const auto feed_hash=file_sha256(dir/"feeds.jsonl");
        const bool data_ok=pcm_ok&&events_ok&&feeds_ok&&pcm_hash.size()==64&&event_hash.size()==64&&feed_hash.size()==64;
        const std::uint16_t endian=1;const bool little=*reinterpret_cast<const unsigned char*>(&endian)==1;
        const bool complete=writers_closed&&committed&&contexts&&!s->sample_overflow&&!s->descriptor_overflow&&!s->metadata_overflow&&!s->identity_overflow&&!s->config_incomplete&&!s->input_incomplete&&s->samples>0&&little&&sizeof(float)==4&&std::numeric_limits<float>::is_iec559&&data_ok;
        std::ostringstream seal;
        seal<<std::boolalpha<<"{\"schema\":1,\"purpose\":\"copied Help diagnostic input, not qualification\",\"writers_closed\":"<<writers_closed<<",\"all_attempts_committed\":"<<committed<<",\"sample_overflow\":"<<s->sample_overflow<<",\"descriptor_overflow\":"<<s->descriptor_overflow<<",\"metadata_overflow\":"<<s->metadata_overflow<<",\"identity_overflow\":"<<s->identity_overflow<<",\"config_incomplete\":"<<s->config_incomplete<<",\"input_incomplete\":"<<s->input_incomplete<<",\"complete\":"<<complete<<",\"float_little_endian\":"<<little<<",\"float_bytes\":"<<sizeof(float)<<",\"float_iec559\":"<<std::numeric_limits<float>::is_iec559<<",\"samples\":"<<s->samples<<",\"attempts\":"<<s->attempts<<",\"metadata_bytes\":"<<s->bytes<<",\"records\":"<<s->records<<",\"sample_cap\":"<<sample_cap<<",\"descriptor_cap\":"<<descriptor_cap<<",\"metadata_cap\":"<<metadata_cap<<",\"record_byte_cap\":"<<record_cap<<",\"pcm_sha256\":\""<<pcm_hash<<"\",\"events_sha256\":\""<<event_hash<<"\",\"feeds_sha256\":\""<<feed_hash<<"\",\"data_files_ok\":"<<data_ok<<",\"full_session_settings_captured\":false,\"projected_complex_bytes_captured\":false,\"banks\":[";
        for(std::size_t i=0;i<s->bank_count;++i){const auto& b=s->banks[i];if(i)seal<<",";seal<<"{\"session\":"<<b.session<<",\"bank\":"<<b.identity<<",\"generation\":"<<b.generation<<",\"revision\":"<<b.revision<<",\"continuous_sample_zero_prefix\":"<<b.prefix<<",\"committed_end\":"<<b.next<<",\"config_complete\":"<<b.config_complete<<",\"context_seen\":"<<b.context_seen<<",\"admissions\":"<<b.admissions<<"}";}seal<<"]}\n";
        const auto seal_text=seal.str();const bool seal_ok=exclusive_write(dir/"seal.json",seal_text.data(),seal_text.size());
        std::fprintf(stdout,"{\"event\":\"copied_help_pcm_finalization\",\"complete\":%s,\"seal_close_ok\":%s}\n",complete?"true":"false",seal_ok?"true":"false");std::fflush(stdout);
    } catch(...) {std::fputs("copied Help PCM diagnostic flush failed\n",stderr);}
}
inline void arm() noexcept {
    std::lock_guard lock(arm_mutex);if(active.load())return;
    const auto* env=std::getenv("DATAPUMP_COPIED_HELP_PCM_TRACE");if(!env||!*env)return;
    try {
        if(std::strlen(env)>4096){std::fputs("copied Help PCM path exceeds bound\n",stderr);return;}
        const std::filesystem::path dir(env);if(!dir.is_absolute()||!std::filesystem::create_directory(dir)){std::fputs("copied Help PCM directory must be absolute and new\n",stderr);return;}
        auto state=std::make_unique<State>();state->directory=dir.string();
        if(std::atexit(flush)!=0){std::fputs("copied Help PCM atexit registration failed\n",stderr);return;}
        storage=std::move(state);active.store(storage.get(),std::memory_order_release);
    } catch(...) {std::fputs("copied Help PCM arm failed\n",stderr);}
}
} // namespace datapump::diagnostic::copied_help_pcm
