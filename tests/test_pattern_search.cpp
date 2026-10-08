#include "datapump/pattern_search.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <iterator>
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
}
void zero_shift_uses_only_baseband() {
    auto config=oscillator_config();auto& policy=*config.oscillator_search;
    for(const auto carrier:{50.,1500.})for(const auto& baseband:tuning::oscillator_presets()) {
        config.carrier_hz=carrier;
        policy.lf=tuning::oscillator_model(baseband);policy.rf={0,0};
        policy.reference=modem::OscillatorReference::independent_audio;
        policy.sideband=modem::OscillatorSideband::upper;
        const auto expected=modem::oscillator_pattern_search(config);
        for(const auto reference:{modem::OscillatorReference::independent_audio,
                                  modem::OscillatorReference::shared_radio})
            for(const auto sideband:{modem::OscillatorSideband::upper,modem::OscillatorSideband::lower})
                for(const auto& shift:tuning::oscillator_presets()) {
                    policy.reference=reference;policy.sideband=sideband;
                    policy.rf=tuning::oscillator_model(shift);
                    const auto effects=modem::oscillator_effects(config);
                    check(effects.physical_rf_hz==carrier && effects.frequency_offset_hz==0 &&
                          effects.clock_error_ppm==policy.lf.accuracy_ppm &&
                          effects.phase_noise_degrees_per_sqrt_second==policy.lf.phase_noise_degrees_per_sqrt_second,
                          "zero Shift must use only Baseband frequency, clock and phase effects for every preset and topology");
                    const auto actual=modem::oscillator_pattern_search(config);
                    check(actual.hypotheses==expected.hypotheses && actual.limited==expected.limited &&
                          actual.frequency.count==expected.frequency.count &&
                          actual.frequency.step_hz==expected.frequency.step_hz &&
                          actual.frequency.half_width_hz==expected.frequency.half_width_hz &&
                          actual.frequency.requested_half_width_hz==expected.frequency.requested_half_width_hz &&
                          actual.requested_clock_half_width_ppm==expected.requested_clock_half_width_ppm &&
                          actual.clock_half_width_ppm==expected.clock_half_width_ppm &&
                          actual.clock_step_ppm==expected.clock_step_ppm,
                          "zero Shift must retain the exact Baseband-only search region despite the stored Shift model");
                }
    }
    policy.reference=modem::OscillatorReference::shared_radio;
    policy.lf={0,179};policy.rf={10000,180};
    modem::validate(config);
    check(modem::oscillator_effects(config).phase_noise_degrees_per_sqrt_second==179 &&
          modem::oscillator_pattern_search(config).hypotheses.size()==1,
          "inactive Shift phase must not cause a combined-phase validation failure or extra search lanes");
}
void independent_reference_geometry() {
    auto config=oscillator_config();auto& policy=*config.oscillator_search;
    policy.rf_shift_hz=10000000;
    policy.rf=tuning::oscillator_model(tuning::parse_oscillator_preset("gpsdo-ocxo"));
    const auto bank=modem::oscillator_pattern_search(config);
    check(near(bank.frequency.requested_half_width_hz,.453) && near(bank.clock_half_width_ppm,300) && !bank.limited,
          "independent LF and RF errors have distinct conservative bounds");
    check(bank.hypotheses.size()==bank.frequency.count &&
          bank.clock_step_ppm<=.25*modem::pattern_chip_samples(config)/modem::symbol_sample_count(config)*1e6,
          "unresolved independent conversion error fits the existing timing resolution with one lane per frequency");
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
void check_independent_cell_coverage(const modem::Config& config,const modem::OscillatorPatternSearch& bank) {
    const auto& policy=*config.oscillator_search;
    const auto clock=static_cast<double>(static_cast<long double>(policy.margin)*policy.lf.accuracy_ppm);
    const auto shift=static_cast<double>(static_cast<long double>(policy.margin)*policy.rf_shift_hz*
        policy.rf.accuracy_ppm*1e-6L);
    const auto timing_step=.25*static_cast<double>(modem::pattern_chip_samples(config))/
        static_cast<double>(modem::symbol_sample_count(config))*1e6;
    check(bank.clock_step_ppm<=timing_step,"sparse bank retains the independent quarter-chip timing resolution");
    auto sorted=bank.hypotheses;
    std::sort(sorted.begin(),sorted.end(),[](auto a,auto b){return a.frequency_offset_hz<b.frequency_offset_hz;});
    const auto phase_tolerance=16*std::numeric_limits<double>::epsilon()*bank.frequency.requested_half_width_hz;
    const auto timing_tolerance=8*std::numeric_limits<double>::epsilon()*clock;
    const auto covered=[&](double rate,double rf) {
        if(std::abs(rate)>clock || std::abs(rf)>shift)return;
        const auto frequency=static_cast<long double>(config.carrier_hz)*rate*1e-6L+rf;
        const auto candidate=std::lower_bound(sorted.begin(),sorted.end(),frequency,
            [](auto pair,long double f){return pair.frequency_offset_hz<f;});
        const auto matches=[&](auto pair) {
            // Rounded exact frequency endpoints can differ from a uniform
            // spacing by a few public-coordinate ulps, including at a tie.
            return std::abs(frequency-pair.frequency_offset_hz)<=bank.frequency.step_hz/2+phase_tolerance &&
                std::abs(static_cast<long double>(rate)-pair.clock_error_ppm)<=bank.clock_step_ppm/2+timing_tolerance;
        };
        check((candidate!=sorted.end() && matches(*candidate)) ||
              (candidate!=sorted.begin() && matches(*std::prev(candidate))),
              "independent strip cell edges and their adjacent values retain joint frequency/timing coverage");
    };
    for(auto rate:{-clock,0.,clock})for(auto rf:{-shift,0.,shift})covered(rate,rf);
    // Each frequency cell clips the independent rectangle by two linear
    // boundaries. Its extrema lie at rectangle corners or at the following
    // intersections; these check the continuous domain, not selected clocks.
    for(std::size_t i=1;i<sorted.size();++i) {
        const auto edge=(static_cast<long double>(sorted[i-1].frequency_offset_hz)+sorted[i].frequency_offset_hz)/2;
        for(auto rf:{-shift,shift}) {
            const auto rate=static_cast<double>((edge-rf)/config.carrier_hz*1e6L);
            covered(rate,rf);
            covered(std::nextafter(rate,-std::numeric_limits<double>::infinity()),rf);
            covered(std::nextafter(rate,std::numeric_limits<double>::infinity()),rf);
        }
        for(auto rate:{-clock,clock}) {
            const auto rf=static_cast<double>(edge-static_cast<long double>(config.carrier_hz)*rate*1e-6L);
            covered(rate,rf);
            covered(rate,std::nextafter(rf,-std::numeric_limits<double>::infinity()));
            covered(rate,std::nextafter(rf,std::numeric_limits<double>::infinity()));
        }
    }
}
void thin_independent_regions() {
    auto config=oscillator_config();config.sample_rate=6000;config.carrier_hz=1490;
    auto& policy=*config.oscillator_search;policy.rf_shift_hz=10;
    for(auto accuracy:{.0001,100.})for(auto duration:{1.28,128.,163.84,398.08,600.}) {
        policy.lf.accuracy_ppm=policy.rf.accuracy_ppm=accuracy;config.integration_seconds=duration;
        const auto bank=modem::oscillator_pattern_search(config);
        check(!bank.limited && bank.hypotheses.size()==bank.frequency.count &&
              bank.frequency.half_width_hz==bank.frequency.requested_half_width_hz &&
              bank.clock_half_width_ppm==bank.requested_clock_half_width_ppm,
              "small independent Shift retains complete declared endpoints without a separate rate-axis cap");
        check_independent_cell_coverage(config,bank);
        auto direct=config;direct.carrier_hz=1500;direct.oscillator_search->rf_shift_hz=0;
        const auto zero=modem::oscillator_pattern_search(direct);
        check(bank.frequency.count==zero.frequency.count && bank.hypotheses.size()==zero.hypotheses.size() &&
              near(bank.frequency.requested_half_width_hz,zero.frequency.requested_half_width_hz),
              "equal physical Carrier and equal oscillator accuracy retain comparable zero/small Shift work");
        for(const auto pair:bank.hypotheses) {
            const auto rf=static_cast<long double>(pair.frequency_offset_hz)-
                static_cast<long double>(config.carrier_hz)*pair.clock_error_ppm*1e-6L;
            check(std::abs(rf)<=3*10*accuracy*1e-6+8*std::numeric_limits<double>::epsilon()*bank.frequency.half_width_hz,
                  "sparse lanes remain inside the independent physical strip");
        }
    }
    policy.lf.accuracy_ppm=policy.rf.accuracy_ppm=100;config.integration_seconds=128;
    policy.margin=1;policy.lf.accuracy_ppm=300;policy.rf.accuracy_ppm=1;
    const auto timing_step=.25*static_cast<double>(modem::pattern_chip_samples(config))/
        static_cast<double>(modem::symbol_sample_count(config))*1e6;
    // In this neighborhood the frequency lattice has 244 steps. Solve the
    // strip-width/timing-resolution crossing, then test both sides without
    // weakening either declared resolution.
    const auto boundary=(timing_step*244*1490*1e-6-1490*300*1e-6)/(2*244+1);
    policy.rf_shift_hz=boundary*1e6*(1-1e-10);
    const auto narrow=modem::oscillator_pattern_search(config);
    check(narrow.hypotheses.size()==narrow.frequency.count && !narrow.limited,
          "a strip inside timing resolution uses sparse lanes");
    check_independent_cell_coverage(config,narrow);
    policy.rf_shift_hz=boundary*1e6*(1+1e-10);
    const auto wide=modem::oscillator_pattern_search(config);
    check(wide.hypotheses.size()>wide.frequency.count && !wide.limited,
          "a strip outside timing resolution retains independently resolved timing lanes");
    config.integration_seconds=86400;policy.rf_shift_hz=10;policy.rf.accuracy_ppm=100;policy.lf.accuracy_ppm=100;
    const auto capped=modem::oscillator_pattern_search(config);
    check(capped.limited && capped.hypotheses.size()<=modem::maximum_pattern_frequency_rate_hypotheses,
          "long independent regions retain explicit cap-limited coverage");
    config.integration_seconds=128;config.carrier_hz=31.25;
    const auto edge=modem::oscillator_pattern_search(config);
    check(edge.limited && edge.hypotheses.front()==modem::PatternFrequencyRateHypothesis{0,0},
          "passband-trimmed independent regions retain nominal reception and honest coverage limits");
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
// Deliberately enumerate the former Cartesian construction as an independent
// oracle for the bounded interval implementation, including its exact ordering
// and direct floating-point boundary comparisons.
void compare_enumerated_independent_bank(const modem::Config& config) {
    const auto actual=modem::oscillator_pattern_search(config);
    const auto& policy=*config.oscillator_search;
    const auto clock_bound=static_cast<double>(static_cast<long double>(policy.margin)*policy.lf.accuracy_ppm);
    const auto rf_bound=static_cast<double>(static_cast<long double>(policy.margin)*policy.rf_shift_hz*
        policy.rf.accuracy_ppm*1e-6L);
    check(clock_bound>0 && rf_bound>0,"oracle exercises independent frequency and clock dimensions");
    const auto width=static_cast<double>(static_cast<long double>(config.carrier_hz)*clock_bound*1e-6L+rf_bound);
    const auto requested_steps=std::ceil(static_cast<long double>(width)/
        (.25*config.sample_rate/static_cast<double>(modem::symbol_sample_count(config))));
    const auto step=static_cast<double>(static_cast<long double>(width)/requested_steps);
    check(actual.frequency.step_hz==step && actual.frequency.requested_half_width_hz==width,
          "independent frequency resolution and requested bounds are unchanged");
    const auto fitting=[](double limit,double spacing,std::size_t cap) {
        const auto possible=std::floor(static_cast<long double>(limit)/spacing);
        auto count=possible>=cap?cap:static_cast<std::size_t>(possible);
        while(count && static_cast<double>(count)*spacing>limit)--count;
        return count;
    };
    const auto endpoint=[&](std::size_t count) {
        return count==requested_steps?width:std::min(static_cast<double>(count)*step,width);
    };
    const auto offsets=[&](std::size_t count) {
        std::vector<double> values{0};values.reserve(1+2*count);
        for(std::size_t index=1;index<=count;++index) {
            values.push_back(-endpoint(index));values.push_back(endpoint(index));
        }
        return values;
    };
    constexpr auto maximum_steps=(modem::maximum_pattern_frequency_hypotheses-1)/2;
    auto steps=requested_steps>=maximum_steps?maximum_steps:static_cast<std::size_t>(requested_steps);
    const auto limit=modem::pattern_frequency_offset_limit(config);
    if(width>limit)steps=std::min(steps,fitting(limit,step,maximum_steps));
    const auto requested_rates=std::ceil(static_cast<long double>(clock_bound)/
        (.25*static_cast<double>(modem::pattern_chip_samples(config))/
         static_cast<double>(modem::symbol_sample_count(config))*1e6));
    const auto rate_step=static_cast<double>(static_cast<long double>(clock_bound)/requested_rates);
    const auto timing_step=.25*static_cast<double>(modem::pattern_chip_samples(config))/
        static_cast<double>(modem::symbol_sample_count(config))*1e6;
    if(clock_bound<=10000 && (static_cast<long double>(step)+2.L*rf_bound)/config.carrier_hz*1e6L<timing_step) {
        check(actual.hypotheses.size()==actual.frequency.count,"thin independent oracle cases use one timing lane per frequency");
        if(!actual.limited)check_independent_cell_coverage(config,actual);
        return;
    }
    check(actual.clock_step_ppm==rate_step,"independent timing resolution is unchanged");
    constexpr auto maximum_rates=(modem::maximum_pattern_rate_hypotheses-1)/2;
    auto rate_steps=requested_rates>=maximum_rates?maximum_rates:static_cast<std::size_t>(requested_rates);
    if(clock_bound>10000)rate_steps=std::min(rate_steps,fitting(10000,rate_step,maximum_rates));
    std::vector<double> rates{0};
    for(std::size_t index=1;index<=rate_steps;++index) {
        const auto rate=index==requested_rates?clock_bound:std::min(static_cast<double>(index)*rate_step,clock_bound);
        rates.push_back(-rate);rates.push_back(rate);
    }
    const auto half_band=modem::pattern_pulse_enabled(config)?
        (1+modem::pattern_pulse_rolloff)*config.sample_rate/(2.*static_cast<double>(modem::pattern_chip_samples(config))):
        config.bandwidth_hz/2;
    const auto nyquist=config.sample_rate/2.;
    const auto tolerance=4*(std::nextafter(nyquist,std::numeric_limits<double>::infinity())-nyquist);
    const auto radius=rf_bound+config.carrier_hz*rate_step*.5e-6+step;
    const auto enumerate=[&](std::size_t count) {
        std::vector<modem::PatternFrequencyRateHypothesis> pairs;
        const auto frequencies=offsets(count);
        for(auto rate:rates)for(auto offset:frequencies) {
            const auto support=static_cast<long double>(half_band)*(1+static_cast<long double>(rate)*1e-6L);
            const auto carrier=static_cast<long double>(config.carrier_hz)+offset;
            if(std::abs(offset-config.carrier_hz*rate*1e-6)<=radius &&
                carrier-support>=-tolerance && carrier+support<=nyquist+tolerance)pairs.push_back({offset,rate});
        }
        return pairs;
    };
    if(enumerate(steps).size()>modem::maximum_pattern_frequency_rate_hypotheses) {
        std::size_t lower=0,upper=steps;
        while(lower<upper) {
            const auto middle=lower+(upper-lower+1)/2;
            if(enumerate(middle).size()<=modem::maximum_pattern_frequency_rate_hypotheses)lower=middle;
            else upper=middle-1;
        }
        steps=lower;
    }
    auto expected=enumerate(steps);
    std::vector<bool> positive(steps+1),negative(steps+1);
    for(auto pair:expected) {
        const auto index=std::min(steps,static_cast<std::size_t>(std::llround(std::abs(pair.frequency_offset_hz)/step)));
        (pair.frequency_offset_hz<0?negative:positive)[index]=true;
    }
    for(std::size_t index=1;index<=steps;++index)
        if(!positive[index] || !negative[index]) {steps=index-1;break;}
    std::erase_if(expected,[&](auto pair){return std::abs(pair.frequency_offset_hz)>endpoint(steps);});
    if(expected.empty()) {expected.push_back({0,0});steps=0;}
    double maximum_clock=0;
    for(auto pair:expected)maximum_clock=std::max(maximum_clock,std::abs(pair.clock_error_ppm));
    check(actual.hypotheses==expected,"bounded construction preserves every exact paired hypothesis and its order");
    check(actual.frequency.count==1+2*steps && actual.frequency.half_width_hz==endpoint(steps) &&
          actual.clock_half_width_ppm==maximum_clock,"bounded construction preserves pair-cap and boundary coverage");
}
void bounded_independent_construction() {
    auto config=oscillator_config();auto& policy=*config.oscillator_search;
    for(auto shift:{10000.,1000000.,30000000.})for(auto duration:{1.3,128.,86400.}) {
        policy.rf_shift_hz=shift;config.integration_seconds=duration;
        compare_enumerated_independent_bank(config);
    }
    policy.rf_shift_hz=1000000;config.integration_seconds=82;
    for(auto accuracy:{.0001,100.,10000.})for(auto margin:{1.,3.}) {
        policy.lf.accuracy_ppm=accuracy;policy.rf.accuracy_ppm=.0001;policy.margin=margin;
        compare_enumerated_independent_bank(config);
    }
    policy.margin=1;policy.lf.accuracy_ppm=100;policy.rf.accuracy_ppm=.0001;
    config.sample_rate=400;config.bandwidth_hz=100;
    for(auto carrier:{31.25,31.251,168.749,168.75}) {
        config.carrier_hz=carrier;compare_enumerated_independent_bank(config);
    }
    config.sample_rate=6000;config.bandwidth_hz=1300;config.carrier_hz=1500;
    config.spreading_mode=modem::SpreadingMode::tone;config.integration_seconds=.25;
    policy.rf_shift_hz=1600000;policy.rf.accuracy_ppm=100;
    compare_enumerated_independent_bank(config);
    config=oscillator_config();config.sample_rate=400;config.bandwidth_hz=140;
    config.carrier_hz=200-41.666666666666664;
    config.oscillator_search->margin=1;config.oscillator_search->lf.accuracy_ppm=1e-10;
    config.oscillator_search->rf_shift_hz=1000000;config.oscillator_search->rf.accuracy_ppm=1e-15;
    compare_enumerated_independent_bank(config);
}
}
int main() {
    try {
        unchanged_short_searches();long_pattern_coverage();short_carrier_passband_boundaries();quantization_and_endpoints();
        tones_and_invalid_configurations();projected_bin_coverage();
        oscillator_bounds_and_phase();exact_declared_lattice_endpoints();shared_radio_frequency_mapping();zero_shift_uses_only_baseband();independent_reference_geometry();
        short_and_bounded_oscillator_searches();invalid_oscillator_policies();
        joint_passband_and_quantized_tones();thin_independent_regions();bounded_independent_construction();
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    std::cout<<"pattern search tests passed\n";
}
