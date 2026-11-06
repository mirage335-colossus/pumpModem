#include "datapump/live.hpp"
#include "datapump/background_noise.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    using namespace datapump;
    try {
        live::Settings settings;
        settings.background_noise.mode=background_noise::Mode::EffectiveBits;
        settings.background_noise.value=10;
        live::validate_settings(settings);
        const auto playback=live::detail::playback_output_options(settings,.375);
        if(playback.transmit_gain!=.375||playback.background_noise.mode!=background_noise::Mode::EffectiveBits||
           playback.background_noise.value!=10)
            throw std::runtime_error("live playback options dropped the configured background-noise level");

        settings.simulation=true;
        bool rejected=false;
        try { live::validate_settings(settings); }
        catch(const std::exception& error) {
            rejected=std::string(error.what()).find("full-rate background noise")!=std::string::npos;
        }
        if(!rejected)throw std::runtime_error("narrowband live simulation silently accepted full-rate DAC noise");

        settings.simulation=false;
        settings.background_noise={};
        settings.background_noise.mode=background_noise::Mode::RelativeWatts;
        settings.background_noise.value=.001;
        settings.background_noise.reference_mean_square=.25;
        settings.background_noise.reference_watts=40;
        live::validate_settings(settings);
        const auto relative_playback=live::detail::playback_output_options(settings,.625);
        if(relative_playback.transmit_gain!=.625||
           relative_playback.background_noise.mode!=background_noise::Mode::RelativeWatts||
           relative_playback.background_noise.reference_mean_square!=.25||
           relative_playback.background_noise.reference_watts!=40)
            throw std::runtime_error("live playback options changed relative-watts references");

        settings.background_noise.reference_mean_square=1.01;
        rejected=false;
        try { live::validate_settings(settings); }
        catch(const std::exception&) { rejected=true; }
        if(!rejected)throw std::runtime_error("relative-watts reference above normalized DAC full scale was accepted");
        std::cout<<"live full-rate background-noise settings passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
