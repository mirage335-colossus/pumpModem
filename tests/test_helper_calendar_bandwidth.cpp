#include "datapump/helper_carrier.hpp"
#include "datapump/transfer.hpp"
#include <cmath>
#include <iostream>
#include <limits>

using namespace datapump;
namespace {
void check(bool value,const char* reason) {if(!value)throw Error(reason);}
template<class F>void rejects(F action,const char* reason) {
    bool rejected=false;try {action();}catch(const Error&) {rejected=true;}
    check(rejected,reason);
}
modem::FhssPlan raw_hops() {
    modem::FhssPlan p;p.channels=2;p.shifts[0]=30e6;p.shifts[1]=30.1e6;
    p.dwell_seconds=.4;return p;
}
modem::Config hf() {
    modem::Config c;c.sample_rate=14400;c.carrier_hz=1500;c.bandwidth_hz=360;
    c.spreading_factor=64;c.scramble=true;c.dsss=true;c.dsss_factor=10;
    c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.stream_epoch=1800000000;
    transfer::Options tx;tx.modem=c;tx.audio_timing_error_seconds=0;
    c.fake_hopping=transfer::transmit_hopping(raw_hops(),tx);return c;
}
modem::Config eme() {
    auto c=hf();c.sample_rate=6000;c.bandwidth_hz=1;c.dsss_factor=1;
    transfer::Options tx;tx.modem=c;tx.audio_timing_error_seconds=0;
    c.fake_hopping=transfer::transmit_hopping(raw_hops(),tx);return c;
}
helper::Options options() {
    helper::Options h;h.power_w=15000;h.data_power_w=4;return h;
}
bool same_plan(const helper::Plan& a,const helper::Plan& b) {
    return a.frequency_hz==b.frequency_hz&&a.auxiliary_frequency_hz==b.auxiliary_frequency_hz&&
        a.low_frequency_hz==b.low_frequency_hz&&a.pilot_dwell_gating==b.pilot_dwell_gating&&a.helper_gain==b.helper_gain&&
        a.data_gain==b.data_gain&&a.effective_power_w==b.effective_power_w;
}
void deferred_support_still_needs_gate_skirt() {
    const auto c=hf();modem::validate(c);
    check(modem::symbol_sample_count(c)==5120&&modem::pattern_chip_samples(c)==8&&
          modem::deferred_symbols(c),"HF acceptance geometry lost its ordinary deferred calendar");
    const auto support=modem::pattern_pulse_padding_samples(c);
    const modem::FhssSymbolCalendar calendar(*c.fake_hopping,c.sample_rate,support,modem::symbol_sample_count(c));
    const modem::FhssPowerShape shape(*c.fake_hopping,c.sample_rate,support,modem::symbol_sample_count(c));
    // Every scored sample is at full gain. This does not cover the finite
    // RRC shoulders: smooth deferral reserves zero extra pulse drain.
    for(std::uint64_t rank=0;rank<5;++rank) {
        const auto first=calendar.slot(rank);
        for(std::uint64_t n=0;n<calendar.symbol;++n)
            check(shape.gain(0,static_cast<long double>(first+n),c.sample_rate)==1,
                  "deferred scoring interval was not in the full-power window");
    }
    bool finite_pulse=false;
    modem::pattern_pulse_each(-4.,calendar.symbol,modem::pattern_chip_samples(c),
        [&](std::uint64_t,double coefficient) {finite_pulse|=coefficient!=0;});
    const auto shoulder_gain=shape.gain(0,static_cast<long double>(calendar.slot(0))-4,c.sample_rate);
    check(finite_pulse&&shoulder_gain>0&&shoulder_gain<1,
          "test failed to retain a real pulse shoulder inside the post-shaper ramp");
    const auto band=helper::placement_band(c);
    check(band.waveform_bandwidth_hz==2250&&std::abs(band.gate_bandwidth_hz-3600)<1e-9&&
          helper::occupied_bandwidth(c)==5850,"HF gate allowance was dropped or ordinary bandwidth changed");
    const auto mono=helper::mono_passband(band);
    check(mono.lower_hz==0&&mono.upper_hz==4425,"real mono gate spectrum was not folded into its full positive union");
    auto h=options();
    rejects([&] {helper::plan(h,band,48000);},
            "folded data overlapping the fixed550/950Hz helpers was accepted");
    auto separated=band;separated.carrier_hz=6500;
    const auto high=helper::plan(h,separated,48000);
    check(high.low_frequency_hz==550&&high.auxiliary_frequency_hz==950&&
          high.frequency_hz>=std::max(2400.,helper::mono_passband(separated).upper_hz+450)&&high.frequency_hz+250<24000&&
          high.data_gain==helper::calibrated_data_gain(h,true),
          "raised Robust carrier changed fixed helper frequencies, payload power or leakage guard");
    check(helper::minimum_output_rate(h,separated,c.sample_rate)==48000,
          "separated helper did not keep independent hardware-rate negotiation");
    const auto wider=helper::placement_band(c,6000);
    check(wider.waveform_bandwidth_hz==2250&&wider.gate_bandwidth_hz==3750&&
          helper::mono_passband(wider).upper_hz==4500,"local profile union was truncated during placement");
    rejects([&] {helper::plan(h,c.carrier_hz,5850,48000);},
            "strict scalar nominal passband validation was weakened");
    h.frequency=helper::Frequency::low;
    rejects([&] {helper::plan(h,band,48000);},"Low helper claimed isolation below a band that reaches DC");
    h.frequency=helper::Frequency::cochannel;
    rejects([&] {helper::plan(h,band,48000);},"retired cochannel helper placement was accepted");
    std::cout<<"HF core=2250 gate=3600 positive=[0,4425] helper="<<high.frequency_hz
             <<" Hz scoring_flat=1 finite_shoulder_gain="<<shoulder_gain<<'\n';
}
void long_gate_and_nonfolding_identity() {
    const auto c=eme();modem::validate(c);
    check(modem::symbol_sample_count(c)==768000&&!modem::deferred_symbols(c),
          "EME geometry lost its ordinary128-second active gate");
    const auto band=helper::placement_band(c);
    check(band.waveform_bandwidth_hz==.625&&band.gate_bandwidth_hz==200&&
          helper::occupied_bandwidth(c)==200.625,"active long-symbol gate allowance was reduced");
    const auto h=options();
    auto separated=band;separated.carrier_hz=4000;
    check(same_plan(helper::plan(h,separated,48000),
                    helper::plan(h,helper::PlacementBand{4000,200.625,0,true},48000)),
          "nonfolding gate-aware placement changed the equivalent symmetric gated helper plan");
    // Gate-aware and scalar geometry agree for the same valid High group.
    const helper::PlacementBand plain{4000,200,0};
    check(same_plan(helper::plan(h,plain,48000),helper::plan(h,4000,200,48000)),
          "ordinary symmetric helper placement changed");
    for(const auto placement:{helper::Frequency::low,helper::Frequency::cochannel}) {
        auto retired=h;retired.frequency=placement;
        rejects([&] {helper::plan(retired,plain,48000);},"gate-aware planner accepted retired helper placement");
        rejects([&] {helper::plan(retired,4000,200,48000);},"scalar planner accepted retired helper placement");
    }
    check(modem::waveform_gate_bandwidth_allowance_hz(c)==200&&c.bandwidth_hz==1&&
          c.sample_rate==6000&&modem::symbol_sample_count(c)==768000,
          "helper planning changed ordinary Rate or gate geometry");
}
void malformed_geometry_fails_closed() {
    const auto h=options();
    for(const auto band:{helper::PlacementBand{1500,4000,3600},
                         helper::PlacementBand{1500,2250,-1},
                         helper::PlacementBand{1500,2250,std::numeric_limits<double>::infinity()},
                         helper::PlacementBand{std::numeric_limits<double>::quiet_NaN(),2250,3600}})
        rejects([&] {helper::plan(h,band,48000);},"malformed nominal/gate geometry was accepted");
    rejects([&] {helper::plan(h,helper::PlacementBand{1500,2250,3600},8000);},
            "folded payload band beyond output Nyquist was accepted");
    auto invalid=hf();invalid.carrier_hz=100;
    rejects([&] {helper::placement_band(invalid);},"invalid ordinary nominal waveform became a valid helper placement");
    invalid=hf();auto plan=std::make_shared<modem::FhssPlan>(*invalid.fake_hopping);
    plan->ramp_seconds=0;invalid.fake_hopping=plan;
    rejects([&] {helper::placement_band(invalid);},"unsupported gate silently became ungated helper geometry");
    rejects([&] {helper::placement_band(hf(),-1);},"invalid local profile union was accepted");
    auto disabled=h;disabled.power_w=0;
    const auto off=helper::plan(disabled,helper::PlacementBand{},0);
    check(off.data_gain==1&&off.helper_gain==0&&helper::minimum_output_rate(disabled,helper::PlacementBand{},0)==0,
          "disabled helper changed ordinary default gain or hardware negotiation");
}
void reference_level_accounting() {
    const auto h=options();
    const auto nominal=36.020599913279625-205+164;
    const auto hardware=helper::reference_level(h,nominal);
    const auto expected=30+10*std::log10(14996.)-205+164;
    check(hardware.effective_power_ratio==3749&&std::abs(hardware.straight_cn0_db_hz-expected)<1e-12&&
          hardware.usable_cn0_db_hz==hardware.straight_cn0_db_hz&&
          hardware.payload_average_cn0_db_hz==nominal,"helper reference level did not match the RF link budget");
    const auto mean=helper::mean_power(h).total;
    check(std::abs(hardware.pilot_effective_power_ratio-hardware.effective_power_ratio*helper::pilot_power_fraction/mean)<1e-9&&
          std::abs(hardware.pilot_straight_cn0_db_hz-hardware.straight_cn0_db_hz-
              10*std::log10(helper::pilot_power_fraction/mean))<1e-12&&
          std::abs(hardware.pilot_straight_cn0_db_hz-hardware.auxiliary_straight_cn0_db_hz-
              9.)<1e-12&&
          std::abs(10*std::log10(helper::low_pilot_power_fraction/helper::pilot_power_fraction)+3)<1e-12,
          "pilot/auxiliary reference levels did not preserve mean calibration and3/9dB steady-tone powers");
    const auto sampled=helper::reference_level(h,nominal,.5,.25,.5);
    const auto backoff=20*std::log10(.5);
    check(std::abs(sampled.straight_cn0_db_hz-hardware.straight_cn0_db_hz-backoff)<1e-12&&
          std::abs(sampled.usable_cn0_db_hz-sampled.straight_cn0_db_hz-10*std::log10(.5))<1e-12&&
          std::abs(sampled.payload_average_cn0_db_hz-nominal-backoff-10*std::log10(.25))<1e-12,
          "waveform backoff, payload duty and usable helper energy were not kept separate");
    const auto different_duty=helper::reference_level(h,nominal,.5,1,.5);
    check(different_duty.straight_cn0_db_hz==sampled.straight_cn0_db_hz&&
          different_duty.usable_cn0_db_hz==sampled.usable_cn0_db_hz,
          "lower payload duty invented additional helper power");
    auto limited=h;limited.ratio_limit_db=10;
    check(helper::reference_level(limited,nominal).effective_power_ratio==10,
          "helper level planning ignored the effective power-ratio limit");
    rejects([&] {helper::reference_level(h,std::numeric_limits<double>::quiet_NaN());},
            "nonfinite nominal link budget was accepted");
    for(const auto fraction:{0.,-1.,1.01,std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        rejects([&] {helper::reference_level(h,nominal,fraction);},"invalid amplitude scale was accepted");
        rejects([&] {helper::reference_level(h,nominal,1,fraction);},"invalid payload power fraction was accepted");
        rejects([&] {helper::reference_level(h,nominal,1,1,fraction);},"invalid usable tone fraction was accepted");
    }
    auto no_remainder=h;no_remainder.power_w=std::numeric_limits<double>::denorm_min();
    no_remainder.data_power_w=std::numeric_limits<double>::max();
    const auto absent=helper::reference_level(no_remainder,nominal);
    check(absent.effective_power_ratio==0&&std::isinf(absent.straight_cn0_db_hz)&&absent.straight_cn0_db_hz<0,
          "total power below Robust invented a helper reference");
    auto disabled=h;disabled.power_w=0;
    rejects([&] {helper::reference_level(disabled,nominal);},"disabled helper produced a finite tone-level prediction");
}
void auxiliary_punctuation_calendar() {
    for(const auto epoch:{0ULL,1ULL,1800000000ULL}) {
        const auto origin=epoch%2?-.2L:0.L;
        const auto first=origin+.236L,last=origin+.268L;
        for(const auto stop:{first,origin+.244L,origin+.264L,last-.000001L})
            check(std::abs(helper::protected_endpoint(epoch,origin,stop)-last)<1e-15L,
                  "protected helper stop split its auxiliary reversal or inverted straight-tone flank");
        check(helper::protected_endpoint(epoch,origin,last)==last&&
              helper::protected_endpoint(epoch,origin,origin+.272L)==origin+.272L&&
              helper::protected_endpoint(epoch,origin+.212001L,first)==first,
              "punctuation protection changed an unpunctuated partial dwell");
    }
}
void bounded_envelope_planning() {
    const auto c=eme();const auto initial=helper::plan_envelope(c);
    check(initial.lead_seconds==38&&std::abs(initial.tail_seconds-161.2L)<1e-14L,
          "helper envelope lost its fixed 36+2-second lead or 128-second absence and pulse-support tail");
    check(initial.preparation_seconds==38.L&&
          std::abs(initial.maximum_stop_extension_seconds-(200.L+2.L/6000))<1e-14L,
          "helper preparation was widened or protected-stop bounds lost their tail margin");
    helper::EnvelopeRequirements requirements;requirements.frequency_acquisition_seconds=6.4;
    auto planned=helper::plan_envelope(c,requirements);
    check(planned.lead_seconds==38,"receiver acquisition widened the fixed helper lead");
    requirements.frequency_acquisition_seconds=std::nextafter(6.4,std::numeric_limits<double>::infinity());
    check(helper::plan_envelope(c,requirements).lead_seconds==38,
          "an acquisition ULP changed the fixed helper lead");
    requirements.frequency_acquisition_seconds=1000000000000.0002;
    const auto large=helper::plan_envelope(c,requirements);
    check(large.lead_seconds==38,"a large receiver forecast enlarged the fixed helper lead");
    requirements.frequency_acquisition_seconds=2.4;
    check(helper::plan_envelope(c,requirements).lead_seconds==38,
          "the receiver aperture replaced the fixed helper lead");
    requirements.frequency_acquisition_seconds=6.4;requirements.frontend_lookahead_seconds=.08;
    requirements.receiver_buffer_deficit_seconds=512;
    planned=helper::plan_envelope(c,requirements);
    check(planned.lead_seconds==38&&std::abs(planned.tail_seconds-512.4L)<1e-14L,
          "helper lookahead changed the fixed lead or was omitted from the frozen tail obligation");
    requirements.maximum_source_capture_ratio=.5;
    const auto slower=helper::plan_envelope(c,requirements);
    check(slower.lead_seconds==38&&slower.tail_seconds==planned.tail_seconds,
          "a slower source clock changed the fixed lead or tail coverage");
    requirements.maximum_source_capture_ratio=1.001;
    const auto faster=helper::plan_envelope(c,requirements);
    check(faster.lead_seconds==38&&faster.tail_seconds>=1.001L*(512.L+.08L),
          "source clock allowance changed the fixed lead or shortened the drain aperture");
    // The former adaptive input remains accepted for Planner forecast
    // compatibility, but cannot change the fixed 36+2-second transmission.
    for(const auto extra:{0.,.001,.4,std::nextafter(.4,std::numeric_limits<double>::infinity()),
            .4001,35.9,36.}) {
        auto precision=requirements;precision.frequency_precision_extension_seconds=extra;
        const auto extended=helper::plan_envelope(c,precision);
        check(extended.lead_seconds==38 &&
              extended.tail_seconds==faster.tail_seconds,
              "precision forecast changed the fixed lead or physical tail");
        check(extended.preparation_seconds==faster.preparation_seconds&&
              extended.maximum_stop_extension_seconds==faster.maximum_stop_extension_seconds,
              "precision forecast changed preparation or protected-stop reservation");
    }
    for(const auto extra:{std::nextafter(36.,std::numeric_limits<double>::infinity()),36.01}) {
        auto invalid_precision=requirements;invalid_precision.frequency_precision_extension_seconds=extra;
        rejects([&]{helper::plan_envelope(c,invalid_precision);},"precision extension beyond 36 seconds was accepted");
    }
    for(const auto value:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        helper::EnvelopeRequirements bad;bad.frequency_acquisition_seconds=value;
        rejects([&] {helper::plan_envelope(c,bad);},"invalid acquisition duration was accepted");
        bad={};bad.frontend_lookahead_seconds=value;
        rejects([&] {helper::plan_envelope(c,bad);},"invalid frontend duration was accepted");
        bad={};bad.receiver_buffer_deficit_seconds=value;
        rejects([&] {helper::plan_envelope(c,bad);},"invalid receiver-buffer duration was accepted");
        bad={};bad.frequency_precision_extension_seconds=value;
        rejects([&] {helper::plan_envelope(c,bad);},"invalid precision extension was accepted");
        bad={};bad.maximum_source_capture_ratio=value;
        rejects([&] {helper::plan_envelope(c,bad);},"invalid source/capture ratio was accepted");
    }
    helper::EnvelopeRequirements invalid;invalid.maximum_source_capture_ratio=0;
    rejects([&] {helper::plan_envelope(c,invalid);},"zero source/capture ratio was accepted");
    invalid={};invalid.receiver_buffer_deficit_seconds=std::numeric_limits<double>::max();
    rejects([&] {helper::plan_envelope(c,invalid);},"overflowing helper drain obligation was accepted");
}
}
int main() {
    try {deferred_support_still_needs_gate_skirt();long_gate_and_nonfolding_identity();malformed_geometry_fails_closed();
        reference_level_accounting();auxiliary_punctuation_calendar();bounded_envelope_planning();
        std::cout<<"Helper calendar bandwidth checks passed\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
