#pragma once
#include "datapump/modem.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/lpi_estimate.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>

namespace datapump::gui::fhss {
enum class Preference { off, fcc, eu };
inline Preference parse(std::string_view id) {
    if(id=="off")return Preference::off;
    // The original illustration had no jurisdiction. Migrate to FCC once;
    // the preference is subsequently independent of the resolved RF band.
    if(id=="fake-fcc"||id=="fake-0.4s-200")return Preference::fcc;
    if(id=="fake-eu")return Preference::eu;
    throw std::invalid_argument("FHSS must be off, fake-fcc or fake-eu.");
}
inline std::string_view id(Preference p) {
    return p==Preference::fcc?"fake-fcc":p==Preference::eu?"fake-eu":"off";
}
struct Band {double lower_hz=0,upper_hz=0;};
struct Profile {
    Preference preference=Preference::off;
    bool available=false,experimental=false;
    std::string name,reason,assumptions;
    Band band;
    unsigned channels=0;
    double occupied_hz=0,guarded_hz=0,spacing_hz=0,dwell_seconds=.4;
    double configured_rf_hz=0,first_hz=0,last_hz=0,span_hz=0;
    double occupancy_seconds=0,occupancy_window_seconds=0;
    double return_deadline_seconds=0;
    std::optional<lpi::Hopping> observer() const {
        if(!available)return std::nullopt;
        return lpi::Hopping{channels,dwell_seconds};
    }
};
// Presentation/planning only. No profile changes PCM, tuning, cryptographic
// addresses or modem admission. These are engineering illustrations, never a
// measured RF emission mask or compliance certificate. See maintained sources
// and profile/access limitations in docs/spread-spectrum-controls.md.
inline Profile resolve(Preference preference,const modem::Config& config,
                       std::optional<Band> limits=std::nullopt) {
    Profile p;p.preference=preference;
    if(preference==Preference::off){p.name="Off";return p;}
    const double shift=config.oscillator_search?config.oscillator_search->rf_shift_hz:0;
    p.configured_rf_hz=config.carrier_hz+shift; // Includes manual Doppler.
    p.occupied_hz=modem::pattern_pulse_enabled(config)?
        (1+modem::pattern_pulse_rolloff)*config.sample_rate/static_cast<double>(modem::pattern_chip_samples(config)):
        modem::waveform_bandwidth_hz(config);
    // The planner may change pulse eligibility at a fractional rate/short
    // symbol boundary. Nominal width bounds the sampled shaped width and the
    // rectangular alternative, so the hop set stays valid across that sweep.
    // This conservative envelope is still proportional to signal bandwidth.
    p.guarded_hz=1.25*std::max(p.occupied_hz,modem::waveform_bandwidth_hz(config));
    const auto fail=[&](std::string reason){p.reason=std::move(reason);return p;};
    if(!std::isfinite(p.configured_rf_hz)||!std::isfinite(p.guarded_hz)||p.guarded_hz<=0)
        return fail("Invalid RF or waveform bandwidth");
    const auto rf=p.configured_rf_hz;
    if(rf<800e6) {
        p.experimental=true;p.name="Experimental below 800 MHz";
        p.band={0,800e6};p.channels=200;p.spacing_hz=p.guarded_hz;
        p.assumptions="Product convention, no regulatory profile; nominal/shaped width envelope plus 25% margin, not a measured emission mask.";
    } else if(preference==Preference::fcc) {
        p.spacing_hz=std::max(25000.,p.guarded_hz);
        if(rf>=902e6&&rf<=928e6) {
            p.name="FCC 902–928 MHz";p.band={902e6,928e6};
            if(p.guarded_hz>500000)return fail("Guarded channel exceeds FCC 902 MHz 500 kHz width proxy");
            p.channels=p.guarded_hz<250000?50:25;
            p.assumptions=p.channels==50?"1 W conducted ceiling before antenna restrictions; 0.4 s/channel in 20 s.":
                "250 mW conducted ceiling before antenna restrictions; 0.4 s/channel in 10 s.";
        } else if(rf>=2400e6&&rf<=2483.5e6) {
            p.name="FCC 2.4 GHz";p.band={2400e6,2483.5e6};p.channels=75;
            if(74*p.spacing_hz+p.guarded_hz>83.5e6)p.channels=15;
            p.assumptions=p.channels==75?"75 nonoverlapping channels; 1 W conducted ceiling before antenna restrictions.":
                "15 channels; 125 mW conducted ceiling before antenna restrictions.";
        } else if(rf>=5725e6&&rf<=5850e6) {
            p.name="FCC 5.8 GHz";p.band={5725e6,5850e6};p.channels=75;
            if(p.guarded_hz>1e6)return fail("Guarded channel exceeds FCC 5.8 GHz 1 MHz width proxy");
            p.assumptions="1 W conducted ceiling before antenna restrictions; 0.4 s/channel in 30 s.";
        } else return fail("No FCC hopping profile for this RF band");
        p.occupancy_seconds=.4;p.occupancy_window_seconds=.4*p.channels;
        p.assumptions+=" Guarded waveform width substitutes for unmeasured 20 dB width; equal-use cyclic illustration.";
    } else {
        if(rf>=863e6&&rf<868.6e6) {
            p.name="EU 863–870 MHz national SRD";p.band={863e6,868.6e6};p.channels=47;
            if(p.guarded_hz>100000)return fail("Guarded channel exceeds national sub-GHz 100 kHz width proxy");
            p.spacing_hz=100000;p.return_deadline_seconds=20;
            p.assumptions="National availability required, not EU-wide harmonised; 25 mW e.r.p., 0.1% whole-transmission duty required, not implemented. Alarm subbands excluded by using 863–868.6 MHz. Return <=20 s. Observer estimate conditional on on-air dwell; duty gaps excluded.";
        } else if(rf>=2400e6&&rf<=2483.5e6) {
            p.name="EU 2.4 GHz adaptive";p.band={2400e6,2483.5e6};p.channels=79;
            p.spacing_hz=std::max(1e6,p.guarded_hz);
            p.occupancy_seconds=.4;
            p.occupancy_window_seconds=.4*std::max(15.,std::ceil(15e6/p.spacing_hz));
            p.assumptions="100 mW e.i.r.p.; adaptive CCA/DAA required, not implemented. 400 ms/channel in 6 s (minimum required 15 channels); at least 70% band coverage. LBT bursts <=60 ms plus >=5% idle (minimum 100 us). Observer estimate conditional on on-air dwell; access gaps excluded.";
        } else if(rf>=5725e6&&rf<=5875e6) {
            p.name="EU 5.8 GHz SRD";p.band={5725e6,5875e6};p.channels=20;
            p.spacing_hz=std::max(7.2e6,p.guarded_hz);
            p.return_deadline_seconds=4*p.dwell_seconds*p.channels;
            p.assumptions="25 mW e.i.r.p.; >90% of 150 MHz covered; >=20 channels, dwell <=1 s, every channel within 4 × dwell × N. No generic duty restriction in this SRD entry.";
        } else return fail("No implemented EU hopping profile for this RF band (sub-GHz rules differ)");
    }
    if(limits) {
        if(!std::isfinite(limits->lower_hz)||!std::isfinite(limits->upper_hz)||limits->lower_hz>=limits->upper_hz)
            return fail("Invalid operating-band limits");
        p.band.lower_hz=std::max(p.band.lower_hz,limits->lower_hz);
        p.band.upper_hz=std::min(p.band.upper_hz,limits->upper_hz);
    }
    // Keep the complete guarded hop set in band and every displayed Shift >=0.
    const double low=std::max(p.band.lower_hz+p.guarded_hz/2,config.carrier_hz);
    const double high=p.band.upper_hz-p.guarded_hz/2;
    if(p.experimental&&high>=low)
        p.channels=static_cast<unsigned>(std::min(200.,1+std::floor((high-low)/p.spacing_hz)));
    p.span_hz=(p.channels-1)*p.spacing_hz;
    if(p.channels<2||high<low||p.span_hz>high-low)
        return fail("Complete guarded hop set does not fit the operating band");
    p.first_hz=std::clamp(rf,low,high-p.span_hz);p.last_hz=p.first_hz+p.span_hz;
    p.available=true;return p;
}
}
