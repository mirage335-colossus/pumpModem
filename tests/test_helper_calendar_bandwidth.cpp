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
    return a.frequency_hz==b.frequency_hz&&a.helper_gain==b.helper_gain&&
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
    auto h=options();const auto high=helper::plan(h,band,48000);
    check(high.frequency_hz>=mono.upper_hz+450&&high.frequency_hz+250<24000&&
          high.data_gain==helper::calibrated_data_gain(h),"folded High helper changed payload power or lost its leakage guard");
    check(helper::minimum_output_rate(h,band,c.sample_rate)==48000,
          "folded helper did not keep independent hardware-rate negotiation");
    const auto wider=helper::placement_band(c,6000);
    check(wider.waveform_bandwidth_hz==2250&&wider.gate_bandwidth_hz==3750&&
          helper::mono_passband(wider).upper_hz==4500,"local profile union was truncated during placement");
    rejects([&] {helper::plan(h,c.carrier_hz,5850,48000);},
            "strict scalar nominal passband validation was weakened");
    h.frequency=helper::Frequency::low;
    rejects([&] {helper::plan(h,band,48000);},"Low helper claimed isolation below a band that reaches DC");
    h.frequency=helper::Frequency::cochannel;
    check(helper::plan(h,band,48000).frequency_hz==c.carrier_hz,
          "folded cochannel helper moved away from the actual data carrier");
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
    check(same_plan(helper::plan(h,band,48000),helper::plan(h,1500,200.625,48000)),
          "nonfolding gate-aware placement changed the established symmetric helper plan");
    // Leave enough room below the data for the unchanged 15kW Low leakage
    // guard; identity does not imply every placement fits a narrow passband.
    const helper::PlacementBand plain{4000,200,0};
    for(const auto placement:{helper::Frequency::high,helper::Frequency::low,helper::Frequency::cochannel}) {
        auto choice=h;choice.frequency=placement;
        check(same_plan(helper::plan(choice,plain,48000),helper::plan(choice,4000,200,48000)),
              "ordinary symmetric helper placement changed");
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
    const auto expected=30+10*std::log10(15000.)-205+164;
    check(hardware.effective_power_ratio==3750&&std::abs(hardware.straight_cn0_db_hz-expected)<1e-12&&
          hardware.usable_cn0_db_hz==hardware.straight_cn0_db_hz&&
          hardware.payload_average_cn0_db_hz==nominal,"helper reference level did not match the RF link budget");
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
    auto unrepresentable=h;unrepresentable.power_w=std::numeric_limits<double>::denorm_min();
    unrepresentable.data_power_w=std::numeric_limits<double>::max();
    rejects([&] {helper::reference_level(unrepresentable,nominal);},"unrepresentable helper ratio was silently rounded to zero");
    auto disabled=h;disabled.power_w=0;
    rejects([&] {helper::reference_level(disabled,nominal);},"disabled helper produced a finite tone-level prediction");
}
void bounded_envelope_planning() {
    const auto c=eme();const auto initial=helper::plan_envelope(c);
    check(initial.lead_seconds==2&&std::abs(initial.tail_seconds-161.2L)<1e-14L,
          "helper envelope lost its minimum lead or 128-second absence and pulse-support tail");
    check(std::abs(initial.preparation_seconds-(2.4L+1.L/6000))<1e-14L&&
          std::abs(initial.maximum_stop_extension_seconds-(164.L+2.L/6000))<1e-14L,
          "helper preparation and protected-stop bounds omitted dwell alignment or sample rounding");
    helper::EnvelopeRequirements requirements;requirements.frequency_acquisition_seconds=6.4;
    auto planned=helper::plan_envelope(c,requirements);
    check(std::abs(planned.lead_seconds-6.4L)<1e-14L,"binary64 aperture rounding added an unintended whole dwell");
    requirements.frequency_acquisition_seconds=std::nextafter(6.4,std::numeric_limits<double>::infinity());
    check(std::abs(helper::plan_envelope(c,requirements).lead_seconds-6.4L)<1e-14L,
          "one input ULP changed the intended dwell count");
    requirements.frequency_acquisition_seconds=1000000000000.0002;
    const auto large=helper::plan_envelope(c,requirements);
    check(large.lead_seconds>=static_cast<long double>(requirements.frequency_acquisition_seconds)&&
          large.lead_seconds-static_cast<long double>(requirements.frequency_acquisition_seconds)>.3L,
          "large-duration ULP snapping shortened capture by more than half a source frame");
    requirements.frequency_acquisition_seconds=2.4;
    check(std::abs(helper::plan_envelope(c,requirements).lead_seconds-2.4L)<1e-14L,
          "wide-search aperture was replaced by a hardcoded receiver policy");
    requirements.frequency_acquisition_seconds=6.4;requirements.frontend_lookahead_seconds=.08;
    requirements.receiver_buffer_deficit_seconds=512;
    planned=helper::plan_envelope(c,requirements);
    check(std::abs(planned.lead_seconds-6.8L)<1e-14L&&std::abs(planned.tail_seconds-512.4L)<1e-14L,
          "helper envelope omitted frontend lookahead or its frozen receiver-buffer obligation");
    requirements.maximum_source_capture_ratio=.5;
    const auto slower=helper::plan_envelope(c,requirements);
    check(slower.lead_seconds==planned.lead_seconds&&slower.tail_seconds==planned.tail_seconds,
          "a slower source clock shortened required receiver coverage");
    requirements.maximum_source_capture_ratio=1.001;
    const auto faster=helper::plan_envelope(c,requirements);
    check(faster.lead_seconds>=1.001L*(6.4L+.08L)&&faster.tail_seconds>=1.001L*(512.L+.08L),
          "source clock allowance shortened the physical acquisition or drain aperture");
    for(const auto value:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        helper::EnvelopeRequirements bad;bad.frequency_acquisition_seconds=value;
        rejects([&] {helper::plan_envelope(c,bad);},"invalid acquisition duration was accepted");
        bad={};bad.frontend_lookahead_seconds=value;
        rejects([&] {helper::plan_envelope(c,bad);},"invalid frontend duration was accepted");
        bad={};bad.receiver_buffer_deficit_seconds=value;
        rejects([&] {helper::plan_envelope(c,bad);},"invalid receiver-buffer duration was accepted");
        bad={};bad.maximum_source_capture_ratio=value;
        rejects([&] {helper::plan_envelope(c,bad);},"invalid source/capture ratio was accepted");
    }
    helper::EnvelopeRequirements invalid;invalid.maximum_source_capture_ratio=0;
    rejects([&] {helper::plan_envelope(c,invalid);},"zero source/capture ratio was accepted");
    invalid={};invalid.frequency_acquisition_seconds=std::numeric_limits<double>::max();
    rejects([&] {helper::plan_envelope(c,invalid);},"overflowing helper preparation was accepted");
    invalid={};invalid.receiver_buffer_deficit_seconds=std::numeric_limits<double>::max();
    rejects([&] {helper::plan_envelope(c,invalid);},"overflowing helper drain obligation was accepted");
}
}
int main() {
    try {deferred_support_still_needs_gate_skirt();long_gate_and_nonfolding_identity();malformed_geometry_fails_closed();
        reference_level_accounting();bounded_envelope_planning();
        std::cout<<"Helper calendar bandwidth checks passed\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
