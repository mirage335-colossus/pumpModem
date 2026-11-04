#pragma once
#include "datapump/helper_carrier.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>
#include <cmath>
#include <memory>

namespace datapump::helper {
// A bounded, actual metadata window inside the prepared Robust payload. The
// reference never includes settling/suppression noise, helper lead/tail, or a
// source-calendar null. It is consumed during preparation, before any PCM.
inline PowerReference transmit_power_reference(modem::Config config,
        const modem::StreamingTransmitter& source,std::uint64_t helper_epoch,
        std::uint64_t payload_epoch,const std::optional<Crypto>& key={},std::stop_token stop={}) {
    if(!config.sample_rate)throw Error("Helper Carrier power reference has no source clock");
    config.stream_epoch=payload_epoch;
    if(key && !config.data_key)config.data_key=key;
    const auto rate=config.sample_rate;
    const auto origin=source.physical_output_origin();
    const auto valid=source.physical_valid_payload_end();
    if(valid>UINT64_MAX-origin)throw Error("Helper Carrier power reference endpoint overflow");
    // The source owns the exact first slot, including a calendar's nonzero
    // first-symbol offset after a configured epoch/phase between active slots.
    const auto payload=source.physical_payload_begin();
    std::shared_ptr<modem::SchedHopSourceMap> scheduled;
    if(modem::scheduled_bursts(config)) {
        if(!config.data_key)throw Error("Helper Carrier power reference requires the scheduled source key");
        scheduled=std::make_shared<modem::SchedHopSourceMap>(*config.scheduled_hopping,*config.data_key,rate);
    }
    config.stream_epoch=payload/rate;config.stream_phase_samples=payload%rate;
    long double begin=payload;
    long double end=std::min(static_cast<long double>(origin)+valid,begin+2.L*rate);
    // These calendars deliberately leave long periods between short complete
    // symbols. Use the first real symbol instead of undersampling their nulls.
    if(config.symbol_burst.enabled() || modem::deferred_symbols(config) ||
       modem::packed_scheduled_symbols(config))
        end=std::min(end,begin+modem::symbol_sample_count(config));
    if(scheduled) {
        auto group=scheduled->locate(payload);
        // A split-source symbol can start near an active group's end. Its
        // first available active interval is sufficient for this reference;
        // do not scan an arbitrarily long keyed null calendar.
        if(!group.active || begin>=group.fall_end) {
            const auto next=scheduled->source_to_physical({group.source_end,0});
            group=scheduled->locate(next);
            begin=static_cast<long double>(next.sample)+next.fraction;
            end=std::min(static_cast<long double>(origin)+valid,begin+2.L*rate);
        }
        begin=std::max(begin,static_cast<long double>(group.rise_begin));
        end=std::min({end,begin+2.L*rate,static_cast<long double>(group.fall_end)});
    }
    if(!(end>begin))throw Error("Helper Carrier has no active Robust power-reference interval");
    PowerReference reference;
    reference.epoch=helper_epoch;reference.time_key=key;
    const auto relative=[&](long double sample) {
        const auto whole=std::floor(sample/rate);
        return (whole-static_cast<long double>(helper_epoch))+(sample-whole*rate)/rate;
    };
    reference.begin_seconds=relative(begin);reference.end_seconds=relative(end);
    std::optional<modem::FhssPowerShape> shape;
    if(config.fake_hopping)shape.emplace(*config.fake_hopping,rate,
        modem::pattern_pulse_padding_samples(config),modem::symbol_sample_count(config));
    if(scheduled || (shape && shape->enabled)) {
        reference.active=[scheduled,shape,helper_epoch,rate](long double seconds) {
            if(scheduled) {
                const auto sample=(static_cast<long double>(helper_epoch)+seconds)*rate;
                if(sample<0 || sample>=std::ldexp(1.L,64))return false;
                const auto whole=static_cast<std::uint64_t>(std::floor(sample));
                const modem::SchedHopCoordinate at{whole,sample-whole};
                const auto group=scheduled->locate(at);
                if(!group.active || modem::SchedHopSourceMap::gain(group,at)<=0)return false;
            }
            if(shape && shape->enabled) {
                if(shape->smooth)return shape->gain(helper_epoch,seconds*rate,rate)>0;
                return !modem::fhss_utc_quiet(helper_epoch,seconds*rate,rate,shape->quiet,shape->lag_center);
            }
            return true;
        };
    }
    // Match the helper calculator's bounded stratified quadrature. Analytic
    // magnitude is component power independent of finite-window carrier beats.
    // Duplicate discrete source positions are reconstructed once at low rates.
    constexpr auto nodes=power_reference_nodes;
    double energy=0;std::size_t accepted=0;
    std::optional<std::uint64_t> previous_sample;
    double previous_power=0;
    const auto origin_seconds=static_cast<long double>(origin%rate)/rate;
    const auto epoch_offset=static_cast<long double>(helper_epoch)-origin/rate;
    for(unsigned i=0;i<nodes;++i) {
        if((i&63U)==0 && stop.stop_requested())throw Error("Helper Carrier power preparation cancelled");
        const auto seconds=power_reference_node(reference,i);
        if(reference.active && !reference.active(seconds))continue;
        const auto position=(epoch_offset+seconds-origin_seconds)*rate;
        if(position<0 || position>=source.total_samples())
            throw Error("Helper Carrier power reference is outside the prepared Robust waveform");
        const auto sample=static_cast<std::uint64_t>(std::floor(position));
        if(!previous_sample || sample!=*previous_sample) {
            previous_power=std::norm(source.preview_analytic_at(sample,stop))/2;
            previous_sample=sample;
        }
        energy+=previous_power;++accepted;
    }
    if(!accepted || !std::isfinite(energy) || energy<=0)
        throw Error("Helper Carrier active power reference contains no Robust energy");
    reference.data_mean_power=energy/accepted;
    return reference;
}
}
