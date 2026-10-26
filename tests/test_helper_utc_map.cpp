#include "../src/live_helper_prior.hpp"
#include <iostream>
#include <set>
using namespace datapump;
void check(bool value,const char* text){if(!value)throw Error(text);}
int main(){try {
    constexpr double H=5275,D=1500;
    modem::Config config;config.sample_rate=14400;config.carrier_hz=D;config.bandwidth_hz=360;
    config.pattern_symbols=true;config.constellation_bits=1;config.spreading_factor=64;
    config.scramble=true;config.dsss=true;config.dsss_factor=10;
    config.oscillator_search=modem::OscillatorSearchConfig{{100,0},{1,0},30000000,3};
    const auto extra=live::detail::helper_utc_frequency_allowance(config,true,H);
    check(extra.valid&&extra.radius_hz>=1.58297475&&extra.radius_hz<1.583,
          "UTC helper/data mapping term was missing or incorrectly scaled");
    const auto physical=extra.physical_audio_fraction,correction=extra.correction_fraction;
    for(const auto a:{-physical,0.,physical})for(const auto c:{-correction,0.,correction}) {
        const long double b=(1+static_cast<long double>(a))*(1+static_cast<long double>(c))-1;
        const auto omitted=static_cast<long double>(H)*(b-a);
        check(std::abs(omitted)<=extra.radius_hz,"UTC mapping allowance missed a physical audio/correction endpoint");
    }
    const auto original=modem::oscillator_pattern_search(config,modem::utc_transmit_rate_limit(config));
    check(!original.limited&&!original.hypotheses.empty(),"ordinary full RF/LF catalogue unavailable");
    std::set<double> clocks;for(const auto& h:original.hypotheses)clocks.insert(h.clock_error_ppm);
    std::size_t checked=0;
    const auto rf_radius=.125*config.sample_rate/modem::symbol_sample_count(config);
    for(const auto a:{-300e-6,0.,300e-6})for(const auto c:{-300e-6,0.,300e-6}) {
        constexpr double rf=30,raw_radius=.01;
        const long double b=(1+static_cast<long double>(a))*(1+static_cast<long double>(c))-1;
        const long double data=rf+D*b;
        const auto helper=rf+H*a;
        const auto expanded=live::detail::helper_prior_detail::sum(raw_radius,extra.radius_hz).upper;
        const auto mask=live::detail::helper_prior_mask(original.hypotheses,helper,expanded,D-H,rf_radius,
            original.clock_step_ppm/2,original.requested_clock_half_width_ppm);
        check(mask.admitted&&mask.rates==clocks.size()&&mask.represented_rates==clocks.size(),
              "corrected RF prior lost an ordinary DATA LF alternative");
        bool covered=false;
        for(std::size_t i=0;i<original.hypotheses.size();++i)if(mask.cells[i]) {
            const auto& h=original.hypotheses[i];
            covered|=std::abs(static_cast<long double>(h.clock_error_ppm)-b*1e6L)<=original.clock_step_ppm/2&&
                std::abs(static_cast<long double>(h.frequency_offset_hz)+mask.origin_hz-data)<=rf_radius;
        }
        check(covered,"corrected original RF/LF cell union omitted a jointly physical UTC point");++checked;
    }
    check(live::detail::helper_utc_frequency_allowance(config,false,H).radius_hz==0,
          "disabled UTC steering gained helper uncertainty");
    config.oscillator_search->reference=modem::OscillatorReference::shared_radio;
    const auto shared=live::detail::helper_utc_frequency_allowance(config,true,H);
    check(shared.valid&&shared.radius_hz==0&&modem::utc_transmit_rate_limit(config)==0,
          "coupled shared-radio helper catalogue gained independent UTC steering");
    check(!live::detail::helper_utc_frequency_allowance(config,true,
        std::numeric_limits<double>::quiet_NaN()).valid,"invalid helper mapping was admitted");
    std::cout<<"utc_helper_map extra_hz="<<extra.radius_hz<<" original_pairs="<<original.hypotheses.size()
        <<" retained_clock_rates="<<clocks.size()<<" joint_boundary_points="<<checked
        <<" shared_radio_correction=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
