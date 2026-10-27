#pragma once
#include "state.hpp"
#include "datapump/live.hpp"
#include "datapump/runtime.hpp"
#include <ostream>
#if __has_include(<syncstream>)
#include <syncstream>
#endif
#include <mutex>
#include <sstream>
#include <algorithm>
#include <map>
#include <tuple>
#include <set>
#include <string_view>

namespace datapump::gui {
namespace debug_live_detail {
// The repeated physical diagnostic needs dated reference/re-entry evidence;
// per-row acquisition records can exhaust the trace before its next cycle.
inline bool reentry_trace_event(const char* event) noexcept {
    const std::string_view name=event?event:"";
    return name=="trace_begin" || name=="gui_start" || name=="receiver_helper_raw_reference" ||
        name=="receiver_helper_clock_refresh" || name=="receiver_helper_reference_reentry";
}
#if defined(__cpp_lib_syncbuf) && __cpp_lib_syncbuf>=201803L
using LogLine=std::osyncstream;
#else
// Some pinned browser libraries provide the header without syncbuf. Buffer the
// whole record, then serialize emission and flush the actual destination.
inline std::mutex output_mutex;
class LogLine:public std::ostringstream {
    std::ostream& destination_;
public:
    explicit LogLine(std::ostream& destination):destination_(destination){}
    ~LogLine() {
        try {
            const auto text=str();
            std::lock_guard lock(output_mutex);
            destination_.write(text.data(),static_cast<std::streamsize>(text.size()));
            destination_.flush();
        }catch(...){} // Like syncbuf destruction, diagnostic emission cannot throw.
    }
};
#endif
}
// Opt-in physical-audio diagnostic, not a simulation or receiver qualification.
// The caller drives ordinary Controller actions and passes its normal snapshots.
class DebugLive {
public:
    enum class Action { wait, transmit, close };
    static constexpr std::size_t maximum_repeat_seen_ids=1024;
    explicit DebugLive(double timeout,std::ostream& log,bool require_utc=false,std::string bits="1",unsigned repeat=1):
        timeout_(timeout),log_(log),expected_(std::move(bits)),require_utc_(require_utc),repeat_(repeat) {
        if(expected_.empty() || expected_.size()>16 || expected_.find_first_not_of("01")!=std::string::npos)
            throw Error("Debug bits must contain 1..16 binary digits");
        if(repeat_<1 || repeat_>3)throw Error("Debug repeat must be 1..3");
    }
    int result() const {return fallback_received_?2:passed_?0:1;}
    Action step(double elapsed,const live::Snapshot& s,const Signals& signals,bool ready,bool keyed,const std::string& status={}) {
        if(done_)return Action::close;
        if(status.substr(0,2048)!=last_status_) {
            last_status_=status.substr(0,2048);
            debug_live_detail::LogLine(log_)<<"{\"event\":\"status\",\"seconds\":"<<elapsed
                <<",\"text\":\""<<json_escape(last_status_)<<"\"}\n"<<std::flush;
        }
        bool complete=false,unexpected=false,identity_overflow=false;
        if(repeat_>1) {
            const auto observe=[&](std::uint64_t id) {
                if(seen_ids_.contains(id))return;
                if(seen_ids_.size()==maximum_repeat_seen_ids)identity_overflow=true;
                else seen_ids_.insert(id);
            };
            // Delivered updates may already have disappeared from bounded
            // GUI rows. Remember partial rows and superseded aliases too.
            for(const auto& update:s.signals) {
                observe(update.id);
                for(const auto id:update.superseded_ids)observe(id);
            }
            for(const auto& line:signals.lines())observe(line.id);
        }
        // A stronger hypothesis may supersede a wrong admission within the
        // same GUI poll. Inspect delivered updates before bounded row retention
        // hides that evidence; this diagnostic does not alter receiver policy.
        for(const auto& update:s.signals) {
            const auto& bits=update.raw_bits.empty()&&update.binary?update.text:update.raw_bits;
            if(update.received_bits && (bits.size()!=update.received_bits || !expected_.starts_with(bits) ||
               update.missing_symbols || (update.complete && bits!=expected_)))unexpected=true;
        }
        for(const auto& line:signals.lines()) {
            const auto bits=line.raw_bits.empty()&&line.binary?line.text:line.raw_bits;
            if(line.received_bits) {
                if(requested_ && !preexisting_.contains(line.id) && line.complete && line.received_bits==expected_.size() && bits==expected_ && !line.missing_symbols)complete=true;
                if(bits.size()!=line.received_bits || !expected_.starts_with(bits) || line.missing_symbols || (line.complete && bits!=expected_))unexpected=true;
            }
            const auto identity=std::tuple{line.revision,line.received_bits,line.complete,line.missing_symbols};
            if(!rows_.contains(line.id)||rows_.at(line.id)!=identity) {
                rows_[line.id]=identity;
                debug_live_detail::LogLine(log_)<<"{\"event\":\"stream\",\"seconds\":"<<elapsed<<",\"id\":"<<line.id
                    <<",\"revision\":"<<line.revision<<",\"bits\":"<<line.received_bits
                    <<",\"complete\":"<<line.complete<<",\"missing\":"<<line.missing_symbols
                    <<",\"expected_prefix\":"<<expected_.starts_with(bits)<<"}\n"<<std::flush;
            }
        }
        // State follows the GUI's bounded retained identities, not an unbounded
        // history of incoming streams. No contents or cryptographic bytes logged.
        for(auto it=rows_.begin();it!=rows_.end();) {
            if(std::none_of(signals.lines().begin(),signals.lines().end(),[&](const auto& line){return line.id==it->first;}))it=rows_.erase(it);
            else ++it;
        }
        // A wrong admission remains evidence even after bounded GUI retention
        // evicts its row. Later correct reception must not turn that run green.
        unexpected_seen_|=unexpected;
        if(elapsed>=next_log_ || s.error!=last_error_ || s.transmission_failed || s.transmission_stopping) {
            next_log_=elapsed+1;last_error_=s.error;
            debug_live_detail::LogLine(log_)<<"{\"event\":\"live\",\"seconds\":"<<elapsed<<",\"tx\":"<<s.transmitting
                <<",\"tx_id\":"<<s.transmission_id<<",\"tx_seconds\":"<<s.transmission_seconds
                <<",\"tx_fraction\":"<<s.transmission_fraction<<",\"tx_finished\":"<<s.transmission_finished
                <<",\"samples\":"<<s.samples_received<<",\"hardware_rate\":"<<s.hardware_sample_rate
                <<",\"processed_physical_samples\":"<<s.processed_physical_samples
                <<",\"queued_physical_samples\":"<<s.queued_physical_samples
                <<",\"decoding_physical_samples\":"<<s.decoding_physical_samples
                <<",\"input_overruns\":"<<s.receiver_health.input_overruns
                <<",\"dropped_samples\":"<<s.receiver_health.dropped_samples
                <<",\"receiver_reset\":"<<s.receiver_health.receiver_reset
                <<",\"backlog\":"<<s.receiver_backlog_seconds<<",\"oldest\":"<<s.receiver_oldest_input_seconds
                <<",\"banks\":"<<s.receiver_instances<<",\"admissions\":"<<s.receiver_admissions
                <<",\"rx_timed\":"<<s.clock_window_modeled<<",\"fallback\":"<<s.clock_timing_fallback
                <<",\"tx_timed\":"<<s.transmit_clock_following<<",\"streams\":"<<signals.lines().size()
                <<",\"complete_match\":"<<complete<<",\"unexpected_bits\":"<<unexpected
                <<",\"unexpected_seen\":"<<unexpected_seen_
                <<",\"helper_status\":\""<<json_escape(s.helper_status)<<"\",\"helper_snr_db\":"
                <<(s.helper_snr_db?std::to_string(*s.helper_snr_db):"null")
                <<",\"helper_frequency_offset_hz\":"
                <<(s.helper_frequency_offset_hz?std::to_string(*s.helper_frequency_offset_hz):"null")
                <<",\"helper_frequency_assistance_active\":"<<s.helper_frequency_assistance_active
                <<",\"helper_phase_assistance_active\":"<<s.helper_phase_assistance_active
                <<",\"helper_timing_assistance_active\":"<<s.helper_timing_assistance_active
                <<",\"helper_original_search_pairs\":"<<s.helper_original_search_pairs
                <<",\"helper_selected_search_pairs\":"<<s.helper_selected_search_pairs
                <<",\"helper_conditional_jobs\":"<<s.helper_conditional_jobs
                <<",\"helper_audit_jobs\":"<<s.helper_audit_jobs
                <<",\"helper_input_view_conditional_jobs\":"<<s.helper_input_view_conditional_jobs
                <<",\"helper_input_view_count\":"<<s.helper_input_view_count
                <<",\"helper_compact_projected_samples\":"<<s.helper_compact_projected_samples
                <<",\"helper_compact_lane_samples\":"<<s.helper_compact_lane_samples
                <<",\"helper_compact_skipped_projected_samples\":"<<s.helper_compact_skipped_projected_samples
                <<",\"helper_compact_skipped_lane_samples\":"<<s.helper_compact_skipped_lane_samples
                <<",\"helper_compact_audit_projected_samples\":"<<s.helper_compact_audit_projected_samples
                <<",\"helper_compact_audit_lane_samples\":"<<s.helper_compact_audit_lane_samples
                <<",\"helper_compact_phase_projected_samples\":"<<s.helper_compact_phase_projected_samples
                <<",\"helper_compact_phase_moment_updates\":"<<s.helper_compact_phase_moment_updates
                <<",\"timing\":\""<<json_escape(s.clock_timing_status)<<"\",\"error\":\""<<json_escape(s.error)<<"\"}\n"<<std::flush;
        }
        if(s.simulation)return finish(elapsed,"refused simulation",false);
        if(!s.error.empty() || s.transmission_failed || s.transmission_stopping)
            return finish(elapsed,s.error.empty()?"transmit failure/protected stop":s.error,false);
        if(s.receiver_health.failed())return finish(elapsed,"receiver lost input or required coverage",false);
        if(identity_overflow)return finish(elapsed,"diagnostic reception identity quota exceeded",false);
        if(elapsed>=timeout_)return finish(elapsed,"deadline: incomplete hardware duplex reception",false);
        if(!requested_ && ready && keyed && s.running && s.samples_received) {
            return request(elapsed,s,signals);
        }
        if(requested_ && s.transmission_id>initial_id_ && s.transmitting) {
            if(!started_) {started_=true;start_samples_=s.samples_received;}
            capture_advanced_|=s.samples_received>start_samples_;
            utc_seen_|=s.transmit_clock_following && s.transmission_seconds>0;
        }
        // Completion comes from the unchanged physical receiver, never from
        // TX EOF or our deadline. Examine all retained rows, including extras.
        if(started_ && !s.transmitting && s.transmission_finished && complete) {
            if(!settled_)settled_=elapsed;
            if(elapsed-*settled_>=2) {
                const bool physical_ok=capture_advanced_&&!unexpected_seen_&&!s.transmission_cancelled;
                if(physical_ok && completed_+1<repeat_) {
                    // Keep the same Controller/session. Its ordinary transmit
                    // eligibility includes the existing hardware separation.
                    if(!ready || !keyed || !s.running)return Action::wait;
                    ++completed_;utc_missing_seen_|=require_utc_&&!utc_seen_;
                    requested_=started_=capture_advanced_=utc_seen_=false;settled_.reset();
                    return request(elapsed,s,signals);
                }
                if(physical_ok)++completed_;
                const bool utc_missing=require_utc_&&(!utc_seen_||utc_missing_seen_);
                fallback_received_=utc_missing&&physical_ok;
                return finish(elapsed,utc_missing?
                "hardware duplex received; requested UTC playback untested after fallback":"hardware duplex reception",
                physical_ok&&!utc_missing);
            }
        }
        return Action::wait;
    }
private:
    Action request(double elapsed,const live::Snapshot& s,const Signals& signals) {
        // Repeated runs retain every observed identity, including partial rows
        // evicted before a request. Overflow fails instead of losing evidence.
        if(repeat_>1)preexisting_=seen_ids_;
        else for(const auto& line:signals.lines())preexisting_.insert(line.id);
        requested_=true;initial_id_=s.transmission_id;
        if(repeat_>1)debug_live_detail::LogLine(log_)<<"{\"event\":\"cycle\",\"seconds\":"<<elapsed
            <<",\"cycle\":"<<completed_+1<<",\"repeat\":"<<repeat_<<"}\n"<<std::flush;
        return Action::transmit;
    }
    Action finish(double elapsed,const std::string& reason,bool pass) {
        done_=true;passed_=pass;
        debug_live_detail::LogLine(log_)<<"{\"event\":\"result\",\"seconds\":"<<elapsed<<",\"passed\":"<<pass
            <<",\"reason\":\""<<json_escape(reason)<<"\",\"capture_during_tx\":"<<capture_advanced_<<"}\n"<<std::flush;
        return Action::close;
    }
    std::map<std::uint64_t,std::tuple<std::uint64_t,std::size_t,bool,std::size_t>> rows_;
    std::set<std::uint64_t> preexisting_,seen_ids_; // Repeated runs fail at the explicit identity cap.
    double timeout_,next_log_=0;
    std::ostream& log_;
    std::string last_error_,last_status_,expected_;
    bool require_utc_=false,utc_seen_=false,fallback_received_=false;
    unsigned repeat_=1,completed_=0;
    bool utc_missing_seen_=false;
    bool requested_=false,started_=false,capture_advanced_=false,done_=false,passed_=false,unexpected_seen_=false;
    std::optional<double> settled_;
    std::uint64_t initial_id_=0,start_samples_=0;
};
}
