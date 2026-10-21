#pragma once
// PRIVATE diagnostic overlay only. No API/object-layout or receiver-policy change.
#include "audio_trace.hpp"
#include "receiver_storage_allocator.hpp"
#include "live_receptions.hpp"
#include "native_peak_workspace.hpp"
#include <span>
#include "datapump/streaming_modem.hpp"
#include <type_traits>
#include <openssl/sha.h>
#include <bit>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <optional>
namespace datapump::modem {class StreamingReceiver;}
namespace datapump::diagnostic::first_transition {
inline constexpr std::size_t owner_cap=512,track_cap=128,row_cap=131072;
inline int marker() noexcept {
    static const int mode=[]() noexcept {const auto* p=std::getenv("DATAPUMP_FIRST_TRANSITION_CENSUS");
        return !p||std::strcmp(p,"0")==0?0:std::strcmp(p,"1")==0?1:-1;}();return mode;
}
inline bool enabled() noexcept{return marker()==1;}
inline bool input_enabled()noexcept{return marker()>=0&&audio::trace::framed_census_trace.load(std::memory_order_relaxed)&&bool(audio::trace::active.load());}
inline constexpr std::uint64_t input_witness_cap=8192;
inline std::atomic<std::uint64_t> input_commits{0},input_witnesses{0},input_omitted{0},input_errors{0},input_context_overflows{0};

struct JournalRow {
    std::uint64_t consumer=0,epoch=0,origin=0;std::size_t family=0;
    const char* reason="unavailable";bool retire=false,expiry_known=false,expired=false,frontier_queried=false;
    std::optional<std::uint64_t> frontier;
};
struct Journal {
    const void* bank=nullptr;const void* identity=nullptr;std::uint64_t revision=0,sample=0;double now=0;std::size_t count=0,total=0;bool valid=false;
    std::array<JournalRow,owner_cap> rows{};
};
inline thread_local Journal journal;
inline void refresh_begin(const void* bank,const void* identity,std::uint64_t revision,std::uint64_t sample,double now) noexcept {
    if(!enabled())return;journal.bank=bank;journal.identity=identity;journal.revision=revision;journal.sample=sample;journal.now=now;
    journal.count=journal.total=0;journal.valid=true;
}
inline bool journal_matches(const void* identity,std::uint64_t revision)noexcept{return journal.valid&&identity&&journal.identity==identity&&journal.revision==revision;}
template<class R> bool decision(const R& r,bool retire,const char* reason,bool expiry_known=false,
        bool expired=false,bool queried=false,std::optional<std::uint64_t> frontier={}) noexcept {
    if(enabled()) {++journal.total;if(journal.count<owner_cap)journal.rows[journal.count++]={
        r.point_consumer,r.epoch,r.sample_origin,r.family,reason,retire,expiry_known,expired,queried,frontier};}
    return retire;
}
struct Candidate {
    bool valid=false,config_known=false,search_known=false,allocation_known=false;
    std::uint64_t epoch=0,origin=0,phase=0;std::size_t family=0,key_size=0;
    std::array<unsigned char,32> tag{};
    std::uint32_t rate=0;unsigned sf=0,dsss=0,arithmetic=0,strategy=0;
    double bandwidth=0,carrier=0,integration=0,start=0,radius=0;
    bool start_known=false,search_phases=false;std::size_t initial=0,pairs=0,allocation=0,capacity=0,margin=0;
    const char* stage="preselection";
};
template<class C>void profile(Candidate& p,const C& c) noexcept {
    p.rate=c.sample_rate;p.sf=c.spreading_factor;p.dsss=c.dsss_factor;p.bandwidth=c.bandwidth_hz;
    p.carrier=c.carrier_hz;p.integration=c.integration_seconds;p.phase=c.stream_phase_samples;
    p.arithmetic=static_cast<unsigned>(c.search_arithmetic);p.strategy=static_cast<unsigned>(c.receiver_strategy);
}
template<class Tag>void key(Candidate& c,const Tag& tag) noexcept {
    c.key_size=tag.size();std::copy_n(tag.begin(),std::min(tag.size(),c.tag.size()),c.tag.begin());
}
template<class Tag>std::string hex(const Tag& tag) {
    constexpr char digits[]="0123456789abcdef";std::string result;
    for(std::size_t i=0;i<std::min<std::size_t>(tag.size(),32);++i){result+=digits[tag[i]>>4];result+=digits[tag[i]&15];}return result;
}
struct Capture {std::size_t rows=0,owners=0,tracks=0,states=0,owner=0,track_total=0,errors=0,fallback=0;
    bool limited=false,inspected=false,queue_timeout=false;std::uint64_t capture_id=0;std::size_t components=0,component_bytes=0,optional_bytes=0,queue_clock_reads=0,unavailable_queries=0;};
inline thread_local Capture* active_capture=nullptr;
inline std::atomic<std::uint64_t> callback_errors{0};
template<class F>void emit(Capture& c,const char* event,F fields) noexcept {
    if(c.queue_timeout)return;
    if(c.rows>=row_cap){c.limited=true;return;}
    const auto state=audio::trace::active.load();++c.queue_clock_reads;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(state&&state->queued()>128){++c.queue_clock_reads;if(std::chrono::steady_clock::now()>=deadline){c.queue_timeout=true;++c.errors;return;}std::this_thread::yield();}
    ++c.rows;audio::trace::record(event,[&](auto& f){f.add("schema",1);f.add("capture",c.capture_id);fields(f);});
}
struct OwnerScope {
    Capture* prior;explicit OwnerScope(Capture& c,std::size_t owner) noexcept:prior(active_capture) {
        c.owner=owner;c.inspected=false;active_capture=&c;}
    ~OwnerScope(){active_capture=prior;}
};

struct ConstructorInfo {
    bool valid=false,required_known=false,fallback=false,local=false,streamed=false,permit_local=false,coupled=false;
    std::size_t outer_grant=0,core_grant=0,wrapper=0,frequencies=0,transform=0,track_limit=0;
    long double required_before=0,required_after=0,without_rows=0,native_minimum=0;
    bool quota_failure_known=false;std::size_t quota_request=0,quota_used=0,quota_limit=0;const char* quota_phase="unobserved";
    const char* branch="not_observed";Candidate candidate;
};
struct Context {
    const void* bank=nullptr;const void* identity=nullptr;const std::uint64_t* revision=nullptr;const void* closure=nullptr;
    void(*observe)(const void*,const char*,const ConstructorInfo&,const modem::StreamingReceiver*)noexcept=nullptr;
    ConstructorInfo constructor;
    const std::size_t* accounted=nullptr;const void* accounted_receiver=nullptr;std::uint64_t accounted_consumer=0;
};
inline thread_local Context* context=nullptr;
inline void allocation_failed(const std::bad_alloc& error,const char* phase)noexcept {
    if(!enabled()||!context)return;auto& c=context->constructor;c.quota_failure_known=false;c.quota_phase=phase;
    if(const auto* q=dynamic_cast<const ::datapump::live::detail::ReceiverAllocationLimit*>(&error)) {
        c.quota_failure_known=true;c.quota_request=q->requested_bytes;c.quota_used=q->allocated_bytes;c.quota_limit=q->allocation_limit;
    }
}
struct PendingInput {const void* identity=nullptr;std::uint64_t revision=0,first=0;std::span<const float> samples;};
inline thread_local PendingInput pending_input;
struct PendingInputScope {
    PendingInput prior;
    PendingInputScope(const void* identity,std::uint64_t revision,std::uint64_t first,std::span<const float> samples)noexcept:prior(pending_input){if(enabled())pending_input={identity,revision,first,samples};}
    ~PendingInputScope(){pending_input=prior;}
};

struct Scope {
    Context state;Context* prior=nullptr;
    template<class F>Scope(const void* bank,const void* identity,const std::uint64_t& revision,F& observe)noexcept:prior(context) {
        state.bank=bank;state.identity=identity;state.revision=&revision;state.closure=&observe;
        state.observe=[](const void* p,const char* why,const ConstructorInfo& c,const modem::StreamingReceiver* m)noexcept{(*static_cast<F*>(p))(why,c,m);};
        if(enabled())context=&state;
    }
    ~Scope(){context=prior;}
};
struct AccountingScope {
    Context* c=context;const std::size_t* prior=nullptr;const void* receiver=nullptr;std::uint64_t consumer=0;
    template<class R>AccountingScope(const R& r,const std::size_t& bytes)noexcept {
        if(!c)return;prior=c->accounted;receiver=c->accounted_receiver;consumer=c->accounted_consumer;
        c->accounted=&bytes;c->accounted_receiver=&r;c->accounted_consumer=r.point_consumer;
    }
    ~AccountingScope(){if(c){c->accounted=prior;c->accounted_receiver=receiver;c->accounted_consumer=consumer;}}
};
struct Prefix {const void* quota=nullptr;std::uint64_t revision=0,next=0,partitions=0;SHA256_CTX sha{};bool valid=true;unsigned witness_mask=0;};
inline thread_local std::array<Prefix,16> prefixes{};
inline thread_local std::size_t prefix_count=0;
inline thread_local bool prefix_overflow=false;
template<class B>const Prefix* prefix(const B& bank)noexcept{for(std::size_t i=0;i<prefix_count;++i)if(prefixes[i].quota==bank.source_quota.get()&&prefixes[i].revision==bank.revision)return &prefixes[i];return nullptr;}
template<class B,class Samples>void committed(const B& bank,std::uint64_t first,const Samples& samples,std::uint64_t count)noexcept {
    if(!input_enabled())return;++input_commits;std::size_t i=0;for(;i<prefix_count;++i)if(prefixes[i].quota==bank.source_quota.get()&&prefixes[i].revision==bank.revision)break;
    if(i==prefix_count){if(i==prefixes.size()){prefix_overflow=true;++input_context_overflows;return;}++prefix_count;prefixes[i].quota=bank.source_quota.get();prefixes[i].revision=bank.revision;prefixes[i].valid=SHA256_Init(&prefixes[i].sha)==1;}
    auto& x=prefixes[i];if(!first&&x.next){x={};x.quota=bank.source_quota.get();x.revision=bank.revision;x.valid=SHA256_Init(&x.sha)==1;}
    if(first!=x.next||samples.size()!=count||count>SIZE_MAX/sizeof(float)){x.valid=false;++input_errors;return;}
    if(x.valid)x.valid=SHA256_Update(&x.sha,samples.data(),samples.size()*sizeof(float))==1;
    x.next+=count;++x.partitions;
    const auto ordinal=input_witnesses.fetch_add(1)+1;
    if(ordinal>input_witness_cap){++input_omitted;return;}
    auto copy=x.sha;std::array<unsigned char,SHA256_DIGEST_LENGTH> digest{};
    const bool valid=x.valid&&!prefix_overflow&&SHA256_Final(digest.data(),&copy)==1;
    if(!valid)++input_errors;
    audio::trace::record("first_transition_input_witness",[&](auto& f){
        const auto stamp=audio::trace::first_transition_case_stamp.load();
        f.add("schema",1);f.add("witness",ordinal);f.add("case_stamp",stamp);
        f.add("bank_identity",reinterpret_cast<std::uintptr_t>(bank.receiver_allocations.get()));
        f.add("revision",bank.revision);f.add("sample",bank.samples);f.add("first",first);f.add("count",count);
        f.add("partitions",x.partitions);f.add("committed_pcm_samples",x.next);
        f.add("committed_pcm_sha256_valid",valid);if(valid)f.add("committed_pcm_sha256",hex(digest));
        f.add("pcm_little_endian",std::endian::native==std::endian::little);
        f.add("prefix_overflow",prefix_overflow);f.add("scope","same bounded committed PCM witness in observer OFF and ON; no pending input");
    });

}
template<class C,class Tag>void selected(const C& config,const Tag& tag,std::uint64_t epoch,std::uint64_t origin,std::size_t family)noexcept {
    if(!enabled()||!context)return;auto& x=context->constructor;x={};x.candidate.valid=true;x.candidate.config_known=true;
    profile(x.candidate,config);key(x.candidate,tag);x.candidate.epoch=epoch;x.candidate.origin=origin;x.candidate.family=family;x.candidate.stage="admission_candidate_before_preflight";
}
inline void preflight(std::size_t capacity,std::size_t margin)noexcept {if(!enabled()||!context)return;auto& p=context->constructor.candidate;p.capacity=capacity;p.margin=margin;p.stage="workspace_preflight_current_candidate";}
inline std::atomic<bool> fallback_taken{false},limit_taken{false};
inline std::atomic<std::uint64_t> capture_ids{0},checks{0},attempts{0},trims{0},context_mismatches{0},continuation_calls{0};
inline void transition(const void* bank,const char* origin,const modem::StreamingReceiver* candidate=nullptr)noexcept {
    if(!enabled())return;++checks;
    if(!context||context->bank!=bank){++context_mismatches;return;}
    if(limit_taken.exchange(true))return;
    context->observe(context->closure,origin,context->constructor,candidate);
}
template<class R,class S,class Selection>void attempt(const R& r,const S& search,const Selection& selection,std::size_t grant,std::size_t capacity,std::size_t margin)noexcept {
    if(!enabled()||!context)return;++attempts;auto& x=context->constructor;x={};x.valid=true;x.outer_grant=grant;
    auto& p=x.candidate;p.valid=p.config_known=p.search_known=p.allocation_known=true;
    profile(p,r.options.modem);key(p,r.key_tag);p.epoch=r.epoch;p.origin=r.sample_origin;p.family=r.family;
    p.initial=search.initial_stream_symbols;p.pairs=search.hypotheses.size();p.search_phases=search.search_stream_phases;
    p.start_known=search.start_offset_seconds.has_value();if(p.start_known)p.start=*search.start_offset_seconds;
    p.radius=search.start_uncertainty_seconds;p.allocation=grant;p.capacity=capacity;p.margin=margin;p.stage="constructor_attempt";
    (void)selection;
}

inline void requirements(long double before,long double after,long double without,std::size_t track_limit,bool streamed,bool permit,bool coupled,std::size_t f,std::size_t transform)noexcept {
    if(!enabled()||!context)return;auto& x=context->constructor;x.required_known=true;x.required_before=before;x.required_after=after;
    x.without_rows=without;x.native_minimum=without+2*track_limit+2;x.track_limit=track_limit;x.streamed=streamed;x.permit_local=permit;x.coupled=coupled;x.frequencies=f;x.transform=transform;
}
inline void fallback(const char* branch,bool local=false)noexcept {if(!enabled()||!context)return;auto& x=context->constructor;x.fallback=true;x.branch=branch;x.local=local;}
struct Reclaim {const void* bank=nullptr;const void* identity=nullptr;std::uint64_t revision=0,calls=0;std::size_t receiver_and_shared=0,source=0,after=0;};
inline thread_local Reclaim last_reclaim;
inline void reclaimed(const void* bank,const void* identity,std::uint64_t revision,std::size_t released,std::size_t source,std::size_t after)noexcept {if(enabled())last_reclaim={bank,identity,revision,last_reclaim.calls+1,released,source,after};}
struct Trim {std::uint64_t bank=0,sequence=0;const void* identity=nullptr;std::uint64_t revision=0;std::size_t before=0,after=0;};
inline thread_local Trim last_trim;
inline void trimmed(std::size_t before,std::size_t after)noexcept {if(!enabled())return;const auto n=++trims;last_trim={context?reinterpret_cast<std::uintptr_t>(context->bank):0,n,context?context->identity:nullptr,context&&context->revision?*context->revision:0,before,after};}
struct Continuation {const void* impl=nullptr;std::uint64_t epoch=0,origin=0,calls=0,sample=0,bins=0,cap=0;bool final=false;};
inline thread_local std::array<Continuation,512> continuations{};
inline thread_local std::size_t continuation_count=0,last_continuation=512;
inline thread_local bool continuation_overflow=false;
template<class S>void core_begin(const S& s,std::size_t bytes)noexcept {if(!enabled()||!context)return;auto& x=context->constructor;x.core_grant=bytes;x.wrapper=s.wrapper_bytes();for(std::size_t i=0;i<continuation_count;++i)if(continuations[i].impl==&s)continuations[i].calls=0;}
template<class S>void continuation(const S& s,bool final,std::uint64_t cap)noexcept {
    if(!enabled())return;++continuation_calls;std::size_t i=last_continuation;
    const auto same=[&](const Continuation& x){return x.impl==&s&&x.epoch==s.config.stream_epoch&&x.origin==s.search.input_sample_origin;};
    if(i>=continuation_count||!same(continuations[i])){for(i=0;i<continuation_count&&!same(continuations[i]);++i){} }
    if(i==continuation_count){if(i==continuations.size()){continuation_overflow=true;return;}++continuation_count;continuations[i].impl=&s;continuations[i].epoch=s.config.stream_epoch;continuations[i].origin=s.search.input_sample_origin;}
    last_continuation=i;auto& x=continuations[i];++x.calls;x.sample=s.sample;x.bins=s.bins;x.cap=cap;x.final=final;
}
template<class V>void vector(Capture& c,const char* name,const V& values,std::size_t index=0,bool additive=true)noexcept {
    const auto bytes=values.capacity()*sizeof(typename V::value_type);++c.components;if(additive)c.component_bytes+=bytes;
    emit(c,"first_transition_component",[&](auto& f){f.add("owner",c.owner);f.add("component",name);f.add("index",index);f.add("size",values.size());f.add("capacity",values.capacity());f.add("element_bytes",sizeof(typename V::value_type));f.add("capacity_bytes",bytes);f.add("additive",additive);});
}
template<class S>void inspect(const S& s) noexcept {
    auto* p=active_capture;if(!p)return;auto& c=*p;c.inspected=true;++c.states;
    try {
        if(s.fallback){--c.states;emit(c,"first_transition_wrapper",[&](auto& f){f.add("owner",c.owner);
            f.add("wrapper_bytes",s.wrapper_bytes());f.add("local_clock_fallback",s.local_search_fallback);
            f.add("backend","pattern_receiver_correlator_wrapper");});return;}
        c.track_total+=s.tracks.size();if(s.tracks.size()>track_cap)c.limited=true;
        emit(c,"first_transition_pattern_state",[&](auto& f){f.add("owner",c.owner);f.add("available",true);
            f.add("backend","pattern_receiver");f.add("epoch",s.config.stream_epoch);f.add("input_origin",s.search.input_sample_origin);
            f.add("input_samples",s.sample);f.add("bins",s.bins);f.add("bin_samples",s.bin_samples);f.add("length_bins",s.length);
            f.add("hop_bins",s.hop);f.add("next_start_bin",s.next_start);f.add("earliest_unscored_start_bin",s.earliest_unscored_start);
            f.add("continuation_schedule_bin",s.continuation_schedule);f.add("track_total",s.tracks.size());f.add("track_retained",std::min(track_cap,s.tracks.size()));
            f.add("budget",s.budget);f.add("fixed_reservation",s.fixed_reservation);
            f.add("current_working_bytes",s.working_bytes());
            f.add("acquisition_window_available",s.acquisition_window.has_value());
            if(s.acquisition_window) {
                f.add("acquisition_window_epoch_origin_samples",s.acquisition_window->epoch_origin_samples);
                f.add("acquisition_window_phase_scale",s.acquisition_window->phase_scale);
                f.add("acquisition_window_half_width_samples",s.acquisition_window->half_width_samples);
            }
            f.add("broad_start_window",s.broad_start_window);
            f.add("restricted_timing_active_available",static_cast<bool>(s.fft_workload));
            if(s.fft_workload)f.add("restricted_timing_active",s.fft_workload->restricted_timing_active);
            f.add("current_cache_reservation_bytes",s.cache_reservation);f.add("current_scoring_reservation_bytes",s.scoring_reservation);
            f.add("current_arithmetic_reservation_bytes",s.arithmetic_reservation);
            f.add("current_native_active_bytes",s.native_active_bytes);f.add("current_native_plan_bytes",s.native_plan_bytes());
            f.add("current_fixed_reservation_bytes",s.fixed_reservation);
            f.add("current_latest_bit_capacity_bytes",s.latest.bits.capacity());
            f.add("current_history_bytes",s.history.capacity()*sizeof(typename decltype(s.history)::value_type));
            f.add("current_peak_bytes",s.peaks.capacity()*sizeof(typename decltype(s.peaks)::value_type));
            f.add("history_size",s.history.size());f.add("history_capacity",s.history.capacity());
            f.add("peak_size",s.peaks.size());f.add("peak_capacity",s.peaks.capacity());
            f.add("evidence_object_bytes",sizeof(typename decltype(s.history)::value_type));
            f.add("candidate_limit",s.search.candidate_limit);f.add("track_limit",s.search.track_limit);
            f.add("transform_bins",s.transform);f.add("frequency_count",s.search.frequency_offsets_hz.size());
            f.add("current_ring_bytes",s.ring.capacity()*sizeof(typename decltype(s.ring)::value_type));
            f.add("current_point_bytes",s.points.capacity()*sizeof(typename decltype(s.points)::value_type));
            f.add("current_execution_vector_bytes",(s.work.capacity()+s.spectrum.capacity()+s.product.capacity()+s.reference.capacity())*sizeof(typename decltype(s.work)::value_type));
            std::size_t track_bit_capacity=0,burst_bit_capacity=0;
            for(const auto& track:s.tracks)track_bit_capacity+=track.burst.bits.capacity();
            for(const auto& burst:s.bursts)burst_bit_capacity+=burst.bits.capacity();
            f.add("current_track_bit_capacity_bytes",track_bit_capacity);f.add("current_burst_bit_capacity_bytes",burst_bit_capacity);
            f.add("reservation_semantics","budget/fixed/cache/scoring/arithmetic reservations are charges or limits, not equivalent to heap residency");
            f.add("finished",s.finished);
            f.add("candidate_limited",s.candidate_limit_reached);f.add("trials",s.trials);f.add("ring_bins",s.ring.size());
            f.add("start_known",s.search.start_offset_seconds.has_value());if(s.search.start_offset_seconds)f.add("start_seconds",*s.search.start_offset_seconds);
            f.add("start_radius_seconds",s.search.start_uncertainty_seconds);f.add("initial_symbols",s.search.initial_stream_symbols);
            f.add("search_stream_phases",s.search.search_stream_phases);f.add("calendar_symbols",s.calendar_symbols());
            f.add("phase_upper",s.phase_upper);f.add("timing_minimum_rate",s.timing_minimum_rate);f.add("timing_maximum_rate",s.timing_maximum_rate);
        });

        std::size_t optional=s.cached_templates.capacity()*sizeof(typename decltype(s.cached_templates)::value_type);
        if(!s.required_tracking_reference)optional+=s.tracking_reference.capacity()*sizeof(typename decltype(s.tracking_reference)::value_type);
        for(const auto& b:s.cached_templates){optional+=b.rows.capacity()*sizeof(typename decltype(b.rows)::value_type)+b.energy.capacity()*sizeof(typename decltype(b.energy)::value_type)+b.square.capacity()*sizeof(typename decltype(b.square)::value_type);for(const auto& row:b.rows)for(const auto& v:row)optional+=v.capacity()*sizeof(typename std::remove_reference_t<decltype(v)>::value_type);}
        if(s.cached_from_streamed)optional+=s.scoring_reservation;
        c.optional_bytes+=optional;
        const Continuation* last=nullptr;for(std::size_t k=0;k<continuation_count;++k){const auto& x=continuations[k];if(x.impl==&s&&x.epoch==s.config.stream_epoch&&x.origin==s.search.input_sample_origin){last=&x;break;}}
        emit(c,"first_transition_optional",[&](auto& f){f.add("owner",c.owner);f.add("template_cache_reclaimable_charge_bytes",optional);f.add("includes_scoring_reservation",s.cached_from_streamed);f.add("reservation_part_nonadditive",s.cached_from_streamed);f.add("last_continuation_known",last!=nullptr&&last->calls>0);
            for(std::size_t k=0;k<continuation_count;++k){const auto& x=continuations[k];if(x.calls&&x.impl==&s&&x.epoch==s.config.stream_epoch&&x.origin==s.search.input_sample_origin){f.add("continuation_calls",x.calls);f.add("continuation_sample",x.sample);f.add("continuation_bins",x.bins);f.add("continuation_cap",x.cap);f.add("continuation_final",x.final);break;}}
        });
#define V(n) vector(c,"native." #n,s.n)
        V(history);V(peaks);V(points);V(ring);V(work);V(spectrum);V(product);V(reference);V(energy_prefix);V(packed_phases);V(drift_scratch);V(differential_scratch);V(drift_scores);V(templates);V(template_energy);V(template_square);V(cached_templates);vector(c,"native.scoring",s.scoring,0,false);V(tracking_reference);V(completed);V(bursts);V(tracks);
#undef V
        vector(c,"native.latest.bits",s.latest.bits);vector(c,"search.frequency_offsets",s.search.frequency_offsets_hz);vector(c,"search.clock_errors",s.search.clock_errors_ppm);vector(c,"search.hypotheses",s.search.hypotheses);
        std::size_t nested=0;for(const auto& row:s.templates)for(const auto& v:row)vector(c,"native.templates.values",v,nested++);
        nested=0;for(const auto& b:s.cached_templates){vector(c,"native.cached.rows",b.rows,nested);vector(c,"native.cached.energy",b.energy,nested);vector(c,"native.cached.square",b.square,nested);++nested;}
        nested=0;for(const auto& b:s.cached_templates)for(const auto& row:b.rows)for(const auto& v:row)vector(c,"native.cached.values",v,nested++);
        nested=0;for(const auto& t:s.tracks){vector(c,"native.track.bits",t.burst.bits,nested);vector(c,"native.track.frequency_scores",t.frequency_scores,nested++);}
        nested=0;for(const auto& b:s.bursts)vector(c,"native.burst.bits",b.bits,nested++);
        std::size_t i=0;for(const auto& t:s.tracks){if(i==track_cap)break;const auto ordinal=i++;
            std::uint64_t radius=0;bool geometry=!s.burst_plan;if(!geometry)++c.unavailable_queries;else try{radius=s.track_radius(t);}catch(...){geometry=false;++c.errors;}
            const bool overflow=geometry&&(t.next>UINT64_MAX-s.length||t.next+s.length>UINT64_MAX-radius);
            const auto needed=!geometry||overflow?0:t.next+s.length+radius;
            ++c.tracks;emit(c,"first_transition_track",[&](auto& f){f.add("owner",c.owner);f.add("track",ordinal);
                f.add("admitted",t.admitted);f.add("established",t.established);f.add("confirmed",t.confirmed);
                f.add("chunk_bits",t.burst.bits.size());f.add("missing_slots",t.burst.missing_slots);
                f.add("pending_symbols",t.burst.bits.size()>=t.confirmed?t.burst.bits.size()-t.confirmed:0);
                f.add("pending_count_valid",t.burst.bits.size()>=t.confirmed);f.add("pending_gap",t.pending_gap);f.add("gap_slots",t.gap_slots);
                f.add("stream_first_sample",t.burst.stream_first_sample);f.add("stream_first_symbol",t.burst.stream_first_symbol);
                f.add("stream_phase_samples",t.burst.stream_phase_samples);f.add("first_sample",t.burst.first_sample);
                f.add("end_sample",t.burst.end_sample);f.add("confirmed_end",t.confirmed_end);f.add("complete",t.burst.complete);
                f.add("frequency_hz",t.burst.frequency_hz);f.add("frequency_index",t.frequency);f.add("unconfirmed_symbols",t.unconfirmed_symbols);
                f.add("absent_samples",t.absent_samples);f.add("absence_required_samples",6.L*s.config.sample_rate);
                f.add("next_bin",t.next);f.add("stream_index",t.index);f.add("anchor_start_bin",t.anchor_start);f.add("anchor_index",t.anchor_index);
                f.add("phase_lower",t.phase_lower);f.add("phase_upper",t.phase_upper);f.add("radius_known",geometry);f.add("radius_bins",radius);
                f.add("radius_unavailable_burst_cursor",bool(s.burst_plan));f.add("needed_end_known",geometry&&!overflow);f.add("needed_end_overflow",overflow);if(geometry&&!overflow){f.add("needed_end_bin",needed);f.add("input_ready",needed<=s.bins);}
                f.add("available_bins",s.bins);f.add("lookahead_assumption","non-final continuation radius");
                f.add("pending_score",t.pending_score);f.add("confirmed_score",t.confirmed_score);f.add("penalty",t.penalty);
            });
        }
    }catch(...){++c.errors;}
}
template<class S>void inspect_correlator(const S& s)noexcept {
    auto* p=active_capture;if(!p)return;auto& c=*p;c.inspected=true;++c.states;
    try {
        const auto* lazy=s.lazy_state();
        const auto total=s.hypotheses.size();c.track_total+=total;
        if(total>track_cap)c.limited=true;
        emit(c,"first_transition_pattern_state",[&](auto& f){f.add("owner",c.owner);f.add("available",true);
            f.add("backend","pattern_correlator");f.add("epoch",s.config.stream_epoch);
            f.add("input_origin",s.search.input_sample_origin);f.add("input_samples",s.sample);
            f.add("budget",s.budget);f.add("current_working_bytes",s.working_bytes());
            f.add("accounted_bytes",s.accounted_bytes);f.add("bit_limit",s.bit_limit);
            f.add("candidate_limit",s.search.candidate_limit);f.add("track_limit",s.search.track_limit);
            f.add("track_total",total);f.add("track_retained",std::min(total,track_cap));
            f.add("finished",s.finished);f.add("amplitude_only",s.options.amplitude_only);
            f.add("lazy",lazy!=nullptr);f.add("burst",bool(s.burst));
            f.add("symbol_samples",s.code.symbol_samples());f.add("trials",s.trials);
            if(lazy){f.add("processed_frontier",lazy->processed);f.add("capture_end",lazy->capture_end);
                f.add("initial_complete_due",lazy->initial_complete_due);}
            f.add("private_component_breakdown_complete",false);
            f.add("private_component_scope","exact public total plus selected vector capacities; residual stays unclassified");
        });
        // These buffers have independent ownership. StableOwnerStore and the
        // scheduled/lazy owners remain within the explicit exact-total residual.
        vector(c,"correlator.history",s.history);vector(c,"correlator.bursts",s.bursts);
        vector(c,"correlator.emissions",s.emissions);vector(c,"correlator.points",s.points);
        vector(c,"correlator.search.frequency_offsets",s.search.frequency_offsets_hz);
        vector(c,"correlator.search.clock_errors",s.search.clock_errors_ppm);
        vector(c,"correlator.search.hypotheses",s.search.hypotheses);
        std::size_t i=0;for(const auto& t:s.hypotheses){if(i==track_cap)break;const auto ordinal=i++;++c.tracks;
            vector(c,"correlator.hypothesis.bits",t.burst.bits,ordinal);
            emit(c,"first_transition_track",[&](auto& f){f.add("owner",c.owner);f.add("track",ordinal);
                f.add("backend","pattern_correlator");f.add("admitted",t.admitted);
                f.add("chunk_bits",t.burst.bits.size());f.add("confirmed",t.committed);
                f.add("pending_count_valid",t.burst.bits.size()>=t.committed);
                f.add("pending_symbols",t.burst.bits.size()>=t.committed?t.burst.bits.size()-t.committed:0);
                f.add("pending_gap",t.pending_gaps);f.add("gap_slots",t.gap_slots);
                f.add("stream_index",t.index);f.add("origin",t.origin);f.add("rate",t.rate);
                f.add("phase_lower",t.phase_lower);f.add("phase_upper",t.phase_upper);
                f.add("observed_start",t.observed_start);f.add("frequency_index",t.frequency);
                f.add("rate_index",t.rate_index);f.add("confirmed_end",t.committed_end);
                f.add("first_sample",t.burst.first_sample);f.add("end_sample",t.burst.end_sample);
                f.add("stream_first_sample",t.burst.stream_first_sample);f.add("stream_first_symbol",t.burst.stream_first_symbol);
                f.add("stream_phase_samples",t.burst.stream_phase_samples);f.add("complete",t.burst.complete);
                f.add("missing_slots",t.burst.missing_slots);f.add("available_samples",s.sample);
                f.add("pending_score",t.pending_score);f.add("confirmed_score",t.sum_score);
                // The actual clock/calendar completion predicate is deliberately
                // not replayed; a simple arithmetic endpoint is not readiness.
                f.add("input_ready_known",false);f.add("absence_known",bool(s.burst));
                f.add("absence_required_seconds",6);
                if(s.burst){const auto& lane=s.burst->lanes[ordinal];
                    f.add("failed_duration_samples",lane.failed_duration);f.add("last_guard_end",lane.last_guard_end);
                    f.add("last_scored_index",lane.last_scored_index);f.add("last_full",lane.last_full);
                    f.add("last_found",lane.last_found);f.add("last_qualifying",lane.last_qualifying);
                    f.add("acquisition_cutoff",lane.acquisition_cutoff);f.add("first_full_index",lane.first_full_index);}
            });
        }
        // Unknown detail is not silently certified as complete capture.
        ++c.fallback;
    }catch(...){++c.errors;}
}
inline bool thread_cpu(long double& result) noexcept {
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec stamp{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&stamp))return false;
    result=static_cast<long double>(stamp.tv_sec)+static_cast<long double>(stamp.tv_nsec)/1000000000.L;return true;
#else
    return false;
#endif
}
inline const char* case_name(std::uint64_t stamp)noexcept {
    switch(stamp&3){case 1:return "raw";case 2:return "short";case 3:return "framed";default:return "unknown_external_join_required";}
}
template<class B,class V,class Workspace>void capture(const B& bank,const V& value,const char* reason,const ConstructorInfo& ctor,
        const modem::StreamingReceiver* candidate,double now,std::size_t capacity,std::size_t recomputed,Workspace workspace)noexcept {
    const auto case_stamp=audio::trace::first_transition_case_stamp.load();Capture c;c.capture_id=++capture_ids;long double cb=0,ce=0;const bool cbv=thread_cpu(cb);const auto wb=std::chrono::steady_clock::now();
    try {
        emit(c,"first_transition_begin",[&](auto& f){f.add("reason",reason);f.add("kind","first_coverage_condition");
            f.add("bank_identity",reinterpret_cast<std::uintptr_t>(bank.receiver_allocations.get()));f.add("bank_identity_kind","receiver_allocation_account+revision");f.add("bank_address",reinterpret_cast<std::uintptr_t>(&bank));f.add("revision",bank.revision);f.add("sample",bank.samples);f.add("case",case_name(case_stamp));f.add("case_sequence",case_stamp>>2);
            f.add("now",now);f.add("observed_epoch_known",bank.observed_epoch.has_value());if(bank.observed_epoch)f.add("observed_epoch",*bank.observed_epoch);f.add("observed_sample",bank.observed_sample);
            f.add("coverage_limited_before_store",bank.coverage_limited);f.add("resident",bank.receivers().size());f.add("admissions",bank.admissions);f.add("retirements",bank.retirements);
            f.add("configured_dsp_bytes",value.dsp_workspace_bytes);f.add("bank_capacity",capacity);f.add("stored_bank_bytes",bank.working_bytes);f.add("recomputed_bank_bytes",recomputed);
            f.add("stored_minus_recomputed",static_cast<std::int64_t>(bank.working_bytes)-static_cast<std::int64_t>(recomputed));f.add("stored_matches_recomputed",bank.working_bytes==recomputed);
            f.add("receiver_storage_kind","allocator_list");f.add("receiver_storage_nodes",bank.receivers().size());f.add("receiver_container_object_bytes",bank.receiver_storage_bytes());f.add("receiver_inline_bytes",sizeof(typename std::remove_reference_t<decltype(bank.receivers())>::value_type));
            f.add("receiver_allocation_bytes",bank.receiver_allocations->allocated_bytes());f.add("receiver_allocation_limit",bank.receiver_allocations->allocation_limit());
            f.add("actual_quota_failure_known",ctor.quota_failure_known);f.add("actual_quota_failure_phase",ctor.quota_phase);
            if(ctor.quota_failure_known){f.add("actual_quota_requested_bytes",ctor.quota_request);f.add("actual_quota_used_bytes",ctor.quota_used);f.add("actual_quota_limit_bytes",ctor.quota_limit);}
            const bool pending_match=pending_input.identity==bank.receiver_allocations.get()&&pending_input.revision==bank.revision;
            f.add("trigger_input_known",pending_match);if(pending_match) {
                f.add("trigger_input_first",pending_input.first);f.add("trigger_input_samples",pending_input.samples.size());
                std::array<unsigned char,SHA256_DIGEST_LENGTH> pd{};const bool pv=SHA256(reinterpret_cast<const unsigned char*>(pending_input.samples.data()),pending_input.samples.size_bytes(),pd.data())!=nullptr;
                f.add("trigger_input_sha256_valid",pv);if(pv)f.add("trigger_input_sha256",hex(pd));
            }

            f.add("caller_control_allowance",65536);f.add("native_peak_bytes",bank.native_peaks?bank.native_peaks->capacity_bytes():0);f.add("native_peak_leased",bank.native_peaks&&bank.native_peaks->leased());
            f.add("native_point_working_bytes",bank.native_points?bank.native_points->working_bytes():0);f.add("native_point_data_bytes",bank.native_points?bank.native_points->data_bytes():0);
            f.add("native_point_active_bytes_nonadditive",bank.native_points?bank.native_points->active_bytes():0);f.add("native_point_entries",bank.native_points?bank.native_points->entries():0);f.add("native_point_owners",bank.native_points?bank.native_points->owners():0);
            f.add("reclaim_calls",last_reclaim.calls);f.add("last_reclaim_identity",reinterpret_cast<std::uintptr_t>(last_reclaim.identity));f.add("last_reclaim_revision",last_reclaim.revision);f.add("last_reclaim_bank_match",last_reclaim.identity==bank.receiver_allocations.get()&&last_reclaim.revision==bank.revision);f.add("last_reclaim_receiver_and_shared_bytes",last_reclaim.receiver_and_shared);f.add("last_reclaim_source_bytes",last_reclaim.source);f.add("last_reclaim_after_stored_bytes",last_reclaim.after);f.add("last_trim_scope","one most recent peer PatternReceiver::trim_optional_workspace, NOT aggregate reclaim_scratch");f.add("last_trim_sequence",last_trim.sequence);f.add("last_trim_bank",last_trim.bank);f.add("last_trim_identity",reinterpret_cast<std::uintptr_t>(last_trim.identity));f.add("last_trim_revision",last_trim.revision);f.add("last_trim_matches_bank",last_trim.identity==bank.receiver_allocations.get()&&last_trim.revision==bank.revision);f.add("last_trim_before",last_trim.before);f.add("last_trim_after",last_trim.after);
            f.add("receiver_limit",bank.receiver_limit.substr(0,512));f.add("oscillator_limit",bank.oscillator_limit.substr(0,512));f.add("retirement_pending",bank.retirement_pending);
            f.add("journal_current",journal_matches(bank.receiver_allocations.get(),bank.revision));f.add("journal_bank",reinterpret_cast<std::uintptr_t>(journal.bank));f.add("journal_identity",reinterpret_cast<std::uintptr_t>(journal.identity));f.add("journal_revision",journal.revision);f.add("journal_sample",journal.sample);f.add("journal_now",journal.now);f.add("journal_total",journal.total);
            f.add("pending_accounted_known",context&&context->accounted);if(context&&context->accounted){f.add("pending_consumer",context->accounted_consumer);f.add("pending_accounted_bytes",*context->accounted);for(const auto& r:bank.receivers())if(&r==context->accounted_receiver)f.add("pending_actual_bytes",workspace(r));}
            const auto* input=prefix(bank);bool valid=!bank.samples;std::uint64_t n=0,partitions=0;SHA256_CTX copy{};
            if(input){valid=input->valid&&input->next==bank.samples;n=input->next;partitions=input->partitions;copy=input->sha;}else valid=valid&&SHA256_Init(&copy)==1;
            std::array<unsigned char,SHA256_DIGEST_LENGTH> digest{};if(valid)valid=SHA256_Final(digest.data(),&copy)==1;
            f.add("committed_pcm_sha256_valid",valid);if(valid)f.add("committed_pcm_sha256",hex(digest));f.add("committed_pcm_samples",n);f.add("committed_pcm_partitions",partitions);f.add("committed_pcm_boundary",bank.samples);
            f.add("prefix_overflow",prefix_overflow);f.add("pcm_format","native float32 object bytes, little-endian host required; committed blocks only, current uncommitted input excluded");f.add("pcm_little_endian",std::endian::native==std::endian::little);f.add("diagnostic_prefix_storage_bytes",sizeof(prefixes));
            f.add("decision_scope","condition/constructor success already evaluated; original bank flag store or candidate admission may follow; generated is not transmitted");
        });
        std::size_t owner_now=0;for(const auto& r:bank.receivers())owner_now+=workspace(r);
        emit(c,"first_transition_shared",[&](auto& f){
            f.add("recomputed_bank_bytes",recomputed);f.add("owner_current_sum",owner_now);
            const auto control=sizeof(::datapump::live::detail::ReceptionHistory)+sizeof(bank.source_curves)+sizeof(bank.exact_source_samples)+
                sizeof(bank.exact_source_generation)+sizeof(bank.next_exact_source_trace)+sizeof(bank.exact_source_points)+
                sizeof(bank.point_source_generation)+sizeof(bank.next_point_consumer)+sizeof(bank.next_point_source_trace)+
                sizeof(bank.permutations)+sizeof(bank.temporal_plans)+sizeof(bank.deferred_input)+sizeof(bank.input_queued_at)+
                sizeof(bank.shared_capture)+sizeof(bank.future_capture);
            const auto storage=sizeof(bank.receiver_allocations)+sizeof(::datapump::live::detail::ReceiverAllocationAccount)+
                bank.receiver_storage_bytes()+bank.receiver_allocations->allocated_bytes();
            const auto native=sizeof(bank.native_peaks)+sizeof(::datapump::modem::detail::NativePeakWorkspace)+sizeof(bank.native_points)+
                (bank.native_peaks?bank.native_peaks->capacity_bytes():0)+(bank.native_points?bank.native_points->working_bytes():0);
            const auto curves=bank.source_curves?bank.source_curves->working_bytes():0;
            const auto samples=bank.exact_source_samples?bank.exact_source_samples->working_bytes():0;
            const auto points=bank.exact_source_points?bank.exact_source_points->working_bytes():0;
            const auto permutations=bank.permutations?bank.permutations->working_bytes():0;
            const auto plans=bank.temporal_plans?bank.temporal_plans->working_bytes():0;
            const auto capture_reservation=bank.shared_capture?bank.shared_capture->capacity_reservation():0;
            const auto deferred=bank.deferred_input.working_bytes();
            const auto sum=control+storage+native+curves+samples+points+permutations+plans+capture_reservation+deferred+65536;
            f.add("control_object_bytes",control);f.add("receiver_allocator_and_nodes",storage);f.add("native_pool_charge",native);
            f.add("source_curve_charge",curves);f.add("exact_sample_charge",samples);f.add("exact_point_charge",points);
            f.add("permutation_charge",permutations);f.add("temporal_plan_charge",plans);
            f.add("capture_capacity_reservation_nonheap",capture_reservation);f.add("deferred_input_charge",deferred);
            f.add("caller_control_allowance",65536);f.add("shared_once_sum",sum);
            f.add("bank_ledger_matches",owner_now+sum==recomputed);
            f.add("semantics","application workspace charges, including explicit reservation; not process heap or RSS");
        });
        emit(c,"first_transition_candidate",[&](auto& f){const auto& x=ctor;const auto& p=x.candidate;
            f.add("valid",p.valid);f.add("constructor_valid",x.valid);f.add("candidate_stage",p.stage);f.add("config_known",p.config_known);f.add("search_known",p.search_known);bool resident=false;for(const auto& r:bank.receivers())resident|=r.modem.get()==candidate;f.add("candidate_in_bank",candidate&&resident);f.add("epoch",p.epoch);f.add("family",p.family);f.add("origin",p.origin);f.add("key_tag",hex(p.tag));f.add("key_tag_size",p.key_size);
            f.add("sample_rate",p.rate);f.add("bandwidth_hz",p.bandwidth);f.add("carrier_hz",p.carrier);f.add("integration_seconds",p.integration);f.add("spreading_factor",p.sf);f.add("dsss_factor",p.dsss);f.add("stream_phase",p.phase);f.add("arithmetic",p.arithmetic);f.add("strategy",p.strategy);f.add("initial_symbols",p.initial);f.add("pairs",p.pairs);f.add("search_phases",p.search_phases);f.add("start_known",p.start_known);if(p.start_known)f.add("start",p.start);f.add("radius",p.radius);f.add("allocation_known",p.allocation_known);f.add("candidate_capacity",p.capacity);f.add("candidate_control_margin",p.margin);f.add("outer_grant",x.outer_grant);f.add("core_grant",x.core_grant);f.add("wrapper_bytes",x.wrapper);f.add("required_known",x.required_known);f.add("required_before_rows",x.required_before);f.add("required_after_rows",x.required_after);f.add("without_rows",x.without_rows);f.add("native_minimum",x.native_minimum);
            f.add("branch",x.branch);f.add("fallback",x.fallback);f.add("local_fallback",x.local);f.add("streamed",x.streamed);f.add("permit_local",x.permit_local);f.add("coupled",x.coupled);f.add("frequency_count",x.frequencies);f.add("transform",x.transform);f.add("track_limit",x.track_limit);
            f.add("candidate_modem_known",candidate!=nullptr);if(candidate){f.add("candidate_working_bytes",candidate->working_bytes());f.add("candidate_reserved_bytes",candidate->reserved_workspace_bytes());f.add("candidate_clock_windowed",candidate->clock_windowed());f.add("candidate_local_clock_fallback",candidate->local_clock_fallback());f.add("candidate_limited",candidate->candidate_limited());}
            f.add("candidate_scope",candidate?"fully constructed StreamingReceiver; candidate_in_bank distinguishes new admission from replacement":"last constructor metadata only; not necessarily the owner causing this limit");
        });
        if(bank.receivers().size()>owner_cap)c.limited=true;
        std::size_t sum=0;std::size_t ordinal=0;
        for(const auto& r:bank.receivers()){if(ordinal==owner_cap)break;c.owner=ordinal++;++c.owners;
            bool sync=false,complete=false;std::optional<std::uint64_t> frontier;const auto component_before=c.component_bytes;
            {OwnerScope scope(c,c.owner);sync=r.modem->synchronized();if(!r.options.modem.symbol_burst.enabled())complete=r.modem->initial_search_complete();else ++c.unavailable_queries;frontier=r.modem->delayed_processing_frontier();}
            if(!c.inspected){++c.errors;}
            const auto charge=workspace(r);sum+=charge;
            std::size_t owner_bursts=r.bursts.capacity()*sizeof(typename std::remove_reference_t<decltype(r.bursts)>::value_type);for(const auto& b:r.bursts)owner_bursts+=b.bits.capacity();
            const auto owner_plan=r.options.key&&r.options.modem.fake_hopping?sizeof(modem::FhssPlan)+r.options.modem.fake_hopping->power_fallback_reason.capacity():0;
            const auto owner_source=r.source_curve_family?sizeof(typename std::remove_reference_t<decltype(r.source_curve_family)>::element_type):0;
            const JournalRow* j=nullptr;if(journal_matches(bank.receiver_allocations.get(),bank.revision))for(std::size_t k=0;k<journal.count;++k){const auto& x=journal.rows[k];if(x.consumer==r.point_consumer&&x.epoch==r.epoch&&x.origin==r.sample_origin&&x.family==r.family){j=&x;break;}}
            emit(c,"first_transition_owner",[&](auto& f){f.add("owner",c.owner);f.add("consumer",r.point_consumer);f.add("epoch",r.epoch);f.add("family",r.family);f.add("origin",r.sample_origin);f.add("key_tag",hex(r.key_tag));f.add("workspace",charge);f.add("admitted_at",r.admitted_at);f.add("last_confident_at",r.last_confident_at);f.add("last_confident_end",r.last_confident_end);f.add("search_seconds",r.options.search_seconds);f.add("integration_seconds",r.options.modem.integration_seconds);f.add("sample_rate",r.options.modem.sample_rate);f.add("bandwidth_hz",r.options.modem.bandwidth_hz);f.add("carrier_hz",r.options.modem.carrier_hz);f.add("spreading_factor",r.options.modem.spreading_factor);f.add("dsss_factor",r.options.modem.dsss_factor);
                f.add("modem_working",r.modem->working_bytes());f.add("modem_reserved",r.modem->reserved_workspace_bytes());f.add("content_working",r.content?r.content->working_bytes():0);f.add("presentation_count",r.presentations.size());f.add("presentation_bytes",r.presentations.size()*(sizeof(typename std::remove_reference_t<decltype(r.presentations)>::value_type)+4*sizeof(void*)));
                f.add("score_history_heap",r.pattern_score_history.working_bytes());f.add("history_ordinal_heap",r.history_ordinals.heap_bytes());f.add("key_capacity",r.key_tag.capacity());f.add("option_target_capacity_bytes",r.options.receive_targets_db_hz.capacity()*sizeof(double));
                f.add("bursts_bytes",owner_bursts);f.add("owned_fhss_plan_bytes",owner_plan);f.add("source_curve_family_bytes",owner_source);const auto owner_known=r.modem->working_bytes()+owner_bursts+r.history_ordinals.heap_bytes()+r.pattern_score_history.working_bytes()+r.key_tag.capacity()+r.options.receive_targets_db_hz.capacity()*sizeof(double)+owner_plan+r.presentations.size()*(sizeof(typename std::remove_reference_t<decltype(r.presentations)>::value_type)+4*sizeof(void*))+(r.content?r.content->working_bytes():0)+owner_source;f.add("owner_component_sum",owner_known);f.add("owner_ledger_matches",owner_known==charge);f.add("point_owner_bound",r.native_points.bound());f.add("point_owner_leased",r.native_points.leased());f.add("synchronized",sync);f.add("initial_search_complete_known",!r.options.modem.symbol_burst.enabled());f.add("initial_search_complete",complete);f.add("clock_windowed",r.modem->clock_windowed());f.add("local_clock_fallback",r.modem->local_clock_fallback());f.add("candidate_limited",r.modem->candidate_limited());
                f.add("ingress_valid",r.sample_origin<=bank.samples);if(r.sample_origin<=bank.samples)f.add("local_ingress",bank.samples-r.sample_origin);f.add("frontier_known",frontier.has_value());if(frontier)f.add("frontier",*frontier);
                f.add("native_component_bytes",c.component_bytes-component_before);f.add("native_other_charge_known",!r.modem->clock_windowed());if(!r.modem->clock_windowed())f.add("native_other_charge",static_cast<std::int64_t>(r.modem->working_bytes())-static_cast<std::int64_t>(c.component_bytes-component_before));
                f.add("retention_journal_match",j!=nullptr);f.add("retention_reason",j?j->reason:"not_observed_in_latest_refresh");if(j){f.add("retire",j->retire);f.add("expiry_known",j->expiry_known);if(j->expiry_known)f.add("expired",j->expired);f.add("retirement_frontier_queried",j->frontier_queried);f.add("retirement_frontier_known",j->frontier.has_value());if(j->frontier)f.add("retirement_frontier",*j->frontier);}
            });
        }
        const auto resident_optional=c.optional_bytes;bool candidate_resident=false;for(const auto& r:bank.receivers())candidate_resident|=r.modem.get()==candidate;
        if(candidate&&!candidate_resident){OwnerScope scope(c,owner_cap);(void)candidate->synchronized();}
        const auto we=std::chrono::steady_clock::now();const bool cev=thread_cpu(ce);const bool cpu=cbv&&cev&&ce>=cb;
        emit(c,"first_transition_summary",[&](auto& f){f.add("rows_before_summary",c.rows-1);f.add("owners",c.owners);f.add("owner_total",bank.receivers().size());f.add("states",c.states);f.add("tracks",c.tracks);f.add("track_total",c.track_total);f.add("components",c.components);f.add("receiver_sum",sum);f.add("resident_optional_template_reclaimable_charge_bytes",resident_optional);f.add("unadmitted_candidate_optional_charge_bytes",c.optional_bytes-resident_optional);
            f.add("limited",c.limited);f.add("unavailable_queries",c.unavailable_queries);f.add("errors",c.errors);f.add("queue_timeout",c.queue_timeout);f.add("fallback_private_state_unavailable",0);f.add("correlator_component_breakdown_incomplete",c.fallback);f.add("continuation_overflow",continuation_overflow);f.add("cpu_valid",cpu);if(cpu)f.add("calling_thread_cpu_seconds",ce-cb);f.add("wall_seconds",std::chrono::duration<long double>(we-wb).count());f.add("dedicated_measurement_clock_reads",4);f.add("backpressure_clock_reads",c.queue_clock_reads);f.add("trace_row_clock_reads",c.rows);
            f.add("clock_scope","capture emission, queries and bounded backpressure; excludes summary, hooks/journal/filter and writer thread");f.add("comparison_scope","single fixed-origin first-loss capture; matched observer OFF/ON required; no qualification or performance claim");
        });
    }catch(...){++c.errors;emit(c,"first_transition_failure",[&](auto& f){f.add("errors",c.errors);f.add("limited",c.limited);f.add("queue_timeout",c.queue_timeout);});}
}
inline void run_summary()noexcept {
    if(input_enabled())audio::trace::record("first_transition_input_summary",[&](auto& f){
        f.add("schema",1);f.add("commits",input_commits.load());f.add("witness_attempts",input_witnesses.load());
        f.add("witness_cap",input_witness_cap);f.add("omitted",input_omitted.load());f.add("errors",input_errors.load());
        f.add("context_overflows",input_context_overflows.load());
        f.add("scope","cumulative common OFF/ON witnesses at joined Session destruction; final reached prefixes are last bank/revision witness rows");
    });
    if(!enabled())return;audio::trace::record("first_transition_run_summary",[&](auto& f){f.add("schema",1);f.add("marker",marker());f.add("fallback_captured",fallback_taken.load());f.add("limit_captured",limit_taken.load());f.add("capture_count",capture_ids.load());f.add("transition_checks",checks.load());f.add("constructor_attempts",attempts.load());f.add("trim_calls",trims.load());f.add("continuation_calls",continuation_calls.load());f.add("context_mismatches",context_mismatches.load());f.add("callback_errors",callback_errors.load());f.add("hook_cpu_measured",false);f.add("scope","cumulative process counters at joined Session destruction inside trace Scope");});
}
} // namespace datapump::diagnostic::first_transition
