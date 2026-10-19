#pragma once
// PRIVATE diagnostic overlay only. No API/object-layout or receiver-policy change.
#include "audio_trace.hpp"
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
namespace datapump::diagnostic::owner_census {
inline constexpr std::size_t owner_cap=128,track_cap=16,row_cap=2400;
inline int marker() noexcept {
    static const int mode=[]() noexcept {const auto* p=std::getenv("DATAPUMP_OWNER_CENSUS_DIAGNOSTIC");
        return !p||std::strcmp(p,"0")==0?0:std::strcmp(p,"1")==0?1:-1;}();return mode;
}
inline bool enabled() noexcept{return marker()==1;}
inline std::atomic<bool> captured{false};
struct JournalRow {
    std::uint64_t consumer=0,epoch=0,origin=0;std::size_t family=0;
    const char* reason="unavailable";bool retire=false,expiry_known=false,expired=false,frontier_queried=false;
    std::optional<std::uint64_t> frontier;
};
struct Journal {
    std::uint64_t revision=0,sample=0;double now=0;std::size_t count=0,total=0;bool valid=false;
    std::array<JournalRow,owner_cap> rows{};
};
inline thread_local Journal journal;
inline void refresh_begin(std::uint64_t revision,std::uint64_t sample,double now) noexcept {
    if(!enabled())return;journal.revision=revision;journal.sample=sample;journal.now=now;
    journal.count=journal.total=0;journal.valid=true;
}
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
    bool limited=false,inspected=false;};
