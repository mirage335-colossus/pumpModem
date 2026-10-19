#pragma once
// Private aggregate instrumentation for one natural-origin Live diagnostic.
#include "audio_trace.hpp"
#include "owner_census_probe.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <time.h>

namespace datapump::diagnostic::capacity_shape {
inline bool enabled() noexcept {
    static const bool value=[] {const auto* p=std::getenv("DATAPUMP_CAPACITY_SHAPE_DIAGNOSTIC");
        return p&&std::strcmp(p,"1")==0;}();return value;
}
inline constexpr std::size_t event_cap=512, signature_cap=32;
struct Signature {
    std::uint32_t rate=0;double bandwidth=0,carrier=0;unsigned sf=0,dsss=0,strategy=0;
    bool keyed=false,clock_windowed=false,local_fallback=false,candidate_limited=false;
    friend bool operator==(const Signature&,const Signature&)=default;
};
struct State {
    std::mutex mutex;bool active=false,truncated=false;std::string label;
    std::uint64_t revision=0,peak_owner_sample=0,peak_byte_sample=0,last_sample=0,last_epoch=UINT64_MAX;
    std::size_t points=0,peak_owners=0,peak_bytes=0,last_owners=0,last_bytes=0,point_owners=0,point_bytes=0;
    std::uint64_t peak_admissions=0,peak_retirements=0,last_admissions=0,last_retirements=0,point_admissions=0,point_retirements=0;
    std::size_t last_stored=0,last_recomputed=0,last_receiver_sum=0,last_modem_working=0,last_modem_reserved=0,last_base=0;
    std::size_t last_shared_capture=0,last_deferred=0,last_source=0,last_exact_samples=0,last_exact_points=0;
    std::size_t last_permutations=0,last_temporal=0;
    std::size_t journal_total=0,journal_rows=0,synchronized=0,initial_incomplete=0,retired=0,other_reasons=0;bool journal_complete=false;
    std::array<Signature,signature_cap> signatures{};std::size_t signature_count=0;bool signatures_truncated=false;
    long double wall_seconds=0,cpu_seconds=0;bool cpu_valid=true;
};
inline State state;
inline int cpu_clock(long double& value) noexcept {
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec ts{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&ts))return 0;
    value=static_cast<long double>(ts.tv_sec)+static_cast<long double>(ts.tv_nsec)/1000000000.L;return 1;
#else
    (void)value;return 0;
#endif
}
inline void begin_case(const char* label) noexcept {
    if(!enabled())return;std::lock_guard lock(state.mutex);
    state.active=false;state.truncated=false;state.label.clear();state.revision=0;
    state.peak_owner_sample=state.peak_byte_sample=state.last_sample=0;state.last_epoch=UINT64_MAX;
    state.points=state.peak_owners=state.peak_bytes=state.last_owners=state.last_bytes=state.point_owners=state.point_bytes=0;
    state.peak_admissions=state.peak_retirements=state.last_admissions=state.last_retirements=state.point_admissions=state.point_retirements=0;
    state.last_stored=state.last_recomputed=state.last_receiver_sum=state.last_modem_working=state.last_modem_reserved=state.last_base=0;
    state.last_shared_capture=state.last_deferred=state.last_source=state.last_exact_samples=state.last_exact_points=0;
    state.last_permutations=state.last_temporal=state.journal_total=state.journal_rows=0;
    state.synchronized=state.initial_incomplete=state.retired=state.other_reasons=0;state.journal_complete=false;
    state.signature_count=0;state.signatures_truncated=false;state.wall_seconds=state.cpu_seconds=0;state.cpu_valid=true;
    state.active=true;state.label=label?label:"unknown";
}
template<class Bank,class Value,class Workspace>void observe(const Bank& bank,const Value& value,Workspace workspace) noexcept {
    if(!enabled())return;long double cpu0=0,cpu1=0;const bool c0=cpu_clock(cpu0);
    const auto wall0=std::chrono::steady_clock::now();
    try {
        std::lock_guard lock(state.mutex);if(!state.active)return;
        std::size_t receiver_sum=0,modem_working=0,modem_reserved=0,non_windowed=0,windowed=0,fallback=0,candidate_limited=0;
        for(const auto& r:bank.receivers) {
            receiver_sum+=workspace(r);modem_working+=r.modem->working_bytes();modem_reserved+=r.modem->reserved_workspace_bytes();non_windowed+=!r.modem->clock_windowed();windowed+=r.modem->clock_windowed();
            fallback+=r.modem->local_clock_fallback();candidate_limited+=r.modem->candidate_limited();
            const auto& c=r.options.modem;Signature sig{c.sample_rate,c.bandwidth_hz,c.carrier_hz,c.spreading_factor,
                c.dsss_factor,static_cast<unsigned>(c.receiver_strategy),r.options.key.has_value(),
                r.modem->clock_windowed(),r.modem->local_clock_fallback(),r.modem->candidate_limited()};
            bool found=false;for(std::size_t i=0;i<state.signature_count;++i)if(state.signatures[i]==sig){found=true;break;}
            if(!found) {if(state.signature_count<signature_cap)state.signatures[state.signature_count++]=sig;else state.signatures_truncated=true;}
        }
        const auto shared_capture=bank.shared_capture?bank.shared_capture->capacity_reservation():0;
        const auto deferred=bank.deferred_input.working_bytes();
        const auto source=bank.source_curves?bank.source_curves->working_bytes():0;
        const auto exact_samples=bank.exact_source_samples?bank.exact_source_samples->working_bytes():0;
        const auto exact_points=bank.exact_source_points?bank.exact_source_points->working_bytes():0;
        const auto permutations=bank.permutations?bank.permutations->working_bytes():0;
        const auto temporal=bank.temporal_plans?bank.temporal_plans->working_bytes():0;
        const auto base=sizeof(bank.receptions)+sizeof(bank.source_curves)+sizeof(bank.exact_source_samples)+
            sizeof(bank.exact_source_generation)+sizeof(bank.next_exact_source_trace)+sizeof(bank.exact_source_points)+
            sizeof(bank.point_source_generation)+sizeof(bank.next_point_consumer)+sizeof(bank.next_point_source_trace)+
            sizeof(bank.permutations)+sizeof(bank.temporal_plans)+sizeof(bank.deferred_input)+sizeof(bank.input_queued_at)+
            sizeof(bank.shared_capture)+sizeof(bank.future_capture);
        const auto recomputed=base+shared_capture+deferred+source+exact_samples+exact_points+permutations+temporal+receiver_sum;
        const auto resident=bank.receivers.size();const auto bytes=bank.working_bytes;
        const auto epoch=bank.observed_epoch?static_cast<std::uint64_t>(*bank.observed_epoch):0;
        const bool journal_complete=owner_census::journal.valid&&owner_census::journal.total==resident&&
            owner_census::journal.count==owner_census::journal.total;
        std::size_t sync=0,incomplete=0,retired_now=0,other=0;
        if(owner_census::journal.valid)for(std::size_t i=0;i<owner_census::journal.count;++i) {
            const auto& row=owner_census::journal.rows[i];
            if(std::strcmp(row.reason,"synchronized")==0)++sync;
            else if(std::strcmp(row.reason,"initial_search_incomplete")==0)++incomplete;
            else if(row.retire)++retired_now;
            else ++other;
        }
        state.revision=bank.revision;state.last_sample=bank.samples;state.last_owners=resident;state.last_bytes=bytes;
        state.last_admissions=bank.admissions;state.last_retirements=bank.retirements;
        state.last_stored=bytes;state.last_recomputed=recomputed;state.last_receiver_sum=receiver_sum;state.last_modem_working=modem_working;state.last_modem_reserved=modem_reserved;state.last_base=base;
        state.last_shared_capture=shared_capture;state.last_deferred=deferred;state.last_source=source;
        state.last_exact_samples=exact_samples;state.last_exact_points=exact_points;state.last_permutations=permutations;state.last_temporal=temporal;
        state.journal_total=owner_census::journal.total;state.journal_rows=owner_census::journal.count;
        state.synchronized=sync;state.initial_incomplete=incomplete;state.retired=retired_now;state.other_reasons=other;state.journal_complete=journal_complete;
        if(resident>state.peak_owners){state.peak_owners=resident;state.peak_owner_sample=bank.samples;}
        if(bytes>state.peak_bytes){state.peak_bytes=bytes;state.peak_byte_sample=bank.samples;}
        state.peak_admissions=std::max(state.peak_admissions,bank.admissions);state.peak_retirements=std::max(state.peak_retirements,bank.retirements);
        const auto sec=epoch;const bool meaningful=state.points==0 || sec!=state.last_epoch ||
            resident>=state.point_owners+8 || bytes>=state.point_bytes+1024*1024 ||
            bank.admissions!=state.point_admissions || bank.retirements!=state.point_retirements;
        if(meaningful) {
            if(state.points<event_cap) {
                ++state.points;state.last_epoch=sec;state.point_owners=resident;state.point_bytes=bytes;
                state.point_admissions=bank.admissions;state.point_retirements=bank.retirements;
                audio::trace::record("capacity_shape_point",[&](auto& f){
                    f.add("schema",1);f.add("case",state.label);f.add("revision",bank.revision);f.add("sample",bank.samples);
                    f.add("observed_sample",bank.observed_sample);f.add("epoch",epoch);f.add("resident",resident);
                    f.add("admissions",bank.admissions);f.add("retirements",bank.retirements);
                    f.add("sampled_peak_resident",state.peak_owners);f.add("joined_bank_peak_resident",bank.peak_receivers);
                    f.add("native_and_fallback_config_signatures",state.signature_count);
                    f.add("stored_bytes",bytes);f.add("recomputed_bytes",recomputed);f.add("ledger_delta",static_cast<std::int64_t>(bytes)-static_cast<std::int64_t>(recomputed));
                    f.add("receiver_sum",receiver_sum);f.add("modem_working_sum",modem_working);f.add("modem_reserved_sum",modem_reserved);f.add("base_bytes",base);f.add("shared_capture_reservation",shared_capture);
                    f.add("deferred_input",deferred);f.add("source_curves",source);f.add("exact_source_samples",exact_samples);
                    f.add("exact_source_points",exact_points);f.add("permutations",permutations);f.add("temporal_plans",temporal);
                    f.add("journal_valid",owner_census::journal.valid);f.add("journal_complete",journal_complete);
                    f.add("journal_total",owner_census::journal.total);f.add("journal_rows",owner_census::journal.count);
                    f.add("synchronized_at_predicate",sync);f.add("initial_search_incomplete_at_predicate",incomplete);
                    f.add("retired_at_predicate",retired_now);f.add("other_retention_reasons",other);
                    f.add("non_windowed_clock_mode_count",non_windowed);f.add("clock_windowed_mode_count",windowed);f.add("backend_identity_source","selected backend is preserved in receiver_selection events; clock_windowed count is not selected-backend enum");
                    f.add("local_fallback_count",fallback);f.add("candidate_limited_count",candidate_limited);
                    f.add("signature_count",state.signature_count);f.add("signature_truncated",state.signatures_truncated);
                    f.add("event_cap",event_cap);f.add("events_truncated",state.truncated);
                });
            } else state.truncated=true;
        }
        (void)value;
    } catch(...) {std::lock_guard lock(state.mutex);state.truncated=true;}
    const auto wall1=std::chrono::steady_clock::now();if(c0&&cpu_clock(cpu1)&&cpu1>=cpu0) {
        std::lock_guard lock(state.mutex);state.cpu_seconds+=cpu1-cpu0;
    } else {std::lock_guard lock(state.mutex);state.cpu_valid=false;}
    {std::lock_guard lock(state.mutex);state.wall_seconds+=std::chrono::duration<long double>(wall1-wall0).count();}
}
template<class Config,class ChannelConfig,class Channel>void channel_origin(const char* label,const Config& modem,const ChannelConfig& cfg,const Channel& channel,
        std::uint64_t received_origin,std::uint64_t bank_origin,std::uint64_t revision,double bank_epoch) noexcept {
    if(!enabled())return;
    std::string case_label;
    {std::lock_guard lock(state.mutex);case_label=state.active?state.label:(label?label:"unknown");}
    audio::trace::record("capacity_shape_channel_origin",[&](auto& f){
        f.add("schema",1);f.add("case",case_label);f.add("received_sample_origin",received_origin);
        f.add("bank_sample_origin",bank_origin);f.add("revision",revision);f.add("bank_epoch",bank_epoch);
        f.add("channel_seed",cfg.seed);f.add("snr_db",cfg.snr_db);f.add("frequency_offset_hz",cfg.frequency_offset_hz);
        f.add("delay_samples",cfg.delay_samples);f.add("clock_error_ppm",cfg.clock_error_ppm);
        f.add("phase_noise_degrees_per_sqrt_second",cfg.phase_noise_degrees_per_sqrt_second);
        f.add("modem_sample_rate",modem.sample_rate);f.add("modem_bandwidth_hz",modem.bandwidth_hz);
        f.add("modem_carrier_hz",modem.carrier_hz);f.add("modem_spreading_factor",modem.spreading_factor);
        f.add("startup_offset_samples",channel.startup_offset_samples());f.add("carrier_phase_radians",channel.carrier_phase_radians());
    });
}
template<class Snapshot>void transmission(const char* label,const Snapshot& s,std::uint64_t source_origin,
        std::uint64_t bank_origin,std::uint64_t revision,double bank_epoch,double tx_epoch) noexcept {
    if(!enabled())return;
    audio::trace::record("capacity_shape_transmission",[&](auto& f){
        f.add("schema",1);f.add("case",label?label:"unknown");f.add("bank_source_origin",source_origin);
        f.add("bank_sample_origin",bank_origin);f.add("bank_revision",revision);f.add("bank_epoch",bank_epoch);
        f.add("tx_epoch",tx_epoch);f.add("source_transmission_fraction",s.transmission_fraction);
        f.add("source_transmission_seconds",s.transmission_seconds);
        f.add("generated_bits",s.transmit_trace.generated_bits);f.add("total_wire_bits",s.transmit_trace.total_wire_bits);
        f.add("receiver_samples_at_tx_end",s.samples_received);f.add("simulation_receiving_tail",s.simulation_receiving_tail);
        f.add("generated_progress_is_not_transmitted_sample_progress",true);
        f.add("source_endpoint_is_transmitted_samples_not_generated_bits",true);
    });
}
template<class Snapshot>void end_case(const char* label,const Snapshot& s) noexcept {
    if(!enabled())return;try {std::lock_guard lock(state.mutex);if(!state.active)return;state.active=false;
        audio::trace::record("capacity_shape_summary",[&](auto& f){
            f.add("schema",1);f.add("case",label?label:state.label);f.add("complete",!state.truncated&&state.points<=event_cap);
            f.add("points",state.points);f.add("event_cap",event_cap);f.add("events_truncated",state.truncated);
            f.add("revision",state.revision);f.add("last_sample",state.last_sample);f.add("peak_owner_sample",state.peak_owner_sample);f.add("peak_byte_sample",state.peak_byte_sample);
            f.add("sampled_peak_owners",state.peak_owners);f.add("end_owners",state.last_owners);
            f.add("joined_end_peak_owners",s.peak_receiver_instances);f.add("sampled_peak_stored_bytes",state.peak_bytes);
            f.add("end_stored_bytes",state.last_stored);f.add("end_recomputed_bytes",state.last_recomputed);
            f.add("end_ledger_delta",static_cast<std::int64_t>(state.last_stored)-static_cast<std::int64_t>(state.last_recomputed));
            f.add("end_receiver_sum",state.last_receiver_sum);f.add("end_modem_working_sum",state.last_modem_working);f.add("end_modem_reserved_sum",state.last_modem_reserved);f.add("end_base_bytes",state.last_base);
            f.add("end_shared_capture_reservation",state.last_shared_capture);f.add("end_deferred_input",state.last_deferred);
            f.add("end_source_curves",state.last_source);f.add("end_exact_source_samples",state.last_exact_samples);
            f.add("end_exact_source_points",state.last_exact_points);f.add("end_permutations",state.last_permutations);f.add("end_temporal_plans",state.last_temporal);
            f.add("end_admissions",state.last_admissions);f.add("end_retirements",state.last_retirements);
            f.add("end_public_receivers",s.receiver_instances);f.add("end_public_peak_receivers",s.peak_receiver_instances);
            f.add("end_public_admissions",s.receiver_admissions);f.add("end_public_retirements",s.receiver_retirements);
            f.add("end_public_dsp_bytes",s.dsp_buffered_bytes);f.add("owner_local_bit_limits_available",false);f.add("end_search_limited",s.receiver_health.search_limited);
            f.add("last_journal_total",state.journal_total);f.add("last_journal_rows",state.journal_rows);f.add("last_journal_complete",state.journal_complete);
            f.add("last_synchronized_at_predicate",state.synchronized);f.add("last_initial_search_incomplete_at_predicate",state.initial_incomplete);
            f.add("last_retired_at_predicate",state.retired);f.add("last_other_reasons",state.other_reasons);
            f.add("signature_count",state.signature_count);f.add("signature_truncated",state.signatures_truncated);
            f.add("native_clock_mode_signatures",state.signature_count);f.add("instrumentation_wall_seconds",static_cast<double>(state.wall_seconds));
            f.add("instrumentation_thread_cpu_valid",state.cpu_valid);f.add("instrumentation_thread_cpu_seconds",static_cast<double>(state.cpu_seconds));
            f.add("instrumentation_scope","capacity_shape observer wall and calling-thread CPU only; excludes trace writer, census hook and whole fixture");
            f.add("overhead_comparison","none; natural origin and 512MiB diagnostic budget change backend/cache/bit grants");
        });
    }catch(...) {}}
} // namespace datapump::diagnostic::capacity_shape
