#include "datapump/pattern_search.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace datapump;
namespace {
void check(bool condition,const char* text) {if(!condition)throw std::runtime_error(text);}
template<class Action>void rejects(Action action,const char* text) {
    try {action();}catch(const Error&){return;}
    throw std::runtime_error(text);
}
void bounded_bank(const modem::Config& config) {
    const auto geometry=modem::default_pattern_frequency_search(config);
    const auto offsets=modem::default_pattern_frequency_offsets(config);
    check(geometry.count>=1 && geometry.count<=modem::maximum_pattern_frequency_hypotheses &&
          geometry.count%2==1 && offsets.size()==geometry.count,"finite odd frequency bank");
    check(std::isfinite(geometry.step_hz) && geometry.step_hz>0 &&
          std::isfinite(geometry.half_width_hz) && geometry.half_width_hz>=0 &&
          std::isfinite(geometry.requested_half_width_hz),"finite search geometry");
    check(offsets.front()==0,"center hypothesis is first");
    const auto limit=modem::pattern_frequency_offset_limit(config);
    for(std::size_t index=1;index<offsets.size();index+=2) {
        check(offsets[index]<0 && offsets[index+1]==-offsets[index],"ordered symmetric frequency pairs");
        check(std::abs(offsets[index])<=limit,"every generated offset fits its permitted span");
        if(index>1)check(offsets[index]<offsets[index-2],"frequency lattice grows without duplicate bins");
    }
    check(std::abs(offsets.back())==geometry.half_width_hz,"advertised half width equals last actual hypothesis");
}
void unchanged_short_searches() {
    modem::Config config;
    for(const auto bandwidth:{10.,100.,1200.,2400.}) {
        config.bandwidth_hz=bandwidth;
        const auto step=.25*config.sample_rate/static_cast<double>(modem::symbol_sample_count(config));
        const auto offsets=modem::default_pattern_frequency_offsets(config);
        check(offsets==std::vector<double>{0,-step,step,-2*step,2*step},"short profiles preserve exact old five offsets");
        const auto geometry=modem::default_pattern_frequency_search(config);
        check(!geometry.limited && geometry.count==5,"short profile coverage remains complete");
        bounded_bank(config);
    }
    config.sample_rate=14400;config.bandwidth_hz=3600;config.carrier_hz=1500;
    check(modem::pattern_frequency_offset_limit(config)==375,
          "default GUI carrier must reserve actual shaped support rather than half the configured bandwidth");
    check(modem::default_pattern_frequency_search(config).count==5,
          "default shaped GUI profile retains its original five hypotheses");
    bounded_bank(config);
}
void long_pattern_coverage() {
    modem::Config config;config.bandwidth_hz=1;
    auto geometry=modem::default_pattern_frequency_search(config);
    check(geometry.count==309 && geometry.step_hz==1./512 && geometry.half_width_hz==154./512,
          "128-second symbols cover both signs of 200 ppm carrier uncertainty");
    check(!geometry.limited && geometry.half_width_hz>=.3 &&
          geometry.half_width_hz-geometry.step_hz<.3,"extended bank stops at first sufficient pair");
    check(geometry.half_width_hz>config.bandwidth_hz/8,"pattern search can extend beyond old bandwidth fraction");
    bounded_bank(config);
    config.bandwidth_hz=100;config.spreading_factor=8192;
    geometry=modem::default_pattern_frequency_search(config);
    check(geometry.count==395 && geometry.half_width_hz>=.3 && !geometry.limited,
          "weak 100-Hz profile covers nominal carrier uncertainty");
    bounded_bank(config);
    config.integration_seconds=86400;
    geometry=modem::default_pattern_frequency_search(config);
    check(geometry.count==4097 && geometry.limited && geometry.half_width_hz<.006,
          "day-long integration retains finite contiguous central coverage");
    check(geometry.half_width_hz==2048*geometry.step_hz,
          "bounded bank does not widen spacing to disguise incomplete coverage");
    bounded_bank(config);
}
void short_carrier_passband_boundaries() {
    modem::Config config;config.sample_rate=14400;config.bandwidth_hz=3600;
    config.spreading_factor=16;
    for(const auto carrier:{1125.,6075.}) {
        config.carrier_hz=carrier;
        modem::validate(config);
        const auto geometry=modem::default_pattern_frequency_search(config);
        check(geometry.count==1 && geometry.half_width_hz==0 && geometry.limited &&
              modem::default_pattern_frequency_offsets(config)==std::vector<double>{0},
              "short shaped edge carriers must retain a usable nominal hypothesis");
        bounded_bank(config);
    }
    config.carrier_hz=1155;
    const auto limited=modem::default_pattern_frequency_search(config);
    check(limited.count==3 && limited.step_hz==28.125 && limited.half_width_hz==28.125 && limited.limited,
          "short shaped search must retain only complete feasible frequency pairs");
    bounded_bank(config);
    config.carrier_hz=1200;
    check(modem::default_pattern_frequency_search(config).count==5,
          "short shaped search with enough headroom must preserve its five offsets");
    bounded_bank(config);
}
void quantization_and_endpoints() {
    modem::Config config;config.bandwidth_hz=97;config.spreading_factor=8192;
    config.integration_seconds=1.00001;
    const auto geometry=modem::default_pattern_frequency_search(config);
    check(modem::symbol_sample_count(config)==6001 && geometry.step_hz==1500./6001,
          "frequency spacing uses exact observed symbol samples");
    check(geometry.step_hz!=.25/config.integration_seconds,"unquantized duration is not used");
    bounded_bank(config);
    config.bandwidth_hz=100;config.spreading_factor=64;
    config.integration_seconds=15.9998;
    check(modem::default_pattern_frequency_search(config).count==5,
          "profiles shorter than sixteen sampled seconds retain five frequency hypotheses");
    config.integration_seconds=15.99999;
    check(modem::symbol_sample_count(config)==96000 && modem::default_pattern_frequency_search(config).count==41,
          "sixteen-second expansion boundary follows sampled duration rather than fractional request");
    bounded_bank(config);
    config.integration_seconds=16;
    config.carrier_hz=31.25;
    auto edge=modem::default_pattern_frequency_search(config);
    check(edge.count==1 && edge.half_width_hz==0 && edge.limited,"DC boundary retains only nominal carrier");
    bounded_bank(config);
    config.carrier_hz=2968.75;
    edge=modem::default_pattern_frequency_search(config);
    check(edge.count==1 && edge.limited,"Nyquist boundary retains only nominal carrier");
    bounded_bank(config);
    config.carrier_hz=40;
    check(modem::pattern_frequency_offset_limit(config)==8.75,
          "shaped support inside nominal bandwidth retains its actual positive passband headroom");
    bounded_bank(config);
    config.carrier_hz=1500;config.integration_seconds=1e15;
    edge=modem::default_pattern_frequency_search(config);
    check(edge.count==4097 && edge.limited,"huge valid sample coordinates cannot overflow hypothesis counting");
    bounded_bank(config);
}
void tones_and_invalid_configurations() {
    modem::Config config;config.bandwidth_hz=1;config.spreading_mode=modem::SpreadingMode::tone;
    for(const auto duration:{0.,86400.,1e15}) {
        config.integration_seconds=duration;
        const auto step=.25*config.sample_rate/static_cast<double>(modem::symbol_sample_count(config));
        check(modem::default_pattern_frequency_offsets(config)==std::vector<double>{0,-step,step,-2*step,2*step},
              "tones retain their original bank to avoid alternate-bit aliases");
        check(modem::pattern_frequency_offset_limit(config)==.125,"tone offsets retain bandwidth/8 limit");
        bounded_bank(config);
    }
    config={};config.integration_seconds=std::numeric_limits<double>::infinity();
    rejects([&]{modem::default_pattern_frequency_search(config);},"infinite integration is rejected");
    config={};config.carrier_hz=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{modem::pattern_frequency_offset_limit(config);},"invalid carrier is rejected");
    config={};config.bandwidth_hz=0;
    rejects([&]{modem::default_pattern_frequency_offsets(config);},"invalid bandwidth is rejected before allocation");
}
void projected_bin_coverage() {
    modem::Config config;config.bandwidth_hz=1;
    check(modem::pattern_projection_bin_samples(config,0)==6000,"zero offset preserves compact half-chip bins");
    check(modem::pattern_projection_bin_samples(config,.25)==6000,"quarter-cycle endpoint preserves original bin");
    check(modem::pattern_projection_bin_samples(config,.3)==3000,"wider carrier coverage shrinks bins to largest safe divisor");
    check(modem::pattern_projection_bin_samples(config,1)==1500,"projection geometry follows offset coverage");
    check(modem::pattern_projection_bin_samples(config,1500)==1,"largest allowed offset requires individual samples");
    config.integration_seconds=1.00001;
    check(modem::pattern_projection_bin_samples(config,.3)==1,"partial symbols preserve exact shared boundaries");
    config={};config.sample_rate=120000000;config.bandwidth_hz=.01;config.carrier_hz=30000000;
    const auto reduced=modem::pattern_projection_bin_samples(config,6000);
    check(reduced==5000,"large chip counts use bounded divisor search");
    rejects([&]{modem::pattern_projection_bin_samples(config,-1);},"negative maximum offset is invalid");
    rejects([&]{modem::pattern_projection_bin_samples(config,std::numeric_limits<double>::infinity());},
            "nonfinite maximum offset is invalid");
    rejects([&]{modem::pattern_projection_bin_samples(config,config.sample_rate);},
            "offset beyond quarter-cycle sample limit is invalid");
}
bool near(double a,double b) {
    return std::abs(a-b)<=1e-12*std::max({std::abs(a),std::abs(b),1e-100});
}
modem::Config oscillator_config() {
    modem::Config config;config.bandwidth_hz=100;config.integration_seconds=128;
    config.oscillator_search=modem::OscillatorSearchConfig{};
    return config;
}
void oscillator_bounds_and_phase() {
    auto config=oscillator_config();
    const auto crystal=modem::oscillator_pattern_search(config);
    check(near(crystal.frequency.requested_half_width_hz,.45) &&
          near(crystal.frequency.half_width_hz,.45) && !crystal.limited,
          "default margin applies once to the effective relative crystal bound");
    check(crystal.hypotheses.size()==crystal.frequency.count &&
          near(crystal.clock_half_width_ppm,300),"untranslated audio searches linked frequency/rate hypotheses");
    const auto effects=modem::oscillator_effects(config);
    check(effects.clock_error_ppm==100 && effects.frequency_offset_hz==0 &&
          effects.phase_noise_degrees_per_sqrt_second==.5,"inactive RF model adds no untranslated impairment");
    config.oscillator_search->margin=2;
    check(near(modem::oscillator_pattern_search(config).frequency.requested_half_width_hz,.3),
          "two-times margin is applied without endpoint doubling");
    config.oscillator_search->margin=3;
    config.oscillator_search->lf=tuning::oscillator_model(tuning::parse_oscillator_preset("gpsdo-xo"));
    const auto gps=modem::oscillator_pattern_search(config);
    check(gps.frequency.count==3 && gps.hypotheses.size()==3 && !gps.limited &&
          near(gps.clock_half_width_ppm,.0003),"GPS residual narrows the actual bank without a broad minimum");
    for(auto name:{"gpsdo-tcxo","gpsdo-ocxo"}) {
        config.oscillator_search->lf=tuning::oscillator_model(tuning::parse_oscillator_preset(name));
        check(modem::oscillator_pattern_search(config).hypotheses==gps.hypotheses,
              "equal GPS accuracy assumptions retain identical frequency/rate geometry");
    }
    config.oscillator_search->rf_shift_hz=10000000;
    config.oscillator_search->rf.phase_noise_degrees_per_sqrt_second=.05;
    check(near(modem::oscillator_effects(config).phase_noise_degrees_per_sqrt_second,std::hypot(.005,.05)),
          "independent active stages combine phase variances");
    config.oscillator_search->reference=modem::OscillatorReference::shared_radio;
    check(modem::oscillator_effects(config).phase_noise_degrees_per_sqrt_second==.05,
          "shared radio uses the RF model without double-counting the LF phase model");
}
void exact_declared_lattice_endpoints() {
    auto config=oscillator_config();auto& policy=*config.oscillator_search;
    for(const auto carrier:{50.,1500.})for(const auto duration:{1.3,82.,128.})
        for(const auto accuracy:{.0001,100.})for(const auto margin:{1.,3.}) {
            config.carrier_hz=carrier;config.integration_seconds=duration;
            policy.margin=margin;policy.lf.accuracy_ppm=accuracy;
            const auto plan=modem::oscillator_pattern_search(config);
            check(!plan.limited && plan.frequency.half_width_hz==plan.frequency.requested_half_width_hz &&
                  plan.clock_half_width_ppm==plan.requested_clock_half_width_ppm,
                  "complete correlated lattice must retain exact declared frequency and clock endpoints");
            check(modem::default_pattern_frequency_offsets(config).back()==plan.frequency.half_width_hz &&
                  plan.hypotheses.back().clock_error_ppm==plan.requested_clock_half_width_ppm,
                  "frequency wrapper and correlated outer pair must agree with declared endpoints exactly");
        }
    policy.margin=1;policy.lf.accuracy_ppm=100;policy.rf_shift_hz=10000;policy.rf.accuracy_ppm=.0001;
    const auto independent=modem::oscillator_pattern_search(config);
    check(!independent.limited && independent.clock_half_width_ppm==independent.requested_clock_half_width_ppm &&
          independent.frequency.half_width_hz==independent.frequency.requested_half_width_hz,
          "independent reference lattice must retain exact outer rate and frequency endpoints");
}
void shared_radio_frequency_mapping() {
    auto config=oscillator_config();auto& policy=*config.oscillator_search;
    policy.reference=modem::OscillatorReference::shared_radio;policy.rf_shift_hz=10000000;
    policy.rf=tuning::oscillator_model(tuning::parse_oscillator_preset("gpsdo-ocxo"));
    const auto upper=modem::oscillator_pattern_search(config);
    const auto effects=modem::oscillator_effects(config);
    check(effects.physical_rf_hz==10001500 && near(effects.frequency_offset_hz,.001) &&
          effects.clock_error_ppm==.0001,"RF LO and actual PCM tone are added exactly once");
    check(near(upper.frequency.requested_half_width_hz,.00300045) && !upper.limited,
          "shared-radio bound uses the physical RF carrier");
    for(auto pair:upper.hypotheses)
        check(near(pair.frequency_offset_hz,effects.physical_rf_hz*pair.clock_error_ppm*1e-6),
              "shared radio retains frequency/time correlation on every lane");
    policy.lf={10000,179};
    check(modem::oscillator_pattern_search(config).hypotheses==upper.hypotheses,
          "disabled independent LF oscillator does not alter the shared radio bank");
    policy.sideband=modem::OscillatorSideband::lower;
    const auto lower=modem::oscillator_pattern_search(config);
    check(modem::oscillator_effects(config).physical_rf_hz==9998500 &&
          near(modem::oscillator_effects(config).frequency_offset_hz,-.001),
          "lower sideband reverses RF translation error in real-tone coordinates");
    for(auto pair:lower.hypotheses)
        check(near(pair.frequency_offset_hz,-9998500*pair.clock_error_ppm*1e-6),
              "lower-sideband frequency and time hypotheses retain the signed relation");
    policy.rf_shift_hz=0;
    check(modem::oscillator_effects(config).clock_error_ppm==.0001 &&
          near(modem::oscillator_pattern_search(config).clock_half_width_ppm,.0003),
          "explicit shared-reference mode remains active when external shift is zero");
}
void independent_reference_geometry() {
    auto config=oscillator_config();auto& policy=*config.oscillator_search;
    policy.rf_shift_hz=10000000;
    policy.rf=tuning::oscillator_model(tuning::parse_oscillator_preset("gpsdo-ocxo"));
    const auto bank=modem::oscillator_pattern_search(config);
    check(near(bank.frequency.requested_half_width_hz,.453) && near(bank.clock_half_width_ppm,300) && !bank.limited,
          "independent LF and RF errors have distinct conservative bounds");
    check(bank.hypotheses.size()>bank.frequency.count && bank.hypotheses.size()<bank.frequency.count*17,
          "independent bank retains genuine clock dimensions and trims impossible corners");
    for(auto clock:{-300.,-173.,0.,91.,300.})for(auto rf:{-.003,0.,.003}) {
        const auto frequency=1500*clock*1e-6+rf;
        const auto found=std::any_of(bank.hypotheses.begin(),bank.hypotheses.end(),[&](auto pair) {
            return std::abs(pair.clock_error_ppm-clock)<=bank.clock_step_ppm/2+1e-12 &&
                std::abs(pair.frequency_offset_hz-frequency)<=bank.frequency.step_hz/2+1e-12;
        });
        check(found,"trimmed independent bank covers both signs and declared uncertainty edges");
    }
    policy.rf.accuracy_ppm=100;
    const auto wider=modem::oscillator_pattern_search(config);
    check(near(wider.requested_clock_half_width_ppm,bank.requested_clock_half_width_ppm) && wider.limited &&
          wider.hypotheses.size()<=modem::maximum_pattern_frequency_rate_hypotheses,
          "RF quality never silently changes independent LF timing and excessive coverage is explicit");
}
void short_and_bounded_oscillator_searches() {
    auto config=oscillator_config();config.sample_rate=400;config.carrier_hz=50;
    config.bandwidth_hz=50;config.integration_seconds=.08;
    for(auto name:{"crystal","gpsdo-xo"}) {
        config.oscillator_search->lf=tuning::oscillator_model(tuning::parse_oscillator_preset(name));
        const auto bank=modem::oscillator_pattern_search(config);
        check(bank.hypotheses.size()==3 && !bank.limited,"short symbols retain the actual narrow static region");
        for(auto pair:bank.hypotheses)
            check(std::abs(pair.clock_error_ppm)<=3*config.oscillator_search->lf.accuracy_ppm &&
                  std::abs(pair.clock_error_ppm)<=10000,"short-symbol phase bins never inflate the clock model");
    }
    config=oscillator_config();config.integration_seconds=86400;
    const auto limited=modem::oscillator_pattern_search(config);
    check(limited.frequency.count==4097 && limited.limited &&
          limited.frequency.step_hz<=.25/86400,"frequency cap preserves fine spacing and reports narrower coverage");
    config=oscillator_config();config.carrier_hz=31.25;
    const auto edge=modem::oscillator_pattern_search(config);
    check(edge.hypotheses==std::vector<modem::PatternFrequencyRateHypothesis>{{0,0}} && edge.limited,
          "real-stream passband boundary retains nominal reception and reports missing margin coverage");
    config=oscillator_config();config.oscillator_search->lf.accuracy_ppm=0;
    check(modem::oscillator_pattern_search(config).hypotheses==std::vector<modem::PatternFrequencyRateHypothesis>{{0,0}},
          "declared exact clock needs no arbitrary extra frequency lanes");
}
void invalid_oscillator_policies() {
    auto config=oscillator_config();
    for(auto margin:{0.,.5,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        config.oscillator_search->margin=margin;
        rejects([&]{modem::validate(config);},"invalid oscillator margin accepted");
    }
    config=oscillator_config();config.oscillator_search->rf_shift_hz=-1;
    rejects([&]{modem::validate(config);},"negative RF LO accepted");
    config.oscillator_search->rf_shift_hz=1000;config.oscillator_search->sideband=modem::OscillatorSideband::lower;
    rejects([&]{modem::validate(config);},"nonpositive lower-sideband physical RF accepted");
    config=oscillator_config();config.oscillator_search->lf.accuracy_ppm=10001;
    rejects([&]{modem::validate(config);},"out-of-range effective relative clock accuracy accepted");
    config=oscillator_config();config.oscillator_search->margin=1e308;config.oscillator_search->rf_shift_hz=1e308;
    rejects([&]{modem::validate(config);},"overflowing oscillator coverage accepted");
    config.oscillator_search.reset();
    rejects([&]{modem::oscillator_pattern_search(config);},"missing policy unexpectedly changes low-level defaults");
}
void joint_passband_and_quantized_tones() {
    auto config=oscillator_config();config.sample_rate=400;config.carrier_hz=168.65;
    auto& policy=*config.oscillator_search;
    policy.margin=1;policy.lf.accuracy_ppm=.1/config.carrier_hz*1e6;
    const auto edge=modem::oscillator_pattern_search(config);
    check(edge.limited && edge.frequency.limited && edge.frequency.half_width_hz<.1,
          "clock-stretched occupied support limits joint passband coverage");
    for(auto pair:edge.hypotheses) {
        const auto support=31.25L*(1+static_cast<long double>(pair.clock_error_ppm)*1e-6L);
        check(static_cast<long double>(config.carrier_hz)+pair.frequency_offset_hz+support<=200 &&
              static_cast<long double>(config.carrier_hz)+pair.frequency_offset_hz-support>=0,
              "every retained pair fits the clock-scaled real-stream passband");
    }
    config=oscillator_config();config.spreading_mode=modem::SpreadingMode::tone;
    config.bandwidth_hz=1300;config.integration_seconds=.25;
    auto& tone=*config.oscillator_search;
    tone.margin=1;tone.lf.accuracy_ppm=0;tone.rf_shift_hz=1600000;
    const auto aliases=modem::oscillator_pattern_search(config);
    check(modem::pattern_frequency_offset_limit(config)<150 && aliases.limited &&
          aliases.frequency.half_width_hz<150,"tone uncertainty respects strict quantized chip aliases");
    for(auto pair:aliases.hypotheses)
        check(std::abs(pair.frequency_offset_hz)<150,"new tone bank never emits an alternate-bit alias");
    config=oscillator_config();config.sample_rate=400;config.bandwidth_hz=140;
    config.carrier_hz=200-41.666666666666664;
    auto& rounding=*config.oscillator_search;
    rounding.margin=1;rounding.lf.accuracy_ppm=1e-10;
    rounding.rf_shift_hz=1000000;rounding.rf.accuracy_ppm=1e-15;
    modem::validate(config);
    const auto endpoint=modem::oscillator_pattern_search(config);
    check(!endpoint.hypotheses.empty() && endpoint.hypotheses.front()==modem::PatternFrequencyRateHypothesis{0,0} &&
          endpoint.limited,"validated floating-point endpoint always retains an authoritative nominal lane");
}
}
int main() {
    try {
        unchanged_short_searches();long_pattern_coverage();short_carrier_passband_boundaries();quantization_and_endpoints();
        tones_and_invalid_configurations();projected_bin_coverage();
        oscillator_bounds_and_phase();exact_declared_lattice_endpoints();shared_radio_frequency_mapping();independent_reference_geometry();
        short_and_bounded_oscillator_searches();invalid_oscillator_policies();
        joint_passband_and_quantized_tones();
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    std::cout<<"pattern search tests passed\n";
}
