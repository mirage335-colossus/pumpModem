#include "../src/live_helper.hpp"
#include <iostream>

using namespace datapump;
void check(bool v,const char* text){if(!v)throw Error(text);}
helper::Reference observation(double hertz,std::uint64_t at) {
    helper::Reference r;r.sample=at;r.sample_rate=14400;
    r.frequency_sample=at-1;r.frequency_first_sample=at-576;
    r.frequency_hz=hertz;r.frequency_uncertainty_hz=.01;
    r.frequency_valid=true;r.frequency_fit=helper::FrequencyFit::local_polynomial;
    // No punctuation, phase, metadata, UTC or time code is provided.
    return r;
}
int main(){try {
    constexpr double helper_hz=5275,rf=30000000;
    for(const auto side:{modem::OscillatorSideband::upper,modem::OscillatorSideband::lower}) {
        modem::Config c;c.sample_rate=14400;c.carrier_hz=1500;
        c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{1,0},rf,3};
        c.oscillator_search->reference=modem::OscillatorReference::independent_audio;c.oscillator_search->sideband=side;
        const auto sign=side==modem::OscillatorSideband::upper?1.:-1.;
        const auto a=observation(helper_hz+sign*rf*1e-6+helper_hz*100e-6,14400);
        const auto b=observation(helper_hz+sign*rf*.5e-6+helper_hz*100e-6,28800);
        const auto first=live::detail::helper_current_link_measurement(c,helper_hz,a,a.sample+1,1);
        const auto later=live::detail::helper_current_link_measurement(c,helper_hz,b,b.sample+1,1);
        check(first.authority==helper::LinkAuthority::current_measurement&&
              later.authority==helper::LinkAuthority::current_measurement,"valid moving current reference was frozen or declined");
        check(live::detail::helper_current_link_measurement(c,helper_hz,a,a.sample,1).authority==
              helper::LinkAuthority::declined,"exclusive captured frontier admitted an uncaptured publication");
        check(std::abs(a.frequency_hz-b.frequency_hz)>a.frequency_uncertainty_hz+b.frequency_uncertainty_hz,
              "test mean did not leave the first Hz interval");
        check(later.audio_fraction.lower<=-300e-6&&later.audio_fraction.upper>=300e-6,
              "one helper frequency removed independent physical audio ambiguity");
        check(!later.phase_bridge_allowed,"frequency-only observation manufactured phase continuity");
        auto outside=observation(helper_hz+sign*rf*4e-6,43200);
        check(live::detail::helper_current_link_measurement(c,helper_hz,outside,outside.sample+1,1).authority==
              helper::LinkAuthority::declined,"current reference escaped the static physical RF/audio envelope");
        auto unknown=a;unknown.frequency_fit=helper::FrequencyFit::unknown;
        check(live::detail::helper_current_link_measurement(c,helper_hz,unknown,unknown.sample+1,1).authority==
              helper::LinkAuthority::declined,"coarse discovery seed was treated as a fine measured fit");
        std::cout<<"current_reference side="<<sign<<" moved_hz="<<b.frequency_hz-a.frequency_hz
            <<" physical_audio_ppm=["<<later.audio_fraction.lower*1e6<<','<<later.audio_fraction.upper*1e6
            <<"] original_data_clock_union_unchanged\n";
    }
    std::cout<<"private current-link caller guards passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
