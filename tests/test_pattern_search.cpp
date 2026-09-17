#include "datapump/pattern_search.hpp"
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
}
int main() {
    try {
        unchanged_short_searches();long_pattern_coverage();short_carrier_passband_boundaries();quantization_and_endpoints();
        tones_and_invalid_configurations();projected_bin_coverage();
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    std::cout<<"pattern search tests passed\n";
}
