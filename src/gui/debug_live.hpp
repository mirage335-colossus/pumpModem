#pragma once
#include "state.hpp"
#include "datapump/live.hpp"
#include "datapump/runtime.hpp"
#include <ostream>
#include <syncstream>
#include <algorithm>
#include <map>
#include <tuple>
#include <set>

namespace datapump::gui {
// Opt-in physical-audio diagnostic, not a simulation or receiver qualification.
// The caller drives ordinary Controller actions and passes its normal snapshots.
class DebugLive {
public:
    enum class Action { wait, transmit, close };
    explicit DebugLive(double timeout,std::ostream& log,bool require_utc=false,std::string bits="1"):
        timeout_(timeout),log_(log),expected_(std::move(bits)),require_utc_(require_utc) {
        if(expected_.empty() || expected_.size()>16 || expected_.find_first_not_of("01")!=std::string::npos)
            throw Error("Debug bits must contain 1..16 binary digits");
    }
    int result() const {return fallback_received_?2:passed_?0:1;}
    Action step(double elapsed,const live::Snapshot& s,const Signals& signals,bool ready,bool keyed,const std::string& status={}) {
        if(done_)return Action::close;
        if(status.substr(0,2048)!=last_status_) {
            last_status_=status.substr(0,2048);
            std::osyncstream(log_)<<"{\"event\":\"status\",\"seconds\":"<<elapsed
                <<",\"text\":\""<<json_escape(last_status_)<<"\"}\n"<<std::flush;
        }
        bool complete=false,unexpected=false;
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
                std::osyncstream(log_)<<"{\"event\":\"stream\",\"seconds\":"<<elapsed<<",\"id\":"<<line.id
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
            std::osyncstream(log_)<<"{\"event\":\"live\",\"seconds\":"<<elapsed<<",\"tx\":"<<s.transmitting
                <<",\"tx_id\":"<<s.transmission_id<<",\"tx_seconds\":"<<s.transmission_seconds
                <<",\"tx_fraction\":"<<s.transmission_fraction<<",\"tx_finished\":"<<s.transmission_finished
                <<",\"samples\":"<<s.samples_received<<",\"hardware_rate\":"<<s.hardware_sample_rate
                <<",\"backlog\":"<<s.receiver_backlog_seconds<<",\"oldest\":"<<s.receiver_oldest_input_seconds
                <<",\"banks\":"<<s.receiver_instances<<",\"admissions\":"<<s.receiver_admissions
                <<",\"rx_timed\":"<<s.clock_window_modeled<<",\"fallback\":"<<s.clock_timing_fallback
                <<",\"tx_timed\":"<<s.transmit_clock_following<<",\"streams\":"<<signals.lines().size()
                <<",\"complete_match\":"<<complete<<",\"unexpected_bits\":"<<unexpected
                <<",\"unexpected_seen\":"<<unexpected_seen_
                <<",\"timing\":\""<<json_escape(s.clock_timing_status)<<"\",\"error\":\""<<json_escape(s.error)<<"\"}\n"<<std::flush;
        }
        if(s.simulation)return finish(elapsed,"refused simulation",false);
        if(!s.error.empty() || s.transmission_failed || s.transmission_stopping)
            return finish(elapsed,s.error.empty()?"transmit failure/protected stop":s.error,false);
        if(s.receiver_health.failed())return finish(elapsed,"receiver lost input or required coverage",false);
        if(elapsed>=timeout_)return finish(elapsed,"deadline: incomplete hardware duplex reception",false);
        if(!requested_ && ready && keyed && s.running && s.samples_received) {
            for(const auto& line:signals.lines())preexisting_.insert(line.id);
            requested_=true;initial_id_=s.transmission_id;return Action::transmit;
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
                fallback_received_=require_utc_&&!utc_seen_&&capture_advanced_&&!unexpected_seen_&&!s.transmission_cancelled;
                return finish(elapsed,require_utc_&&!utc_seen_?
                "hardware duplex received; requested UTC playback untested after fallback":"hardware duplex reception",
                capture_advanced_&&!unexpected_seen_&&!s.transmission_cancelled&&(!require_utc_||utc_seen_));
            }
        }
        return Action::wait;
    }
private:
    Action finish(double elapsed,const std::string& reason,bool pass) {
        done_=true;passed_=pass;
        std::osyncstream(log_)<<"{\"event\":\"result\",\"seconds\":"<<elapsed<<",\"passed\":"<<pass
            <<",\"reason\":\""<<json_escape(reason)<<"\",\"capture_during_tx\":"<<capture_advanced_<<"}\n"<<std::flush;
        return Action::close;
    }
    std::map<std::uint64_t,std::tuple<std::uint64_t,std::size_t,bool,std::size_t>> rows_;
    std::set<std::uint64_t> preexisting_; // Bounded snapshot at the single TX request.
    double timeout_,next_log_=0;
    std::ostream& log_;
    std::string last_error_,last_status_,expected_;
    bool require_utc_=false,utc_seen_=false,fallback_received_=false;
    bool requested_=false,started_=false,capture_advanced_=false,done_=false,passed_=false,unexpected_seen_=false;
    std::optional<double> settled_;
    std::uint64_t initial_id_=0,start_samples_=0;
};
}