inline thread_local Capture* active_capture=nullptr;
template<class F>void emit(Capture& c,const char* event,F fields) noexcept {
    if(c.rows>=row_cap){c.limited=true;return;}++c.rows;
    audio::trace::record(event,[&](auto& f){f.add("census",1);fields(f);});
}
struct OwnerScope {
    Capture* prior;explicit OwnerScope(Capture& c,std::size_t owner) noexcept:prior(active_capture) {
        c.owner=owner;c.inspected=false;active_capture=&c;}
    ~OwnerScope(){active_capture=prior;}
};
template<class S>void inspect(const S& s) noexcept {
    auto* p=active_capture;if(!p)return;auto& c=*p;c.inspected=true;++c.states;
    try {
        if(s.fallback){++c.fallback;emit(c,"owner_census_pattern_state",[&](auto& f){f.add("owner",c.owner);
            f.add("available",false);f.add("backend","pattern_correlator_fallback");});return;}
        c.track_total+=s.tracks.size();if(s.tracks.size()>track_cap)c.limited=true;
        emit(c,"owner_census_pattern_state",[&](auto& f){f.add("owner",c.owner);f.add("available",true);
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
        std::size_t i=0;for(const auto& t:s.tracks){if(i==track_cap)break;const auto ordinal=i++;
            std::uint64_t radius=0;bool geometry=true;try{radius=s.track_radius(t);}catch(...){geometry=false;++c.errors;}
            const bool overflow=!geometry||t.next>UINT64_MAX-s.length||t.next+s.length>UINT64_MAX-radius;
            const auto needed=overflow?0:t.next+s.length+radius;
            ++c.tracks;emit(c,"owner_census_track",[&](auto& f){f.add("owner",c.owner);f.add("track",ordinal);
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
                f.add("needed_end_overflow",overflow);if(!overflow){f.add("needed_end_bin",needed);f.add("input_ready",needed<=s.bins);}
                f.add("available_bins",s.bins);f.add("lookahead_assumption","non-final continuation radius");
                f.add("pending_score",t.pending_score);f.add("confirmed_score",t.confirmed_score);f.add("penalty",t.penalty);
            });
        }
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
template<class B,class V,class Margin>void capture(const B& bank,const V& value,const Candidate& candidate,
        const std::string& reason,double now,std::size_t capacity,Margin margin_of) noexcept {
    if(!enabled()||captured.exchange(true))return;
    Capture c;long double cpu_begin=0,cpu_end=0;const bool cpu_begin_valid=thread_cpu(cpu_begin);
    const auto begin=std::chrono::steady_clock::now();
    try {
        const auto bank_base=sizeof(decltype(bank.receptions))+sizeof(bank.source_curves)+sizeof(bank.exact_source_samples)+
            sizeof(bank.exact_source_generation)+sizeof(bank.next_exact_source_trace)+sizeof(bank.exact_source_points)+
            sizeof(bank.point_source_generation)+sizeof(bank.next_point_consumer)+sizeof(bank.next_point_source_trace)+
            sizeof(bank.permutations)+sizeof(bank.temporal_plans)+sizeof(bank.deferred_input)+sizeof(bank.input_queued_at)+
            sizeof(bank.shared_capture)+sizeof(bank.future_capture);
        const auto shared_capture=bank.shared_capture?bank.shared_capture->capacity_reservation():0;
        const auto deferred=bank.deferred_input.working_bytes();
        const auto source_curves=bank.source_curves?bank.source_curves->working_bytes():0;
        const auto sample_pool=bank.exact_source_samples?bank.exact_source_samples->working_bytes():0;
        const auto point_pool=bank.exact_source_points?bank.exact_source_points->working_bytes():0;
        const auto permutations=bank.permutations?bank.permutations->working_bytes():0;
        const auto temporal_plans=bank.temporal_plans?bank.temporal_plans->working_bytes():0;
        std::size_t receiver_sum=0;
        emit(c,"owner_census_begin",[&](auto& f){f.add("schema",2);f.add("marker",marker());f.add("reason",reason.substr(0,512));
            f.add("revision",bank.revision);f.add("sample",bank.samples);f.add("now",now);f.add("resident",bank.receivers.size());
            f.add("ledger_scope","application workspace accounting; excludes process heap/RSS outside these charges");
            for(const auto& current:bank.receivers) {
                const auto& config=current.options.modem;
                std::size_t bursts=current.bursts.capacity()*sizeof(modem::PatternBurst);
                for(const auto& burst:current.bursts)bursts+=burst.bits.capacity();
                const auto history=current.history_ordinals.heap_bytes();
                const auto presentations=current.presentations.size()*(sizeof(typename decltype(current.presentations)::value_type)+4*sizeof(void*));
                const auto content=current.content?current.content->working_bytes():0;
                const auto family=current.source_curve_family?sizeof(*current.source_curve_family):0;
                const auto control=margin_of(current);
                const auto modem=current.modem->working_bytes();
                receiver_sum+=modem+control+bursts+history+presentations+content+family;
            }
            const auto recomputed_bank=bank_base+shared_capture+deferred+source_curves+sample_pool+point_pool+permutations+temporal_plans+receiver_sum;
            f.add("stored_bank_working_bytes",bank.working_bytes);f.add("summed_receiver_workspace_bytes",receiver_sum);
            f.add("recomputed_current_bank_ledger_bytes",recomputed_bank);f.add("stored_vs_recomputed_match",bank.working_bytes==recomputed_bank);
            f.add("stored_minus_recomputed_bytes",static_cast<std::int64_t>(bank.working_bytes)-static_cast<std::int64_t>(recomputed_bank));
            f.add("bank_base_bytes",bank_base);f.add("shared_capture_bytes",shared_capture);f.add("deferred_input_bytes",deferred);
            f.add("source_curve_pool_bytes",source_curves);f.add("exact_source_sample_pool_bytes",sample_pool);
            f.add("exact_source_point_pool_bytes",point_pool);f.add("permutation_pool_bytes",permutations);f.add("temporal_plan_pool_bytes",temporal_plans);
            f.add("configured_dsp_workspace_bytes",value.dsp_workspace_bytes);f.add("configured_bank_capacity_bytes",capacity);
            f.add("admissions",bank.admissions);f.add("retirements",bank.retirements);f.add("working_bytes",bank.working_bytes);f.add("capacity",capacity);
            f.add("observed_epoch_known",bank.observed_epoch.has_value());if(bank.observed_epoch)f.add("observed_epoch",*bank.observed_epoch);
            f.add("observed_sample",bank.observed_sample);f.add("retirement_pending",bank.retirement_pending);
            f.add("owner_cap",owner_cap);f.add("track_cap",track_cap);f.add("row_cap",row_cap);
            f.add("journal_valid",journal.valid);f.add("journal_revision",journal.revision);f.add("journal_sample",journal.sample);
            f.add("journal_now",journal.now);f.add("journal_total",journal.total);f.add("journal_retained",journal.count);
        });
        emit(c,"owner_census_candidate",[&](auto& f){const auto& p=candidate;f.add("schema",2);f.add("valid",p.valid);f.add("epoch",p.epoch);
            f.add("family",p.family);f.add("sample_origin",p.origin);f.add("key_tag",hex(p.tag));f.add("key_tag_size",p.key_size);
            f.add("key_tag_truncated",p.key_size>p.tag.size());f.add("stage",p.stage);f.add("sample_rate",p.rate);f.add("bandwidth_hz",p.bandwidth);
            f.add("carrier_hz",p.carrier);f.add("spreading_factor",p.sf);f.add("dsss_factor",p.dsss);f.add("integration_seconds",p.integration);
            f.add("phase",p.phase);f.add("arithmetic",p.arithmetic);f.add("strategy",p.strategy);f.add("config_known",p.config_known);
            f.add("search_known",p.search_known);f.add("start_known",p.start_known);if(p.start_known)f.add("start_seconds",p.start);
            f.add("start_radius_seconds",p.radius);f.add("initial_symbols",p.initial);f.add("search_stream_phases",p.search_phases);
            f.add("frequency_rate_pairs",p.pairs);f.add("allocation_known",p.allocation_known);if(p.allocation_known)f.add("allocation",p.allocation);
            f.add("capacity",p.capacity);f.add("control_margin",p.margin);
        });
        if(bank.receivers.size()>owner_cap)c.limited=true;
        std::size_t owner=0;for(const auto& r:bank.receivers){if(owner==owner_cap)break;const auto ordinal=owner++;++c.owners;
            bool synchronized=false,complete=false,queried=false;std::optional<std::uint64_t> frontier;
            {OwnerScope scope(c,ordinal);try{synchronized=r.modem->synchronized();complete=r.modem->initial_search_complete();frontier=r.modem->delayed_processing_frontier();queried=true;}catch(...){++c.errors;}}
            if(!c.inspected){++c.errors;emit(c,"owner_census_pattern_state",[&](auto& f){f.add("owner",ordinal);f.add("available",false);f.add("backend","no_pattern_receiver_inspection");});}
            const JournalRow* j=nullptr;if(journal.valid&&journal.revision==bank.revision)for(std::size_t i=0;i<journal.count;++i){const auto& x=journal.rows[i];
                if(x.consumer==r.point_consumer&&x.epoch==r.epoch&&x.origin==r.sample_origin&&x.family==r.family){j=&x;break;}}
            emit(c,"owner_census_owner",[&](auto& f){const auto& config=r.options.modem;f.add("owner",ordinal);f.add("consumer",r.point_consumer);
                f.add("epoch",r.epoch);f.add("family",r.family);f.add("key_tag",hex(r.key_tag));f.add("key_tag_size",r.key_tag.size());f.add("key_tag_truncated",r.key_tag.size()>32);
                f.add("sample_origin",r.sample_origin);f.add("ingress_valid",r.sample_origin<=bank.samples);if(r.sample_origin<=bank.samples)f.add("local_ingress",bank.samples-r.sample_origin);
                const auto owner_modem_bytes=r.modem->working_bytes();
                const auto owner_control_bytes=margin_of(r);
                const auto owner_burst_struct_bytes=r.bursts.capacity()*sizeof(modem::PatternBurst);
                std::size_t owner_burst_bit_bytes=0;for(const auto& burst:r.bursts)owner_burst_bit_bytes+=burst.bits.capacity();
                const auto owner_history_bytes=r.history_ordinals.heap_bytes();
                const auto owner_presentation_bytes=r.presentations.size()*(sizeof(typename decltype(r.presentations)::value_type)+4*sizeof(void*));
                const auto owner_content_bytes=r.content?r.content->working_bytes():0;
                const auto owner_family_bytes=r.source_curve_family?sizeof(*r.source_curve_family):0;
                f.add("ledger_modem_reserved_bytes",r.modem->reserved_workspace_bytes());f.add("ledger_modem_working_bytes",owner_modem_bytes);f.add("ledger_control_margin_bytes",owner_control_bytes);
                f.add("ledger_burst_vector_struct_bytes",owner_burst_struct_bytes);f.add("ledger_burst_bit_capacity_bytes",owner_burst_bit_bytes);
                f.add("ledger_history_heap_bytes",owner_history_bytes);f.add("ledger_presentation_bytes",owner_presentation_bytes);
                f.add("ledger_content_working_bytes",owner_content_bytes);f.add("ledger_optional_source_family_bytes",owner_family_bytes);
                f.add("ledger_receiver_workspace_bytes",owner_modem_bytes+owner_control_bytes+owner_burst_struct_bytes+owner_burst_bit_bytes+owner_history_bytes+owner_presentation_bytes+owner_content_bytes+owner_family_bytes);
                f.add("synchronized",synchronized);f.add("initial_search_complete",complete);f.add("query_complete",queried);f.add("frontier_known",frontier.has_value());if(frontier)f.add("frontier",*frontier);
                f.add("sample_rate",config.sample_rate);f.add("bandwidth_hz",config.bandwidth_hz);f.add("carrier_hz",config.carrier_hz);
                f.add("spreading_factor",config.spreading_factor);f.add("dsss_factor",config.dsss_factor);f.add("integration_seconds",config.integration_seconds);
                f.add("phase",config.stream_phase_samples);f.add("arithmetic",static_cast<unsigned>(config.search_arithmetic));f.add("strategy",static_cast<unsigned>(config.receiver_strategy));
                f.add("admitted_at",r.admitted_at);f.add("last_confident_end",r.last_confident_end);f.add("last_confident_at",r.last_confident_at);
                f.add("search_seconds",value.transfer.search_seconds);f.add("explicit_timestamp",value.transfer.timestamp);
                f.add("symbol_seconds",static_cast<long double>(modem::symbol_sample_count(config))/config.sample_rate);
                f.add("prefix_seconds",(static_cast<long double>(modem::training_sample_count(config))+modem::pattern_pulse_padding_samples(config))/config.sample_rate);
                f.add("journal_match",j!=nullptr);f.add("decision_reason",j?j->reason:"not_observed_in_latest_refresh");
                if(j){f.add("decision_retire",j->retire);f.add("expiry_known",j->expiry_known);if(j->expiry_known)f.add("expired",j->expired);
                    f.add("decision_frontier_queried",j->frontier_queried);f.add("decision_frontier_known",j->frontier.has_value());if(j->frontier)f.add("decision_frontier",*j->frontier);}
            });
        }
    }catch(...){++c.errors;}
    const auto end=std::chrono::steady_clock::now();const bool cpu_end_valid=thread_cpu(cpu_end);
    const auto wall=std::chrono::duration<long double>(end-begin).count();const bool cpu_valid=cpu_begin_valid&&cpu_end_valid&&cpu_end>=cpu_begin;
    // Synchronous summary is inside the existing trace Scope, before footer/seal.
    emit(c,"owner_census_summary",[&](auto& f){f.add("schema",2);f.add("rows_before_summary",c.rows-1);f.add("owners",c.owners);
        f.add("owner_total",bank.receivers.size());f.add("pattern_states",c.states);f.add("tracks",c.tracks);f.add("track_total",c.track_total);
        f.add("limited",c.limited);f.add("probe_errors",c.errors);f.add("fallback_unavailable",c.fallback);f.add("journal_overflow",journal.total>owner_cap);
        f.add("cpu_valid",cpu_valid);if(cpu_valid)f.add("thread_cpu_seconds",cpu_end-cpu_begin);f.add("wall_seconds",wall);f.add("explicit_clock_read_attempts",4);
        f.add("clock_scope","synchronous census emission and owner queries; excludes journal maintenance, marker/filter checks and summary");
        f.add("comparison_scope","single natural-default-origin capture; no matched overhead or fixed-work causal comparison");
    });
}
} // namespace datapump::diagnostic::owner_census
