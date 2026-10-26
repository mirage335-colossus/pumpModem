#include "scheduled_acquisition_policy.hpp"
#include "scheduled_work_profile.hpp"
#include "shared_scheduled_capture.hpp"
#include "datapump/live.hpp"
#include "transfer_pending.hpp"
#include "live_receiver_layout.hpp"
#include "live_mutex_probe.hpp"
#include "live_snapshot_probe.hpp"
#include "live_snapshot_copy.hpp"
#include "live_capture_monitor_probe.hpp"
#include "receiver_storage_allocator.hpp"
#include "native_peak_workspace.hpp"
#include "native_point_workspace.hpp"
#include "native_diagnostic_history.hpp"
#include "live_receiver_admission.hpp"
#include "live_helper.hpp"
#include "live_helper_prior.hpp"
#include "datapump/helper_assistance.hpp"
#include "datapump/helper_filter.hpp"
#include "live_signal_queue.hpp"
#include "datapump/fhss_schedule.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/execution.hpp"
#include "datapump/audio.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_receiver.hpp"
#include "datapump/runtime.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/scheduled_complex_search.hpp"
#include "datapump/receiver_strategy.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/symbol_schedule.hpp"
#include "signal_view.hpp"
#include "live_pattern_scores.hpp"
#include "live_receptions.hpp"
#include "live_replay_staging.hpp"
#include "live_scheduling.hpp"
#include "live_simulation_clock.hpp"
#include "live_deferred_input.hpp"
#include "live_deferred_pattern_history.hpp"
#include "sched_capture.hpp"
#include "transmit_timing.hpp"
#include "transmit_epoch_guard.hpp"
#include "pattern_projection_cache.hpp"
#include "scheduled_exact_source_samples.hpp"
#include "scheduled_exact_source_points.hpp"
#include <charconv>
#include "audio_trace.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <list>
#include <iterator>
#include <iomanip>
#include <locale>
#include <cstdlib>
#include <string_view>
#include <limits>
#include <map>
#include <mutex>
#include <numbers>
#include <optional>
#include <random>
#include <sstream>
#include <thread>
#include <utility>
#include <variant>

namespace datapump::live {
namespace rx_cpu = ::datapump::detail::cpu_profile;
namespace {
using Clock = std::chrono::steady_clock;
#if !defined(DATAPUMP_LIVE_MUTEX_PROBE) || defined(DATAPUMP_EXECUTION_FIBERS)
using LiveMutex = execution::Mutex;
#define DATAPUMP_MUTEX_CONTEXT(name, stage)
#else
#define DATAPUMP_MUTEX_CONTEXT(name, stage) mutex_probe::Context name(mutex_probe::Stage::stage)
// Diagnostic observer only. The underlying mutex and lock ordering are unchanged.
// The fixture owns the observer until every Session worker has joined.
class LiveMutex {
    execution::Mutex mutex_;
    mutex_probe::State* observer_ = nullptr; // Accessed only while mutex_ is owned.
    mutex_probe::Record record_{};
    void acquired(mutex_probe::State* observer,mutex_probe::Stamp requested,std::uintptr_t caller) noexcept {
        observer_=observer;
        if(!observer)return;
        record_={mutex_probe::current_stage(),reinterpret_cast<std::uintptr_t>(this),
            mutex_probe::thread_id(),caller,requested,mutex_probe::sample(),{}};
    }
public:
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    void lock() {
        auto* observer=mutex_probe::observer();
        const auto requested=observer?mutex_probe::sample():mutex_probe::Stamp{};
        std::uintptr_t caller=0;
#if defined(__GNUC__) || defined(__clang__)
        if(observer)caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
#endif
        mutex_.lock();acquired(observer,requested,caller);
    }
    bool try_lock() {
        auto* observer=mutex_probe::observer();
        const auto requested=observer?mutex_probe::sample():mutex_probe::Stamp{};
        if(!mutex_.try_lock())return false;
        acquired(observer,requested,0);return true;
    }
    void unlock() {
        auto* observer=observer_;
        if(!observer){mutex_.unlock();return;}
        auto record=record_;record.released=mutex_probe::sample();observer_=nullptr;
        mutex_.unlock();
        observer->add(record); // Bounded publication happens after releasing Live.
    }
};
#endif
using detail::plot_size;
using detail::maximum_events;
using detail::replay_frames;
using detail::replay_wave_samples;
using detail::replay_bins;
using detail::minimum_constellation_limit;
using detail::pattern_score_limit;
using detail::replay_text_limit;
using detail::ReplayFrame;
using detail::replay_frame_base;
using detail::replay_result_workspace;
using detail::constellation_limit;
using detail::replay_workspace;
using detail::plot_workspace;
using detail::audio_reserve;
using detail::bank_capacity;
using detail::source_budget;
using detail::channel_config;
// Optional monitor remains within the existing audio reservation.
constexpr std::size_t monitor_workspace = 96 * 1024;
constexpr std::size_t maximum_recovery_jobs = 8;
constexpr std::size_t recovery_workspace_limit = 16 * 1024 * 1024;
constexpr auto replay_duration = std::chrono::seconds(3);
constexpr std::size_t reception_alias_bytes = detail::ReceptionHistory::capacity * sizeof(std::uint64_t);
double epoch_now() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string reception_id(const Message& message) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : message.local_id) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
std::string display_text(const Message& message) {
    if (message.kind != MessageKind::text)
        return terminal_text(Bytes(message.filename.begin(), message.filename.end())) +
               " (" + std::to_string(message.data.size()) + " bytes)";
    const auto size = std::min<std::size_t>(message.data.size(), 4096);
    return terminal_text(std::span<const std::uint8_t>(message.data).first(size));
}
Settings normalized(Settings value) {
    // Capability remains provider-selected and is checked before any mutation,
    // exactly as before. The pure layout routine shares all remaining checks.
    audio::validate_options({value.transmit_gain,value.exclusive});
    return detail::normalized_layout(std::move(value));
}

using Plots = detail::SignalPlots;
using detail::signal_plots;
void count_dropped(std::uint64_t& total, std::uint64_t count) {
    total += std::min(count, std::numeric_limits<std::uint64_t>::max() - total);
}
void append_points(modem::ConstellationBatch& target, modem::ConstellationBatch batch,
                   std::size_t limit) {
    count_dropped(target.dropped, batch.dropped);
    if (batch.points.size() > limit) {
        count_dropped(target.dropped, batch.points.size() - limit);
        batch.points.erase(batch.points.begin(), batch.points.end() - static_cast<std::ptrdiff_t>(limit));
    }
    const auto excess = target.points.size() + batch.points.size() > limit ?
        target.points.size() + batch.points.size() - limit : 0;
    count_dropped(target.dropped, excess);
    target.points.erase(target.points.begin(), target.points.begin() + static_cast<std::ptrdiff_t>(excess));
    target.points.reserve(limit);
    target.points.insert(target.points.end(), batch.points.begin(), batch.points.end());
}
}

struct Session::Impl {
    struct Prepared {
        modem::Config modem;
        std::unique_ptr<modem::StreamingTransmitter> transmitter;
        std::function<void(Prepared&)> prepare_hardware;
        std::function<void(Prepared&,double)> prepare_simulation;
        std::uint64_t simulation_leading_samples=0;
        std::uint64_t hardware_helper_leading_samples=0,hardware_helper_trailing_samples=0;
        bool hardware_helper_enabled=false;
        double timing_stop_seconds=0,timing_rate_fraction=0;
        audio::ChannelMode output_channels=audio::ChannelMode::left_mono;
        double output_gain=1;
        double helper_bandwidth=0;
        std::optional<std::uint64_t> protected_epoch;
        Bytes protected_key;
        bool hardware = false;
        bool binary = false;
        bool noise = false;
        bool helper_only = false;
        bool graceful = false;
        std::uint64_t generation = 0, serial = 0;
        std::uint64_t transmitted_samples = 0;
        std::uint64_t tail_remaining = 0;
        bool tail_started = false;
        bool simulation_envelope_finished = false;
        bool simulation_helper_enabled = false;
        bool simulation_channel_pending=true;
        std::vector<ReplayFrame> replay;
        std::shared_ptr<detail::ReplayStaging> receives;
        // Stamped when a physically completed result is staged, so clearing
        // history can invalidate a source-thread result before replay handoff.
        std::uint64_t recovery_clear_generation=0;
        std::uint64_t receive_clear_generation=0;
        std::size_t replay_count = 0, point_limit = 0;
        std::vector<std::complex<double>> pattern_scores;
        std::vector<PatternScoreObservation> pattern_score_observations;
        std::uint64_t pattern_score_observation_id = 0;
        std::stop_token stop;
    };
    struct AudioBlock {
        std::vector<float> samples;
        std::uint64_t revision;
        double observed_epoch;
        Clock::time_point queued_at;
        std::optional<audio::TimePrediction> timing;
        std::uint64_t sample_origin=0, skipped_samples=0;
        std::unique_ptr<helper::Reference> helper;
    };
    struct CaptureQuietProof {
        std::uint64_t revision=0, begin=0, end=0;
        double utc_second=0;
        std::uint64_t generation=0,burst_fence=0;
        std::optional<detail::BurstCaptureTimestamp> burst_timestamp;
    };
    struct CaptureFutureProof {
        detail::SchedCaptureIdentity identity;
        std::uint64_t begin=0,end=0,admissions=0;
        long double utc_seconds=0;
        std::uint32_t rate=0;
        std::vector<std::unique_ptr<modem::SchedHopSourceMap>> calendars;
        std::vector<detail::SchedCaptureBound> bounds;
        bool timestamp_valid(std::uint64_t first,std::uint64_t count,double observed_epoch,
                             long double seconds_per_frame) const {
            // Interior decoder admissions follow the actual callback slope.
            // Both endpoints must remain in the original affine UTC cone;
            // a valid first timestamp alone cannot certify a whole callback.
            return detail::checkedCaptureTimestamp(begin,first,count,rate,utc_seconds,
                                                   observed_epoch,seconds_per_frame);
        }
        bool excludes(std::uint64_t first,std::uint64_t count,double observed_epoch,
                      long double seconds_per_frame,detail::SchedCaptureIdentity current) const {
            if(first<begin || first>end || count>end-first ||
               !timestamp_valid(first,count,observed_epoch,seconds_per_frame))return false;
            detail::SchedCaptureCertificate proof{identity,begin,end,bounds,true,true,true,true};
            return proof.excludes(first,count,current);
        }
    };
    struct RecoveryTask {
        transfer::Received result;
        std::shared_ptr<transfer::RecoveryJob> job;
        SignalUpdate event;
        std::stop_source stop;
        std::uint64_t generation=0;
        std::size_t reserved_bytes=0, retained_bytes=0;
        bool started=false, obsolete=false;
    };
    using Receiver = detail::ReceiverState;
    using ReceiverList = std::list<Receiver,detail::ReceiverStorageAllocator<Receiver>>;
    struct Bank {
        struct HelperHistory {
            std::vector<float> pcm;
            std::uint64_t end=0;
            std::size_t count=0,next=0;
            std::size_t bytes()const noexcept{return sizeof(HelperHistory)+pcm.capacity()*sizeof(float);}
            std::uint64_t first()const noexcept{return end-count;}
            template<class Visit> void visit(Visit&& consume)const {
                auto at=(next+pcm.size()-count)%pcm.size(),remaining=count;
                while(remaining) {
                    const auto n=std::min({remaining,pcm.size()-at,std::size_t{4096}});
                    consume(std::span<const float>(pcm).subspan(at,n));
                    remaining-=n;at=(at+n)%pcm.size();
                }
            }
            void append(std::span<const float> input,std::uint64_t frontier) {
                if(frontier<input.size() || frontier-input.size()!=end) {count=next=0;}
                for(auto sample:input) {pcm[next]=sample;next=(next+1)%pcm.size();count=std::min(count+1,pcm.size());}
                end=frontier;
            }
        };
        struct HelperAssistance {
            Receiver* receiver=nullptr;
            helper::Reference reference;
            helper::PayloadFilter filter;
            std::uint64_t filter_input_origin=0,publication_floor=0;
            std::uint64_t anchor_sample=0;
            std::size_t numeric_limit=0,ordinary_numeric_loan=0,preroll_count=0;
            double anchor_phase=0,anchor_frequency=0,unwrapped_phase=0;
            detail::HelperPhaseContinuation phase_continuation;
            std::array<modem::PatternPhaseKnot,2> phase_knots{};
            bool phase_ready=false,phase_qualified=false,failed=false,tail_only=false,payload_retained=false;
            bool bounded_initial_search=false,phase_applied=false,acquisition_frozen=false;
            std::size_t bytes()const noexcept{return sizeof(HelperAssistance)+filter.allocated_bytes();}
        };
        struct HelperConditioning {
            // A failed conditional search restores this same original bank,
            // then replays every dated PCM sample captured while it was idle.
            std::unique_ptr<Bank> original;
            std::unique_ptr<HelperHistory> fallback;
            helper::PayloadFilter filter;
            helper::Reference reference;
            std::vector<float> preroll;
            std::vector<std::pair<std::uint64_t,std::uint64_t>> initial_epochs;
            struct ReplayBudget {const Receiver* receiver=nullptr;std::size_t numeric=0,total=0,cold_constructor=0;};
            std::vector<ReplayBudget> replay_budgets;
            std::size_t native_pool_bound=0;
            std::uint64_t filter_output_origin=0,publication_floor=0,attempt_end=0;
            std::size_t original_bytes=0;
            double admitted_frequency=0,admitted_radius=0;
            bool committed=false,broad_future=false;
            std::size_t bytes()const noexcept {
                return sizeof(HelperConditioning)+filter.allocated_bytes()+preroll.capacity()*sizeof(float)+
                    initial_epochs.capacity()*sizeof(decltype(initial_epochs)::value_type)+
                    replay_budgets.capacity()*sizeof(ReplayBudget)+(fallback?fallback->bytes():0)+(original?original_bytes:0);
            }
        };
        struct HelperCohort {
            // One stable front end and one native executor per ordinary row.
            // The FIR basis is fixed before local sample zero and never changes
            // when the optional reference becomes unavailable.
            helper::PayloadFilter filter;
            struct AuditGroup {
                std::uint64_t family=0,symbol_samples=0,due=0;
                bool audit_now=false;
            };
            std::array<AuditGroup,32> groups{};
            std::size_t group_count=0;
            std::uint64_t filter_origin=0,lease_until=0,probation_until=0,quarantine_until=0;
            std::uint64_t conditional_jobs=0,audit_jobs=0;
            struct PhaseControl {
                modem::PatternPhaseCurve curve;
                std::uint64_t jobs=0,corrected_bins=0;
                double anchor=0;
            };
            static_assert(sizeof(PhaseControl)<=664);
            std::unique_ptr<PhaseControl> phase;
            std::uint64_t original_pairs=0,selected_pairs=0;
            double expected_hz=0,last_frequency_hz=0,last_radius_hz=0;
            std::optional<helper::Reference> reference;
            const char* reason="waiting for a measured helper frequency reference";
            const char* phase_reason="helper phase and punctuation timing are not independently qualified";
            bool qualified=false,active=false,last_frequency_valid=false,lease_compatible=true;
            std::size_t bytes()const noexcept{return sizeof(HelperCohort)+filter.allocated_bytes()+
                (phase?sizeof(PhaseControl):0);}
        };
        std::unique_ptr<HelperCohort> helper_cohort;
        const char* helper_cohort_decline_reason="helper rolling acquisition has not been admitted";
        std::unique_ptr<HelperConditioning> helper_conditioning;
        bool helper_conditioning_attempted=false,helper_gap_protection_qualified=false;
        std::uint64_t helper_conditioning_probe=0,helper_last_frequency_sample=0;
        std::size_t helper_fallback_replay_bytes=0;
        const HelperConditioning* helper_fallback_budget=nullptr; // Borrowed only by scoped original replay.
        // Only enabled assistance allocates this control. The ordinary receiver
        // type/layout and its container grants remain unchanged.
        std::unique_ptr<HelperAssistance> helper_assistance;
        std::unique_ptr<HelperHistory> helper_history;
        std::uint64_t helper_input_origin_samples=0; // Simulation source-to-bank map only.
        // Advisory first tone-support date, not a clock or authenticated epoch.
        double helper_evidence_epoch=std::numeric_limits<double>::quiet_NaN();
        std::shared_ptr<transfer::ReceiveStorageQuota> source_quota;
        // Pools precede handles for destruction. Whole-object move assignment
        // destroys destination handles before replacing either stable heap pool.
        std::shared_ptr<detail::ReceiverAllocationAccount> receiver_allocations=
            std::make_shared<detail::ReceiverAllocationAccount>(0);
        std::unique_ptr<modem::detail::NativePeakWorkspace> native_peaks=
            std::make_unique<modem::detail::NativePeakWorkspace>();
        std::unique_ptr<modem::detail::NativePointPool> native_points;
        // A Bank move transfers this handle, never a container that may need
        // a replacement sentinel. Construct it only after setting its quota.
        std::unique_ptr<ReceiverList> receiver_storage;
        ReceiverList& receivers()noexcept{return *receiver_storage;}
        const ReceiverList& receivers()const noexcept{return *receiver_storage;}
        std::size_t receiver_storage_bytes()const noexcept {
            return sizeof(receiver_storage)+(receiver_storage?sizeof(ReceiverList):0);
        }
        Bank()=default;
        Bank(const Bank&)=delete;
        Bank& operator=(const Bank&)=delete;
        Bank(Bank&&)noexcept=default;
        Bank& operator=(Bank&& other)noexcept {
            if(this!=&other){this->~Bank();std::construct_at(this,std::move(other));}
            return *this;
        }
        detail::ReceptionHistory receptions;
        std::unique_ptr<modem::detail::SharedSourceCurves<>> source_curves;
        std::unique_ptr<modem::detail::scheduled_source_samples::Pool> exact_source_samples;
        std::uint64_t exact_source_generation=0;
        std::unique_ptr<modem::detail::scheduled_source_points::PointPool> exact_source_points;
        std::uint64_t point_source_generation=0,next_point_consumer=1;
        Clock::time_point next_point_source_trace{};
        Clock::time_point next_exact_source_trace{};
        std::unique_ptr<modem::detail::PatternPermutationPools> permutations;
        std::unique_ptr<modem::detail::SharedTemporalPlans> temporal_plans;
        using SharedCapture=modem::detail::scheduled_capture::SharedScheduledCapture;
        std::shared_ptr<SharedCapture> shared_capture;
        std::optional<SharedCapture::Retention> future_capture;
        detail::DeferredInputFences deferred_input;
        std::optional<Clock::time_point> input_queued_at;
        Clock::time_point next_deferred_history_query{};
        std::size_t deferred_history_cursor=0;
        bool retirement_pending=false, reception_history_dirty=false;
        std::uint64_t samples = 0, next_candidate = 1,revision=0;
        std::uint64_t admissions = 0, retirements = 0, builds = 0;
        std::size_t peak_receivers = 0;
        std::size_t working_bytes = sizeof(detail::ReceptionHistory)+sizeof(source_curves)+sizeof(exact_source_samples)+sizeof(exact_source_generation)+sizeof(next_exact_source_trace)+sizeof(exact_source_points)+sizeof(point_source_generation)+sizeof(next_point_consumer)+sizeof(next_point_source_trace)+sizeof(permutations)+sizeof(temporal_plans)+sizeof(deferred_input)+sizeof(input_queued_at)+sizeof(shared_capture)+sizeof(future_capture)+sizeof(receiver_allocations)+sizeof(detail::ReceiverAllocationAccount)+sizeof(receiver_storage)+sizeof(native_peaks)+sizeof(modem::detail::NativePeakWorkspace)+sizeof(native_points)+sizeof(helper_conditioning)+sizeof(helper_conditioning_attempted)+sizeof(helper_conditioning_probe)+sizeof(helper_last_frequency_sample)+sizeof(helper_fallback_replay_bytes)+sizeof(helper_fallback_budget)+sizeof(helper_cohort)+sizeof(helper_cohort_decline_reason)+65536;
        double created_at = 0;
        std::optional<long double> public_amplitude_epoch_origin;
        std::uint64_t refreshed_sample=0;
        bool public_amplitude_windows=false;
        // All original acquisition promises survive this bank-wide barrier.
        // Fresh first-start cohorts remain suspended until active Burst
        // contexts end and previously certified sparse input has drained.
        bool burst_admission_frozen=false,burst_resume_pending=false;
        std::optional<std::uint64_t> burst_resume_sample;
        std::uint64_t burst_admission_fence=0;
        // UTC for a captured block, independent of when its search runs. Sample
        // coordinates remain continuous; fresh stamps only guide new admission.
        std::optional<double> observed_epoch;
        bool simulation_capture_dated=false;
        std::optional<audio::TimePrediction> timing;
        std::uint64_t observed_sample = 0;
        bool limited = false;
        bool coverage_limited = false;
        std::string oscillator_limit;
        std::string receiver_limit;
        std::complex<double> mixer{1,0};
    };
    static void trace_permutation_release(const Bank& bank,const char* reason) {
        if(!bank.permutations)return;
        audio::trace::record("receiver_permutation_release",[&](auto& f) {
            const auto all=bank.permutations->request_statistics();
            const auto rows=bank.permutations->statistics();
            f.add("sample",bank.samples);f.add("reason",reason);f.add("final_snapshot",true);
            f.add("requests",all.requests);f.add("hits",all.hits);
            f.add("builder_attempts",all.builder_attempts);f.add("builder_completed",all.builder_completed);
            f.add("child_fallbacks",all.child_fallbacks);f.add("row_misses",rows.misses);
            f.add("row_private_builds",rows.private_builds);f.add("publications",rows.publications);
            f.add("busy",rows.busy);f.add("evictions",rows.evictions);f.add("bypassed",rows.bypassed);
            f.add("reserved_bytes",bank.permutations->working_bytes());
            f.add("allocated_bytes",bank.permutations->allocated_bytes());
            f.add("effective_row_limit",bank.permutations->row_limit());
            const auto inner=bank.permutations->inner_coefficient_statistics();
            f.add("inner_bytes",bank.permutations->inner_coefficient_bytes());
            f.add("inner_requests",inner.requests);f.add("inner_hits",inner.hits);
            f.add("inner_builds",inner.builder_completed);f.add("inner_evictions",inner.evictions);
            f.add("inner_busy",inner.busy);f.add("inner_bypassed",inner.bypassed);
        });
    }
    static bool exact_source_pool_requested() {
        static const bool requested=[] {const auto* text=std::getenv("DATAPUMP_SCHED_EXACT_SOURCE_TILES");
            return text && std::string_view(text)=="1";}();return requested;
    }
    static bool exact_source_receiver(const Receiver& receiver) {
        const auto& c=receiver.options.modem;
        return receiver.options.key && receiver.modem->deferred_pattern_history() &&
            modem::scheduled_bursts(c) && !modem::packed_scheduled_symbols(c) && modem::pattern_pulse_enabled(c);
    }
    enum class PointMode {off,census,cache};
    static PointMode point_mode() {
        static const auto mode=[] {
            const auto flag=[](const char* name) {
                const auto* value=std::getenv(name);if(!value)return false;
                if(std::string_view(value)!="0" && std::string_view(value)!="1")throw Error(std::string(name)+" requires 0 or 1");
                return std::string_view(value)=="1";
            };
            const bool census=flag("DATAPUMP_SCHED_EXACT_SOURCE_POINT_CENSUS");
            const bool cache=flag("DATAPUMP_SCHED_EXACT_SOURCE_POINT_CACHE");
            if(census && cache)throw Error("source point census and cache are mutually exclusive");
            return cache?PointMode::cache:census?PointMode::census:PointMode::off;
        }();return mode;
    }
    static std::size_t point_rows(PointMode mode) {
        const auto parse=[](const char* name,std::size_t maximum) {
            std::size_t n=131072;const auto* value=std::getenv(name);
            if(value) {const std::string_view text(value);const auto result=std::from_chars(text.data(),text.data()+text.size(),n);
                if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size())throw Error(std::string("invalid ")+name);}
            if(n<4096 || n>maximum || (n&(n-1)))throw Error(std::string(name)+" requires a power of two in the supported range");
            return n;
        };
        // Only the selected mode's row setting is evaluated. Both remain fixed
        // for this process; this option cannot resize a borrowed arena.
        if(mode==PointMode::cache) {
            static const auto rows=parse("DATAPUMP_SCHED_EXACT_SOURCE_POINT_CACHE_ROWS",524288);return rows;
        }
        static const auto rows=parse("DATAPUMP_SCHED_EXACT_SOURCE_POINT_CENSUS_ROWS",1048576);return rows;
    }
    using PointSnapshot=modem::detail::scheduled_source_points::PointPool::Snapshot;
    static PointSnapshot point_snapshot(const Bank& bank) {
        return bank.exact_source_points?bank.exact_source_points->snapshot():PointSnapshot{};
    }
    static void trace_point_pool(const Bank& bank,const char* reason,bool final=false,
                                 std::optional<PointSnapshot> saved={}) {
        const auto snapshot=saved?*saved:point_snapshot(bank);
        if(!snapshot.point_rows && !snapshot.census_rows)return;
        audio::trace::record(snapshot.point_rows?"receiver_exact_source_point_cache":"receiver_exact_source_point_census",[&](auto& f) {
            const auto& c=snapshot.counts;
            f.add("revision",bank.revision);f.add("sample",bank.samples);f.add("generation",bank.point_source_generation);
            f.add("pool_identity",snapshot.pool);f.add("reason",reason);f.add("final_snapshot",final);
            f.add("bytes",snapshot.bytes);f.add("census_rows",snapshot.census_rows);f.add("point_rows",snapshot.point_rows);
            f.add("bound_families",snapshot.bound_families);f.add("bound_symbols",snapshot.bound_symbols);
            f.add("queries",c.queries);f.add("census_queries",c.census_queries);
            f.add("unique_lower_bound",c.census_unique_lower_bound);f.add("repeats",c.census_repeats);
            f.add("untracked",c.census_untracked);f.add("saturated",c.census_saturated);
            f.add("eligible_unique_exact",snapshot.eligible_unique_count_exact());f.add("all_pool_queries_unique_exact",snapshot.unique_count_exact());
            f.add("same_consumer_repeats",c.census_relations.same_consumer);f.add("cross_lane_repeats",c.census_relations.cross_lane);
            f.add("cross_bank_repeats",c.census_relations.cross_bank);f.add("unknown_repeats",c.census_relations.unknown);
            f.add("invalid_handles",c.invalid_handles);f.add("environment_bypasses",c.environment_bypasses);
            f.add("family_registry_full",c.family_registry_full);f.add("symbol_registry_full",c.symbol_registry_full);
            f.add("address_failures",c.address_failures);f.add("trace_bypasses",c.trace_bypasses);
            f.add("hits",c.hits);f.add("misses",c.misses);f.add("cache_build_calls",c.cache_build_calls);f.add("builds",c.builds);
            f.add("direct_calls",c.direct_calls);f.add("busy",c.busy);f.add("exhaustions",c.exhaustions);f.add("evictions",c.evictions);
            f.add("build_failures",c.build_failures);f.add("cancelled",c.cancelled);f.add("retired_builds",c.retired_builds);
            f.add("nonfinite_results",c.nonfinite_results);f.add("invalid_arguments",c.invalid_arguments);
            f.add("same_consumer_hits",c.cache_relations.same_consumer);f.add("cross_lane_hits",c.cache_relations.cross_lane);
            f.add("cross_bank_hits",c.cache_relations.cross_bank);f.add("unknown_hits",c.cache_relations.unknown);
            f.add("scope",snapshot.point_rows?
                "one cumulative exact point cache generation; scalar q requests only; copied hits avoid Original q calls; cache_build_calls reserves actual one-point builders; direct_calls use Original fallback; no unique-cardinality claim; do not sum successive snapshots":
                "one cumulative census generation; actual Original scheduled scalar replay qcalls only; no q replacement; unique counts become lower bounds after saturation; perpurpose invalid counters include absent/reclaimed pools; do not sum successive snapshots or generation unique counts");
        });
    }
    static std::size_t drop_point_pool(Bank& bank,const char* reason) {
        if(!bank.exact_source_points)return 0;
        const auto old=bank.exact_source_points->working_bytes();
        const auto borrowed=modem::detail::scheduled_source_samples::binding;
        if(borrowed.points==bank.exact_source_points.get()) {
            if(!bank.exact_source_points->enabled())return 0;
            const auto release=borrowed.reclaim==&ExactSourceLoan::reclaim_cohort?
                &ExactSourceLoan::reclaim_points:borrowed.reclaim;
            if(release && release(borrowed.pressure_context))return old-bank.exact_source_points->working_bytes();
            return 0;
        }
        const auto saved=point_snapshot(bank);
        if(!bank.exact_source_points->try_reclaim())return 0;
        trace_point_pool(bank,reason,true,saved);
        if(old>bank.working_bytes)throw Error("point pool charge exceeds receiver bank");
        bank.exact_source_points.reset();bank.working_bytes-=old;return old;
    }
    struct ExactSourceSnapshot {
        modem::detail::scheduled_source_samples::Counts counts;
        std::size_t bytes=0,rows=0;
    };
    static ExactSourceSnapshot exact_source_snapshot(const Bank& bank) {
        if(!bank.exact_source_samples)return {};
        return {bank.exact_source_samples->counts(),bank.exact_source_samples->working_bytes(),bank.exact_source_samples->capacity()};
    }
    static void trace_exact_source_pool(const Bank& bank,const char* reason,bool final=false,
                                       std::optional<ExactSourceSnapshot> saved={}) {
        const auto snapshot=saved?*saved:exact_source_snapshot(bank);
        if(!snapshot.rows)return; // A tombstone must not overwrite the retired generation's final counts.
        audio::trace::record("receiver_exact_source_samples",[&](auto& f) {
            const auto& counts=snapshot.counts;
            f.add("revision",bank.revision);f.add("sample",bank.samples);f.add("pool_generation",bank.exact_source_generation);
            f.add("reason",reason);f.add("final_snapshot",final);
            f.add("counter_scope","one exact-source pool generation; cumulative, do not sum successive snapshots");
            f.add("bytes",snapshot.bytes);f.add("rows",snapshot.rows);
            f.add("queries",counts.queries);f.add("cached_points",counts.cached_points);
            f.add("original_build_calls",counts.original_build_calls);f.add("direct_calls",counts.direct_calls);
            f.add("net_avoided_original_calls",static_cast<long double>(counts.queries)-counts.original_build_calls-counts.direct_calls);
            f.add("argument_mismatches",counts.argument_mismatches);f.add("speculative_failures",counts.speculative_failures);
            f.add("borrows",counts.borrows);f.add("tile_hits",counts.tile_hits);f.add("builds",counts.builds);
            f.add("failed_builds",counts.failed_builds);f.add("busy",counts.busy);
            f.add("evictions",counts.evictions);f.add("exhaustions",counts.exhaustions);
            f.add("source_arithmetic","original FP64 paired q, exact argument bits, no interpolation");
        });
    }
    static std::size_t drop_exact_source_pool(Bank& bank,const char* reason) {
        if(!bank.exact_source_samples)return 0;
        const auto old=bank.exact_source_samples->working_bytes();
        const auto borrowed=modem::detail::scheduled_source_samples::binding;
        if(borrowed.pool==bank.exact_source_samples.get()) {
            // An active Scope is a borrowed capability even after its Readers
            // join. Reclaim backing/restore its loan, but defer owner destruction
            // until that Scope has restored the previous binding.
            if(!bank.exact_source_samples->capacity())return 0;
            // Teardown asks for this Pool only. General pressure may trim an
            // idle peer when a Pool is pinned; that does not free Pool bytes.
            // Preserve foreign capability semantics, but narrow our known
            // coordinator callback instead of evicting unrelated warm caches.
            const auto release=borrowed.reclaim==&ExactSourceLoan::reclaim_cohort?
                &ExactSourceLoan::reclaim:borrowed.reclaim;
            if(release && release(borrowed.pressure_context))
                return old-bank.exact_source_samples->working_bytes();
            return 0;
        }
        // Only joined coordinator barriers may destroy the borrowed object.
        // If any backing is pinned/in construction it remains fully charged.
        const auto snapshot=exact_source_snapshot(bank);
        if(!bank.exact_source_samples->try_reclaim())return 0;
        trace_exact_source_pool(bank,reason,true,snapshot);
        if(old>bank.working_bytes)throw Error("exact source pool charge exceeds receiver bank");
        bank.exact_source_samples.reset();bank.working_bytes-=old;return old;
    }
    struct ExactSourceLoan {
        Bank& bank;Receiver& receiver;std::size_t capacity=0,loan=0;
        std::stop_token stop;
        // All peers remain joined/stable for this synchronous Scope. A peer
        // cannot regrow optional state until the active receiver returns.
        ReceiverList::iterator next_peer=bank.receivers().begin();
        static bool reclaim(void* opaque) {
            auto& self=*static_cast<ExactSourceLoan*>(opaque);
            auto& pool=self.bank.exact_source_samples;if(!pool || !pool->capacity())return false;
            const auto old=pool->working_bytes();
            const auto snapshot=exact_source_snapshot(self.bank);
            if(!pool->try_reclaim())return false;
            trace_exact_source_pool(self.bank,"mandatory_pressure",true,snapshot);
            // Keep the Pool object alive until this Scope and every
            // copied worker binding have gone. No source Reader is
            // pinned when the serial room_for hook requests this.
            const auto retained=pool->working_bytes();
            if(retained>old || old-retained>self.bank.working_bytes)
                throw Error("exact source reclamation charge is inconsistent");
            const auto freed=old-retained;self.bank.working_bytes-=freed;
            if(self.loan>self.capacity || freed>self.capacity-self.loan)throw Error("exact source loan restoration exceeds cohort workspace");
            self.loan+=freed;self.receiver.modem->set_workspace_bytes(self.loan);
            audio::trace::record("receiver_exact_source_loan_restored",[&](auto& f){
                f.add("revision",self.bank.revision);f.add("sample",self.bank.samples);
                f.add("pool_generation",self.bank.exact_source_generation);f.add("freed_bytes",freed);
                f.add("retained_tombstone_bytes",retained);f.add("receiver_loan",self.loan);});
            return freed!=0;
        }
        static bool reclaim_points(void* opaque) {
            auto& self=*static_cast<ExactSourceLoan*>(opaque);auto& pool=self.bank.exact_source_points;
            if(!pool || !pool->enabled())return false;
            const auto old=pool->working_bytes();const auto saved=point_snapshot(self.bank);
            if(!pool->try_reclaim())return false;
            const auto retained=pool->working_bytes();
            if(retained>old || old-retained>self.bank.working_bytes)throw Error("point pool reclamation charge inconsistent");
            const auto freed=old-retained;
            if(self.loan>self.capacity || freed>self.capacity-self.loan)throw Error("point pool loan exceeds cohort workspace");
            self.bank.working_bytes-=freed;self.loan+=freed;self.receiver.modem->set_workspace_bytes(self.loan);
            trace_point_pool(self.bank,"mandatory_pressure",true,saved);return freed!=0;
        }
        // Owner-thread only: all other receivers have joined at this serial
        // feed/history barrier. Reclaim caches, never peers, captured PCM,
        // original processing journals, prepared decisions or admitted aliases.
        static bool reclaim_cohort(void* opaque) {
            auto& self=*static_cast<ExactSourceLoan*>(opaque);
            if(self.stop.stop_requested())return false;
            if(reclaim_points(opaque))return true;
            if(!self.receiver.modem->deferred_pattern_history())return reclaim(opaque);
            // One positive grant per request. The caller retests its precise
            // need before another idle peer loses its warm acceleration.
            if(reclaim(opaque))return true; // Retain the scoped Pool tombstone.
            bool changed=false;
            std::size_t peers=0,freed_total=0;
            for(;self.next_peer!=self.bank.receivers().end();) {
                if(self.stop.stop_requested())break;
                auto& peer=*self.next_peer++;
                if(&peer==&self.receiver || !peer.modem->deferred_pattern_history())continue;
                const auto before=receiver_workspace(peer);
                if(before>self.bank.working_bytes)throw Error("idle receiver charge exceeds cohort workspace");
                const auto settle=[&] {
                    const auto after=receiver_workspace(peer);
                    if(after>before)throw Error("idle optional trim grew receiver state");
                    const auto freed=before-after;
                    if(self.loan>self.capacity || freed>self.capacity-self.loan || freed>self.bank.working_bytes)
                        throw Error("idle receiver loan restoration exceeds cohort workspace");
                    // Publish each freed delta before another operation can
                    // throw. Outer feed accounting owns only the active delta.
                    self.bank.working_bytes-=freed;self.loan+=freed;
                    self.receiver.modem->set_workspace_bytes(self.loan);
                    if(freed){++peers;freed_total+=freed;changed=true;}
                };
                try {peer.modem->trim_optional_workspace();}
                catch(...) {settle();throw;}
                settle();
                if(changed)break;
            }
            if(freed_total)audio::trace::record("receiver_idle_cache_loan_restored",[&](auto& f){
                f.add("revision",self.bank.revision);f.add("sample",self.bank.samples);
                f.add("epoch",self.receiver.epoch);f.add("trimmed_peers",peers);
                f.add("freed_bytes",freed_total);f.add("receiver_loan",self.loan);
                f.add("retained_cohort_bytes",self.bank.working_bytes);
            });
            return changed;
        }
    };
    static modem::detail::scheduled_source_samples::Binding source_binding(Bank& bank,Receiver& receiver,
            void* pressure,bool (*reclaim)(void*),modem::detail::scheduled_source_samples::Purpose purpose) {
        namespace source=modem::detail::scheduled_source_samples;
        source::Binding result;
        if(helper_assisted_receiver(bank,receiver))return result;
        const auto eligible=exact_source_receiver(receiver);
        result.pool=eligible?bank.exact_source_samples.get():nullptr;
        result.points=eligible?bank.exact_source_points.get():nullptr;
        result.point_requested=point_mode()!=PointMode::off && eligible;
        result.point_cache=point_mode()==PointMode::cache && eligible;
        if((result.pool || result.point_requested) && receiver.family==UINT64_MAX)throw Error("source key namespace exhausted");
        result.key_namespace=eligible?receiver.family+1:0;
        result.pressure_context=pressure;result.reclaim=reclaim;result.consumer=receiver.point_consumer;result.purpose=purpose;
        if(result.points && result.points->enabled()) {
            const auto saved=result.points->snapshot();
            if(receiver.point_family.pool!=saved.pool || receiver.point_family.generation!=saved.generation) {
                try{const auto config=transfer::seeded_config(receiver.options,receiver.epoch);
                    receiver.point_family=result.points->bind(modem::detail::source_family(config,result.key_namespace));}
                catch(const Error&){receiver.point_family={};} // Speculative identity failure cannot reject Original q.
                catch(const std::bad_alloc&){receiver.point_family={};}
            }
            result.point_family=receiver.point_family;
        }
        return result;
    }
    static void note_oscillator_coverage(Bank& bank,const modem::OscillatorPatternSearch& search) {
        if(!search.limited)return;
        bank.limited=bank.coverage_limited=true;
        std::ostringstream detail;
        detail<<"RX oscillator coverage incomplete: frequency +/-"<<search.frequency.half_width_hz
            <<" Hz (requested "<<search.frequency.requested_half_width_hz<<"), clock +/-"
            <<search.clock_half_width_ppm<<" ppm (requested "<<search.requested_clock_half_width_ppm<<")";
        bank.oscillator_limit=detail.str();
    }
    static std::size_t receiver_workspace(const Receiver& receiver) {
        // Reserve bounded diagnostic growth and control storage before
        // admitting a receiver, rather than reporting only its idle allocation.
        const auto& config=receiver.options.modem;
        // Inline shells live in the allocator-accounted receiver vector. One
        // bank-wide serialized allowance covers caller/control temporaries.
        const auto owned_plan=receiver.options.key && config.fake_hopping?
            sizeof(modem::FhssPlan)+config.fake_hopping->power_fallback_reason.capacity():0;
        std::size_t burst_bytes=receiver.bursts.capacity()*sizeof(modem::PatternBurst);
        for(const auto& burst:receiver.bursts)burst_bytes+=burst.bits.capacity();
        return receiver.modem->working_bytes() + burst_bytes + receiver.history_ordinals.heap_bytes() +
            receiver.pattern_score_history.working_bytes()+receiver.key_tag.capacity()+
            receiver.options.receive_targets_db_hz.capacity()*sizeof(double)+owned_plan +
            receiver.presentations.size()*(sizeof(decltype(receiver.presentations)::value_type)+4*sizeof(void*)) +
            (receiver.content?receiver.content->working_bytes():0) +
            (receiver.source_curve_family?sizeof(modem::detail::SourceFamily):0);
    }
    static bool helper_assisted_receiver(const Bank& bank,const Receiver& receiver)noexcept {
        return bank.helper_assistance && bank.helper_assistance->receiver==&receiver;
    }
    static bool retain_helper_absence_tail(Bank& bank) {
        if(!bank.helper_assistance)return false;
        auto& state=*bank.helper_assistance;
        if(state.receiver->presentations.empty() && !state.receiver->modem->synchronized()) {
            // A finite admitted arrival prior still owns already captured PCM.
            // Its first FFT may not have filled before the auxiliary tone ends.
            // Retain that original bounded search, without new helper updates.
            if(state.bounded_initial_search && !state.receiver->modem->initial_search_complete()) {
                state.tail_only=true;return true;
            }
            return false;
        }
        // Losing the auxiliary tone is not observed payload END. Stop fresh
        // acquisition while retaining ordinary tracking/absence scoring, without
        // new helper updates or a different historical phase convention.
        if(!state.receiver->modem->suspend_helper_acquisition())return false;
        state.acquisition_frozen=true;
        state.tail_only=true;return true;
    }
    static void drop_helper_assistance(Bank& bank,const char* reason,bool physically_complete=false) {
        if(!bank.helper_assistance)return;
        if(bank.helper_assistance->payload_retained && !physically_complete) {
            // Accepted bits belong to an ordinary Robust payload. Pressure may
            // stop optional new acquisition/phase updates, never its retained
            // source, recovery capture, progress or physical absence scoring.
            if(!bank.helper_assistance->tail_only) {
                retain_helper_absence_tail(bank);
                audio::trace::record("receiver_helper_assistance_suspended",[&](auto& f){
                    f.add("sample",bank.samples);f.add("reason",reason);f.add("payload_retained",true);
                });
            }
            return;
        }
        auto* target=bank.helper_assistance->receiver;
        const auto found=std::find_if(bank.receivers().begin(),bank.receivers().end(),[&](const auto& r){return &r==target;});
        if(found!=bank.receivers().end()) {
            const auto owned=receiver_workspace(*found);
            const auto nodes=bank.receiver_allocations->allocated_bytes();
            const auto pool=bank.native_points?bank.native_points->working_bytes():0;
            bank.receivers().erase(found);
            bank.working_bytes-=owned+nodes-bank.receiver_allocations->allocated_bytes()+pool-
                (bank.native_points?bank.native_points->working_bytes():0);
            bank.reception_history_dirty=true;
        }
        const auto control=bank.helper_assistance->bytes();
        bank.helper_assistance.reset();bank.working_bytes-=control;
        audio::trace::record("receiver_helper_assistance_retired",[&](auto& f){f.add("sample",bank.samples);f.add("reason",reason);});
    }
    static bool helper_surplus_available(const Bank& bank,std::size_t capacity,std::size_t input_samples) {
        auto reserved=bank.working_bytes;
        const auto add=[&](std::size_t bytes){if(reserved>capacity || bytes>capacity-reserved)return false;reserved+=bytes;return true;};
        for(const auto& receiver:bank.receivers())if(!helper_assisted_receiver(bank,receiver) || bank.helper_assistance->payload_retained) {
            const auto actual=receiver.modem->working_bytes(),promised=receiver.modem->reserved_workspace_bytes();
            if(promised>actual && !add(promised-actual))return false;
            const auto growth=receiver.modem->projection_cache_headroom(input_samples);
            // FFT reserves its fixed native detector/history/bit promises;
            // SIZE_MAX only disables optional shared preprocessing there.
            if(growth!=std::numeric_limits<std::size_t>::max() && !add(growth))return false;
        }
        return reserved<=capacity;
    }
    static bool helper_fallback_capacity(const Bank& bank,std::size_t capacity,std::size_t future_samples,
            Bank::HelperConditioning* promises=nullptr) {
        auto reserved=bank.working_bytes;
        const auto add=[&](std::size_t bytes) {
            if(reserved>capacity || bytes>capacity-reserved)return false;
            reserved+=bytes;return true;
        };
        if(promises){promises->replay_budgets.clear();promises->replay_budgets.reserve(bank.receivers().size());}
        // No fresh rows may be constructed during this fixed-bank replay.
        for(const auto& receiver:bank.receivers()) {
            const auto numeric=receiver.modem->helper_replay_budget(future_samples);
            if(!numeric || !numeric->cold_constructor_bytes || !receiver.content)return false;
            const auto content=receiver.content->helper_replay_storage_bound(numeric->maximum_stream_bits);
            audio::trace::record("receiver_helper_conditioning_replay_budget",[&](auto& f) {
                f.add("sample",bank.samples);f.add("epoch",receiver.epoch);f.add("future_samples",future_samples);
                f.add("maximum_stream_bits",numeric->maximum_stream_bits);f.add("numeric_bytes",numeric->workspace_bytes);
                f.add("source_supported",content.has_value());
            });
            if(!content)return false;
            const auto before=reserved;
            const auto actual_numeric=receiver.modem->working_bytes();
            if(numeric->workspace_bytes>actual_numeric && !add(numeric->workspace_bytes-actual_numeric))return false;
            const auto actual_content=receiver.content->working_bytes();
            if(*content>actual_content && !add(*content-actual_content))return false;
            // A row-only ordinary reset constructs new empty source/options
            // state before replacing the old bounded source. Include target
            // vector copies explicitly; their configured length is not capped
            // by the numeric search's fixed control allowance.
            if(!add(actual_content) || !add(actual_content))return false;
            constexpr auto candidates=transfer::StreamReceiver::candidate_limit;
            constexpr auto presentation=sizeof(decltype(receiver.presentations)::value_type)+4*sizeof(void*);
            if(!add(numeric->reset_overlap_bytes) || !add((candidates+1)*presentation+2*candidates*sizeof(modem::PatternBurst)+
                    2*candidates*numeric->maximum_stream_bits+
                    2*detail::PatternScoreHistory::reserved_entries_bytes))return false;
            if(promises)promises->replay_budgets.push_back({&receiver,numeric->workspace_bytes,
                receiver_workspace(receiver)+reserved-before,numeric->cold_constructor_bytes});
        }
        // Immutable pooled observations can share old backing. Cover each
        // owner's replacement plus one writable overlap before either is freed.
        constexpr auto points=2*(modem::detail::NativePointPool::point_limit*sizeof(std::complex<double>)+
            sizeof(modem::detail::NativePointEntry));
        if(bank.receivers().size()>SIZE_MAX/points)return false;
        const auto pool_growth=bank.receivers().size()*points;
        if(!add(pool_growth))return false;
        if(promises)promises->native_pool_bound=native_pool_workspace(bank)+pool_growth;
        if(bank.source_quota && (bank.source_quota->used>bank.source_quota->limit ||
           !add(bank.source_quota->limit-bank.source_quota->used)))return false;
        return reserved<=capacity;
    }
    static std::size_t helper_replay_unspent(const Bank& bank) {
        if(!bank.helper_fallback_budget)return 0;
        auto bytes=std::size_t{0};
        const auto add=[&](std::size_t n){if(n>SIZE_MAX-bytes)throw Error("helper replay promise exceeds address space");bytes+=n;};
        for(const auto& promise:bank.helper_fallback_budget->replay_budgets) {
            const auto actual=receiver_workspace(*promise.receiver);
            if(promise.total>actual)add(promise.total-actual);
        }
        const auto pool=native_pool_workspace(bank),bound=bank.helper_fallback_budget->native_pool_bound;
        if(bound>pool)add(bound-pool);
        if(bank.source_quota) {
            if(bank.source_quota->used>bank.source_quota->limit)throw Error("helper replay source quota is inconsistent");
            add(bank.source_quota->limit-bank.source_quota->used);
        }
        return bytes;
    }
    static std::size_t helper_replay_numeric_limit(const Bank& bank,const Receiver& receiver) {
        if(!bank.helper_fallback_budget)return SIZE_MAX;
        const auto& rows=bank.helper_fallback_budget->replay_budgets;
        const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& p){return p.receiver==&receiver;});
        if(row==rows.end())throw Error("helper replay admitted an unbudgeted ordinary receiver");
        return row->numeric;
    }
    static std::size_t helper_replay_cold_grant(const Bank& bank,const Receiver& receiver) {
        if(!bank.helper_fallback_budget)return 0;
        const auto& rows=bank.helper_fallback_budget->replay_budgets;
        const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& p){return p.receiver==&receiver;});
        if(row==rows.end() || !row->cold_constructor)throw Error("helper replay has no certified ordinary reset recipe");
        return row->cold_constructor;
    }
    static void drop_helper_history(Bank& bank) {
        if(!bank.helper_history)return;
        bank.working_bytes-=bank.helper_history->bytes();bank.helper_history.reset();
    }
    static void prepare_helper_history(Bank& bank,const Settings& value,std::size_t input_samples) {
        if(bank.helper_fallback_replay_bytes)return;
        if(value.helper.power_w>0 && std::any_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver){
            return !receiver.presentations.empty();
        })) {drop_helper_history(bank);return;}
        if(bank.helper_history) {
            if(value.helper.power_w<=0 || !helper_surplus_available(bank,bank_capacity(value),input_samples))
                drop_helper_history(bank);
            return;
        }
        if(value.helper.power_w<=0)return;
        auto count=static_cast<std::size_t>(std::min<std::uint64_t>(
            16ULL*value.transfer.modem.sample_rate,256*1024));
        const auto capacity=bank_capacity(value),cap=std::min<std::size_t>(64*1024*1024,capacity/8);
        // A long-symbol helper may qualify after the old 16-second ring has
        // lost symbol zero. Expensive banks retain the complete bounded
        // original aperture BEFORE qualification, not just after suspension.
        std::uint64_t aperture=count;
        for(const auto& receiver:bank.receivers()) {
            if(helper_assisted_receiver(bank,receiver))continue;
            const auto& config=receiver.options.modem;
            if(!config.oscillator_search || modem::oscillator_pattern_search(config,
                transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(config):0).hypotheses.size()<512)continue;
            const auto additional=modem::symbol_sample_count(config)+modem::canonical_phase_extent(config)+
                static_cast<std::uint64_t>(2*value.transfer.search_seconds+4)*config.sample_rate;
            aperture=std::max(aperture,std::max(receiver.modem->initial_fft_input_samples(),additional));
        }
        if(aperture<=(cap-std::min(cap,sizeof(Bank::HelperHistory)))/sizeof(float)) {
            const auto proposed=sizeof(Bank::HelperHistory)+static_cast<std::size_t>(aperture)*sizeof(float);
            if(proposed<=capacity && helper_surplus_available(bank,capacity-proposed,input_samples))
                count=static_cast<std::size_t>(aperture);
        }
        const auto bytes=sizeof(Bank::HelperHistory)+count*sizeof(float);
        if(!count || bytes>=cap || bytes>capacity || !helper_surplus_available(bank,capacity-bytes,input_samples))return;
        try {
            auto history=std::make_unique<Bank::HelperHistory>();history->pcm.resize(count);history->end=bank.samples;
            const auto actual=history->bytes();
            if(actual>capacity || !helper_surplus_available(bank,capacity-actual,input_samples))return;
            bank.working_bytes+=actual;bank.helper_history=std::move(history);
        } catch(const std::bad_alloc&){} // Optional PCM retention yields to payload work.
    }
    static std::size_t native_pool_workspace(const Bank& bank)noexcept {
        return (bank.native_peaks?bank.native_peaks->capacity_bytes():0)+
            (bank.native_points?bank.native_points->working_bytes():0);
    }
    static void set_receiver_container_limit(Bank& bank,std::size_t capacity) {
        const auto used=bank.receiver_allocations->allocated_bytes();
        if(used>bank.working_bytes)throw Error("receiver container charge exceeds bank");
        const auto other=bank.working_bytes-used;
        bank.receiver_allocations->set_allocation_limit(other<capacity?capacity-other:0);
    }
    static void trim_native_caches(Bank& bank) {
        for(auto& peer:bank.receivers())if(!peer.modem->clock_windowed()) {
            const auto before=peer.modem->working_bytes();peer.modem->trim_optional_workspace();
            bank.working_bytes-=before-peer.modem->working_bytes();
        }
    }
    // Point COW allocation is preflighted before input. Both arrays remain
    // visible in the active ledger; restores happen before reset/publication.
    struct NativeStorageScope {
        Bank& bank;Receiver& receiver;
        std::optional<modem::detail::NativePeakLease> peaks;
        std::optional<modem::detail::NativePointLease> points;
        NativeStorageScope(Bank& b,Receiver& r,std::size_t capacity,std::size_t& accounted):bank(b),receiver(r) {
            const auto growth=bank.native_points?modem::detail::NativePointLease::allocation_headroom(
                *bank.native_points,receiver.native_points,*receiver.modem):0;
            if(bank.working_bytes>capacity || growth>capacity-bank.working_bytes)trim_native_caches(bank);
            accounted=receiver_workspace(receiver);
            if(bank.working_bytes>capacity || growth>capacity-bank.working_bytes)
                throw Error("native point writable overlap exceeds receiver bank");
            const auto before=receiver_workspace(receiver)+native_pool_workspace(bank);
            try {
                peaks.emplace(bank.native_peaks.get(),*receiver.modem);
                points.emplace(bank.native_points.get(),receiver.native_points,*receiver.modem);
            } catch(...) {points.reset();peaks.reset();throw;}
            bank.working_bytes=bank.working_bytes-before+receiver_workspace(receiver)+native_pool_workspace(bank);
            accounted=receiver_workspace(receiver);
        }
        void release()noexcept {
            if(!peaks&&!points)return;
            const auto before=receiver_workspace(receiver)+native_pool_workspace(bank);
            points.reset();peaks.reset();
            bank.working_bytes=bank.working_bytes-before+receiver_workspace(receiver)+native_pool_workspace(bank);
        }
        ~NativeStorageScope(){release();}
    };
    static void bind_native_storage(Bank& bank,Receiver& receiver,std::size_t capacity) {
        if(bank.working_bytes>capacity)throw Error("receiver construction exceeds bank before binding");
        const auto before=receiver_workspace(receiver)+native_pool_workspace(bank);
        const auto history=modem::detail::NativeDiagnosticHistoryAccess::requirements(*receiver.modem,pattern_score_limit);
        if(history) {
            const auto available=capacity-bank.working_bytes;
            if(history->binding_growth_bytes>available ||
               !modem::detail::NativeDiagnosticHistoryAccess::bind(*receiver.modem,pattern_score_limit,available))
                throw Error("native diagnostic binding exceeds receiver construction overlap");
        }
        // Constructor loans have already restored this joined backing. Adoption
        // removes any remaining private array without changing candidate coverage.
        (void)modem::detail::NativePeakLease::adopt(*bank.native_peaks,*receiver.modem);
        if(bank.native_points) {
            const auto now=receiver_workspace(receiver)+native_pool_workspace(bank);
            const auto remaining=capacity-(bank.working_bytes-before+now);
            if(bank.native_points->adoption_metadata_headroom()<=remaining)
                (void)modem::detail::NativePointLease::try_adopt(*bank.native_points,receiver.native_points,*receiver.modem);
        }
        bank.working_bytes=bank.working_bytes-before+receiver_workspace(receiver)+native_pool_workspace(bank);
    }
    const bool custom_epoch_clock;
    EpochClock epoch_clock;
    ReplayClock replay_clock;
    LiveMutex mutex;
    execution::StopCondition changed;
    Settings settings;
    Snapshot current;
    std::uint64_t spectrum_segment_serial=0, spectrum_source=0;
    std::uint64_t generation = 0, decoder_generation = 0, tx_serial = 0, receive_revision = 0, next_signal = 1, next_event = 1;
    // Material identity, independent of a keyfile name or selected profile.
    // Deliberately lives only as long as this Session; never written to disk.
    std::map<Bytes,std::uint64_t> transmit_epochs;
    Bytes selected_transmit_key;
    std::uint64_t recovery_clear_generation = 0,receive_clear_generation=0;
    std::array<std::uint8_t,16> reception_namespace{};
    std::atomic<std::uint64_t> pattern_score_observation_id{0};
    bool tx_busy = false;
    bool capture_suspended=false,source_active=false,capture_active=false,receive_pending=false;
    std::optional<Clock::time_point> simulation_compute_started;
    Clock::time_point next_hardware_send{};
    using Transmission = std::variant<Message, Bytes, modem::Noise>;
    struct TransmitRequest {
        Transmission content; bool force = false;
        bool noise_long_profile = false;
        std::shared_ptr<const modem::FhssPlan> hopping;
        bool helper_only = false;
    };
    std::deque<TransmitRequest> queued;
    std::shared_ptr<Prepared> ready;
    std::deque<AudioBlock> input;
    std::size_t input_bytes = 0, helper_input_bytes=0, decoding_helper_bytes=0, decoding_bytes = 0, received_bytes = 0, receiver_bytes = 0;
    std::uint64_t capture_samples=0;
    std::optional<CaptureQuietProof> capture_quiet;
    std::uint64_t capture_burst_fence=0;
    bool capture_burst_revoked=false;
    std::optional<detail::BurstCaptureTimestamp> capture_burst_timestamp;
    std::shared_ptr<CaptureFutureProof> capture_future;
    bool capture_future_revoked=false;
    std::size_t audio_bytes = 0,capture_audio_bytes=0,playback_audio_bytes=0;
    std::string capture_timing_status,playback_timing_status;
    // Duplex capture can restart after output fails. Keep the TX failure until
    // an explicit new transmission or configuration reset, independently of
    // transient input status and Clear received.
    std::string transmit_error;
    bool capture_timing_fallback=false,playback_timing_fallback=false;
    std::optional<Clock::time_point> decoding_queued_at;
    std::size_t decoding_remaining_samples=0;
    std::uint64_t decoding_remaining_physical_samples=0;
    std::uint64_t delayed_processing_pcm_samples=0;
    std::optional<Clock::time_point> delayed_processing_queued_at;
    bool delayed_processing_pending=false;
    // At most one coordinator runs; each job owns its bounded parallel search.
    std::deque<std::shared_ptr<RecoveryTask>> recoveries;
    std::shared_ptr<RecoveryTask> active_recovery;
    modem::ConstellationBatch pending_points;
    ConstellationSource pending_source = ConstellationSource::input;
    std::vector<ReplayFrame> replay;
    // Shared with the source-local Prepared so clear/cancel also withdraws
    // results that have not reached the replay handoff yet.
    std::shared_ptr<detail::ReplayStaging> staged_receives;
    std::uint64_t replay_omitted = 0;
    Clock::time_point replay_started{};
    std::optional<std::size_t> delivered_replay_frame;
    std::size_t first_visible_replay_frame = 0;
    double replay_bin_hz = 0;
    std::stop_source capture_stop, tx_stop, decode_stop;
    bool source_pcm_started=false,finish_requested=false;
    execution::Task source, capture_worker, encoder, decoder, recovery_worker;

    explicit Impl(EpochClock clock, ReplayClock presentation_clock)
        : custom_epoch_clock(bool(clock)),
          epoch_clock(clock ? std::move(clock) : EpochClock(epoch_now)),
          replay_clock(presentation_clock ? std::move(presentation_clock) : ReplayClock(Clock::now)) {
        (void)current_epoch();
        // Local cache identity only: this namespace never enters the wire.
        std::random_device random;
        for(auto& byte:reception_namespace)byte=static_cast<std::uint8_t>(random());
        source = execution::Task([this](std::stop_token stop) { source_loop(stop); });
        capture_worker = execution::Task([this](std::stop_token stop) { capture_loop(stop); });
        encoder = execution::Task([this](std::stop_token stop) { encode_loop(stop); });
        decoder = execution::Task([this](std::stop_token stop) { decode_loop(stop); });
        recovery_worker = execution::Task([this](std::stop_token stop) { recovery_loop(stop); });
    }
    ~Impl() {
        halt(); source.request_stop(); capture_worker.request_stop(); encoder.request_stop(); decoder.request_stop(); recovery_worker.request_stop(); changed.notify_all();
        source.join(); capture_worker.join(); encoder.join(); decoder.join(); recovery_worker.join();
        if(rx_cpu::global().mode!=rx_cpu::Mode::disabled)audio::trace::record("receiver_component_cpu",[&](auto& f) {rx_cpu::write_fields(f,rx_cpu::global().snapshot());});
    }
    double current_epoch() const {
        const auto now = epoch_clock();
        if (!std::isfinite(now) || now < 0 ||
            static_cast<long double>(now) >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
            throw Error("epoch clock must return finite nonnegative Unix seconds within uint64 range");
        return now;
    }
    bool simulation_media_clock(const Settings& value) const noexcept {
        return value.simulation && !custom_epoch_clock && !value.transfer.timestamp &&
            !value.transfer.modem.symbol_burst.enabled();
    }
    std::string idle_status() const {
        return settings.simulation ? "Simulated independent radios; sampled audio, clock error and phase noise" : "Listening to audio input";
    }
    void update_simulation_elapsed() {
        if(simulation_compute_started)
            current.simulation_compute_seconds=std::chrono::duration<double>(Clock::now()-*simulation_compute_started).count();
    }
    void finish_simulation_elapsed() {
        update_simulation_elapsed();simulation_compute_started.reset();
        current.simulation_receiving_tail=false;
    }
    void clear_replay(bool discard_pending = true) {
        if (discard_pending && staged_receives)
            std::erase_if(current.signals, [this](const auto& event) {
                return !event.validated && !event.complete && staged_receives->owns_pending(event);
            });
        if(staged_receives)staged_receives->clear();
        staged_receives.reset();
        replay = {}; delivered_replay_frame.reset(); first_visible_replay_frame = 0;
        current.simulation_replay = false;
        current.replay_frame_index = current.replay_frame_count = 0;
        current.simulation_sample_fraction = 0;
        current.pattern_scores.clear();
        current.pattern_score_observations.clear();
    }
    void discard_staged_recoveries() {
        if(!staged_receives)return;
        std::erase_if(current.signals,[this](const auto& event) {
            return std::any_of(staged_receives->entries().begin(),staged_receives->entries().end(),[&](const auto& staged) {
                return staged && staged->recovering() &&
                    ((staged->event.id==event.id && staged->event.revision>=event.revision) ||
                     std::find(staged->event.superseded_ids.begin(),staged->event.superseded_ids.end(),event.id)!=
                         staged->event.superseded_ids.end());
            });
        });
        staged_receives->clear_recoveries();
    }
    // Called with mutex held, after validation. Invalid input must leave a
    // currently presented simulation and its pending result untouched.
    double key_lock_seconds(const Settings& value, const modem::Config& config,
                            const Bytes& key) const {
        if(value.simulation || key.empty())return 0;
        const auto used=transmit_epochs.find(key);
        if(used==transmit_epochs.end())return 0;
        auto keyed=config;
        if(keyed.scheduled_hopping && keyed.scheduled_hopping->enabled())keyed.data_key=value.transfer.key;
        if(value.transfer.timestamp) {
            if(value.transfer.timestamp>used->second)return 0;
            // An explicit timestamp must be changed or deliberately forced;
            // passage of time alone cannot make that fixed address unused.
            return std::max(1.,datapump::detail::transmit_epoch_lock_seconds(keyed,used->second,current_epoch()));
        }
        return datapump::detail::transmit_epoch_lock_seconds(keyed,used->second,current_epoch());
    }
    void enqueue(Transmission transmission, bool force = false,std::shared_ptr<const modem::FhssPlan> hopping = {}, bool noise_long_profile = false,bool helper_only = false) {
        if(capture_suspended)throw Error("Audio is reserved by the other mode");
        if (current.transmitting_noise) throw Error("stop noise before transmitting a message");
        const bool noise = std::holds_alternative<modem::Noise>(transmission);
        if(hopping) {
            hopping->validate();
            if(!noise && !settings.transfer.key)throw Error("FHSS requires an encryption key");
            // Freeze caller-owned values for this queued TX, independently of UI edits.
            hopping=std::make_shared<const modem::FhssPlan>(*hopping);
        }
        const auto* message=std::get_if<Message>(&transmission);
        const auto& config=message && settings.long_message_modem && !transfer::uses_raw_message(*message)?
            *settings.long_message_modem:settings.transfer.modem;
        if(!noise && !force && key_lock_seconds(settings,config,selected_transmit_key)>0)
            throw Error("Transmit locked: earlier output used this key for a future symbol");
        if (noise && (tx_busy || !queued.empty())) throw Error("wait for transmission to finish before starting noise");
        if (queued.size() >= 8) throw Error("transmit queue contains eight pending messages");
        advance_replay(replay_clock());
        queued.push_back({std::move(transmission),force,noise_long_profile,std::move(hopping),helper_only}); current.transmitting = true;
        current.transmission_finished = current.transmission_cancelled = false;
        if (!tx_busy) {
            current.transmitting_noise = noise;
            current.transmission_fraction = current.transmission_seconds = 0;
            current.simulation_compute_seconds=0;current.simulation_receiving_tail=false;
            current.transmit_trace = {};
            clear_replay();
        }
        pending_points = {}; replay_omitted = 0;
        current.constellation.clear(); current.constellation_source = ConstellationSource::input;
        current.constellation_dropped = 0;
        transmit_error.clear(); current.error.clear(); changed.notify_all();
    }
    void queue_points(modem::ConstellationBatch batch, ConstellationSource source_kind) {
        if (batch.points.empty() && !batch.dropped) return;
        if (pending_source != source_kind) pending_points = {};
        pending_source = source_kind;
        append_points(pending_points, std::move(batch), constellation_limit(settings));
    }
    void advance_replay(Clock::time_point now) {
        if (replay.empty()) return;
        const auto elapsed = std::max(Clock::duration::zero(), now - replay_started);
        const bool finished = elapsed >= replay_duration;
        const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::min(elapsed, Clock::duration(replay_duration))).count();
        const auto due = finished ? replay.size() : std::min(replay.size(),
            static_cast<std::size_t>(static_cast<std::uint64_t>(elapsed_ns) * replay.size() / 3000000000ULL) + 1);
        // Retain only the displayed generation frame if replay is cancelled.
        // The CPU may already have generated the entire transmission.
        const auto trace_index = finished ? replay.size() - 1 : std::min(replay.size() - 1,
            static_cast<std::size_t>(static_cast<std::uint64_t>(elapsed_ns) * replay.size() / 3000000000ULL));
        current.transmit_trace = replay[trace_index].transmit_trace;
        if(staged_receives)staged_receives->drain(due,finished,[&](detail::ReplayStaging::Entry entry) {
            if(entry.terminal()) {
                // Suppress obsolete prefixes for every completed reception,
                // while retaining any independent later revision.
                std::erase_if(current.signals,[&](const auto& event) {
                    return (event.id==entry.event.id && event.revision<=entry.event.revision) ||
                        std::find(entry.event.superseded_ids.begin(),entry.event.superseded_ids.end(),event.id)!=
                            entry.event.superseded_ids.end();
                });
                if(entry.received && entry.received->recovery) {
                    queue_recovery(std::move(*entry.received),entry.event);
                    entry.received.reset();
                }
                if(entry.received) {
                    admit_received(entry.received->content.message.data.capacity(),settings.content_limit);
                    current.received.push_back(std::move(*entry.received));
                }
            }
            append_signal(std::move(entry.event));
        });
        if (finished) {
            // A stalled UI must not extend the replay or paint old symbol
            // coordinates over live input. Account for undelivered points.
            const auto first = delivered_replay_frame ? *delivered_replay_frame + 1 : 0;
            for (auto i = first; i < replay.size(); ++i) {
                count_dropped(replay_omitted, replay[i].constellation.size());
                count_dropped(replay_omitted, replay[i].dropped);
            }
            clear_replay(false); current.status = idle_status(); ++current.sequence;
            changed.notify_all();
        }
    }
    void replay_snapshot(Snapshot& result, Clock::time_point now) {
        if (replay.empty()) return;
        const auto elapsed = std::max(Clock::duration::zero(), now - replay_started);
        const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        const auto index = static_cast<std::size_t>(static_cast<std::uint64_t>(elapsed_ns) * replay.size() / 3000000000ULL);
        if (!delivered_replay_frame || index != *delivered_replay_frame) {
            first_visible_replay_frame = delivered_replay_frame ? *delivered_replay_frame + 1 : 0;
            delivered_replay_frame = index;
        }
        const auto& frame = replay[index];
        result.transmit_trace = frame.transmit_trace;
        result.simulation_replay = true; result.replay_frame_index = index; result.replay_frame_count = replay.size();
        result.simulation_sample_fraction = frame.fraction;
        result.waveform = frame.waveform; result.spectrum_db.assign(frame.spectrum.begin(), frame.spectrum.end());
        result.spectrum_complex.clear();result.spectrum_sample_count=0;result.spectrum_segment=0;
        result.waveform_sample_rate = static_cast<std::uint32_t>(replay_bin_hz * (plot_size / 4));
        result.environment_monitor = false;
        result.simulation_spectrum_gain_db = frame.simulation_spectrum_gain_db;
        result.spectrum_bin_hz = replay_bin_hz; result.constellation_source = frame.source;
        modem::ConstellationBatch visible;
        // Delayed UI polls consume intervening points together, not a long
        // rolling history. Coordinate/source changes must never be mixed.
        for (auto i = first_visible_replay_frame; i <= index; ++i) {
            if (replay[i].source != frame.source) { visible = {}; continue; }
            modem::ConstellationBatch points; points.dropped = replay[i].dropped;
            for (const auto point : replay[i].constellation) points.points.emplace_back(point.real(), point.imag());
            append_points(visible, std::move(points), constellation_limit(settings));
        }
        result.constellation = std::move(visible.points); result.constellation_dropped = visible.dropped;
        // Each frame already contains a bounded history; replacing it keeps
        // repeated polls stable and never leaks later evidence into replay.
        result.pattern_scores.assign(frame.pattern_scores.begin(), frame.pattern_scores.end());
        result.pattern_score_observations = frame.pattern_score_observations;
        result.pattern_score_observation_id = frame.pattern_score_observation_id;
    }
    void halt() {
        std::lock_guard lock(mutex);
        finish_simulation_elapsed();
        current.running = current.transmitting = current.transmitting_noise = false;
        current.transmission_finished = true; current.transmission_cancelled = true;
        current.status = "Stopped";
        clear_replay(); pending_points = {};
        replay_omitted = 0;
        ++generation; ++tx_serial; ++receive_revision;
        queued.clear(); ready.reset(); tx_busy = false; input.clear(); input_bytes = helper_input_bytes = 0;
        capture_samples=0;capture_quiet.reset();capture_future.reset();capture_future_revoked=false;
        capture_burst_fence=0;capture_burst_revoked=false;capture_burst_timestamp.reset();
        current.capture_recorded_samples=current.capture_certified_null_samples=0;
        current.processed_physical_samples=0;decoding_remaining_physical_samples=0;
        delayed_processing_pcm_samples=0;delayed_processing_queued_at.reset();delayed_processing_pending=false;
        const auto unavailable=unavailable_recovery_events();invalidate_recoveries();
        for(auto event:unavailable)append_signal(std::move(event));
        capture_stop.request_stop(); tx_stop.request_stop(); decode_stop.request_stop(); changed.notify_all();
    }
    void configure(const Settings& requested) {
        auto value = normalized(requested);
        const auto key=value.transfer.key?value.transfer.key->mac(Bytes{'D','P','-','T','X','-','E','P','O','C','H','-','G','U','A','R','D'}):Bytes{};
        std::lock_guard lock(mutex);
        // These fixed display buffers share plot_workspace. Prepare before any
        // configuration side effects, then retain capacity across publishers.
        current.waveform.reserve(plot_size);
        current.spectrum_db.reserve(plot_size/2+1);
        current.spectrum_complex.reserve(plot_size/2+1);
        capture_stop.request_stop(); tx_stop.request_stop(); decode_stop.request_stop();
        decode_stop = std::stop_source{};
        settings = std::move(value); ++generation; ++tx_serial; ++receive_revision;
        capture_samples=0;capture_quiet.reset();capture_future.reset();capture_future_revoked=false;
        capture_burst_fence=0;capture_burst_revoked=false;capture_burst_timestamp.reset();
        current.capture_recorded_samples=current.capture_certified_null_samples=0;
        current.processed_physical_samples=0;decoding_remaining_physical_samples=0;
        delayed_processing_pcm_samples=0;delayed_processing_queued_at.reset();delayed_processing_pending=false;
        selected_transmit_key=key;
        receive_pending=false;
        queued.clear(); ready.reset(); tx_busy = false; input.clear();
        input_bytes = helper_input_bytes = receiver_bytes = received_bytes = audio_bytes = capture_audio_bytes = playback_audio_bytes = 0;
        auto unavailable=unavailable_recovery_events();invalidate_recoveries();
        clear_replay(); pending_points = {};
        replay_omitted = 0;
        auto waveform=std::move(current.waveform);
        auto spectrum=std::move(current.spectrum_db);
        auto complex_spectrum=std::move(current.spectrum_complex);
        current = {};
        current.waveform=std::move(waveform);current.waveform.clear();
        current.spectrum_db=std::move(spectrum);current.spectrum_db.clear();
        current.spectrum_complex=std::move(complex_spectrum);current.spectrum_complex.clear();
        transmit_error.clear(); current.running = true; current.simulation = settings.simulation;
        simulation_compute_started.reset();
        capture_timing_status.clear();playback_timing_status.clear();
        capture_timing_fallback=playback_timing_fallback=false;
        for(auto& event:unavailable)append_signal(std::move(event));
        current.status = idle_status(); changed.notify_all();
    }
    void account(std::uint64_t samples, std::uint64_t version) {
        std::lock_guard lock(mutex);
        if (!current.running || generation != version) return;
        if (samples > std::numeric_limits<std::uint64_t>::max() - current.samples_received)
            throw Error("virtual sample clock exceeds its platform range");
        current.samples_received += samples;
        current.virtual_seconds = static_cast<double>(current.samples_received) / settings.transfer.modem.sample_rate;
    }
    void audio_format(const audio::StreamFormat& format, std::uint64_t version,bool capture=true) {
        std::lock_guard lock(mutex);
        if (!current.running || generation != version) return;
        const auto peer=capture?playback_audio_bytes:capture_audio_bytes;
        if (peer > audio_reserve(settings) || format.workspace_bytes > audio_reserve(settings)-peer)
            throw Error("sample-rate conversion exceeds the audio workspace reservation; increase DSP capacity");
        current.hardware_sample_rate = format.hardware_rate;
        current.audio_passband_hz = format.usable_passband_hz;
        if(capture || !settings.full_duplex) {
            current.clock_timing_quality=format.timing_quality;
            current.clock_backend_uncertainty_seconds=format.timing_uncertainty_seconds;
        }
        (capture?capture_audio_bytes:playback_audio_bytes)=format.workspace_bytes;
        audio_bytes=capture_audio_bytes+playback_audio_bytes;
        const auto upper_edge = settings.transfer.modem.carrier_hz + modem::waveform_bandwidth_hz(settings.transfer.modem) / 2;
        if (upper_edge > format.usable_passband_hz)
            throw Error("Selected upper band edge (" + std::to_string(upper_edge) +
                        " Hz) exceeds this audio path's usable passband (" +
                        std::to_string(format.usable_passband_hz) + " Hz). Select a narrower band, a wider audio device, or simulation.");
    }
    void timing_status(const audio::TimingStatus& status,std::uint64_t version,bool capture) {
        std::lock_guard lock(mutex);
        if(!current.running || generation!=version)return;
        auto& text=capture?capture_timing_status:playback_timing_status;
        auto& fallback=capture?capture_timing_fallback:playback_timing_fallback;
        fallback=!status.following_system_clock;
        text=(capture?"RX timing: ":"TX timing: ")+status.reason;
        current.clock_timing_fallback=capture_timing_fallback || playback_timing_fallback;
        current.clock_timing_status=capture_timing_status;
        if(!playback_timing_status.empty()) {
            if(!current.clock_timing_status.empty())current.clock_timing_status+="; ";
            current.clock_timing_status+=playback_timing_status;
        }
        if(capture && !status.following_system_clock)current.receiver_timing.reset();
        if(!capture)current.transmit_clock_following=status.following_system_clock;
        ++current.sequence;
    }
    // All environment writers retain the capacities prepared by configure.
    // Source geometry is the fixed plot tail, independent of message contents.
    template<class T,class Range>
    static void copy_environment_vector(std::vector<T>& target,const Range& source) {
        if(source.size()>target.capacity())throw Error("environment plot exceeds its prepared capacity");
        target.resize(source.size());
        std::copy(source.begin(),source.end(),target.begin());
    }
    // Called under mutex; no receiver state or FFT work depends on this display
    // identity. A restart, source switch or capture gap invalidates phase pairs.
    void publish_spectrum_metadata(std::span<const std::complex<float>> spectrum,
        std::uint64_t start,std::size_t count,double bin_hz,std::uint64_t source) {
        if(current.spectrum_complex.empty() || source!=spectrum_source ||
           start<=current.spectrum_start_sample || bin_hz!=current.spectrum_bin_hz) {
            ++spectrum_segment_serial;
            if(!spectrum_segment_serial)++spectrum_segment_serial;
        }
        spectrum_source=source;current.spectrum_segment=spectrum_segment_serial;
        copy_environment_vector(current.spectrum_complex,spectrum);
        current.spectrum_start_sample=start;
        current.spectrum_sample_count=count;
    }
    void publish(const detail::SignalWindow& window, const modem::Config& config, std::uint64_t version,
                 Clock::time_point& last_plot, bool force = false,
                 modem::StreamingTransmitter* transmitter = nullptr, std::uint64_t serial = 0,
                 const std::vector<std::complex<double>>* pattern_scores = nullptr,
                 const std::vector<PatternScoreObservation>* pattern_observations = nullptr,
                 std::uint64_t observation_id = 0, bool preserve_monitor = false) {
        if (!force && Clock::now() - last_plot < std::chrono::milliseconds(50)) return;
        auto measured = window.frame(config,preserve_monitor?detail::SignalView::constellation:detail::SignalView::all);
        auto transmitted = transmitter ? transmitter->take_payload_constellation() : modem::ConstellationBatch{};
        auto transmitted_history = transmitter ? transmitter->payload_constellation() :
            std::vector<std::complex<double>>{};
        std::lock_guard lock(mutex);
        if (!current.running || generation != version) return;
        if (transmitter && tx_serial != serial) return;
        if (pattern_scores && tx_serial != serial) return;
        if(!preserve_monitor) {
            publish_spectrum_metadata(measured.spectrum_complex,measured.spectrum_start_sample,
                measured.spectrum_sample_count,static_cast<double>(config.sample_rate)/plot_size,
                transmitter?serial+1:0);
            copy_environment_vector(current.waveform,measured.waveform);
            copy_environment_vector(current.spectrum_db,measured.spectrum);
            current.waveform_sample_rate = config.sample_rate; current.environment_monitor = false;
            current.simulation_spectrum_gain_db = settings.simulation ?
                detail::spectrum_display_gain(settings.simulation_spectrum_gain_db, current.waveform.size()) : std::nullopt;
            current.spectrum_bin_hz = static_cast<double>(config.sample_rate) / plot_size;
            ++current.spectrum_revision;
        }
        if (pattern_scores) {
            current.pattern_scores = *pattern_scores;
            current.pattern_score_observations = *pattern_observations;
            current.pattern_score_observation_id = observation_id;
        }
        current.constellation = std::move(measured.constellation);
        current.constellation_source = ConstellationSource::input;
        current.constellation_dropped = 0;
        if (transmitter) {
            // Keep actual emitted chips across display intervals. Narrow bands
            // can produce only one new chip in several GUI updates. Until the
            // payload begins, the measured settling waveform remains visible.
            if (!transmitted_history.empty())
                current.constellation = std::move(transmitted_history);
            current.pattern_scores.clear();
            current.pattern_score_observations.clear();
            current.constellation_source = ConstellationSource::transmitted;
            queue_points(std::move(transmitted), ConstellationSource::transmitted);
        }
        ++current.sequence; last_plot = Clock::now();
    }
    void publish_monitor(const detail::SignalWindow& window,detail::EnvironmentFrameWorkspace& workspace,
                         const modem::Config& config,std::uint64_t version,Clock::time_point& last_plot) {
        if(Clock::now()-last_plot<std::chrono::milliseconds(50))return;
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
        capture_monitor_probe::mark(capture_monitor_probe::Phase::frame_start);
        {
#endif
        const auto measured=window.environment_frame(workspace);
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
        capture_monitor_probe::mark(capture_monitor_probe::Phase::frame_ready);
        {
#endif
        std::lock_guard lock(mutex);
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
        capture_monitor_probe::mark(capture_monitor_probe::Phase::lock_acquired);
#endif
        if(!current.running || generation!=version)return;
        publish_spectrum_metadata(measured.spectrum_complex,measured.spectrum_start_sample,
            measured.spectrum_sample_count,static_cast<double>(config.sample_rate)/plot_size,
            std::numeric_limits<std::uint64_t>::max());
        copy_environment_vector(current.waveform,measured.waveform);
        copy_environment_vector(current.spectrum_db,measured.spectrum);
        current.waveform_sample_rate=config.sample_rate;current.environment_monitor=true;
        current.spectrum_bin_hz=static_cast<double>(config.sample_rate)/plot_size;
        current.simulation_spectrum_gain_db.reset();
        current.dsp_buffered_bytes=receiver_bytes+queue_storage_bytes()+decoding_bytes+audio_bytes+plot_workspace(settings)+
            (tx_busy?settings.dsp_workspace_bytes/4:0);
        ++current.spectrum_revision;++current.sequence;last_plot=Clock::now();
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
        capture_monitor_probe::mark(capture_monitor_probe::Phase::published);
        }
        capture_monitor_probe::mark(capture_monitor_probe::Phase::lock_released);
        }
        capture_monitor_probe::mark(capture_monitor_probe::Phase::frame_destroyed);
#endif
    }
    static std::uint64_t replay_target(const Prepared& wave) {
        const auto samples = wave.transmitter->total_samples();
        const auto divisor = wave.replay_count - 1, index = wave.replay.size();
        return (samples / divisor) * index + (samples % divisor * index + divisor - 1) / divisor;
    }
    void collect_replay(Prepared& wave, const modem::Config& config, const detail::SignalWindow& window,
                        std::optional<double> simulation_spectrum_gain_db) {
        auto measured = window.frame(config);
        ReplayFrame frame;
        frame.simulation_spectrum_gain_db = detail::spectrum_display_gain(simulation_spectrum_gain_db, measured.waveform.size());
        const auto keep = std::min(replay_wave_samples, measured.waveform.size());
        frame.waveform.assign(measured.waveform.end() - static_cast<std::ptrdiff_t>(keep), measured.waveform.end());
        frame.spectrum.resize(replay_bins);
        for (std::size_t i = 0; i < frame.spectrum.size(); ++i) {
            const auto begin = i * 4;
            const auto end = std::min(begin + 4, measured.spectrum.size());
            frame.spectrum[i] = static_cast<float>(*std::max_element(measured.spectrum.begin() + static_cast<std::ptrdiff_t>(begin),
                                            measured.spectrum.begin() + static_cast<std::ptrdiff_t>(end)));
        }
        modem::ConstellationBatch bounded;
        append_points(bounded, {std::move(measured.constellation), 0}, wave.point_limit);
        frame.constellation.reserve(bounded.points.size());
        for (const auto point : bounded.points) frame.constellation.emplace_back(static_cast<float>(point.real()), static_cast<float>(point.imag()));
        frame.pattern_scores.reserve(wave.pattern_scores.size());
        for (const auto point : wave.pattern_scores) frame.pattern_scores.emplace_back(static_cast<float>(point.real()), static_cast<float>(point.imag()));
        frame.pattern_score_observations = wave.pattern_score_observations;
        frame.pattern_score_observation_id = wave.pattern_score_observation_id;
        frame.dropped = bounded.dropped;
        frame.fraction = static_cast<double>(static_cast<long double>(wave.transmitted_samples) /
                                            wave.transmitter->total_samples());
        frame.transmit_trace = wave.transmitter->transmit_trace();
        wave.replay.push_back(std::move(frame));
    }
    static std::size_t helper_reference_bytes(const helper::Reference* reference) {
        return reference?sizeof(*reference)+(reference->metadata?reference->metadata->callsign.capacity():0):0;
    }
    std::size_t queue_storage_bytes() const {
        return input_bytes+helper_input_bytes+input.size()*sizeof(AudioBlock);
    }
    void enqueue_audio(std::span<const float> samples, std::uint64_t version, double observed_epoch,
                       std::optional<audio::TimePrediction> timing={},
                       std::optional<helper::Reference> reference={},std::uint64_t helper_frontier=0) {
        std::lock_guard lock(mutex);
        if (!current.running || generation != version || capture_suspended) return;
        if(samples.size()>std::numeric_limits<std::uint64_t>::max()-capture_samples)
            throw Error("capture sample counter exhausted");
        const auto seconds_per_frame=timing?static_cast<long double>(timing->seconds_per_frame):
            1.L/settings.transfer.modem.sample_rate;
        if(capture_future && !capture_future->timestamp_valid(capture_samples,samples.size(),observed_epoch,seconds_per_frame)) {
            capture_future.reset();capture_future_revoked=true;
        }
        const bool ahead=capture_future && capture_future->excludes(capture_samples,samples.size(),
            observed_epoch,seconds_per_frame,{generation,receive_revision});
        if(capture_burst_timestamp &&
           !capture_burst_timestamp->accepts(capture_samples,samples.size(),observed_epoch,timing)) {
            capture_quiet.reset();capture_burst_revoked=true;
        }
        const auto quiet=ahead || (capture_quiet && capture_quiet->revision==receive_revision &&
            capture_samples>=capture_quiet->begin && capture_samples+samples.size()<=capture_quiet->end &&
            (capture_quiet->burst_timestamp?
                capture_quiet->generation==generation && capture_quiet->burst_fence==capture_burst_fence && !capture_burst_revoked:
                detail::checkedCaptureFence(observed_epoch,samples.size(),
                timing?static_cast<long double>(timing->seconds_per_frame):1.L/settings.transfer.modem.sample_rate,
                capture_quiet->utc_second)));
        auto bytes = quiet?std::size_t{}:samples.size_bytes();
        bool discard=quiet;
        const auto queue_limit = settings.dsp_workspace_bytes / 8;
        if(quiet)reference.reset(); // Raw tracking continues through null waits.
        // One popped helper reference lives in the auxiliary reservation below;
        // it cannot reduce the original queue's PCM allowance.
        const auto decoding_pcm_bytes=decoding_bytes-std::min(decoding_bytes,decoding_helper_bytes);
        auto retained=queue_storage_bytes()+decoding_pcm_bytes+sizeof(AudioBlock);
        auto reference_bytes=helper_reference_bytes(reference?&*reference:nullptr);
        auto block_bytes=bytes+sizeof(AudioBlock)+reference_bytes;
        if((block_bytes>queue_limit || retained>queue_limit-block_bytes) &&
           (reference || helper_input_bytes)) {
            // Optional dates/metadata may never evict ordinary captured PCM.
            reference.reset();reference_bytes=0;
            for(auto& block:input)block.helper.reset();
            helper_input_bytes=0;
            retained=queue_storage_bytes()+decoding_pcm_bytes+sizeof(AudioBlock);
            block_bytes=bytes+sizeof(AudioBlock);
        }
        if (block_bytes > queue_limit || retained > queue_limit-block_bytes) {
            count_dropped(current.receiver_health.input_overruns,1);
            count_dropped(current.receiver_health.dropped_samples,input_bytes/sizeof(float));
            count_dropped(current.receiver_health.dropped_samples,delayed_processing_pcm_samples);
            count_dropped(current.receiver_health.dropped_samples,decoding_remaining_samples);
            // Every gap, including an oversized callback, invalidates the old
            // acquisition. Never concatenate later samples across missing PCM.
            reset_input_locked();discard=false;bytes=samples.size_bytes();
            current.status = "Audio receiver overrun; restarting acquisition";
            const auto restart_bytes=bytes+sizeof(AudioBlock)+reference_bytes;
            if (restart_bytes > queue_limit || decoding_pcm_bytes+sizeof(AudioBlock) > queue_limit-restart_bytes) {
                count_dropped(current.receiver_health.dropped_samples,samples.size());
                if(restart_bytes>queue_limit)current.error="audio callback exceeds streaming workspace";
                return;
            }
        }
        if(discard && !input.empty() && input.back().skipped_samples &&
           input.back().revision==receive_revision &&
           input.back().sample_origin+input.back().skipped_samples==capture_samples &&
           std::floor(input.back().observed_epoch)==std::floor(observed_epoch)) {
            input.back().skipped_samples+=samples.size();
        } else {
            AudioBlock block{{},receive_revision,observed_epoch,Clock::now(),timing,capture_samples,0};
            if(discard)block.skipped_samples=samples.size();
            else block.samples.assign(samples.begin(),samples.end());
            if(reference && helper_frontier>=capture_samples) {
                const auto origin=helper_frontier-capture_samples;
                if(auto mapped=detail::rebase_helper_reference(std::move(*reference),origin,helper_frontier,
                                                              settings.transfer.modem.sample_rate)) {
                    block.helper=std::make_unique<helper::Reference>(std::move(*mapped));
                    helper_input_bytes+=helper_reference_bytes(block.helper.get());
                }
            }
            input_bytes+=block.samples.size()*sizeof(float);
            input.push_back(std::move(block));
        }
        if(discard)count_dropped(current.capture_certified_null_samples,samples.size());
        else count_dropped(current.capture_recorded_samples,samples.size());
        capture_samples+=samples.size(); current.buffered_samples = input_bytes / sizeof(float);
        current.dsp_buffered_bytes = queue_storage_bytes() + decoding_bytes + receiver_bytes + audio_bytes + plot_workspace(settings)+(tx_busy?settings.dsp_workspace_bytes/4:0);
        changed.notify_all();
    }
    // Caller holds mutex. Intentional half-duplex changes use this without
    // marking a fault; overload/device failures additionally latch health.
    void reset_input_locked() {
        input.clear(); input_bytes = helper_input_bytes = 0; ++receive_revision;
        capture_samples=0;capture_quiet.reset();capture_future.reset();capture_future_revoked=false;
        capture_burst_fence=0;capture_burst_revoked=false;capture_burst_timestamp.reset();
        current.capture_recorded_samples=current.capture_certified_null_samples=0;
        current.processed_physical_samples=0;decoding_remaining_physical_samples=0;
        delayed_processing_pcm_samples=0;delayed_processing_queued_at.reset();delayed_processing_pending=false;
        current.clock_window_modeled=false;current.receiver_timing.reset();
        current.receiver_instances=current.peak_receiver_instances=0;
        current.receiver_admissions=current.receiver_retirements=0;
        current.receiver_bank_builds=0;current.receiver_error.clear();
        decode_stop.request_stop();decode_stop=std::stop_source{};receive_pending=false;
        decoding_remaining_samples=0;decoding_queued_at.reset();
        current.buffered_samples = 0;
        current.dsp_buffered_bytes=decoding_bytes+receiver_bytes+audio_bytes+plot_workspace(settings)+(tx_busy?settings.dsp_workspace_bytes/4:0);
        changed.notify_all();
    }
    void discontinuity() {
        std::lock_guard lock(mutex);
        reset_input_locked();
    }
    void input_interrupted(std::uint64_t version,std::stop_token stop) {
        std::lock_guard lock(mutex);
        if(!current.running || generation!=version || capture_suspended || stop.stop_requested())return;
        current.receiver_health.input_interrupted=true;
        count_dropped(current.receiver_health.dropped_samples,input_bytes/sizeof(float));
        count_dropped(current.receiver_health.dropped_samples,delayed_processing_pcm_samples);
        count_dropped(current.receiver_health.dropped_samples,decoding_remaining_samples);
        reset_input_locked();
    }
    double observation_epoch(const Bank& bank,const Settings& value,std::uint64_t sample) const {
        if(bank.timing)return static_cast<double>(bank.timing->utc_seconds+
            (static_cast<long double>(sample)-bank.observed_sample)*bank.timing->seconds_per_frame);
        if(!bank.observed_epoch)return current_epoch(); // simulation retains its own timeline
        return *bank.observed_epoch+static_cast<double>((static_cast<long double>(sample)-
            bank.observed_sample)/value.transfer.modem.sample_rate);
    }
    static void amplitude_metadata(Receiver& receiver,const modem::Config& config,
            const modem::PatternSearch& search,const modem::ReceiverStrategyPlan& selection) {
        receiver.amplitude_fft_samples=receiver.amplitude_channel_count=0;
        receiver.amplitude_geometry.reset();
        if(selection.backend!=modem::ReceiverBackend::amplitude_energy||!selection.supported)return;
        receiver.clock_window_modeled=search.amplitude_start_window.has_value()||search.qualified_start_window.has_value();
        receiver.amplitude_geometry=modem::pattern_amplitude_geometry(config,search);
        std::string{}.swap(receiver.amplitude_geometry->reason); // Retain allocation-free geometry only.
        receiver.amplitude_fft_samples=receiver.amplitude_geometry->fft_samples;
        receiver.amplitude_channel_count=selection.amplitude_channels;
    }
    bool modeled_start_window(modem::PatternSearch& search,const modem::Config& config,
            std::uint64_t epoch,const Bank& bank,const Settings& value,std::uint64_t origin,bool amplitude=false) {
        detail::ReceiverAdmissionContext context;context.simulation_capture_dated=bank.simulation_capture_dated;context.timing=bank.timing;context.observed_sample=bank.observed_sample;
        return detail::modeled_receiver_start_window(search,config,epoch,context,value,origin,amplitude);
    }
    static bool crossbank_temporal_plans_requested() {
        // Internal experiment: OFF by default; row capacity is independently
        // compiled as DATAPUMP_SCHED_CROSSBANK_PLAN_CACHE_ENTRIES (default 4).
        static const bool requested=[] {
            const auto* value=std::getenv("DATAPUMP_SCHED_CROSSBANK_PLAN_CACHE");
            return value && std::string_view(value)=="1";
        }();
        return requested;
    }
    static bool large_permutation_cache_requested() {
        // Explicit developer experiment; default grants remain 8 rows/256KiB
        // even in binaries compiled with larger finite capacity ceilings.
        static const bool requested=[] {
            const auto* value=std::getenv("DATAPUMP_RX_PERMUTATION_LARGE_POOL");
            return value && std::string_view(value)=="1";
        }();
        return requested;
    }
    static void drop_shared_temporal_plans(Bank& bank,const char* reason="optimization_or_cohort") {
        if(!bank.temporal_plans)return;
        if(!bank.temporal_plans->idle())throw Error("shared temporal Plan owner still borrowed");
        const auto bytes=bank.temporal_plans->working_bytes();
        const auto counters=bank.temporal_plans->statistics();
        audio::trace::record("receiver_shared_temporal_plans_retired",[&](auto& f) {
            f.add("reason",reason);
            f.add("sample",bank.samples);f.add("bytes",bytes);f.add("workers",bank.temporal_plans->workers());
            f.add("requests",counters.requests);f.add("hits",counters.hits);f.add("builds",counters.builds);
            f.add("evictions",counters.evictions);f.add("unavailable",counters.unavailable);
            f.add("rejected",counters.rejected);f.add("cancelled",counters.cancelled);f.add("exceptions",counters.exceptions);
            f.add("hash_words",counters.hash_words);f.add("compared_words",counters.compared_words);
        });
        bank.temporal_plans.reset();bank.working_bytes-=bytes;
    }
    // A shared owner is an actual allocation/retention contract. It is never
    // inferred from a duty-cycle estimate or an unbound planner reservation.
    void ensure_shared_capture(Bank& bank,const Settings& value) {
        if(bank.shared_capture)return;
        if(!modem::detail::scheduled_acquisition_policy::requested())return;
        const auto& base=value.transfer.modem;
        if(!modem::scheduled_bursts(base))return;
        const Crypto* key=value.transfer.key?&*value.transfer.key:
            value.receive_keys.empty()?nullptr:&value.receive_keys.front();
        if(!key)return;
        auto profiles=value.transfer.automatic_receive_profiles?
            tuning::receive_profiles(base,value.transfer.receive_targets_db_hz,value.transfer.receive_pattern_mode,true):
            std::vector<modem::Config>{base};
        bool eligible=false;std::size_t consumers=128;
        for(auto c:profiles) {
            c.data_key=*key;const auto n=modem::symbol_sample_count(c);
            if(!modem::scheduled_bursts(c) || modem::packed_scheduled_symbols(c) ||
               !modem::detail::scheduled_capture::shared_source_duration_eligible(n,c.sample_rate) || !modem::pattern_pulse_enabled(c))continue;
            eligible=true;
            const auto count=modem::receiver_strategy_bank_count(c,value.transfer.search_seconds,
                std::max<std::size_t>(1,value.receive_keys.size()+std::size_t(value.transfer.key.has_value())),profiles.size());
            if(count>(std::numeric_limits<std::size_t>::max()-128)/2)throw Error("shared capture consumer reservation overflow");
            consumers=std::max(consumers,2*count+128);
        }
        if(!eligible)return;
        namespace shared=modem::detail::scheduled_capture;
        const auto capacity=bank_capacity(value);
        const auto layout=Bank::SharedCapture::cohort_layout(capacity,consumers);
        for(auto c:profiles) {
            c.data_key=*key;
            const auto n=modem::symbol_sample_count(c);
            if(!modem::scheduled_bursts(c) || modem::packed_scheduled_symbols(c) ||
               !shared::shared_source_duration_eligible(n,c.sample_rate) || !modem::pattern_pulse_enabled(c))continue;
            // Provision only an owner that can retain the actual worst-case
            // physical horizon. Too-long profiles retain the preceding local
            // history path; optional sharing must not make them unavailable.
            const auto clock=c.oscillator_search?modem::oscillator_pattern_search(c,
                transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(c):0).requested_clock_half_width_ppm:10000.;
            if(!std::isfinite(clock) || clock<0 || clock>10000)return;
            const auto aperture=std::ceil(static_cast<long double>(modem::scheduled_physical_span_samples(c,n))/(1-clock*1e-6L));
            const auto history=std::ceil(aperture+2.L*(value.transfer.search_seconds+1.)*c.sample_rate+
                modem::canonical_phase_extent(c)+2*512+8192);
            if(history>layout.guaranteed_samples) {
                audio::trace::record("receiver_shared_capture_unavailable",[&](auto& f) {
                    f.add("symbol_samples",n);f.add("required_history_samples",history);
                    f.add("guaranteed_history_samples",layout.guaranteed_samples);
                    f.add("reason","actual horizon exceeds bounded owner; original local history retained");
                });
                return;
            }
        }
        auto owner=std::make_shared<Bank::SharedCapture>(layout.limits,shared::Identity{bank.revision,bank.revision},bank.samples);
        const auto charge=owner->capacity_reservation();
        if(charge>capacity-std::min(capacity,bank.working_bytes))throw Error("shared capture capacity exceeds remaining DSP workspace");
        auto future=owner->subscribe(bank.samples,true);
        bank.shared_capture=std::move(owner);bank.future_capture.emplace(std::move(future));bank.working_bytes+=charge;
        audio::trace::record("receiver_shared_capture_reserved",[&](auto& f){
            f.add("sample",bank.samples);f.add("revision",bank.revision);f.add("capacity",charge);f.add("consumers",consumers);
        });
    }
    static void initialize_receiver_storage(Bank& bank,std::size_t capacity) {
        if(bank.receiver_storage)return;
        const auto object_bytes=sizeof(ReceiverList);
        if(object_bytes>capacity-std::min(capacity,bank.working_bytes))
            throw Error("receiver container control exceeds DSP workspace");
        bank.working_bytes+=object_bytes;
        set_receiver_container_limit(bank,capacity);
        const auto before=bank.receiver_allocations->allocated_bytes();
        try {
            bank.receiver_storage=std::make_unique<ReceiverList>(
                detail::ReceiverStorageAllocator<Receiver>{bank.receiver_allocations});
        }catch(...) {
            bank.working_bytes-=object_bytes;
            throw;
        }
        bank.working_bytes+=bank.receiver_allocations->allocated_bytes()-before;
    }
    // Prepare one node while every retained owner stays joined and resident.
    // The account also measures platform-specific sentinel/proxy allocations.
    // No list object is moved; successful admission is an equal-allocator splice.
    struct ReceiverAdmissionStage {
        Bank& bank;
        std::optional<ReceiverList> pending;
        bool owner_charged=false;
        explicit ReceiverAdmissionStage(Bank& owner)noexcept:bank(owner){}
        ReceiverAdmissionStage(const ReceiverAdmissionStage&)=delete;
        ReceiverAdmissionStage& operator=(const ReceiverAdmissionStage&)=delete;
        void prepare() {
            const auto before=bank.receiver_allocations->allocated_bytes();
            try {
                pending.emplace(detail::ReceiverStorageAllocator<Receiver>{bank.receiver_allocations});
                pending->emplace_back();
            }catch(...) {
                pending.reset();
                // Failed list/node construction must release every partial allocation.
                if(bank.receiver_allocations->allocated_bytes()!=before)std::terminate();
                throw;
            }
            bank.working_bytes+=bank.receiver_allocations->allocated_bytes()-before;
        }
        Receiver& receiver()noexcept{return pending->front();}
        void charge_receiver() {
            bank.working_bytes+=receiver_workspace(receiver());owner_charged=true;
        }
        void commit()noexcept {
            bank.receivers().splice(bank.receivers().end(),*pending,pending->begin());
            owner_charged=false;
        }
        ~ReceiverAdmissionStage()noexcept {
            if(!pending)return;
            if(owner_charged) {
                const auto bytes=receiver_workspace(receiver());
                if(bytes>bank.working_bytes)std::terminate();
                bank.working_bytes-=bytes;
            }
            const auto before=bank.receiver_allocations->allocated_bytes();
            pending.reset();
            const auto after=bank.receiver_allocations->allocated_bytes();
            if(after>before || before-after>bank.working_bytes)std::terminate();
            bank.working_bytes-=before-after;
        }
    };
    struct HelperPlanningIsolation {
        Bank& bank;
        std::optional<ReceiverList> isolated;
        explicit HelperPlanningIsolation(Bank& owner):bank(owner) {
            if(!bank.helper_assistance)return;
            auto& receivers=bank.receivers();
            const auto found=std::find_if(receivers.begin(),receivers.end(),[&](const auto& r){
                return &r==bank.helper_assistance->receiver;
            });
            if(found==receivers.end())return;
            const auto before=bank.receiver_allocations->allocated_bytes();
            try {isolated.emplace(detail::ReceiverStorageAllocator<Receiver>{bank.receiver_allocations});}
            catch(const std::bad_alloc&) {
                // Accepted payloads use the ordinary memory-error policy.
                // Speculative assistance can simply release its own owner.
                if(bank.helper_assistance->payload_retained)throw;
                drop_helper_assistance(bank,"cohort_isolation_unavailable");return;
            }
            bank.working_bytes+=bank.receiver_allocations->allocated_bytes()-before;
            isolated->splice(isolated->end(),receivers,found);
        }
        HelperPlanningIsolation(const HelperPlanningIsolation&)=delete;
        ~HelperPlanningIsolation(){restore();}
        void restore()noexcept {
            if(!isolated)return;
            // Ordinary order is unchanged; assistance always follows every
            // ordinary receiver, including newly appended epoch admissions.
            bank.receivers().splice(bank.receivers().end(),*isolated);
            const auto before=bank.receiver_allocations->allocated_bytes();
            isolated.reset();
            bank.working_bytes-=before-bank.receiver_allocations->allocated_bytes();
        }
    };
    static helper::Reference measurement_only(const helper::Reference& source) {
        helper::Reference result;
        result.sample=source.sample;result.sample_rate=source.sample_rate;
        result.frequency_hz=source.frequency_hz;result.phase_radians=source.phase_radians;
        result.frequency_uncertainty_hz=source.frequency_uncertainty_hz;
        result.phase_uncertainty_radians=source.phase_uncertainty_radians;
        result.dwell_start_seconds=source.dwell_start_seconds;result.dwell_uncertainty_seconds=source.dwell_uncertainty_seconds;
        result.frequency_valid=source.frequency_valid;result.phase_valid=source.phase_valid;result.timing_valid=source.timing_valid;
        result.frequency_sample=source.frequency_sample;return result;
    }
    static std::uint64_t helper_search_sequence() noexcept {
        static std::atomic<std::uint64_t> sequence{0};
        auto previous=sequence.load(std::memory_order_relaxed);
        do {if(previous==UINT64_MAX)return 0;}
        while(!sequence.compare_exchange_weak(previous,previous+1,std::memory_order_relaxed));
        return previous+1;
    }
    static bool ordinary_payload_active(const Bank& bank) {
        return std::any_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver) {
            return receiver.modem->synchronized() || !receiver.presentations.empty() ||
                std::any_of(receiver.bursts.begin(),receiver.bursts.end(),[](const auto& burst){return !burst.bits.empty();});
        });
    }
    static bool helper_tracking_reference_qualified(const Bank& bank,const Settings& value,
            const std::optional<helper::Reference>& reference) noexcept {
        if(value.helper.power_w<=0 || !reference)return false;
        const auto& ref=*reference;
        const auto dated=ref.frequency_sample?ref.frequency_sample:ref.sample;
        return ref.frequency_valid && ref.sample_rate==value.transfer.modem.sample_rate &&
            ref.sample<=bank.samples && dated<=ref.sample && bank.samples-dated<=ref.sample_rate &&
            std::isfinite(ref.frequency_hz) && std::isfinite(ref.frequency_uncertainty_hz) &&
            ref.frequency_uncertainty_hz>=0;
    }
    static bool fixed_epoch_admission(const Bank& bank,const Settings& value) {
        return value.transfer.timestamp && !bank.burst_admission_frozen && !bank.burst_resume_pending &&
            std::none_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver) {
                return receiver.key_tag.empty() && receiver.modem->clock_windowed();
            });
    }
    static void commit_helper_conditioning(Bank& bank) {
        if(!bank.helper_conditioning || bank.helper_conditioning->committed)return;
        auto& state=*bank.helper_conditioning;
        const auto before=state.bytes();
        // Only an ordinary detector's accepted bit reaches this boundary.
        // Tone presence, punctuation and metadata never commit acquisition.
        state.committed=true;state.original.reset();state.fallback.reset();
        std::vector<Bank::HelperConditioning::ReplayBudget>{}.swap(state.replay_budgets);
        bank.working_bytes-=before-state.bytes();
        audio::trace::record("receiver_helper_conditioning_committed",[&](auto& f) {
            f.add("sample",bank.samples);f.add("resident",bank.receivers().size());
            f.add("released_bytes",before-state.bytes());
        });
    }
    helper::AssistancePlan conditioned_search(const Bank& bank,const Settings& value,
            const modem::Config& config,const modem::PatternSearch& original,std::uint64_t origin,
            const helper::Reference& reference) {
        helper::AssistanceRequest request;
        request.expected_helper_hz=helper::plan(value.helper,value.transfer.modem.carrier_hz,
            detail::helper_occupied_bandwidth(value),120000000,0,
            modem::outer_dsss_transmit_gain(value.transfer.modem)).frequency_hz;
        request.current_sample=bank.samples;request.reference_sequence=helper_search_sequence();
        request.maximum_hypotheses=257;request.cover_continuous_clock=true;
        request.receiver_input_origin_samples=origin;
        // Punctuation is modulo .4 seconds. Keep EVERY original epoch/start
        // alternative instead of choosing the nearest advisory alias.
        request.qualify_epoch_timing=false;
        auto plan=helper::plan_assistance(config,original,reference,request);
        if(plan.search) {
            plan.search->input_sample_origin=origin;
            plan.search->compact_clock_search=false;
            plan.search->receiver_strategy_workspace_bytes=0;
            plan.search->prefer_streamed_templates=true;
        }
        return plan;
    }
    bool begin_helper_conditioning(Bank& bank,const Settings& value,const helper::Reference& reference) {
        const auto rate=value.transfer.modem.sample_rate;
        const auto dated=reference.frequency_sample?reference.frequency_sample:reference.sample;
        if(!rate || value.helper.power_w<=0 || bank.helper_cohort || bank.helper_conditioning || bank.helper_conditioning_attempted ||
           bank.samples<bank.helper_conditioning_probe || bank.helper_fallback_replay_bytes || !reference.frequency_valid || reference.sample_rate!=rate ||
           reference.sample>bank.samples || dated>reference.sample || bank.samples-dated>rate ||
           !std::isfinite(reference.frequency_hz) || !std::isfinite(reference.frequency_uncertainty_hz) ||
           reference.frequency_uncertainty_hz<0 || bank.receivers().empty() || ordinary_payload_active(bank) ||
           !bank.helper_history || !bank.helper_history->count || bank.shared_capture ||
           value.transfer.modem.symbol_burst.enabled() || modem::scheduled_bursts(value.transfer.modem))return false;
        bank.helper_conditioning_probe=bank.samples+rate;
        // Replaying retained PCM must not reduce a fresh ordinary cohort's
        // constructor grant. Only the SAME fixed bank may be suspended; its
        // original growth promises are reserved before this transaction.
        if(!fixed_epoch_admission(bank,value)) {
            audio::trace::record("receiver_helper_conditioning_declined",[&](auto& f) {
                f.add("sample",bank.samples);f.add("reason","rolling_ordinary_admission");
            });
            return false;
        }
        // Small banks retain the existing optional phase/frequency assistance.
        // This transaction targets the large oscillator acquisition multiplier.
        bool expensive=false;
        std::uint64_t horizon=0;
        for(const auto& receiver:bank.receivers()) {
            if(helper_assisted_receiver(bank,receiver))continue;
            const auto config=transfer::seeded_config(receiver.options,receiver.epoch);
            const bool amplitude=receiver.amplitude_geometry.has_value() ||
                receiver.amplitude_fft_samples || receiver.amplitude_channel_count;
            if(config.receiver_strategy!=modem::ReceiverStrategy::complex_only ||
               receiver.modem->clock_windowed() || amplitude) {
                // A frequency reference does not establish phase coherence.
                // Retain an actual energy detector and explicit backend policy
                // rather than replacing either with a complex detector.
                audio::trace::record("receiver_helper_conditioning_backend_retained",[&](auto& f) {
                    f.add("sample",bank.samples);f.add("epoch",receiver.epoch);f.add("family",receiver.family);
                    f.add("strategy",static_cast<unsigned>(config.receiver_strategy));f.add("amplitude_backend",amplitude);
                });
                return false;
            }
            // The reversible bank must also retain the SAME ordinary row-reset
            // backend under its immutable cold constructor grant. Other modes
            // keep ordinary reception and the smaller optional helper lane.
            if(value.transfer.clock_sync || config.fake_hopping || config.scheduled_hopping ||
               config.dsss || config.dsss_factor!=1 || config.sample_rate!=rate)return false;
            if(config.oscillator_search && modem::oscillator_pattern_search(config,
                transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(config):0).hypotheses.size()>=512)
                expensive=true;
            const auto fft=receiver.modem->initial_fft_input_samples();
            const auto additional=modem::symbol_sample_count(config)+modem::canonical_phase_extent(config)+
                static_cast<std::uint64_t>(2*value.transfer.search_seconds+4)*rate;
            horizon=std::max(horizon,std::max(fft,additional));
        }
        if(!expensive || !horizon)return false;
        const auto capacity=bank_capacity(value);
        const auto history_limit=std::min<std::size_t>(64*1024*1024,capacity/8);
        if(horizon>history_limit/sizeof(float) || horizon>SIZE_MAX-2ULL*rate)return false;
        horizon+=2ULL*rate;
        if(horizon>history_limit/sizeof(float))return false;
        // An unaccepted additional lane is optional. It cannot become a fresh
        // unbudgeted consumer while the original fixed bank is being replayed.
        drop_helper_assistance(bank,"primary_acquisition_priority");
        constexpr auto maximum_filter_storage=4097*(sizeof(double)+sizeof(float));
        auto planned=sizeof(Bank::HelperConditioning)+sizeof(Bank)+sizeof(Bank::HelperHistory)+maximum_filter_storage;
        const auto plan_storage=[&](std::size_t count,std::size_t size) {
            if(count>(SIZE_MAX-planned)/size)return false;
            planned+=count*size;return true;
        };
        if(!plan_storage(static_cast<std::size_t>(horizon),sizeof(float)) ||
           !plan_storage(bank.helper_history->count,sizeof(float)) ||
           !plan_storage(bank.receivers().size(),sizeof(std::pair<std::uint64_t,std::uint64_t>)+
               sizeof(Bank::HelperConditioning::ReplayBudget)))return false;
        if(planned>=capacity || !helper_fallback_capacity(bank,capacity-planned,static_cast<std::size_t>(horizon))) {
            audio::trace::record("receiver_helper_conditioning_declined",[&](auto& f) {
                f.add("sample",bank.samples);f.add("reason","finite_fallback_capacity");
            });
            return false;
        }
        try {
            auto state=std::make_unique<Bank::HelperConditioning>();
            state->reference=measurement_only(reference);
            state->admitted_frequency=reference.frequency_hz;state->admitted_radius=reference.frequency_uncertainty_hz;
            {std::lock_guard lock(mutex);state->publication_floor=current.processed_physical_samples;}
            state->attempt_end=bank.samples+horizon-2ULL*rate;
            if(value.helper.frequency!=helper::Frequency::cochannel && reference.frequency_hz>225 &&
               reference.frequency_hz+225<rate/2.)state->filter=helper::PayloadFilter(rate,reference.frequency_hz);
            const auto input_origin=bank.helper_history->first();
            if(bank.helper_history->count<=2*state->filter.delay())return false;
            if(std::any_of(bank.receivers().begin(),bank.receivers().end(),[&](const auto& receiver) {
                return !helper_assisted_receiver(bank,receiver) && receiver.sample_origin<input_origin;
            }))return false;
            const auto origin=input_origin+state->filter.delay();
            state->filter_output_origin=origin;
            state->preroll.reserve(bank.helper_history->count);
            bank.helper_history->visit([&](auto raw) {
                std::array<float,4096> filtered{};
                const auto count=state->filter.push(raw,filtered);
                state->preroll.insert(state->preroll.end(),filtered.begin(),filtered.begin()+count);
            });
            state->fallback=std::make_unique<Bank::HelperHistory>();
            state->fallback->pcm.resize(static_cast<std::size_t>(horizon));state->fallback->end=bank.samples;
            state->initial_epochs.reserve(bank.receivers().size());
            state->replay_budgets.reserve(bank.receivers().size());
            // Charge the original before constructing any replacement object;
            // no allocator or numeric grant may spend its resident bytes twice.
            state->original=std::make_unique<Bank>();
            state->original_bytes=sizeof(Bank)+bank.working_bytes;
            const auto extra=state->bytes()-bank.working_bytes;
            if(extra>=capacity || !helper_fallback_capacity(bank,capacity-extra,static_cast<std::size_t>(horizon),state.get()))return false;
            Bank replacement;
            replacement.samples=bank.samples;replacement.created_at=bank.created_at;
            replacement.revision=bank.revision;replacement.observed_epoch=bank.observed_epoch;
            replacement.simulation_capture_dated=bank.simulation_capture_dated;
            replacement.observed_sample=bank.observed_sample;replacement.timing=bank.timing;
            replacement.public_amplitude_epoch_origin=bank.public_amplitude_epoch_origin;
            replacement.helper_input_origin_samples=bank.helper_input_origin_samples;
            replacement.mixer=bank.mixer;replacement.source_quota=bank.source_quota;
            replacement.next_candidate=bank.next_candidate;replacement.admissions=bank.admissions;
            replacement.builds=bank.builds;replacement.helper_conditioning_attempted=true;
            replacement.helper_last_frequency_sample=dated;
            replacement.working_bytes+=state->bytes();replacement.helper_conditioning=std::move(state);
            if(replacement.working_bytes>=capacity)return false;
            initialize_receiver_storage(replacement,capacity);
            replacement.native_points=std::make_unique<modem::detail::NativePointPool>(capacity/sizeof(modem::detail::NativePointEntry));
            replacement.working_bytes+=replacement.native_points->working_bytes();
            std::size_t original_lanes=0,conditioned_lanes=0;
            for(const auto& old:bank.receivers()) {
                if(helper_assisted_receiver(bank,old))continue;
                auto config=transfer::seeded_config(old.options,old.epoch);
                detail::ReceiverAdmissionContext admission;
                admission.simulation_capture_dated=bank.simulation_capture_dated;
                admission.sample_origin=origin;admission.observed_sample=bank.observed_sample;admission.timing=bank.timing;
                admission.public_amplitude_epoch_origin=bank.public_amplitude_epoch_origin;
                admission.now=observation_epoch(bank,value,origin);admission.keyed=old.options.key.has_value();
                admission.capacity=capacity;admission.prefer_streamed_templates=true;
                admission.propagation=!value.simulation&&value.transfer.clock_sync?value.transfer.clock_sync->offset_seconds:0;
                auto prepared=detail::prepare_receiver_search(config,value,old.epoch,admission);
                const bool calendar=!value.simulation && transfer::shared_hop_calendar(config) && admission.timing;
                if(value.transfer.timestamp && !calendar && !bank.simulation_capture_dated && prepared.search.start_offset_seconds)
                    *prepared.search.start_offset_seconds-=(static_cast<long double>(bank.helper_input_origin_samples)+origin)/rate;
                const auto old_count=prepared.search.hypotheses.size();
                auto plan=conditioned_search(bank,value,config,prepared.search,origin,reference);
                if(!old_count)return false;
                if(!plan.search || plan.search->hypotheses.size()*2>=old_count) {
                    // A mixed family can contain a profile which was already
                    // inexpensive. Preserve that complete original search;
                    // it must not prevent acceleration of the large profiles.
                    if(old_count>=512)return false;
                    plan.search=std::move(prepared.search);
                }
                original_lanes+=old_count;conditioned_lanes+=plan.search->hypotheses.size();
                constexpr std::size_t margin=64*1024;
                if(replacement.working_bytes>=capacity || capacity-replacement.working_bytes<=margin)return false;
                set_receiver_container_limit(replacement,capacity);
                ReceiverAdmissionStage staged(replacement);staged.prepare();auto& receiver=staged.receiver();
                receiver.options=old.options;receiver.epoch=old.epoch;receiver.family=old.family;receiver.key_tag=old.key_tag;
                receiver.sample_origin=origin;receiver.admitted_at=observation_epoch(bank,value,bank.samples);
                receiver.prefer_streamed_templates=true;receiver.clock_window_modeled=prepared.clock_window_modeled;
                config.receiver_strategy=modem::ReceiverStrategy::complex_only;
                const auto grant=std::min(value.dsp_workspace_bytes/2,capacity-replacement.working_bytes-margin);
                receiver.modem=std::make_unique<modem::StreamingReceiver>(config,grant,*plan.search);
                receiver.content=std::make_unique<transfer::StreamReceiver>(receiver.options,receiver.epoch,replacement.source_quota);
                staged.charge_receiver();if(replacement.working_bytes>capacity)return false;
                staged.commit();++replacement.admissions;
                replacement.helper_conditioning->initial_epochs.emplace_back(receiver.epoch,receiver.family);
            }
            if(replacement.receivers().empty())return false;
            if(conditioned_lanes*2>=original_lanes)return false;
            replacement.peak_receivers=std::max(bank.peak_receivers,replacement.receivers().size());
            // The source bank is moved only after ALL original alternatives
            // have a real, fully configured ordinary replacement receiver.
            const auto old_bytes=bank.working_bytes;
            drop_helper_assistance(bank,"conditional_acquisition");
            replacement.working_bytes-=old_bytes-bank.working_bytes;
            replacement.helper_conditioning->original_bytes=sizeof(Bank)+bank.working_bytes;
            *replacement.helper_conditioning->original=std::move(bank);
            bank=std::move(replacement);
            audio::trace::record("receiver_helper_conditioning_admitted",[&](auto& f) {
                f.add("sample",bank.samples);f.add("resident",bank.receivers().size());
                f.add("original_lanes",original_lanes);f.add("conditioned_lanes",conditioned_lanes);
                f.add("fallback_samples",horizon);f.add("fallback_bytes",bank.helper_conditioning->fallback->bytes());
                f.add("filter_delay_samples",bank.helper_conditioning->filter.delay());
                f.add("input_origin",origin);f.add("preroll_samples",bank.helper_conditioning->preroll.size());
                f.add("workspace",bank.working_bytes);f.add("capacity",capacity);
            });
            return true;
        }catch(const Error&){}catch(const std::bad_alloc&){}
        return false;
    }
    void prepare_helper_assistance(Bank& bank,const Settings& value,const helper::Reference& reference,
                                   std::size_t input_samples,std::uint64_t version) {
        if(bank.helper_cohort || bank.helper_conditioning || bank.helper_conditioning_attempted || bank.helper_fallback_replay_bytes)return;
        const auto capacity=bank_capacity(value);
        const auto frequency_sample=reference.frequency_sample?reference.frequency_sample:reference.sample;
        if(value.helper.power_w<=0 ||
           (bank.input_queued_at && Clock::now()-*bank.input_queued_at>std::chrono::milliseconds(500))) {
            drop_helper_assistance(bank,"ordinary_priority_or_disabled");return;
        }
        if(reference.sample_rate!=value.transfer.modem.sample_rate ||
           !reference.frequency_valid || !std::isfinite(reference.frequency_hz) ||
           !std::isfinite(reference.frequency_uncertainty_hz) || reference.frequency_uncertainty_hz<0 || reference.sample>bank.samples ||
           frequency_sample>reference.sample || bank.samples-frequency_sample>reference.sample_rate) {
            if(frequency_sample<=bank.samples && bank.samples-frequency_sample>6ULL*reference.sample_rate)
                bank.helper_evidence_epoch=std::numeric_limits<double>::quiet_NaN();
            if(retain_helper_absence_tail(bank))return;
            drop_helper_assistance(bank,"unavailable_stale_or_lagging");return;
        }
        if(!std::isfinite(bank.helper_evidence_epoch)) {
            // This merely ranks one speculative private epoch. Detection can
            // be late; every other epoch remains in the original search.
            std::unique_lock lock(helper_mutex,std::try_to_lock);
            if(!lock.owns_lock() || !helper_workspace || helper_workspace->generation!=version ||
               helper_workspace->first_reference_end==UINT64_MAX || !helper_workspace->rate)return;
            const auto& work=*helper_workspace;
            const auto first=static_cast<long double>(work.first_reference_end)/work.rate;
            const auto current=(static_cast<long double>(bank.helper_input_origin_samples)+reference.sample)/reference.sample_rate;
            if(first>current)return; // No future evidence from capture/processing skew.
            const auto local=first-work.first_reference_support-
                static_cast<long double>(bank.helper_input_origin_samples)/reference.sample_rate;
            bank.helper_evidence_epoch=observation_epoch(bank,value,0)+static_cast<double>(local);
        }
        if(bank.helper_assistance) {
            auto& state=*bank.helper_assistance;
            if(state.tail_only)return;
            const auto scored_frontier=state.receiver->sample_origin+state.filter.output_samples();
            const auto bin=state.receiver->modem->phase_assistance_bin_samples();
            const auto qualified=reference.timing_valid && reference.phase_valid && std::isfinite(reference.phase_radians) &&
                std::isfinite(reference.phase_uncertainty_radians) && reference.phase_uncertainty_radians>=0 &&
                reference.phase_uncertainty_radians<=.25;
            if(bin && qualified!=state.phase_qualified) {
                if(qualified) {
                    // The canonical punctuation sign changes only an unknown
                    // constant phase. Start its new measured anchor at the last
                    // committed correction, preserving all historical bins.
                    if(reference.sample<state.reference.sample ||
                       reference.sample>scored_frontier-std::min<std::uint64_t>(scored_frontier,bin))return;
                    state.anchor_sample=reference.sample;
                    const auto age=static_cast<double>(scored_frontier-reference.sample)/reference.sample_rate;
                    state.anchor_phase=reference.phase_radians+2*std::numbers::pi*
                        (reference.frequency_hz-state.anchor_frequency)*age-
                        state.phase_continuation.at(scored_frontier-state.receiver->sample_origin,reference.sample_rate);
                    state.unwrapped_phase=reference.phase_radians;
                    state.reference=measurement_only(reference);
                    state.phase_qualified=state.phase_ready=true;
                    audio::trace::record("receiver_helper_phase_qualified",[&](auto& f){
                        f.add("sample",bank.samples);f.add("reference_sample",reference.sample);f.add("epoch",state.receiver->epoch);
                    });
                    if(!helper_surplus_available(bank,capacity,input_samples))drop_helper_assistance(bank,"ordinary_growth_priority");
                    return;
                }
                if(!retain_helper_absence_tail(bank))drop_helper_assistance(bank,"phase_timing_qualification_changed");return;
            }
            // A model chosen now must predate the FIRST sample of a partial
            // bin; merely predating its completion would condition that bin.
            if(reference.sample>=state.reference.sample && reference.sample<=scored_frontier-std::min<std::uint64_t>(scored_frontier,bin)) {
                const auto elapsed=static_cast<double>(reference.sample-state.reference.sample)/reference.sample_rate;
                const auto prediction=state.unwrapped_phase+2*std::numbers::pi*state.reference.frequency_hz*elapsed;
                const auto observed=reference.phase_radians;
                if(reference.timing_valid && reference.phase_valid && std::isfinite(observed) && std::isfinite(reference.phase_uncertainty_radians) &&
                   reference.phase_uncertainty_radians>=0 && reference.phase_uncertainty_radians<=.25 &&
                   std::isfinite(prediction)) {
                    state.unwrapped_phase=prediction+std::remainder(observed-prediction,2*std::numbers::pi);
                    state.phase_ready=state.reference.phase_valid && state.reference.timing_valid;
                }else state.phase_ready=false;
                state.reference=measurement_only(reference);
            }
            if(!helper_surplus_available(bank,capacity,input_samples))drop_helper_assistance(bank,"ordinary_growth_priority");
            return;
        }
        if(bank.receivers().empty())return;
        if(ordinary_payload_active(bank))return;
        // Helper work is isolated from original cohort planning below. Its
        // bounded numeric grant uses actual surplus after every original
        // detector/growth promise, rather than a fixed tiny FFT-only allowance.
        const auto history_bytes=bank.helper_history?bank.helper_history->bytes():0;
        const auto total_cap=std::min<std::size_t>(64*1024*1024,capacity/8);
        const auto cap=total_cap-std::min(total_cap,history_bytes);
        if(cap<128*1024 || bank.working_bytes>capacity || cap>capacity-bank.working_bytes ||
           !helper_surplus_available(bank,capacity-cap,input_samples))return;
        const Receiver* preferred=nullptr;double distance=std::numeric_limits<double>::infinity(),tie=distance;
        for(const auto& original:bank.receivers()) {
            const auto& config=original.options.modem;
            const auto prefix=(static_cast<double>(modem::training_sample_count(config))+
                static_cast<double>(modem::pattern_pulse_padding_samples(config)))/config.sample_rate;
            const auto propagation=!value.simulation&&value.transfer.clock_sync?value.transfer.clock_sync->offset_seconds:0;
            const auto target=bank.helper_evidence_epoch+prefix-propagation;
            const auto epoch=static_cast<double>(original.epoch);
            const auto extent=static_cast<double>(modem::canonical_phase_extent(config)-1)/config.sample_rate;
            const auto candidate=std::max({epoch-target,target-epoch-extent,0.});
            const auto near=std::abs(epoch-target);
            if(candidate<distance || (candidate==distance && near<tie)) {
                distance=candidate;tie=near;preferred=&original;
            }
        }
        for(unsigned pass=0;pass<2;++pass)for(const auto& original:bank.receivers()) {
            if((pass==0)!=(&original==preferred))continue;
            const auto config=transfer::seeded_config(original.options,original.epoch);
            if((config.fake_hopping && config.fake_hopping->defer_symbols) ||
               (config.scheduled_hopping && config.scheduled_hopping->enabled() && !config.scheduled_hopping->continuous()) ||
               config.symbol_burst.enabled() || config.receiver_strategy==modem::ReceiverStrategy::amplitude_only)continue;
            try {
                const auto replay=bank.helper_history && !original.modem->clock_windowed();
                const auto input_origin=replay?bank.helper_history->first():bank.samples;
                helper::PayloadFilter filter;
                if(value.helper.frequency!=helper::Frequency::cochannel && reference.frequency_hz>225 &&
                   reference.frequency_hz+225<config.sample_rate/2.) {
                    filter=helper::PayloadFilter(config.sample_rate,reference.frequency_hz);
                    if(!replay || bank.helper_history->count<=2*filter.delay())continue;
                }
                const auto origin=input_origin+filter.delay();
                detail::ReceiverAdmissionContext admission;
                admission.simulation_capture_dated=bank.simulation_capture_dated;
                admission.sample_origin=origin;admission.observed_sample=bank.observed_sample;
                admission.public_amplitude_epoch_origin=bank.public_amplitude_epoch_origin;
                admission.timing=bank.timing;admission.now=observation_epoch(bank,value,origin);
                admission.propagation=!value.simulation&&value.transfer.clock_sync?value.transfer.clock_sync->offset_seconds:0;
                admission.keyed=original.options.key.has_value();admission.capacity=capacity;
                auto prepared=detail::prepare_receiver_search(config,value,original.epoch,admission);
                const bool dated_calendar=!value.simulation && transfer::shared_hop_calendar(config) && admission.timing;
                if(value.transfer.timestamp && !dated_calendar && !bank.simulation_capture_dated && prepared.search.start_offset_seconds)
                    *prepared.search.start_offset_seconds-=(static_cast<long double>(bank.helper_input_origin_samples)+origin)/config.sample_rate;
                const auto expected=helper::plan(value.helper,value.transfer.modem.carrier_hz,
                    detail::helper_occupied_bandwidth(value),120000000,0,
                    modem::outer_dsss_transmit_gain(value.transfer.modem)).frequency_hz;
                static std::atomic<std::uint64_t> sequence{0};
                auto ordinal=sequence.load(std::memory_order_relaxed);
                do {if(ordinal==UINT64_MAX)return;}
                while(!sequence.compare_exchange_weak(ordinal,ordinal+1,std::memory_order_relaxed));
                helper::AssistanceRequest request;request.expected_helper_hz=expected;request.current_sample=bank.samples;
                request.reference_sequence=ordinal+1;request.maximum_hypotheses=257;
                request.cover_continuous_clock=true;
                request.receiver_input_origin_samples=origin;request.qualify_epoch_timing=true;
                auto plan=helper::plan_assistance(config,prepared.search,reference,request);
                if(!plan.search)continue;
                // A measured dwell can qualify an optional epoch alias; generic
                // symbol zero itself need not coincide with that boundary.
                plan.search->input_sample_origin=origin;plan.search->worker_threads=1;
                plan.search->receiver_bank_count=1;plan.search->receiver_strategy_workspace_bytes=0;
                plan.search->candidate_limit=32;plan.search->track_limit=2;
                plan.search->prefer_streamed_templates=true;
                plan.search->compact_clock_search=original.modem->clock_windowed();
                plan.search->compact_clock_search=helper::prefer_compact_assistance(config,*plan.search);
                auto state=std::make_unique<Bank::HelperAssistance>();
                state->filter=std::move(filter);state->filter_input_origin=input_origin;
                const auto node_allowance=sizeof(Receiver)+16*sizeof(void*)+sizeof(ReceiverList);
                const auto control=state->bytes()+node_allowance;
                constexpr std::size_t publication_margin=64*1024;
                if(control>=cap || publication_margin>=cap-control)continue;
                state->numeric_limit=cap-control-publication_margin;
                set_receiver_container_limit(bank,capacity);
                ReceiverAdmissionStage staged(bank);staged.prepare();auto& receiver=staged.receiver();
                receiver.options=original.options;receiver.options.modem.receiver_strategy=modem::ReceiverStrategy::complex_only;
                receiver.epoch=original.epoch;receiver.family=original.family;receiver.key_tag=original.key_tag;
                receiver.sample_origin=origin;receiver.admitted_at=observation_epoch(bank,value,bank.samples);
                receiver.modem=std::make_unique<modem::StreamingReceiver>(transfer::seeded_config(receiver.options,receiver.epoch),
                    state->numeric_limit,*plan.search);
                // This is an ordinary Robust payload decoder, guided only by
                // the separate tone measurements. It shares the existing bank
                // source quota and keeps every content/recovery option intact.
                // Foreground codec calls are serialized; their independent
                // allocator cap and the global recovery queue remain unchanged.
                receiver.content=std::make_unique<transfer::StreamReceiver>(receiver.options,receiver.epoch,bank.source_quota);
                if(receiver_workspace(receiver)+control>cap)continue;
                state->bounded_initial_search=plan.search->start_offset_seconds &&
                    std::isfinite(*plan.search->start_offset_seconds) && std::isfinite(plan.search->start_uncertainty_seconds) &&
                    plan.search->start_uncertainty_seconds>=0;
                state->publication_floor=bank.samples;
                state->preroll_count=replay?bank.helper_history->count:0;
                state->receiver=&receiver;state->reference=measurement_only(reference);state->anchor_sample=reference.sample;
                state->anchor_phase=reference.phase_radians;state->unwrapped_phase=reference.phase_radians;
                state->anchor_frequency=reference.frequency_hz;
                state->phase_ready=reference.timing_valid && reference.phase_valid && std::isfinite(reference.phase_radians) &&
                    std::isfinite(reference.phase_uncertainty_radians) && reference.phase_uncertainty_radians>=0 &&
                    reference.phase_uncertainty_radians<=.25;
                state->phase_qualified=state->phase_ready;
                staged.charge_receiver();staged.commit();
                bank.working_bytes+=state->bytes();bank.helper_assistance=std::move(state);
                audio::trace::record("receiver_helper_assistance_admitted",[&](auto& f){
                    f.add("sample",bank.samples);f.add("epoch",receiver.epoch);f.add("frequency_lanes",plan.search->hypotheses.size());
                    f.add("input_origin",origin);f.add("preroll_samples",bank.helper_assistance->preroll_count);
                    f.add("filter_delay_samples",bank.helper_assistance->filter.delay());
                    f.add("evidence_epoch",bank.helper_evidence_epoch);
                    f.add("history_bytes",history_bytes);
                    f.add("phase_supported",receiver.modem->phase_assistance_supported());f.add("allowance",cap);
                    f.add("numeric_limit",bank.helper_assistance->numeric_limit);
                    f.add("clock_windowed",receiver.modem->clock_windowed());
                    f.add("content_limit",receiver.options.content_limit);f.add("configured_bit_limit",plan.search->bit_limit);
                    f.add("recovery_enabled",receiver.options.recovery_options.enabled);
                    f.add("recovery_budget_ms",receiver.options.recovery_options.budget.count());
                    f.add("recovery_workspace",receiver.options.recovery_options.workspace_bytes);
                });
                return;
            }catch(const Error&){}catch(const std::bad_alloc&){} // Optional assistance never resets ordinary reception.
        }
    }
    static bool helper_cohort_policy(const Settings& value,const modem::Config& config) {
        return value.helper.power_w>0 && value.helper.frequency!=helper::Frequency::cochannel &&
            !value.transfer.timestamp && !config.symbol_burst.enabled() && !modem::scheduled_bursts(config) &&
            (config.receiver_strategy==modem::ReceiverStrategy::automatic ||
             config.receiver_strategy==modem::ReceiverStrategy::complex_only);
    }
    static bool helper_cohort_receiver(const Bank& bank,const Receiver& receiver)noexcept {
        return bank.helper_cohort && (receiver.helper_flags&Receiver::helper_filtered_input);
    }
    static bool helper_cohort_eligible(const Bank& bank,const Receiver& receiver)noexcept {
        constexpr auto required=Receiver::helper_filtered_input|Receiver::helper_eligible;
        return bank.helper_cohort && (receiver.helper_flags&required)==required &&
            !(receiver.helper_flags&Receiver::helper_departure);
    }
    static void clear_helper_row_transients(Receiver& receiver)noexcept {
        if(receiver.helper_flags&Receiver::helper_departure)
            receiver.helper_flags&=static_cast<std::uint8_t>(~Receiver::helper_eligible);
        receiver.helper_flags&=static_cast<std::uint8_t>(~(Receiver::helper_deferred_error|Receiver::helper_departure));
    }
    void initialize_helper_cohort(Bank& bank,const Settings& value) {
        if(bank.helper_cohort || bank.samples || bank.admissions)return;
        if(value.helper.power_w<=0) {bank.helper_cohort_decline_reason="Helper carrier disabled";return;}
        if(value.helper.frequency==helper::Frequency::cochannel) {
            bank.helper_cohort_decline_reason="cochannel helper cannot qualify an isolated rolling frequency reference";return;
        }
        if(value.transfer.timestamp) {
            bank.helper_cohort_decline_reason="fixed-epoch reception retains its separate helper acquisition path";return;
        }
        if(value.transfer.modem.symbol_burst.enabled() || modem::scheduled_bursts(value.transfer.modem)) {
            bank.helper_cohort_decline_reason="scheduled physical apertures retain their ordinary acquisition policy";return;
        }
        if(!helper_cohort_policy(value,value.transfer.modem)) {
            bank.helper_cohort_decline_reason="the selected ordinary receiver policy does not support helper rolling narrowing";return;
        }
        if(value.transfer.key && modem::symbol_sample_count(value.transfer.modem)>=
                60ULL*value.transfer.modem.sample_rate) {
            bank.helper_cohort_decline_reason="ordinary compact clock-search backend retained; helper rolling narrowing is unsupported";return;
        }
        try {
            const auto& config=value.transfer.modem;
            const auto expected=helper::plan(value.helper,config.carrier_hz,
                detail::helper_occupied_bandwidth(value),120000000,0,
                modem::outer_dsss_transmit_gain(config)).frequency_hz;
            const auto taps=static_cast<std::size_t>(std::ceil(config.sample_rate*.04))|1U;
            constexpr auto allowance=modem::SampledSimulationChannel::helper_cohort_workspace_bound;
            if(taps<3 || taps>4097 || sizeof(Bank::HelperCohort)>allowance ||
               taps>(allowance-sizeof(Bank::HelperCohort))/(sizeof(double)+sizeof(float))) {
                bank.helper_cohort_decline_reason="helper front end exceeds its reserved 7 KiB control allowance";return;
            }
            auto cohort=std::make_unique<Bank::HelperCohort>();
            cohort->filter=helper::PayloadFilter(config.sample_rate,expected);
            // The 40ms Blackman stop filter has a finite transition. Require
            // the full declared RF/audio-clock/profile support to clear it;
            // unsafe configurations retain their ordinary unfiltered path.
            const auto safe=[&](const modem::Config& profile) {
                double rf=0,clock=0;
                if(profile.oscillator_search) {
                    const auto domain=modem::oscillator_pattern_search(profile,
                        transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(profile):0);
                    rf=domain.frequency.requested_half_width_hz;
                    clock=domain.requested_clock_half_width_ppm;
                }
                const auto half=helper::occupied_bandwidth(profile)/2;
                const auto uncertainty=rf+(std::abs(profile.carrier_hz)+half)*clock*1e-6;
                // +/-100Hz is the declared helper tracking range. The filter
                // remains centered on the nominal carrier, including on loss.
                const auto wanted_low=profile.carrier_hz-half-uncertainty;
                const auto wanted_high=profile.carrier_hz+half+uncertainty;
                constexpr double stop_and_transition=325;
                return wanted_low>0 && wanted_high<profile.sample_rate/2. &&
                    (wanted_high<expected-stop_and_transition || wanted_low>expected+stop_and_transition) &&
                    cohort->filter.maximum_amplitude_error(wanted_low,wanted_high)<=.001;
            };
            auto profiles=value.transfer.automatic_receive_profiles?
                tuning::receive_profiles(config,value.transfer.receive_targets_db_hz,
                    value.transfer.receive_pattern_mode,value.transfer.key.has_value()):std::vector<modem::Config>{config};
            if(value.long_message_modem)profiles.push_back(*value.long_message_modem);
            if(!std::all_of(profiles.begin(),profiles.end(),safe)) {
                bank.helper_cohort_decline_reason="full declared payload support fails the fixed helper-filter separation or amplitude bound";return;
            }
            cohort->expected_hz=expected;cohort->filter_origin=cohort->filter.delay();
            // This bounded control and its immutable FIR occupy the channel's
            // already reserved auxiliary allowance. They cannot reduce an
            // ordinary constructor grant, bit store or source quota.
            if(cohort->bytes()>modem::SampledSimulationChannel::helper_cohort_workspace_bound) {
                bank.helper_cohort_decline_reason="helper front end exceeds its reserved 7 KiB control allowance";return;
            }
            bank.helper_cohort=std::move(cohort);
            audio::trace::record("receiver_helper_cohort_frontend",[&](auto& f) {
                f.add("sample",bank.samples);f.add("filter_delay",bank.helper_cohort->filter.delay());
                f.add("expected_hz",expected);f.add("declared_support_guard_hz",325);
                f.add("reserved_bytes",modem::SampledSimulationChannel::helper_cohort_workspace_bound);
                f.add("actual_bytes",bank.helper_cohort->bytes());f.add("cohort_control_bytes",sizeof(Bank::HelperCohort));
                f.add("wanted_amplitude_error_limit",.001);
            });
        }catch(const Error&) {bank.helper_cohort_decline_reason="helper front end has no valid passband or bounded filter geometry";}
        catch(const std::bad_alloc&) {bank.helper_cohort_decline_reason="optional helper front-end allocation unavailable; ordinary reception retained";}
    }
    Bank make_bank(const Settings& value) { return make_bank(value, Bank{}); }
    Bank make_bank(const Settings& value, Bank bank) {
        // The caller's former owner is now moved-from. The private checkpoint
        // exercises capture invalidation at this joined admission boundary.
        if(bank.samples && simulation_media_clock(value))detail::simulation_checkpoint(
            detail::SimulationPhase::bank_refresh_moved,bank.samples,bank.samples,bank.revision);
        initialize_helper_cohort(bank,value);
        initialize_receiver_storage(bank,bank_capacity(value));
        rx_cpu::Context cpu_context(rx_cpu::Stage::rolling);
        // Calendar refresh alone neither invalidates public N/C/coordinate
        // keys nor changes any original receiver grant. Retain the complete
        // joined owner until a genuinely new receiver is found below.
        if(!bank.native_points) {
            const auto capacity=bank_capacity(value);
            if(sizeof(modem::detail::NativePointPool)>capacity-std::min(capacity,bank.working_bytes))
                throw Error("native point pool control exceeds receiver bank");
            bank.native_points=std::make_unique<modem::detail::NativePointPool>(capacity/sizeof(modem::detail::NativePointEntry));
            bank.working_bytes+=bank.native_points->working_bytes();
        }
        const auto prior_admissions=bank.admissions;
        // Optional reusable scratch must not displace a required admission.
        // Preserve it while space suffices; a calendar refresh is not itself
        // cache invalidation. Retry sizing/construction after actual pressure.
        const auto reclaim_scratch=[&](std::size_t minimum=std::numeric_limits<std::size_t>::max()) {
            rx_cpu::Scope cpu_reclaim(rx_cpu::Stage::allocation);
            // This helper updates Bank itself; do not subtract it twice below.
            const auto source_released=drop_exact_source_pool(bank,"admission_pressure")+drop_point_pool(bank,"admission_pressure");
            std::size_t released=0;
            if(source_released+released<minimum && bank.permutations) {
                const auto bytes=bank.permutations->working_bytes();
                trace_permutation_release(bank,"admission_pressure");
                bank.permutations.reset();released+=bytes;
            }
            // Feeds join every reader before admission/reclamation. A pinned
            // row refuses reclamation rather than dropping its reservation.
            if(source_released+released<minimum && bank.source_curves) {
                const auto bytes=bank.source_curves->working_bytes();
                if(bank.source_curves->try_reclaim()){released+=bytes;bank.source_curves.reset();}
            }
            for(auto& receiver:bank.receivers()) {
                if(source_released+released>=minimum)break;
                if(receiver.source_curve_family){released+=sizeof(modem::detail::SourceFamily);receiver.source_curve_family.reset();}
                if(source_released+released>=minimum)break;
                const auto before=receiver.modem->working_bytes();receiver.modem->trim_optional_workspace();
                released+=before-receiver.modem->working_bytes();
            }
            bank.working_bytes-=released;
            if(released)audio::trace::record("receiver_optional_scratch_reclaimed",[&](auto& f) {
                f.add("sample",bank.samples);f.add("bytes",released);f.add("resident",bank.receivers().size());
            });
            return released+source_released;
        };
        bool scheduled_quiet_only=false;
        count_dropped(bank.builds,1);
        {
            std::lock_guard lock(mutex);
            if(bank.revision && bank.revision!=receive_revision)
                throw Error("receiver admission belongs to an obsolete capture revision");
            if(!bank.revision)bank.revision=receive_revision;
        }
        ensure_shared_capture(bank,value);
        audio::trace::record("receiver_bank_build",[&](auto& f) {
            f.add("revision",bank.revision);f.add("build",bank.builds);f.add("sample",bank.samples);
            f.add("resident",bank.receivers().size());f.add("workspace",bank.working_bytes);
            f.add("workspace_limit",bank_capacity(value));f.add("scheduled",modem::scheduled_bursts(value.transfer.modem));
        });
        bank.limited=!bank.oscillator_limit.empty() || !bank.receiver_limit.empty();
        const auto unavailable=[&](const std::string& reason) {
            // Retain the first concrete omission even when a later epoch is
            // quiet or another profile succeeds. No reset/absence is implied.
            if(bank.receiver_limit.empty())bank.receiver_limit=reason;
            bank.limited=bank.coverage_limited=true;
            audio::trace::record("receiver_profile_unavailable",[&](auto& f) {
                f.add("revision",bank.revision);f.add("sample",bank.samples);f.add("reason",reason);
            });
        };
        if(!bank.source_quota)bank.source_quota=std::make_shared<transfer::ReceiveStorageQuota>(
            transfer::ReceiveStorageQuota{transfer::source_storage_limit(value.content_limit),0});
        std::vector<std::optional<Crypto>> keys;
        std::vector<Bytes> fingerprints;
        if (value.permits_plaintext()) keys.emplace_back();
        const auto add = [&](const Crypto& key) {
            const auto fingerprint = key.mac(Bytes{'D','P','-','R','X','-','B','A','N','K'});
            if (std::find(fingerprints.begin(), fingerprints.end(), fingerprint) == fingerprints.end()) {
                fingerprints.push_back(fingerprint); keys.emplace_back(key);
            }
        };
        if (value.transfer.key) add(*value.transfer.key);
        for (const auto& key : value.receive_keys) add(key);
        const auto now = observation_epoch(bank,value,bank.samples);
        if(!bank.public_amplitude_epoch_origin)
            bank.public_amplitude_epoch_origin=static_cast<long double>(now)-
                static_cast<long double>(bank.samples)/value.transfer.modem.sample_rate;
        const auto propagation=!value.simulation && value.transfer.clock_sync?value.transfer.clock_sync->offset_seconds:0;
        const auto center = value.transfer.timestamp ? value.transfer.timestamp : static_cast<std::uint64_t>(std::max(0.,now-propagation));
        std::optional<std::size_t> amplitude_family_count;
        if(value.transfer.modem.symbol_burst.enabled()&&!value.transfer.timestamp) {
            try {
                std::vector<modem::Config> family_profiles;
                for(const auto& key:keys) {
                    const auto rows=value.transfer.automatic_receive_profiles?
                        tuning::receive_profiles(value.transfer.modem,value.transfer.receive_targets_db_hz,
                            value.transfer.receive_pattern_mode,key.has_value()):std::vector<modem::Config>{value.transfer.modem};
                    for(const auto& row:rows) {
                        auto options=value.transfer;options.modem=row;options.key=key;
                        if(value.transfer.modem.fake_hopping&&key)
                            options.modem.fake_hopping=transfer::transmit_hopping(*value.transfer.modem.fake_hopping,options);
                        family_profiles.push_back(transfer::seeded_config(options,center));
                    }
                }
                detail::ReceiverAdmissionContext context;context.simulation_capture_dated=bank.simulation_capture_dated;
                context.sample_origin=bank.samples;context.observed_sample=bank.observed_sample;
                context.public_amplitude_epoch_origin=bank.public_amplitude_epoch_origin;
                context.timing=bank.timing;context.now=now;context.propagation=propagation;
                context.capacity=bank_capacity(value);
                context.capture_owner_bytes=bank.shared_capture?bank.shared_capture->capacity_reservation():0;
                amplitude_family_count=detail::burst_amplitude_family_bank_count(family_profiles,value,context);
            } catch(const Error&) {
                // Keep the former conservative allocation if pure preparation
                // fails. Each actual admission still reports its own error.
            }
        }
        for (std::size_t family=0;family<keys.size();++family) {
            const auto& key=keys[family];
            const auto profiles=value.transfer.automatic_receive_profiles?
                tuning::receive_profiles(value.transfer.modem,value.transfer.receive_targets_db_hz,value.transfer.receive_pattern_mode,key.has_value()):
                std::vector<modem::Config>{value.transfer.modem};
            for(const auto& profile:profiles) {
            auto public_config=profile;public_config.data_key=key;
            const bool public_rolling=modem::symbol_burst_public_rolling_search(public_config) && !value.transfer.timestamp;
            auto epochs = key || profile.symbol_burst.enabled() ?
                drift_candidates(center, value.transfer.search_seconds, key.has_value()) : std::vector<std::uint64_t>{center};
            if(public_rolling) {
                const modem::SymbolBurstPlan public_plan(public_config);
                epochs.insert(epochs.begin(),public_plan.first_slot({center,0}).payload_begin.epoch);
            }
            if(profile.symbol_burst.enabled()) {
                // Admit before the earliest complete arrival, including the
                // one-second refresh fence and the next input subdivision.
                // A unique UTC-grid cohort cannot borrow an adjacent epoch's
                // leading PCM after that PCM has already passed.
                const auto lead=1U+static_cast<unsigned>((plot_size+profile.sample_rate-1)/profile.sample_rate);
                long double latest=static_cast<long double>(center)+value.transfer.search_seconds+lead;
                if(!value.simulation && bank.timing) {
                    const auto& stamp=*bank.timing;
                    const auto delta=static_cast<long double>(stamp.rate_uncertainty_fraction)/profile.sample_rate;
                    const auto tau=static_cast<long double>(stamp.seconds_per_frame);
                    const auto clock=value.transfer.clock_sync?value.transfer.clock_sync->half_window_seconds():
                        value.transfer.search_seconds+1.;
                    if(stamp.quality!=audio::TimingQuality::unavailable && std::isfinite(tau) &&
                       std::isfinite(delta) && delta>=0 && tau>delta && std::isfinite(stamp.uncertainty_seconds) &&
                       stamp.uncertainty_seconds>=0) {
                        const auto age=static_cast<long double>(bank.samples)-bank.observed_sample+plot_size+profile.sample_rate;
                        // Invert the earliest corner of calendar_arrival_map.
                        // Its causal TX center cancels the TX half-lag; retain
                        // the full capture/provider radius and station prior.
                        const auto radius=clock+std::max(stamp.uncertainty_seconds,
                            clock_sync::audio_lag_radius(value.transfer.audio_timing_error_seconds))+.01L/profile.sample_rate;
                        latest=std::max(latest,stamp.utc_seconds+age*(tau+delta)+radius-propagation);
                    }
                }
                if(!std::isfinite(latest) || latest>=std::ldexp(1.L,64)) {
                    unavailable("Symbol_Burst arrival admission exceeds the UTC range");continue;
                }
                const auto last=static_cast<std::uint64_t>(std::ceil(latest));
                const auto first=static_cast<std::uint64_t>(std::min<long double>(latest,
                    static_cast<long double>(center)+value.transfer.search_seconds+1));
                const auto padding_seconds=modem::pattern_pulse_padding_samples(profile)/profile.sample_rate;
                auto eligible=first<padding_seconds?padding_seconds:first;
                const auto residue=(eligible-padding_seconds)%30;
                const auto increment=residue?30-residue:0;
                if(increment<=std::numeric_limits<std::uint64_t>::max()-eligible) {
                    eligible+=increment;
                    for(;eligible<=last;) {
                        epochs.push_back(eligible);
                        if(last-eligible<30)break;
                        eligible+=30;
                    }
                }
            }
            if(key && !value.transfer.timestamp && value.simulation) {
                // A listener can start after hardware settling has begun. Its
                // clock-error window still describes clock uncertainty; the
                // older transmit epochs below cover the physical prefix.
                const auto settling=static_cast<unsigned>(std::ceil(
                    (static_cast<double>(modem::training_sample_count(profile))+
                     static_cast<double>(modem::pattern_pulse_padding_samples(profile)))/profile.sample_rate));
                for(unsigned age=value.transfer.search_seconds+1;age<=value.transfer.search_seconds+settling;++age)
                    if(center>=age)epochs.push_back(center-age);
            }
            // No payload can begin in a scheduled null hop. Prune only these
            // impossible epoch alternatives; retained receivers and their
            // outstanding full-symbol observations remain alive across nulls.
            std::optional<modem::SchedHopSchedule> schedule;
            if(key && profile.scheduled_hopping && profile.scheduled_hopping->enabled() && !profile.scheduled_hopping->continuous())
                schedule.emplace(*profile.scheduled_hopping,*key);
            auto scheduled_profile=profile;
            scheduled_profile.data_key=key;
            const bool packed_schedule=schedule && modem::packed_scheduled_symbols(scheduled_profile);
            if(schedule) {
                // Diagnose impossible waveform geometry even inside a long
                // null frame. Exact nominal arrival and optimistic workspace
                // make this only a necessary test; declared frequency/clock
                // coverage is retained. Success still needs the actual epoch,
                // prior and cohort-workspace checks below. This preflight never
                // admits a receiver or scores any sample.
                try {
                    const auto preflight_begin=Clock::now();
                    auto options=value.transfer;options.modem=profile;options.key=key;
                    if(value.transfer.modem.fake_hopping)
                        options.modem.fake_hopping=transfer::transmit_hopping(*value.transfer.modem.fake_hopping,options);
                    auto ideal=transfer::seeded_config(options,center);
                    // Retain declared oscillator/UTC coverage here. A nominal
                    // center-only preflight cannot diagnose an unavailable
                    // forced decoder during a long keyed null interval.
                    // Geometry preflight must not depend on whether this
                    // arbitrary current second is a keyed null. Actual epoch
                    // and prior admission remain unchanged below.
                    ideal=modem::scheduled_estimate_config(ideal);
                    modem::PatternSearch nominal;nominal.scheduled_capture=bank.shared_capture;nominal.input_sample_origin=bank.samples;nominal.start_offset_seconds=0;
                    nominal.start_uncertainty_seconds=0;nominal.hypotheses={{0,0}};
                    if(ideal.oscillator_search) {
                        const auto correction=transfer::effective_clock_sync(value.transfer)?
                            modem::utc_transmit_rate_limit(ideal):0;
                        const auto declared=modem::oscillator_pattern_search(ideal,correction);
                        nominal.hypotheses=declared.hypotheses;
                        nominal.admitted_frequency_half_width_hz=declared.frequency.requested_half_width_hz;
                        nominal.admitted_clock_half_width_ppm=declared.requested_clock_half_width_ppm;
                        nominal.admitted_clock_cell_half_width_ppm=declared.clock_step_ppm/2;
                        nominal.complex_frequency_coverage=!declared.frequency.limited;
                        nominal.complex_clock_coverage=declared.clock_half_width_ppm>=declared.requested_clock_half_width_ppm;
                    }
                    if(packed_schedule) {
                        const modem::ScheduledSymbolCalendar calendar(ideal);(void)calendar;
                    }
                    const auto strategy=modem::resolve_receiver_strategy(ideal,nominal,bank_capacity(value));
                    audio::trace::record("receiver_profile_preflight",[&](auto& f) {
                        f.add("revision",bank.revision);f.add("family",family);f.add("sample_rate",ideal.sample_rate);
                        f.add("declared_oscillator",ideal.oscillator_search.has_value());
                        f.add("frequency_rate_pairs",nominal.hypotheses.size());f.add("supported",strategy.supported);
                        f.add("backend",modem::receiver_backend_name(strategy.backend));f.add("reason",strategy.reason);
                        f.add("wall_seconds",std::chrono::duration<double>(Clock::now()-preflight_begin).count());
                    });
                    if(!strategy.supported) {unavailable(strategy.reason);continue;}
                } catch(const Error& error) {unavailable(error.what());continue;}
            }
            bool amplitude_horizon_added=false;
            const auto extend_amplitude_horizon=[&](const modem::PatternSearch& search) {
                if(amplitude_horizon_added||key||!search.amplitude_lookahead_epochs)return;
                amplitude_horizon_added=true;
                if(search.amplitude_epoch_stride_seconds>1) {
                    const auto stride=search.amplitude_epoch_stride_seconds;
                    const auto sample_now=*bank.public_amplitude_epoch_origin+
                        static_cast<long double>(bank.samples)/profile.sample_rate;
                    const auto current=static_cast<std::uint64_t>(std::max(0.L,std::floor(sample_now/stride)*stride));
                    const auto end=sample_now+search.amplitude_lookahead_epochs;
                    for(auto candidate=current;candidate<=end;) {
                        if(std::find(epochs.begin(),epochs.end(),candidate)==epochs.end())epochs.push_back(candidate);
                        if(candidate>std::numeric_limits<std::uint64_t>::max()-stride)break;
                        candidate+=stride;
                    }
                    return;
                }
                for(std::size_t ahead=1;ahead<=search.amplitude_lookahead_epochs &&
                        ahead<=std::numeric_limits<std::uint64_t>::max()-center;++ahead)
                    epochs.push_back(center+ahead);
            };
            for (std::size_t epoch_index=0;epoch_index<epochs.size();++epoch_index) {
                const auto epoch=epochs[epoch_index];
                const auto burst_phase=modem::symbol_burst_payload_start_in_epoch(profile,epoch);
                if(profile.symbol_burst.enabled() && !burst_phase) {
                    scheduled_quiet_only=true;
                    continue;
                }
                if(schedule) {
                    if(epoch>std::numeric_limits<std::uint64_t>::max()/profile.sample_rate-1)
                        throw Error("scheduled receive epoch exceeds sample range");
                    const auto first=modem::SchedHopSchedule::hop_containing_sample(epoch*profile.sample_rate,profile.sample_rate);
                    const auto last=modem::SchedHopSchedule::hop_containing_sample((epoch+1)*profile.sample_rate-1,profile.sample_rate);
                    if(schedule->first_active_at_or_after(first)>last ||
                       (packed_schedule && !modem::packed_scheduled_phase_count(scheduled_profile,epoch))) {
                        // Short slots may start after this second even when an
                        // active hop overlaps it. Their exact joint calendar,
                        // not merely active-hop membership, admits epochs.
                        scheduled_quiet_only=true;
                        audio::trace::record("receiver_empty_epoch",[&](auto& f) {
                            f.add("revision",bank.revision);f.add("epoch",epoch);f.add("family",family);
                            f.add("packed",packed_schedule);f.add("symbol_samples",modem::symbol_sample_count(profile));
                        });
                        continue;
                    }
                }
                const auto tag = key ? key->mac(Bytes{'D','P','-','R','X','-','B','A','N','K'}) : Bytes{};
                const auto existing = std::find_if(bank.receivers().begin(), bank.receivers().end(), [&](const auto& receiver) {
                    if(helper_assisted_receiver(bank,receiver))return false;
                    const auto& c=receiver.options.modem;
                    return receiver.key_tag == tag &&
                        ((tag.empty() && !receiver.amplitude_fft_samples && (!profile.symbol_burst.enabled() || public_rolling)) || receiver.epoch == epoch) &&
                        c.spreading_factor==profile.spreading_factor && c.integration_seconds==profile.integration_seconds &&
                        c.dsss_factor==profile.dsss_factor &&
                        (c.dsss_factor==1 || c.outer_dsss_version==profile.outer_dsss_version) &&
                        c.scramble==profile.scramble && c.spreading_mode==profile.spreading_mode &&
                        c.search_arithmetic==profile.search_arithmetic &&
                        c.scheduled_hopping==profile.scheduled_hopping && c.symbol_burst==profile.symbol_burst &&
                        c.frequency_search_multiplier==profile.frequency_search_multiplier &&
                        c.receiver_strategy==profile.receiver_strategy && c.receiver_cpu_budget==profile.receiver_cpu_budget &&
                        c.pulse_shaping==profile.pulse_shaping && c.oscillator_search==profile.oscillator_search;
                });
                if (existing != bank.receivers().end()) {
                    // Replenish the horizon even when today's epoch was
                    // already prepared by an earlier refresh.
                    if(epoch_index==0&&existing->amplitude_fft_samples&&!key) {
                        detail::ReceiverAdmissionContext context;context.simulation_capture_dated=bank.simulation_capture_dated;
                        context.sample_origin=bank.samples;context.observed_sample=bank.observed_sample;
                        context.timing=bank.timing;context.now=now;context.propagation=propagation;
                        context.public_amplitude_epoch_origin=bank.public_amplitude_epoch_origin;
                        context.key_count=keys.size();context.profile_count=profiles.size();
                        context.keyed=false;context.capacity=bank_capacity(value);
                        const auto prepared=detail::prepare_receiver_search(existing->options.modem,value,epoch,context);
                        extend_amplitude_horizon(prepared.search);
                    }
                    continue;
                }
                // Retain optional acquisition across calendar refresh only
                // when even the maximum ordinary constructor grant and all
                // original growth promises fit with its actual bytes charged.
                // Otherwise release it before ordinary sizing/selection.
                const auto control_margin=receiver_control_margin(profile,key.has_value());
                const auto capacity=bank_capacity(value);
                const auto node_margin=sizeof(Receiver)+16*sizeof(void*)+sizeof(ReceiverList);
                const auto maximum_grant=value.dsp_workspace_bytes/2;
                const auto reserve_fits=maximum_grant<=capacity && control_margin<=capacity-maximum_grant &&
                    node_margin<=capacity-maximum_grant-control_margin;
                const auto reserve=reserve_fits?maximum_grant+control_margin+node_margin:capacity;
                if((bank.helper_assistance || bank.helper_history) &&
                   (!reserve_fits || !helper_surplus_available(bank,capacity-reserve,0))) {
                    drop_helper_assistance(bank,"ordinary_admission_priority");drop_helper_history(bank);
                }
                drop_shared_temporal_plans(bank,"new_receiver");
                // Exact source reuse cannot change original constructor sizing.
                // Required new-bank admissions precede every optional grant.
                drop_exact_source_pool(bank,"new_receiver");
                if(bank.exact_source_samples)throw Error("exact source pool still borrowed at receiver admission barrier");
                drop_point_pool(bank,"new_receiver");
                if(bank.exact_source_points)throw Error("point pool still borrowed at receiver admission barrier");
                // Experimental surplus may not change a new receiver's
                // original constructor/worker grant. Admission is a joined barrier.
                if(bank.permutations && (bank.permutations->working_bytes()>256*1024 ||
                   bank.permutations->row_limit()>8)) {
                    const auto bytes=bank.permutations->working_bytes();
                    trace_permutation_release(bank,"new_receiver");
                    bank.permutations.reset();bank.working_bytes-=bytes;
                }
                const auto compact=key && modem::symbol_sample_count(profile)>=60ULL*profile.sample_rate;
                if(bank.working_bytes>=capacity || capacity-bank.working_bytes<=control_margin)reclaim_scratch();
                if(bank.working_bytes>=capacity || capacity-bank.working_bytes<=control_margin) {
                    bank.limited=bank.coverage_limited=true;
                    audio::trace::record("receiver_allocation_refused",[&](auto& f) {
                        f.add("revision",bank.revision);f.add("epoch",epoch);f.add("family",family);
                        f.add("workspace",bank.working_bytes);f.add("capacity",capacity);
                        f.add("control_margin",control_margin);
                    });
                    continue;
                }
                set_receiver_container_limit(bank,capacity);
                ReceiverAdmissionStage staged(bank);
                try {staged.prepare();}
                catch(const std::bad_alloc&) {
                    if(!reclaim_scratch()) {
                        unavailable("receiver node reservation exceeds DSP workspace");continue;
                    }
                    set_receiver_container_limit(bank,capacity);
                    try {staged.prepare();}
                    catch(const std::bad_alloc&) {
                        unavailable("receiver node reservation exceeds DSP workspace");continue;
                    }
                }
                if(bank.working_bytes>=capacity || capacity-bank.working_bytes<=control_margin)reclaim_scratch();
                if(bank.working_bytes>=capacity || capacity-bank.working_bytes<=control_margin) {
                    unavailable("receiver container leaves insufficient constructor headroom");continue;
                }
                auto& receiver=staged.receiver();
                bool cohort_candidate=bank.helper_cohort && helper_cohort_policy(value,profile) && !compact;
                const auto helper_delay=bank.helper_conditioning?bank.helper_conditioning->filter.delay():0;
                receiver.sample_origin=cohort_candidate?
                    bank.helper_cohort->filter_origin+bank.helper_cohort->filter.output_samples():
                    bank.samples-std::min<std::uint64_t>(bank.samples,helper_delay);
                receiver.family=family;
                if(bank.next_point_consumer==UINT64_MAX)throw Error("point pool consumer identity exhausted");
                receiver.point_consumer=bank.next_point_consumer++;
                receiver.prefer_streamed_templates=keys.size()>1 || profiles.size()>1 || epochs.size()>1;
                receiver.options = value.transfer; receiver.options.modem=profile;receiver.options.key = key; receiver.epoch = epoch;
                if(burst_phase)receiver.options.modem.stream_phase_samples=*burst_phase;
                if(value.transfer.modem.fake_hopping && key)
                    receiver.options.modem.fake_hopping=transfer::transmit_hopping(*value.transfer.modem.fake_hopping,receiver.options);
                receiver.admitted_at=now;
                receiver.key_tag = tag;
                auto config = transfer::seeded_config(receiver.options, epoch);
                std::size_t constructor_grant=0;
                try {
                    detail::ReceiverAdmissionContext admission;
                admission.simulation_capture_dated=bank.simulation_capture_dated;
                    admission.sample_origin=receiver.sample_origin;admission.observed_sample=bank.observed_sample;
                    admission.public_amplitude_epoch_origin=bank.public_amplitude_epoch_origin;
                    admission.timing=bank.timing;admission.now=observation_epoch(bank,value,receiver.sample_origin);admission.propagation=propagation;
                    admission.key_count=keys.size();admission.profile_count=profiles.size();admission.capacity=capacity;
                    admission.capture_owner_bytes=bank.shared_capture?bank.shared_capture->capacity_reservation():0;
                    admission.amplitude_family_bank_count=amplitude_family_count;
                    admission.control_margin=control_margin;admission.keyed=key.has_value();
                    admission.prefer_streamed_templates=receiver.prefer_streamed_templates;admission.compact_clock_search=compact;
                    modem::PatternSearch initial_search;initial_search.scheduled_capture=bank.shared_capture;
                    auto prepared=detail::prepare_receiver_search(config,value,epoch,admission,std::move(initial_search));
                    auto search=std::move(prepared.search);
                    auto declared_search=std::move(prepared.declared_search);
                    const auto select=[&] {
                        auto selected=modem::resolve_receiver_strategy(config,search,
                            std::min(value.dsp_workspace_bytes/2,capacity-bank.working_bytes-control_margin));
                        if(!selected.supported && !selected.empty_scheduled_epoch && reclaim_scratch())
                            selected=modem::resolve_receiver_strategy(config,search,
                                std::min(value.dsp_workspace_bytes/2,capacity-bank.working_bytes-control_margin));
                        return selected;
                    };
                    // Resolve the operator's original search first. A future
                    // cohort can need an energy detector even when the initial
                    // cohort was eligible for frequency-conditioned acquisition.
                    auto selection=select();
                    if(cohort_candidate && selection.backend!=modem::ReceiverBackend::existing_complex) {
                        // Auto may require an energy/differential/calendar path.
                        // That original policy receives original unfiltered PCM.
                        cohort_candidate=false;receiver.sample_origin=bank.samples;
                        admission.sample_origin=receiver.sample_origin;
                        admission.now=observation_epoch(bank,value,receiver.sample_origin);
                        prepared=detail::prepare_receiver_search(config,value,epoch,admission);
                        search=std::move(prepared.search);declared_search=std::move(prepared.declared_search);
                        selection=select();
                    }
                    if(bank.helper_conditioning && !bank.helper_conditioning->broad_future) {
                        if(config.receiver_strategy==modem::ReceiverStrategy::existing_only ||
                           config.receiver_strategy==modem::ReceiverStrategy::amplitude_only ||
                           selection.backend==modem::ReceiverBackend::amplitude_energy) {
                            bank.helper_conditioning->broad_future=true;
                        } else {
                            auto plan=conditioned_search(bank,value,config,search,receiver.sample_origin,
                                bank.helper_conditioning->reference);
                            if(!plan.search && search.hypotheses.size()>=512) {
                                if(!bank.helper_conditioning->committed)
                                    throw Error("helper conditional acquisition is no longer qualified");
                                // Once bits are accepted, a wider or unusable
                                // new reference cannot remove future ordinary
                                // admissions. Keep this original search and
                                // the operator's normal backend policy.
                                bank.helper_conditioning->broad_future=true;
                            }
                            if(plan.search && plan.search->hypotheses.size()<search.hypotheses.size()) {
                                search=std::move(*plan.search);
                                declared_search.reset(); // Conditional coverage is reported separately.
                                config.receiver_strategy=modem::ReceiverStrategy::complex_only;
                                selection=select();
                            }
                        }
                    }
                    receiver.clock_window_modeled=prepared.clock_window_modeled;
                    audio::trace::record("receiver_selection",[&](auto& f) {
                        f.add("revision",bank.revision);f.add("epoch",epoch);f.add("family",family);
                        f.add("keyed",key.has_value());f.add("sample_rate",config.sample_rate);
                        f.add("fine_chip_samples",modem::pattern_chip_samples(config));
                        f.add("symbol_samples",modem::symbol_sample_count(config));f.add("dsss_factor",config.dsss_factor);
                        f.add("packed",packed_schedule);f.add("scheduled",selection.scheduled);
                        f.add("backend",modem::receiver_backend_name(selection.backend));f.add("supported",selection.supported);
                        f.add("arithmetic_policy",static_cast<unsigned>(config.search_arithmetic));
                        f.add("frequency_rate_pairs",search.hypotheses.size());f.add("initial_symbols",search.initial_stream_symbols);
                        f.add("timing_qualified",search.qualified_start_window.has_value());
                        f.add("amplitude_timing_qualified",search.amplitude_start_window.has_value());
                        f.add("amplitude_lookahead_epochs",search.amplitude_lookahead_epochs);
                        if(search.amplitude_start_window) {
                            f.add("amplitude_arrival_origin",search.amplitude_start_window->epoch_origin_samples);
                            f.add("amplitude_arrival_radius",search.amplitude_start_window->half_width_samples);
                        }
                        f.add("shared_hop_calendar",transfer::shared_hop_calendar(config));
                        f.add("calendar_default_policy",transfer::shared_hop_calendar(config) && !value.transfer.clock_sync);
                        if(bank.timing) {
                            f.add("capture_timestamp_radius_seconds",bank.timing->uncertainty_seconds);
                            f.add("capture_rate_uncertainty",bank.timing->rate_uncertainty_fraction);
                        }
                        if(search.start_offset_seconds)f.add("start_offset",*search.start_offset_seconds);
                        f.add("start_radius",search.start_uncertainty_seconds);
                        if(search.qualified_start_window) {
                            f.add("arrival_origin",search.qualified_start_window->epoch_origin_samples);
                            f.add("arrival_scale",search.qualified_start_window->phase_scale);
                            f.add("arrival_radius",search.qualified_start_window->half_width_samples);
                        }
                        f.add("canonical_phases",selection.scheduled_canonical_phases);
                        f.add("timing_positions_allowance",selection.scheduled_timing_positions);
                        f.add("full_frequency_coverage",selection.full_frequency_coverage);
                        f.add("full_clock_coverage",selection.full_clock_coverage);
                        f.add("workspace_share",selection.selected_workspace_bytes);f.add("cohort_banks",selection.selected_bank_count);f.add("reason",selection.reason);
                    });
                    if(selection.empty_scheduled_epoch) {scheduled_quiet_only=true;continue;}
                    if(!selection.supported) {unavailable(selection.reason);continue;}
                    // Additional epochs exist solely for amplitude arrival windows.
                    // Auto's continuous complex receiver remains a singleton.
                    if(!key && (!profile.symbol_burst.enabled() || public_rolling) && epoch_index>0 && (selection.backend!=modem::ReceiverBackend::amplitude_energy||
                            (!search.amplitude_start_window && search.amplitude_epoch_stride_seconds==1)))continue;
                    if(selection.backend==modem::ReceiverBackend::amplitude_energy) {
                        if(search.amplitude_epoch_stride_seconds>1)bank.public_amplitude_windows=true;
                        extend_amplitude_horizon(search);
                        if(search.amplitude_epoch_stride_seconds>1 && epoch_index==0) {
                            const auto stride=search.amplitude_epoch_stride_seconds;
                            const auto sample_now=*bank.public_amplitude_epoch_origin+
                                static_cast<long double>(bank.samples)/config.sample_rate;
                            const auto anchor=static_cast<std::uint64_t>(std::max(0.L,std::floor(sample_now/stride)*stride));
                            if(epoch!=anchor)continue; // Original center was only Auto's complex preflight.
                        }
                    }
                    amplitude_metadata(receiver,config,search,selection);
                    if(declared_search && !(selection.supported && selection.full_frequency_coverage && selection.full_clock_coverage))
                        note_oscillator_coverage(bank,*declared_search);
                    std::size_t minimum_reclaim=std::numeric_limits<std::size_t>::max();
                    std::size_t failed_native_loan_grant=0;
                    const auto construct=[&] {
                        minimum_reclaim=std::numeric_limits<std::size_t>::max();
                        failed_native_loan_grant=0;
                        rx_cpu::Scope cpu_construct(rx_cpu::Stage::setup);
                        const auto grant=[&] {
                            const auto allocation=std::min(value.dsp_workspace_bytes/2,capacity-bank.working_bytes-control_margin);
                            return selection.backend==modem::ReceiverBackend::existing_complex && !(selection.scheduled && !packed_schedule)?allocation:
                                std::min(allocation,selection.selected_workspace_bytes);
                        };
                        const auto ordinary_grant=grant();
                        auto loan_grant=ordinary_grant;
                        const auto factory=[&](std::size_t actual_grant) {
                            constructor_grant=actual_grant;
                            return std::make_unique<modem::StreamingReceiver>(config,actual_grant,search);
                        };
                        try {
                            // The real backing leaves the bank before its byte loan
                            // grows. Ordinary/fallback construction receives no credit.
                            modem::detail::NativePeakConstruction construction(bank.native_peaks.get(),
                                modem::PatternSearch{}.candidate_limit,&bank.working_bytes);
                            loan_grant=grant();
                            construction.set_grant_increase(loan_grant-ordinary_grant);
                            auto constructed=factory(loan_grant);
                            return constructed;
                        }catch(const modem::detail::NativePeakConstructionFallback& shortage) {
                            // The shortfall belongs to the failed real peak-loan
                            // core. Only an achievable increase can target reclaim;
                            // the ordinary fallback still gets its original grant.
                            const auto additional=shortage.additional_required_bytes;
                            const auto maximum=selection.backend==modem::ReceiverBackend::existing_complex &&
                                !(selection.scheduled && !packed_schedule)?value.dsp_workspace_bytes/2:
                                std::min(value.dsp_workspace_bytes/2,selection.selected_workspace_bytes);
                            if(additional && loan_grant<=maximum && additional<=maximum-loan_grant) {
                                minimum_reclaim=additional;failed_native_loan_grant=loan_grant;
                            }
                            return factory(ordinary_grant);
                        }
                    };
                    try {receiver.modem=construct();}
                    catch(const Error& error) {
                        const auto known=minimum_reclaim!=std::numeric_limits<std::size_t>::max();
                        const auto native_shortfall=minimum_reclaim;
                        const auto before_reclaim=bank.working_bytes;
                        const auto freed=reclaim_scratch(minimum_reclaim);
                        audio::trace::record("receiver_constructor_workspace_retry",[&](auto& f) {
                            f.add("revision",bank.revision);f.add("sample",bank.samples);f.add("epoch",epoch);
                            f.add("native_shortfall_known",known);f.add("native_shortfall_bytes",known?native_shortfall:0);
                            f.add("failed_native_loan_grant",failed_native_loan_grant);
                            f.add("reclaim_all",minimum_reclaim==std::numeric_limits<std::size_t>::max());
                            f.add("requested_reclaim_bytes",minimum_reclaim);f.add("freed_bytes",freed);
                            f.add("before_bank_bytes",before_reclaim);f.add("after_bank_bytes",bank.working_bytes);
                            f.add("original_error",std::string(error.what()));
                        });
                        if(!freed)throw;
                        // Repeat the real backing transfer, not just the ordinary
                        // factory. Required state and the single retry are unchanged.
                        receiver.modem=construct();
                    }
                    if(cohort_candidate && receiver.modem->clock_windowed()) {
                        // Selection policy can fall back inside construction.
                        // Rebuild the untouched original raw-input dates before
                        // this row observes even its first physical sample.
                        receiver.modem.reset();cohort_candidate=false;receiver.sample_origin=bank.samples;
                        admission.sample_origin=receiver.sample_origin;
                        admission.now=observation_epoch(bank,value,receiver.sample_origin);
                        prepared=detail::prepare_receiver_search(config,value,epoch,admission);
                        search=std::move(prepared.search);declared_search=std::move(prepared.declared_search);
                        selection=select();receiver.clock_window_modeled=prepared.clock_window_modeled;
                        receiver.modem=construct();
                    }
                } catch(const Error& error) {
                    unavailable(error.what());continue;
                }
                bank.coverage_limited |= receiver.modem->local_clock_fallback();
                receiver.content=std::make_unique<transfer::StreamReceiver>(receiver.options,receiver.epoch,bank.source_quota);
                staged.charge_receiver();
                if(bank.working_bytes>capacity)throw Error("receiver construction exceeds configured DSP workspace");
                bind_native_storage(bank,receiver,capacity);
                if (bank.working_bytes > bank_capacity(value))
                    throw Error("key and epoch receiver bank exceeds the configured DSP workspace");
                staged.commit();
                if(cohort_candidate) {
                    auto& cohort=*bank.helper_cohort;
                    const auto symbol=modem::symbol_sample_count(config);
                    std::size_t group=0;
                    // Keep one shared overflow group. Its shortest symbol
                    // period and earliest armed deadline cover every overflow
                    // profile without disabling any ordinary row or helper.
                    constexpr auto overflow=std::tuple_size_v<decltype(cohort.groups)>-1;
                    for(;group<std::min(cohort.group_count,overflow);++group)
                        if(cohort.groups[group].family==receiver.family && cohort.groups[group].symbol_samples==symbol)break;
                    if(group==std::min(cohort.group_count,overflow)) {
                        if(cohort.group_count<overflow)cohort.groups[cohort.group_count++]={receiver.family,symbol,0,false};
                        else {
                            group=overflow;auto& shared=cohort.groups[group];
                            if(cohort.group_count==overflow) {
                                shared={receiver.family,symbol,0,false};++cohort.group_count;
                            } else {
                                shared.symbol_samples=std::min(shared.symbol_samples,symbol);
                                const auto due=detail::helper_audit_deadline(bank.samples,config.sample_rate,
                                    shared.symbol_samples);
                                if(shared.due)shared.due=std::min(shared.due,due);
                            }
                        }
                    }
                    receiver.helper_flags=Receiver::helper_filtered_input|Receiver::helper_eligible;
                    receiver.helper_audit_group=static_cast<std::uint8_t>(group);
                }
                ++bank.admissions;
                audio::trace::record("receiver_bank_admitted",[&](auto& f) {
                    f.add("revision",bank.revision);f.add("epoch",epoch);f.add("family",family);
                    f.add("sample",bank.samples);f.add("resident",bank.receivers().size());
                    f.add("workspace",bank.working_bytes);f.add("admissions",bank.admissions);
                    f.add("constructor_grant",constructor_grant);
                });
                bank.peak_receivers=std::max(bank.peak_receivers,bank.receivers().size());
            }
            }
        }
        if(bank.temporal_plans && bank.admissions==prior_admissions)
            audio::trace::record("receiver_shared_temporal_plans_retained",[&](auto& f) {
                f.add("sample",bank.samples);f.add("bytes",bank.temporal_plans->working_bytes());
                f.add("workers",bank.temporal_plans->workers());f.add("resident",bank.receivers().size());
                f.add("admissions",bank.admissions);f.add("retirements",bank.retirements);
                f.add("reason","no_new_receiver");
            });
        if(bank.helper_cohort && !bank.samples && std::none_of(bank.receivers().begin(),bank.receivers().end(),
            [&](const auto& receiver){return helper_cohort_receiver(bank,receiver);})) {
            bank.helper_cohort.reset();
            bank.helper_cohort_decline_reason="Auto retained an ordinary energy or compact backend; helper rolling narrowing is unsupported";
        }
        bank.created_at = now;
        bank.refreshed_sample=bank.samples;
        if(bank.receivers().empty() && !(scheduled_quiet_only && !bank.coverage_limited) && bank.receiver_limit.empty())
            unavailable("no receive profile fits the configured DSP workspace");
        // Keep an unavailable bank, including its physical sample coordinate.
        // Retest at normal epoch-admission fences or a configuration change,
        // not by throwing away input and rebuilding on every audio callback.
        return bank;
    }
    void refresh_bank(Bank& bank, const Settings& value) {
        rx_cpu::Scope cpu_scope(rx_cpu::Stage::rolling);
        // Receiver epoch admission is independent of transmit preparation.
        // Both hardware and simulation retain only observed acquisition state.
        if(fixed_epoch_admission(bank,value))return;
        const auto now = observation_epoch(bank,value,bank.samples);
        const bool new_epoch=bank.burst_resume_pending || std::floor(now)!=std::floor(bank.created_at) ||
            (bank.public_amplitude_windows && bank.samples-bank.refreshed_sample>=value.transfer.modem.sample_rate);
        if (!new_epoch && !bank.retirement_pending) return;
        bank.retirement_pending=false;
        const auto retired=std::erase_if(bank.receivers(),[&](const auto& receiver){
            // Short streams can hold fewer than one output chunk throughout
            // the physical absence window. Their admitted clock must survive
            // epoch refresh even before any chunk updates last_confident_end.
            if(receiver.modem->synchronized())return false;
            // Slow decoding can leave captured/simulated PCM behind wall time. An
            // expired wall-clock hint is not evidence that its original
            // acquisition window has actually competed across every start,
            // carrier and clock hypothesis. Keep it until that work finishes.
            if(!receiver.modem->initial_search_complete())return false;
            if((bank.burst_admission_frozen || bank.burst_resume_pending) && value.transfer.modem.symbol_burst.enabled() &&
               !receiver.modem->symbol_burst_tracking_active()) {
                // A frozen Burst receiver cannot discover an unrelated first
                // start. Once its original search really finished, its empty
                // context can retire without imposing wall-clock aging.
                const auto frontier=receiver.modem->delayed_processing_frontier();
                if(!frontier || *frontier>=bank.samples-receiver.sample_origin)return true;
                bank.retirement_pending=true;return false;
            }
            const auto& config=receiver.options.modem;
            const auto symbol=static_cast<double>(modem::symbol_sample_count(config))/config.sample_rate;
            const auto prefix=(static_cast<double>(modem::training_sample_count(config))+
                static_cast<double>(modem::pattern_pulse_padding_samples(config)))/config.sample_rate;
            const auto allowance=value.transfer.search_seconds+1.;
            // Rejecting a completed search no longer removes the only key for
            // a whole message: subsequent symbol-start seconds are independent.
            // Unconfirmed epochs need one complete symbol plus clock coverage.
            // Retire confirmed receivers only after their physical search has
            // ended the stream using the fixed complete-symbol absence rule.
            const auto propagation=!value.simulation && value.transfer.clock_sync?value.transfer.clock_sync->offset_seconds:0;
            const auto chip=modem::pattern_chip_samples(config);
            const bool sample_clock=bank.public_amplitude_windows && receiver.amplitude_geometry &&
                receiver.amplitude_geometry->clock_hypotheses==1 && !receiver.options.key &&
                chip && !(chip%2) && config.sample_rate%(chip/2)==0;
            const auto age=static_cast<long double>(bank.samples-std::min(bank.samples,receiver.sample_origin))/config.sample_rate;
            const auto radius=std::max(allowance,33.);
            const bool expired=sample_clock?(receiver.last_confident_end?
                age>static_cast<long double>(receiver.last_confident_end)/config.sample_rate+6+radius:
                age>prefix+symbol+radius):receiver.last_confident_end?now>receiver.last_confident_at+6.+allowance:
                receiver.key_tag.empty()?receiver.modem->clock_windowed() && now>receiver.admitted_at+prefix+symbol+allowance:
                !value.transfer.timestamp && now>static_cast<double>(receiver.epoch)+propagation+prefix+symbol+allowance;
            if(!expired)return false;
            if(const auto committed=receiver.modem->delayed_processing_frontier()) {
                if(receiver.sample_origin>bank.samples || *committed>bank.samples-receiver.sample_origin)
                    throw Error("delayed receiver retirement frontier exceeds capture ingress");
                if(*committed<bank.samples-receiver.sample_origin) {
                    bank.retirement_pending=true;return false;
                }
            }
            return true;
        });
        bank.retirements+=retired;
        if(bank.helper_assistance && std::none_of(bank.receivers().begin(),bank.receivers().end(),[&](const auto& r){
            return helper_assisted_receiver(bank,r);
        })) {
            bank.helper_assistance.reset(); // Its receiver was retired by the original physical policy.
        }
        bank.reception_history_dirty|=retired!=0;
        if(retired)audio::trace::record("receiver_bank_retired",[&](auto& f) {
            f.add("revision",bank.revision);f.add("sample",bank.samples);f.add("retired",retired);
            f.add("resident",bank.receivers().size());f.add("retirements",bank.retirements);
        });
        bank.working_bytes=sizeof(detail::ReceptionHistory)+sizeof(bank.source_curves)+sizeof(bank.exact_source_samples)+
            sizeof(bank.exact_source_generation)+sizeof(bank.next_exact_source_trace)+sizeof(bank.exact_source_points)+
            sizeof(bank.point_source_generation)+sizeof(bank.next_point_consumer)+sizeof(bank.next_point_source_trace)+sizeof(bank.permutations)+sizeof(bank.temporal_plans)+
            sizeof(bank.deferred_input)+sizeof(bank.input_queued_at)+sizeof(bank.shared_capture)+sizeof(bank.future_capture)+
            (bank.shared_capture?bank.shared_capture->capacity_reservation():0)+bank.deferred_input.working_bytes()+
            (bank.source_curves?bank.source_curves->working_bytes():0)+
            (bank.exact_source_samples?bank.exact_source_samples->working_bytes():0)+
            (bank.exact_source_points?bank.exact_source_points->working_bytes():0)+(bank.permutations?bank.permutations->working_bytes():0)+
            (bank.temporal_plans?bank.temporal_plans->working_bytes():0);
        bank.working_bytes+=sizeof(bank.receiver_allocations)+sizeof(detail::ReceiverAllocationAccount)+
            bank.receiver_storage_bytes()+bank.receiver_allocations->allocated_bytes()+sizeof(bank.native_peaks)+
            sizeof(modem::detail::NativePeakWorkspace)+sizeof(bank.native_points)+native_pool_workspace(bank)+
            sizeof(bank.helper_conditioning)+sizeof(bank.helper_conditioning_attempted)+sizeof(bank.helper_conditioning_probe)+
            sizeof(bank.helper_last_frequency_sample)+sizeof(bank.helper_fallback_replay_bytes)+sizeof(bank.helper_fallback_budget)+sizeof(bank.helper_cohort)+sizeof(bank.helper_cohort_decline_reason)+65536;
        for(const auto& receiver:bank.receivers())bank.working_bytes+=receiver_workspace(receiver);
        if(bank.helper_assistance)bank.working_bytes+=bank.helper_assistance->bytes();
        if(bank.helper_history)bank.working_bytes+=bank.helper_history->bytes();
        if(bank.helper_conditioning)bank.working_bytes+=bank.helper_conditioning->bytes();
        bank.working_bytes+=bank.helper_fallback_replay_bytes;
        if(new_epoch && !bank.burst_admission_frozen) {
            bank.burst_resume_pending=false;
            trace_receiver_acquisition_work(bank,false,"refresh_before_move");
            bank=make_bank(value,std::move(bank));
        }
    }
    void append_signal(SignalUpdate event) {
        detail::append_signal_event(current.signals,std::move(event),maximum_events);
    }
    void helper_unavailable(std::uint64_t version,const std::string& reason) {
        std::lock_guard lock(mutex);
        if(generation!=version)return;
        current.helper_status="Helper unavailable: "+reason;
        ++current.sequence;
    }
    double helper_search_width(const Settings& value,std::uint64_t version) {
        double requested=20;
        if(value.transfer.modem.oscillator_search) {
            const auto search=modem::oscillator_pattern_search(value.transfer.modem,
                transfer::effective_clock_sync(value.transfer)?modem::utc_transmit_rate_limit(value.transfer.modem):0);
            requested=std::max(requested,search.frequency.requested_half_width_hz);
        }
        if(requested>100) {
            std::lock_guard lock(mutex);
            if(generation==version)current.helper_status="Helper tracking limited to +/-100 Hz; ordinary Robust reception retained";
        }
        return std::min(100.,requested);
    }
    helper::Options sampled_helper(const Settings& value,std::uint32_t rate,std::uint64_t version) {
        auto options=value.helper;
        if(options.power_w<=0)return options;
        try {
            helper::validate(options);
            if(!rate)throw Error("Helper Carrier simulation requires a positive logical sample rate");
        }
        catch(const Error& error) {helper_unavailable(version,error.what());options.power_w=0;}
        return options;
    }
    struct HelperReception {
        std::uint64_t id=0,clear_generation=0;
        bool complete=false;
        double frequency=0,snr=0;
        std::optional<helper::Metadata> metadata;
    };
    struct HelperWorkspace {
        helper::Tracker tracker;
        helper::Receiver receiver;
        HelperReception reception;
        std::array<std::optional<helper::Reference>,4> references;
        std::size_t next_reference=0;
        std::uint64_t generation=0,next_raw=0,first_reference_end=UINT64_MAX;
        double first_reference_support=0;
        std::uint32_t rate=0;
        double expected=0;
        HelperWorkspace(double frequency,double width,std::vector<Crypto> keys,
                        std::uint32_t sample_rate,std::uint64_t version,
                        std::optional<helper::OccupiedBand> ordinary_band):
            tracker(frequency,width),receiver(frequency,width,std::move(keys),ordinary_band),
            generation(version),rate(sample_rate),expected(frequency) {}
    };
    // One owner for both modes, including handoff. Auxiliary storage was already
    // reserved by the original sampled channel; ordinary receiver/audio grants
    // are unchanged. Failed try-locks discard helper work, never payload input.
    static_assert(helper::Tracker::workspace_bound+helper::Receiver::workspace_bound+
        sizeof(HelperWorkspace)+sizeof(execution::Mutex)+sizeof(helper::Reference)+64<=
        modem::SampledSimulationChannel::auxiliary_workspace_bound);
    execution::Mutex helper_mutex;
    std::unique_ptr<HelperWorkspace> helper_workspace;
    bool configure_helper(const Settings& value,std::uint64_t version,std::uint32_t physical_rate) {
        {std::lock_guard lock(mutex);if(generation!=version||!current.running)return false;}
        const auto width=value.helper.power_w>0?helper_search_width(value,version):0;
        // Format setup is serialized; raw callbacks below still yield with a
        // try-lock. A handoff must not permanently lose the new helper setup.
        std::unique_lock lock(helper_mutex);
        {std::lock_guard state_lock(mutex);if(generation!=version||!current.running)return false;}
        helper_workspace.reset();
        if(value.helper.power_w<=0)return false;
        try {
            const auto plan=helper::plan(value.helper,value.transfer.modem.carrier_hz,
                detail::helper_occupied_bandwidth(value),physical_rate,
                0,modem::outer_dsss_transmit_gain(value.transfer.modem));
            std::vector<Crypto> keys;keys.reserve(8);
            std::vector<Bytes> fingerprints;fingerprints.reserve(8);
            const auto add=[&](const Crypto& key) {
                if(keys.size()==8)return;
                const auto fingerprint=key.mac(Bytes{'D','P','-','H','E','L','P','E','R','-','R','X'});
                if(std::find(fingerprints.begin(),fingerprints.end(),fingerprint)==fingerprints.end()) {
                    keys.push_back(key);fingerprints.push_back(fingerprint);
                }
            };
            if(value.transfer.key)add(*value.transfer.key);
            for(const auto& key:value.receive_keys)add(key);
            helper_workspace=std::make_unique<HelperWorkspace>(plan.frequency_hz,width,
                std::move(keys),physical_rate,version,detail::helper_occupied_receive_band(value));
            helper_workspace->tracker.push({},physical_rate);
            return true;
        } catch(const std::exception& error) {
            helper_workspace.reset();lock.unlock();helper_unavailable(version,error.what());return false;
        }
    }
    void reset_helper(std::uint64_t version) {
        std::unique_lock lock(helper_mutex,std::try_to_lock);
        if(lock.owns_lock() && helper_workspace && helper_workspace->generation==version) {
            helper_workspace->tracker.reset();helper_workspace->receiver.reset();
            helper_workspace->references={};helper_workspace->reception={};helper_workspace->next_raw=0;
            helper_workspace->first_reference_end=UINT64_MAX;
        }
    }
    std::optional<helper::Reference> helper_reference(std::uint64_t frontier,std::uint32_t logical_rate,
                                                   std::uint64_t version) {
        std::unique_lock lock(helper_mutex,std::try_to_lock);
        if(!lock.owns_lock()||!helper_workspace||helper_workspace->generation!=version||!logical_rate)return {};
        const auto& work=*helper_workspace;
        const auto raw_frontier=static_cast<long double>(frontier)*work.rate/logical_rate;
        const helper::Reference* selected=nullptr;
        // frontier is exclusive; Reference.sample is the last raw sample used.
        for(const auto& candidate:work.references)if(candidate && candidate->sample<raw_frontier &&
            (!selected||candidate->sample>selected->sample))selected=&*candidate;
        if(!selected)return {};
        auto result=measurement_only(*selected);
        const auto mapped=std::ceil(static_cast<long double>(result.sample)*logical_rate/work.rate);
        if(mapped>frontier)return {};
        if(result.frequency_sample) {
            if(result.frequency_sample>result.sample)return {};
            // Round the separate measurement date backward, so rate mapping
            // cannot make an older frequency estimate appear younger.
            result.frequency_sample=static_cast<std::uint64_t>(std::floor(
                static_cast<long double>(result.frequency_sample)*logical_rate/work.rate));
            if(!result.frequency_sample)return {}; // Preserve zero's legacy same-date meaning.
        }
        const auto dt=static_cast<double>(mapped/logical_rate-
            static_cast<long double>(result.sample)/work.rate);
        result.phase_radians=std::remainder(result.phase_radians+2*std::numbers::pi*result.frequency_hz*dt,
                                           2*std::numbers::pi);
        result.phase_uncertainty_radians+=2*std::numbers::pi*result.frequency_uncertainty_hz*std::abs(dt);
        result.sample=static_cast<std::uint64_t>(mapped);result.sample_rate=logical_rate;
        return result;
    }
    void observe_helper(std::span<const float> samples,std::uint32_t rate,
                        std::uint64_t first_raw,std::uint64_t version) {
        std::unique_lock helper_lock(helper_mutex,std::try_to_lock);
        if(!helper_lock.owns_lock()||!helper_workspace||helper_workspace->generation!=version)return;
        auto& work=*helper_workspace;
        if(first_raw!=work.next_raw||rate!=work.rate) {
            work.tracker.reset();work.receiver.reset();work.references={};work.reception={};
            work.first_reference_end=UINT64_MAX;
        }
        std::optional<helper::Reference> reference;
        try {
            work.tracker.push(samples,rate);
            // Observe the first fit independently of provider callback size.
            // The caller still receives the last update per original callback.
            const auto step=std::max<std::size_t>(1,rate/25);
            for(std::size_t at=0;at<samples.size();) {
                const auto count=std::min(step,samples.size()-at);
                work.receiver.push(samples.subspan(at,count),rate,first_raw+at);at+=count;
                if(auto update=work.receiver.take_update()) {
                    if(work.first_reference_end==UINT64_MAX) {
                        work.first_reference_end=update->sample;
                        work.first_reference_support=update->phase_valid?.04:.144;
                    }
                    if(update->metadata)work.reception.metadata=update->metadata;
                    reference=std::move(update);
                }
            }
        } catch(const std::exception& error) {
            work.tracker.reset();work.receiver.reset();work.references={};work.first_reference_end=UINT64_MAX;
            helper_lock.unlock();helper_unavailable(version,error.what());return;
        }
        work.next_raw=first_raw+samples.size();
        if(reference) {
            work.references[work.next_reference++%work.references.size()]=measurement_only(*reference);
        }
        const auto observation=work.tracker.take_update();
        if(!observation)return;
        auto& reception=work.reception;
        const auto expected=work.expected;
        std::lock_guard lock(mutex);
        if(generation!=version || !current.running)return;
        if(reception.clear_generation!=receive_clear_generation) {
            reception={};reception.clear_generation=receive_clear_generation;
        }
        if(!observation->present) {
            current.helper_frequency_offset_hz.reset();current.helper_snr_db.reset();
            if(current.helper_status.empty())current.helper_status="Helper carrier absent; ordinary Robust reception retained";
            if(observation->absent_seconds+1e-9>=6)work.first_reference_end=UINT64_MAX;
            if(reception.id && observation->absent_seconds+1e-9>=6) {
                if(!reception.complete) {
                    SignalUpdate event;event.helper_carrier=true;event.id=reception.id;
                    event.frequency_hz=reception.frequency;event.snr_db=reception.snr;
                    event.text="Helper carrier observation ended";event.complete=true;event.revision=receive_revision;
                    add_signal(std::move(event));++current.sequence;
                }
                reception.id=0;reception.complete=false;reception.metadata.reset();
            }
            return;
        }
        if(!reception.id)reception.id=next_signal++;
        reception.complete=reception.complete||observation->confirmed;
        reception.frequency=observation->frequency_hz;reception.snr=observation->snr_db;
        const auto offset=observation->frequency_hz-expected;
        current.helper_frequency_offset_hz=offset;current.helper_snr_db=observation->snr_db;
        if(current.helper_status.empty())current.helper_status="Helper carrier present; acquisition assistance not active";
        SignalUpdate event;event.helper_carrier=true;event.id=reception.id;event.frequency_hz=observation->frequency_hz;
        event.snr_db=observation->snr_db;event.complete=reception.complete;event.revision=receive_revision;
        std::ostringstream text;text.imbue(std::locale::classic());
        text<<"Tone SNR "<<std::fixed<<std::setprecision(1)<<observation->snr_db<<" dB; offset "<<std::showpos<<offset<<" Hz";
        text<<std::noshowpos;
        if(reception.metadata) {
            const auto& metadata=*reception.metadata;
            if(metadata.kind==helper::MetadataKind::callsign)
                text<<"; callsign "<<terminal_text(Bytes(metadata.callsign.begin(),metadata.callsign.end()));
            else text<<"; "<<(metadata.kind==helper::MetadataKind::encrypted_time?"encrypted advisory":"clear advisory")<<" time "
                     <<std::setprecision(5)<<static_cast<double>(metadata.time_ticks)/65536;
        }
        if(reference&&reference->timing_valid)text<<"; dwell timing tracked";
        event.text=text.str();add_signal(std::move(event));++current.sequence;
    }
    std::size_t recovery_bytes() const {
        const auto bytes=[](const RecoveryTask& task) {
            return task.retained_bytes+task.job->working_bytes();
        };
        std::size_t total=0;
        for(const auto& task:recoveries)total+=bytes(*task);
        if(active_recovery && std::find(recoveries.begin(),recoveries.end(),active_recovery)==recoveries.end())
            total+=bytes(*active_recovery);
        return total;
    }
    std::vector<SignalUpdate> unavailable_recovery_events() {
        std::vector<SignalUpdate> events;
        for(const auto& task:recoveries)if(!task->obsolete) {
            auto event=task->event;event.recovery_progress=task->job->progress();
            event.recovery_progress.state=transfer::RecoveryState::unavailable;
            event.sequence=next_event++;events.push_back(std::move(event));
        }
        return events;
    }
    std::size_t coordinator_recovery_count() const {
        return recoveries.size()+static_cast<std::size_t>(active_recovery &&
            std::find(recoveries.begin(),recoveries.end(),active_recovery)==recoveries.end());
    }
    std::size_t recovery_reserved_bytes() const {
        std::size_t total=0;
        for(const auto& task:recoveries)total+=task->reserved_bytes;
        if(active_recovery && std::find(recoveries.begin(),recoveries.end(),active_recovery)==recoveries.end())
            total+=active_recovery->reserved_bytes;
        return total;
    }
    void invalidate_recoveries(const SignalUpdate* replacement=nullptr) {
        const auto obsolete=[&](const auto& task) {
            return !replacement || (task->event.id==replacement->id && task->event.revision<replacement->revision) ||
                std::find(replacement->superseded_ids.begin(),replacement->superseded_ids.end(),task->event.id)!=replacement->superseded_ids.end();
        };
        for(const auto& task:recoveries)if(obsolete(task)) {
            task->obsolete=true;task->stop.request_stop();
        }
        std::erase_if(recoveries,[&](const auto& task){return task->obsolete;});
        changed.notify_all();
    }
    void queue_recovery(transfer::Received result,SignalUpdate& event) {
        if(!result.recovery || !result.stream_complete || result.content_validated)return;
        event.recovery_progress=result.recovery->progress();
        // Plot samples remain in the live diagnostic presentation. Background
        // recovery retains only the hard-bit capture and scalar diagnostics.
        result.diagnostics.waveform=std::vector<float>{};
        result.diagnostics.constellation=std::vector<std::complex<double>>{};
        const auto retained=sizeof(RecoveryTask)+4096+result.raw_bits.capacity()+result.error.capacity()+
            result.content.message.data.capacity()+result.content.message.filename.capacity()+
            result.content.message.callsign.capacity()+result.content.message.grid.capacity()+
            event.text.capacity()+event.raw_bits.capacity()+event.reception_id.capacity()+
            event.superseded_ids.capacity()*sizeof(std::uint64_t);
        const auto memory=result.recovery->workspace_bound()+retained;
        const auto used=recovery_reserved_bytes()+(staged_receives?staged_receives->recovery_reserved_bytes():0),limit=recovery_workspace_limit;
        if(coordinator_recovery_count()+(staged_receives?staged_receives->recovery_count():0)>=maximum_recovery_jobs ||
           memory>limit || used>limit-memory) {
            event.recovery_progress.state=transfer::RecoveryState::unavailable;
            return;
        }
        auto task=std::make_shared<RecoveryTask>();task->job=result.recovery;
        task->reserved_bytes=memory;task->retained_bytes=retained;
        task->result=std::move(result);task->event=event;task->generation=generation;
        recoveries.push_back(std::move(task));changed.notify_all();
    }
    void publish_recovery_progress() {
        // Only thread-safe counters are read on the UI path; no search or
        // source decoding runs here, even when an audio stream is idle.
        for(const auto& task:recoveries) {
            if(task->obsolete || task!=active_recovery || task->generation!=generation)continue;
            auto progress=task->job->progress();
            // The codeword search may finish before bounded source validation.
            // Keep that CPU work pending until the coordinator publishes it.
            if(progress.state==transfer::RecoveryState::recovered)progress.state=transfer::RecoveryState::running;
            const auto& previous=task->event.recovery_progress;
            if(progress.state==previous.state && progress.attempts==previous.attempts &&
               progress.elapsed.count()/1000==previous.elapsed.count()/1000)continue;
            task->event.recovery_progress=progress;
            auto event=task->event;event.sequence=next_event++;
            append_signal(std::move(event));
        }
    }
    void recovery_loop(std::stop_token stop) {
        DATAPUMP_MUTEX_CONTEXT(mutex_context,recovery);
        while(!stop.stop_requested()) {
            execution::checkpoint();
            std::shared_ptr<RecoveryTask> task;
            transfer::Received result;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock,stop,[&]{return current.running && std::any_of(recoveries.begin(),recoveries.end(),
                    [](const auto& item){return !item->started && !item->obsolete;});});
                if(stop.stop_requested())return;
                const auto next=std::find_if(recoveries.begin(),recoveries.end(),[](const auto& item){return !item->started && !item->obsolete;});
                if(next==recoveries.end())continue;
                task=*next;task->started=true;active_recovery=task;
                result=std::move(task->result);
            }
            try { result=transfer::recover_received(std::move(result),task->stop.get_token()); }
            catch(const std::exception& error) {
                result.error=error.what();result.recovery_progress=task->job->progress();
                result.recovery_progress.state=transfer::RecoveryState::unavailable;
            }
            std::lock_guard lock(mutex);
            active_recovery.reset();
            task->result=std::move(result);
            if(task->obsolete || !current.running || task->generation!=generation)continue;
            auto& recovered=task->result;
            auto event=task->event;event.recovery_progress=recovered.recovery_progress;
            if(recovered.content_validated) {
                recovered.content.message.local_id=content_identity(event.id);
                event.text=display_text(recovered.content.message);event.binary=false;event.validated=true;
                event.reception_id=reception_id(recovered.content.message);
                event.pre_fec_accuracy=recovered.content.pre_fec_accuracy;event.fec_stats=recovered.content.fec_stats;
                try {
                    admit_received(recovered.content.message.data.capacity(),settings.content_limit);
                    current.received.push_back(std::move(recovered));
                } catch(const Error& error) {
                    current.error=error.what();event=task->event;
                    event.recovery_progress.state=transfer::RecoveryState::unavailable;
                }
            }
            task->event=event;event.sequence=next_event++;append_signal(std::move(event));
            const auto state=task->event.recovery_progress.state;
            if(state!=transfer::RecoveryState::incomplete && state!=transfer::RecoveryState::cancelled)
                std::erase(recoveries,task);
            ++current.sequence;
        }
    }
    std::array<std::uint8_t,16> content_identity(std::uint64_t signal_id) const {
        auto id=reception_namespace;
        for(std::size_t i=0;i<8;++i)id[8+i]^=static_cast<std::uint8_t>(signal_id>>(8*i));
        return id;
    }
    void discard_obsolete(const SignalUpdate& replacement,bool discard_staged=true) {
        invalidate_recoveries(&replacement);
        const auto merged=[&](std::uint64_t id) {
            return std::find(replacement.superseded_ids.begin(),replacement.superseded_ids.end(),id)!=
                replacement.superseded_ids.end();
        };
        const auto stale=[&](const SignalUpdate& event) {
            return merged(event.id) || (event.id==replacement.id && event.revision<replacement.revision);
        };
        const auto old_content=[&](const transfer::Received& result) {
            const auto& id=result.content.message.local_id;
            return id==content_identity(replacement.id) ||
                std::any_of(replacement.superseded_ids.begin(),replacement.superseded_ids.end(),
                    [&](auto signal_id){return id==content_identity(signal_id);});
        };
        std::erase_if(current.signals,stale);
        std::erase_if(current.received,[&](const auto& result) {
            if(!old_content(result))return false;
            received_bytes-=result.content.message.data.capacity();return true;
        });
        if(discard_staged && staged_receives)staged_receives->discard_obsolete(replacement);
    }
    bool add_signal(SignalUpdate event, Prepared* wave = nullptr,
                    std::optional<transfer::Received> received = {},bool replacement = false) {
        event.sequence = next_event++; event.virtual_seconds = current.virtual_seconds;
        if (!wave) {
            if(replacement)discard_obsolete(event);
            append_signal(std::move(event));
            return true;
        }
        event.text = std::string(event.text.data(), std::min(event.text.size(), replay_text_limit));
        // Floor quantization and the original frame geometry retain the fixed
        // presentation deadline. Independent IDs may share a due frame.
        const auto fraction = static_cast<long double>(wave->transmitted_samples) / wave->transmitter->total_samples();
        const auto index = std::min(wave->replay_count - 1, std::max<std::size_t>(1,
            wave->binary ? wave->replay.size() :
                static_cast<std::size_t>(fraction * static_cast<long double>(wave->replay_count))));
        const auto jobs=coordinator_recovery_count(),bytes=recovery_reserved_bytes();
        const auto* accepted=wave->receives->stage(std::move(event),index,std::move(received),settings.content_limit,
            maximum_recovery_jobs-std::min(maximum_recovery_jobs,jobs),
            recovery_workspace_limit-std::min(recovery_workspace_limit,bytes));
        if(!accepted)return false;
        // No cache eviction or obsolete-result withdrawal precedes successful
        // transactional staging, so a visible capacity error preserves it.
        // Staging already withdrew obsolete future entries transactionally,
        // while preserving its earlier pending presentation frames.
        if(replacement)discard_obsolete(*accepted,false);
        make_receive_room(0,settings.content_limit,false);
        return true;
    }
    void make_receive_room(std::size_t bytes, std::size_t limit,bool appending=true) {
        const auto staged_received_bytes=staged_receives?staged_receives->content_bytes():0;
        if (staged_received_bytes > limit || bytes > limit - staged_received_bytes)
            throw Error("incoming content exceeds remaining receive cache capacity");
        while (!current.received.empty() &&
               ((appending && current.received.size() >= maximum_events) || received_bytes > limit - staged_received_bytes - bytes)) {
            received_bytes -= current.received.front().content.message.data.capacity();
            current.received.erase(current.received.begin());
        }
    }
    void admit_received(std::size_t bytes, std::size_t limit) {
        make_receive_room(bytes, limit);
        received_bytes += bytes;
    }
    static bool discard_retired_presentations(Receiver& receiver) {
        bool retired=false;
        for(auto it=receiver.presentations.begin();it!=receiver.presentations.end();) {
            const auto id=it->first;
            // take_pattern_bursts moved accepted chunks out of the detector.
            // Protect all of them until the entire external batch is published.
            const auto queued=std::any_of(receiver.bursts.begin(),receiver.bursts.end(),[&](const auto& burst) {
                return burst.stream_first_sample==id.first && burst.stream_first_symbol==id.second;
            });
            if(queued || receiver.modem->retains_pattern_stream(id.first,id.second)) {++it;continue;}
            receiver.content->discard(id.first,id.second);
            it=receiver.presentations.erase(it);retired=true;
        }
        return retired;
    }
    static void retire_unreferenced_receptions(Bank& bank) {
        if(!bank.reception_history_dirty)return;
        bank.receptions.retire_unreferenced([&](std::uint64_t id) {
            return std::any_of(bank.receivers().begin(),bank.receivers().end(),[&](const auto& receiver) {
                return std::any_of(receiver.presentations.begin(),receiver.presentations.end(),[&](const auto& item) {
                    return item.second.signal_id==id;
                });
            });
        });
        bank.reception_history_dirty=false;
    }
    static detail::ReceptionHistory::Candidate reception_candidate(
        const Receiver& receiver,const modem::PatternBurst& burst,Receiver::Presentation& display,bool helper_assisted=false) {
        detail::ReceptionHistory::Candidate candidate;
        candidate.id=display.candidate_id;candidate.family=receiver.family;
        candidate.signal_id=display.signal_id;
        candidate.first=receiver.sample_origin+burst.stream_first_sample;
        candidate.end=receiver.sample_origin+burst.end_sample;
        candidate.frequency=burst.frequency_hz;
        candidate.frequency_tolerance=static_cast<double>(receiver.options.modem.sample_rate)/
            static_cast<double>(modem::symbol_sample_count(receiver.options.modem));
        if(receiver.amplitude_geometry&&receiver.amplitude_channel_count) {
            const auto& geometry=*receiver.amplitude_geometry;
            auto channel=modem::pattern_amplitude_channel(geometry,0,receiver.amplitude_channel_count);
            for(std::size_t i=1;i<receiver.amplitude_channel_count;++i) {
                const auto other=modem::pattern_amplitude_channel(geometry,i,receiver.amplitude_channel_count);
                if(std::abs(other.center_hz-burst.frequency_hz)<std::abs(channel.center_hz-burst.frequency_hz))channel=other;
            }
            candidate.amplitude_low_hz=channel.first_bin*geometry.bin_width_hz;
            candidate.amplitude_high_hz=(channel.first_bin+channel.bin_count)*geometry.bin_width_hz;
        }
        candidate.absence_samples=modem::pattern_absence_capture_samples(receiver.options.modem);
        candidate.symbol_samples=modem::symbol_sample_count(receiver.options.modem);
        candidate.timing_tolerance=modem::pattern_chip_samples(receiver.options.modem);
        candidate.score=burst.support_samples;
        candidate.complete=burst.complete;
        candidate.helper_assisted=burst.helper_acquired || helper_assisted;
        if(receiver.options.modem.symbol_burst.enabled()) {
            auto config=receiver.options.modem;config.data_key=receiver.options.key;
            const modem::SymbolBurstPlan plan(config);
            if(!display.burst_first) {
                modem::SymbolBurstCursor cursor(plan,plan.bind_payload(receiver.epoch,config.stream_phase_samples));
                const auto first=cursor.at(burst.stream_first_symbol);
                display.burst_first=first.payload_begin;display.burst_next=first;
            }
            const auto next=modem::symbol_burst_detail::add(burst.first_stream_symbol,
                modem::symbol_burst_detail::add(burst.bits.size(),burst.missing_slots));
            if(next>display.burst_next->index) {
                modem::SymbolBurstCursor cursor(plan,*display.burst_next);
                display.burst_next=cursor.at(next);
            }
            candidate.source_identity=detail::ReceptionHistory::BurstIdentity{
                *display.burst_first,display.burst_next->payload_begin,true,true};
        } else if(const auto source=receiver.modem->source_identity(burst))candidate.source_identity=*source;
        return candidate;
    }
    static void trace_receiver_pattern_wire(const Bank& bank,const Receiver& receiver,
            const modem::PatternBurst& burst,std::uint64_t candidate_id,std::uint64_t frontier) {
        const auto trace=audio::trace::active.load();
        if(!trace)return;
        detail::simulation_wire_checkpoint({bank.revision,receiver.epoch,receiver.family,
            receiver.point_consumer,candidate_id,receiver.sample_origin,
            burst.stream_first_sample,burst.stream_first_symbol,burst.first_stream_symbol,burst.stream_phase_samples,
            receiver.sample_origin+burst.stream_first_sample,receiver.sample_origin+burst.end_sample,
            frontier,burst.missing_slots,burst.bits,burst.complete,burst.helper_acquired});
        if(trace->event_filter && !trace->event_filter("receiver_pattern_wire"))return;
        // Diagnostic copies of the actual ordinary detector output, before
        // StreamReceiver applies the Data mask or interprets framing. Piece
        // boundaries never change its original vector or completion decision.
        std::size_t offset=0;
        do {
            const auto count=std::min<std::size_t>(1024,burst.bits.size()-offset);
            std::string bits;bits.reserve(count);
            for(const auto bit:std::span(burst.bits).subspan(offset,count))bits.push_back(static_cast<char>('0'+bit));
            const bool last=offset+count==burst.bits.size();
            audio::trace::record("receiver_pattern_wire",[&](auto& f) {
                f.add("input_revision",bank.revision);f.add("epoch",receiver.epoch);f.add("family",receiver.family);
                f.add("instance_id",receiver.point_consumer);f.add("candidate_id",candidate_id);
                f.add("sample_origin",receiver.sample_origin);f.add("stream_first_sample",burst.stream_first_sample);
                f.add("stream_first_symbol",burst.stream_first_symbol);
                f.add("first_stream_symbol",burst.first_stream_symbol+offset);f.add("piece_offset",offset);
                f.add("stream_phase_samples",burst.stream_phase_samples);
                f.add("physical_first",receiver.sample_origin+burst.stream_first_sample);
                f.add("physical_end",receiver.sample_origin+burst.end_sample);f.add("feed_end_sample",frontier);
                f.add("bits",bits);f.add("missing_slots",offset?0:burst.missing_slots);
                f.add("complete",last && burst.complete);f.add("helper_acquired",burst.helper_acquired);
                f.add("scope","ordinary detector wire bits before Data XOR and framing; trace only");
            });
            offset+=count;
        }while(offset<burst.bits.size());
    }
    // Call only at a joined receiver boundary: end of feed, or after its
    // cancellation/error unwind and before this Bank is destroyed/reset.
    static void trace_receiver_acquisition_work(const Bank& bank,bool incomplete,const char* reason,
                                                const Receiver* only=nullptr,bool numeric_retired=false) {
        if(!audio::trace::active.load())return;
        if(!bank.receiver_storage) {
            audio::trace::record("receiver_acquisition_work_unavailable",[&](auto& f) {
                f.add("revision",bank.revision);f.add("sample",bank.samples);f.add("reason",reason);
                f.add("incomplete",true);f.add("numeric_owner",false);
                f.add("scope","receiver storage unavailable after an incomplete bank transition");
            });
            return;
        }
        for(const auto& receiver:bank.receivers()) {
            if(only && only!=&receiver)continue;
            const auto work=receiver.modem?receiver.modem->fft_work():modem::PatternFftWork{};
            audio::trace::record("receiver_acquisition_work",[&](auto& f) {
                f.add("revision",bank.revision);f.add("sample",bank.samples);f.add("epoch",receiver.epoch);
                f.add("family",receiver.family);f.add("sample_origin",receiver.sample_origin);f.add("instance_id",receiver.point_consumer);
                f.add("template_jobs",work.template_jobs);f.add("input_transforms",work.input_transforms);
                f.add("conditional_jobs",work.conditional_template_jobs);f.add("audit_jobs",work.original_audit_template_jobs);
                f.add("phase_jobs",work.phase_view_template_jobs);f.add("phase_tracking_fits",work.phase_view_tracking_fits);
                f.add("phase_corrected_bins",work.phase_corrected_bins);
                f.add("incomplete",incomplete || !receiver.modem);f.add("reason",reason);
                f.add("numeric_owner",bool(receiver.modem));f.add("numeric_retired",numeric_retired);
                f.add("scope","cumulative numeric work keyed by revision, row instance_id and sample_origin");
            });
        }
    }
    static void record_helper_cohort_phase_work(Bank& bank,const Receiver& receiver,
                                               const modem::PatternFftWork& before,std::uint64_t frontier) {
        if(!helper_cohort_receiver(bank,receiver))return;
        auto& cohort=*bank.helper_cohort;
        if(!cohort.phase)return;
        auto& phase=*cohort.phase;
        const auto after=receiver.modem->fft_work();
        const auto jobs=after.phase_view_template_jobs-before.phase_view_template_jobs;
        phase.jobs+=jobs;
        phase.corrected_bins+=after.phase_corrected_bins-before.phase_corrected_bins;
        const auto observed=receiver.modem->acquisition_state().observed_samples;
        const auto consumer=observed<=UINT64_MAX-receiver.sample_origin?receiver.sample_origin+observed:UINT64_MAX;
        if(jobs && consumer<=phase.curve.qualified_until())audio::trace::record("receiver_helper_cohort_phase_applied",[&](auto& f) {
            f.add("sample",frontier);f.add("reference_sample",phase.curve.latest_evidence());
            f.add("phase_consumer_frontier",consumer);
            f.add("epoch",receiver.epoch);f.add("sample_origin",receiver.sample_origin);
            f.add("phase_jobs",phase.jobs);f.add("phase_tracking_fits",after.phase_view_tracking_fits);
            f.add("phase_corrected_bins",phase.corrected_bins);f.add("timing_qualified",true);
            f.add("baseline_residual_hz",phase.curve.baseline());
        });
    }
    static std::uint64_t committed_bank_frontier(const Bank& bank,std::optional<std::uint64_t> prospective={}) {
        const auto ingress=prospective.value_or(bank.samples);
        auto committed=ingress;
        for(const auto& receiver:bank.receivers()) {
            if(helper_cohort_receiver(bank,receiver) && receiver.sample_origin>ingress) {
                committed=0;continue; // The first symmetric FIR window is not observed yet.
            }
            const detail::DeferredConsumerFrontier consumer{receiver.sample_origin,
                receiver.modem->delayed_processing_frontier()};
            committed=std::min(committed,detail::committed_frontier(ingress,
                std::span<const detail::DeferredConsumerFrontier>{&consumer,1}));
        }
        if(bank.helper_conditioning) {
            const auto& state=*bank.helper_conditioning;
            const auto centers=state.filter_output_origin+state.filter.output_samples();
            // Replayed pre-roll is a new consumer of already committed old
            // PCM, so it cannot retract the prior published frontier. New
            // input commits only through fully observed FIR center samples.
            committed=std::min(std::max(committed,state.publication_floor),
                std::max(state.publication_floor,centers));
            // Until an ordinary detector accepts a bit, the dormant broad
            // bank still owns the original acquisition obligation. It is a
            // real consumer, not merely optional retained memory. Holding its
            // frontier also makes automatic replay publication monotone.
            if(state.original)committed=std::min(committed,committed_bank_frontier(*state.original));
        }
        if(bank.helper_cohort && std::any_of(bank.receivers().begin(),bank.receivers().end(),
            [&](const auto& receiver){return helper_cohort_receiver(bank,receiver);})) {
            const auto& cohort=*bank.helper_cohort;
            const auto centers=cohort.filter.output_samples()?cohort.filter_origin+cohort.filter.output_samples():0;
            committed=std::min(committed,std::min(ingress,centers));
        }
        if(bank.helper_assistance) {
            const auto& state=*bank.helper_assistance;
            // FIR output dates refer to its centers, not the newest ingress.
            // A newly admitted pre-roll consumer cannot retract publication.
            const auto centers=state.receiver->sample_origin+state.filter.output_samples();
            committed=std::min(committed,std::max(state.publication_floor,centers));
        }
        return committed;
    }
    void record_deferred_input(Bank& bank,std::uint64_t count,bool pcm,const Settings& value) {
        if(!count || !bank.input_queued_at)return;
        if(std::none_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver) {
            return receiver.modem->delayed_processing_frontier().has_value();
        }))return;
        if(!bank.deferred_input.working_bytes()) {
            const auto capacity=bank_capacity(value);
            if(bank.working_bytes>=capacity)throw Error("receiver timing metadata exceeds configured DSP workspace");
            const auto allowance=std::min({std::size_t{256*1024},capacity/512,capacity-bank.working_bytes});
            bank.deferred_input.reserve(allowance);
            bank.working_bytes+=bank.deferred_input.working_bytes();
        }
        bank.deferred_input.record(bank.samples,count,*bank.input_queued_at,pcm);
    }
    // Caller holds mutex. The owning decoder has joined every consumer. Do not
    // read receiver internals on the GUI thread, or publish old-revision work.
    void publish_processing_frontier(Bank& bank) {
        const auto committed=committed_bank_frontier(bank);
        if(committed<current.processed_physical_samples)
            throw Error("receiver committed frontier regressed within one capture revision");
        bank.deferred_input.discard_committed(committed);
        const auto pending=bank.deferred_input.pending(committed,bank.samples);
        if(committed>capture_samples)throw Error("receiver committed frontier exceeds capture revision ingress");
        current.processed_physical_samples=committed;
        // An idle worker owns an internal batch, rather than an ingress block.
        // Shrink that ownership atomically as the all-consumer frontier commits;
        // ordinary popped-block remaining work is already a disjoint subset.
        decoding_remaining_physical_samples=std::min(decoding_remaining_physical_samples,capture_samples-committed);
        delayed_processing_pcm_samples=pending.pcm_samples;
        delayed_processing_queued_at=pending.oldest;
        delayed_processing_pending=committed<bank.samples;
    }
    template<class Feed> void feed_bank(Bank& bank, const Settings& value, std::uint64_t version,
                                        std::stop_token stop, std::span<const float> samples, Feed feed,
                                        Prepared* simulation_wave = nullptr,bool allow_admission=true,
                                        bool account_frontier=true,std::optional<std::uint64_t> coordinate_count={}) {
        DATAPUMP_MUTEX_CONTEXT(mutex_context,feed_bank);
        rx_cpu::Context cpu_context(rx_cpu::Stage::control_residual);
        // Admit the receiver's current clock epoch before consuming this PCM
        // block. A short burst can finish within one block after a second rolls
        // over; refreshing afterward can miss every chip of the new stream.
        if(allow_admission)refresh_bank(bank,value);
        if(bank.coverage_limited) {
            std::lock_guard lock(mutex);
            if(current.running && generation==version && bank.revision==receive_revision && !stop.stop_requested()) {
                current.receiver_health.search_limited=true;
                // A previously published exclusion proof cannot authorize
                // capture omission after required receiver coverage is lost.
                capture_future.reset();capture_quiet.reset();
            }
        }
        const auto sample_count=samples.size();
        // Admission can create the first delayed receiver inside a callback.
        // Record AFTER admission and BEFORE feed, using the original callback
        // timestamp for every subdivision. No captured PCM is copied here.
        if(!value.simulation)record_deferred_input(bank,coordinate_count.value_or(sample_count),!samples.empty(),value);
        if(sample_count>std::numeric_limits<std::uint64_t>::max()-bank.samples)
            throw Error("receive sample clock exceeds its platform range");
        if(bank.shared_capture && !samples.empty()) {
            const auto identity=modem::detail::scheduled_capture::Identity{bank.revision,bank.revision};
            const auto captured=bank.shared_capture->accounting().captured_end;
            if(captured!=bank.samples)throw Error("shared acquisition capture is not contiguous");
            for(std::size_t offset=0;offset<samples.size();) {
                const auto n=std::min<std::size_t>(8192,samples.size()-offset);
                bank.shared_capture->append_copy(bank.samples+offset,samples.subspan(offset,n),identity);offset+=n;
            }
            // Every later admission starts at its admission sample_origin and
            // never requests pre-local-zero PCM. Current consumers and leases
            // separately retain all of their replay/guard obligations.
            bank.future_capture->certify({identity,bank.samples+sample_count,bank.samples+sample_count});
        }
        const auto capacity = bank_capacity(value);
        // Optional assistance must not change original cohort eligibility,
        // sizing, sharing or cost counters. Its node/bytes stay resident and
        // charged; restore the same node/order before any numeric/publication.
        HelperPlanningIsolation helper_planning(bank);
        // Phase 1 is read-only. Any cold eligible profile forces complete
        // eviction before the first feed can run original worker preparation.
        std::size_t temporal_receivers=0,temporal_workers=0;bool temporal_cold=false;
        if(!bank.helper_conditioning && !bank.helper_fallback_budget && crossbank_temporal_plans_requested())for(const auto& receiver:bank.receivers()) {
            const auto workers=receiver.modem->temporal_plan_workers();
            if(!workers)continue;
            ++temporal_receivers;temporal_cold|=!*workers;temporal_workers=std::max(temporal_workers,*workers);
        }
        const bool temporal_ready=temporal_receivers>1 && !temporal_cold && temporal_workers &&
            temporal_workers<=modem::detail::SharedTemporalPlans::maximum_workers;
        if(!temporal_ready || (bank.temporal_plans && bank.temporal_plans->workers()!=temporal_workers))
            drop_shared_temporal_plans(bank,"cold_or_changed_cohort");
        struct SharedProjection {
            Bank& bank;
            std::unique_ptr<modem::detail::CorrelationProjectionCache> cache;
            std::size_t charged=0,remaining_headroom=0,deferred_detectors=0;
            void release(){cache.reset();bank.working_bytes-=charged;charged=remaining_headroom=deferred_detectors=0;}
            ~SharedProjection(){release();}
        } projection{bank};
        // This optional cache spends surplus only. Preserve every already
        // admitted detector reservation, plus the finite push/drain allocation bound,
        // before charging its bounded numeric storage to the shared bank.
        const auto moment_receivers=std::count_if(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver){
            const auto& config=receiver.options.modem;
            const auto chip=modem::pattern_chip_samples(config),symbol=modem::symbol_sample_count(config);
            return receiver.amplitude_fft_samples ||
                (modem::scheduled_bursts(config) && config.search_arithmetic!=modem::SearchArithmetic::fp64) ||
                (config.oscillator_search && modem::pattern_pulse_enabled(config) &&
                (chip>4096 || (chip>=1024 && symbol>=16ULL*config.sample_rate)));
        });
        const auto permutation_receivers=std::count_if(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver) {
            const auto& c=receiver.options.modem;
            return modem::detail::pattern_permutation_sharing_supported && receiver.options.key && c.dsss_factor>1 && c.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2;
        });
        if(!bank.helper_conditioning && !bank.helper_fallback_budget && !samples.empty() && (moment_receivers>1 || permutation_receivers>1) && bank.working_bytes<=capacity &&
           std::all_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver){return receiver.modem->clock_windowed();})) {
            auto reserved=bank.working_bytes;
            bool fits=true;
            for(const auto& receiver:bank.receivers()) {
                const auto actual=receiver.modem->working_bytes();
                const auto deferred=receiver.modem->reserved_workspace_bytes();
                const auto extra=deferred>actual?deferred-actual:0;
                const auto headroom=receiver.modem->projection_cache_headroom(sample_count);
                if(extra>capacity-reserved || headroom>capacity-reserved-extra){fits=false;break;}
                reserved+=extra+headroom;projection.remaining_headroom+=headroom;projection.deferred_detectors+=extra;
            }
            if(fits) {
                std::size_t fft_samples=0;
                for(const auto& receiver:bank.receivers())fft_samples=std::max(fft_samples,receiver.amplitude_fft_samples);
                const auto allowance=std::min<std::size_t>(256*1024+64*fft_samples,capacity-reserved);
                const auto mapped=std::any_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver){
                    return modem::scheduled_bursts(receiver.options.modem) && !receiver.amplitude_fft_samples;
                });
                projection.cache=std::make_unique<modem::detail::CorrelationProjectionCache>(samples,allowance,fft_samples,
                    mapped?std::optional<std::uint64_t>{bank.samples}:std::nullopt);
                if(projection.cache->enabled()) {
                    projection.charged=projection.cache->working_bytes();bank.working_bytes+=projection.charged;
                } else projection.cache.reset();
            }
        }
        // V2 permutation reuse is independent of fine-chip curve eligibility.
        // Keep its persistent optional pool inside the same protected surplus.
        // Diagnostic restriction: every receiver must be keyed V2 and already
        // retain its original workers AND Temporal state. Other algorithms and
        // mixed cohorts keep the original 8-row/256KiB optional policy.
        // Lazy scheduled preparation and queued polling borrow the exact
        // private pool synchronously, even without an input projection cache.
        // The real capture owner identifies this cohort; no borrowed binding
        // survives the joined call. Optional bytes remain in the global loan.
        const bool independent_permutation=!bank.helper_fallback_budget && bank.shared_capture && !bank.receivers().empty() &&
            std::all_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver) {
                const auto& c=receiver.options.modem;
                return receiver.modem->deferred_pattern_history() && receiver.options.key && c.dsss_factor>1 &&
                    c.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2;
            });
        const bool permutation_workers_retained=std::all_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver) {
            const auto& c=receiver.options.modem;
            if(!receiver.options.key || c.dsss_factor<=1 || c.outer_dsss_version!=modem::OuterDsssVersion::interleaved_v2)return false;
            const auto workers=receiver.modem->temporal_plan_workers();return receiver.modem->deferred_pattern_history() || (workers && *workers);
        });
        if(bank.permutations && !permutation_workers_retained &&
           (bank.permutations->working_bytes()>256*1024 || bank.permutations->row_limit()>8)) {
            const auto bytes=bank.permutations->working_bytes();
            trace_permutation_release(bank,"workers_unretained");
            bank.permutations.reset();bank.working_bytes-=bytes;
        }
        if((projection.cache || independent_permutation) && permutation_receivers>1 && bank.working_bytes<=capacity) {
            const auto old_bytes=bank.permutations?bank.permutations->working_bytes():0;
            auto reserved=bank.working_bytes-old_bytes;
            const auto reserve=[&](std::size_t bytes) {
                if(bytes>capacity-reserved)return false;
                reserved+=bytes;return true;
            };
            if(reserve(projection.remaining_headroom) && reserve(projection.deferred_detectors)) {
                const bool larger=permutation_workers_retained && large_permutation_cache_requested() &&
                    modem::detail::permutation_bank_byte_limit>256*1024 &&
                    modem::detail::permutation_bank_byte_limit<=capacity-reserved;
                const auto allowance=larger?modem::detail::permutation_bank_byte_limit:
                    std::min<std::size_t>(256*1024,capacity-reserved);
                const auto rows=larger?modem::detail::permutation_cache_max_rows:8;
                const bool replace=bank.permutations && larger &&
                    (old_bytes<allowance || bank.permutations->row_limit()<rows);
                if((!bank.permutations || replace) && allowance>4096) {
                    // Release before allocation: no peak double reservation.
                    if(bank.permutations){trace_permutation_release(bank,"optional_upgrade");bank.permutations.reset();bank.working_bytes-=old_bytes;}
                    try {
                        auto pool=std::make_unique<modem::detail::PatternPermutationPools>(allowance,rows);
                        bank.working_bytes+=pool->working_bytes();bank.permutations=std::move(pool);
                    }catch(const std::bad_alloc&) {} // Exact original private generator remains available.
                }
            }
        }
        // Spend only genuine surplus after all detector and push/drain
        // reservations. Existing projection eligibility supplies a synchronous
        // borrowing channel; other geometries retain the original local L1.
        const auto prepare_source_curve_pool=[&](bool include_fine) {
            if(!projection.cache || bank.working_bytes>capacity)return;
            using Curves=modem::detail::SharedSourceCurves<>;
            const auto eligible=[include_fine](const Receiver& receiver) {
                const auto& c=receiver.options.modem;
                return !receiver.amplitude_fft_samples && receiver.options.key &&
                    modem::scheduled_bursts(c) && c.scramble &&
                    c.spreading_mode==modem::SpreadingMode::pattern && modem::pattern_pulse_enabled(c) &&
                    (modem::pattern_chip_samples(c)>=1024 ||
                     modem::detail::fine_source_curve_geometry(c,include_fine)) &&
                    modem::symbol_sample_count(c)<(1ULL<<40) &&
                    c.search_arithmetic!=modem::SearchArithmetic::fp64;
            };
            std::size_t count=0,missing=0;
            for(const auto& receiver:bank.receivers())if(eligible(receiver)) {
                ++count;if(!receiver.source_curve_family)++missing;
            }
            auto reserved=bank.working_bytes;
            const auto add=[&](std::size_t bytes) {
                if(bytes>capacity-reserved)return false;
                reserved+=bytes;return true;
            };
            const bool fits=add(projection.remaining_headroom) && add(projection.deferred_detectors);
            const auto capability_bytes=sizeof(modem::detail::SourceFamily);
            if(count>1 && fits && missing<=(capacity-reserved)/capability_bytes &&
               (bank.source_curves || capacity-reserved-missing*capability_bytes>=Curves::minimum_working_bytes())) {
                try {
                    for(auto& receiver:bank.receivers())if(eligible(receiver) && !receiver.source_curve_family) {
                        // Family numbers identify keys only within this Bank
                        // generation; exact purpose roots also bind equality.
                        if(receiver.family==UINT64_MAX)throw Error("source family namespace exhausted");
                        const auto c=transfer::seeded_config(receiver.options,receiver.epoch);
                        receiver.source_curve_family=std::make_unique<const modem::detail::SourceFamily>(
                            modem::detail::source_family(c,receiver.family+1));
                        bank.working_bytes+=capability_bytes;reserved+=capability_bytes;
                    }
                    if(!bank.source_curves) {
                        const auto ceiling=include_fine && modem::detail::fine_source_curve_revision()==2?
                            1024U*1024U:256U*1024U;
                        auto pool=std::make_unique<Curves>(std::min<std::size_t>(ceiling,capacity-reserved));
                        if(pool->capacity()) {
                            bank.working_bytes+=pool->working_bytes();bank.source_curves=std::move(pool);
                            audio::trace::record("receiver_source_curve_cache",[&](auto& f) {
                                f.add("sample",bank.samples);f.add("bytes",bank.source_curves->working_bytes());
                                f.add("rows",bank.source_curves->capacity());f.add("families",count);
                                f.add("fine_curve_table_revision",include_fine?modem::detail::fine_source_curve_revision():1U);
                            });
                        }
                    }
                } catch(const std::bad_alloc&) {
                    // Optional allocation failure retains original local rows.
                }
            }
        };
        prepare_source_curve_pool(false); // Keep legacy coarse grant order.
        // Phase 2: additive optional grant AFTER all original workers/Temporal,
        // older optional caches, deferred detectors, and finite push/drain
        // reservations. This pool is separate from the 256 KiB projection cap.
        if(projection.cache && temporal_ready && !bank.temporal_plans && bank.working_bytes<=capacity) {
            auto reserved=bank.working_bytes;
            const auto add=[&](std::size_t bytes) {
                if(bytes>capacity-reserved)return false;reserved+=bytes;return true;
            };
            if(add(projection.remaining_headroom) && add(projection.deferred_detectors))try {
                auto pool=modem::detail::SharedTemporalPlans::create(temporal_workers,capacity-reserved);
                if(pool) {
                    const auto bytes=pool->working_bytes();
                    bank.temporal_plans=std::move(pool);bank.working_bytes+=bytes;
                    audio::trace::record("receiver_shared_temporal_plans_allocated",[&](auto& f) {
                        f.add("sample",bank.samples);f.add("bytes",bytes);f.add("workers",temporal_workers);
                        f.add("entries",modem::detail::ScheduledTemporalTemplates::SharedPlanCache::capacity);
                    });
                }
            }catch(const std::bad_alloc&) {} // Original owned/public Plan fallback.
        }
        // A second additive optional grant, after the ORIGINAL exact owner,
        // earlier caches, all original worker grants and deferred/push reserve.
        // Payload is owned by each public shard row and shared across sequential
        // receivers through their already-acquired exact Lease; no private data.
        if(projection.cache && modem::detail::ScheduledTemporalFp32Execution::payload_requested() && bank.temporal_plans &&
           bank.temporal_plans->idle() && !bank.temporal_plans->payload_bytes() && bank.working_bytes<=capacity) {
            auto reserved=bank.working_bytes;
            const auto add=[&](std::size_t bytes){if(bytes>capacity-reserved)return false;reserved+=bytes;return true;};
            if(add(projection.remaining_headroom) && add(projection.deferred_detectors))try {
                const auto before=bank.temporal_plans->working_bytes();
                if(bank.temporal_plans->template allocate_payloads<modem::detail::ScheduledTemporalFp32Execution::Native::PackedPlan>(capacity-reserved)) {
                    const auto added=bank.temporal_plans->working_bytes()-before;bank.working_bytes+=added;
                    audio::trace::record("receiver_shared_temporal_fp32_payloads",[&](auto& f) {
                        f.add("sample",bank.samples);f.add("bytes",added);f.add("workers",bank.temporal_plans->workers());
                        f.add("entries",modem::detail::ScheduledTemporalTemplates::SharedPlanCache::capacity);
                    });
                }
            }catch(const std::bad_alloc&) {} // All new shard arenas rolled back.
        }
        // Fine L2 uses final surplus after preceding exact/shared grants.
        if(modem::detail::fine_source_curves_requested())prepare_source_curve_pool(true);
        const auto reclaim_source_curves=[&](bool retain_permutations=false) {
            drop_shared_temporal_plans(bank);
            // Every synchronous feed/worker has joined before this callback.
            if(bank.permutations && !retain_permutations) {
                const auto bytes=bank.permutations->working_bytes();
                trace_permutation_release(bank,"workspace_loan");
                bank.permutations.reset();bank.working_bytes-=bytes;
            }
            if(bank.source_curves) {
                const auto bytes=bank.source_curves->working_bytes();
                if(!bank.source_curves->try_reclaim())return;
                bank.source_curves.reset();bank.working_bytes-=bytes;
            }
            for(auto& receiver:bank.receivers())if(receiver.source_curve_family) {
                receiver.source_curve_family.reset();bank.working_bytes-=sizeof(modem::detail::SourceFamily);
            }
        };
        const auto release_optimization=[&] {projection.release();reclaim_source_curves();drop_exact_source_pool(bank,"optimization_or_error");drop_point_pool(bank,"optimization_or_error");};
        // An unavailable context cannot use the optional arena and must not
        // let it reduce the original receiver recording/replay allowance.
        if(!projection.cache)reclaim_source_curves(independent_permutation);
        const bool source_requested=!bank.helper_fallback_budget && exact_source_pool_requested() && modem::detail::scheduled_source_samples::environment();
        const bool source_cohort=bank.shared_capture && !bank.receivers().empty() &&
            std::all_of(bank.receivers().begin(),bank.receivers().end(),exact_source_receiver);
        if(!source_requested || !source_cohort)drop_exact_source_pool(bank,"ineligible_cohort_or_environment");
        else if(bank.exact_source_samples && !bank.exact_source_samples->capacity())
            drop_exact_source_pool(bank,"joined_tombstone");
        if(source_requested && source_cohort && !bank.exact_source_samples &&
           bank.working_bytes<capacity && bank.exact_source_generation<UINT64_MAX) {
            auto reserved=bank.working_bytes;
            const auto add=[&](std::size_t bytes){if(bytes>capacity-reserved)return false;reserved+=bytes;return true;};
            // Existing projection reservations still win in a mixed future
            // implementation. Lazy headroom is intentionally not treated as a
            // zero finite promise: a joined mandatory-pressure callback below
            // reclaims this reversible grant and restores the original loan.
            constexpr std::size_t cover=2U*1024U*1024U;
            if(add(projection.remaining_headroom) && add(projection.deferred_detectors) && add(cover)) {
                using Pool=modem::detail::scheduled_source_samples::Pool;
                const auto ceiling=std::min<std::size_t>(8U*1024U*1024U,capacity-reserved);
                if(ceiling>=Pool::minimum_working_bytes())try {
                    auto pool=std::make_unique<Pool>(ceiling);
                    if(pool->capacity()) {
                        const auto bytes=pool->working_bytes();
                        if(bytes>ceiling)throw Error("exact source pool exceeds its explicit optional grant");
                        bank.working_bytes+=bytes;bank.exact_source_samples=std::move(pool);++bank.exact_source_generation;
                        bank.next_exact_source_trace=Clock::now();
                        trace_exact_source_pool(bank,"allocated");
                    }
                } catch(const std::bad_alloc&) {} // Original q construction remains available.
            }
        }
        const auto points_mode=point_mode();
        const bool points_requested=!bank.helper_fallback_budget && points_mode!=PointMode::off && modem::detail::scheduled_source_points::environment();
        // Source-only identities and copied q/census keys do not borrow PCM.
        // Short split profiles deliberately retain local capture; they may use
        // this optional owner under the SAME joined loan/pressure accounting.
        const bool point_cohort=!bank.receivers().empty() &&
            std::all_of(bank.receivers().begin(),bank.receivers().end(),exact_source_receiver);
        if(!points_requested || !point_cohort)drop_point_pool(bank,"ineligible_cohort_or_environment");
        else if(bank.exact_source_points && !bank.exact_source_points->enabled())drop_point_pool(bank,"joined_tombstone");
        if(points_requested && point_cohort && !bank.exact_source_points && bank.working_bytes<capacity &&
           bank.point_source_generation<UINT64_MAX) {
            auto reserved=bank.working_bytes;
            const auto add=[&](std::size_t bytes){if(bytes>capacity-reserved)return false;reserved+=bytes;return true;};
            constexpr std::size_t cover=2U*1024U*1024U;
            if(add(projection.remaining_headroom) && add(projection.deferred_detectors) && add(cover)) {
                using Pool=modem::detail::scheduled_source_points::PointPool;
                const Pool::Limits limits{points_mode==PointMode::cache?point_rows(points_mode):0,64,1024,
                    points_mode==PointMode::census?point_rows(points_mode):0};
                const auto ceiling=std::min<std::size_t>(64U*1024U*1024U,capacity-reserved);
                if(Pool::required_bytes(limits)<=ceiling)try {
                    auto owner=std::make_unique<Pool>(ceiling,limits);
                    if(owner->enabled()) {
                        const auto bytes=owner->working_bytes();if(bytes>ceiling)throw Error("point pool exceeds optional grant");
                        bank.working_bytes+=bytes;bank.exact_source_points=std::move(owner);++bank.point_source_generation;
                        bank.next_point_source_trace=Clock::now();trace_point_pool(bank,"allocated");
                    }
                }catch(const std::bad_alloc&) {} // Original q calls and original loan remain.
            }
        }
        if(points_mode!=PointMode::off && !bank.exact_source_points && Clock::now()>=bank.next_point_source_trace) {
            audio::trace::record("receiver_exact_source_point_unavailable",[&](auto& f) {
                f.add("revision",bank.revision);f.add("sample",bank.samples);f.add("environment_qualified",points_requested);
                f.add("eligible_cohort",point_cohort);f.add("shared_capture",bool(bank.shared_capture));
                f.add("bank_bytes",bank.working_bytes);f.add("capacity",capacity);
                f.add("remaining_headroom",projection.remaining_headroom);f.add("deferred_detectors",projection.deferred_detectors);
                f.add("mode",points_mode==PointMode::cache?"cache":"census");f.add("requested_rows",point_rows(points_mode));
                f.add("scope","requested optional source point arena absent; Original q retained; perpurpose counters include these omissions");
            });
            bank.next_point_source_trace=Clock::now()+std::chrono::seconds(1);
        }
        const bool projection_context_created=static_cast<bool>(projection.cache);
        bool projection_context_used=false;
        std::vector<std::complex<double>> pattern_scores;
        std::vector<PatternScoreObservation> pattern_observations;
        const auto record_history_generation=[&](Receiver& receiver) {
            if(!receiver.modem->deferred_pattern_history())return;
            receiver.history_ordinals.record(receiver.modem->diagnostic_history_generation(),replay_clock(),[&](auto count) {
                return detail::reserve_pattern_observation_ids(pattern_score_observation_id,count);
            });
            // Clear-through sees every reserved completed-event identity even
            // while another profile or a deferred query is still computing.
            std::lock_guard lock(mutex);
            if(current.running && generation==version && bank.revision==receive_revision && !stop.stop_requested()) {
                const auto id=pattern_score_observation_id.load(std::memory_order_relaxed);
                current.pattern_score_observation_id=id;
                if(simulation_wave)simulation_wave->pattern_score_observation_id=id;
            }
        };
        const auto query_history=[&](Receiver& receiver,bool pressure) {
            const bool deferred=receiver.modem->deferred_pattern_history();
            const auto generation=receiver.modem->diagnostic_history_generation();
            if(deferred && !receiver.history_poll.due(generation,Clock::now(),pressure))return;
            execution::checkpoint();if(stop.stop_requested())return;
            rx_cpu::Scope cpu_candidates(rx_cpu::Stage::publication);
            const auto retained=receiver_workspace(receiver),actual=receiver.modem->working_bytes();
            const auto overhead=retained-actual,other=bank.working_bytes-retained;
            if(other>capacity || overhead>capacity-other)throw Error("diagnostic replay exceeds exact cohort loan");
            ExactSourceLoan history_loan{bank,receiver,capacity,capacity-other-overhead,stop};
            const bool exact_source=exact_source_receiver(receiver) && bank.exact_source_samples;
            if(exact_source && receiver.family==UINT64_MAX)throw Error("exact source diagnostic key namespace exhausted");
            // History polls keep warm q tiles. Joined allocation/guard
            // pressure reclaims optional backing and restores this core loan.
            modem::detail::scheduled_source_samples::Scope history_source_scope(source_binding(bank,receiver,
                deferred?&history_loan:nullptr,deferred?&ExactSourceLoan::reclaim_cohort:nullptr,
                modem::detail::scheduled_source_samples::Purpose::diagnostic));
            if(deferred)receiver.modem->set_workspace_bytes(history_loan.loan);
            const auto& c=receiver.options.modem;
            const bool permutation_eligible=receiver.options.key && c.dsss_factor>1 &&
                c.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2;
            const auto permutation_ns=permutation_eligible && receiver.family<UINT64_MAX?receiver.family+1:0;
            modem::detail::PatternPermutationScope history_permutations(permutation_eligible?bank.permutations.get():nullptr,permutation_ns);
            const auto accounted=receiver_workspace(receiver);
            const auto candidates=receiver.modem->pattern_candidates(pattern_score_limit,stop);
            const auto history_growth=receiver.pattern_score_history.allocation_headroom(candidates.size());
            if(bank.working_bytes>capacity || history_growth>capacity-bank.working_bytes)
                throw Error("pattern score history replacement exceeds receiver bank");
            if(deferred) {
                if(receiver.modem->diagnostic_history_generation()!=generation)
                    throw Error("diagnostic history query changed receiver completion events");
                receiver.pattern_score_history.update_observations(candidates,[&](const auto& candidate) {
                    return receiver.history_ordinals.observation(candidate.completed_event,candidate.admission_threshold);
                });
                receiver.history_poll.completed(generation,Clock::now());
                receiver.history_ordinals.discard_resolved_through(receiver.modem->diagnostic_history_resolved_through());
            } else receiver.pattern_score_history.update(candidates,replay_clock(),[&] {
                return detail::reserve_pattern_observation_ids(pattern_score_observation_id,1);
            });
            bank.working_bytes=bank.working_bytes-accounted+receiver_workspace(receiver);
            if(bank.working_bytes>capacity)throw Error("receive diagnostic history exceeds configured DSP workspace");
            audio::trace::record("receiver_history_query",[&](auto& f) {
                f.add("epoch",receiver.epoch);f.add("revision",bank.revision);f.add("generation",generation);
                f.add("deferred",deferred);f.add("pressure",pressure);f.add("returned",candidates.size());
                f.add("role","diagnostics only; accepted decisions already published");
            });
        };
        const auto prepare_history_fence=[&](Receiver& receiver) {
            using Ordinals=detail::DeferredPatternHistoryOrdinals;
            auto& ordinals=receiver.history_ordinals;
            if(!receiver.modem->deferred_pattern_history() || ordinals.range_count()+2<=ordinals.capacity())return;
            // Provenance is cheap metadata, not a request to score all history.
            // Grow only from actual unpromised cohort space. The full new heap
            // stays charged alongside old backing until grow() has returned.
            if(ordinals.capacity()<Ordinals::maximum_range_capacity) {
                const auto next=std::min(Ordinals::maximum_range_capacity,2*ordinals.capacity());
                const auto replacement=Ordinals::growth_bytes(next);
                auto reserved=bank.working_bytes;
                const auto reserve=[&](std::size_t n){if(reserved>capacity || n>capacity-reserved)return false;reserved+=n;return true;};
                if(reserve(projection.remaining_headroom) && reserve(projection.deferred_detectors) &&
                   reserve(replacement)) {
                    const auto before=receiver_workspace(receiver);bool grown=false;
                    {
                        struct Transient {
                            Bank& bank;std::size_t bytes=0;
                            ~Transient(){bank.working_bytes-=bytes;}
                        } transient{bank};
                        try {grown=ordinals.grow(next,[&](std::size_t n){
                            if(n!=replacement || bank.working_bytes>capacity || n>capacity-bank.working_bytes)
                                throw Error("pattern history provenance replacement exceeds cohort loan");
                            bank.working_bytes+=n;transient.bytes=n;
                        },stop);}catch(const std::bad_alloc&){} // Old metadata and required exact replay remain.
                    }
                    bank.working_bytes=bank.working_bytes-before+receiver_workspace(receiver);
                    if(bank.working_bytes>capacity)throw Error("pattern history provenance exceeds configured DSP workspace");
                    if(stop.stop_requested())return;
                    if(grown) {
                        audio::trace::record("receiver_history_provenance_growth",[&](auto& f){
                            f.add("epoch",receiver.epoch);f.add("ranges",ordinals.range_count());f.add("capacity",ordinals.capacity());
                            f.add("heap_bytes",ordinals.heap_bytes());f.add("replacement_peak_bytes",replacement);
                        });
                        return;
                    }
                }
            }
            // Genuine pressure resolves OLD completed events before another
            // fence. Last feed's accepted decisions have already been published.
            query_history(receiver,true);
            // Reserve BOTH feed and process_pending completion fences before
            // either can accept a bit. No numerical diagnostic may intervene
            // between a new acceptance and ordinary phase0/1 publication.
            if(receiver.history_ordinals.range_count()+2>receiver.history_ordinals.capacity())
                throw Error("receiver diagnostic provenance remains unresolved at workspace limit");
        };
        // Observe every profile before publishing any completion. Otherwise a
        // weaker profile processed first could close the shared pending row.
        helper_planning.restore();
        // Delayed cohort work rejoins this same ordinary per-row recovery
        // boundary before publication. A numeric failure never resets the
        // entire capture/transmitter merely because a helper was enabled.
        // Sparse exceptions preserve the original Error, within the stack
        // space formerly occupied by three fixed per-row control arrays.
        struct CohortFailure {Receiver* receiver=nullptr;std::exception_ptr error;};
        std::array<CohortFailure,240> cohort_failures{};
        std::size_t cohort_failure_count=0;
        struct JoinedHelperFlags {
            Bank& bank;bool pending=false;
            ~JoinedHelperFlags() {
                if(!pending || !bank.receiver_storage)return;
                for(auto& receiver:bank.receivers())clear_helper_row_transients(receiver);
            }
        } joined_helper_flags{bank,static_cast<bool>(bank.helper_cohort)};
        static_assert(sizeof(cohort_failures)+sizeof(cohort_failure_count)+sizeof(JoinedHelperFlags)<=
            sizeof(std::array<std::exception_ptr,240>)+sizeof(std::array<bool,240>)+sizeof(std::array<const char*,240>));
        for(unsigned phase=0;phase<2;++phase) {
            for (auto& receiver : bank.receivers()) {
                execution::checkpoint();
                if (stop.stop_requested()) return;
                auto accounted = receiver_workspace(receiver);
                std::optional<NativeStorageScope> native_storage;
                const auto update_workspace = [&] {
                    const auto actual = receiver_workspace(receiver);
                    bank.working_bytes = bank.working_bytes - accounted + actual;
                    accounted = actual;
                };
                try {
                    if(phase==1 && (receiver.helper_flags&Receiver::helper_deferred_error)) {
                        // Consume once before the same ordinary recovery.
                        // Only failed rows search this bounded sparse table.
                        receiver.helper_flags&=static_cast<std::uint8_t>(~Receiver::helper_deferred_error);
                        for(std::size_t i=0;i<cohort_failure_count;++i)if(cohort_failures[i].receiver==&receiver) {
                            auto error=std::move(cohort_failures[i].error);cohort_failures[i].receiver=nullptr;
                            std::rethrow_exception(error);
                        }
                        // More than 240 simultaneous Errors still retain their
                        // exact diagnostic trace, without a growing allocation.
                        throw Error("ordinary helper cohort operation failed; original diagnostic recorded");
                    }
                    // Empty publication batches only retire presentation metadata.
                    // Keep numeric storage pooled until a modem operation needs it.
                    if((phase==0 || !receiver.bursts.empty()) && !helper_assisted_receiver(bank,receiver))
                        native_storage.emplace(bank,receiver,capacity,accounted);
                    if(phase==0) {
                        // Keep a helper-qualified accepted owner's ordinary
                        // END scorer when a later start competes, including
                        // after reference loss. This sets no acquisition prior
                        // or phase correction.
                        receiver.modem->set_helper_gap_protection(bank.helper_gap_protection_qualified);
                        auto* local_projection=(helper_assisted_receiver(bank,receiver) ||
                            helper_cohort_receiver(bank,receiver))?nullptr:projection.cache.get();
                        // Resolve/grow provenance before borrowing numeric caches
                        // or computing the modem loan. New heap bytes then reduce
                        // that loan exactly, including after pressure replay.
                        prepare_history_fence(receiver);
                        accounted=receiver_workspace(receiver);
                        if(stop.stop_requested())return;
                        const auto required=receiver.modem->reserved_workspace_bytes();
                        const auto retained=receiver.modem->working_bytes();
                        const auto native_growth=required>retained?required-retained:0;
                        if(bank.working_bytes>capacity || native_growth>capacity-bank.working_bytes) {
                            if(helper_assisted_receiver(bank,receiver) && !bank.helper_assistance->payload_retained)
                                throw Error("speculative helper numeric growth exceeds the available surplus");
                            drop_helper_history(bank);trim_native_caches(bank);accounted=receiver_workspace(receiver);
                        }
                        const auto actual=receiver.modem->working_bytes();
                        auto overhead = accounted - actual;
                        auto other = bank.working_bytes - accounted;
                        const auto growth=local_projection?receiver.modem->projection_cache_headroom(sample_count):0;
                        const auto deferred=local_projection?receiver.modem->reserved_workspace_bytes():actual;
                        const auto extra=deferred>actual?deferred-actual:0;
                        if(local_projection && (growth>projection.remaining_headroom || extra>projection.deferred_detectors)) {
                            release_optimization();local_projection=nullptr;accounted=receiver_workspace(receiver);overhead=accounted-actual;other=bank.working_bytes-accounted;
                        }
                        if(local_projection) {
                            const auto peers=projection.deferred_detectors-extra;
                            if(peers>capacity-std::min(capacity,other))other=capacity;
                            else other+=peers;
                        }
                        if(local_projection) {
                            // Query each unprocessed profile once at admission.
                            // Its state stays unchanged until this sequential
                            // feed; consumed bounds become measured bank bytes.
                            const auto pending=projection.remaining_headroom-growth;
                            if(pending>capacity-std::min(capacity,other))other=capacity;
                            else other+=pending;
                        }
                        if(local_projection && (other>capacity || overhead>capacity-other ||
                           deferred>capacity-other-overhead ||
                           growth>capacity-other-overhead-deferred)) {
                            // If recording grew past the surplus, release the
                            // optimization before restoring the original loan.
                            release_optimization();local_projection=nullptr;accounted=receiver_workspace(receiver);overhead=accounted-actual;other=bank.working_bytes-accounted;
                        }
                        if (other > capacity || overhead > capacity - other)
                            throw Error("key and epoch receiver bank exceeds the configured DSP workspace");
                        // Idle keys reserve their actual state. The receiver being
                        // fed can use all remaining shared space for recording and
                        // replay, while later keys see its measured growth.
                        auto loan=capacity-other-overhead;
                        if(bank.helper_fallback_budget) {
                            const auto ceiling=helper_replay_numeric_limit(bank,receiver);
                            const auto unspent=helper_replay_unspent(bank);
                            const auto own_growth=ceiling>actual?ceiling-actual:0;
                            if(unspent<own_growth || unspent-own_growth>loan || ceiling>loan-(unspent-own_growth))
                                throw Error("helper replay cannot retain its ordinary finite workspace promises");
                            // Numeric optional caches may use this ceiling, but
                            // cannot borrow another row's future bits or source.
                            // Streaming's argument includes its Impl, while its
                            // actual-byte count also includes the outer shell.
                            loan=ceiling-sizeof(modem::StreamingReceiver);
                        }
                        if(helper_assisted_receiver(bank,receiver))bank.helper_assistance->ordinary_numeric_loan=loan;
                        if(helper_assisted_receiver(bank,receiver) && !bank.helper_assistance->payload_retained) {
                            // Speculative numeric work uses only its admitted
                            // surplus. Accepted payloads use the full ordinary
                            // shared loan/source/recovery rules below.
                            loan=std::min(loan,bank.helper_assistance->numeric_limit);
                        }
                        receiver.modem->set_workspace_bytes(loan);
                        ExactSourceLoan source_loan{bank,receiver,capacity,capacity-other-overhead,stop};
                        const bool exact_source=exact_source_receiver(receiver) && bank.exact_source_samples;
                        if(exact_source && receiver.family==UINT64_MAX)throw Error("exact source key namespace exhausted");
                        const bool deferred_pressure=receiver.modem->deferred_pattern_history();
                        modem::detail::scheduled_source_samples::Scope source_scope(source_binding(bank,receiver,
                            deferred_pressure?&source_loan:nullptr,deferred_pressure?&ExactSourceLoan::reclaim_cohort:nullptr,
                            modem::detail::scheduled_source_samples::Purpose::acquisition));
                        {
                            auto* context=local_projection;
                            projection_context_used|=context!=nullptr;
                            // One exclusive coordinator Borrow. Outer guards
                            // may pin rows through raw work; joined cleanup
                            // unbinds every Temporal before the next receiver.
                            const auto workers=receiver.modem->temporal_plan_workers();
                            auto plans=!helper_assisted_receiver(bank,receiver) && bank.temporal_plans && workers && *workers?
                                bank.temporal_plans->try_borrow(*workers):modem::detail::SharedTemporalPlans::Borrow{};
                            struct ClearSourceBinding {
                                modem::detail::CorrelationProjectionCache* cache;
                                ~ClearSourceBinding(){if(cache){cache->clear_source_curves();cache->clear_permutations();cache->clear_temporal_plans();}}
                            } clear{context};
                            if(context && plans)context->bind_temporal_plans(&plans);
                            if(context)context->bind_source_curves(bank.source_curves.get(),receiver.source_curve_family.get());
                            const auto& c=receiver.options.modem;
                            const bool eligible=!helper_assisted_receiver(bank,receiver) && receiver.options.key && c.dsss_factor>1 &&
                                c.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2;
                            const auto ns=eligible && receiver.family<UINT64_MAX?receiver.family+1:0;
                            modem::detail::PatternPermutationScope permutation_scope(eligible?bank.permutations.get():nullptr,ns);
                            if(context)context->bind_permutations(eligible?bank.permutations.get():nullptr,ns);
                            feed(receiver,context);
                            record_history_generation(receiver);
                            receiver.modem->process_pending(stop);
                            record_history_generation(receiver);
                        }
                        if(simulation_media_clock(value))
                            detail::simulation_checkpoint(detail::SimulationPhase::receiver_processed,0,bank.samples,bank.revision);
                        update_workspace();
                        auto drained=receiver.modem->take_pattern_bursts();
                        if(bank.helper_conditioning && !bank.helper_conditioning->committed &&
                           std::any_of(drained.begin(),drained.end(),[](const auto& burst){return !burst.bits.empty();}))
                            commit_helper_conditioning(bank);
                        if(!helper_assisted_receiver(bank,receiver) || receiver.bursts.empty())receiver.bursts=std::move(drained);
                        else receiver.bursts.insert(receiver.bursts.end(),std::make_move_iterator(drained.begin()),
                            std::make_move_iterator(drained.end()));
                        if(helper_assisted_receiver(bank,receiver) && !bank.helper_assistance->payload_retained &&
                           std::any_of(receiver.bursts.begin(),receiver.bursts.end(),[](const auto& b){return !b.bits.empty();})) {
                            // Promote before joined reclamation/publication:
                            // an accepted prefix is now mandatory payload work.
                            bank.helper_assistance->payload_retained=true;
                            if(bank.helper_assistance->tail_only && receiver.modem->suspend_helper_acquisition())
                                bank.helper_assistance->acquisition_frozen=true;
                            audio::trace::record("receiver_helper_payload_retained",[&](auto& f){
                                f.add("sample",bank.samples);f.add("epoch",receiver.epoch);
                            });
                        }
                        if(audio::trace::active.load() && !receiver.bursts.empty()) {
                            static std::atomic<std::uint64_t> next_drain{0};const auto call=++next_drain;
                            for(std::size_t i=0;i<receiver.bursts.size();++i) {
                                const auto& b=receiver.bursts[i];
                                audio::trace::record("receiver_pattern_drain",[&](auto& f) {
                                    f.add("call",call);f.add("index",i);f.add("event_count",receiver.bursts.size());
                                    f.add("revision",bank.revision);f.add("epoch",receiver.epoch);f.add("family",receiver.family);
                                    f.add("captured_samples",bank.samples);f.add("sample_origin",receiver.sample_origin);
                                    f.add("stream_first_sample",b.stream_first_sample);f.add("stream_first_symbol",b.stream_first_symbol);
                                    f.add("first_sample",b.first_sample);f.add("end_sample",b.end_sample);f.add("first_stream_symbol",b.first_stream_symbol);
                                    f.add("stream_phase_samples",b.stream_phase_samples);f.add("frequency_hz",b.frequency_hz);
                                    f.add("accepted_count",b.bits.size());f.add("missing",b.missing_slots);f.add("complete",b.complete);
                                    f.add("scope","actual public detector drain; shared candidate selection/publication follows");
                                });
                            }
                        }
                        update_workspace();
                        if(bank.working_bytes>capacity)throw Error("receive event storage exceeds the configured DSP workspace");
                        if(local_projection) {
                            projection.remaining_headroom-=growth;
                            projection.deferred_detectors-=extra;
                            const auto retained=receiver.modem->working_bytes();
                            const auto promised=receiver.modem->reserved_workspace_bytes();
                            const auto pending=promised>retained?promised-retained:0;
                            if(pending>capacity-projection.deferred_detectors) {release_optimization();local_projection=nullptr;accounted=receiver_workspace(receiver);}
                            else projection.deferred_detectors+=pending;
                        }
                        continue;
                    }
                    {
                        rx_cpu::Scope cpu_publish(rx_cpu::Stage::publication);
                        if((bank.exact_source_samples || bank.exact_source_points || bank.helper_history) && std::any_of(receiver.bursts.begin(),receiver.bursts.end(),[](const auto& burst){
                            return !burst.bits.empty() || burst.missing_slots || burst.complete;})) {
                            // Accepted events may retain arbitrary allowed content.
                            // Required presentation/source growth is not bounded by
                            // the optional preparation cover; release before it.
                            drop_helper_history(bank);
                            drop_exact_source_pool(bank,"accepted_publication");drop_point_pool(bank,"accepted_publication");
                            if(bank.exact_source_points)throw Error("point pool still pinned at accepted publication barrier");
                            if(bank.exact_source_samples)throw Error("exact source pool still pinned at accepted publication barrier");
                        }
                        for(auto& burst:receiver.bursts) {
                            if(burst.bits.empty() && !burst.missing_slots && !burst.complete)continue;
                            const auto score=burst.score,frequency=burst.frequency_hz;
                            const auto stream_phase_samples=burst.stream_phase_samples;
                            const auto physical_id=std::pair{burst.stream_first_sample,burst.stream_first_symbol};
                            if(burst.end_sample>receiver.last_confident_end) {
                                receiver.last_confident_end=burst.end_sample;
                                receiver.last_confident_at=observation_epoch(bank,value,receiver.sample_origin+burst.end_sample);
                            }
                            auto presentation=receiver.presentations.find(physical_id);
                            if(presentation==receiver.presentations.end())continue;
                            auto& display=presentation->second;
                            const auto candidate=reception_candidate(receiver,burst,display,
                                bank.helper_conditioning || helper_assisted_receiver(bank,receiver));
                            trace_receiver_pattern_wire(bank,receiver,burst,display.candidate_id,
                                bank.samples+coordinate_count.value_or(sample_count));
                            auto result=transfer::detail::push_live_pending(
                                *receiver.content,std::move(burst),receiver.modem->diagnostics());
                            update_workspace();
                            if(bank.working_bytes>capacity)throw Error("receive content diagnostics exceed the configured DSP workspace");
                            std::string bits;bits.reserve(result.raw_bits.size());
                            for(auto bit:result.raw_bits)bits.push_back(bit?'1':'0');
                            DATAPUMP_MUTEX_CONTEXT(mutex_context,phase1);
                            std::lock_guard lock(mutex);
                            if(!current.running || generation!=version || bank.revision!=receive_revision || stop.stop_requested())return;
                            const auto complete=result.stream_complete;
                            const auto decision=bank.receptions.observe(candidate,[&]{return next_signal++;});
                            bank.limited|=decision.limited;
                            if(decision.limited && !decision.selected)bank.coverage_limited=true;
                            display.signal_id=decision.signal_id;
                            if(decision.selected) {
                                SignalUpdate event;event.id=decision.signal_id;event.frequency_hz=frequency;
                                event.revision=decision.revision;
                                event.superseded_ids.assign(decision.superseded_ids.begin(),decision.superseded_ids.end());
                                result.content.message.local_id=content_identity(event.id);
                                event.text=std::move(bits);event.binary=true;event.complete=result.stream_complete;
                                event.recovery_progress=result.recovery_progress;
                                if(simulation_wave && result.recovery_progress.state!=transfer::RecoveryState::none)
                                    simulation_wave->recovery_clear_generation=recovery_clear_generation;
                                event.received_bits=result.observed_bits;event.pattern_score=score;event.missing_symbols=result.missing_symbols;
                                if(result.content_validated) {
                                    event.text=display_text(result.content.message);event.binary=false;event.validated=true;
                                    event.reception_id=reception_id(result.content.message);event.pre_fec_accuracy=result.content.pre_fec_accuracy;event.fec_stats=result.content.fec_stats;
                                }
                                if(result.short_text_decoded) {
                                    event.raw_bits=std::move(event.text);
                                    event.text.assign(result.content.message.data.begin(),result.content.message.data.end());
                                    event.binary=false;
                                    event.reception_id=reception_id(result.content.message);
                                }
                                const bool revised=display.published_revision!=decision.revision;
                                // Hardware/ordinary idle publication retains its
                                // existing replacement-before-recovery order.
                                // Simulation defers withdrawal until admission.
                                if(revised && !simulation_wave)discard_obsolete(event);
                                const bool recovery=result.recovery && result.stream_complete && !result.content_validated;
                                const bool report_content=!recovery && (result.content_validated || result.short_text_decoded) && !display.content_reported;
                                if(recovery && !simulation_wave)queue_recovery(std::move(result),event);
                                audio::trace::record("receiver_signal_commit",[&](auto& f) {
                                    f.add("cohort_start_sample",bank.samples);f.add("feed_end_sample",bank.samples+coordinate_count.value_or(sample_count));
                                    f.add("captured_samples",current.samples_received);f.add("input_revision",bank.revision);f.add("epoch",receiver.epoch);f.add("family",receiver.family);
                                    f.add("signal_id",event.id);f.add("revision",event.revision);f.add("received_bits",event.received_bits);
                                    f.add("raw_bits_prefix",(event.raw_bits.empty()?event.text:event.raw_bits).substr(0,128));
                                    f.add("candidate_id",candidate.id);f.add("candidate_score",candidate.score);
                                    f.add("sample_origin",receiver.sample_origin);f.add("symbol_samples",candidate.symbol_samples);
                                    f.add("keyed",receiver.options.key.has_value());
                                    f.add("helper_assisted",candidate.helper_assisted);
                                    f.add("burst_enabled",receiver.options.modem.symbol_burst.enabled());
                                    f.add("physical_first",candidate.first);f.add("physical_end",candidate.end);f.add("first_stream_symbol",physical_id.second);
                                    f.add("stream_phase_samples",stream_phase_samples);
                                    f.add("missing",event.missing_symbols);f.add("complete",event.complete);f.add("simulation",bool(simulation_wave));
                                    f.add("superseded_count",event.superseded_ids.size());
                                    f.add("scope","selected shared Live decision under snapshot mutex; next poll must expose this or a later prefix");
                                });
                                std::optional<transfer::Received> staged_result;
                                if(simulation_wave && (recovery || report_content))staged_result=std::move(result);
                                if(add_signal(std::move(event),simulation_wave,std::move(staged_result),simulation_wave && revised)) {
                                    if(revised)display.published_revision=decision.revision;
                                    if(report_content) {
                                        if(!simulation_wave) {
                                            admit_received(result.content.message.data.capacity(),value.content_limit);
                                            current.received.push_back(std::move(result));
                                        }
                                        display.content_reported=true;
                                    }
                                }
                            }
                            if(complete) {
                                receiver.presentations.erase(presentation);bank.reception_history_dirty=true;
                            }
                            update_workspace();
                            if(bank.working_bytes>capacity)throw Error("receive presentation state exceeds the configured DSP workspace");
                        }
                        receiver.bursts.clear();
                        bank.reception_history_dirty|=discard_retired_presentations(receiver);
                        retire_unreferenced_receptions(bank);
                        update_workspace();
                        continue;
                    }
                } catch (const Error& error) {
                    update_workspace();if(native_storage)native_storage->release();accounted=receiver_workspace(receiver);
                    if (stop.stop_requested()) return;
                    if(helper_assisted_receiver(bank,receiver)) {
                        if(!bank.helper_assistance->payload_retained) {
                            bank.helper_assistance->failed=true;receiver.bursts.clear();continue;
                        }
                        // A retained payload gets the same visible error and
                        // reconstruction policy as every ordinary receiver.
                        // The replacement searches normally, with no stale
                        // helper phase convention attached to its new origin.
                        const auto control=bank.helper_assistance->bytes();
                        bank.helper_assistance.reset();bank.working_bytes-=control;
                    }
                    release_optimization();accounted=receiver_workspace(receiver);
                    if(bank.helper_fallback_budget) {
                        // The saved reset-control overlap is available before
                        // local search copies are allocated. Peers keep all of
                        // their immutable finite-input promises throughout.
                        receiver.modem->trim_optional_workspace();update_workspace();
                        accounted=receiver_workspace(receiver);
                    }
                    {
                        {
                            std::lock_guard lock(mutex);
                            if(!current.running || generation!=version || bank.revision!=receive_revision || stop.stop_requested())return;
                            {
                                current.error=error.what();
                                current.receiver_health.receiver_reset=true;
                                // Reconstructed lanes have new origins even when the epoch is unchanged.
                                capture_future.reset();capture_quiet.reset();
                                if(value.transfer.modem.symbol_burst.enabled() || (value.transfer.modem.scheduled_hopping &&
                                   value.transfer.modem.scheduled_hopping->enabled() &&
                                   !value.transfer.modem.scheduled_hopping->continuous())) {
                                    // Queued sparse markers were proved against
                                    // the original bank. Reconstruction must not
                                    // consume them under different affine origins.
                                    // Keep the failure visible and restart capture;
                                    // this is loss/reset, never observed absence.
                                    count_dropped(current.receiver_health.dropped_samples,
                                        input_bytes/sizeof(float)+decoding_remaining_samples+delayed_processing_pcm_samples);
                                    reset_input_locked();
                                    bank.limited=true;
                                    return;
                                }
                            }
                        }
                        bank.limited=true;
                        auto next_input=helper_cohort_receiver(bank,receiver)?
                            bank.helper_cohort->filter_origin+bank.helper_cohort->filter.output_samples():bank.samples+sample_count;
                        modem::PatternSearch search;
                        std::optional<modem::OscillatorPatternSearch> declared_search;
                        // Reconstruct the same admitted families/profiles: plaintext is
                        // a family, while duplicate loaded keys are not extra banks.
                        std::vector<Bytes> fingerprints;
                        const auto add_family=[&](const Crypto& key) {
                            const auto fingerprint=key.mac(Bytes{'D','P','-','R','X','-','B','A','N','K'});
                            if(std::find(fingerprints.begin(),fingerprints.end(),fingerprint)==fingerprints.end())
                                fingerprints.push_back(fingerprint);
                        };
                        if(value.transfer.key)add_family(*value.transfer.key);
                        for(const auto& key:value.receive_keys)add_family(key);
                        const auto key_count=fingerprints.size()+static_cast<std::size_t>(value.permits_plaintext());
                        const auto profile_count=value.transfer.automatic_receive_profiles?
                            tuning::receive_profiles(value.transfer.modem,value.transfer.receive_targets_db_hz,
                                value.transfer.receive_pattern_mode,receiver.options.key.has_value()).size():1;
                        search.receiver_schedule_key_count=key_count;
                        search.receiver_bank_count=modem::receiver_strategy_bank_count(receiver.options.modem,
                            value.transfer.search_seconds,key_count,profile_count);
                        const auto owner_charge=bank.shared_capture?bank.shared_capture->capacity_reservation():0;
                        const auto cohort_share=(capacity-std::min(capacity,owner_charge))/std::max<std::size_t>(1,search.receiver_bank_count);
                        const auto control_margin=receiver_control_margin(receiver.options.modem,receiver.options.key.has_value());
                        search.receiver_strategy_workspace_bytes=cohort_share>control_margin?cohort_share-control_margin:1;
                        search.input_sample_origin=next_input;search.scheduled_capture=bank.shared_capture;
                        if(receiver.options.modem.oscillator_search) {
                            auto oscillator_search=modem::oscillator_pattern_search(receiver.options.modem,transfer::effective_clock_sync(receiver.options)?
                                modem::utc_transmit_rate_limit(receiver.options.modem):0);
                            declared_search=oscillator_search;
                            search.admitted_frequency_half_width_hz=oscillator_search.frequency.requested_half_width_hz;
                            search.admitted_clock_half_width_ppm=oscillator_search.requested_clock_half_width_ppm;
                            search.admitted_clock_cell_half_width_ppm=oscillator_search.clock_step_ppm/2;
                            search.complex_frequency_coverage=!oscillator_search.frequency.limited;
                            search.complex_clock_coverage=oscillator_search.clock_half_width_ppm>=oscillator_search.requested_clock_half_width_ppm;
                            search.hypotheses=std::move(oscillator_search.hypotheses);
                        }
                        else search.expand_clock_search=true;
                        search.prefer_streamed_templates=receiver.prefer_streamed_templates;
                        search.allow_local_clock_fallback=true;
                        search.bit_limit=transfer::pattern_bit_limit(value.content_limit);

                        search.compact_clock_search=receiver.options.key &&
                            modem::symbol_sample_count(receiver.options.modem)>=60ULL*receiver.options.modem.sample_rate;
                        search.search_stream_phases=receiver.options.key.has_value() &&
                            !receiver.options.modem.symbol_burst.enabled();
                        search.start_offset_seconds=static_cast<double>(receiver.epoch)-observation_epoch(bank,value,next_input);
                        const bool dated_calendar=!value.simulation && transfer::shared_hop_calendar(receiver.options.modem) && bank.timing;
                        if(!value.simulation && (!value.transfer.timestamp || dated_calendar) && value.transfer.clock_sync)
                            *search.start_offset_seconds+=value.transfer.clock_sync->offset_seconds;
                        if(value.simulation || (value.transfer.timestamp && !dated_calendar))
                            *search.start_offset_seconds+=(static_cast<double>(modem::training_sample_count(receiver.options.modem))+
                                static_cast<double>(modem::pattern_pulse_padding_samples(receiver.options.modem)))/receiver.options.modem.sample_rate;
                        search.start_uncertainty_seconds=value.transfer.search_seconds+1.;
                        receiver.clock_window_modeled=modeled_start_window(search,receiver.options.modem,receiver.epoch,bank,value,next_input);
                        if(value.transfer.clock_sync&&!search.qualified_start_window) {
                            auto amplitude_search=search;
                            if(modeled_start_window(amplitude_search,receiver.options.modem,receiver.epoch,
                                    bank,value,next_input,true))
                                search.amplitude_start_window=amplitude_search.qualified_start_window;
                        }
                        if(!value.transfer.timestamp&&!modem::scheduled_bursts(receiver.options.modem)) {
                            detail::prepare_public_amplitude_windows(receiver.options.modem,value,receiver.options.key.has_value(),search,key_count,profile_count);
                            if(search.amplitude_epoch_stride_seconds>1 && bank.public_amplitude_epoch_origin) {
                                search.amplitude_start_offset_seconds=static_cast<double>(static_cast<long double>(receiver.epoch)-
                                    *bank.public_amplitude_epoch_origin-static_cast<long double>(next_input)/receiver.options.modem.sample_rate);
                                search.amplitude_lookahead_epochs=detail::amplitude_epoch_lookahead(receiver.options.modem,search,
                                    1.L/receiver.options.modem.sample_rate,0);
                            }
                            if(!receiver.options.key&&bank.timing)
                                search.amplitude_lookahead_epochs=std::max(search.amplitude_lookahead_epochs,
                                    detail::amplitude_epoch_lookahead(receiver.options.modem,search,
                                    bank.timing->seconds_per_frame,bank.timing->rate_uncertainty_fraction));
                            search.amplitude_receiver_bank_count=modem::receiver_strategy_amplitude_bank_count(
                                receiver.options.modem,search,
                                key_count,profile_count);
                            const auto amplitude_share=(capacity-std::min(capacity,owner_charge))/
                                std::max<std::size_t>(1,search.amplitude_receiver_bank_count);
                            search.amplitude_strategy_workspace_bytes=amplitude_share>control_margin?amplitude_share-control_margin:1;
                        }
                        const auto other=bank.working_bytes-accounted,overhead=accounted-receiver.modem->working_bytes();
                        if(other>capacity || overhead>capacity-other)throw;
                        auto remaining=capacity-other-overhead;
                        const auto cold=helper_replay_cold_grant(bank,receiver);
                        auto replay_ceiling=std::size_t{0};
                        const auto replay_end=bank.helper_fallback_budget?bank.helper_fallback_budget->fallback->end:0;
                        const auto next_sample=next_input;
                        if(cold) {
                            const auto proof=receiver.modem->helper_replay_budget(0,&search);
                            if(!proof || !proof->cold_constructor_bytes || next_sample>replay_end)
                                throw Error("ordinary reset differs from the certified helper replay recipe");
                            replay_ceiling=helper_replay_numeric_limit(bank,receiver);
                            const auto actual=receiver.modem->working_bytes();
                            const auto own_growth=replay_ceiling>actual?replay_ceiling-actual:0;
                            const auto unspent=helper_replay_unspent(bank);
                            if(unspent<own_growth || unspent-own_growth>remaining ||
                               replay_ceiling>remaining-(unspent-own_growth))
                                throw Error("ordinary reset cannot retain helper replay workspace promises");
                            remaining=cold;
                        }
                        auto selection=modem::resolve_receiver_strategy(receiver.options.modem,search,remaining);
                        amplitude_metadata(receiver,receiver.options.modem,search,selection);
                        if(declared_search && !(selection.supported && selection.full_frequency_coverage && selection.full_clock_coverage))
                            note_oscillator_coverage(bank,*declared_search);
                        const auto old_pool_bytes=native_pool_workspace(bank);
                        receiver.native_points.reset();
                        bank.working_bytes=bank.working_bytes-old_pool_bytes+native_pool_workspace(bank);
                        trace_receiver_acquisition_work(bank,true,"ordinary_row_reset",&receiver,true);
                        receiver.modem.reset();
                        receiver.modem=std::make_unique<modem::StreamingReceiver>(transfer::seeded_config(receiver.options,receiver.epoch),
                            cold?cold:selection.backend==modem::ReceiverBackend::existing_complex &&
                                !(selection.scheduled && !modem::packed_scheduled_symbols(receiver.options.modem))?remaining:
                                std::min(remaining,selection.selected_workspace_bytes),search);
                        if(cold)receiver.modem->set_workspace_bytes(replay_ceiling-sizeof(modem::StreamingReceiver));
                        if(helper_cohort_receiver(bank,receiver) &&
                           (selection.backend!=modem::ReceiverBackend::existing_complex || receiver.modem->clock_windowed())) {
                            // This replacement has observed no samples. Return
                            // unsupported backends to their original raw dates
                            // before removing the row from the helper cohort.
                            next_input=bank.samples+sample_count;
                            detail::ReceiverAdmissionContext admission;
                admission.simulation_capture_dated=bank.simulation_capture_dated;
                            admission.sample_origin=next_input;admission.observed_sample=bank.observed_sample;
                            admission.public_amplitude_epoch_origin=bank.public_amplitude_epoch_origin;
                            admission.timing=bank.timing;admission.now=observation_epoch(bank,value,next_input);
                            admission.propagation=!value.simulation && value.transfer.clock_sync?
                                value.transfer.clock_sync->offset_seconds:0;
                            admission.key_count=key_count;admission.profile_count=profile_count;admission.capacity=capacity;
                            admission.capture_owner_bytes=owner_charge;admission.control_margin=control_margin;
                            admission.keyed=receiver.options.key.has_value();
                            admission.prefer_streamed_templates=receiver.prefer_streamed_templates;
                            admission.compact_clock_search=search.compact_clock_search;
                            auto prepared=detail::prepare_receiver_search(
                                transfer::seeded_config(receiver.options,receiver.epoch),value,receiver.epoch,admission);
                            search=std::move(prepared.search);declared_search=std::move(prepared.declared_search);
                            receiver.clock_window_modeled=prepared.clock_window_modeled;
                            receiver.modem.reset();
                            selection=modem::resolve_receiver_strategy(receiver.options.modem,search,remaining);
                            amplitude_metadata(receiver,receiver.options.modem,search,selection);
                            receiver.modem=std::make_unique<modem::StreamingReceiver>(
                                transfer::seeded_config(receiver.options,receiver.epoch),
                                selection.backend==modem::ReceiverBackend::existing_complex &&
                                    !(selection.scheduled && !modem::packed_scheduled_symbols(receiver.options.modem))?remaining:
                                    std::min(remaining,selection.selected_workspace_bytes),search);
                            // A complete zero-input numeric reconstruction
                            // above restored raw dates before changing basis.
                            receiver.helper_flags&=static_cast<std::uint8_t>(~Receiver::helper_filtered_input);
                            receiver.helper_flags|=Receiver::helper_departure;
                            audio::trace::record("receiver_helper_cohort_backend_fallback",[&](auto& f) {
                                f.add("sample",next_input);f.add("epoch",receiver.epoch);f.add("raw_input",true);
                                f.add("backend",modem::receiver_backend_name(selection.backend));
                                f.add("clock_windowed",receiver.modem->clock_windowed());
                            });
                        }
                        bank.coverage_limited |= receiver.modem->local_clock_fallback();
                        update_workspace();bind_native_storage(bank,receiver,capacity);accounted=receiver_workspace(receiver);
                        if(cold) {
                            const auto proof=receiver.modem->helper_replay_budget(static_cast<std::size_t>(replay_end-next_sample));
                            if(!proof || !proof->steady_workspace_bytes || proof->steady_workspace_bytes>replay_ceiling ||
                               receiver.modem->clock_windowed())
                                throw Error("ordinary reset exceeded its certified finite helper replay envelope");
                            audio::trace::record("receiver_helper_replay_ordinary_reset",[&](auto& f) {
                                f.add("sample",next_sample);f.add("epoch",receiver.epoch);f.add("replay_end",replay_end);
                                f.add("feed_begin_sample",bank.samples);
                                f.add("cold_constructor_grant",cold);f.add("numeric_ceiling",replay_ceiling);
                                f.add("remaining_bound",proof->steady_workspace_bytes);
                            });
                        }
                        receiver.content=std::make_unique<transfer::StreamReceiver>(receiver.options,receiver.epoch,bank.source_quota);
                        receiver.admitted_at=observation_epoch(bank,value,next_input);
                        receiver.sample_origin=next_input;
                        receiver.last_confident_at=0;receiver.last_confident_end=0;
                        receiver.pattern_score_history = {};
                        receiver.history_ordinals={};receiver.history_poll={};
                        receiver.presentations.clear();
                        bank.reception_history_dirty=true;
                        receiver.bursts.clear();
                        if(simulation_media_clock(value))detail::simulation_checkpoint(
                            detail::SimulationPhase::helper_row_recovered,bank.samples+sample_count,
                            receiver.sample_origin,bank.revision,static_cast<double>(receiver.epoch));
                    }
                    update_workspace();
                }catch(const std::bad_alloc&) {
                    if(!helper_assisted_receiver(bank,receiver) || bank.helper_assistance->payload_retained)throw;
                    update_workspace();if(native_storage)native_storage->release();
                    bank.helper_assistance->failed=true;receiver.bursts.clear();
                }
            }
            if(phase==0) {
                projection.release();
                if(bank.helper_cohort) {
                    auto& cohort=*bank.helper_cohort;
                    bool accepted=false,had_prior=false;
                    const auto defer_error=[&](Receiver& receiver,const Error& error,const char* stage) {
                        receiver.helper_flags|=Receiver::helper_deferred_error;
                        const bool retained=cohort_failure_count<cohort_failures.size();
                        if(retained)cohort_failures[cohort_failure_count++]={&receiver,std::current_exception()};
                        audio::trace::record("receiver_helper_cohort_error",[&](auto& f) {
                            f.add("sample",bank.samples+sample_count);f.add("revision",bank.revision);
                            f.add("epoch",receiver.epoch);f.add("family",receiver.family);
                            f.add("instance_id",receiver.point_consumer);f.add("sample_origin",receiver.sample_origin);
                            f.add("stage",stage);f.add("reason",error.what());f.add("exception_retained",retained);
                        });
                    };
                    std::size_t conditional_rows=0,aligned_rows=0,declined_rows=0,audited_rows=0;
                    const auto operate=[&](Receiver& receiver,auto operation) {
                        auto accounted=receiver_workspace(receiver);
                        NativeStorageScope storage(bank,receiver,capacity,accounted);
                        const auto other=bank.working_bytes-accounted;
                        const auto overhead=accounted-receiver.modem->working_bytes();
                        const auto before_work=receiver.modem->fft_work();
                        const auto settle=[&] {
                            const auto after_work=receiver.modem->fft_work();
                            record_helper_cohort_phase_work(bank,receiver,before_work,bank.samples+sample_count);
                            cohort.conditional_jobs+=after_work.conditional_template_jobs-before_work.conditional_template_jobs;
                            cohort.audit_jobs+=after_work.original_audit_template_jobs-before_work.original_audit_template_jobs;
                            record_history_generation(receiver);
                            bank.working_bytes=bank.working_bytes-accounted+receiver_workspace(receiver);
                            accounted=receiver_workspace(receiver);
                        };
                        try {
                            if(other>capacity || overhead>capacity-other)
                                throw Error("ordinary helper cohort exceeds its receiver workspace");
                            const auto allowance=capacity-other-overhead;
                            if(allowance<=sizeof(modem::StreamingReceiver))throw Error("ordinary helper cohort has no numeric loan");
                            receiver.modem->set_workspace_bytes(allowance-sizeof(modem::StreamingReceiver));
                            operation();
                        }catch(...) {settle();throw;}
                        settle();
                    };
                    // Close the observation batch before replacing a prior.
                    // The newest preceding measurement may refine a mask that
                    // was first installed many seconds before this FFT filled.
                    if(cohort.qualified)for(auto& receiver:bank.receivers()) {
                        if(!helper_cohort_eligible(bank,receiver))continue;
                        const auto state=receiver.modem->acquisition_state();
                        if(!state.supported || !state.ready || state.prior_pending)continue;
                        try {operate(receiver,[&] {
                            bool applied=false;
                            detail::HelperPriorFacts facts;
                            try {applied=set_helper_cohort_prior(bank,receiver,&facts);}
                            catch(const Error&) {facts.reason=cohort.reason="helper prior workspace unavailable; broad acquisition retained";}
                            catch(const std::bad_alloc&) {facts.reason=cohort.reason="helper prior workspace unavailable; broad acquisition retained";}
                            if(!applied)receiver.modem->clear_acquisition_prior();
                            const auto selected=receiver.modem->acquisition_state();
                            audio::trace::record("receiver_helper_cohort_prior_decision",[&](auto& f) {
                                f.add("sample",bank.samples+sample_count);f.add("revision",bank.revision);
                                f.add("epoch",receiver.epoch);f.add("family",receiver.family);
                                f.add("instance_id",receiver.point_consumer);f.add("sample_origin",receiver.sample_origin);
                                f.add("origin_hz",facts.origin_hz);f.add("original_pairs",state.original_pairs);
                                f.add("selected_pairs",facts.selected);f.add("measured_pairs",facts.measured_selected);
                                f.add("clock_rates",facts.rates);f.add("represented_rates",facts.represented_rates);
                                f.add("radius_hz",facts.radius_hz);f.add("clock_cell_half_width_ppm",facts.clock_cell_half_width_ppm);
                                f.add("reference_offset_hz",cohort.reference?cohort.reference->frequency_hz-cohort.expected_hz:0.);
                                f.add("reference_uncertainty_hz",cohort.reference?cohort.reference->frequency_uncertainty_hz:0.);
                                f.add("supported",selected.supported);f.add("ready",selected.ready);
                                f.add("prior_active",selected.prior_active);f.add("prior_pending",selected.prior_pending);
                                f.add("admitted",applied);f.add("reason",facts.reason);
                            });
                        });}catch(const Error& error) {
                            if(stop.stop_requested())return;
                            defer_error(receiver,error,"prior_admission");
                        }
                    }
                    // First all ready keys/profiles/epochs compete under the
                    // same measured link prior. A wrong old epoch cannot run
                    // its expensive broad miss before the correct epoch.
                    for(auto& receiver:bank.receivers()) {
                        if(!helper_cohort_eligible(bank,receiver) || (receiver.helper_flags&Receiver::helper_deferred_error))continue;
                        const auto state=receiver.modem->acquisition_state();
                        if(!state.supported || !state.ready || !state.prior_active)continue;
                        had_prior=true;
                        try {operate(receiver,[&] {
                            try {
                                if(simulation_media_clock(value))detail::simulation_checkpoint(
                                    detail::SimulationPhase::helper_prior,bank.samples+sample_count,
                                    receiver.sample_origin,bank.revision,static_cast<double>(receiver.epoch));
                                accepted=receiver.modem->process_prior_acquisition(stop)||accepted;
                            }
                            catch(const Error& error) {
                                if(stop.stop_requested())throw;
                                // The core retains this same original cursor
                                // and accepted tracks on a failed prior pass.
                                // Audit before invoking ordinary row recovery.
                                if(simulation_media_clock(value))detail::simulation_checkpoint(
                                    detail::SimulationPhase::helper_audit,bank.samples+sample_count,
                                    receiver.sample_origin,bank.revision,static_cast<double>(receiver.epoch));
                                receiver.modem->resolve_acquisition(false,stop);
                                auto drained=receiver.modem->take_pattern_bursts();
                                receiver.bursts.insert(receiver.bursts.end(),std::make_move_iterator(drained.begin()),
                                    std::make_move_iterator(drained.end()));
                                cohort.qualified=cohort.active=false;cohort.lease_until=cohort.probation_until=0;
                                cohort.reason="optional helper acquisition declined; original same-window audit retained";
                                audio::trace::record("receiver_helper_cohort_fallback",[&](auto& f) {
                                    f.add("sample",bank.samples);f.add("epoch",receiver.epoch);f.add("reason",error.what());
                                });
                            }
                        });}catch(const Error& error) {
                            if(stop.stop_requested())return;
                            defer_error(receiver,error,"conditional_acquisition");
                        }
                    }
                    const auto frontier=bank.samples+sample_count;
                    const auto interval=std::max<std::uint64_t>(6ULL*value.transfer.modem.sample_rate,
                        2*modem::symbol_sample_count(value.transfer.modem));
                    if(accepted && cohort.lease_compatible)cohort.lease_until=frontier+interval;
                    const bool link=cohort.qualified &&
                        (frontier<=cohort.probation_until || frontier<=cohort.lease_until);
                    // Audit dates belong to a key/profile family, not to
                    // disposable rolling rows. The first actual ready batch
                    // arms a finite deadline AFTER its unavoidable FFT fill.
                    for(auto& group:cohort.groups)group.audit_now=false;
                    for(const auto& receiver:bank.receivers()) {
                        if(!helper_cohort_eligible(bank,receiver) || (receiver.helper_flags&Receiver::helper_deferred_error))continue;
                        const auto state=receiver.modem->acquisition_state();
                        if(!state.supported || !state.ready)continue;
                        auto& group=cohort.groups[receiver.helper_audit_group];
                        if(!group.due)group.due=detail::helper_audit_deadline(frontier,
                            value.transfer.modem.sample_rate,group.symbol_samples);
                        group.audit_now|=frontier>=group.due;
                    }
                    bool audited=false;
                    for(auto& receiver:bank.receivers()) {
                        if(!helper_cohort_eligible(bank,receiver) || (receiver.helper_flags&Receiver::helper_deferred_error))continue;
                        const auto state=receiver.modem->acquisition_state();
                        if(!state.supported || !state.ready)continue;
                        auto& group=cohort.groups[receiver.helper_audit_group];
                        const bool conditional=state.prior_pending && link && !group.audit_now;
                        const auto* resolution_reason=conditional?
                            (state.selected_pairs<state.original_pairs?"conditional acquisition narrowing":"conditional RF alignment without work reduction"):
                            group.audit_now?"periodic original broad audit":!cohort.qualified?"helper reference unavailable":
                            !link?"conditional link probation and lease expired":
                            "ordinary row has no eligible helper mask; see its prior decision";
                        try {operate(receiver,[&]{
                            receiver.modem->resolve_acquisition(conditional,stop);
                            auto drained=receiver.modem->take_pattern_bursts();
                            receiver.bursts.insert(receiver.bursts.end(),std::make_move_iterator(drained.begin()),
                                std::make_move_iterator(drained.end()));
                        });}catch(const Error& error) {
                            if(stop.stop_requested())return;
                            defer_error(receiver,error,"original_resolution");continue;
                        }
                        audio::trace::record("receiver_helper_cohort_resolution",[&](auto& f) {
                            f.add("sample",frontier);f.add("revision",bank.revision);f.add("epoch",receiver.epoch);
                            f.add("family",receiver.family);f.add("instance_id",receiver.point_consumer);
                            f.add("sample_origin",receiver.sample_origin);f.add("original_pairs",state.original_pairs);
                            f.add("selected_pairs",state.selected_pairs);f.add("origin_hz",state.frequency_origin_hz);
                            f.add("prior_pending",state.prior_pending);f.add("link",link);
                            f.add("audit_due",group.audit_now);f.add("conditional",conditional);f.add("reason",resolution_reason);
                        });
                        if(!conditional) {audited=true;++audited_rows;if(!state.prior_pending)++declined_rows;}
                        else {cohort.active=true;++conditional_rows;if(state.selected_pairs==state.original_pairs)++aligned_rows;}
                    }
                    for(std::size_t i=0;i<cohort.group_count;++i)if(cohort.groups[i].audit_now) {
                        auto& group=cohort.groups[i];
                        group.due=detail::helper_audit_deadline(frontier,value.transfer.modem.sample_rate,
                            group.symbol_samples);
                    }
                    if(audited && cohort.qualified && frontier>cohort.probation_until &&
                       frontier>cohort.lease_until)cohort.probation_until=frontier+interval;
                    cohort.original_pairs=cohort.selected_pairs=0;
                    bool narrowing=false,alignment=false;
                    for(const auto& receiver:bank.receivers()) {
                        if(!helper_cohort_eligible(bank,receiver) || (receiver.helper_flags&Receiver::helper_deferred_error))continue;
                        const auto state=receiver.modem->acquisition_state();
                        cohort.original_pairs=std::max<std::uint64_t>(cohort.original_pairs,state.original_pairs);
                        if(state.prior_active) {
                            cohort.selected_pairs=std::max<std::uint64_t>(cohort.selected_pairs,state.selected_pairs);
                            narrowing|=state.selected_pairs<state.original_pairs;
                            alignment|=state.selected_pairs==state.original_pairs && state.frequency_origin_hz!=0;
                        }
                    }
                    cohort.active=cohort.qualified && cohort.selected_pairs && cohort.conditional_jobs &&
                        (frontier<=cohort.probation_until || frontier<=cohort.lease_until);
                    if(cohort.active && narrowing)cohort.reason=declined_rows?
                        "qualified helper narrows eligible ordinary rows; other rows retain broad acquisition":
                        "qualified helper narrows ordinary rolling acquisition; periodic broad audits retained";
                    else if(cohort.active && alignment)cohort.reason=declined_rows?
                        "qualified helper aligns eligible ordinary rows without reducing their work; other rows retain broad acquisition":
                        "qualified helper aligns ordinary acquisition frequencies without reducing search work";
                    else if(audited && had_prior && cohort.qualified)
                        cohort.reason="ordinary broad audit; measured helper prior remains available";
                    audio::trace::record("receiver_helper_cohort_dispatch",[&](auto& f) {
                        f.add("sample",frontier);f.add("qualified",cohort.qualified);f.add("conditional",cohort.active);
                        f.add("accepted",accepted);f.add("audited",audited);f.add("original_pairs",cohort.original_pairs);
                        f.add("selected_pairs",cohort.selected_pairs);f.add("lease_until",cohort.lease_until);
                        f.add("conditional_rows",conditional_rows);f.add("alignment_rows",aligned_rows);
                        f.add("audited_rows",audited_rows);f.add("declined_rows",declined_rows);
                        f.add("reason",cohort.reason);
                    });
                }
                if(bank.helper_assistance && (bank.helper_assistance->failed ||
                   std::any_of(bank.receivers().begin(),bank.receivers().end(),[&](const auto& r){
                       return !helper_assisted_receiver(bank,r) && std::any_of(r.bursts.begin(),r.bursts.end(),[](const auto& b){
                           return !b.bits.empty() || b.missing_slots || b.complete;
                       });
                   })))drop_helper_assistance(bank,"ordinary_publication_or_optional_failure");
                DATAPUMP_MUTEX_CONTEXT(mutex_context,phase0);
                std::lock_guard lock(mutex);
                if(!current.running || generation!=version || bank.revision!=receive_revision || stop.stop_requested())return;
                for(auto& receiver:bank.receivers()) {
                    if(helper_assisted_receiver(bank,receiver) && !bank.helper_assistance->payload_retained && std::any_of(bank.receivers().begin(),bank.receivers().end(),[&](const auto& peer) {
                        return !helper_assisted_receiver(bank,peer) && peer.family==receiver.family &&
                            (!peer.bursts.empty() || !peer.presentations.empty());
                    }))receiver.bursts.clear(); // An ordinary pending stream always wins.
                    const auto before=receiver_workspace(receiver);
                    bank.reception_history_dirty|=discard_retired_presentations(receiver);
                    bank.working_bytes=bank.working_bytes-before+receiver_workspace(receiver);
                }
                retire_unreferenced_receptions(bank);
                for(auto& receiver:bank.receivers()) {
                    const auto before=receiver_workspace(receiver);
                    for(const auto& burst:receiver.bursts) {
                        if(burst.bits.empty() && !burst.missing_slots && !burst.complete)continue;
                        const auto physical_id=std::pair{burst.stream_first_sample,burst.stream_first_symbol};
                        auto presentation=receiver.presentations.find(physical_id);
                        if(presentation==receiver.presentations.end()) {
                            // Current detector tracks plus drained queued old
                            // prefixes: both are bounded at sixteen in Live.
                            if(receiver.presentations.size()>=transfer::StreamReceiver::candidate_limit) {
                                bank.limited=bank.coverage_limited=true;continue;
                            }
                            presentation=receiver.presentations.emplace(physical_id,
                                Receiver::Presentation{bank.next_candidate++}).first;
                        }
                        auto candidate=reception_candidate(receiver,burst,presentation->second,
                            bank.helper_conditioning || helper_assisted_receiver(bank,receiver));
                        candidate.complete=false;
                        const auto decision=bank.receptions.observe(candidate,[&]{return next_signal++;});
                        bank.limited|=decision.limited;
                        if(decision.limited && !decision.selected)bank.coverage_limited=true;
                        presentation->second.signal_id=decision.signal_id;
                    }
                    bank.working_bytes=bank.working_bytes-before+receiver_workspace(receiver);
                }
            }
            if(phase==1 && bank.helper_cohort) {
                for(auto& receiver:bank.receivers())clear_helper_row_transients(receiver);
                joined_helper_flags.pending=false;
            }
        }
        // Every accepted bit/completion from every profile was published above.
        // Optional history materialization cannot hold these events behind an
        // unrelated numerical diagnostic, including during an idle drain.
        for(auto& receiver:bank.receivers()) {
            execution::checkpoint();if(stop.stop_requested())return;
            if(helper_assisted_receiver(bank,receiver))continue;
            if(!receiver.modem->deferred_pattern_history())query_history(receiver,false);
        }
        // A cohort-wide fair budget, rather than 4 Hz multiplied by every key
        // and rolling epoch. Pressure queries above remain necessary and bypass
        // only this optional diagnostic cadence, never accepted publication.
        const auto* history_mode=std::getenv("DATAPUMP_RX_HISTORY_BACKLOG_PRIORITY");
        bool optional_history=true;
        if(!value.simulation && (!history_mode || std::string_view(history_mode)!="0")) {
            const auto extent=coordinate_count.value_or(sample_count);
            if(extent>std::numeric_limits<std::uint64_t>::max()-bank.samples)
                throw Error("diagnostic priority physical coordinate overflow");
            const auto prospective=bank.samples+extent;
            const auto committed=committed_bank_frontier(bank,prospective);
            std::lock_guard lock(mutex);
            optional_history=detail::optional_pattern_history_ready(prospective,committed,capture_samples,input.empty());
        }
        if(!optional_history && bank.samples/value.transfer.modem.sample_rate!=
           (bank.samples+sample_count)/value.transfer.modem.sample_rate)
            audio::trace::record("receiver_history_backlog_priority",[&](auto& f) {
                f.add("sample",bank.samples+sample_count);f.add("revision",bank.revision);
                f.add("pressure_queries_unchanged",true);f.add("accepted_publication_unchanged",true);
            });
        if(optional_history && !bank.receivers().empty() && Clock::now()>=bank.next_deferred_history_query) {
            const auto count=bank.receivers().size();
            auto index=bank.deferred_history_cursor%count;
            auto next=std::next(bank.receivers().begin(),static_cast<std::ptrdiff_t>(index));
            for(std::size_t attempt=0;attempt<count;++attempt) {
                auto& receiver=*next;
                ++bank.deferred_history_cursor;
                const auto following=bank.deferred_history_cursor%count;
                if(following==(index+1)%count) {
                    if(++next==bank.receivers().end())next=bank.receivers().begin();
                }else next=std::next(bank.receivers().begin(),static_cast<std::ptrdiff_t>(following));
                index=following;
                if(!receiver.modem->deferred_pattern_history() ||
                   !receiver.history_poll.due(receiver.modem->diagnostic_history_generation(),Clock::now()))continue;
                query_history(receiver,false);
                bank.next_deferred_history_query=Clock::now()+detail::DeferredPatternHistoryPoll::period;
                break;
            }
        }
        // Advisory cadence runs only after feed, queued processing, publication
        // and every numerical worker/Reader have joined. Never a per-q hot log.
        if(bank.exact_source_samples && Clock::now()>=bank.next_exact_source_trace) {
            trace_exact_source_pool(bank,"joined_poll");bank.next_exact_source_trace=Clock::now()+std::chrono::seconds(1);
        }
        if(bank.exact_source_points && Clock::now()>=bank.next_point_source_trace) {
            trace_point_pool(bank,"joined_poll");bank.next_point_source_trace=Clock::now()+std::chrono::seconds(1);
        }
        if(bank.source_curves && bank.samples/value.transfer.modem.sample_rate!=
           (bank.samples+sample_count)/value.transfer.modem.sample_rate) {
            const auto stats=bank.source_curves->statistics();
            audio::trace::record("receiver_source_curve_counts",[&](auto& f) {
                f.add("sample",bank.samples+sample_count);
                f.add("counter_scope","current bank pool lifetime; successive records are cumulative");
                f.add("bytes",bank.source_curves->working_bytes());f.add("rows",bank.source_curves->capacity());
                f.add("hits",stats.hits);f.add("misses",stats.misses);f.add("builds",stats.builds);
                f.add("busy",stats.busy);f.add("evictions",stats.evictions);f.add("exhaustions",stats.exhaustions);
                f.add("failed_builds",stats.failed_builds);
            });
        }
        if(permutation_receivers && (
#if defined(DATAPUMP_PERMUTATION_TRACE_EVERY_PUSH)
           true ||
#endif
           bank.samples/value.transfer.modem.sample_rate!=
           (bank.samples+sample_count)/value.transfer.modem.sample_rate)) {
            audio::trace::record("receiver_permutation_counts",[&](auto& f) {
                f.add("sample",bank.samples+sample_count);f.add("resident",bank.receivers().size());
                f.add("eligible",permutation_receivers);f.add("projection_context_created",projection_context_created);
                f.add("projection_context_used",projection_context_used);
                f.add("pool_present",static_cast<bool>(bank.permutations));
                f.add("row_limit",modem::detail::permutation_cache_max_rows);
                f.add("byte_limit",modem::detail::permutation_bank_byte_limit);
                if(bank.permutations) {
                    const auto rows=bank.permutations->statistics();
                    const auto all=bank.permutations->request_statistics();
                    f.add("reserved_bytes",bank.permutations->working_bytes());
                    f.add("allocated_bytes",bank.permutations->allocated_bytes());
                    f.add("count_classes",bank.permutations->count_classes());
                    f.add("row_capacity",bank.permutations->row_capacity());
                    f.add("effective_row_limit",bank.permutations->row_limit());
            const auto inner=bank.permutations->inner_coefficient_statistics();
            f.add("inner_bytes",bank.permutations->inner_coefficient_bytes());
            f.add("inner_requests",inner.requests);f.add("inner_hits",inner.hits);
            f.add("inner_builds",inner.builder_completed);f.add("inner_evictions",inner.evictions);
            f.add("inner_busy",inner.busy);f.add("inner_bypassed",inner.bypassed);
                    f.add("requests",all.requests);f.add("hits",all.hits);
                    f.add("builder_attempts",all.builder_attempts);f.add("builder_completed",all.builder_completed);
                    f.add("child_fallbacks",all.child_fallbacks);
                    f.add("row_misses",rows.misses);f.add("row_private_builds",rows.private_builds);
                    f.add("publications",rows.publications);f.add("busy",rows.busy);
                    f.add("evictions",rows.evictions);f.add("bypassed",rows.bypassed);
                }
            });
        }
        if(bank.helper_assistance && bank.helper_assistance->failed)drop_helper_assistance(bank,"optional_error");
        if(bank.helper_assistance) {
            const auto& state=*bank.helper_assistance;
            const auto& receiver=*state.receiver;
            const auto frontier=receiver.modem->delayed_processing_frontier();
            if((state.tail_only || state.payload_retained) &&
               (receiver.modem->initial_search_complete() || state.acquisition_frozen) &&
               !state.preroll_count && receiver.bursts.empty() && receiver.presentations.empty() &&
               !receiver.modem->acquiring() && !receiver.modem->synchronized() &&
               (!frontier || *frontier>=state.filter.output_samples()))
                drop_helper_assistance(bank,"observed_payload_tail_complete",true);
        }
        if(simulation_media_clock(value)) {
            detail::simulation_checkpoint(detail::SimulationPhase::before_commit,0,bank.samples,bank.revision);
            // A stopped receiver may return without consuming its whole span.
            // A committed simulation frontier must never certify that input.
            if(stop.stop_requested())return;
        }
        bank.samples+=sample_count;
        if(simulation_media_clock(value))
            detail::simulation_checkpoint(detail::SimulationPhase::after_commit,0,bank.samples,bank.revision);
        // Receiver histories are updated even while another receiver wins the
        // plot. Retained old evidence must not gain a new age or mask a fresh,
        // weaker receiver when the selected receiver changes.
        const auto now = replay_clock();
        const detail::PatternScoreHistory* selected = nullptr;
        double best_pattern_score = -1;
        for (const auto& receiver : bank.receivers()) {
            const auto score = receiver.pattern_score_history.best_score(now);
            if (score > best_pattern_score) {
                best_pattern_score = score; selected = &receiver.pattern_score_history;
            }
        }
        if (selected) {
            pattern_scores.reserve(selected->entries().size());
            pattern_observations.reserve(selected->entries().size());
            for (const auto& entry : selected->entries()) {
                if (detail::PatternScoreHistory::expired(entry.observation, now)) continue;
                const auto& candidate = entry.candidate;
                pattern_scores.emplace_back(candidate.bit ? candidate.alternative_score : candidate.score,
                                            candidate.bit ? candidate.score : candidate.alternative_score);
                pattern_observations.push_back(entry.observation);
            }
        }
        const auto observation_id = pattern_score_observation_id.load(std::memory_order_relaxed);
        bank.coverage_limited |= std::any_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver) {
            return receiver.modem->local_clock_fallback() || receiver.modem->candidate_limited();
        });
        trace_receiver_acquisition_work(bank,false,"feed_complete");
        update_symbol_burst_tracking(bank,value,version);
        // Publish a prospective Burst proof before publishing the matching
        // committed frontier. A fast capture provider can otherwise outrun
        // the proof between those two externally observable states.
        if(value.transfer.modem.symbol_burst.enabled() && account_frontier)
            publish_capture_quiet(bank,value,version,stop);
        DATAPUMP_MUTEX_CONTEXT(commit_context,bank_commit);
        std::lock_guard lock(mutex);
        if (current.running && generation == version && bank.revision==receive_revision && !stop.stop_requested()) {
            current.receiver_instances=bank.receivers().size();current.peak_receiver_instances=bank.peak_receivers;
            current.receiver_admissions=bank.admissions;current.receiver_retirements=bank.retirements;
            current.receiver_bank_builds=bank.builds;
            current.helper_frequency_assistance_active=bank.helper_cohort && bank.helper_cohort->active;
            const auto* cohort_phase=bank.helper_cohort?bank.helper_cohort->phase.get():nullptr;
            const bool cohort_phase_qualified=cohort_phase && cohort_phase->curve.qualified_until() &&
                bank.samples<=cohort_phase->curve.qualified_until();
            current.helper_phase_assistance_active=(cohort_phase_qualified && cohort_phase->jobs) ||
                (bank.helper_assistance && bank.helper_assistance->phase_applied &&
                 bank.helper_assistance->phase_qualified && !bank.helper_assistance->tail_only);
            current.helper_original_search_pairs=bank.helper_cohort?bank.helper_cohort->original_pairs:0;
            current.helper_selected_search_pairs=bank.helper_cohort?bank.helper_cohort->selected_pairs:0;
            current.helper_conditional_jobs=bank.helper_cohort?bank.helper_cohort->conditional_jobs:0;
            current.helper_audit_jobs=bank.helper_cohort?bank.helper_cohort->audit_jobs:0;
            if(bank.helper_cohort) {
                current.helper_status=bank.helper_cohort->reason;current.helper_status+="; ";
                current.helper_status+=current.helper_phase_assistance_active?
                    "phase and punctuation assistance active":cohort_phase_qualified?
                    "phase and punctuation qualified; waiting for eligible ordinary work":bank.helper_cohort->phase_reason;
            }
            else if(value.helper.power_w>0 && !current.helper_phase_assistance_active)
                current.helper_status=bank.helper_cohort_decline_reason;
            current.receiver_initial_fft_input_samples=0;
            for(const auto& receiver:bank.receivers())
                current.receiver_initial_fft_input_samples=std::max(current.receiver_initial_fft_input_samples,
                    receiver.modem->initial_fft_input_samples());
            current.receiver_error=bank.receiver_limit.empty()?std::string{}:
                (bank.receivers().empty()?"RX unavailable: ":"RX search incomplete: ")+bank.receiver_limit;
            current.clock_window_modeled=std::any_of(bank.receivers().begin(),bank.receivers().end(),
                [](const auto& receiver){return receiver.clock_window_modeled;});
            if(current.clock_window_modeled && bank.timing &&
               bank.timing->quality!=audio::TimingQuality::unavailable &&
               (transfer::shared_hop_calendar(value.transfer.modem) ||
                (bank.timing->uncertainty_seconds<=clock_sync::audio_lag_radius(value.transfer.audio_timing_error_seconds) &&
                 !capture_timing_fallback)))current.receiver_timing=bank.timing;
            else current.receiver_timing.reset();
            receive_pending=std::any_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver) {
                return receiver.modem->synchronized();
            });
            if (simulation_wave) {
                simulation_wave->pattern_scores = std::move(pattern_scores);
                simulation_wave->pattern_score_observations = std::move(pattern_observations);
                simulation_wave->pattern_score_observation_id = observation_id;
            } else if (value.simulation || value.full_duplex || !tx_busy) {
                if (current.pattern_scores != pattern_scores || current.pattern_score_observations != pattern_observations) {
                    current.pattern_scores = std::move(pattern_scores);
                    current.pattern_score_observations = std::move(pattern_observations); ++current.sequence;
                }
                current.pattern_score_observation_id = observation_id;
            }
            if(!value.simulation) {
                decoding_remaining_samples-=std::min(decoding_remaining_samples,samples.size());
                decoding_remaining_physical_samples-=std::min<std::uint64_t>(decoding_remaining_physical_samples,sample_count);
                if(account_frontier)publish_processing_frontier(bank);
            }
            receiver_bytes = bank.working_bytes;
            bank.coverage_limited |=
                std::any_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver) {
                    return receiver.modem->local_clock_fallback() || receiver.modem->candidate_limited();
                });
            current.receiver_health.search_limited |= bank.coverage_limited;
            if(bank.coverage_limited) {
                // Quota/candidate loss can also arise during this feed, after
                // its entry check. Revoke old proofs before publishing state.
                capture_future.reset();capture_quiet.reset();
            }
            if(!current.receiver_error.empty())current.status=current.receiver_error;
            else if(bank.limited)current.status=bank.oscillator_limit.empty()?
                "Pattern search is limited by the configured DSP workspace":bank.oscillator_limit;
            else if(std::any_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver) {
                return receiver.modem->local_clock_fallback();
            }))current.status="Using local carrier search; increase DSP memory for wider clock coverage";
            else if(current.status=="Pattern search is limited by the configured DSP workspace" ||
                    current.status.starts_with("RX oscillator coverage incomplete:") ||
                    current.status=="Using local carrier search; increase DSP memory for wider clock coverage")current.status=idle_status();
            current.dsp_buffered_bytes = receiver_bytes + queue_storage_bytes() + decoding_bytes + audio_bytes + plot_workspace(value) + (tx_busy ? value.dsp_workspace_bytes / 4 : 0);
        }
    }
    void update_symbol_burst_tracking(Bank& bank,const Settings& value,std::uint64_t version) {
        if(!value.transfer.modem.symbol_burst.enabled())return;
        {
            std::lock_guard lock(mutex);
            if(!current.running || generation!=version || bank.revision!=receive_revision)return;
        }
        const auto active=std::any_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver) {
            return receiver.modem->symbol_burst_tracking_active();
        });
        if(active && !bank.burst_admission_frozen) {
            // Joining feed workers precedes this barrier. Every original
            // first-start promise is retained; only new promises are frozen.
            for(auto& receiver:bank.receivers())receiver.modem->suspend_fresh_acquisition();
            bank.burst_admission_frozen=true;bank.burst_resume_sample.reset();
            std::lock_guard lock(mutex);
            if(!current.running || generation!=version || bank.revision!=receive_revision)return;
            bank.burst_admission_fence=++capture_burst_fence;
            capture_quiet.reset();
            capture_burst_timestamp.reset();
            if(bank.timing) {
                detail::BurstCaptureTimestamp stamp{bank.observed_sample,value.transfer.modem.sample_rate,*bank.timing};
                if(stamp.accepts(bank.observed_sample,0,bank.timing->utc_seconds,bank.timing))
                    capture_burst_timestamp=stamp;
            }
        }
        if(!bank.burst_admission_frozen || active)return;
        if(std::any_of(bank.receivers().begin(),bank.receivers().end(),[](const auto& receiver) {
            return !receiver.modem->initial_search_complete();
        }) || committed_bank_frontier(bank)<bank.samples)return;
        std::lock_guard lock(mutex);
        if(!current.running || generation!=version || bank.revision!=receive_revision)return;
        if(!bank.burst_resume_sample) {
            // A proof may already have queued sparse blocks beyond the final
            // scored symbol. Drain them under the old barrier before allowing
            // a new receiver to depend on PCM that was deliberately omitted.
            capture_quiet.reset();
            bank.burst_admission_fence=++capture_burst_fence;
            bank.burst_resume_sample=capture_samples;
        }
        if(bank.samples<*bank.burst_resume_sample)return;
        bank.burst_admission_frozen=false;bank.burst_resume_sample.reset();
        capture_burst_timestamp.reset();
        bank.burst_resume_pending=true;bank.retirement_pending=true;
    }
    void publish_capture_future(const Bank& bank,const Settings& value,std::uint64_t version,std::stop_token stop={}) {
        DATAPUMP_MUTEX_CONTEXT(mutex_context,capture_future);
        if(stop.stop_requested() || bank.shared_capture)return;
        const auto& nominal=value.transfer.modem;
        if(value.simulation || value.transfer.timestamp || bank.coverage_limited ||
           !nominal.scheduled_hopping || !nominal.scheduled_hopping->enabled() || nominal.scheduled_hopping->continuous())return;
        {
            std::lock_guard lock(mutex);
            if(capture_future_revoked || generation!=version || receive_revision!=bank.revision)return;
            if(capture_future && capture_future->admissions==bank.admissions &&
               bank.samples<capture_future->end-nominal.sample_rate)return;
        }
        const auto profiles=value.transfer.automatic_receive_profiles?
            tuning::receive_profiles(nominal,value.transfer.receive_targets_db_hz,value.transfer.receive_pattern_mode,true):
            std::vector<modem::Config>{nominal};
        const auto key_count=value.receive_keys.size()+static_cast<std::size_t>(value.transfer.key.has_value());
        // Reserve enough for old + building certificates, allocator overhead and
        // temporary keys/profiles. Bound allocation before constructing calendars.
        const auto per_bound=sizeof(modem::SchedHopSourceMap)+sizeof(detail::SchedCaptureBound)+128;
        const auto budget=value.dsp_workspace_bytes/256;
        if(budget<=sizeof(CaptureFutureProof) || !key_count ||
           key_count>(budget-sizeof(CaptureFutureProof))/per_bound/std::max<std::size_t>(1,profiles.size()))return;
        const auto future_count=key_count*profiles.size();
        const auto bound_limit=(budget-sizeof(CaptureFutureProof))/per_bound;
        auto proof=std::make_shared<CaptureFutureProof>();
        const auto reserve_count=std::min(bound_limit,bank.receivers().size()+future_count);
        proof->calendars.reserve(reserve_count);
        proof->bounds.reserve(reserve_count);
        struct CalendarIdentity {Bytes key_tag;const modem::SchedHopPlan* plan;};
        std::vector<CalendarIdentity> calendar_ids;calendar_ids.reserve(reserve_count);
        proof->identity={version,bank.revision};proof->begin=bank.samples;proof->admissions=bank.admissions;
        proof->rate=nominal.sample_rate;proof->utc_seconds=observation_epoch(bank,value,bank.samples);
        const auto horizon=60ULL*nominal.sample_rate;
        if(horizon>std::numeric_limits<std::uint64_t>::max()-bank.samples)return;
        proof->end=bank.samples+horizon;
        const auto add=[&](const modem::Config& c,const Crypto& key,const Bytes& tag,modem::ScheduledCaptureEnvelope envelope) {
            // The source calendar depends only on this key, immutable plan and
            // sample rate, not payload epoch/bit. Union affine bounds at the
            // same physical reference; never share private pattern templates.
            for(std::size_t i=0;i<calendar_ids.size();++i)if(calendar_ids[i].key_tag==tag &&
                calendar_ids[i].plan==c.scheduled_hopping.get()) {
                auto& b=proof->bounds[i].envelope;
                if(envelope.lower.distance(b.lower)<0)b.lower=envelope.lower;
                if(envelope.upper.distance(b.upper)>0)b.upper=envelope.upper;
                b.minimum_rate=std::min(b.minimum_rate,envelope.minimum_rate);
                b.maximum_rate=std::max(b.maximum_rate,envelope.maximum_rate);
                return true;
            }
            if(proof->bounds.size()>=bound_limit)return false;
            proof->calendars.push_back(std::make_unique<modem::SchedHopSourceMap>(*c.scheduled_hopping,key,c.sample_rate));
            proof->bounds.push_back({proof->calendars.back().get(),envelope});
            calendar_ids.push_back({tag,c.scheduled_hopping.get()});return true;
        };
        // Resident bounds retain their ORIGINAL epochs and affine origins, no
        // matter how old a split symbol is. New admission never resets that age.
        for(const auto& receiver:bank.receivers()) {
            const auto envelope=receiver.modem->scheduled_capture_envelope(bank.samples-receiver.sample_origin);
            if(!envelope || !receiver.options.key)return;
            auto bound=*envelope;bound.reference_sample=bank.samples;
            if(!add(receiver.options.modem,*receiver.options.key,receiver.key_tag,bound))return;
        }
        std::vector<Crypto> keys;
        if(value.permits_plaintext())return;
        if(value.transfer.key)keys.push_back(*value.transfer.key);
        keys.insert(keys.end(),value.receive_keys.begin(),value.receive_keys.end());
        if(keys.empty())return;
        for(const auto& key:keys) {
            const auto tag=key.mac(Bytes{'D','P','-','R','X','-','B','A','N','K'});
            for(const auto& c:profiles) {
                if(!c.scheduled_hopping || !c.scheduled_hopping->enabled() || c.scheduled_hopping->continuous() ||
                   c.sample_rate!=nominal.sample_rate)return;
                detail::SchedCaptureFuture future;
                future.reference_sample=bank.samples;future.horizon_samples=horizon;future.sample_rate=c.sample_rate;
                const auto ticks=proof->utc_seconds*c.sample_rate;
                if(ticks<0 || ticks>=static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))return;
                future.utc_at_reference={static_cast<std::uint64_t>(std::floor(ticks)),ticks-std::floor(ticks)};
                future.utc_rate_lower=.999L;future.utc_rate_upper=1.001L;
                future.timestamp_radius_samples=c.sample_rate+2.L;
                // The amplitude geometry refuses a declared clock domain above
                // 10,000 ppm. Include that entire domain, not its selected grid
                // or the requested tight GPS prior. A later wider backend must
                // extend this certificate before it can use discarded capture.
                // Include the full outer clock cell, not merely its center lane.
                future.minimum_rate=.979999L;future.maximum_rate=1.020001L;
                future.propagation_samples=value.transfer.clock_sync?value.transfer.clock_sync->offset_seconds*c.sample_rate:0;
                future.epoch_rounding_radius_samples=(value.transfer.search_seconds+1.L)*c.sample_rate;
                future.start_uncertainty_samples=(value.transfer.search_seconds+1.L)*c.sample_rate+
                    static_cast<long double>(modem::pattern_chip_samples(c))/4/future.minimum_rate;
                future.maximum_phase_samples=c.sample_rate-1;
                const auto envelope=detail::sched_capture_future_envelope(future);
                if(!envelope)return;
                if(!add(c,key,tag,*envelope))return;
            }
        }
        // Control certificates share the existing DSP reservation. No PCM
        // matrix or symbol-sized buffer is allocated here.
        const auto bytes=sizeof(CaptureFutureProof)+proof->calendars.capacity()*(sizeof(modem::SchedHopSourceMap)+sizeof(void*)+128)+
            proof->bounds.capacity()*sizeof(detail::SchedCaptureBound);
        if(bytes>budget)return;
        std::lock_guard lock(mutex);
        if(current.running && generation==version && receive_revision==bank.revision && !capture_future_revoked && !stop.stop_requested())
            capture_future=std::move(proof);
    }
    void publish_capture_quiet(const Bank& bank,const Settings& value,std::uint64_t version,std::stop_token stop={}) {
        DATAPUMP_MUTEX_CONTEXT(mutex_context,capture_quiet);
        if(value.simulation || stop.stop_requested())return;
        const auto burst=value.transfer.modem.symbol_burst.enabled();
        const auto trace_burst=burst && bank.samples%value.transfer.modem.sample_rate<value.transfer.modem.sample_rate/20+1;
        std::optional<detail::BurstCaptureTimestamp> burst_stamp;
        {
            std::lock_guard lock(mutex);
            if(trace_burst)audio::trace::record("symbol_burst_capture_state",[&](auto& f) {
                f.add("sample",bank.samples);f.add("frozen",bank.burst_admission_frozen);
                f.add("resuming",bank.burst_resume_sample.has_value());f.add("revoked",capture_burst_revoked);
                f.add("anchor",capture_burst_timestamp.has_value());f.add("fence",bank.burst_admission_fence);
                f.add("observed_sample",bank.observed_sample);
                if(capture_burst_timestamp) {
                    f.add("anchor_sample",capture_burst_timestamp->reference_sample);
                    f.add("anchor_utc",capture_burst_timestamp->reference.utc_seconds);
                    f.add("anchor_radius",capture_burst_timestamp->reference.uncertainty_seconds);
                    f.add("anchor_rate",capture_burst_timestamp->reference.seconds_per_frame);
                }
                if(bank.timing){f.add("utc",bank.timing->utc_seconds);f.add("radius",bank.timing->uncertainty_seconds);}
            });
            // Reuse an exact current-cohort proof only within its original UTC
            // admission fence. A broad ahead proof may retain every sample;
            // it must not suppress this independently useful shorter proof.
            if(burst && capture_burst_revoked)return;
            if(burst) {
                burst_stamp=capture_burst_timestamp;
                // Also reconcile dated input captured before the decoder
                // erected its admission barrier. Never renew old origins with
                // a later, wider or contradictory timestamp model.
                if(burst_stamp && (!bank.timing || !burst_stamp->accepts(bank.observed_sample,0,
                    bank.timing->utc_seconds,bank.timing))) {
                    capture_quiet.reset();capture_burst_revoked=true;return;
                }
            }
            if(!burst && capture_quiet && capture_quiet->revision==bank.revision && bank.samples<capture_quiet->end &&
               capture_quiet->utc_second==std::floor(bank.created_at))return;
        }
        std::optional<CaptureQuietProof> proof;
        if(bank.shared_capture)return;
        const auto& config=value.transfer.modem;
        if((burst || (config.scheduled_hopping && config.scheduled_hopping->enabled() &&
           !config.scheduled_hopping->continuous())) && !bank.coverage_limited &&
           (!burst || (bank.burst_admission_frozen && !bank.burst_resume_sample && !bank.receivers().empty()))) {
            auto end=std::numeric_limits<std::uint64_t>::max();
            bool valid=true;
            for(const auto& receiver:bank.receivers()) {
                const auto local=bank.samples-receiver.sample_origin;
                const auto limit=receiver.modem->unobserved_end_from(local);
                if(trace_burst)audio::trace::record("symbol_burst_capture_consumer",[&](auto& f) {
                    f.add("sample",bank.samples);f.add("origin",receiver.sample_origin);f.add("epoch",receiver.epoch);
                    f.add("initial_complete",receiver.modem->initial_search_complete());
                    f.add("tracking",receiver.modem->symbol_burst_tracking_active());
                    f.add("limit",limit.value_or(0));f.add("proven",limit.has_value());
                });
                if(!limit || *limit>std::numeric_limits<std::uint64_t>::max()-receiver.sample_origin){valid=false;break;}
                end=std::min(end,receiver.sample_origin+*limit);
            }
            // An empty bank is valid only after make_bank proved every current
            // epoch impossible. Expire before the next rolling-admission fence.
            if(valid && end>bank.samples && !burst)
                proof=CaptureQuietProof{bank.revision,bank.samples,end,std::floor(bank.created_at)};
            else if(valid && end>bank.samples && burst_stamp) {
                proof=CaptureQuietProof{bank.revision,bank.samples,end,0,version,bank.burst_admission_fence,burst_stamp};
            }
        }
        std::lock_guard lock(mutex);
        if(current.running && generation==version && receive_revision==bank.revision && !stop.stop_requested() &&
           (!burst || (!capture_burst_revoked && bank.burst_admission_fence==capture_burst_fence)))
            capture_quiet=proof;
    }
    void feed_unobserved(Bank& bank,std::uint64_t count,const Settings& value,std::uint64_t version,std::stop_token stop) {
        // A capture certificate has already proved no retained lane can consume
        // this interval. Advance physical coordinates only, never absent fits.
        feed_bank(bank,value,version,stop,{},[&](auto& row,auto*) {
            auto& receiver=*row.modem;
            if(!receiver.try_skip_unobserved(count,stop))
                throw Error("scheduled capture certificate no longer covers the receiver bank");
        },nullptr,true,false,count);
        if(stop.stop_requested())return; // an error reset invalidated the old bank/proof
        if(count>std::numeric_limits<std::uint64_t>::max()-bank.samples)
            throw Error("scheduled capture coordinate exhausted");
        bank.samples+=count;
        update_symbol_burst_tracking(bank,value,version);
        if(value.transfer.modem.symbol_burst.enabled())publish_capture_quiet(bank,value,version,stop);
        bank.mixer*=std::polar(1.,-2*std::numbers::pi*std::remainder(
            static_cast<double>(count)*value.transfer.modem.carrier_hz/value.transfer.modem.sample_rate,1.));
        bank.mixer/=std::abs(bank.mixer);
        std::lock_guard lock(mutex);
        if(current.running && generation==version && bank.revision==receive_revision && !stop.stop_requested()) {
            decoding_remaining_physical_samples-=std::min(decoding_remaining_physical_samples,count);
            publish_processing_frontier(bank);
        }
    }
    void restore_helper_conditioning(Bank& bank,const Settings& value,std::uint64_t version,
            std::stop_token stop,Prepared* wave,const char* reason) {
        if(!bank.helper_conditioning || bank.helper_conditioning->committed)return;
        auto state=std::move(bank.helper_conditioning);
        if(!state->original || !state->fallback || state->fallback->first()!=state->original->samples ||
           state->fallback->end!=bank.samples)
            throw Error("helper fallback lost its original dated acquisition input");
        const auto frontier=bank.samples;
        const auto restored=state->fallback->count;
        Bank original=std::move(*state->original);state->original.reset();
        state->filter=helper::PayloadFilter{};std::vector<float>{}.swap(state->preroll);
        std::vector<std::pair<std::uint64_t,std::uint64_t>>{}.swap(state->initial_epochs);
        bank=std::move(original);
        bank.helper_conditioning_attempted=true;bank.helper_last_frequency_sample=state->reference.frequency_sample?
            state->reference.frequency_sample:state->reference.sample;
        struct ReplayCharge {
            Bank& bank;const Bank::HelperConditioning* state;
            void release()noexcept {
                if(bank.helper_fallback_budget!=state)return;
                bank.working_bytes-=bank.helper_fallback_replay_bytes;
                bank.helper_fallback_replay_bytes=0;bank.helper_fallback_budget=nullptr;
            }
            ~ReplayCharge(){release();}
        } charge{bank,state.get()};
        bank.helper_fallback_budget=state.get();
        bank.helper_fallback_replay_bytes=state->bytes();bank.working_bytes+=bank.helper_fallback_replay_bytes;
        audio::trace::record("receiver_helper_conditioning_fallback",[&](auto& f) {
            f.add("sample",bank.samples);f.add("replay_end",frontier);f.add("replay_samples",restored);
            f.add("reason",reason);f.add("resident",bank.receivers().size());
        });
        // Replay actual captured PCM through the SAME original receivers and
        // normal publication path. No skip, synthetic silence or END is used.
        state->fallback->visit([&](auto raw) {
            if(!stop.stop_requested())feed_samples(bank,raw,value,version,stop,wave);
        });
        charge.release();
        if(!stop.stop_requested() && bank.samples!=frontier)
            throw Error("helper fallback did not restore the complete physical input frontier");
        if(!stop.stop_requested())audio::trace::record("receiver_helper_conditioning_replayed",[&](auto& f) {
            f.add("sample",bank.samples);f.add("replay_samples",restored);f.add("resident",bank.receivers().size());
        });
    }
    void feed_conditioned_samples(Bank& bank,std::span<const float> samples,const Settings& value,
            std::uint64_t version,std::stop_token stop,Prepared* wave,
            const std::optional<helper::Reference>& reference) {
        auto& state=*bank.helper_conditioning;
        const auto rate=value.transfer.modem.sample_rate;
        bool fresh=false,inconsistent=false;
        if(reference) {
            const auto dated=reference->frequency_sample?reference->frequency_sample:reference->sample;
            fresh=reference->frequency_valid && reference->sample_rate==rate && reference->sample<=bank.samples &&
                dated<=reference->sample && bank.samples-dated<=rate &&
                std::isfinite(reference->frequency_hz) && std::isfinite(reference->frequency_uncertainty_hz) &&
                reference->frequency_uncertainty_hz>=0;
            if(fresh) {
                inconsistent=std::abs(reference->frequency_hz-state.admitted_frequency)>
                    state.admitted_radius+reference->frequency_uncertainty_hz;
                if(!inconsistent)state.reference=measurement_only(*reference);
                bank.helper_last_frequency_sample=dated;
            }
        }
        const auto dated=state.reference.frequency_sample?state.reference.frequency_sample:state.reference.sample;
        const auto lost=dated>bank.samples || bank.samples-dated>rate || inconsistent;
        bank.helper_gap_protection_qualified=value.helper.power_w>0 && fresh && !inconsistent;
        if(lost && !state.committed) {
            restore_helper_conditioning(bank,value,version,stop,wave,inconsistent?"reference_inconsistent":"reference_lost");
            if(!stop.stop_requested())feed_samples(bank,samples,value,version,stop,wave);
            return;
        }
        if(lost)state.broad_future=true; // Existing accepted payload clocks remain intact.
        if(!state.committed && (state.fallback->end!=bank.samples ||
           samples.size()>state.fallback->pcm.size()-state.fallback->count)) {
            restore_helper_conditioning(bank,value,version,stop,wave,"history_capacity");
            if(!stop.stop_requested())feed_samples(bank,samples,value,version,stop,wave);
            return;
        }
        if(!state.preroll.empty()) {
            feed_bank(bank,value,version,stop,{},[&](auto& row,auto*) {
                auto& receiver=*row.modem;
                for(std::size_t offset=0;offset<state.preroll.size() && !stop.stop_requested();) {
                    const auto count=std::min<std::size_t>(4096,state.preroll.size()-offset);
                    receiver.push(std::span(state.preroll).subspan(offset,count),stop);offset+=count;
                }
            },wave,false);
            const auto before=state.bytes();std::vector<float>{}.swap(state.preroll);
            bank.working_bytes-=before-state.bytes();
        }
        if(stop.stop_requested())return;
        if(!state.committed)state.fallback->append(samples,bank.samples+samples.size());
        std::array<float,plot_size> filtered{};
        const auto count=state.filter.push(samples,filtered);
        const auto payload=std::span(filtered).first(count);
        // Bank coordinates follow the physical capture. Each ordinary modem
        // receives only fully observed FIR centers, at its dated local origin.
        feed_bank(bank,value,version,stop,samples,[&](auto& row,auto*) {
            row.modem->push(payload,stop);
        },wave,true);
        const auto rotation=-2*std::numbers::pi*value.transfer.modem.carrier_hz/rate;
        bank.mixer*=std::polar(1.,std::remainder(rotation*samples.size(),2*std::numbers::pi));
        bank.mixer/=std::abs(bank.mixer);
        if(!state.committed && !stop.stop_requested()) {
            const auto complete=std::all_of(state.initial_epochs.begin(),state.initial_epochs.end(),[&](const auto& identity) {
                const auto found=std::find_if(bank.receivers().begin(),bank.receivers().end(),[&](const auto& receiver) {
                    return receiver.epoch==identity.first && receiver.family==identity.second;
                });
                return found==bank.receivers().end() || found->modem->initial_search_complete();
            });
            if(complete || bank.samples>=state.attempt_end || bank.coverage_limited)
                restore_helper_conditioning(bank,value,version,stop,wave,complete?"initial_search_unsuccessful":
                    bank.coverage_limited?"conditional_coverage_unavailable":"attempt_window");
        }
    }
    static void close_helper_cohort_phase(Bank::HelperCohort& cohort,const char* reason)noexcept {
        // Closing fresh qualification never changes the curve already pinned
        // by an accepted ordinary track. Its final affine convention survives.
        if(cohort.phase)cohort.phase->curve.close();cohort.phase_reason=reason;
    }
    void update_helper_cohort_phase(Bank& bank,const Settings& value) {
        auto& cohort=*bank.helper_cohort;
        const auto decline=[&](const char* reason){close_helper_cohort_phase(cohort,reason);};
        if(!cohort.qualified || !cohort.reference) {
            decline("helper phase reference unavailable; established phase convention retained");return;
        }
        const auto& reference=*cohort.reference;
        const auto rate=value.transfer.modem.sample_rate;
        constexpr double tau=2*std::numbers::pi;
        if(!reference.phase_valid || !reference.timing_valid ||
           !std::isfinite(reference.phase_radians) || !std::isfinite(reference.phase_uncertainty_radians) ||
           reference.phase_uncertainty_radians<0 || reference.phase_uncertainty_radians>=.25 ||
           !std::isfinite(reference.dwell_start_seconds) || !std::isfinite(reference.dwell_uncertainty_seconds) ||
           reference.dwell_uncertainty_seconds<0 || reference.dwell_uncertainty_seconds>.005) {
            decline("helper phase or punctuation timing is not independently qualified");return;
        }
        if(!cohort.phase) {
            constexpr auto allowance=modem::SampledSimulationChannel::helper_cohort_workspace_bound;
            const auto actual=cohort.bytes();
            if(actual>allowance || sizeof(Bank::HelperCohort::PhaseControl)>allowance-actual) {
                decline("reserved helper storage supports frequency assistance but not the optional phase model");return;
            }
            try {cohort.phase=std::make_unique<Bank::HelperCohort::PhaseControl>();}
            catch(const std::bad_alloc&) {
                decline("optional helper phase storage unavailable; frequency assistance retained");return;
            }
            audio::trace::record("receiver_helper_cohort_phase_storage",[&](auto& f) {
                f.add("sample",bank.samples);f.add("actual_bytes",cohort.bytes());
                f.add("cohort_control_bytes",sizeof(Bank::HelperCohort));
                f.add("phase_control_bytes",sizeof(Bank::HelperCohort::PhaseControl));
                f.add("reserved_bytes",allowance);
            });
        }
        auto& phase=*cohort.phase;
        std::size_t maximum_bin=modem::pattern_chip_samples(value.transfer.modem);
        for(const auto& receiver:bank.receivers())if(helper_cohort_receiver(bank,receiver))
            maximum_bin=std::max(maximum_bin,receiver.modem->acquisition_state().bin_samples);
        const auto guard=cohort.filter.delay();
        if(maximum_bin>UINT64_MAX-guard-1 || bank.samples>UINT64_MAX-guard-maximum_bin-1 ||
           reference.sample>UINT64_MAX-rate) {
            decline("helper phase model exceeds its dated sample range");return;
        }
        const auto first=bank.samples+guard+maximum_bin+1;
        const auto expiry=reference.sample+rate;
        auto& curve=phase.curve;
        if(curve.available_segments()==24) {
            if(!curve.view_id() && !curve.configure(1,rate,reference.frequency_hz-cohort.expected_hz,guard,maximum_bin)) {
                decline("ordinary receiver cannot represent the bounded helper phase model");return;
            }
            const auto forecast=reference.phase_uncertainty_radians+
                tau*reference.frequency_uncertainty_hz*static_cast<double>(first-reference.sample)/rate;
            const modem::PatternPhaseSegment initial{first,0,
                tau*(reference.frequency_hz-cohort.expected_hz-curve.baseline())/rate};
            if(first>=expiry || forecast>.25 || !curve.append(std::span(&initial,1),reference.sample,expiry)) {
                decline("helper phase precision does not cover the first future PCM bin");return;
            }
            // The unknown Tx/propagation phase is a fitted constant. Freeze
            // this anchor and residual CFO B independently of grid origin O.
            phase.anchor=std::remainder(reference.phase_radians+tau*reference.frequency_hz*
                static_cast<double>(first-reference.sample)/rate,tau);
            cohort.phase_reason="helper phase and punctuation timing qualified";return;
        }
        if(curve.sample_rate()!=rate || maximum_bin>curve.maximum_bin_samples()) {
            decline("new ordinary profile exceeds the frozen helper phase-bin guard");return;
        }
        if(reference.sample<=curve.latest_evidence())return;
        const auto helper_baseline=cohort.expected_hz+curve.baseline();
        const auto predicted=[&](long double at) {
            return static_cast<long double>(phase.anchor)+tau*helper_baseline*
                (at-curve.first_sample())/rate+curve.value_at(at);
        };
        const auto phase_error=std::remainder(reference.phase_radians-
            static_cast<double>(std::remainder(predicted(reference.sample),static_cast<long double>(tau))),tau);
        if(std::abs(phase_error)>.25) {
            decline("helper canonical phase changed beyond the coherent handoff limit; established affine convention retained");return;
        }
        const auto future_error=phase_error+tau*(reference.frequency_hz-helper_baseline)*
            static_cast<double>(expiry-reference.sample)/rate-
            (curve.value_at(expiry)-curve.value_at(reference.sample));
        const auto forecast=reference.phase_uncertainty_radians+tau*reference.frequency_uncertainty_hz;
        if(std::abs(phase_error)<=.02 && std::abs(future_error)+forecast<=.125 &&
           curve.requalify(reference.sample,expiry)) {
            cohort.phase_reason="helper phase and punctuation timing qualified";return;
        }
        // Correct a measured phase residual with a finite continuous bridge,
        // then return to the measured stable slope. Append both atomically so
        // reaching the fixed capacity can never leave a transient slew forever.
        const auto bridge=std::max<std::uint64_t>(rate/5,2*maximum_bin);
        if(first>UINT64_MAX-bridge || first+bridge>=expiry || curve.available_segments()<2) {
            decline(curve.available_segments()<2?
                "helper phase curve is full; established affine convention retained":
                "helper phase precision cannot cover a continuous future handoff");return;
        }
        const auto end=first+bridge;
        const auto beginning=curve.value_at(first);
        const auto target=std::remainder(reference.phase_radians+tau*reference.frequency_hz*
            static_cast<double>(first-reference.sample)/rate-
            static_cast<double>(std::remainder(predicted(first),static_cast<long double>(tau))),tau);
        if(std::abs(target)>.25) {
            decline("helper future phase residual exceeds the coherent handoff limit; established affine convention retained");return;
        }
        const auto slope=tau*(reference.frequency_hz-helper_baseline)/rate;
        const auto ending=beginning+target+slope*bridge;
        const auto end_uncertainty=reference.phase_uncertainty_radians+tau*reference.frequency_uncertainty_hz*
            static_cast<double>(end-reference.sample)/rate;
        const std::array<modem::PatternPhaseSegment,2> segments{{
            {first,beginning,(ending-beginning)/bridge},{end,ending,slope}}};
        if(end_uncertainty>.25 || !curve.append(segments,reference.sample,expiry)) {
            decline("helper phase handoff cannot preserve the immutable future-bin convention");return;
        }
        cohort.phase_reason="helper phase and punctuation timing qualified";
    }
    void update_helper_cohort_reference(Bank& bank,const Settings& value,
                                       const std::optional<helper::Reference>& reference) {
        if(!bank.helper_cohort)return;
        auto& cohort=*bank.helper_cohort;cohort.qualified=false;
        const auto rate=value.transfer.modem.sample_rate;
        const auto dated=reference?(reference->frequency_sample?reference->frequency_sample:reference->sample):0;
        if(!reference || !reference->frequency_valid || reference->sample_rate!=rate ||
           reference->sample>bank.samples || dated>reference->sample || bank.samples-dated>rate ||
           !std::isfinite(reference->frequency_hz) || !std::isfinite(reference->frequency_uncertainty_hz) ||
           reference->frequency_uncertainty_hz<0) {
            cohort.reason="helper frequency reference unavailable or stale; broad acquisition retained";
            cohort.active=false;cohort.reference.reset();cohort.lease_until=cohort.probation_until=0;return;
        }
        if(std::abs(reference->frequency_hz-cohort.expected_hz)>100) {
            cohort.reason="helper frequency lies outside the supported tracker range; broad acquisition retained";
            cohort.active=false;cohort.reference.reset();cohort.lease_until=cohort.probation_until=0;return;
        }
        if(bank.samples<cohort.quarantine_until) {
            cohort.active=false;cohort.reason="helper reference changed inconsistently; broad reacquisition retained";return;
        }
        if(cohort.last_frequency_valid &&
           std::abs(reference->frequency_hz-cohort.last_frequency_hz)>
                reference->frequency_uncertainty_hz+cohort.last_radius_hz+
                .25*rate/static_cast<double>(modem::symbol_sample_count(value.transfer.modem))) {
            cohort.active=false;cohort.reference.reset();cohort.lease_until=cohort.probation_until=0;
            cohort.quarantine_until=bank.samples+rate;cohort.lease_compatible=false;
            cohort.last_frequency_hz=reference->frequency_hz;cohort.last_radius_hz=reference->frequency_uncertainty_hz;
            cohort.reason="helper reference changed inconsistently; broad reacquisition retained";return;
        }
        cohort.last_frequency_valid=true;cohort.last_frequency_hz=reference->frequency_hz;
        cohort.last_radius_hz=reference->frequency_uncertainty_hz;
        cohort.reference=measurement_only(*reference);cohort.qualified=true;
        if(!cohort.probation_until) {
            const auto interval=std::max<std::uint64_t>(6ULL*rate,2*modem::symbol_sample_count(value.transfer.modem));
            std::uint64_t aperture=0;
            for(const auto& receiver:bank.receivers())if(helper_cohort_eligible(bank,receiver))
                aperture=std::max(aperture,receiver.modem->initial_fft_input_samples());
            cohort.probation_until=bank.samples+aperture+interval;
        }
        cohort.reason="measured helper available; waiting for an ordinary acquisition batch";
    }
    bool set_helper_cohort_prior(Bank& bank,Receiver& receiver,detail::HelperPriorFacts* facts=nullptr) {
        auto& cohort=*bank.helper_cohort;
        if(!cohort.qualified || !cohort.reference)return false;
        receiver.modem->set_acquisition_phase_view(cohort.phase && cohort.phase->curve.view_id()?&cohort.phase->curve:nullptr);
        const auto state=receiver.modem->acquisition_state();
        const auto pairs=receiver.modem->acquisition_hypotheses();
        const auto& config=receiver.options.modem;
        const auto offset=receiver.modem->acquisition_phase_view_active()?cohort.phase->curve.baseline():
            cohort.reference->frequency_hz-cohort.expected_hz;
        // Both values come from the SAME original oscillator builder. An
        // uncertainty inflation derived from LF endpoints is not proof that
        // each timing cell actually supports that radius. Known zero is valid.
        if(!state.admitted_clock_cell_half_width_ppm || !state.admitted_clock_half_width_ppm) {
            cohort.reason="ordinary physical clock domain or timing-cell coverage is unknown; broad acquisition retained";
            if(facts)facts->reason=cohort.reason;
            return false;
        }
        auto decision=detail::helper_prior_mask(pairs,offset,cohort.reference->frequency_uncertainty_hz,
            config.carrier_hz-cohort.expected_hz,.125*config.sample_rate/
                static_cast<double>(modem::symbol_sample_count(config)),
            *state.admitted_clock_cell_half_width_ppm,*state.admitted_clock_half_width_ppm);
        if(facts)*facts=decision;
        cohort.reason=decision.reason;
        if(!decision.admitted)return false;
        if(!receiver.modem->set_acquisition_prior(decision.origin_hz,std::span(decision.cells).first(pairs.size()))) {
            cohort.reason="ordinary receiver cannot represent the measured helper prior; broad acquisition retained";
            if(facts){facts->admitted=false;facts->reason=cohort.reason;}
            return false;
        }
        cohort.original_pairs=std::max<std::uint64_t>(cohort.original_pairs,pairs.size());
        cohort.selected_pairs=std::max<std::uint64_t>(cohort.selected_pairs,decision.selected);
        return true;
    }
    void feed_samples(Bank& bank,std::span<const float> samples,const Settings& value,std::uint64_t version,
                      std::stop_token stop,Prepared* wave=nullptr,std::stop_token receive_stop={},
                      std::optional<helper::Reference> assistance={}) {
        if(receive_stop.stop_possible()) {
            std::stop_source combined;
            std::stop_callback primary(stop,[&]{combined.request_stop();});
            std::stop_callback secondary(receive_stop,[&]{combined.request_stop();});
            feed_samples(bank,samples,value,version,combined.get_token(),wave,{},std::move(assistance));
            return;
        }
        std::array<std::complex<double>,plot_size> projected{};
        const auto rotation=std::polar(1.,-2*std::numbers::pi*value.transfer.modem.carrier_hz/value.transfer.modem.sample_rate);
        for(std::size_t offset=0;offset<samples.size();) {
            refresh_bank(bank,value);
            bank.helper_gap_protection_qualified=helper_tracking_reference_qualified(bank,value,assistance);
            update_helper_cohort_reference(bank,value,assistance);
            if(bank.helper_cohort) {
                try {update_helper_cohort_phase(bank,value);}
                catch(const Error&) {close_helper_cohort_phase(*bank.helper_cohort,
                    "optional helper phase model unavailable; ordinary acquisition retained");}
            }
            auto count=std::min(projected.size(),samples.size()-offset);
            if(bank.helper_cohort) {
                auto& cohort=*bank.helper_cohort;
                bool batch_ready=false;
                for(auto& row:bank.receivers()) {
                    if(!helper_cohort_eligible(bank,row))continue;
                    const auto installed=row.modem->acquisition_state();
                    const auto clear_prior=[&] {
                        const auto before=receiver_workspace(row);row.modem->clear_acquisition_prior();
                        bank.working_bytes=bank.working_bytes-before+receiver_workspace(row);
                    };
                    if(cohort.qualified && !installed.prior_pending && (!installed.prior_active || installed.ready)) {
                        // Keep the mask through observation. Its atomic closed-
                        // batch refresh preserves compatible ordinary bit support.
                        const auto before=receiver_workspace(row);
                        const auto other=bank.working_bytes-before,overhead=before-row.modem->working_bytes();
                        const auto capacity=bank_capacity(value);
                        if(other<=capacity && overhead<capacity-other &&
                           sizeof(modem::StreamingReceiver)<capacity-other-overhead)
                            row.modem->set_workspace_bytes(capacity-other-overhead-sizeof(modem::StreamingReceiver));
                        bool applied=false;
                        try {applied=set_helper_cohort_prior(bank,row);}
                        catch(const Error&) {cohort.reason="helper prior exceeds optional numeric workspace; broad acquisition retained";}
                        catch(const std::bad_alloc&) {cohort.reason="helper prior workspace unavailable; broad acquisition retained";}
                        if(!applied)row.modem->clear_acquisition_prior();
                        bank.working_bytes=bank.working_bytes-before+receiver_workspace(row);
                        if(bank.working_bytes>capacity)throw Error("ordinary helper prior exceeds receiver workspace");
                    } else if(!cohort.qualified)clear_prior();
                    const auto state=row.modem->acquisition_state();
                    batch_ready|=state.supported && state.ready;
                    if(state.supported && !state.ready)count=std::min(count,state.input_room_samples);
                }
                if(batch_ready) {
                    feed_bank(bank,value,version,stop,{},[](auto&,auto*){},wave,false);
                    if(stop.stop_requested())return;
                    continue;
                }
                if(!count)throw Error("ordinary helper observation fence did not advance");
            }
            const auto conditioned_feed=[&] {
                const auto first=bank.samples;
                try {feed_conditioned_samples(bank,samples.subspan(offset,count),value,version,stop,wave,assistance);}
                catch(const std::exception&) {
                    if(!bank.helper_conditioning || bank.helper_conditioning->committed)throw;
                    const auto saved=bank.helper_conditioning->fallback->end;
                    const auto current_saved=saved>=first+count;
                    // Numeric failure may precede the shared feed commit. The
                    // retained ring, not that incomplete numeric frontier,
                    // determines which physical samples must be replayed.
                    bank.samples=saved;
                    restore_helper_conditioning(bank,value,version,stop,wave,"conditional_error");
                    if(!current_saved && !stop.stop_requested())
                        feed_samples(bank,samples.subspan(offset,count),value,version,stop,wave);
                }
            };
            if(assistance && assistance->frequency_valid) {
                const auto dated=assistance->frequency_sample?assistance->frequency_sample:assistance->sample;
                if(dated<=bank.samples)bank.helper_last_frequency_sample=dated;
            } else if(bank.helper_conditioning_attempted && !bank.helper_conditioning && !bank.helper_fallback_replay_bytes &&
                      bank.samples>=bank.helper_last_frequency_sample &&
                      bank.samples-bank.helper_last_frequency_sample>6ULL*value.transfer.modem.sample_rate) {
                bank.helper_conditioning_attempted=false;bank.helper_conditioning_probe=bank.samples;
            }
            if(bank.helper_conditioning) {
                conditioned_feed();
                offset+=count;continue;
            }
            if(assistance && begin_helper_conditioning(bank,value,*assistance)) {
                conditioned_feed();
                offset+=count;continue;
            }
            for(std::size_t i=0;i<count;++i) {
                projected[i]=static_cast<double>(samples[offset+i])*bank.mixer;bank.mixer*=rotation;
            }
            bank.mixer/=std::abs(bank.mixer);
            const auto raw=samples.subspan(offset,count);const auto mixed=std::span(projected).first(count);
            std::array<float,plot_size> cohort_filtered{};
            const auto cohort_count=bank.helper_cohort?bank.helper_cohort->filter.push(raw,cohort_filtered):0;
            const auto cohort_pcm=std::span(cohort_filtered).first(cohort_count);
            if(!bank.helper_cohort)prepare_helper_history(bank,value,count);
            if(assistance && assistance->sample<=bank.samples)
                prepare_helper_assistance(bank,value,*assistance,count,version);
            else if(bank.helper_assistance) {
                const auto& reference=bank.helper_assistance->reference;
                const auto frequency_sample=reference.frequency_sample?reference.frequency_sample:reference.sample;
                if(frequency_sample>bank.samples || bank.samples-frequency_sample>value.transfer.modem.sample_rate)
                    if(!retain_helper_absence_tail(bank))drop_helper_assistance(bank,"reference_expired");
            }
            if(bank.helper_assistance && !helper_surplus_available(bank,bank_capacity(value),count))
                drop_helper_assistance(bank,"ordinary_growth_priority");
            const auto feed_origin=bank.samples;
            feed_bank(bank,value,version,stop,raw,[&](auto& row,auto* cache){
                auto& receiver=*row.modem;
                if(helper_cohort_receiver(bank,row)) {
                    const auto before=receiver.fft_work();
                    const auto observed=[&] {
                        const auto state=receiver.acquisition_state();
                        if(helper_cohort_eligible(bank,row) && state.supported) {
                            const auto consumed=receiver.observe_acquisition(cohort_pcm,stop);
                            if(!stop.stop_requested() && consumed!=cohort_pcm.size())
                                throw Error("ordinary helper cohort crossed an unprocessed acquisition fence");
                        } else receiver.push(cohort_pcm,stop);
                    };
                    try {observed();}
                    catch(...) {record_helper_cohort_phase_work(bank,row,before,feed_origin+raw.size());throw;}
                    record_helper_cohort_phase_work(bank,row,before,feed_origin+raw.size());
                    return;
                }
                if(receiver.try_skip_unobserved(raw.size(),stop))return;
                if(bank.helper_assistance && bank.helper_assistance->receiver->modem.get()==&receiver) {
                    auto& state=*bank.helper_assistance;
                    if(state.preroll_count) {
                        if(!bank.helper_history || bank.helper_history->count!=state.preroll_count ||
                           bank.helper_history->first()!=state.filter_input_origin || bank.helper_history->end!=bank.samples)
                            throw Error("helper pre-roll lost its dated PCM");
                        // Drain accepted bursts through the same feed_bank
                        // publication below. Past PCM uses no phase correction
                        // from a later observation, and never advances ingress.
                        const auto initial_overhead=receiver_workspace(*state.receiver)-receiver.working_bytes();
                        bank.helper_history->visit([&](auto history){
                            std::array<float,4096> filtered{};
                            const auto count=state.filter.push(history,filtered);
                            receiver.push(std::span(filtered).first(count),stop);
                            auto drained=receiver.take_pattern_bursts();
                            if(!state.payload_retained && std::any_of(drained.begin(),drained.end(),[](const auto& b){return !b.bits.empty();})) {
                                state.payload_retained=true;
                                if(state.tail_only && receiver.suspend_helper_acquisition())state.acquisition_frozen=true;
                                audio::trace::record("receiver_helper_payload_retained",[&](auto& f){
                                    f.add("sample",bank.samples);f.add("epoch",state.receiver->epoch);f.add("preroll",true);
                                });
                            }
                            auto& queued=state.receiver->bursts;
                            if(queued.empty())queued=std::move(drained);
                            else queued.insert(queued.end(),std::make_move_iterator(drained.begin()),std::make_move_iterator(drained.end()));
                            if(state.payload_retained) {
                                const auto overhead=receiver_workspace(*state.receiver)-receiver.working_bytes();
                                const auto growth=overhead>initial_overhead?overhead-initial_overhead:0;
                                if(growth>=state.ordinary_numeric_loan)throw Error("accepted helper pre-roll exceeds ordinary workspace");
                                receiver.set_workspace_bytes(state.ordinary_numeric_loan-growth);
                            }
                        });
                        state.preroll_count=0;
                    }
                    std::array<float,plot_size> filtered{};
                    const auto filtered_count=state.filter.push(raw,filtered);
                    if(!filtered_count)return;
                    const auto payload=std::span(filtered).first(filtered_count);
                    const auto scored_frontier=state.receiver->sample_origin+state.filter.output_samples()-payload.size();
                    const auto bin=receiver.phase_assistance_bin_samples();
                    if(bin && state.tail_only) {
                        const auto local=scored_frontier-state.receiver->sample_origin;
                        const auto begin=local>bin?local-bin:0,end=local+payload.size()+bin;
                        state.phase_knots={modem::PatternPhaseKnot{begin,state.phase_continuation.at(begin,value.transfer.modem.sample_rate)},
                            modem::PatternPhaseKnot{end,state.phase_continuation.at(end,value.transfer.modem.sample_rate)}};
                        if(receiver.push_phase_assisted(payload,state.phase_knots,end-begin,stop))return;
                    }
                    const auto forecast_seconds=static_cast<double>(static_cast<long double>(scored_frontier)+payload.size()+bin-state.reference.sample)/
                        value.transfer.modem.sample_rate;
                    const auto forecast_uncertainty=state.reference.phase_uncertainty_radians+
                        2*std::numbers::pi*state.reference.frequency_uncertainty_hz*forecast_seconds;
                    if(bin && state.phase_qualified && (!std::isfinite(forecast_uncertainty) || forecast_uncertainty>.25)) {
                        if(retain_helper_absence_tail(bank)) {
                            const auto local=scored_frontier-state.receiver->sample_origin;
                            const auto begin=local>bin?local-bin:0,end=local+payload.size()+bin;
                            state.phase_knots={modem::PatternPhaseKnot{begin,state.phase_continuation.at(begin,value.transfer.modem.sample_rate)},
                                modem::PatternPhaseKnot{end,state.phase_continuation.at(end,value.transfer.modem.sample_rate)}};
                            if(receiver.push_phase_assisted(payload,state.phase_knots,end-begin,stop))return;
                        }
                        state.failed=true;return;
                    }
                    if(bin && state.phase_ready && state.reference.phase_valid && state.reference.timing_valid &&
                       forecast_uncertainty<=.25 &&
                       state.reference.sample<=scored_frontier-std::min<std::uint64_t>(scored_frontier,bin)) {
                        const auto origin=state.receiver->sample_origin;
                        const auto local=scored_frontier-origin;
                        const auto begin=local>bin?local-bin:0;
                        const auto end=local+payload.size()+bin;
                        const auto phase=[&](std::uint64_t at) {
                            const auto absolute=static_cast<long double>(origin)+at;
                            const auto from_reference=(absolute-state.reference.sample)/value.transfer.modem.sample_rate;
                            const auto from_anchor=(absolute-state.anchor_sample)/value.transfer.modem.sample_rate;
                            return static_cast<double>(state.unwrapped_phase+2*std::numbers::pi*state.reference.frequency_hz*from_reference-
                                state.anchor_phase-2*std::numbers::pi*state.anchor_frequency*from_anchor);
                        };
                        state.phase_knots={modem::PatternPhaseKnot{begin,phase(begin)},modem::PatternPhaseKnot{end,phase(end)}};
                        if(receiver.push_phase_assisted(payload,state.phase_knots,end-begin,stop)) {
                            state.phase_continuation={local+payload.size(),phase(local+payload.size()),
                                state.reference.frequency_hz-state.anchor_frequency};
                            if(!state.phase_applied) {
                                state.phase_applied=true;
                                audio::trace::record("receiver_helper_phase_applied",[&](auto& f){
                                    f.add("sample",scored_frontier);f.add("reference_sample",state.reference.sample);f.add("epoch",state.receiver->epoch);
                                });
                            }
                            return;
                        }
                    }
                    // Pre-roll and phase assistance use the receiver-local
                    // oscillator. Keep that convention across every fallback;
                    // the bank's projection has a different phase at origin.
                    receiver.push(payload,stop);return;
                }
                receiver.push(raw,mixed,stop,cache);},wave,false);
            if(bank.helper_history && bank.samples==feed_origin+raw.size())bank.helper_history->append(raw,bank.samples);
            offset+=count;
        }
    }
    void complete_tx(Prepared& wave, const std::string& error = {}) {
        std::lock_guard lock(mutex);
        if (wave.generation != generation || wave.serial != tx_serial) return;
        // Keep the existing normal-completion quiet interval. Cancellation
        // retains key exposure independently, without adding a full-symbol
        // backend delay to the GUI's existing cancellation separation.
        if(wave.hardware && !wave.noise && !wave.graceful && !finish_requested && wave.transmitter && wave.transmitter->samples_emitted())
            next_hardware_send=std::max(next_hardware_send,Clock::now()+std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(static_cast<double>(modem::pattern_absence_capture_samples(wave.modem))/wave.modem.sample_rate+1.)));
        finish_simulation_elapsed();
        if (wave.transmitter) current.transmit_trace = wave.transmitter->transmit_trace();
        tx_busy = false; current.transmitting = settings.simulation ? false : !queued.empty();
        current.transmitting_noise = false;
        current.transmission_finished = settings.simulation || queued.empty();
        wave.graceful=wave.graceful || finish_requested;
        current.transmission_cancelled = wave.stop.stop_requested() || wave.graceful;
        current.transmission_stopping=false;source_pcm_started=false;finish_requested=false;
        current.status = idle_status();
        if (!error.empty()) { transmit_error=error; current.error = error; clear_replay(); }
        else if(wave.stop.stop_requested() || wave.graceful)clear_replay();
        else if (!wave.stop.stop_requested() && !wave.graceful) {
            current.transmission_fraction = 1;
            if (settings.simulation && !wave.replay.empty()) {
                if(wave.receive_clear_generation!=receive_clear_generation) {
                    wave.receives->clear();
                } else if(wave.recovery_clear_generation!=recovery_clear_generation)
                    discard_staged_recoveries();
                replay = std::move(wave.replay); delivered_replay_frame.reset(); first_visible_replay_frame = 0;
                replay_bin_hz = static_cast<double>(wave.modem.sample_rate) / (plot_size / 4);
                replay_started = replay_clock(); current.simulation_replay = true;
                detail::rebase_pattern_score_observations(replay, replay_started, replay_duration);
                current.transmit_trace = replay.front().transmit_trace;
                current.replay_frame_count = replay.size();
                current.status = wave.binary ? "Simulation; three seconds of raw binary signal" :
                    "Simulation; three seconds of sampled signal and reception";
                ++current.sequence;
            }
        }
        changed.notify_all();
    }
    void progress(const Prepared& wave, const Settings& value) {
        std::lock_guard lock(mutex);
        if (generation != wave.generation || tx_serial != wave.serial) return;
        current.transmit_trace = wave.transmitter->transmit_trace();
        current.transmission_seconds = static_cast<double>(value.simulation ? wave.transmitted_samples : wave.transmitter->samples_emitted()) / wave.modem.sample_rate;
        current.transmission_fraction = wave.noise || !wave.transmitter->total_samples() ? 0 : static_cast<double>(value.simulation ? wave.transmitted_samples : wave.transmitter->samples_emitted()) /
                                        static_cast<double>(wave.transmitter->total_samples());
        current.simulation_receiving_tail=value.simulation && !wave.noise && wave.tail_started;
    }
    void prepare_source_read(Prepared& wave) {
        std::lock_guard lock(mutex);
        if(generation!=wave.generation || tx_serial!=wave.serial || wave.stop.stop_requested())
            throw Error("transmission cancelled");
        source_pcm_started=true;
        if(finish_requested && !wave.graceful) {
            wave.transmitter->request_finish();wave.graceful=true;
        }
    }
    void retain_emitted_epoch(const Prepared& wave) {
        if(!wave.protected_epoch || !wave.transmitter->samples_emitted())return;
        const auto scheduled=wave.modem.symbol_burst.enabled() ||
            (wave.modem.scheduled_hopping && wave.modem.scheduled_hopping->enabled());
        const auto epoch=scheduled?wave.transmitter->emitted_payload_epoch():
            datapump::detail::exposed_transmit_epoch(wave.modem,*wave.protected_epoch,
            wave.transmitter->original_total_samples(),wave.transmitter->samples_emitted());
        std::lock_guard lock(mutex);
        auto& retained=transmit_epochs[wave.protected_key];
        retained=std::max(retained,epoch);
    }
    void source_loop(std::stop_token stop) {
        DATAPUMP_MUTEX_CONTEXT(mutex_context,source);
        std::uint64_t local_generation = 0;
        std::optional<Bank> simulation_bank;
        const auto reset_simulation_bank=[&](const char* reason) {
            if(simulation_bank){trace_receiver_acquisition_work(*simulation_bank,true,reason);trace_exact_source_pool(*simulation_bank,reason,true);trace_point_pool(*simulation_bank,reason,true);}
            simulation_bank.reset();
        };
        struct FinalSimulationSourceSnapshot {
            std::optional<Bank>& bank;
            ~FinalSimulationSourceSnapshot(){try{if(bank){trace_receiver_acquisition_work(*bank,true,"simulation_join");trace_exact_source_pool(*bank,"simulation_join",true);trace_point_pool(*bank,"simulation_join",true);}}catch(...) {}}
        } final_simulation_source_snapshot{simulation_bank};
        std::unique_ptr<modem::SampledSimulationChannel> simulation_channel;
        std::uint64_t simulation_bank_source_origin=0;
        std::optional<double> simulation_origin;
        std::shared_ptr<Prepared> wave;
        detail::SignalWindow plot_window;
        unsigned idle_fraction = 0;
        auto last_plot = Clock::time_point{};
        while (!stop.stop_requested()) {
            execution::checkpoint();
            Settings value; std::uint64_t version,input_revision,input_clear_generation; std::stop_token processing_token;
            bool reset_simulation_helper=false;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, stop, [this] { return current.running && !capture_suspended && decoder_generation == generation &&
                    (settings.simulation || (ready && (settings.full_duplex || !capture_active))); });
                if (stop.stop_requested()) break;
                advance_replay(replay_clock());
                value = settings; version = generation;input_revision=receive_revision;
                input_clear_generation=receive_clear_generation;processing_token = decode_stop.get_token();
                if(simulation_bank && simulation_bank->revision!=receive_revision)reset_simulation_bank("simulation_revision");
                if(wave && wave->receive_clear_generation!=receive_clear_generation) {
                    if(wave->receives)wave->receives->clear();
                    wave->receive_clear_generation=receive_clear_generation;
                }
                if (local_generation != version) {
                    local_generation = version; wave.reset(); reset_simulation_bank("simulation_generation"); simulation_channel.reset();
                    simulation_origin.reset();simulation_bank_source_origin=0;
                    reset_simulation_helper=value.simulation;
                    last_plot = {};
                    plot_window = detail::SignalWindow(value.transfer.modem);
                    idle_fraction = 0;
                }
                if (wave && wave->stop.stop_requested()) {
                    wave.reset();
                }
                if (!wave && ready) {
                    wave = std::move(ready); pending_points = {};
                    current.transmit_clock_following=false;
                    if (!value.simulation) plot_window.reset();
                    current.status = wave->noise ? (value.simulation ? "Simulating continuous noise; stop when finished" :
                        "Transmitting continuous noise; stop when finished") :
                        value.simulation ? "Transmitting sampled audio to an independent receiver" :
                        value.full_duplex ? "Transmitting; duplex audio receiver active" : "Transmitting; audio input paused";
                }
                source_active=true;
            }
            if(reset_simulation_helper)reset_helper(version);
            const auto release_activity=[this,local_generation](Impl*) {
                std::lock_guard lock(mutex);source_active=false;
                if(generation==local_generation) {playback_audio_bytes=0;audio_bytes=capture_audio_bytes;}
                changed.notify_all();
            };
            const std::unique_ptr<Impl,decltype(release_activity)> activity(this,release_activity);
            const bool media_clock=simulation_media_clock(value);
            bool preparing_media_transmitter=false;
            std::optional<detail::SimulationInputTransaction> simulation_input;
            const auto begin_simulation_input=[&] {
                if(!media_clock)return;
                simulation_input=detail::SimulationInputTransaction{simulation_channel->received_samples(),
                    simulation_bank->samples,simulation_bank->revision};
                detail::simulation_checkpoint(detail::SimulationPhase::before_read,simulation_input->source,
                    simulation_input->bank,simulation_input->revision);
            };
            const auto before_simulation_feed=[&](std::size_t count) {
                if(!simulation_input)return;
                simulation_input->expected=count;simulation_input->feed_attempted=true;
                detail::simulation_checkpoint(detail::SimulationPhase::before_feed,simulation_channel->received_samples(),
                    simulation_bank->samples,simulation_bank->revision);
            };
            const auto finish_simulation_input=[&] {
                if(!simulation_input)return false;
                const auto input=*simulation_input;simulation_input.reset();
                if(!input.interrupted(simulation_channel->received_samples(),simulation_bank->samples))return false;
                bool invalidated=false;
                {
                    std::lock_guard lock(mutex);
                    if(current.running && generation==version && receive_revision==input.revision) {
                        current.receiver_health.input_interrupted=true;
                        // The exact per-lane discarded extent is unknown. Do
                        // not turn it into fabricated dropped PCM or absence.
                        reset_input_locked();invalidated=true;
                    }
                }
                if(invalidated) {
                    reset_simulation_bank("simulation_input_interrupted");
                    detail::simulation_checkpoint(detail::SimulationPhase::input_invalidated,
                        simulation_channel->received_samples(),input.bank,input.revision);
                }
                return invalidated;
            };
            try {
                const auto& transmit_modem = wave ? wave->modem : value.transfer.modem;
                if (value.simulation) {
                    const auto helper_options=simulation_channel?helper::Options{}:
                        sampled_helper(value,value.transfer.modem.sample_rate,version);
                    const auto make_simulation_channel=[&] {
                        std::unique_ptr<modem::SampledSimulationChannel> channel;
                        try {
                            channel=std::make_unique<modem::SampledSimulationChannel>(
                                value.transfer.modem,channel_config(value),helper_options,detail::helper_occupied_bandwidth(value));
                        }catch(const Error& error) {
                            if(helper_options.power_w<=0 || (wave && wave->helper_only))throw;
                            helper_unavailable(version,std::string(error.what())+"; ordinary sampled Robust channel retained");
                            channel=std::make_unique<modem::SampledSimulationChannel>(
                                value.transfer.modem,channel_config(value));
                        }catch(const std::bad_alloc&) {
                            if(helper_options.power_w<=0 || (wave && wave->helper_only))throw;
                            helper_unavailable(version,"Helper channel allocation unavailable; ordinary sampled Robust channel retained");
                            channel=std::make_unique<modem::SampledSimulationChannel>(
                                value.transfer.modem,channel_config(value));
                        }
                        channel->set_helper_time_key(value.transfer.key);
                        if(helper_options.power_w>0&&configure_helper(value,version,channel->physical_sample_rate()))
                            channel->set_physical_observer([this,version](std::span<const float> raw,std::uint32_t rate,std::uint64_t first) {
                                observe_helper(raw,rate,first,version);
                            });
                        return channel;
                    };
                    if(media_clock && !simulation_channel) {
                        auto channel=make_simulation_channel();
                        const auto origin=detail::simulation_initial_epoch(current_epoch());
                        simulation_channel=std::move(channel);simulation_origin=origin;
                    }
                    if(!media_clock && !simulation_channel && value.helper.power_w>0 &&
                       value.transfer.timestamp && !transfer::shared_hop_calendar(value.transfer.modem)) {
                        simulation_channel=make_simulation_channel();
                        if(simulation_channel->physical_sample_rate()!=value.transfer.modem.sample_rate) {
                            // A fixed diagnostic's timestamp dates source zero.
                            // Its real helper pre-roll is captured earlier; date
                            // that capture rather than moving the Robust epoch.
                            const auto& config=wave?wave->modem:value.transfer.modem;
                            const auto anchor=modem::symbol_schedule_detail::add(
                                modem::symbol_schedule_detail::multiply(value.transfer.timestamp,config.sample_rate),
                                config.stream_phase_samples);
                            const auto prefix=modem::symbol_schedule_detail::add(
                                modem::training_sample_count(config),modem::pattern_pulse_padding_samples(config));
                            if(anchor<prefix)throw Error("diagnostic capture precedes the representable sample clock");
                            const auto lead=detail::helper_leading_seconds(anchor-prefix,config.sample_rate)/
                                (1+channel_config(value).clock_error_ppm*1e-6);
                            simulation_origin=static_cast<double>(static_cast<long double>(value.transfer.timestamp)-lead);
                        }
                    }
                    // The receiving computer runs continuously. TX control can
                    // start a waveform, but cannot reset, seed or align RX.
                    if (!simulation_bank) {
                        Bank initial;
                        if(media_clock) {
                            initial.observed_epoch=detail::simulation_media_epoch(*simulation_origin,
                                simulation_channel->received_samples(),value.transfer.modem.sample_rate);
                            initial.revision=input_revision;
                        } else if(simulation_origin) {
                            initial.observed_epoch=detail::simulation_media_epoch(*simulation_origin,
                                simulation_channel->received_samples(),value.transfer.modem.sample_rate);
                            initial.simulation_capture_dated=true;
                        } else if(value.transfer.modem.symbol_burst.enabled()) {
                            initial.observed_epoch=current_epoch();
                            std::lock_guard lock(mutex);initial.revision=receive_revision;
                        }
                        simulation_bank = make_bank(value,std::move(initial));
                        simulation_bank_source_origin=simulation_channel?
                            simulation_channel->received_samples()-simulation_bank->samples:0;
                        simulation_bank->helper_input_origin_samples=simulation_bank_source_origin;
                        if(media_clock)detail::simulation_checkpoint(detail::SimulationPhase::bank_created,
                            simulation_channel->received_samples(),simulation_bank->samples,simulation_bank->revision,
                            *simulation_bank->observed_epoch);
                    }
                    if(wave && wave->prepare_simulation) {
                        if(!simulation_channel && value.helper.power_w>0)
                            simulation_channel=make_simulation_channel();
                        wave->simulation_helper_enabled=simulation_channel &&
                            simulation_channel->physical_sample_rate()!=value.transfer.modem.sample_rate;
                        if(media_clock) {
                            std::lock_guard lock(mutex);
                            if(generation!=wave->generation || tx_serial!=wave->serial ||
                               wave->stop.stop_requested())
                                throw Error("transmission cancelled");
                            if(processing_token.stop_requested())continue;
                        }
                        preparing_media_transmitter=media_clock;
                        if(media_clock) {
                            // Clear may obsolete this attempt without retiring
                            // TX. Keep a failed factory retryable until success.
                            wave->prepare_simulation(*wave,observation_epoch(*simulation_bank,value,simulation_bank->samples));
                            wave->prepare_simulation={};
                        } else {
                            auto prepare=std::move(wave->prepare_simulation);
                            prepare(*wave,observation_epoch(*simulation_bank,value,simulation_bank->samples));
                        }
                        preparing_media_transmitter=false;
                        if(media_clock && processing_token.stop_requested())continue;
                    }
                    if (!simulation_channel) simulation_channel=make_simulation_channel();
                    const auto simulation_reference=[&]() -> std::optional<helper::Reference> {
                        if(value.helper.power_w<=0)return std::nullopt;
                        auto reference=helper_reference(simulation_bank_source_origin+simulation_bank->samples,value.transfer.modem.sample_rate,version);
                        if(!reference)return std::nullopt;
                        return detail::rebase_helper_reference(std::move(*reference),simulation_bank_source_origin,
                            simulation_bank_source_origin+simulation_bank->samples,value.transfer.modem.sample_rate);
                    };
                    // Freeze already observed assistance before producing the
                    // next block: physical FIR lookahead must neither leak into
                    // this frontier nor evict its last eligible reference.
                    const auto reference=simulation_reference();
                    std::array<float, plot_size> samples{};
                    if(wave && wave->simulation_leading_samples) {
                        // Advance the independent listener on ordinary idle
                        // media until TX's future calendar boundary arrives.
                        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),wave->simulation_leading_samples));
                        auto idle=std::span(samples).first(count);begin_simulation_input();simulation_channel->read_noise(idle);
                        wave->simulation_leading_samples-=count;account(count,version);plot_window.push(idle);
                        before_simulation_feed(count);
                        feed_samples(*simulation_bank,idle,value,version,wave->stop,nullptr,processing_token,reference);
                        finish_simulation_input();
                        continue;
                    }
                    if(wave && wave->simulation_channel_pending) {
                        simulation_channel->set_helper_callsign(value.helper.callsign);
                        simulation_channel->set_helper_time_key(value.transfer.key);
                        simulation_channel->begin_burst(wave->helper_only);wave->simulation_channel_pending=false;
                    }
                    if (wave && wave->noise) {
                        // An open-ended tuning source has no completion replay.
                        // Pace its physical samples while the independent RX
                        // retains its ordinary keys and admission rules.
                        const auto block_start = Clock::now();
                        const auto capacity = std::min<std::size_t>(samples.size(),
                            std::max<std::size_t>(1, value.transfer.modem.sample_rate / 20));
                        prepare_source_read(*wave);
                        begin_simulation_input();
                        const auto count = simulation_channel->read(*wave->transmitter,
                            std::span(samples).first(capacity), wave->stop);
                        wave->transmitted_samples = simulation_channel->transmitted_samples();
                        if (!count) { finish_simulation_input();complete_tx(*wave); wave.reset(); continue; }
                        const auto input_samples = std::span(samples).first(count);
                        account(count, version); progress(*wave, value);
                        plot_window.push(input_samples);
                        before_simulation_feed(count);
                        feed_samples(*simulation_bank,input_samples,value,version,wave->stop,nullptr,processing_token,reference);
                        finish_simulation_input();
                        publish(plot_window,transmit_modem,version,last_plot,false,
                            wave->transmitter.get(),wave->serial);
                        const auto deadline = block_start + std::chrono::duration_cast<Clock::duration>(
                            std::chrono::duration<double>(static_cast<double>(count) / value.transfer.modem.sample_rate));
                        std::unique_lock lock(mutex);
                        changed.wait_until(lock,stop,deadline,[&] {
                            return !current.running || generation != version || wave->stop.stop_requested();
                        });
                        continue;
                    }
                    if(wave && wave->tail_started) {
                        bool finish_after_source=false;
                        {std::lock_guard lock(mutex);finish_after_source=finish_requested;}
                        if(finish_after_source && value.helper.power_w<=0) {
                            wave->graceful=true;complete_tx(*wave);wave.reset();continue;
                        }
                    }
                    if (wave) {
                        // First replay frame records the independently running
                        // receiver before any newly transmitted samples arrive.
                        if (wave->replay.empty()) {
                            wave->pattern_score_observation_id = pattern_score_observation_id.load(std::memory_order_relaxed);
                            collect_replay(*wave, transmit_modem, plot_window, value.simulation_spectrum_gain_db);
                        }
                        std::size_t count = 0;
                        begin_simulation_input();
                        if (!wave->tail_started) {
                            const auto target = wave->replay.size() < wave->replay_count ? replay_target(*wave) :
                                wave->transmitter->total_samples();
                            const auto remaining = target > wave->transmitted_samples ? target - wave->transmitted_samples : 1;
                            const auto capacity = static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(), remaining));
                            prepare_source_read(*wave);
                            count = simulation_channel->read(*wave->transmitter, std::span(samples).first(capacity), wave->stop);
                            wave->transmitted_samples = simulation_channel->transmitted_samples();
                            if (!count || (simulation_channel->physical_sample_rate()!=transmit_modem.sample_rate &&
                                           simulation_channel->payload_finished())) {
                                wave->simulation_envelope_finished=!count;
                                if(!count && wave->graceful) {
                                    // Stop only the source/replay work. The same
                                    // receiver continues on ordinary idle PCM;
                                    // no scored absence or completion is invented.
                                    finish_simulation_input();complete_tx(*wave);wave.reset();continue;
                                }
                                wave->tail_started = true;
                                wave->tail_remaining = modem::pattern_absence_capture_samples(transmit_modem)+transmit_modem.sample_rate+2*modem::pattern_pulse_padding_samples(transmit_modem);
                                if(simulation_channel->physical_sample_rate()!=transmit_modem.sample_rate) {
                                    if(wave->tail_remaining>UINT64_MAX-128)throw Error("Helper Carrier capture tail exceeds the sample clock");
                                    wave->tail_remaining+=128; // Centered physical FIR support.
                                }
                                // A finite simulation must supply real noise
                                // until admitted native FFTs can score captured
                                // audio. Ending before their first batch would
                                // truncate ordinary and assisted reception.
                                for(const auto& receiver:simulation_bank->receivers()) {
                                    const auto span=receiver.modem->initial_fft_input_samples();
                                    const auto origin=receiver.sample_origin,rate=static_cast<std::uint64_t>(transmit_modem.sample_rate);
                                    if(span && origin<=UINT64_MAX-rate && span<=UINT64_MAX-origin-rate) {
                                        const auto required=origin+span+rate;
                                        if(required>simulation_bank->samples)
                                            wave->tail_remaining=std::max(wave->tail_remaining,required-simulation_bank->samples);
                                    }
                                }
                                // Helper post-roll shares the real capture
                                // absence interval; it is not an extra source
                                // EOF followed by a second absence interval.
                                simulation_channel->extend_helper_tail(
                                    static_cast<double>(wave->tail_remaining)/transmit_modem.sample_rate);
                            }
                        }
                        if (!count && wave->tail_started &&
                            (wave->tail_remaining || !wave->simulation_envelope_finished)) {
                            count = wave->tail_remaining?static_cast<std::size_t>(
                                std::min<std::uint64_t>(samples.size(),wave->tail_remaining)):samples.size();
                            const auto helper_count=simulation_channel->read(*wave->transmitter,
                                std::span(samples).first(count),wave->stop);
                            if(helper_count)count=helper_count;
                            else {
                                wave->simulation_envelope_finished=true;
                                if(wave->tail_remaining)simulation_channel->read_noise(std::span(samples).first(count));
                                else count=0;
                            }
                            wave->tail_remaining-=std::min<std::uint64_t>(wave->tail_remaining,count);
                        }
                        const auto input_samples = std::span(samples).first(count);
                        account(count, version); progress(*wave, value);
                        plot_window.push(input_samples);
                        before_simulation_feed(count);
                        feed_samples(*simulation_bank,input_samples,value,version,wave->stop,wave.get(),processing_token,reference);
                        finish_simulation_input();
                        publish(plot_window, transmit_modem, version, last_plot, false,
                                nullptr, wave->serial, &wave->pattern_scores, &wave->pattern_score_observations,
                                wave->pattern_score_observation_id);
                        if (!wave->tail_started && wave->replay.size() < wave->replay_count &&
                            wave->replay.size()+1 < wave->replay_count &&
                            wave->transmitted_samples >= replay_target(*wave))
                            collect_replay(*wave, transmit_modem, plot_window, value.simulation_spectrum_gain_db);
                        if (wave->tail_started && !wave->tail_remaining && wave->simulation_envelope_finished) {
                            // Short pattern bursts can be scored only after
                            // trailing samples complete a receiver window.
                            // Reserve their final replay frame for that evidence.
                            if (wave->replay.size() < wave->replay_count)
                                collect_replay(*wave, transmit_modem, plot_window, value.simulation_spectrum_gain_db);
                            complete_tx(*wave); wave.reset();
                        }
                        execution::yield();
                    } else {
                        // Idle media continues during stream preparation and
                        // replay. Noise and burst audio use the same RX clock.
                        idle_fraction += value.transfer.modem.sample_rate % 20;
                        const auto count = std::min<std::size_t>(plot_size,
                            value.transfer.modem.sample_rate / 20 + idle_fraction / 20);
                        idle_fraction %= 20;
                        auto idle = std::span(samples).first(count);
                        begin_simulation_input();
                        simulation_channel->read_noise(idle);
                        account(idle.size(), version);
                        plot_window.push(idle);
                        publish(plot_window, value.transfer.modem, version, last_plot);
                        before_simulation_feed(idle.size());
                        feed_samples(*simulation_bank,idle,value,version,processing_token,nullptr,{},reference);
                        finish_simulation_input();
                        std::unique_lock lock(mutex);
                        changed.wait_for(lock, stop, std::chrono::milliseconds(50), [this, version] {
                            return !current.running || generation != version || ready;
                        });
                    }
                    continue;
                }
                if (wave) {
                    audio::ChannelMode channels;
                    audio::Options output_options;
                    std::optional<helper::Generator> helper_generator;
                    std::optional<helper::Plan> helper_output_plan;
                    std::uint64_t helper_output_frames=0;
                    bool helper_transmit=value.helper.power_w>0,output_started=false;
                    wave->hardware_helper_enabled=helper_transmit;
                    const auto disable_helper=[&](const std::string& reason) {
                        if(wave->helper_only)throw Error(reason);
                        if(!helper_output_plan)throw Error("Helper Carrier calibration unavailable: "+reason);
                        helper_transmit=false;helper_generator.reset();
                        wave->hardware_helper_enabled=false;
                        if(!wave->transmitter)
                            wave->hardware_helper_leading_samples=wave->hardware_helper_trailing_samples=0;
                        helper_unavailable(version,reason+"; transmitting ordinary Robust signal at calibrated data amplitude");
                    };
                    const auto add_helper_frames=[](std::uint64_t first,std::uint64_t second) {
                        if(first>UINT64_MAX-second)throw Error("Helper Carrier output coordinate exhausted");
                        return first+second;
                    };
                    const auto helper_lead_seconds=[&]() -> long double {
                        return static_cast<long double>(wave->hardware_helper_leading_samples)/wave->modem.sample_rate;
                    };
                    const auto helper_origin=[&]() -> long double {
                        const auto& c=wave->modem;
                        return c.symbol_burst.enabled() ||
                            (c.scheduled_hopping && c.scheduled_hopping->enabled() && !c.scheduled_hopping->continuous())?
                            static_cast<long double>(wave->transmitter->physical_output_origin())/c.sample_rate-c.stream_epoch:
                            (static_cast<long double>(c.stream_phase_samples)-modem::training_sample_count(c)-modem::pattern_pulse_padding_samples(c))/c.sample_rate;
                    };
                    const auto helper_source_end=[&]() -> std::optional<std::uint64_t> {
                        if(!wave->transmitter || (wave->noise&&!wave->graceful))return {};
                        const auto total=wave->transmitter->total_samples();
                        const auto wrapped=add_helper_frames(wave->hardware_helper_leading_samples,total);
                        if(!helper_transmit && !wave->hardware_helper_leading_samples && !wave->hardware_helper_trailing_samples)return total;
                        const auto origin=helper_origin();
                        const auto requested=origin+static_cast<long double>(total)/wave->modem.sample_rate+
                            static_cast<long double>(wave->hardware_helper_trailing_samples)/wave->modem.sample_rate;
                        const auto aligned=std::ceil(((wave->modem.stream_epoch%2)+requested)/.4L)*.4L-(wave->modem.stream_epoch%2);
                        const auto end=helper::protected_endpoint(wave->modem.stream_epoch,origin-helper_lead_seconds(),
                            aligned);
                        const auto frames=std::ceil((end-origin+helper_lead_seconds())*wave->modem.sample_rate);
                        if(!std::isfinite(frames)||frames<0||frames>=std::ldexp(1.L,64))throw Error("Helper Carrier stop coordinate exhausted");
                        return std::max(wrapped,static_cast<std::uint64_t>(frames));
                    };
                    channels=wave->output_channels;
                    output_options={wave->output_gain,value.exclusive};
                    if(helper_transmit) {
                        // Preserve the data amplitude even if optional helper
                        // output later becomes unavailable. Returning full-scale
                        // PCM would invalidate the operator's RF calibration.
                        // Calibration does not require the optional tone to
                        // fit, including when a gate allowance crosses DC.
                        // Invalid options still reject before the placement catch.
                        helper_output_plan=helper::Plan{};
                        helper_output_plan->data_gain=helper::calibrated_data_gain(value.helper);
                        try {
                            helper_output_plan=helper::plan(value.helper,transmit_modem.carrier_hz,
                                wave->helper_bandwidth,120000000,0,modem::outer_dsss_transmit_gain(transmit_modem));
                            output_options.minimum_hardware_rate=helper::minimum_output_rate(value.helper,
                                transmit_modem.carrier_hz,wave->helper_bandwidth,transmit_modem.sample_rate);
                        } catch(const Error& error) {
                            disable_helper(error.what());
                        }
                    }
                    if(value.helper.power_w>0) {
                        output_options.playback_output_transform=[&](float payload,long double seconds,std::uint32_t rate) {
                            if(!helper_transmit)return static_cast<float>(payload*helper_output_plan->data_gain);
                            if(!helper_generator || !wave->transmitter)
                                throw Error("Enabled Helper Carrier has no calibrated output generator");
                            const auto& c=wave->modem;
                            const auto origin=helper_origin();
                            const auto start=origin-helper_lead_seconds();
                            const auto end=helper_source_end();
                            return helper_generator->mix(wave->helper_only?0.f:payload,start+seconds,rate,
                                end?std::optional<long double>(start+static_cast<long double>(*end)/c.sample_rate):std::nullopt,start);
                        };
                    }
                    output_options.maximum_utc_error_seconds=clock_sync::audio_lag_radius(value.transfer.audio_timing_error_seconds);
                    output_options.presentation_lag_seconds=clock_sync::audio_lag_radius(value.transfer.audio_timing_error_seconds);
                    output_options.timing_stop_duration_seconds=wave->timing_stop_seconds;
                    if(helper_transmit && wave->timing_stop_seconds>0) {
                        // Preparation precedes the final keyed origin. Reserve
                        // the maximum dwell alignment plus the independent tail.
                        const auto tail=std::max({2.L,static_cast<long double>(wave->hardware_helper_trailing_samples)/transmit_modem.sample_rate,
                            (static_cast<long double>(modem::pattern_absence_capture_samples(transmit_modem))+
                            transmit_modem.sample_rate+2.L*modem::pattern_pulse_padding_samples(transmit_modem))/transmit_modem.sample_rate});
                        const auto extra=static_cast<double>(2.8L+tail+2.L/transmit_modem.sample_rate);
                        output_options.timing_stop_duration_seconds+=extra;
                        // The lead precedes valid payload, so it cannot be
                        // subtracted from an absolute payload-end forecast.
                        // Only the guaranteed independent post-roll is cover.
                        output_options.timing_independent_cover_seconds=static_cast<double>(tail);
                    }
                    if(wave->timing_stop_seconds>0 && modem::private_cover_enabled(transmit_modem)) {
                        // Reserve the same PHYSICAL clock for both terms. A
                        // source-time cover duration cannot be subtracted from
                        // a paused physical stopping horizon: that would label
                        // most null/cover time as outstanding valid payload.
                        // The runtime callback below uses the exact keyed end.
                        const auto stop_source=modem::transmit_stop_sample_count(transmit_modem);
                        const auto cover_source=modem::suppression_sample_count(transmit_modem);
                        const auto valid_wall=static_cast<double>(modem::scheduled_physical_span_samples(
                            transmit_modem,stop_source-cover_source))/transmit_modem.sample_rate;
                        output_options.timing_independent_cover_seconds+=modem::scheduled_bursts(transmit_modem)?
                            std::max(0.,wave->timing_stop_seconds-valid_wall):
                            static_cast<double>(cover_source)/transmit_modem.sample_rate;
                    }
                    if(value.helper.power_w>0 || transmit_modem.symbol_burst.enabled() ||
                       (transmit_modem.scheduled_hopping && transmit_modem.scheduled_hopping->enabled() && !transmit_modem.scheduled_hopping->continuous())) {
                        output_options.timing_protected_payload_end=[&](std::uint64_t frontier)->std::optional<std::uint64_t> {
                            if(!wave->transmitter)return std::nullopt;
                            const auto lead=wave->hardware_helper_leading_samples;
                            const auto source_frontier=frontier>lead?frontier-lead:0;
                            return add_helper_frames(lead,wave->transmitter->predicted_physical_valid_payload_end(source_frontier));
                        };
                    }
                    output_options.timing_source_end=[&]() -> std::optional<std::uint64_t> {
                        // Identical termination accounting for message/noise.
                        // No source callbacks or keystream addresses advance.
                        return helper_source_end();
                    };
                    if(transmit_modem.fake_hopping && transmit_modem.fake_hopping->smooth_ramps) {
                        const modem::FhssPowerShape shape(*transmit_modem.fake_hopping,transmit_modem.sample_rate,
                            modem::pattern_pulse_padding_samples(transmit_modem),modem::symbol_sample_count(transmit_modem));
                        output_options.playback_output_gain=[&,shape](long double seconds) {
                            seconds-=helper_lead_seconds();
                            const auto& c=wave->modem;
                            const auto origin=c.symbol_burst.enabled() ||
                                (c.scheduled_hopping && c.scheduled_hopping->enabled() && !c.scheduled_hopping->continuous())?
                                static_cast<long double>(wave->transmitter->physical_output_origin())-static_cast<long double>(c.stream_epoch)*c.sample_rate:
                                static_cast<long double>(c.stream_phase_samples)-modem::training_sample_count(c)-modem::pattern_pulse_padding_samples(c);
                            return shape.gain(c.stream_epoch,origin+seconds*c.sample_rate,c.sample_rate);
                        };
                    }
                    output_options.follow_system_clock=transfer::effective_clock_sync(value.transfer) &&
                        audio::utc_follow_supported() && (!transmit_modem.oscillator_search ||
                        transmit_modem.oscillator_search->reference==modem::OscillatorReference::independent_audio);
                    if(output_options.follow_system_clock && transmit_modem.oscillator_search) {
                        const auto& declared=*transmit_modem.oscillator_search;
                        // The existing accuracy is an effective relative-link
                        // model, not measured per-device metrology. Use it ONCE
                        // as an explicit local continuity assumption; observations
                        // must remain consistent. Search margins still belong to
                        // the receiver bank and are not a second hardware clock.
                        const auto& reference=declared.reference==modem::OscillatorReference::shared_radio && declared.rf_shift_hz>0?
                            declared.rf:declared.lf;
                        output_options.clock_timing=audio::ClockTimingPolicy{reference.accuracy_ppm*1e-6,.001,transfer::effective_clock_sync(value.transfer)->accuracy_seconds};
                    }
                    output_options.maximum_timing_slew_per_second=audio::conservative_timing_slew(
                        transmit_modem.sample_rate,static_cast<double>(modem::symbol_sample_count(transmit_modem))/transmit_modem.sample_rate);
                    if(output_options.follow_system_clock && wave->timing_rate_fraction>0) {
                        output_options.maximum_timing_rate_correction=wave->timing_rate_fraction;
                        output_options.estimated_timing=audio::EstimatedDeviceTiming{};
                        output_options.estimated_timing->maximum_utc_error_seconds=clock_sync::audio_lag_radius(value.transfer.audio_timing_error_seconds);
                    }
                    output_options.allow_timing_fallback=output_options.follow_system_clock;
                    if(output_options.follow_system_clock) {
                        const auto symbol_seconds=static_cast<double>(modem::symbol_sample_count(transmit_modem))/transmit_modem.sample_rate;
                        // A token budget limits counter-only reports to at most
                        // two budgets in any symbol-length interval. It is NOT
                        // a hardware-loss certificate. Fast symbols remain strict
                        // when a graph quantum exceeds their budget.
                        output_options.reported_xrun_budget_seconds=std::min(.1,.01*symbol_seconds);
                        output_options.reported_xrun_window_seconds=symbol_seconds;
                    }
                    output_options.playback_discontinuity=[](const std::string& reason) {
                        throw Error(reason); // A recovered device gap invalidates this private transmission.
                    };
                    output_options.timing_status=[&,version](const audio::TimingStatus& status) {
                        timing_status(status,version,false);
                        if(!status.following_system_clock) {
                            // Preflight runs before scheduling/private generation.
                            // Keep monotonic fresh epochs but use ordinary playback.
                            wave->timing_rate_fraction=wave->timing_stop_seconds=0;

                        }
                    };
                    output_options.timing_protected_stop=[&](const std::string& reason) {
                        std::lock_guard lock(mutex);
                        if(generation!=wave->generation || tx_serial!=wave->serial || wave->stop.stop_requested())
                            throw Error("transmission cancelled");
                        wave->transmitter->request_finish();wave->graceful=true;
                        // This is a truncated request, not successful completion
                        // or a user cancellation. Keep the failure through cover,
                        // DAC drain and later capture updates. Do not throw here:
                        // the finite coefficient source must finish protection.
                        transmit_error=reason;current.error=reason;
                        finish_requested=true;current.transmission_stopping=true;
                        queued.clear();
                        current.status="Finishing echo protection and pulse tails; cancel again to abort immediately";
                        ++current.sequence;changed.notify_all();
                    };
                    if(!value.full_duplex)discontinuity();
                    const auto play=[&] {audio::playback(transmit_modem.sample_rate, value.device, [&](std::span<float> output) {
                        if(!output_started) {
                            output_started=true;
                            audio::trace::record("transmitter_seed_committed",[&](auto& f) {
                                f.add("transmission_id",wave->serial);f.add("generation",wave->generation);
                                f.add("epoch",wave->modem.stream_epoch);
                                f.add("stream_phase_samples",wave->modem.stream_phase_samples);
                                f.add("sample_rate",wave->modem.sample_rate);
                                f.add("symbol_samples",modem::symbol_sample_count(wave->modem));
                                f.add("helper_enabled",helper_transmit);
                                f.add("helper_leading_samples",wave->hardware_helper_leading_samples);
                            });
                        }
                        const auto before=wave->transmitter->samples_emitted();
                        std::size_t count=0;
                        if(helper_output_frames<wave->hardware_helper_leading_samples) {
                            count=static_cast<std::size_t>(std::min<std::uint64_t>(output.size(),
                                wave->hardware_helper_leading_samples-helper_output_frames));
                            std::fill_n(output.begin(),count,0.f);
                            helper_output_frames=add_helper_frames(helper_output_frames,count);
                        }
                        if(count<output.size() && !wave->transmitter->finished()) {
                            prepare_source_read(*wave);
                            const auto payload=wave->transmitter->read(output.subspan(count),wave->stop);
                            count+=payload;helper_output_frames=add_helper_frames(helper_output_frames,payload);
                        }
                        if(wave->transmitter->finished() && (helper_transmit ||
                           wave->hardware_helper_leading_samples || wave->hardware_helper_trailing_samples)) {
                            if(const auto end=helper_source_end();end && helper_output_frames<*end) {
                                const auto padding=static_cast<std::size_t>(std::min<std::uint64_t>(output.size()-count,*end-helper_output_frames));
                                std::fill_n(output.begin()+static_cast<std::ptrdiff_t>(count),padding,0.f);
                                count+=padding;helper_output_frames=add_helper_frames(helper_output_frames,padding);
                            }
                        }
                        retain_emitted_epoch(*wave);
                        if(!value.full_duplex)account(count, version);
                        plot_window.push(output.first(count));
                        const auto payload_start=modem::training_sample_count(transmit_modem)+
                            modem::pattern_pulse_padding_samples(transmit_modem);
                        const auto first_payload=before<=payload_start && wave->transmitter->samples_emitted()>payload_start;
                        publish(plot_window, transmit_modem, version, last_plot,
                                first_payload || wave->transmitter->finished(), wave->transmitter.get(), wave->serial,
                                nullptr,nullptr,0,value.full_duplex);
                        // Publish the first actual payload chips before progress
                        // can describe a post-settling constellation to the UI.
                        progress(*wave, value);
                        return count;
                    }, wave->stop, [&](const auto& format) {
                        audio_format(format, version,false);
                        if(helper_transmit) {
                            try {
                                helper_output_plan=helper::plan(value.helper,wave->modem.carrier_hz,wave->helper_bandwidth,
                                    format.hardware_rate,0,modem::outer_dsss_transmit_gain(wave->modem));
                            }catch(const Error& error){disable_helper(error.what());}
                        }
                        if(wave->prepare_hardware) {
                            wave->prepare_hardware(*wave);
                            wave->prepare_hardware={};
                        } else if(wave->hardware_helper_leading_samples) {
                            // A provider pre-source retry retains the private
                            // payload origin. Declining a lead that no longer
                            // fits must not move that already planned origin.
                            const auto physical=wave->transmitter->physical_output_origin();
                            const auto payload_epoch=static_cast<double>(physical/wave->modem.sample_rate)+
                                static_cast<double>(physical%wave->modem.sample_rate)/wave->modem.sample_rate;
                            auto playback_epoch=payload_epoch-static_cast<double>(helper_lead_seconds());
                            if(!helper_transmit || current_epoch()+audio::minimum_lead_seconds()>=playback_epoch) {
                                if(helper_transmit)disable_helper("Helper Carrier lead no longer fits after reopening the audio provider");
                                wave->hardware_helper_leading_samples=wave->hardware_helper_trailing_samples=0;
                                playback_epoch=payload_epoch;
                            }
                            audio::schedule_output(playback_epoch,wave->stop);
                        }
                        if(helper_transmit&&!wave->hardware_helper_enabled)
                            disable_helper("Helper Carrier lead does not fit before the explicit Robust timestamp");
                        if(helper_transmit) {
                            try {
                                const auto& c=wave->modem;
                                helper_output_plan=helper::plan(value.helper,c.carrier_hz,
                                    wave->helper_bandwidth,format.hardware_rate,0,
                                    modem::outer_dsss_transmit_gain(c));
                                helper_generator.emplace(value.helper,*helper_output_plan,c.stream_epoch,true,value.transfer.key);
                            } catch(const Error& error) {
                                disable_helper(error.what());
                            }
                        }
                    }, channels, output_options);};
                    try {play();}
                    catch(const Error& error) {
                        // A helper-only rate request may fail during provider
                        // setup. Retry ordinary playback only before any PCM
                        // callback; an actual output gap must still fail TX.
                        if(!helper_transmit || output_started || wave->helper_only)throw;
                        disable_helper(error.what());
                        output_options.minimum_hardware_rate=0;
                        play();
                    }
                    if(!value.full_duplex)discontinuity();
                    complete_tx(*wave); wave.reset(); plot_window.reset(); continue;
                }
            } catch (const std::exception& exception) {
                const bool invalidated_input=finish_simulation_input();
                if(media_clock && processing_token.stop_requested()) {
                    std::lock_guard lock(mutex);
                    // Clear intentionally replaces RX without cancelling TX.
                    // An obsolete admission/construction error must not turn
                    // that receive-only operation into a transmission error.
                    if(current.running && generation==version && receive_clear_generation!=input_clear_generation)continue;
                }
                const auto cancelled_transmission = wave && wave->stop.stop_requested();
                if (!cancelled_transmission && !preparing_media_transmitter) reset_simulation_bank("simulation_error");
                if (wave) { complete_tx(*wave, wave->stop.stop_requested() ? "" : exception.what()); wave.reset(); }
                std::unique_lock lock(mutex);
                if (current.running && generation == version && (!processing_token.stop_requested() || invalidated_input) && !cancelled_transmission && !preparing_media_transmitter) {
                    current.error = exception.what(); current.status = value.simulation ? "Simulation paused after channel error" : "Audio output unavailable; retrying";
                    changed.wait_for(lock, stop, std::chrono::seconds(2), [this, version] {
                        return !current.running || generation != version || ready || capture_suspended;
                    });
                }
            }
        }
    }
    bool capture_allowed_locked(std::uint64_t version) const {
        return current.running && generation==version && !settings.simulation && !capture_suspended &&
            (settings.full_duplex || (!ready && !source_active));
    }
    void capture_loop(std::stop_token stop) {
        DATAPUMP_MUTEX_CONTEXT(mutex_context,capture);
        std::optional<std::uint64_t> helper_capture_unavailable_generation;
        while(!stop.stop_requested()) {
            Settings value;std::uint64_t version;std::stop_token capture_token;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock,stop,[this] {return decoder_generation==generation && capture_allowed_locked(generation);});
                if(stop.stop_requested())break;
                value=settings;version=generation;
                capture_stop=std::stop_source{};capture_token=capture_stop.get_token();capture_active=true;
            }
            const auto release_activity=[this,version](Impl*) {
                std::lock_guard lock(mutex);capture_active=false;
                if(generation==version) {capture_audio_bytes=0;audio_bytes=playback_audio_bytes;}
                changed.notify_all();
            };
            const std::unique_ptr<Impl,decltype(release_activity)> activity(this,release_activity);
            detail::SignalWindow plot_window(value.transfer.modem);
            auto last_plot=Clock::time_point{};
            bool capture_format_accepted=false;
            std::uint32_t requested_helper_rate=0;
            try {
                {
                    std::lock_guard lock(mutex);
                    if (generation == version) {
                        if (!tx_busy) { current.status = idle_status(); current.error.clear(); }
                        // A device reopen starts a new observation segment even
                        // when its first plotted absolute coordinate increases.
                        current.spectrum_complex.clear();current.spectrum_sample_count=0;
                    }
                }
                plot_window.reset();
                std::optional<detail::SignalWindow> monitor_window;
                std::unique_ptr<detail::EnvironmentFrameWorkspace> monitor_frame;
                bool monitor_seen=false;
                bool helper_configured=false;
                bool helper_mapping_valid=true;
                std::uint64_t raw_samples=0,logical_samples=0;
                auto last_monitor=Clock::time_point{};
                // Raw monitor callbacks precede the resampler. Map each logical
                // output timestamp through its unconsumed input/lookahead, using
                // a fresh raw end time so hardware clock drift cannot accumulate
                // for hours. No waveform sample, filter state or rate is changed.
                long double raw_seconds=0,logical_seconds=0;
                std::optional<double> raw_end_epoch,logical_origin;
                std::optional<audio::TimePrediction> timed_chunk;
                audio::Options capture_options{1.0,value.exclusive};
                if(helper_capture_unavailable_generation!=version)
                    requested_helper_rate=helper::minimum_output_rate(value.helper,
                        value.transfer.modem.carrier_hz,detail::helper_occupied_bandwidth(value),
                        value.transfer.modem.sample_rate);
                capture_options.minimum_hardware_rate=requested_helper_rate;
                capture_options.maximum_utc_error_seconds=clock_sync::audio_lag_radius(value.transfer.audio_timing_error_seconds);
                // Observe physical sample dates independently of strict Audio
                // qualification/steering. In particular Audio0 retains the
                // provider's nonzero dating uncertainty in the calendar model.
                capture_options.request_capture_timing=transfer::shared_hop_calendar(value.transfer.modem);
                capture_options.follow_system_clock=transfer::effective_clock_sync(value.transfer) && audio::utc_follow_supported() &&
                    (!value.transfer.modem.oscillator_search ||
                     value.transfer.modem.oscillator_search->reference==modem::OscillatorReference::independent_audio);
                if((capture_options.follow_system_clock || capture_options.request_capture_timing) && value.transfer.modem.oscillator_search) {
                    const auto& declared=*value.transfer.modem.oscillator_search;
                    const auto& reference=declared.reference==modem::OscillatorReference::shared_radio && declared.rf_shift_hz>0?
                        declared.rf:declared.lf;
                    const auto policy=transfer::effective_clock_sync(value.transfer);
                    capture_options.clock_timing=audio::ClockTimingPolicy{reference.accuracy_ppm*1e-6,.001,
                        policy?policy->accuracy_seconds:0};
                }
                bool capture_following=capture_options.follow_system_clock;
                capture_options.allow_timing_fallback=capture_following;
                capture_options.timing_status=[&,version](const audio::TimingStatus& status) {
                    capture_following=status.following_system_clock;
                    if(!capture_following && !capture_options.request_capture_timing)timed_chunk.reset();
                    timing_status(status,version,true);
                };
                capture_options.capture_timing=[&](const audio::TimePrediction& timing) {
                    DATAPUMP_MUTEX_CONTEXT(mutex_context,provider_timing);
                    if(!capture_following && !capture_options.request_capture_timing)return;
                    if(timing.quality==audio::TimingQuality::unavailable || !std::isfinite(timing.utc_seconds) ||
                       timing.utc_seconds<0 || !std::isfinite(timing.seconds_per_frame) || timing.seconds_per_frame<=0 ||
                       !std::isfinite(timing.uncertainty_seconds) || timing.uncertainty_seconds<0)
                        throw Error("invalid captured audio UTC mapping");
                    timed_chunk=timing;
                    // arrival_map already adds this station's GPS phase bound.
                    // Keep the full raw timestamp in audio; only the receiver's
                    // separate Audio component is passed into its existing map.
                    timed_chunk->uncertainty_seconds=std::max(timing.provider_uncertainty_floor_seconds,timing.uncertainty_seconds-
                        timing.system_clock_phase_uncertainty_seconds);
                    std::lock_guard lock(mutex);
                    if(generation==version) {
                        current.clock_timing_quality=timing.quality;
                        current.clock_backend_uncertainty_seconds=timed_chunk->uncertainty_seconds;
                    }
                };
                capture_options.capture_discontinuity=[&]{
                    input_interrupted(version,capture_token);
                    plot_window.reset();
                    if(monitor_window)monitor_window->reset();
                    reset_helper(version);raw_samples=logical_samples=0;
                    // Timing-observation loss can preserve the converter while
                    // an xrun resets it. Both use this callback, so its source
                    // coordinate is no longer proved. Keep helper diagnostics,
                    // but yield automatic aid until the next fresh format.
                    helper_mapping_valid=false;
                    monitor_seen=false;
                    {std::lock_guard lock(mutex);
                        if(current.running && generation==version && !capture_suspended && !capture_token.stop_requested()) {
                            current.spectrum_complex.clear();current.spectrum_sample_count=0;
                        }
                    }
                    raw_seconds=logical_seconds=0;raw_end_epoch.reset();logical_origin.reset();timed_chunk.reset();
                };
                capture_options.capture_monitor=[&](std::span<const float> raw,std::uint32_t rate) {
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
                    capture_monitor_probe::Observation monitor_observation(
                        monitor_window?monitor_window->samples_seen():0,raw.size());
#endif
                    DATAPUMP_MUTEX_CONTEXT(mutex_context,monitor);
                    if(raw.empty())return;
                    raw_end_epoch=current_epoch();
                    raw_seconds+=static_cast<long double>(raw.size())/rate;
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
                    capture_monitor_probe::mark(capture_monitor_probe::Phase::epoch);
#endif
                    if(helper_configured)observe_helper(raw,rate,raw_samples,version);
                    raw_samples+=raw.size();
                    if(!monitor_window)return;
                    monitor_window->push(raw);
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
                    capture_monitor_probe::mark(capture_monitor_probe::Phase::push);
#endif
                    auto config=value.transfer.modem;config.sample_rate=rate;
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
                    capture_monitor_probe::mark(capture_monitor_probe::Phase::config);
#endif
                    publish_monitor(*monitor_window,*monitor_frame,config,version,last_monitor);
                    monitor_seen=true;
                };
                audio::capture(value.transfer.modem.sample_rate, value.device, [&](std::span<const float> chunk) {
                    const auto duration=static_cast<long double>(chunk.size())/value.transfer.modem.sample_rate;
                    if(!raw_end_epoch && !logical_origin)logical_origin=current_epoch()-static_cast<double>(duration);
                    const auto observed=timed_chunk?static_cast<double>(timed_chunk->utc_seconds):raw_end_epoch?
                        *raw_end_epoch-static_cast<double>(raw_seconds-logical_seconds):
                        *logical_origin+static_cast<double>(logical_seconds);
                    const auto reference=helper_configured&&helper_mapping_valid?
                        helper_reference(logical_samples,value.transfer.modem.sample_rate,version):std::nullopt;
                    logical_seconds+=duration;
                    account(chunk.size(), version); plot_window.push(chunk);
                    publish(plot_window, value.transfer.modem, version, last_plot,false,nullptr,0,nullptr,nullptr,0,monitor_seen);
                    enqueue_audio(chunk, version,std::max(0.,observed),timed_chunk,reference,logical_samples);
                    logical_samples+=chunk.size();
                    timed_chunk.reset();
                    std::lock_guard lock(mutex);
                    return capture_allowed_locked(version);
                }, capture_token, [&](const auto& format) {
                    // Drop the previous observer before its charge is replaced,
                    // including when a repeated format loses monitor headroom.
                    monitor_frame.reset();monitor_window.reset();monitor_seen=false;
                    audio_format(format, version);
                    capture_format_accepted=true;
                    helper_configured=false;helper_mapping_valid=true;raw_samples=logical_samples=0;
                    bool monitor_reserved=false;
                    {
                        std::lock_guard lock(mutex);
                        const auto reserve=audio_reserve(value),peer=playback_audio_bytes;
                        if(generation==version && peer<=reserve && capture_audio_bytes<=reserve-peer &&
                           monitor_workspace<=reserve-peer-capture_audio_bytes) {
                            capture_audio_bytes+=monitor_workspace;
                            audio_bytes=capture_audio_bytes+playback_audio_bytes;monitor_reserved=true;
                        }
                    }
                    // Default capacity is exactly 2048, independent of the
                    // hardware rate and narrowband symbol length.
                    if(monitor_reserved) {
                        monitor_window.emplace();
                        const auto actual=sizeof(monitor_window)+monitor_window->sample_storage_bytes()+
                            sizeof(monitor_frame)+sizeof(detail::EnvironmentFrameWorkspace);
                        if(actual>monitor_workspace)throw Error("audio monitor exceeds its reserved workspace");
                        monitor_frame=std::make_unique<detail::EnvironmentFrameWorkspace>();
                    }
                    // The existing sampled-channel reservation also holds the
                    // bounded observers during mode transitions (asserted above
                    // and at the complete channel type). Leave audio admission,
                    // monitor priority and the modem bank unchanged.
                    helper_configured=helper_capture_unavailable_generation!=version&&
                        configure_helper(value,version,format.hardware_rate);
                }, std::move(capture_options));
            } catch(const std::exception& exception) {
                // A helper-only hardware requirement must not remove ordinary
                // reception on a slower device. Retry only pre-PCM setup; a
                // failure after format acceptance retains normal gap handling.
                if(!capture_format_accepted&&value.helper.power_w>0&&
                   helper_capture_unavailable_generation!=version&&!capture_token.stop_requested()) {
                    helper_capture_unavailable_generation=version;
                    helper_unavailable(version,std::string(exception.what())+"; ordinary capture retained");
                    continue;
                }
                std::unique_lock lock(mutex);
                if(current.running && generation==version && !capture_token.stop_requested()) {
                    current.receiver_health.input_interrupted=true;
                    count_dropped(current.receiver_health.dropped_samples,input_bytes/sizeof(float));
                    count_dropped(current.receiver_health.dropped_samples,delayed_processing_pcm_samples);
                    count_dropped(current.receiver_health.dropped_samples,decoding_remaining_samples);
                    reset_input_locked();current.error=exception.what();
                    current.status="Audio input unavailable; retrying";
                    changed.wait_for(lock,stop,std::chrono::seconds(2),[this,version] {
                        return !capture_allowed_locked(version);
                    });
                }
            }
        }
    }
    void encode_loop(std::stop_token stop) {
        DATAPUMP_MUTEX_CONTEXT(mutex_context,encoder);
        while (!stop.stop_requested()) {
            Transmission transmission; Settings value; Bytes key; bool force=false,noise_long_profile=false,helper_only=false;
            std::shared_ptr<const modem::FhssPlan> hopping;
            std::uint64_t version, serial,clear_generation; std::stop_token token;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, stop, [this] { return current.running && !tx_busy && replay.empty() && !queued.empty(); });
                if (stop.stop_requested()) break;
                transmission = std::move(queued.front().content); force=queued.front().force;
                hopping=std::move(queued.front().hopping);noise_long_profile=queued.front().noise_long_profile;helper_only=queued.front().helper_only;
                queued.pop_front(); value = settings; key=selected_transmit_key;
                version = generation;clear_generation=receive_clear_generation; serial = ++tx_serial; tx_stop = std::stop_source{}; token = tx_stop.get_token();
                current.transmission_id = serial;current.transmit_clock_following=false;
                current.transmission_stopping=false;source_pcm_started=false;finish_requested=false;
                transmit_error.clear(); current.error.clear();
                playback_timing_status.clear();playback_timing_fallback=false;
                current.clock_timing_status=capture_timing_status;
                current.clock_timing_fallback=capture_timing_fallback;
                current.transmit_trace = {};
                clear_replay(); pending_points = {};
                current.constellation.clear(); current.constellation_source = ConstellationSource::input;
                current.constellation_dropped = 0;
                tx_busy = true; current.transmitting = true; current.transmission_finished = false;
                current.transmitting_noise = std::holds_alternative<modem::Noise>(transmission);
                current.transmission_fraction = current.transmission_seconds = 0;
                current.simulation_compute_seconds=0;current.simulation_receiving_tail=false;
                simulation_compute_started=value.simulation && !current.transmitting_noise?
                    std::optional<Clock::time_point>(Clock::now()):std::nullopt;
                current.status = current.transmitting_noise ? "Preparing noise with temporary keys" :
                    std::holds_alternative<Bytes>(transmission) ? "Preparing raw binary signal" :
                    "Preparing fixed-interval stream";
            }
            try {
                auto helper_bandwidth=detail::helper_occupied_bandwidth(value);
                if (const auto* message = std::get_if<Message>(&transmission);
                    message && value.long_message_modem && !transfer::uses_raw_message(*message))
                    value.transfer.modem = *value.long_message_modem;
                value.transfer.modem.fake_hopping=std::move(hopping);
                const bool noise=std::holds_alternative<modem::Noise>(transmission);
                if(noise) {
                    if(noise_long_profile && value.long_message_modem) {
                        auto plan=std::move(value.transfer.modem.fake_hopping);
                        value.transfer.modem=*value.long_message_modem;
                        value.transfer.modem.fake_hopping=std::move(plan);
                    }
                    key.clear(); // No saved-key history or receive-bank insertion.
                    value.transfer.modem.spreading_mode=modem::SpreadingMode::pattern;
                    value.transfer.modem.scramble=true;
                }
                if(value.transfer.modem.fake_hopping)
                    value.transfer.modem.fake_hopping=transfer::transmit_hopping(*value.transfer.modem.fake_hopping,value.transfer);
                helper_bandwidth=std::max(helper_bandwidth,helper::occupied_bandwidth(value.transfer.modem));
                // Simulation has its own deterministic sample timeline and
                // never consumes the hardware key-use history.
                const bool media_clock=simulation_media_clock(value);
                if(!media_clock && value.simulation && !value.transfer.timestamp && !value.transfer.modem.symbol_burst.enabled())
                    value.transfer.timestamp=static_cast<std::uint64_t>(current_epoch());
                if(!value.simulation && !force && !noise) {
                    // Keep capture available during potentially hours-long
                    // absence waits. The source thread rechecks, rather than
                    // waiting with the output device open, if a later
                    // deadline is established after this point.
                    std::unique_lock lock(mutex);
                    changed.wait_until(lock,token,next_hardware_send,[&]{return token.stop_requested();});
                    if(token.stop_requested() || stop.stop_requested())continue;
                }
                if(!value.simulation && transfer::effective_clock_sync(value.transfer) && force && !key.empty())
                    throw Error("UTC transmission cannot override the key epoch reuse guard");
                auto prepared = std::make_shared<Prepared>();
                prepared->modem = value.transfer.modem;
                prepared->timing_stop_seconds=static_cast<double>(modem::scheduled_physical_span_samples(value.transfer.modem,
                    modem::transmit_stop_sample_count(value.transfer.modem)))/value.transfer.modem.sample_rate;
                prepared->output_gain=value.transmit_gain;prepared->helper_bandwidth=helper_bandwidth;
                if(value.helper.power_w>0 && !value.simulation) {
                    std::lock_guard lock(mutex);
                    const auto aperture=current.receiver_initial_fft_input_samples;
                    const auto guard=static_cast<std::uint64_t>(value.transfer.modem.sample_rate)+128;
                    if(aperture>UINT64_MAX-guard)throw Error("Helper Carrier receiver aperture exceeds the source clock");
                    prepared->hardware_helper_trailing_samples=aperture+guard;
                }
                prepared->output_channels=audio::output_channels(value.mono,value.channel_mode);
                prepared->generation = version; prepared->serial = serial;prepared->receive_clear_generation=clear_generation; prepared->stop = token;
                prepared->binary = std::holds_alternative<Bytes>(transmission);
                prepared->noise=noise;prepared->helper_only=helper_only;
                prepared->hardware = !value.simulation;
                if (!value.simulation) {
                    if(transfer::effective_clock_sync(value.transfer) && value.transfer.modem.oscillator_search && audio::utc_follow_supported()) {
                        const auto geometry=modem::oscillator_pattern_search(value.transfer.modem,
                            modem::utc_transmit_rate_limit(value.transfer.modem));
                        prepared->timing_rate_fraction=geometry.transmit_rate_correction;
                        if(prepared->timing_rate_fraction>0) {
                            // Same immutable geometry and stopping horizon for
                            // finite messages and open-ended temporary-key noise.
                            prepared->timing_stop_seconds=static_cast<double>(modem::scheduled_physical_span_samples(value.transfer.modem,
                    modem::transmit_stop_sample_count(value.transfer.modem)))/value.transfer.modem.sample_rate;
                        }
                    }
                    prepared->prepare_hardware=[this,transmission=std::move(transmission),
                            value,key,force](Prepared& wave) mutable {
                        // This runs on the single audio source thread, after
                        // any cancelled preceding playback has fully returned.
                        // A queued request must recheck history here: the
                        // preceding waveform can advance it after enqueue.
                        if(!force && !wave.noise) {
                            std::lock_guard lock(mutex);
                            if(wave.stop.stop_requested())throw Error("transmission cancelled");
                            if(Clock::now()<next_hardware_send)
                                throw Error("Transmit locked: waiting for reception separation");
                            if(key_lock_seconds(value,value.transfer.modem,key)>0)
                                throw Error("Transmit locked: earlier output used this key for a future symbol");
                        }
                        auto options=value.transfer;
                        if(transfer::effective_clock_sync(options) && options.timestamp)
                            throw Error("UTC hardware transmission requires a freshly scheduled epoch");
                        if(wave.noise) options.modem=modem::StreamingTransmitter::prepare_noise_config(options.modem);
                        else if(options.modem.scheduled_hopping && options.modem.scheduled_hopping->enabled())
                            options.modem=transfer::seeded_config(options,options.timestamp);
                        const auto make=[&](std::uint64_t epoch,std::uint32_t phase=0) {
                            options.timestamp=epoch;options.modem.stream_epoch=epoch;options.modem.stream_phase_samples=phase;
                            wave.modem=options.modem;
                            if(wave.noise) {
                                auto config=options.modem;config.stream_epoch=epoch;
                                return std::make_unique<modem::StreamingTransmitter>(modem::Noise{},config,modem::StreamingTransmitter::PreparedTemporaryKeys{},value.dsp_workspace_bytes/4);
                            }
                            return wave.binary?transfer::binary_transmitter(std::get<Bytes>(transmission),options):
                                transfer::message_transmitter(std::get<Message>(transmission),options);
                        };
                        const auto prepare_helper_envelope=[&] {
                            if(!wave.hardware_helper_enabled)return;
                            const auto rate=wave.modem.sample_rate;
                            const auto physical=wave.transmitter->physical_output_origin();
                            const auto origin=static_cast<long double>(physical/rate)-wave.modem.stream_epoch+
                                static_cast<long double>(physical%rate)/rate;
                            const auto parity=wave.modem.stream_epoch%2;
                            const auto start=std::floor((parity+origin-2.L)/.4L)*.4L-parity;
                            const auto lead=std::ceil((origin-start)*rate);
                            const auto tail=std::max(2.L*rate,static_cast<long double>(modem::pattern_absence_capture_samples(wave.modem))+
                                rate+2.L*modem::pattern_pulse_padding_samples(wave.modem));
                            if(!std::isfinite(lead)||lead<0||lead>=std::ldexp(1.L,64)||
                               !std::isfinite(tail)||tail<0||tail>=std::ldexp(1.L,64))
                                throw Error("Helper Carrier envelope exceeds the source clock");
                            wave.hardware_helper_leading_samples=static_cast<std::uint64_t>(lead);
                            wave.hardware_helper_trailing_samples=std::max(wave.hardware_helper_trailing_samples,
                                static_cast<std::uint64_t>(std::ceil(tail)));
                        };
                        if(!options.timestamp) {
                            options.modem.stream_phase_samples=0;
                            wave.modem.stream_phase_samples=0;
                            std::uint64_t minimum=0;
                            if(!key.empty() && !force) {
                                std::lock_guard lock(mutex);
                                const auto previous=transmit_epochs.find(key);
                                if(previous!=transmit_epochs.end()) {
                                    if(previous->second==std::numeric_limits<std::uint64_t>::max())
                                        throw Error("transmit epoch exhausted");
                                    minimum=previous->second+1;
                                }
                            }
                            // Choose the keyed source once, with room to play
                            // the helper before it. The payload origin is never
                            // advanced or re-keyed by the output wrapper.
                            const auto helper_reservation=wave.hardware_helper_enabled?
                                2.4+1./options.modem.sample_rate:0.;
                            auto scheduled=datapump::detail::schedule_transmission(options.modem,make,
                                [this]{return current_epoch();},wave.stop,minimum,audio::minimum_lead_seconds()+helper_reservation);
                            wave.transmitter=std::move(scheduled.transmitter);
                            prepare_helper_envelope();
                            if(!key.empty())wave.protected_epoch=scheduled.epoch;
                            audio::schedule_output(scheduled.playback_epoch-
                                static_cast<double>(wave.hardware_helper_leading_samples)/wave.modem.sample_rate,wave.stop);
                        } else {
                            wave.transmitter=make(options.timestamp,options.modem.stream_phase_samples);
                            if(!key.empty())wave.protected_epoch=options.timestamp;
                            prepare_helper_envelope();
                            if(wave.hardware_helper_enabled) {
                                const auto physical=wave.transmitter->physical_output_origin();
                                const auto playback=static_cast<double>(physical/wave.modem.sample_rate)+
                                    static_cast<double>(physical%wave.modem.sample_rate)/wave.modem.sample_rate-
                                    static_cast<double>(wave.hardware_helper_leading_samples)/wave.modem.sample_rate;
                                if(current_epoch()+audio::minimum_lead_seconds()<playback)
                                    audio::schedule_output(playback,wave.stop);
                                else {
                                    // An explicit private epoch is immutable.
                                    // Optional assistance yields when its lead
                                    // cannot fit; the ordinary source continues.
                                    wave.hardware_helper_enabled=false;
                                    wave.hardware_helper_leading_samples=wave.hardware_helper_trailing_samples=0;
                                }
                            }
                        }
                        wave.protected_key=key;
                        // Reservation itself happens after each successful read
                        // and before its PCM is handed back to the device.
                    };
                } else if(value.transfer.modem.symbol_burst.enabled()) {
                    prepared->prepare_simulation=[transmission=std::move(transmission),value](Prepared& wave,double now) mutable {
                        auto options=value.transfer;
                        if(wave.noise)options.modem=modem::StreamingTransmitter::prepare_noise_config(options.modem);
                        else options.modem=transfer::seeded_config(options,options.timestamp);
                        const auto make=[&](std::uint64_t epoch,std::uint32_t phase) {
                            options.timestamp=epoch;options.modem.stream_epoch=epoch;options.modem.stream_phase_samples=phase;
                            wave.modem=options.modem;
                            if(wave.noise)return std::make_unique<modem::StreamingTransmitter>(modem::Noise{},options.modem,
                                modem::StreamingTransmitter::PreparedTemporaryKeys{},value.dsp_workspace_bytes/4);
                            return wave.binary?transfer::binary_transmitter(std::get<Bytes>(transmission),options):
                                transfer::message_transmitter(std::get<Message>(transmission),options);
                        };
                        const auto link_rate=1+channel_config(value).clock_error_ppm*1e-6;
                        const auto reserve=wave.simulation_helper_enabled?(2.4+1./options.modem.sample_rate)/link_rate:0.;
                        auto scheduled=datapump::detail::schedule_transmission(options.modem,make,[now]{return now;},wave.stop,
                            options.timestamp,reserve);
                        const auto helper_lead=wave.simulation_helper_enabled?
                            detail::helper_leading_seconds(scheduled.transmitter->physical_output_origin(),options.modem.sample_rate)/link_rate:0.L;
                        const auto leading=std::ceil((static_cast<long double>(scheduled.playback_epoch)-now-helper_lead)*options.modem.sample_rate);
                        if(!std::isfinite(leading) || leading<0 || leading>=std::ldexp(1.L,64))
                            throw Error("simulation calendar wait exceeds the sample range");
                        wave.simulation_leading_samples=static_cast<std::uint64_t>(leading);
                        wave.transmitter=std::move(scheduled.transmitter);
                    };
                } else if(media_clock) {
                    prepared->prepare_simulation=[transmission=std::move(transmission),value](Prepared& wave,double now) mutable {
                        if(wave.stop.stop_requested())throw Error("transmission cancelled");
                        auto options=value.transfer;
                        if(wave.simulation_helper_enabled) {
                            if(wave.noise)options.modem=modem::StreamingTransmitter::prepare_noise_config(options.modem);
                            else options.modem=transfer::seeded_config(options,options.timestamp);
                            const auto make=[&](std::uint64_t epoch,std::uint32_t phase) {
                                options.timestamp=epoch;options.modem.stream_epoch=epoch;options.modem.stream_phase_samples=phase;
                                wave.modem=options.modem;
                                if(wave.noise)return std::make_unique<modem::StreamingTransmitter>(modem::Noise{},options.modem,
                                    modem::StreamingTransmitter::PreparedTemporaryKeys{},value.dsp_workspace_bytes/4);
                                return wave.binary?transfer::binary_transmitter(std::get<Bytes>(transmission),options):
                                    transfer::message_transmitter(std::get<Message>(transmission),options);
                            };
                            const auto link_rate=1+channel_config(value).clock_error_ppm*1e-6;
                            auto scheduled=datapump::detail::schedule_transmission(options.modem,make,[now]{return now;},wave.stop,
                                0,(2.4+1./options.modem.sample_rate)/link_rate);
                            const auto lead=detail::helper_leading_seconds(scheduled.transmitter->physical_output_origin(),options.modem.sample_rate)/link_rate;
                            // Ordinary free-running simulation dates source
                            // zero at its epoch; receiver admission includes
                            // the settling prefix. Preserve that convention
                            // while reserving the helper before source zero.
                            const auto prefix=(static_cast<long double>(modem::training_sample_count(options.modem))+
                                modem::pattern_pulse_padding_samples(options.modem))/options.modem.sample_rate;
                            const auto waiting=std::ceil((static_cast<long double>(scheduled.playback_epoch)+prefix-now-lead)*options.modem.sample_rate);
                            if(!std::isfinite(waiting)||waiting<0||waiting>=std::ldexp(1.L,64))
                                throw Error("Helper Carrier simulation lead exceeds the source clock");
                            wave.simulation_leading_samples=static_cast<std::uint64_t>(waiting);
                            detail::simulation_checkpoint(detail::SimulationPhase::transmit_epoch,0,0,0,
                                static_cast<double>(scheduled.epoch));
                            wave.transmitter=std::move(scheduled.transmitter);
                            return;
                        }
                        options.timestamp=static_cast<std::uint64_t>(detail::simulation_media_epoch(now,0,options.modem.sample_rate));
                        detail::simulation_checkpoint(detail::SimulationPhase::transmit_epoch,0,0,0,now);
                        std::unique_ptr<modem::StreamingTransmitter> transmitter;
                        if(wave.noise) {
                            auto config=options.modem;config.stream_epoch=options.timestamp;
                            transmitter=std::make_unique<modem::StreamingTransmitter>(modem::Noise{},config,value.dsp_workspace_bytes/4);
                        } else if(wave.binary)
                            transmitter=transfer::binary_transmitter(std::get<Bytes>(transmission),options);
                        else transmitter=transfer::message_transmitter(std::get<Message>(transmission),options);
                        if(wave.stop.stop_requested())throw Error("transmission cancelled");
                        wave.transmitter=std::move(transmitter);
                    };
                } else if(noise) {
                    auto config=value.transfer.modem;config.stream_epoch=value.transfer.timestamp;
                    prepared->transmitter=std::make_unique<modem::StreamingTransmitter>(modem::Noise{},config,value.dsp_workspace_bytes/4);
                } else if (prepared->binary) {
                    prepared->transmitter = transfer::binary_transmitter(std::get<Bytes>(transmission),value.transfer);
                } else {
                    if (token.stop_requested()) continue;
                    prepared->transmitter = transfer::message_transmitter(std::get<Message>(transmission), value.transfer);
                }
                if (value.simulation && !noise) {
                    const auto reserved = replay_workspace(value);
                    if (reserved <= replay_result_workspace) throw Error("DSP workspace cannot hold simulation results");
                    const auto budget = reserved - replay_result_workspace;
                    // Keep a complete measured constellation in each frame;
                    // reduce replay cadence when its rate-sized points need
                    // more room, instead of clipping it to the old PCM tail.
                    prepared->point_limit = std::max<std::size_t>(32, detail::SignalWindow::constellation_capacity(value.transfer.modem));
                    prepared->replay_count = std::min(replay_frames,
                        budget / (replay_frame_base + prepared->point_limit * sizeof(std::complex<float>)));
                    if (prepared->replay_count < 2) throw Error("DSP workspace cannot hold a simulation replay");
                    prepared->replay.reserve(prepared->replay_count);
                    // Move the old per-frame pending allowances into one
                    // shared measured pool; leave frame cadence/reservation
                    // arithmetic unchanged, including small-workspace layouts.
                    const auto frame_bytes=prepared->replay_count*(replay_frame_base+
                        prepared->point_limit*sizeof(std::complex<float>)-detail::replay_pending_workspace);
                    prepared->receives=std::make_shared<detail::ReplayStaging>(reserved-frame_bytes,
                        prepared->replay_count,sizeof(RecoveryTask)+4096);
                }
                std::lock_guard lock(mutex);
                if (!current.running || generation != version || tx_serial != serial || token.stop_requested()) continue;
                staged_receives=prepared->receives;
                ready = std::move(prepared); if (!value.simulation && !value.full_duplex) capture_stop.request_stop(); changed.notify_all();
            } catch (const std::exception& exception) {
                std::lock_guard lock(mutex);
                if (generation != version || tx_serial != serial) continue;
                finish_simulation_elapsed();
                tx_busy = false; current.transmitting = !queued.empty(); current.transmission_finished = queued.empty();
                current.transmitting_noise = false;
                current.status = idle_status();
                if (!token.stop_requested()) {transmit_error=exception.what();current.error=transmit_error;}
                changed.notify_all();
            }
        }
    }
    void decode_loop(std::stop_token stop) {
        DATAPUMP_MUTEX_CONTEXT(mutex_context,decoder);
        detail::configure_decoder_scheduling();
        std::optional<Bank> bank; std::uint64_t local_generation = 0, local_revision = 0;
        const auto reset_bank=[&](const char* reason) {
            if(bank){trace_receiver_acquisition_work(*bank,true,reason);trace_exact_source_pool(*bank,reason,true);trace_point_pool(*bank,reason,true);}
            bank.reset();
        };
        struct FinalSourceSnapshot {
            std::optional<Bank>& bank;
            ~FinalSourceSnapshot(){try{if(bank){trace_receiver_acquisition_work(*bank,true,"decoder_join");trace_exact_source_pool(*bank,"decoder_join",true);trace_point_pool(*bank,"decoder_join",true);}}catch(...) {}}
        } final_source_snapshot{bank};
        while (!stop.stop_requested()) {
            Settings value; AudioBlock block{}; std::uint64_t version; std::stop_token token;bool pending_only=false;
            {
                std::unique_lock lock(mutex);
                const auto ready=[this,&local_generation,&local_revision] {
                    return generation!=local_generation || receive_revision!=local_revision ||
                        (current.running && !settings.simulation && !input.empty());
                };
                const auto history_pending=[&] {
                    return bank && std::any_of(bank->receivers().begin(),bank->receivers().end(),[](const auto& receiver) {
                        return receiver.modem->deferred_pattern_history() &&
                            receiver.modem->diagnostic_history_generation()>receiver.history_poll.queried_generation();
                    });
                };
                if(bank && (delayed_processing_pending || history_pending()))
                    changed.wait_for(lock,stop,std::chrono::milliseconds(50),ready);
                else changed.wait(lock,stop,ready);
                if (stop.stop_requested()) break;
                if (local_generation != generation) {
                    reset_bank("generation_reset"); local_generation = generation; local_revision = receive_revision;
                    decoding_bytes = decoding_remaining_samples = 0; decoding_remaining_physical_samples=0; decoding_queued_at.reset(); decoder_generation = generation; changed.notify_all();
                }
                if(local_revision!=receive_revision) {
                    reset_bank("revision_reset");local_revision=receive_revision;receiver_bytes=0;
                }
                if(!current.running || settings.simulation)continue;
                if(input.empty()) {
                    if(!bank || (!delayed_processing_pending && !history_pending()))continue;
                    if(!delayed_processing_pending && Clock::now()<bank->next_deferred_history_query)continue;
                    pending_only=true;
                }
                value=settings;version=generation;token=decode_stop.get_token();
                if(!pending_only){block=std::move(input.front());input.pop_front();input_bytes-=block.samples.size()*sizeof(float);
                    helper_input_bytes-=helper_reference_bytes(block.helper.get());}
                else {block.revision=receive_revision;block.sample_origin=bank->samples;}
                decoding_helper_bytes=helper_reference_bytes(block.helper.get());
                decoding_bytes = block.samples.size() * sizeof(float)+decoding_helper_bytes;
                decoding_queued_at=pending_only?delayed_processing_queued_at:std::optional<Clock::time_point>{block.queued_at};
                decoding_remaining_samples=block.samples.size();
                decoding_remaining_physical_samples=pending_only?
                    bank->samples-std::min(bank->samples,current.processed_physical_samples):
                    block.skipped_samples?block.skipped_samples:block.samples.size();
                current.buffered_samples = input_bytes / sizeof(float);
                if (local_generation != version || local_revision != block.revision) {
                    local_generation = version; local_revision = block.revision; reset_bank("block_revision_reset");
                }
            }
            try {
                if (!bank) {
                    Bank initial;initial.observed_epoch=block.observed_epoch;initial.timing=block.timing;
                    initial.samples=initial.observed_sample=block.sample_origin;initial.revision=block.revision;
                    bank=make_bank(value,std::move(initial));
                } else if(!pending_only) {
                    bank->observed_epoch=block.observed_epoch;bank->observed_sample=bank->samples;bank->timing=block.timing;
                }
                if(bank->samples!=block.sample_origin)
                    throw Error("scheduled capture sample history is not contiguous");
                bank->input_queued_at=pending_only?std::nullopt:std::optional<Clock::time_point>{block.queued_at};
                if(pending_only) {
                    // No new capture, epoch or absence is supplied. The callback
                    // only processes existing delayed input; normal phase0/1
                    // arbitration immediately publishes any resulting decisions.
                    feed_bank(*bank,value,version,token,{},[](auto&,auto*){},nullptr,false);
                } else {
                    if(block.skipped_samples)feed_unobserved(*bank,block.skipped_samples,value,version,token);
                    else feed_samples(*bank,block.samples,value,version,token,nullptr,{},
                                      block.helper?std::optional<helper::Reference>{*block.helper}:std::nullopt);
                    publish_capture_future(*bank,value,version,token);
                }
                if(!value.transfer.modem.symbol_burst.enabled())publish_capture_quiet(*bank,value,version,token);
            } catch (const std::exception& exception) {
                std::lock_guard lock(mutex);
                if (current.running && generation == version && !token.stop_requested()) {
                    current.error = exception.what();
                    current.receiver_health.receiver_reset=true;
                    // A popped failed block is no longer queued and was not
                    // committed as scored. Invalidate the physical input domain
                    // for every decoder, not only scheduled receivers; otherwise
                    // ingress minus frontier reports permanently phantom work.
                    count_dropped(current.receiver_health.dropped_samples,input_bytes/sizeof(float));
                    count_dropped(current.receiver_health.dropped_samples,delayed_processing_pcm_samples);
                    count_dropped(current.receiver_health.dropped_samples,decoding_remaining_samples);
                    reset_input_locked();
                    current.clock_window_modeled=false;current.receiver_timing.reset();
                    current.receiver_instances=current.peak_receiver_instances=0;
                    current.receiver_admissions=current.receiver_retirements=0;
                }
                reset_bank("receiver_exception");
            }
            {
                std::lock_guard lock(mutex);
                decoding_bytes = decoding_remaining_samples = 0;decoding_remaining_physical_samples=0;decoding_queued_at.reset();
            }
        }
    }
};

Session::Session(EpochClock epoch_clock, ReplayClock replay_clock)
    : impl_(std::make_unique<Impl>(std::move(epoch_clock), std::move(replay_clock))) {}
Session::~Session() = default;
void validate_settings(const Settings& settings) { (void)normalized(settings); }
void Session::start(const Settings& settings) { impl_->configure(settings); }
void Session::configure(const Settings& settings) { impl_->configure(settings); }
void Session::set_mono(bool mono) {
    std::lock_guard lock(impl_->mutex);
    impl_->settings.mono = mono;
    impl_->settings.channel_mode = audio::output_channels(mono);
}
void Session::set_channel_mode(audio::ChannelMode channels) {
    std::lock_guard lock(impl_->mutex);
    impl_->settings.channel_mode = channels;
    impl_->settings.mono = channels != audio::ChannelMode::stereo;
}
void Session::set_transmit_gain(double gain) {
    if(!std::isfinite(gain)||gain<0.0001||gain>1.75)throw Error("Transmit volume must be between 0.01% and 175%");
    std::lock_guard lock(impl_->mutex);
    impl_->settings.transmit_gain=gain;
}
void Session::set_helper_callsign(std::string callsign) {
    std::lock_guard lock(impl_->mutex);
    if(impl_->settings.transfer.key)return;
    auto options=impl_->settings.helper;options.callsign=std::move(callsign);helper::validate(options);
    impl_->settings.helper=std::move(options);
}
bool Session::try_suspend_capture() {
    std::lock_guard lock(impl_->mutex);
    if(impl_->settings.simulation)return true;
    if(!impl_->capture_suspended) {
        if(impl_->tx_busy||impl_->current.transmitting||!impl_->queued.empty()||impl_->ready||
           impl_->receive_pending||impl_->decoding_bytes||impl_->delayed_processing_pending||!impl_->input.empty())return false;
        impl_->capture_suspended=true;
        impl_->capture_stop.request_stop();
        impl_->current.status="Regular audio paused while Fast mode owns the device";
        ++impl_->current.sequence;impl_->changed.notify_all();
    }
    return !impl_->source_active && !impl_->capture_active;
}
void Session::resume_capture() {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->capture_suspended)return;
    impl_->capture_suspended=false;
    // No admitted reception was suspended. Discontinuous idle samples must not
    // be concatenated into a hypothetical symbol on the next device opening.
    impl_->reset_input_locked();
    impl_->current.status=impl_->idle_status();++impl_->current.sequence;
    impl_->changed.notify_all();
}
void Session::clear_received() {
    std::lock_guard lock(impl_->mutex);
    ++impl_->receive_clear_generation;++impl_->recovery_clear_generation;
    impl_->reset_input_locked();
    impl_->invalidate_recoveries();
    impl_->current.signals.clear();impl_->current.received.clear();
    impl_->received_bytes=0;
    // Retain TX/replay plots and progress, but discard every staged RX owner.
    if(impl_->staged_receives)impl_->staged_receives->clear();
    if(impl_->ready)impl_->ready->receive_clear_generation=impl_->receive_clear_generation;
    impl_->current.receiver_health={};impl_->current.receiver_behind=false;
    impl_->current.receiver_backlog_seconds=impl_->current.receiver_oldest_input_seconds=0;
    impl_->current.pattern_scores.clear();impl_->current.pattern_score_observations.clear();
    impl_->current.error.clear();
    if(!impl_->current.transmitting)impl_->current.status=impl_->idle_status();
    ++impl_->current.sequence;impl_->changed.notify_all();
}
bool Session::resume_recovery(std::uint64_t signal_id) {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->current.running)return false;
    const auto found=std::find_if(impl_->recoveries.begin(),impl_->recoveries.end(),[&](const auto& task) {
        return task->event.id==signal_id && !task->obsolete && task->generation==impl_->generation;
    });
    if(found==impl_->recoveries.end() || *found==impl_->active_recovery)return false;
    const auto& task=*found;
    const auto state=task->event.recovery_progress.state;
    if(state!=transfer::RecoveryState::incomplete && state!=transfer::RecoveryState::cancelled)return false;
    task->stop=std::stop_source{};task->started=false;
    task->event.recovery_progress.state=transfer::RecoveryState::ready;
    auto event=task->event;event.sequence=impl_->next_event++;
    impl_->append_signal(std::move(event));impl_->changed.notify_all();return true;
}
void Session::cancel_recovery(std::uint64_t signal_id) {
    std::lock_guard lock(impl_->mutex);
    for(const auto& task:impl_->recoveries) {
        if(task->event.id!=signal_id || task->obsolete)continue;
        task->stop.request_stop();
        if(task!=impl_->active_recovery) {
            task->started=true;task->event.recovery_progress.state=transfer::RecoveryState::cancelled;
            auto event=task->event;event.sequence=impl_->next_event++;
            impl_->append_signal(std::move(event));
        }
    }
}
void Session::clear_recoveries() {
    std::lock_guard lock(impl_->mutex);
    ++impl_->recovery_clear_generation;
    impl_->discard_staged_recoveries();
    // A finished coordinator can already have released its job while its
    // publication is still waiting for the next UI poll.
    std::erase_if(impl_->current.signals,[](const auto& event) {
        return event.recovery_progress.state!=transfer::RecoveryState::none;
    });
    std::erase_if(impl_->current.received,[&](const auto& received) {
        if(received.recovery_progress.state==transfer::RecoveryState::none)return false;
        impl_->received_bytes-=received.content.message.data.capacity();return true;
    });
    for(const auto& task:impl_->recoveries)
        std::erase_if(impl_->current.signals,[&](const auto& event) {
            return event.id==task->event.id && event.revision<=task->event.revision;
        });
    impl_->invalidate_recoveries();
}
void Session::transmit(const Message& message, bool force,std::shared_ptr<const modem::FhssPlan> hopping) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->current.running) throw Error("continuous receiver is not running");
    if (message.data.size() > impl_->settings.content_limit) throw Error("message exceeds content capacity");
    impl_->enqueue(message,force,std::move(hopping));
}
void Session::transmit_bits(std::span<const std::uint8_t> bits, bool force,std::shared_ptr<const modem::FhssPlan> hopping) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->current.running) throw Error("continuous receiver is not running");
    if (bits.empty()) throw Error("enter at least one binary bit");
    if (bits.size() > impl_->settings.content_limit) throw Error("binary input exceeds content capacity");
    if (std::any_of(bits.begin(), bits.end(), [](auto bit) { return bit > 1; }))
        throw Error("binary input must contain only 0 and 1 bits");
    impl_->enqueue(Bytes(bits.begin(), bits.end()),force,std::move(hopping));
}
void Session::transmit_noise(std::shared_ptr<const modem::FhssPlan> hopping,bool long_profile) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->current.running) throw Error("continuous receiver is not running");
    impl_->enqueue(modem::Noise{},false,std::move(hopping),long_profile);
}
void Session::transmit_helper(std::shared_ptr<const modem::FhssPlan> hopping) {
    std::lock_guard lock(impl_->mutex);
    if(!impl_->current.running)throw Error("continuous receiver is not running");
    if(impl_->settings.helper.power_w<=0)throw Error("Set Helper Carrier power before transmitting a helper-only signal");
    impl_->enqueue(modem::Noise{},false,std::move(hopping),false,true);
}
void Session::finish_transmit() {
    {
        std::lock_guard lock(impl_->mutex);
        if(impl_->source_pcm_started && impl_->current.transmitting && !impl_->current.simulation_receiving_tail && !impl_->finish_requested) {
            impl_->finish_requested=true;impl_->current.transmission_stopping=true;
            impl_->queued.clear();
            impl_->current.status="Finishing echo protection and pulse tails; cancel again to abort immediately";
            impl_->changed.notify_all();return;
        }
    }
    // Preparation/replay and a repeated request retain responsive hard abort.
    cancel_transmit();
}
void Session::cancel_transmit() {
    std::lock_guard lock(impl_->mutex);
    impl_->advance_replay(impl_->replay_clock());
    impl_->finish_simulation_elapsed();
    impl_->tx_stop.request_stop(); ++impl_->tx_serial; impl_->queued.clear(); impl_->ready.reset(); impl_->tx_busy = false;
    impl_->current.transmitting = impl_->current.transmitting_noise = false;
    impl_->current.transmission_finished = true; impl_->current.transmission_cancelled = true;
    impl_->current.transmission_stopping=false;impl_->finish_requested=false;impl_->source_pcm_started=false;
    impl_->clear_replay(); impl_->pending_points = {}; impl_->replay_omitted = 0;
    impl_->current.constellation.clear(); impl_->current.constellation_source = ConstellationSource::input;
    impl_->current.constellation_dropped = 0;
    impl_->current.status = impl_->idle_status(); impl_->changed.notify_all();
}
Snapshot Session::snapshot() {
    // Preparing a new by-value destination can enter the allocator. Keep that
    // work outside the capture mutex, then copy one coherent locked revision.
    Snapshot result;
    for(;;) {
        detail::SnapshotCopyCapacity required;
        {
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
            snapshot_probe::Observation snapshot_observation;
#endif
            DATAPUMP_MUTEX_CONTEXT(mutex_context,snapshot);
            std::lock_guard lock(impl_->mutex);
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
            snapshot_observation.acquired(&impl_->mutex);
            snapshot_observation.census(impl_->recoveries.size(),bool(impl_->active_recovery),impl_->replay.size(),
                !impl_->selected_transmit_key.empty(),impl_->input.size(),impl_->current.waveform.size(),
                impl_->current.spectrum_db.size(),impl_->current.constellation.size(),impl_->capture_samples,
                impl_->current.processed_physical_samples);
#endif
            impl_->update_simulation_elapsed();
            const auto now = impl_->replay_clock();
            impl_->advance_replay(now);
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
            snapshot_observation.mark(snapshot_probe::Phase::clock);
#endif
            impl_->publish_recovery_progress();
            impl_->current.recovery_working_bytes=impl_->recovery_bytes();
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
            snapshot_observation.mark(snapshot_probe::Phase::recovery);
#endif
            impl_->current.transmit_key_lock_seconds=impl_->key_lock_seconds(impl_->settings,
                impl_->settings.transfer.modem,impl_->selected_transmit_key);
            impl_->current.long_transmit_key_lock_seconds=impl_->key_lock_seconds(impl_->settings,
                impl_->settings.long_message_modem.value_or(impl_->settings.transfer.modem),impl_->selected_transmit_key);
            impl_->current.transmit_separation_seconds=impl_->settings.simulation?0:
                std::max(0.,std::chrono::duration<double>(impl_->next_hardware_send-Clock::now()).count());
            // Include constructor/scoring work after it has left the queue. No loss is
            // implied: a large configured buffer may retain minutes of delayed PCM.
            double pending_seconds=0,oldest_seconds=0;
            if(impl_->current.running && !impl_->settings.simulation && impl_->decoder_generation==impl_->generation) {
                pending_seconds=(static_cast<double>(impl_->input_bytes)/sizeof(float)+impl_->decoding_remaining_samples+
                    impl_->delayed_processing_pcm_samples)/
                    impl_->settings.transfer.modem.sample_rate;
                auto oldest=impl_->decoding_remaining_samples?impl_->decoding_queued_at:std::nullopt;
                if(impl_->delayed_processing_queued_at && (!oldest || *impl_->delayed_processing_queued_at<*oldest))
                    oldest=impl_->delayed_processing_queued_at;
                const auto pending=std::find_if(impl_->input.begin(),impl_->input.end(),[](const auto& block){return !block.samples.empty();});
                if(pending!=impl_->input.end() && (!oldest || pending->queued_at<*oldest))oldest=pending->queued_at;
                if(oldest)oldest_seconds=std::max(0.,std::chrono::duration<double>(Clock::now()-*oldest).count());
            }
            impl_->current.receiver_backlog_seconds=pending_seconds;
            impl_->current.receiver_oldest_input_seconds=oldest_seconds;
            const auto delay=std::max(pending_seconds,oldest_seconds);
            // A transient warning, with hysteresis; only observed loss flags latch.
            impl_->current.receiver_behind=delay>1. || (impl_->current.receiver_behind && delay>.25);
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
            snapshot_observation.mark(snapshot_probe::Phase::backlog);
#endif
            required=detail::snapshot_copy_requirement(impl_->current,impl_->transmit_error);
            if(detail::snapshot_copy_fits(result,required)) {
                auto signals = std::move(impl_->current.signals); auto received = std::move(impl_->current.received);
                impl_->current.signals.clear(); impl_->current.received.clear(); impl_->received_bytes = 0;
                if (!impl_->pending_points.points.empty() || impl_->pending_points.dropped) {
                    // Publication owns the bounded display history. The fresh-point batch
                    // only accounts for observations omitted before the UI consumed them.
                    impl_->current.constellation_dropped = impl_->pending_points.dropped;
                    if (impl_->current.constellation_source != impl_->pending_source)
                        count_dropped(impl_->current.constellation_dropped, impl_->pending_points.points.size());
                    impl_->pending_points = {}; ++impl_->current.sequence;
                }
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
                // Preserve the original copy interval after final-fit drainage.
                snapshot_observation.mark(snapshot_probe::Phase::backlog);
#endif
                result = impl_->current; result.signals = std::move(signals); result.received = std::move(received);
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
                snapshot_observation.mark(snapshot_probe::Phase::copy);
#endif
                if(result.error.empty())result.error=result.receiver_error;
                if(!impl_->transmit_error.empty()) {
                    result.transmission_failed=true;
                    result.error=impl_->transmit_error;
                    if(!result.transmitting)result.status="Transmission failed";
                }
                result.decoding_samples=impl_->decoder_generation==impl_->generation?impl_->decoding_remaining_samples:0;
                result.receiver_input_revision=impl_->receive_revision;
                result.decoding_physical_samples=impl_->decoder_generation==impl_->generation?impl_->decoding_remaining_physical_samples:0;
                // The ingress counter, committed frontier and in-flight remainder are all
                // protected by this mutex and share one revision. Do not infer progress by
                // subtracting only stored PCM: coordinate-only null work is queued too.
                const auto pending_physical=impl_->capture_samples-std::min(impl_->capture_samples,result.processed_physical_samples);
                result.queued_physical_samples=pending_physical-std::min(pending_physical,result.decoding_physical_samples);
                impl_->replay_snapshot(result, now);
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
                snapshot_observation.mark(snapshot_probe::Phase::replay);
#endif
                count_dropped(result.constellation_dropped, impl_->replay_omitted); impl_->replay_omitted = 0;
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
                snapshot_observation.before_unlock();
#endif
                return result;
            }
#if defined(DATAPUMP_LIVE_SNAPSHOT_PROBE)
            snapshot_observation.before_unlock();
#endif
        }
        // A retry consumes no events, fresh-point accounting or replay omissions.
        // Source values may change while unlocked; retain only numeric sizes.
        detail::prepare_snapshot_copy(result,required);
    }
}
void Session::stop() { impl_->halt(); }
}
