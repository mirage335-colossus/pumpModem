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
#include "../frequency_parse.hpp"
#include <vector>
#include <span>
#include <charconv>
#include <limits>

namespace datapump::gui::fhss {
inline constexpr std::size_t list_limit=200,list_byte_limit=8192;
// Pure argument/editor parsing. File access is confined to the controller.
inline std::vector<double> parse_list(std::string_view text) {
    if(text.size()>list_byte_limit)throw std::invalid_argument("FHSS list exceeds 8192 bytes");
    std::string uncommented(text);
    for(std::size_t at=0;(at=uncommented.find('#',at))!=std::string::npos;) {
        const auto end=uncommented.find('\n',at);
        uncommented.erase(at,end==std::string::npos?end:end-at);
    }
    text=uncommented;
    std::vector<double> result;
    while(!text.empty()) {
        const auto end=text.find_first_of(",;\n");
        auto item=text.substr(0,end);
        if(end==std::string_view::npos)text={};else text.remove_prefix(end+1);
        if(const auto comment=item.find('#');comment!=std::string_view::npos)item=item.substr(0,comment);
        while(!item.empty()&&std::isspace(static_cast<unsigned char>(item.front())))item.remove_prefix(1);
        while(!item.empty()&&std::isspace(static_cast<unsigned char>(item.back())))item.remove_suffix(1);
        if(item.empty())continue;
        const auto hz=frequency_input::parse(item);
        if(!hz||*hz<0)throw std::invalid_argument("FHSS list requires nonnegative finite Shift frequencies in Hz, kHz, MHz, GHz or THz");
        if(std::find(result.begin(),result.end(),*hz)!=result.end())throw std::invalid_argument("FHSS list contains a duplicate frequency");
        if(result.size()==list_limit)throw std::invalid_argument("FHSS list permits at most 200 frequencies");
        result.push_back(*hz);
    }
    if(result.size()<2)throw std::invalid_argument("FHSS list requires 2–200 distinct frequencies");
    return result;
}
inline std::string list_text(std::span<const double> list,char separator=',') {
    std::string result;
    for(const auto hz:list) {
        char buffer[32];const auto number=std::to_chars(buffer,buffer+sizeof(buffer),hz,
            std::chars_format::general,std::numeric_limits<double>::max_digits10);
        if(number.ec!=std::errc{})throw std::invalid_argument("FHSS frequency formatting failed");
        if(!result.empty())result+=separator;
        result.append(buffer,number.ptr);
    }
    return result;
}
enum class Preference { off, fcc, eu, list };
inline Preference parse(std::string_view id) {
    if(id=="off")return Preference::off;
    // The original illustration had no jurisdiction. Migrate to FCC once;
    // the preference is subsequently independent of the resolved RF band.
    if(id=="fake-fcc"||id=="fake-0.4s-200")return Preference::fcc;
    if(id=="fake-eu")return Preference::eu;
    if(id=="fake-list")return Preference::list;
    throw std::invalid_argument("FHSS must be off, fake-fcc, fake-eu or fake-list.");
}
inline std::string_view id(Preference p) {
    return p==Preference::fcc?"fake-fcc":p==Preference::eu?"fake-eu":p==Preference::list?"fake-list":"off";
}
struct Band {double lower_hz=0,upper_hz=0;};
struct Profile {
    Preference preference=Preference::off;
    bool available=false,experimental=false,observer_disjoint=true;
    std::vector<double> centers;
    double center(unsigned index) const {return centers.empty()?first_hz+index*spacing_hz:centers.at(index);}
    std::string name,reason,assumptions;
    Band band;
    unsigned channels=0;
    double occupied_hz=0,guarded_hz=0,spacing_hz=0,dwell_seconds=.4;
    double configured_rf_hz=0,first_hz=0,last_hz=0,span_hz=0;
    double occupancy_seconds=0,occupancy_window_seconds=0;
    double return_deadline_seconds=0;
    std::optional<lpi::Hopping> observer() const {
        if(!available||!observer_disjoint)return std::nullopt;
        return lpi::Hopping{channels,dwell_seconds};
    }
};
// Presentation/planning only. No profile changes PCM, tuning, cryptographic
// addresses or modem admission. These are engineering illustrations, never a
// measured RF emission mask or compliance certificate. See maintained sources
// and profile/access limitations in docs/spread-spectrum-controls.md.
inline Profile resolve(Preference preference,const modem::Config& config,
                       std::optional<Band> limits=std::nullopt,std::span<const double> list={}) {
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
    // Fake power gates add smooth modulation skirts. Reserve the accepted
    // 200 Hz full-band allowance in illustrated channel spacing/band edges.
    // This engineering margin is not a measured emission mask.
    p.guarded_hz=1.25*std::max(p.occupied_hz,modem::waveform_bandwidth_hz(config))+200.;
    const auto fail=[&](std::string reason){p.reason=std::move(reason);return p;};
    if(!std::isfinite(p.configured_rf_hz)||!std::isfinite(p.guarded_hz)||p.guarded_hz<=0)
        return fail("Invalid RF or waveform bandwidth");
    const auto rf=p.configured_rf_hz;
    if(preference==Preference::list) {
        p.name="Developer Shift list";p.experimental=true;
        if(list.size()<2||list.size()>list_limit)return fail("Enter and apply 2–200 frequencies in the FHSS tab");
        // List entries are translation/LO Shift frequencies. Add the unchanged
        // Doppler-adjusted audio carrier only for RF/planner geometry.
        for(const auto value:list) {
            if(!std::isfinite(value)||value<0)return fail("Invalid list Shift frequency");
            p.centers.push_back(value+config.carrier_hz);
        }
        auto sorted=p.centers;
        std::sort(sorted.begin(),sorted.end());p.first_hz=sorted.front();p.last_hz=sorted.back();
        p.span_hz=p.last_hz-p.first_hz;p.channels=static_cast<unsigned>(list.size());
        p.spacing_hz=std::numeric_limits<double>::infinity();
        for(std::size_t i=0;i<sorted.size();++i) {
            if(!std::isfinite(sorted[i])||sorted[i]<=0)return fail("Invalid list frequency");
            if(i)p.spacing_hz=std::min(p.spacing_hz,sorted[i]-sorted[i-1]);
        }
        if(p.spacing_hz<=0)return fail("Duplicate list frequencies");
        if(p.first_hz<config.carrier_hz||p.first_hz<p.guarded_hz/2)
            return fail("List would place Shift or guarded waveform below zero");
        if(limits&&(!std::isfinite(limits->lower_hz)||!std::isfinite(limits->upper_hz)||limits->lower_hz>=limits->upper_hz))
            return fail("Invalid operating-band limits");
        if(limits&&(p.first_hz-p.guarded_hz/2<limits->lower_hz||p.last_hz+p.guarded_hz/2>limits->upper_hz))
            return fail("Complete guarded list does not fit the operating band");
        p.observer_disjoint=p.spacing_hz>=p.guarded_hz;
        p.assumptions="Custom Shift frequencies; RF centers add the unchanged audio carrier; 0.4 s residence, no regulatory profile. Spacing is the minimum adjacent gap.";
        if(!p.observer_disjoint)p.assumptions+=" Overlapping guarded channels: hopping observer model unavailable; fixed-band observer estimate retained.";
        p.available=true;return p;
    }
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
        p.assumptions+=" Guarded waveform width substitutes for unmeasured 20 dB width; fresh keyed permutation per cycle. Sliding occupancy limits are requirements, not enforced by this Fake schedule.";
    } else {
        if(rf>=863e6&&rf<868.6e6) {
            p.name="EU 863–870 MHz national SRD";p.band={863e6,868.6e6};p.channels=47;
            if(p.guarded_hz>100000)return fail("Guarded channel exceeds national sub-GHz 100 kHz width proxy");
            p.spacing_hz=100000;p.return_deadline_seconds=20;
            p.assumptions="National availability required, not EU-wide harmonised; 25 mW e.r.p., 0.1% whole-transmission duty required, not implemented. Alarm subbands excluded by using 863–868.6 MHz. Return <=20 s required; fresh-cycle illustration can take 37.2 s, not enforced. Observer estimate conditional on on-air dwell; duty gaps excluded.";
        } else if(rf>=2400e6&&rf<=2483.5e6) {
            p.name="EU 2.4 GHz adaptive";p.band={2400e6,2483.5e6};p.channels=79;
            p.spacing_hz=std::max(1e6,p.guarded_hz);
            p.occupancy_seconds=.4;
            p.occupancy_window_seconds=.4*std::max(15.,std::ceil(15e6/p.spacing_hz));
            p.assumptions="100 mW e.i.r.p.; adaptive CCA/DAA required, not implemented. 400 ms/channel in 6 s (minimum required 15 channels) required, not enforced by the fresh-cycle illustration; at least 70% band coverage. LBT bursts <=60 ms plus >=5% idle (minimum 100 us). Observer estimate conditional on on-air dwell; access gaps excluded.";
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
