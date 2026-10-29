#pragma once
#include "../src/helper_power_reference.hpp"
#include <cmath>
#include <chrono>
#include <iostream>
#include <stdexcept>

inline void test_helper_power_reference() {
    using namespace datapump;
    const auto check=[](bool value,const char* message) {
        if(!value)throw std::runtime_error(message);
    };
    modem::Config actual;
    actual.sample_rate=8000;actual.bandwidth_hz=160;actual.carrier_hz=1725;
    actual.integration_seconds=.1;actual.pulse_shaping=false;
    actual.stream_epoch=1710000000;actual.stream_phase_samples=137;
    modem::TransmitTrace capture;capture.active=true;
    modem::StreamingTransmitter source(modem::RawBits{{0,1}},actual,8*1024*1024,capture);
    modem::StreamingTransmitter untouched(modem::RawBits{{0,1}},actual,8*1024*1024,capture);
    const auto total_before=source.total_samples(),valid_before=source.physical_valid_payload_end();
    const auto trace_before=source.transmit_trace();
    auto stale=actual;stale.stream_epoch=0;stale.stream_phase_samples=0;
    const auto epoch=source.physical_output_origin()/actual.sample_rate;
    const auto reference=helper::transmit_power_reference(stale,source,epoch,0);
    const auto expected=static_cast<long double>(actual.stream_epoch-epoch)+137.L/8000;
    check(std::abs(reference.begin_seconds-expected)<1e-11L,
        "power reference used stale channel timing instead of prepared source");
    check(std::abs(reference.end_seconds-reference.begin_seconds-
        2.L*modem::symbol_sample_count(actual)/actual.sample_rate)<1e-11L,
        "short power reference included settling or suppression noise");
    check(source.samples_emitted()==0,"power reference advanced the transmitter");
    const auto trace_after=source.transmit_trace();
    check(source.total_samples()==total_before && source.physical_valid_payload_end()==valid_before &&
        trace_after.generated_bits==trace_before.generated_bits &&
        trace_after.generated_chips==trace_before.generated_chips && trace_after.revision==trace_before.revision,
        "power preview changed endpoint, progress, or emitted trace");
    std::array<std::complex<double>,257> previewed{},original{};
    while(!source.finished()) {
        const auto a=source.read_analytic(previewed),b=untouched.read_analytic(original);
        check(a==b,"power preview changed subsequent source block length");
        for(std::size_t i=0;i<a;++i)check(std::abs(previewed[i]-original[i])<1e-12,
            "power preview changed subsequent transmitter PCM");
    }
    const auto emitted_trace=source.transmit_trace(),untouched_trace=untouched.transmit_trace();
    check(emitted_trace.generated_bits==untouched_trace.generated_bits &&
        emitted_trace.generated_chips==untouched_trace.generated_chips &&
        emitted_trace.pattern_input==untouched_trace.pattern_input &&
        emitted_trace.pattern_output==untouched_trace.pattern_output,
        "power preview changed emitted pattern trace");
    {
        auto narrow=actual;narrow.sample_rate=6000;narrow.bandwidth_hz=1;
        narrow.integration_seconds=0;narrow.stream_phase_samples=0;narrow.pulse_shaping=true;
        narrow.scramble=true;narrow.data_key.emplace(Bytes(32,0x52));
        modem::StreamingTransmitter inspected(modem::RawBits{{0}},narrow,8*1024*1024,capture);
        modem::StreamingTransmitter clean(modem::RawBits{{0}},narrow,8*1024*1024,capture);
        const auto narrow_reference=helper::transmit_power_reference(narrow,inspected,narrow.stream_epoch,
            narrow.stream_epoch,narrow.data_key);
        check(narrow_reference.data_mean_power.has_value(),"narrow shaped source lost actual power calibration");
        const auto expose=inspected.physical_payload_begin()-inspected.physical_output_origin()+6000;
        while(inspected.samples_emitted()<expose) {
            const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(previewed.size(),
                expose-inspected.samples_emitted()));
            const auto a=inspected.read_analytic(std::span(previewed).first(n));
            const auto b=clean.read_analytic(std::span(original).first(n));
            check(a==b,"narrow preview changed actual source count");
            for(std::size_t i=0;i<a;++i)check(std::abs(previewed[i]-original[i])<1e-12,
                "narrow shaped preview changed actual PCM");
        }
        const auto a=inspected.transmit_trace(),b=clean.transmit_trace();
        check(!b.pattern_input.empty() && a.pattern_input==b.pattern_input &&
            a.pattern_output==b.pattern_output && a.generated_chips==b.generated_chips,
            "narrow shaped preview suppressed actual mapper trace");
    }

    auto burst=actual;burst.stream_phase_samples=0;burst.integration_seconds=.02;
    burst.symbol_burst={1,0};
    modem::StreamingTransmitter sparse(modem::RawBits{{0,1,0}},burst);
    const auto sparse_reference=helper::transmit_power_reference(burst,sparse,
        burst.stream_epoch,burst.stream_epoch);
    check(sparse.total_samples()>60ULL*burst.sample_rate,
        "sparse power-reference fixture lost its real transmission gaps");
    check(std::abs(sparse_reference.end_seconds-sparse_reference.begin_seconds-
        static_cast<long double>(modem::symbol_sample_count(burst))/burst.sample_rate)<1e-11L,
        "Symbol_Burst reference averaged its long null intervals");

    auto scheduled=actual;scheduled.stream_phase_samples=0;scheduled.integration_seconds=1.5;
    scheduled.scramble=true;scheduled.data_key.emplace(Bytes(32,0x67));
    auto schedule=std::make_shared<modem::SchedHopPlan>();
    schedule->time_multiplier=10000;schedule->group_hops=1;
    schedule->clock_error_seconds=schedule->peer_clock_error_seconds=.001;
    schedule->audio_error_seconds=.01;schedule->ramp_seconds=.01;
    scheduled.scheduled_hopping=schedule;
    scheduled=modem::scheduled_estimate_config(scheduled);
    modem::StreamingTransmitter split(modem::RawBits{{0,1}},scheduled);
    const auto split_epoch=split.physical_output_origin()/scheduled.sample_rate;
    const auto split_reference=helper::transmit_power_reference(scheduled,split,split_epoch,
        scheduled.stream_epoch,scheduled.data_key);
    check(split_reference.active && split_reference.end_seconds>split_reference.begin_seconds &&
        split_reference.end_seconds-split_reference.begin_seconds<=.4L,
        "split-source power reference included a long scheduled null");
    check(split_reference.active((split_reference.begin_seconds+split_reference.end_seconds)/2),
        "split-source reference did not select actual active waveform time");
    helper::Options options;options.power_w=40;options.data_power_w=4;
    const auto mean=helper::mean_power(options,false,&split_reference);
    check(std::isfinite(mean.total)&&mean.total>0,
        "actual split-source metadata power reference has no finite power");
    const modem::SchedHopSourceMap schedule_map(*scheduled.scheduled_hopping,*scheduled.data_key,
        scheduled.sample_rate);
    const auto first_group=schedule_map.locate(scheduled.stream_epoch*scheduled.sample_rate+
        scheduled.stream_phase_samples);
    const auto guarded=schedule_map.source_to_physical({first_group.source_end-1,0});
    scheduled.stream_epoch=guarded.sample/scheduled.sample_rate;
    scheduled.stream_phase_samples=guarded.sample%scheduled.sample_rate;
    modem::StreamingTransmitter guard_start(modem::RawBits{{0}},scheduled);
    const auto after_gap=helper::transmit_power_reference(scheduled,guard_start,
        guard_start.physical_output_origin()/scheduled.sample_rate,scheduled.stream_epoch,scheduled.data_key);
    check(after_gap.end_seconds>after_gap.begin_seconds &&
        after_gap.end_seconds-after_gap.begin_seconds<=2 &&
        after_gap.active((after_gap.begin_seconds+after_gap.end_seconds)/2),
        "power reference failed to advance past a guarded scheduled group end");

    auto hopping=actual;hopping.stream_phase_samples=0;hopping.integration_seconds=1.5;
    hopping.pulse_shaping=true;hopping.scramble=true;hopping.data_key.emplace(Bytes(32,0x73));
    auto plan=std::make_shared<modem::FhssPlan>();
    plan->channels=2;plan->shifts[0]=30000000;plan->shifts[1]=31000000;
    plan->dwell_seconds=.4;plan->power_ramps=plan->mandatory_ramps=plan->smooth_ramps=true;
    plan->clock_error_seconds=plan->peer_clock_error_seconds=.001;
    plan->region_seconds=.001;plan->audio_error_seconds=.01;plan->ramp_seconds=.003;
    hopping.fake_hopping=plan;
    modem::StreamingTransmitter hopped(modem::RawBits{{0}},hopping);
    const auto hopped_reference=helper::transmit_power_reference(hopping,hopped,
        hopping.stream_epoch,hopping.stream_epoch,hopping.data_key);
    check(hopped_reference.active && !hopped_reference.active(0) && hopped_reference.active(.2L),
        "FHSS power reference included a protected transmission gap");
    auto deferred=hopping;deferred.integration_seconds=.02;deferred.bandwidth_hz=1600;
    auto deferred_plan=std::make_shared<modem::FhssPlan>(*plan);deferred_plan->defer_symbols=true;
    deferred.fake_hopping=deferred_plan;
    modem::StreamingTransmitter deferred_source(modem::RawBits{{0,1}},deferred);
    const auto deferred_offset=modem::scheduled_symbol_offset(deferred,deferred.stream_epoch,0,0);
    check(deferred_offset>0,"deferred fixture did not begin between complete-symbol slots");
    const auto deferred_reference=helper::transmit_power_reference(deferred,deferred_source,
        deferred.stream_epoch,deferred.stream_epoch,deferred.data_key);
    check(std::abs(deferred_reference.begin_seconds-
        static_cast<long double>(deferred_offset)/deferred.sample_rate)<1e-11L,
        "power reference used FHSS waiting cover instead of the first payload slot");
    auto packed=deferred;packed.fake_hopping.reset();packed.scheduled_hopping=schedule;
    packed=modem::scheduled_estimate_config(packed);
    // Move the configured anchor one source sample past a packed slot. The
    // transmitter selects the following complete slot as payload_offset(0).
    const modem::SchedHopSourceMap packed_map(*packed.scheduled_hopping,*packed.data_key,packed.sample_rate);
    const auto packed_anchor=packed.stream_epoch*packed.sample_rate+packed.stream_phase_samples;
    const auto shifted=packed_map.source_to_physical(packed_map.locate(packed_anchor).source.add_samples(1));
    packed.stream_epoch=shifted.sample/packed.sample_rate;packed.stream_phase_samples=shifted.sample%packed.sample_rate;
    modem::StreamingTransmitter packed_source(modem::RawBits{{0,1}},packed);
    check(packed_source.physical_payload_begin()>shifted.sample,
        "packed fixture did not select a later actual payload slot");
    const auto packed_reference=helper::transmit_power_reference(packed,packed_source,
        packed.stream_epoch,packed.stream_epoch,packed.data_key);
    const auto packed_expected=static_cast<long double>(packed_source.physical_payload_begin()/packed.sample_rate-
        packed.stream_epoch)+static_cast<long double>(packed_source.physical_payload_begin()%packed.sample_rate)/packed.sample_rate;
    check(std::abs(packed_reference.begin_seconds-packed_expected)<1e-11L,
        "power reference used packed waiting cover instead of the first payload slot");

    // Compare bounded point preparation with independently streamed dense
    // source PCM, and a different stratification, at a tractable wide rate.
    for(const bool shaped:{false,true}) {
        auto wide=actual;wide.sample_rate=192000;wide.carrier_hz=60000;
        wide.bandwidth_hz=40000;wide.integration_seconds=0;wide.stream_phase_samples=0;
        wide.pulse_shaping=shaped;wide.scramble=true;
        for(unsigned i=0;i<wide.spreading_seed.size();++i)
            wide.spreading_seed[i]=static_cast<std::uint8_t>(i*7+1);
        modem::StreamingTransmitter prepared(modem::RawBits{Bytes(4096,0)},wide);
        const auto started=std::chrono::steady_clock::now();
        const auto calibrated=helper::transmit_power_reference(wide,prepared,wide.stream_epoch,wide.stream_epoch);
        const auto preparation_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        const auto source_origin=prepared.physical_output_origin();
        const auto origin_relative=(static_cast<long double>(source_origin/wide.sample_rate)-calibrated.epoch)+
            static_cast<long double>(source_origin%wide.sample_rate)/wide.sample_rate;
        const auto first=static_cast<std::uint64_t>(std::llround(
            (calibrated.begin_seconds-origin_relative)*wide.sample_rate));
        const auto last=static_cast<std::uint64_t>(std::llround(
            (calibrated.end_seconds-origin_relative)*wide.sample_rate));
        double dense=0;std::size_t dense_count=0;std::uint64_t at=0;
        std::array<std::complex<double>,1024> block{};
        while(at<last) {
            const auto count=prepared.read_analytic(std::span(block).first(
                static_cast<std::size_t>(std::min<std::uint64_t>(block.size(),last-at))));
            check(count>0,"dense power reference ended before the selected interval");
            for(std::size_t i=0;i<count;++i)if(at+i>=first) {
                const auto seconds=origin_relative+(at+i+.5L)/wide.sample_rate;
                if(calibrated.active&&!calibrated.active(seconds))continue;
                dense+=std::norm(block[i])/2;++dense_count;
            }
            at+=count;
        }
        dense/=dense_count;
        double alternative=0;std::size_t alternate_count=0;
        for(unsigned i=0;i<helper::power_reference_nodes;++i) {
            const auto seconds=helper::power_reference_node(calibrated,i,1);
            if(calibrated.active&&!calibrated.active(seconds))continue;
            const auto sample=static_cast<std::uint64_t>(std::floor(
                (seconds-origin_relative)*wide.sample_rate));
            alternative+=std::norm(prepared.preview_analytic_at(sample))/2;++alternate_count;
        }
        alternative/=alternate_count;
        const auto error=std::abs(*calibrated.data_mean_power/dense-1);
        const auto alternate_error=std::abs(alternative/dense-1);
        check(error<.02&&alternate_error<.02,
            "bounded actual Robust power differs from independent dense waveform by over2%");
        std::cout<<"helper source power shaped="<<shaped<<" dense="<<dense
            <<" reference_error="<<error<<" alternate_error="<<alternate_error
            <<" preparation_seconds="<<preparation_seconds<<'\n';
    }
    {
        auto high=actual;high.sample_rate=120000000;high.bandwidth_hz=20000000;
        high.carrier_hz=30000000;high.integration_seconds=0;high.stream_phase_samples=0;
        high.pulse_shaping=false;high.scramble=true;
        modem::StreamingTransmitter high_source(modem::RawBits{Bytes(1000000,0)},high);
        const auto started=std::chrono::steady_clock::now();
        const auto high_reference=helper::transmit_power_reference(high,high_source,high.stream_epoch,high.stream_epoch);
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        check(std::abs(high_reference.end_seconds-high_reference.begin_seconds-2.L)<1e-11L &&
            high_reference.data_mean_power && *high_reference.data_mean_power>0 && high_source.samples_emitted()==0,
            "high-rate calibration shortened its reference or replayed the source");
        std::cout<<"helper source120MHz bounded_points="<<helper::power_reference_nodes
            <<" preparation_seconds="<<elapsed<<'\n';
    }
}
