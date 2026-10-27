#pragma once
#include "datapump/live.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <string_view>

namespace datapump::live::detail {
// Fixed presentation state, owned only by Live's state mutex. Presence is
// independent of synchronization qualification and payload END. Publication
// never owns a payload queue slot. Missing capture never counts as silence.
class HelperPresence {
public:
    using Clock=std::chrono::steady_clock;
    struct AdvisoryTime {
        bool encrypted=false;
        std::uint64_t ticks=0;
        bool operator==(const AdvisoryTime&)const=default;
    };
    struct Observation {
        bool enabled=true;
        std::uint64_t first_sample=0,end_sample=0;
        std::uint32_t sample_rate=0;
        bool present=false;
        double frequency_hz=0,carrier_to_noise_db_hz=0;
        std::string_view callsign;
        std::uint64_t revision=0;
        // Legacy Tracker's consecutively fully scored absence; its sparse
        // publications are not missing PCM. Realtime uses the END anchor below.
        std::optional<double> captured_absent_seconds;
        std::optional<AdvisoryTime> advisory_time;
    };
    void synchronize(std::uint64_t generation,std::uint64_t clear_generation)noexcept {
        if(generation_==generation && clear_generation_==clear_generation)return;
        *this=HelperPresence{};generation_=generation;clear_generation_=clear_generation;
    }
    void interrupt()noexcept {continuous_=false;absent_first_.reset();}
    std::uint64_t pending_id()const noexcept{return active_==none?0:records_[active_].id;}
    std::size_t retained_records()const noexcept {
        return static_cast<std::size_t>(records_[0].id!=0)+static_cast<std::size_t>(records_[1].id!=0);
    }
    // Exact receipt from a successfully prepared Snapshot, not queue admission.
    // A queue-accepted helper event can still yield its slot before a poll.
    void acknowledge(std::uint64_t id)noexcept {
        for(auto& record:records_)if(record.id==id && record.complete)record={};
    }
    std::optional<SignalUpdate> retry_terminal(Clock::time_point now) {
        Record* chosen=nullptr;
        for(auto& record:records_)if(record.id && record.complete && due(record,now) &&
            (!chosen || record.id<chosen->id))chosen=&record;
        if(!chosen)return {};
        return publish(*chosen,now);
    }
    template<class AllocateId>
    std::optional<SignalUpdate> observe(const Observation& value,AllocateId&& allocate_id,
                                       Clock::time_point now=Clock::now()) {
        if(!value.enabled || !value.sample_rate || value.end_sample<=value.first_sample) {
            interrupt();return {};
        }
        const bool contiguous=continuous_ && rate_==value.sample_rate && end_==value.first_sample;
        if(!contiguous)absent_first_.reset();
        continuous_=true;rate_=value.sample_rate;end_=value.end_sample;
        if(value.present && (!std::isfinite(value.frequency_hz) ||
                             !std::isfinite(value.carrier_to_noise_db_hz))) {
            interrupt();return {};
        }
        if(value.present) {
            absent_first_.reset();
            if(active_==none) {
                std::size_t slot=none;
                for(std::size_t i=0;i<records_.size();++i)if(!records_[i].id) {slot=i;break;}
                // Two unacknowledged episodes are the finite diagnostic quota.
                // Further helper episodes wait; payload storage is never taken.
                if(slot==none)return retry_terminal(now);
                const auto id=allocate_id();if(!id)return {};
                active_=slot;records_[active_].id=id;records_[active_].changed=true;
            }
            auto& record=records_[active_];record.changed=record.changed || !record.present;
            record.present=true;record.frequency=value.frequency_hz;record.cn0=value.carrier_to_noise_db_hz;
            record.measurement_end=value.end_sample;record.measurement_rate=value.sample_rate;
            record.revision=value.revision;
            if(!record.have_measurement || record.cn0>record.peak_cn0) {
                record.peak_cn0=record.cn0;record.peak_end=value.end_sample;record.peak_rate=value.sample_rate;
            }
            record.have_measurement=true;
            if(!value.callsign.empty()) {
                std::array<char,20> next{};
                const auto n=std::min(value.callsign.size(),next.size()-1);
                for(std::size_t i=0;i<n;++i) {
                    const auto c=static_cast<unsigned char>(value.callsign[i]);next[i]=c>=32 && c<127?static_cast<char>(c):'?';
                }
                record.changed=record.changed || next!=record.callsign || record.advisory_time.has_value();
                record.callsign=next;record.advisory_time.reset();
            }else if(value.advisory_time) {
                record.changed=record.changed || record.advisory_time!=value.advisory_time || record.callsign[0];
                record.advisory_time=value.advisory_time;record.callsign={};
            }
        }else if(active_!=none) {
            if(value.captured_absent_seconds && (!std::isfinite(*value.captured_absent_seconds) ||
                                                *value.captured_absent_seconds<0)) {
                interrupt();return {};
            }
            auto& record=records_[active_];record.changed=record.changed || record.present;
            record.present=false;record.revision=value.revision;
            // First false END is conservative: it credits no unknown earlier
            // time. Only subsequent contiguous captured absence advances END.
            if(!absent_first_)absent_first_=value.end_sample;
            const bool ended=value.captured_absent_seconds?*value.captured_absent_seconds>=6:
                value.end_sample-*absent_first_>=static_cast<std::uint64_t>(value.sample_rate)*6;
            if(ended) {record.complete=true;record.changed=true;active_=none;absent_first_.reset();}
        }
        if(auto terminal=retry_terminal(now))return terminal;
        if(active_!=none && (records_[active_].changed || due(records_[active_],now)))
            return publish(records_[active_],now);
        return {};
    }
private:
    struct Record {
        std::uint64_t id=0,revision=0,measurement_end=0,peak_end=0;
        std::uint32_t measurement_rate=0,peak_rate=0;
        bool present=false,complete=false,have_measurement=false,changed=false;
        double frequency=0,cn0=0,peak_cn0=0;
        std::array<char,20> callsign{};
        std::optional<AdvisoryTime> advisory_time;
        std::optional<Clock::time_point> last_attempt;
    };
    static bool due(const Record& record,Clock::time_point now)noexcept {
        return record.changed || !record.last_attempt || now<*record.last_attempt ||
            now-*record.last_attempt>=std::chrono::seconds(1);
    }
    static SignalUpdate publish(Record& record,Clock::time_point now) {
        // Allocation failure retains the record and ID; retry attempts are also
        // throttled. A terminal record survives until an exact snapshot receipt.
        record.last_attempt=now;record.changed=false;
        SignalUpdate event;event.helper_carrier=true;event.id=record.id;event.complete=record.complete;
        event.frequency_hz=record.frequency;event.snr_db=record.cn0-10;event.revision=record.revision;
        std::ostringstream text;text.exceptions(std::ios_base::badbit|std::ios_base::failbit);
        text.imbue(std::locale::classic());
        text<<"Helper carrier "<<(record.complete?"ended after captured absence":record.present?"present":"absent; awaiting captured END");
        const auto label=record.present?"current":"last";
        text<<std::fixed<<std::setprecision(1)<<"; C/N0 "<<label<<' '<<record.cn0<<" dB-Hz, peak "<<record.peak_cn0
            <<" dB-Hz; SNR (10 Hz) "<<label<<' '<<record.cn0-10<<" dB, peak "<<record.peak_cn0-10<<" dB";
        text<<std::setprecision(3)<<"; observed capture "<<static_cast<double>(record.measurement_end)/record.measurement_rate
            <<" s (sample "<<record.measurement_end<<" @ "<<record.measurement_rate<<" Hz)";
        text<<"; peak capture "<<static_cast<double>(record.peak_end)/record.peak_rate
            <<" s (sample "<<record.peak_end<<" @ "<<record.peak_rate<<" Hz)";
        if(record.callsign[0])text<<"; callsign "<<record.callsign.data();
        else if(record.advisory_time)text<<"; "<<(record.advisory_time->encrypted?"encrypted advisory":"clear advisory")
            <<" time "<<std::setprecision(5)<<static_cast<double>(record.advisory_time->ticks)/65536;
        event.text=text.str();return event;
    }
    static constexpr std::size_t none=2;
    std::array<Record,2> records_{};
    std::size_t active_=none;
    std::uint64_t generation_=0,clear_generation_=0,end_=0;
    std::uint32_t rate_=0;
    bool continuous_=false;
    std::optional<std::uint64_t> absent_first_;
};
}
