#include "datapump/helper_carrier.hpp"
#include "datapump/modem.hpp"
#include "datapump/types.hpp"
#include "helper_power_reference_checks.hpp"
#include <openssl/err.h>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

namespace h=datapump::helper;
namespace {
constexpr double tau=2*std::numbers::pi;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f) {
    try {f();}catch(const datapump::Error&) {return;}
    throw std::runtime_error("invalid helper configuration accepted");
}
std::complex<double> oscillator(double frequency,long double time) {
    return std::polar(1.,tau*static_cast<double>(std::fmod(static_cast<long double>(frequency)*time,1.L)));
}
void apsk_constellation() {
    require(std::abs(h::apsk_point(0)-std::complex<double>{.2,0})<1e-14,"APSK inner radius is not 20 percent");
    require(std::abs(h::apsk_point(127U<<7)-std::complex<double>{1,0})<1e-14,"APSK outer radius changed");
    require(std::abs(h::apsk_point(32)-std::complex<double>{0,.2})<1e-14,"APSK quadrant mapping changed");
    rejects([]{h::apsk_point(1U<<14);});
    for(std::uint32_t word=0;word<(1U<<14);++word) {
        const auto point=h::apsk_point(word);
        const auto ring=std::lround((std::abs(point)-.2)*127/.8);
        const auto sector=std::lround(std::arg(point)*128/tau);
        require(((static_cast<std::uint32_t>(ring)<<7)|(static_cast<std::uint32_t>(sector)&127U))==word,
            "APSK mapper does not round-trip all 16384 symbols");
    }
}
h::Generator pilot_only(const h::Plan& plan,std::uint64_t epoch=0) {
    auto pilot=plan;pilot.auxiliary_frequency_hz=0;
    h::Options options;options.power_w=1;options.timecode=false;
    return h::Generator(options,pilot,epoch,false);
}
std::complex<double> auxiliary_baseband(const h::Generator& generator,const h::Generator& pilot,
        const h::Plan& plan,long double time,std::uint32_t rate,
        std::optional<long double> end={},std::optional<long double> start={}) {
    return (generator.mix_analytic({},time,rate,end,start)-pilot.mix_analytic({},time,rate,end,start))/
        (plan.helper_gain*h::auxiliary_amplitude_fraction*oscillator(plan.auxiliary_frequency_hz,time));
}
// The metadata/reversal fixtures deliberately isolate the previous two-tone
// laboratory arrangement; production placement is tested independently below.
h::Plan metadata_fixture_plan(const h::Options& options,double carrier,double bandwidth,std::uint32_t) {
    const auto relative=std::sqrt(2*datapump::modem::nominal_signal_power*options.power_w/options.data_power_w);
    const auto gain=.95/(1+relative*(h::pilot_amplitude_fraction+h::auxiliary_amplitude_fraction));
    return {carrier+bandwidth/2+850,relative*gain,gain,options.power_w,
        carrier+bandwidth/2+450,0,true};
}
void power_and_placement() {
    h::Options options;
    const auto disabled=h::plan(options,1000,200,8000);
    require(disabled.helper_gain==0&&disabled.data_gain==1,"disabled helper changes gain");
    h::Generator off(options,disabled);
    for(const auto sample:{0.f,-0.f,.125f,-.83f,std::numeric_limits<float>::infinity()})
        require(std::bit_cast<std::uint32_t>(off.mix(sample,0,0))==std::bit_cast<std::uint32_t>(sample),
                "disabled mixer changed payload bits");
    options.power_w=40;options.data_power_w=4;options.timecode=false;
    const auto high=h::plan(options,1725,200,48000);
    require(high.low_frequency_hz==550&&high.auxiliary_frequency_hz==950&&high.frequency_hz==2400,
        "fixed low/auxiliary or 2400Hz high-pilot floor changed");
    require(high.effective_power_w==36,"configured total did not subtract Robust allocation");
    require(h::minimum_output_rate(options,1725,200,6000)==48000,
        "helper physical clock changed the logical source clock");
    const auto wider=h::plan(options,4000,3000,48000);
    require(wider.low_frequency_hz==550&&wider.auxiliary_frequency_hz==950&&wider.frequency_hz>=5950,
        "wide Robust signal moved fixed tones or failed to raise high pilot");
    const auto raised=h::plan(options,1725,200,48000,8250);
    require(raised.frequency_hz>=8250&&raised.low_frequency_hz==550&&raised.auxiliary_frequency_hz==950,
        "external placement floor moved low pilots");
    // Mean modulation energy and gain can change with the new record, but
    // matching options must never shift the independently placed tones.
    std::array<std::uint8_t,32> placement_key_bytes{};placement_key_bytes[0]=37;
    const datapump::Crypto placement_key(placement_key_bytes);
    for(const auto gating:{false,true})for(const auto geometry:
            std::array<std::array<double,2>,2>{{{1725,200},{4000,3000}}}) {
        const h::PlacementBand band{geometry[0],geometry[1],0,gating};
        auto blank=options;blank.timecode=false;blank.callsign.clear();
        const auto unchanged=h::plan(blank,band,48000);
        for(const auto name:{"","N0CALL","ABCDEFGHIJKLMNOP"})for(const auto keyed:{false,true}) {
            auto carrying=options;carrying.timecode=true;carrying.callsign=name;
            h::PowerReference reference;
            if(keyed)reference.time_key=placement_key;
            const auto actual=h::plan(carrying,band,48000,0,1,&reference);
            require(actual.frequency_hz==unchanged.frequency_hz&&
                    actual.low_frequency_hz==unchanged.low_frequency_hz&&
                    actual.auxiliary_frequency_hz==unchanged.auxiliary_frequency_hz,
                "metadata waveform changed Helper Carrier tone placement");
        }
    }
    rejects([&]{h::plan(options,1000,200,48000);});
    rejects([&]{h::plan(options,1725,200,4000);});
    rejects([&]{h::plan(options,1725,200,48000,-1);});
    rejects([&]{h::plan(options,1725,200,48000,0,0);});
    const auto below=h::plan(options,.5,1,48000);
    require(below.low_frequency_hz==550,"valid low baseband waveform lost the fixed pilots");
    const auto backed_off=h::plan(options,1725,200,48000,0,.5);
    require(backed_off.helper_gain==high.helper_gain*.5&&backed_off.data_gain==high.data_gain,
        "existing Robust source backoff changed relative calibration");
    require(high.data_gain+high.helper_gain*(h::pilot_amplitude_fraction+h::low_pilot_amplitude_fraction+
        h::auxiliary_amplitude_fraction)<=.950000000001,"three-tone composite lost peak headroom");
    require(std::abs(10*std::log10(h::low_pilot_power_fraction/h::pilot_power_fraction)+3)<1e-12&&
        std::abs(10*std::log10(h::auxiliary_power_fraction/h::pilot_power_fraction)+9)<1e-12,
        "steady component levels are not -3/-9dB relative to high pilot");
    auto exhausted=options;exhausted.power_w=exhausted.data_power_w;
    const auto no_helpers=h::plan(exhausted,1000,200,8000);
    require(no_helpers.helper_gain==0&&no_helpers.data_gain==1&&h::calibrated_data_gain(exhausted)==1&&
        h::minimum_output_rate(exhausted,1000,200,8000)==0,
        "zero helper remainder did not retain the ordinary payload gain/clock");
    const auto absent=h::reference_level(exhausted,10);
    require(absent.effective_power_ratio==0&&std::isinf(absent.straight_cn0_db_hz)&&absent.straight_cn0_db_hz<0,
        "zero helper remainder manufactured a finite reference level");
    options.ratio_limit_db=3;
    const auto capped=h::plan(options,1725,200,48000);
    require(std::abs(capped.effective_power_w-4*std::pow(10.,.3))<1e-12,
        "existing helper/data ratio ceiling changed");
    options.frequency=h::Frequency::low;rejects([&]{h::validate(options);});
    options.frequency=h::Frequency::cochannel;rejects([&]{h::validate(options);});
    options.frequency=h::Frequency::high;options.power_w=-1;rejects([&]{h::validate(options);});
    options.power_w=40;options.callsign="ABCDEFGHIJKLMNOPQ";rejects([&]{h::validate(options);});
    options.callsign=std::string(1,static_cast<char>(128));rejects([&]{h::validate(options);});
}
void active_average_and_continuous_pilots() {
    constexpr std::uint32_t rate=48000;
    std::array<std::uint8_t,32> key_bytes{};key_bytes[0]=23;
    const datapump::Crypto key(key_bytes);
    for(const auto gating:{false,true})for(const auto callsign:{"","N0CALL","ABCDEFGHIJKLMNOP"})for(const auto encrypted:{false,true})for(const auto measured:{false,true}) {
        h::Options options;options.power_w=40;options.data_power_w=4;options.callsign=callsign;
        h::PowerReference reference;reference.epoch=1800000000;reference.begin_seconds=.013L;reference.end_seconds=1.189L;
        reference.time_key=encrypted?std::optional<datapump::Crypto>{key}:std::nullopt;
        if(measured)reference.data_mean_power=datapump::modem::nominal_signal_power*.37;
        reference.active=[](long double t){return t<.1L||t>=.12L;};
        h::PlacementBand band{1725,200,0,gating};
        const auto plan=h::plan(options,band,rate,0,measured?.5:1,&reference);
        h::Generator whole(options,plan,reference.epoch,true,reference.time_key);
        auto high_plan=plan;high_plan.auxiliary_frequency_hz=0;high_plan.low_frequency_hz=0;
        h::Generator high(options,high_plan,reference.epoch,true,reference.time_key);
        auto pilots_plan=plan;pilots_plan.auxiliary_frequency_hz=0;
        h::Generator pilots(options,pilots_plan,reference.epoch,true,reference.time_key);
        double helper=0,data=0,high_energy=0,low_energy=0;std::size_t count=0;
        for(std::uint32_t i=0;i<rate*1.2;++i) {
            const auto t=static_cast<long double>(i)/rate;
            if(t<reference.begin_seconds||t>=reference.end_seconds||!reference.active(t))continue;
            const auto high_sample=high.mix_analytic({},t,rate);
            const auto low_sample=pilots.mix_analytic({},t,rate)-high_sample;
            const auto auxiliary=whole.mix_analytic({},t,rate)-pilots.mix_analytic({},t,rate);
            helper+=std::norm(high_sample)+std::norm(low_sample)+std::norm(auxiliary);
            data+=2*reference.data_mean_power.value_or(datapump::modem::nominal_signal_power)*plan.data_gain*plan.data_gain;
            high_energy+=std::norm(high_sample);low_energy+=std::norm(low_sample);++count;
        }
        require(count&&std::abs(data/(data+helper)-.1)<2e-5,
            "actual active helper gates/APSK did not establish the 40W/4W average allocation");
        require(std::abs(10*std::log10(low_energy/high_energy)+3)<1e-10,
            "steady low/high pilot power ratio changed");
        for(const auto boundary:{.4L,.8L,1.2L}) {
            const auto sample=high.mix_analytic({},boundary,rate);
            require(gating?std::abs(sample)<1e-10:std::abs(sample-plan.helper_gain*h::pilot_amplitude_fraction*
                oscillator(plan.frequency_hz,boundary))<1e-10,
                "straight pilot dwell gate disagrees with FHSS selection");
            if(!gating) {
                const auto t=boundary+1.L/rate;
                require(std::abs(high.mix_analytic({},t,rate)/sample-
                    oscillator(plan.frequency_hz,1.L/rate))<1e-10,
                    "non-FHSS straight pilot reset phase at a dwell boundary");
            }
        }
        require(whole.mix(0,.15L,rate,{},.15L)==0&&whole.mix(0,.15L,rate,.15L)==0,
            "continuous pilots lost shaped transmission endpoints");
    }
}
void metadata_vectors() {
    // Independent MSB-first vector: 14-bit time phase, then 7-bit ASCII pairs.
    constexpr std::array<std::uint32_t,9> expected{0,10032,8641,9804,0,0,0,0,0};
    const auto normal=h::metadata_packet(0,"N0CALL");
    require(normal.count==expected.size()&&normal.symbols==expected,
        "combined metadata wire vector changed");
    h::Options options;options.power_w=100;options.callsign="N0CALL";
    const auto plan=metadata_fixture_plan(options,1000,200,8000);
    h::Generator generator(options,plan);
    const auto pilot=pilot_only(plan);
    for(std::size_t i=0;i<expected.size();++i) {
        const auto time=.212L+(i+1)*.020L;
        const auto observed=-auxiliary_baseband(generator,pilot,plan,time,8000);
        require(std::abs(observed-h::apsk_point(expected[i]))<1e-11,
            "strong analytic metadata symbol changed");
        const auto ring=std::clamp(std::llround((std::abs(observed)-.2)*127/.8),0LL,127LL);
        auto phase=std::llround(std::arg(observed)*128/tau)%128;
        if(phase<0)phase+=128;
        require((static_cast<std::uint32_t>(ring)<<7|static_cast<std::uint32_t>(phase))==expected[i],
            "public algebraic strong-signal metadata slicer failed");
    }
    for(const auto callsign:{"X","XY","N0CALL","ABCDEFGHIJKLMNOP"}) {
        options.callsign=callsign;
        const auto packet=h::metadata_packet(0,callsign);
        const h::Generator fixed(options,plan);
        require(packet.count==9,"metadata did not use a fixed nine-symbol record");
        require(std::abs(auxiliary_baseband(fixed,pilot,plan,.2119L,8000)+std::complex<double>{1,0})<1e-11,
            "metadata started before the fixed record interval");
        require(std::abs(auxiliary_baseband(fixed,pilot,plan,.392L,8000)+h::apsk_point(packet.symbols.back()))<1e-11,
            "metadata did not finish at 392ms");
        require(std::abs(auxiliary_baseband(fixed,pilot,plan,.396L,8000)+.5*h::apsk_point(packet.symbols.back()))<1e-11,
            "metadata exit ramp did not retain the final constellation value");
    }
    // The exit taper must scale the actual last constellation point, even
    // when it has a small radius or a phase far from the inverted reference.
    // No return-to-reference interpolation is allowed inside this taper.
    for(const auto last:std::array<std::array<unsigned char,2>,4>{{
            {1,1},{1,64},{127,32},{127,127}}}) {
        options.callsign=std::string(14,'A');
        options.callsign+=static_cast<char>(last[0]);
        options.callsign+=static_cast<char>(last[1]);
        const auto word=(static_cast<std::uint32_t>(last[0])<<7)|last[1];
        const h::Generator fixed(options,plan);
        constexpr std::array<double,4> fall{1,918./1024,.5,106./1024};
        for(std::size_t i=0;i<fall.size();++i) {
            const auto at=.392L+.002L*i;
            require(std::abs(auxiliary_baseband(fixed,pilot,plan,at,8000)+
                    fall[i]*h::apsk_point(word))<1e-11,
                "final APSK value was not retained throughout the exit taper");
        }
    }
}
void timecode_generation() {
    h::Options options;options.power_w=100;
    require(options.timecode,"empty callsign no longer defaults to timecode");
    const auto plan=metadata_fixture_plan(options,1000,200,8000);
    const auto pilot=pilot_only(plan);
    const auto packet_at=[&](const h::Generator& generator,long double dwell_start) {
        h::MetadataPacket packet;packet.count=9;
        for(std::size_t i=0;i<packet.count;++i) {
            const auto time=dwell_start+.212L+(i+1)*.020L;
            const auto observed=-auxiliary_baseband(generator,pilot,plan,time,8000);
            const auto ring=std::clamp(std::llround((std::abs(observed)-.2)*127/.8),0LL,127LL);
            auto phase=std::llround(std::arg(observed)*128/tau)%128;
            if(phase<0)phase+=128;
            packet.symbols[i]=(static_cast<std::uint32_t>(ring)<<7)|static_cast<std::uint32_t>(phase);
        }
        return packet;
    };
    constexpr std::uint64_t epoch=1800000000;
    constexpr auto expected_ticks=epoch<<16;
    h::Generator clear(options,plan,epoch);
    const auto packet=packet_at(clear,0);
    const auto decoded=h::decode_metadata_packet(packet.symbols);
    require(decoded&&decoded->time_ticks==((expected_ticks%h::metadata_period_q16)*2/65)&&
        decoded->callsign.empty()&&!decoded->authenticated,
        "generator time phase does not describe physical dwell start");
    require(packet.symbols==h::metadata_packet(expected_ticks,{}).symbols,
        "generator timestamp packet differs from the physical epoch");
    (void)packet_at(clear,.8L);
    require(packet_at(clear,0).symbols==packet.symbols,
        "cached timestamp depends on render order");
    options.callsign="N0CALL";
    h::Generator identified(options,plan,epoch);
    const auto identified_packet=packet_at(identified,0);
    const auto identified_metadata=h::decode_metadata_packet(identified_packet.symbols);
    require(identified_metadata&&identified_metadata->callsign==options.callsign&&
        identified_metadata->time_ticks==decoded->time_ticks,
        "combined metadata did not carry both callsign and time phase");
    std::array<std::uint8_t,32> master{};
    for(std::size_t i=0;i<master.size();++i)master[i]=static_cast<std::uint8_t>(3*i+1);
    const datapump::Crypto key(master);
    const std::array<datapump::Crypto,1> keys{key};
    const h::Generator encrypted(options,plan,epoch,true,key);
    const auto encrypted_packet=packet_at(encrypted,0);
    require(encrypted_packet.symbols==h::metadata_packet(expected_ticks,options.callsign,&key).symbols,
        "generator encrypted metadata differs from the physical dwell packet");
    require(encrypted_packet.symbols[0]==identified_packet.symbols[0]&&
        encrypted_packet.symbols!=identified_packet.symbols,
        "keyed metadata did not preserve the time phase while encrypting the callsign");
    for(const auto offset:{-4LL*65536,0LL,4LL*65536}) {
        const auto recovered=h::decode_metadata_packet(encrypted_packet.symbols,keys,
            static_cast<std::uint64_t>(static_cast<std::int64_t>(expected_ticks)+offset));
        require(recovered&&recovered->callsign==options.callsign&&
            recovered->time_ticks==decoded->time_ticks&&!recovered->authenticated,
            "keyed generator record did not decrypt with the selected clock anchor");
    }
    require(!h::decode_metadata_packet(encrypted_packet.symbols,keys),
        "encrypted metadata decoded without a clock anchor");
    (void)packet_at(encrypted,.8L);
    require(packet_at(encrypted,0).symbols==encrypted_packet.symbols,
        "cached encrypted metadata depends on render order");
    options.callsign.clear();options.timecode=true;
    h::Generator cached(options,plan,0);
    const auto original=packet_at(cached,0);
    for(const auto invalid:{-.4L,4294967296.L}) {
        require(std::abs(auxiliary_baseband(cached,pilot,plan,invalid+.38L,8000)+std::complex<double>{1,0})<1e-12,
            "out-of-range time was encoded as metadata");
        require(packet_at(cached,0).symbols==original.symbols,
            "valid-invalid-same-valid sequence lost its cached timestamp");
    }
    options.timecode=false;
    h::Generator pure(options,plan,epoch);
    require(std::abs(auxiliary_baseband(pure,pilot,plan,.36L,8000)+std::complex<double>{1,0})<1e-12,
        "disabled metadata modulates the empty-callsign tone");
    struct RestoreProperties {
        ~RestoreProperties() {EVP_set_default_properties(nullptr,"");ERR_clear_error();}
    } restore;
    require(EVP_set_default_properties(nullptr,"provider=helper_generator_test_missing")==1,
        "could not select unavailable cipher fixture");
    options.timecode=true;
    rejects([&]{h::Generator unavailable(options,plan,epoch,true,key);});
    options.callsign="N0CALL";
    rejects([&]{h::Generator unavailable_with_callsign(options,plan,epoch,true,key);});
    const h::Generator clear_without_cipher(options,plan,epoch);
    require(packet_at(clear_without_cipher,0).symbols==identified_packet.symbols,
        "clear metadata unexpectedly requires the auxiliary cipher");
}
void waveform_coordinates() {
    h::Options options;options.power_w=1500;options.timecode=false;
    const auto plan=metadata_fixture_plan(options,1000,200,8000);
    h::Generator generator(options,plan),odd(options,plan,1);
    const auto pilot=pilot_only(plan);
    const auto baseband=[&](long double time) {
        return auxiliary_baseband(generator,pilot,plan,time,8000);
    };
    require(std::abs(baseband(.05L)-std::complex<double>{1,0})<1e-12,"preceding auxiliary tone not pure");
    require(std::abs(baseband(.151L)-std::complex<double>{1,0})<1e-12,"pre-punctuation straight tone not pure");
    for(const auto time:{.168L,.17L,.18L,.191999L,.20L,.211999L,.28L,.392L})
        require(std::abs(baseband(time)+std::complex<double>{1,0})<1e-12,
                "single auxiliary punctuation did not retain its inverted underlying state");
    require(std::abs(baseband(.16L))<1e-12&&std::abs(baseband(.20L)+std::complex<double>{1,0})<1e-12,
            "auxiliary reversal is not centered at160ms or inverted through212ms");
    // Each 8ms half is an independent C3 taper, reaching zero at the
    // phase reversal with zero slope on both sides.
    constexpr std::array<double,5> blend{0,289./4096,.5,3807./4096,1};
    for(std::size_t i=0;i<blend.size();++i) {
        require(std::abs(baseband(.152L+.002L*i)-std::complex<double>{1-blend[i],0})<1e-11,
            "auxiliary down-ramp does not follow its C3 shape");
        require(std::abs(baseband(.160L+.002L*i)+std::complex<double>{blend[i],0})<1e-11,
            "auxiliary inverted up-ramp does not follow its C3 shape");
    }
    constexpr std::array<double,5> boundary_blend{0,106./1024,.5,918./1024,1};
    for(std::size_t i=0;i<boundary_blend.size();++i) {
        const auto edge=.002L*i;
        for(const bool falling:{false,true}) {
            const auto time=falling?.4L-edge:edge;
            const auto auxiliary=(falling?-1:1)*boundary_blend[i];
            require(std::abs(baseband(time)-std::complex<double>{auxiliary,0})<1e-11,
                "auxiliary periodic boundary ramp is not8ms C2");
            const auto pilot_envelope=i==0?0.:i==1?.5:1.;
            const auto expected=plan.helper_gain*(
                h::pilot_amplitude_fraction*pilot_envelope*oscillator(plan.frequency_hz,time)+
                h::auxiliary_amplitude_fraction*auxiliary*oscillator(plan.auxiliary_frequency_hz,time));
            require(std::abs(generator.mix_analytic({},time,8000)-expected)<1e-11,
                "auxiliary boundary shaping changed the independent4ms pilot ramp");
        }
    }
    const auto late_start=auxiliary_baseband(generator,pilot,plan,.16L,8000,{},.14L);
    require(std::abs(late_start-std::complex<double>{1,0})<1e-10,
        "partial dwell punctuated without20ms preceding tone");
    const auto early_end=auxiliary_baseband(generator,pilot,plan,.16L,8000,.18L,0.L);
    require(std::abs(early_end-std::complex<double>{1,0})<1e-10,
        "partial dwell emitted punctuation without its20ms following flank");
    h::Generator live_generator(options,plan,0,false);
    const auto before_stop=auxiliary_baseband(live_generator,pilot,plan,.169L,8000,.4L,0.L);
    const auto after_stop=auxiliary_baseband(live_generator,pilot,plan,.17L,8000,.18L,0.L);
    require(std::abs(before_stop-std::complex<double>{1,0})<1e-10&&
        std::abs(after_stop-std::complex<double>{1,0})<1e-10,
        "mutable live endpoint changed unpunctuated auxiliary phase");
    require(std::abs(generator.mix(0,0,8000))==0,"dwell begins without ramp");
    require(std::abs(generator.mix(0,.4L,8000))<1e-12,"dwell end is not zero");
    const auto peak=plan.helper_gain*(h::pilot_amplitude_fraction+h::auxiliary_amplitude_fraction);
    double straight_power=0,pilot_power=0,auxiliary_power=0;
    for(std::uint32_t i=0;i<8000;++i) {
        const auto time=static_cast<long double>(i)/8000;
        const auto whole=generator.mix(i%2?1.f:-1.f,time,8000);
        require(std::abs(whole)<=.950001,"composite exceeded reserved headroom");
        require(std::abs(whole-generator.mix(i%2?1.f:-1.f,time,8000))==0,"rendering depends on call order");
        require(std::abs(generator.mix(0,time,8000)-
            odd.mix(0,time-1,8000))<1e-6,"physical UTC dwell grid depends on epoch split");
        if(time>=.02L&&time<.14L) {
            const auto main=pilot.mix_analytic({},time,8000);
            const auto auxiliary=generator.mix_analytic({},time,8000)-main;
            pilot_power+=std::norm(main);auxiliary_power+=std::norm(auxiliary);
            straight_power+=std::norm(generator.mix_analytic({},time,8000));
            require(std::abs(main/(plan.helper_gain*h::pilot_amplitude_fraction*oscillator(plan.frequency_hz,time))-
                std::complex<double>{1,0})<1e-12,"pure pilot unexpectedly modulated");
        }
    }
    require(std::abs(pilot_power/auxiliary_power-h::pilot_power_fraction/h::auxiliary_power_fraction)<1e-11&&
            std::abs(straight_power-pilot_power-auxiliary_power)<1e-9,
            "straight interval did not preserve90/10 power or coherent total accounting");
    // Known auxiliary values, rather than a composite subtraction used as
    // truth, expose any pilot reversal at punctuation or metadata endpoints.
    options.callsign="ABCDEFGHIJKLMNOP";
    h::Generator metadata(options,plan);
    const auto packet=h::callsign_packet(options.callsign);
    const auto auxiliary_gate=[](long double time) {
        const auto u=static_cast<double>(std::clamp((.4L-time)/.008L,0.L,1.L));
        return u*u*u*(10+u*(-15+6*u));
    };
    for(std::size_t symbol=0;symbol<packet.count;++symbol) {
        const auto previous=symbol?h::apsk_point(packet.symbols[symbol-1]):std::complex<double>{1,0};
        const auto next=h::apsk_point(packet.symbols[symbol]);
        const auto begin=.212L+symbol*.020L;
        for(std::size_t i=0;i<blend.size();++i)
            require(std::abs(auxiliary_baseband(metadata,pilot,plan,begin+.005L*i,8000)+
                (previous+(next-previous)*blend[i])*auxiliary_gate(begin+.005L*i))<1e-11,
                "auxiliary metadata does not follow its 20ms C3 shape");
    }
    const auto check_pilot=[&](long double time,std::complex<double> auxiliary) {
        const auto total=metadata.mix_analytic({},time,8000);
        const auto aux=auxiliary*plan.helper_gain*h::auxiliary_amplitude_fraction*
            oscillator(plan.auxiliary_frequency_hz,time);
        require(std::abs((total-aux)/(plan.helper_gain*h::pilot_amplitude_fraction*oscillator(plan.frequency_hz,time))-
            std::complex<double>{1,0})<1e-11,"auxiliary punctuation or metadata reversed the pilot");
    };
    check_pilot(.152L,{1,0});check_pilot(.16L,{});check_pilot(.168L,{-1,0});
    check_pilot(.20L,{-1,0});check_pilot(.396L,-.5*h::apsk_point(packet.symbols.back()));
    for(std::size_t i=0;i<packet.count;++i)
        check_pilot(.212L+(i+1)*.020L,-h::apsk_point(packet.symbols[i]));
    require(generator.mix(0,.15L,8000,.15L)==0,"finite transmission lacks final helper ramp");
    require(std::abs(generator.mix_analytic({},.148L,8000,.15L))<=peak*.500000001,
            "finite transmission final4ms envelope wrong");
    require(generator.mix(0,.15L,8000,{},.15L)==0,"non-dwell transmission start lacks helper ramp");
    require(std::abs(generator.mix_analytic({},.152L,8000,{},.15L))<=peak*.500000001,
            "non-dwell transmission start4ms envelope wrong");
    const auto pilot_quarter=pilot.mix_analytic({},.151L,8000,{},.15L)/
        (plan.helper_gain*h::pilot_amplitude_fraction*oscillator(plan.frequency_hz,.151L));
    require(std::abs(pilot_quarter-std::complex<double>{106./1024,0})<1e-12,
        "common4ms ramp changed from its C2 shape");
}
void protected_streaming_endpoint() {
    for(const auto end:{0.L,.1L,.151999L,.192L,.3L,.4L,.551999L})
        require(h::protected_endpoint(0,0,end)==end,"helper extends an endpoint outside its punctuation guard");
    for(const auto end:{.152L,.16L,.17L,.18L,.191999L})
        require(std::abs(h::protected_endpoint(0,0,end)-.192L)<1e-15,
            "helper streaming stop did not protect the single reversal and its following straight-tone flank");
    require(std::abs(h::protected_endpoint(0,.128L,.16L)-.192L)<1e-15&&
        h::protected_endpoint(0,.128001L,.16L)==.16L,
        "helper endpoint ignored the preceding clean-tone requirement");
    require(std::abs(h::protected_endpoint(1,-1.L,-.04L)+.008L)<1e-15,
        "protected endpoint lost the UTC dwell grid at odd epochs");
    require(std::abs(h::protected_endpoint(2,-2.L,-1.84L)+2-.192L)<1e-15,
        "protected endpoint depends on the physical epoch split");
    rejects([]{h::protected_endpoint(0,1,0);});
    rejects([]{h::protected_endpoint(0,0,std::numeric_limits<long double>::infinity());});
    h::Options options;options.power_w=100;options.timecode=false;
    const auto plan=metadata_fixture_plan(options,1000,200,8000);
    h::Generator generator(options,plan);
    const auto pilot=pilot_only(plan);
    const auto before=generator.mix_analytic({},.17L,8000,.4L,0);
    const auto after=generator.mix_analytic({},.17L,8000,h::protected_endpoint(0,0,.18L),0);
    require(std::abs(before-after)<1e-15,"streaming stop retroactively changed punctuation phase");
    for(const auto time:{.168L,.176L,.187999L}) {
        const auto clean=auxiliary_baseband(generator,pilot,plan,time,8000,.192L,0);
        require(std::abs(clean+std::complex<double>{1,0})<1e-11,
            "protected streaming endpoint lacks20ms inverted auxiliary carrier");
    }
    require(generator.mix(0,.192L,8000,.192L,0)==0,"protected endpoint did not finish its helper ramp");
    // The exact and immediately adjacent representable coordinates must
    // preserve the reversal under either UTC epoch split. Normalized-calendar
    // roundoff may conservatively include an adjacent onset, never erase it.
    for(const auto epoch:{0ULL,1ULL,1800000000ULL}) {
        const auto origin=epoch%2?-.2L:0.L,begin=origin+.152L,end=origin+.192L;
        const h::Generator dated(options,plan,epoch);
        long double previous=0;
        for(const auto stop:{std::nextafter(begin,-std::numeric_limits<long double>::infinity()),
                             begin,std::nextafter(begin,std::numeric_limits<long double>::infinity())}) {
            const auto protected_end=h::protected_endpoint(epoch,origin,stop);
            require(protected_end>=stop&&std::abs(protected_end-end)<1e-15L&&
                    (!previous||protected_end+1e-15L>=previous),
                    "adjacent punctuation onset lost monotone reversal protection");
            previous=protected_end;
            const auto emitted=origin+.17L;
            require(dated.mix_analytic({},emitted,8000,origin+.4L,origin)==
                    dated.mix_analytic({},emitted,8000,protected_end,origin),
                    "calendar protection changed an already emitted inverted plateau");
        }
        for(const auto stop:{std::nextafter(end,-std::numeric_limits<long double>::infinity()),
                             end,std::nextafter(end,std::numeric_limits<long double>::infinity())}) {
            const auto protected_end=h::protected_endpoint(epoch,origin,stop);
            require(protected_end>=stop&&std::abs(protected_end-stop)<1e-15L,
                    "adjacent final guard moved backward or extended beyond calendar roundoff");
        }
        require(h::protected_endpoint(epoch,origin,end)==end&&
                h::protected_endpoint(epoch,origin,begin-.000001L)==begin-.000001L&&
                h::protected_endpoint(epoch,origin,end+.000001L)==end+.000001L,
                "punctuation protection changed an exact final or visibly outside endpoint");
        require(std::abs(h::protected_endpoint(epoch,origin+.128L,begin)-end)<1e-15L&&
                h::protected_endpoint(epoch,origin+.128001L,begin)==begin,
                "reversal protection lost the latest clean-tone start boundary");
    }
}
void fft(std::vector<std::complex<double>>& samples) {
    const auto size=samples.size();
    for(std::size_t i=1,j=0;i<size;++i) {
        auto bit=size>>1;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;if(i<j)std::swap(samples[i],samples[j]);
    }
    for(std::size_t length=2;length<=size;length*=2) {
        const auto step=std::polar(1.,-tau/static_cast<double>(length));
        for(std::size_t begin=0;begin<size;begin+=length) {
            std::complex<double> phase{1,0};
            for(std::size_t i=0;i<length/2;++i) {
                const auto a=samples[begin+i],b=samples[begin+i+length/2]*phase;
                samples[begin+i]=a+b;samples[begin+i+length/2]=a-b;phase*=step;
            }
        }
    }
}
void spectrum() {
    // The unchanged placement calculation may keep its conservative C2
    // derivative-transform bound: C3 is bounded more tightly throughout the
    // permitted data-band distances (at least450Hz for an8ms transition).
    for(const auto z:{std::numbers::pi*450*.008,20.,100.,1000.}) {
        const auto old_bound=15*(3+3*z+z*z)/std::pow(z,5);
        const auto new_bound=105*(15+15*z+6*z*z+z*z*z)/std::pow(z,7);
        require(new_bound<=old_bound,"C3 spectral tail invalidates the retained placement bound");
    }
    // One complete dwell has zero endpoints; no analysis window hides ramps,
    // punctuation or modulation sidebands. 2.5Hz FFT bins at10240Hz.
    std::array<std::uint8_t,32> master{};
    for(std::size_t i=0;i<master.size();++i)master[i]=static_cast<std::uint8_t>(i+23);
    const datapump::Crypto key(master);
    struct MetadataCase {std::string_view callsign;bool encrypted,timecode;};
    for(const auto& test:std::array<MetadataCase,5>{{{"",false,false},{"",false,true},{"",true,true},
            {"N0CALL",false,true},{"ABCDEFGHIJKLMNOP",false,true}}}) {
        h::Options options;options.power_w=100;options.callsign=test.callsign;options.timecode=test.timecode;
        const auto plan=metadata_fixture_plan(options,1000,200,10240);
        h::Generator generator(options,plan,1800000000,true,test.encrypted?std::optional<datapump::Crypto>{key}:std::nullopt);
        const auto pilot=pilot_only(plan,1800000000);
        std::vector<std::complex<double>> samples(4096);
        for(std::size_t i=0;i<samples.size();++i) {
            const auto time=static_cast<long double>(i)/10240;
            samples[i]=auxiliary_baseband(generator,pilot,plan,time,10240);
        }
        fft(samples);
        double total=0,outside=0,data_band=0,pilot_band=0,strongest_sideband=0;
        for(std::size_t i=0;i<samples.size();++i) {
            const auto frequency=(i<=samples.size()/2?static_cast<double>(i):
                static_cast<double>(i)-static_cast<double>(samples.size()))*2.5;
            const auto energy=std::norm(samples[i]);total+=energy;
            if(i)strongest_sideband=std::max(strongest_sideband,energy);
            if(std::abs(frequency)>150)outside+=energy;
            if(frequency>=900-plan.auxiliary_frequency_hz&&frequency<=1100-plan.auxiliary_frequency_hz)data_band+=energy;
            if(std::abs(frequency-h::auxiliary_pilot_spacing_hz)<=25)pilot_band+=energy;
        }
        const auto outside_db=10*std::log10(outside/total),leakage_db=10*std::log10(data_band/total);
        const auto carrier=std::norm(samples[0]);
        const auto leakage_to_pilot=pilot_band/static_cast<double>(samples.size()*samples.size())*
            h::auxiliary_power_fraction/h::pilot_power_fraction;
        std::cout<<"helper auxiliary spectrum callsign="<<test.callsign<<" encrypted="<<test.encrypted<<" timecode="<<test.timecode
                 <<" carrier_fraction="<<carrier/total<<" strongest_sideband_fraction="<<strongest_sideband/total
                 <<" normalized_power="<<total/static_cast<double>(samples.size()*samples.size())
                 <<" outside300Hz_dBc="<<outside_db<<" adjacent_data_band_dBc="<<leakage_db
                 <<" auxiliary_in_pilot50Hz_dBc="<<10*std::log10(leakage_to_pilot)<<'\n';
        require(leakage_to_pilot<1e-7,"actual auxiliary modulation appreciably contaminates the pilot band");
        require(outside/total<.01,"helper exceeds300Hz99percent occupied bandwidth");
        require(data_band/total<1e-5,"helper transition leakage exceeds measured-geometry mask");
    }
    // Sample the actual planned guards at the full ratio ceiling. Fixed
    // lower tones require a high enough Robust carrier before high placement.
    for(const auto bandwidth:{200.,6000.})for(const auto ratio_db:{0.,40.,60.,90.})for(const auto encrypted:{false,true}) {
        constexpr std::uint32_t rate=40960;
        h::Options options;options.data_power_w=1;options.power_w=1+std::pow(10.,ratio_db/10);
        options.callsign=encrypted?"":"ABCDEFGHIJKLMNOP";
        h::PowerReference reference;reference.time_key=encrypted?std::optional<datapump::Crypto>{key}:std::nullopt;
        const auto plan=h::plan(options,10000,bandwidth,rate,0,1,&reference);
        require(plan.low_frequency_hz==550&&plan.auxiliary_frequency_hz==950&&plan.frequency_hz>=2400,
            "power-dependent placement moved fixed helper frequencies");
        h::Generator generator(options,plan,0,true,reference.time_key);
        std::vector<std::complex<double>> samples(16384);
        for(std::size_t i=0;i<samples.size();++i)
            samples[i]=generator.mix(0,static_cast<long double>(i)/rate,rate)/plan.helper_gain;
        fft(samples);double leaked=0;
        for(std::size_t i=0;i<samples.size();++i) {
            const auto frequency=(i<=samples.size()/2?static_cast<double>(i):
                static_cast<double>(i)-static_cast<double>(samples.size()))*2.5;
            if(std::abs(std::abs(frequency)-10000)<=bandwidth/2)leaked+=std::norm(samples[i]);
        }
        const auto fraction=2*leaked/static_cast<double>(samples.size()*samples.size())/
            h::mean_power(options,false,&reference).total;
        const auto relative_to_data=fraction*std::pow(10.,ratio_db/10);
        require(relative_to_data<1e-3,"fixed helper power-dependent guard exceeded sampled leakage mask");
    }
    // A finite transmission may begin in the middle of a dwell. Test both
    // sides of a hop boundary; explicit start/end ramps must keep the same
    // occupied-bandwidth target without adding any payload samples.
    for(const auto start:{.153L,.3995L}) {
        constexpr std::uint32_t rate=40960;
        h::Options options;options.power_w=100;options.callsign="N0CALL";
        const auto plan=metadata_fixture_plan(options,1000,200,rate);
        h::Generator generator(options,plan);
        const auto pilot=pilot_only(plan);
        std::vector<std::complex<double>> samples(32768);
        for(std::size_t i=0;i<samples.size();++i) {
            const auto time=static_cast<long double>(i)/rate;
            samples[i]=auxiliary_baseband(generator,pilot,plan,time,rate,start+.3L,start);
        }
        fft(samples);double total=0,outside=0;
        for(std::size_t i=0;i<samples.size();++i) {
            const auto frequency=(i<=samples.size()/2?static_cast<double>(i):
                static_cast<double>(i)-static_cast<double>(samples.size()))*1.25;
            total+=std::norm(samples[i]);
            if(std::abs(frequency)>150)outside+=std::norm(samples[i]);
        }
        require(outside/total<.01,"finite helper start/end exceeded300Hz99percent bandwidth");
    }
    for(const auto ratio_db:{60.,90.})for(const auto& endpoints:{
            std::pair{.153L,.35L},std::pair{.3995L,.702L},
            std::pair{.398L,.402L},std::pair{.153L,.402L}}) {
        const std::uint32_t rate=ratio_db==90?163840:40960;
        const auto data_carrier=ratio_db==90?45000.:10000.;
        h::Options options;options.data_power_w=1e-6;
        options.power_w=options.data_power_w*std::pow(10.,ratio_db/10);
        options.callsign="ABCDEFGHIJKLMNOP";
        h::PowerReference reference;reference.begin_seconds=endpoints.first;reference.end_seconds=endpoints.second;
        reference.helper_start_seconds=endpoints.first;reference.helper_end_seconds=endpoints.second;
        if(ratio_db==90&&endpoints.second-endpoints.first<.005L)
            rejects([&]{h::plan(options,10000,200,40960,0,1,&reference);});
        const auto plan=h::plan(options,data_carrier,200,rate,0,1,&reference);
        h::Generator generator(options,plan);
        std::vector<std::complex<double>> samples(rate*4/5);
        for(std::size_t i=0;i<samples.size();++i)
            samples[i]=generator.mix(0,static_cast<long double>(i)/rate,rate,
                endpoints.second,endpoints.first)/plan.helper_gain;
        fft(samples);double leaked=0;
        for(std::size_t i=0;i<samples.size();++i) {
            const auto frequency=(i<=samples.size()/2?static_cast<double>(i):
                static_cast<double>(i)-static_cast<double>(samples.size()))*1.25;
            if(std::abs(std::abs(frequency)-data_carrier)<=100)leaked+=std::norm(samples[i]);
        }
        // Average over active source duration, not the zero-padded FFT record.
        const auto active_fraction=static_cast<double>((endpoints.second-endpoints.first)/.8L);
        const auto relative_to_data=2*leaked/static_cast<double>(samples.size()*samples.size())*
            std::pow(10.,ratio_db/10)/active_fraction/h::mean_power(options,false,&reference).total;
        std::cout<<"helper finite ratio_dB="<<ratio_db<<" start="<<static_cast<double>(endpoints.first)
                 <<" end="<<static_cast<double>(endpoints.second)
                 <<" leakage/data_dB="<<10*std::log10(relative_to_data)<<'\n';
        require(relative_to_data<1e-3,"finite transmission violated data leakage mask");
    }
}
void tracker_lifetime_admission() {
    constexpr std::uint32_t rate=1000;
    constexpr std::size_t window=100;
    // Orthogonal deterministic nuisance at350Hz fixes residual energy. Set the
    // wanted250Hz fit to F=5.1, hence6.13dB excess: admitted immediately, but
    // the old fixed-four-window gate rejected it after about60 lifetime looks.
    const auto weak_amplitude=.1*std::sqrt(2*5.1/(window-2));
    const auto fill=[&](double amplitude) {
        std::array<float,window> samples{};
        for(std::size_t i=0;i<window;++i) {
            const auto seconds=static_cast<double>(i)/rate;
            samples[i]=static_cast<float>(amplitude*std::cos(tau*250*seconds)+
                                           .1*std::cos(tau*350*seconds));
        }
        return samples;
    };
    const auto weak=fill(weak_amplitude),absent=fill(0),strong=fill(.2);
    h::Tracker observer(250,0);
    std::size_t completed=0;
    const auto group=[&](const auto& samples,bool isolated_fragment=false) {
        const auto count=4+static_cast<std::size_t>(std::ceil(2*std::log1p(static_cast<double>(completed))));
        // A concentrated fragment can exceed the level gate while failing the
        // finite-noise admission gate. It must not inherit a retired admission.
        const auto fragment=fill(.1*std::sqrt(2*(4.1*static_cast<double>(count)+1)/(window-2)));
        for(std::size_t i=0;i<count;++i)
            observer.push(isolated_fragment?(i?absent:fragment):samples,rate);
        ++completed;
        const auto update=observer.take_update();
        require(update.has_value(),"adaptive tracker group failed to publish its observation");
        return *update;
    };
    for(unsigned i=0;i<200;++i) {
        const auto update=group(weak);
        require(update.present&&update.snr_db>6&&update.snr_db<6.5,
                "admitted weak helper expired solely from lifetime alpha spending");
    }
    require(!group(absent).present,"quiet interval did not report absent helper");
    require(group(weak).present,"brief fade discarded an admitted weak helper");
    h::Observation silence;
    do {silence=group(absent);}while(silence.absent_seconds+1e-9<6);
    require(!silence.present&&!silence.confirmed,"six-second absence did not retire helper presence");
    const auto fragment=group(absent,true);
    require(fragment.snr_db>=6&&!fragment.present,
            "retired helper let a level-only fragment bypass renewed admission");
    require(group(strong).present,"strong new helper could not pass renewed lifetime admission");

    observer.reset();completed=0;
    for(unsigned i=0;i<200;++i)require(!group(absent).present,"idle nuisance manufactured helper admission");
    const auto arrival=group(weak);
    require(arrival.present&&arrival.snr_db>=6,
            "weak new helper after long idle lacked enough accumulated admission evidence");
}
void tracker() {
    constexpr std::uint32_t rate=4000;
    h::Tracker tracker(1000);
    std::mt19937 random(17432);std::normal_distribution<double> normal(0,.02);
    bool present=false,confirmed=false;
    std::vector<float> chunk(137);
    std::uint64_t position=0;
    for(unsigned group=0;group<195;++group) {
        for(auto& value:chunk) {
            value=static_cast<float>(.2*std::cos(tau*1002.2*static_cast<double>(position)/rate)+normal(random));++position;
        }
        tracker.push(chunk,rate);
        if(const auto observation=tracker.take_update()) {
            present|=observation->present;confirmed|=observation->confirmed;
            require(std::abs(observation->frequency_hz-1002.2)<1,"tone estimate missed subcell frequency");
        }
    }
    require(present&&confirmed,"helper-only presence did not confirm after six seconds");
    tracker.reset();require(!tracker.take_update(),"discontinuity retained stale evidence");
    std::vector<float> silence(rate*7);tracker.push(silence,rate);
    const auto absent=tracker.take_update();
    require(absent&&!absent->present&&!absent->confirmed&&absent->observed_seconds==0,
            "silence manufactured helper presence");
    require(absent->absent_seconds>=6,"fully observed absence did not cover six seconds");
    // Independent seeded Gaussian controls. Statistical evidence is a model
    // bound above; this finite fixture catches calibration/normalization errors.
    unsigned false_admissions=0;
    std::vector<float> noise(rate*4);
    for(unsigned seed=0;seed<64;++seed) {
        h::Tracker control(1000);std::mt19937 rng(seed);std::normal_distribution<double> gaussian(0,.1);
        for(auto& value:noise)value=static_cast<float>(gaussian(rng));
        for(std::size_t begin=0;begin<noise.size();begin+=400) {
            control.push(std::span(noise).subspan(begin,400),rate);
            if(const auto update=control.take_update();update&&update->present)++false_admissions;
        }
    }
    require(false_admissions<=2,"Gaussian false-helper controls exceeded expected bound");
    std::cout<<"helper Gaussian controls false_admissions="<<false_admissions<<" streams=64 duration=4s\n";
    for(const auto callsign:{"","N0CALL"}) {
        h::Options options;options.power_w=4;options.callsign=callsign;
        const auto plan=metadata_fixture_plan(options,500,100,rate);
        h::Generator generator(options,plan);h::Tracker actual(plan.frequency_hz);
        bool found=false;
        for(std::size_t begin=0;begin<rate*7;begin+=400) {
            std::array<float,400> samples{};
            for(std::size_t i=0;i<samples.size();++i)
                samples[i]=generator.mix(0,static_cast<long double>(begin+i)/rate,rate,{},0);
            actual.push(samples,rate);
            if(const auto observation=actual.take_update()) {
                found|=observation->confirmed;
                require(std::isfinite(observation->snr_db),"helper tracker exposed nonfinite SNR");
            }
        }
        require(found,"real helper ramps/punctuation/metadata prevented continuous presence");
    }
    rejects([]{h::Tracker bad(1000,101);});
}
}
int main() {
    try {apsk_constellation();test_helper_power_reference();power_and_placement();active_average_and_continuous_pilots();metadata_vectors();timecode_generation();waveform_coordinates();protected_streaming_endpoint();spectrum();tracker();tracker_lifetime_admission();}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
    std::cout<<"helper carrier checks passed\n";return 0;
}
