#include "datapump/helper_carrier.hpp"
#include "datapump/modem.hpp"
#include "datapump/types.hpp"
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
void power_and_placement() {
    h::Options options;
    require(h::minimum_output_rate(options,1000,200,6000)==0,
            "disabled helper changes hardware negotiation");
    const auto disabled=h::plan(options,1000,200,8000);
    require(disabled.helper_gain==0&&disabled.data_gain==1,"disabled helper changes gain");
    h::Generator off(options,disabled);
    for(const auto sample:{0.f,-0.f,.125f,-.83f,std::numeric_limits<float>::infinity()})
        require(std::bit_cast<std::uint32_t>(off.mix(sample,0,0))==std::bit_cast<std::uint32_t>(sample),
                "disabled mixer changed payload bits");
    options.power_w=1500;
    require(h::minimum_output_rate(options,1500,36,6000)==48000,
            "helper did not negotiate independently of the low-rate modem");
    require(h::minimum_output_rate(options,1500,36,48000)==48000&&
            h::minimum_output_rate(options,1500,36,96000)==96000,
            "logical sample clock pushed an adjacent helper into a remote audio band");
    require(h::minimum_output_rate(options,1700,3000,6000)==48000,
            "hardware negotiation applied logical Nyquist to the physical helper geometry");
    const auto high=h::plan(options,1000,200,8000);
    const auto backed_off=h::plan(options,1000,200,8000,0,.5);
    require(backed_off.helper_gain==high.helper_gain*.5&&backed_off.data_gain==high.data_gain&&
            backed_off.effective_power_w==high.effective_power_w,
            "payload backoff calibration changed Robust PCM or failed to reduce only helper amplitude");
    rejects([&]{h::plan(options,1000,200,8000,0,0);});
    rejects([&]{h::plan(options,1000,200,8000,0,std::numeric_limits<double>::quiet_NaN());});
    require(high.auxiliary_frequency_hz==1600&&high.frequency_hz==2000,
            "High helper did not use the nearest leakage-qualified 50Hz separation");
    const auto ssb=h::plan(options,1500,36,48000);
    require(ssb.frequency_hz==2368&&ssb.auxiliary_frequency_hz==1968&&
            ssb.auxiliary_frequency_hz-150>1518&&ssb.frequency_hz-ssb.auxiliary_frequency_hz==400,
            "narrow Robust signal did not retain a close nonoverlapping High helper");
    require(std::abs(high.helper_gain/high.data_gain-
        std::sqrt(2*datapump::modem::nominal_signal_power*1500/4))<1e-12,"power ratio used as amplitude ratio");
    require(high.data_gain==h::calibrated_data_gain(options)&&
            high.data_gain+high.helper_gain*(h::pilot_amplitude_fraction+h::auxiliary_amplitude_fraction)<=.950000000001,
            "two-tone peak sum lost calibrated payload gain or reserved headroom");
    const auto level=h::reference_level(options,10);
    require(std::abs(level.pilot_effective_power_ratio-337.5)<1e-12&&
            std::abs(level.pilot_straight_cn0_db_hz-level.straight_cn0_db_hz-10*std::log10(.9))<1e-12&&
            std::abs(level.pilot_usable_cn0_db_hz-level.usable_cn0_db_hz-10*std::log10(.9))<1e-12&&
            std::abs(level.pilot_straight_cn0_db_hz-level.auxiliary_straight_cn0_db_hz-10*std::log10(9.))<1e-12,
            "reference level confused total helper, pilot and auxiliary power");
    const auto outside_capture=h::plan(options,1000,200,48000,8250);
    require(outside_capture.frequency_hz>=8250&&outside_capture.frequency_hz<8300,
            "caller-supplied High frequency floor was not rounded outward");
    require(outside_capture.data_gain==high.data_gain&&outside_capture.helper_gain==high.helper_gain,
            "capture-stopband placement changed relative power calibration");
    rejects([&]{h::plan(options,1000,200,16000,8250);});
    rejects([&]{h::plan(options,1000,200,48000,-1);});
    rejects([&]{h::plan(options,1000,200,48000,std::numeric_limits<double>::quiet_NaN());});
    options.frequency=h::Frequency::low;
    rejects([&]{h::validate(options);});
    rejects([&]{h::plan(options,1000,200,8000);});
    options.frequency=h::Frequency::high;
    rejects([&]{h::plan(options,3500,500,8000);});
    options.frequency=h::Frequency::cochannel;
    rejects([&]{h::validate(options);});
    rejects([&]{h::plan(options,1000,200,8000);});
    options.frequency=h::Frequency::high;options.data_power_w=1e-6;
    const auto capped=h::plan(options,1000,200,48000);
    require(std::abs(capped.effective_power_w-1000)<1e-9,"ratio ceiling did not reduce helper power");
    options.power_w=-1;rejects([&]{h::validate(options);});
    options.power_w=1;options.callsign="ABCDEFGHIJKLMNOPQRST";rejects([&]{h::validate(options);});
    options.callsign=std::string(1,static_cast<char>(128));rejects([&]{h::validate(options);});
}
void metadata_vectors() {
    // Independently generated with a Python CRC7/MMC and explicit MSB
    // packing; these vectors catch coordinated mapper/framing regressions.
    constexpr std::array<std::uint32_t,3> expected{0x01a730,0x10e0cc,0x133f00};
    const auto normal=h::callsign_packet("N0CALL");
    require(normal.count==expected.size()&&std::equal(expected.begin(),expected.end(),normal.symbols.begin()),
        "compact callsign wire vector changed");
    constexpr std::array<std::uint32_t,7> longest{0x04e0c2,0x10e245,0x11a3c8,0x12654b,0x1326ce,0x13e851,0x14a99f};
    const auto maximum=h::callsign_packet("ABCDEFGHIJKLMNOPQRS");
    require(maximum.count==longest.size()&&std::equal(longest.begin(),longest.end(),maximum.symbols.begin()),
        "nineteen-character compact wire vector changed");
    require(std::abs(h::apsk_point(0)-std::complex<double>{.9,0})<1e-14,"inner APSK point changed");
    require(std::abs(h::apsk_point(1023U<<11)-std::complex<double>{1,0})<1e-14,"outer APSK point changed");
    require(std::abs(h::apsk_point(512)-std::complex<double>{0,.9})<1e-14,"APSK quadrant label changed");
    rejects([]{h::apsk_point(1U<<21);});
    h::Options options;options.power_w=100;options.callsign="N0CALL";
    const auto plan=h::plan(options,1000,200,8000);
    h::Generator generator(options,plan);
    const auto pilot=pilot_only(plan);
    for(std::size_t i=0;i<expected.size();++i) {
        const auto time=.388L-expected.size()*.008L+(i+1)*.008L;
        const auto observed=-auxiliary_baseband(generator,pilot,plan,time,8000);
        // Metadata multiplies the auxiliary's inverted underlying state.
        require(std::abs(observed-h::apsk_point(expected[i]))<1e-11,"strong analytic metadata symbol changed");
        const auto ring=std::clamp(std::llround((std::abs(observed)-.9)*10230),0LL,1023LL);
        auto phase=std::llround(std::arg(observed)*2048/tau)%2048;
        if(phase<0)phase+=2048;
        require((static_cast<std::uint32_t>(ring)<<11|static_cast<std::uint32_t>(phase))==expected[i],
                "public algebraic strong-signal metadata slicer failed");
    }
    for(const auto callsign:{"X","XY","N0CALL","ABCDEFGHIJKLMNOPQRS"}) {
        options.callsign=callsign;
        const auto packet=h::callsign_packet(callsign);
        const auto begin=.388L-packet.count*.008L;
        const h::Generator compact(options,plan);
        const auto before=begin-.0001L;
        const auto observed=auxiliary_baseband(compact,pilot,plan,before,8000);
        require(std::abs(observed+std::complex<double>{1,0})<1e-11,
            "short callsign needlessly occupied fixed metadata airtime");
        const auto last=auxiliary_baseband(compact,pilot,plan,.388L,8000);
        require(std::abs(last+h::apsk_point(packet.symbols[packet.count-1]))<1e-11,
            "compact callsign did not finish at388ms");
        require(std::abs(auxiliary_baseband(compact,pilot,plan,.396L,8000)+std::complex<double>{1,0})<1e-11,
            "metadata did not return to the auxiliary's underlying inverted state");
    }
}
void timecode_generation() {
    h::Options options;options.power_w=100;
    require(options.timecode,"empty callsign no longer defaults to timecode");
    const auto plan=h::plan(options,1000,200,8000);
    const auto pilot=pilot_only(plan);
    std::array<std::uint8_t,32> master{};
    for(std::size_t i=0;i<master.size();++i)master[i]=static_cast<std::uint8_t>(3*i+1);
    const datapump::Crypto key(master);
    const std::array<datapump::Crypto,1> keys{key};
    master[0]^=1;
    const std::array<datapump::Crypto,1> wrong_keys{datapump::Crypto(master)};
    const auto packet_at=[&](const h::Generator& generator,long double dwell_start,std::size_t count) {
        h::MetadataPacket packet;packet.count=count;
        const auto begin=.388L-count*.008L;
        for(std::size_t i=0;i<count;++i) {
            const auto time=dwell_start+begin+(i+1)*.008L;
            const auto observed=-auxiliary_baseband(generator,pilot,plan,time,8000);
            const auto ring=std::clamp(std::llround((std::abs(observed)-.9)*10230),0LL,1023LL);
            auto phase=std::llround(std::arg(observed)*2048/tau)%2048;
            if(phase<0)phase+=2048;
            packet.symbols[i]=(static_cast<std::uint32_t>(ring)<<11)|static_cast<std::uint32_t>(phase);
        }
        return packet;
    };
    const auto view=[](const h::MetadataPacket& packet) {
        return std::span<const std::uint32_t>(packet.symbols).first(packet.count);
    };
    constexpr std::uint64_t epoch=1800000000;
    constexpr auto expected_ticks=epoch<<16;
    h::Generator clear(options,plan,epoch),encrypted(options,plan,epoch,true,key);
    const auto clear_count=h::timecode_packet(expected_ticks).count;
    const auto encrypted_count=h::timecode_packet(expected_ticks,&key).count;
    require(clear_count<=3&&encrypted_count<=3,"48-bit timestamp exceeded24ms metadata airtime");
    const auto clear_packet=packet_at(clear,0,clear_count),encrypted_packet=packet_at(encrypted,0,encrypted_count);
    const auto clear_metadata=h::decode_metadata_packet(view(clear_packet));
    require(clear_metadata&&clear_metadata->kind==h::MetadataKind::clear_time&&
        clear_metadata->time_ticks==expected_ticks&&!clear_metadata->authenticated&&clear_metadata->callsign.empty(),
        "generator clear timestamp does not describe physical dwell start");
    const auto encrypted_metadata=h::decode_metadata_packet(view(encrypted_packet),keys);
    require(encrypted_metadata&&encrypted_metadata->kind==h::MetadataKind::encrypted_time&&
        encrypted_metadata->time_ticks==expected_ticks&&!encrypted_metadata->authenticated&&encrypted_metadata->callsign.empty(),
        "generator encrypted timestamp did not use the selected station key");
    require(!h::decode_metadata_packet(view(encrypted_packet)),"generator transmitted a clear timestamp with encryption enabled");
    const auto wrong=h::decode_metadata_packet(view(encrypted_packet),wrong_keys);
    require(!wrong||!wrong->authenticated,"unauthenticated FF1 time claimed wrong-key integrity");
    require(clear_packet.symbols==h::timecode_packet(expected_ticks).symbols&&
        encrypted_packet.symbols==h::timecode_packet(expected_ticks,&key).symbols,
        "generator timestamp packet differs from the fixed physical epoch");
    // Packet generation uses the supplied physical coordinate, never wall time
    // or call order. Visit a later dwell and then revisit the original one.
    (void)packet_at(encrypted,.8L,encrypted_count);
    require(packet_at(encrypted,0,encrypted_count).symbols==encrypted_packet.symbols,
        "cached encrypted timestamp depends on render order");
    options.callsign="N0CALL";
    h::Generator identified(options,plan,epoch);
    const auto callsign=packet_at(identified,0,h::callsign_packet(options.callsign).count);
    const auto decoded_callsign=h::decode_metadata_packet(view(callsign),keys);
    require(decoded_callsign&&decoded_callsign->kind==h::MetadataKind::callsign&&
        decoded_callsign->callsign==options.callsign&&decoded_callsign->time_ticks==0,
        "clear-mode callsign did not take exclusive precedence over timecode");
    const auto before_callsign=auxiliary_baseband(identified,pilot,plan,.32L,8000);
    require(std::abs(before_callsign+std::complex<double>{1,0})<1e-12,
        "clear callsign transmission also started a timestamp");
    const h::Generator keyed_identified(options,plan,epoch,true,key);
    const auto keyed_packet=packet_at(keyed_identified,0,encrypted_count);
    const auto keyed_metadata=h::decode_metadata_packet(view(keyed_packet),keys);
    require(keyed_metadata&&keyed_metadata->kind==h::MetadataKind::encrypted_time&&
        keyed_metadata->callsign.empty()&&keyed_metadata->time_ticks==expected_ticks&&
        keyed_packet.symbols==encrypted_packet.symbols,
        "encryption allowed a stored helper callsign to replace its timestamp");
    // A stored callsign has no effect on keyed PCM, even when timecode is
    // explicitly disabled or the ignored string is not a valid public ID.
    for(const auto timecode:{false,true}) {
        options.callsign.clear();options.timecode=timecode;
        const h::Generator reference(options,plan,epoch,true,key);
        for(const auto& stored:std::array<std::string,3>{"X","ABCDEFGHIJKLMNOPQRS",std::string(32,static_cast<char>(0xff))}) {
            options.callsign=stored;
            const h::Generator ignored(options,plan,epoch,true,key);
            require(options.callsign==stored,"generator changed the caller's stored helper callsign");
            for(std::uint32_t sample=0;sample<6400;++sample) {
                const auto seconds=static_cast<long double>(sample)/8000;
                require(ignored.mix_analytic({.125,-.25},seconds,8000)==
                    reference.mix_analytic({.125,-.25},seconds,8000),
                    "stored helper callsign changed encrypted-mode samples");
            }
        }
    }

    options.callsign.clear();options.timecode=true;
    for(const auto encrypted_time:{false,true}) {
        h::Generator cached(options,plan,0,true,encrypted_time?std::optional<datapump::Crypto>{key}:std::nullopt);
        const auto count=h::timecode_packet(0,encrypted_time?&key:nullptr).count;
        const auto original=packet_at(cached,0,count);
        for(const auto invalid:{-.4L,4294967296.L}) {
            const auto time=invalid+.38L;
            require(std::abs(auxiliary_baseband(cached,pilot,plan,time,8000)+std::complex<double>{1,0})<1e-12,
                "out-of-range time was encoded as a timestamp");
            require(packet_at(cached,0,count).symbols==original.symbols,
                "valid-invalid-same-valid time sequence lost its cached timestamp");
        }
    }
    options.timecode=false;
    h::Generator pure(options,plan,epoch,true,key);
    require(std::abs(auxiliary_baseband(pure,pilot,plan,.36L,8000)+std::complex<double>{1,0})<1e-12,
        "disabled timecode modulates the empty-callsign tone");
    // Fail before output if encryption is configured but unavailable. The
    // public callsign path is independent and never sends both packet types.
    struct RestoreProperties {
        ~RestoreProperties() {EVP_set_default_properties(nullptr,"");ERR_clear_error();}
    } restore;
    require(EVP_set_default_properties(nullptr,"provider=helper_generator_test_missing")==1,
        "could not select unavailable cipher fixture");
    options.timecode=true;
    rejects([&]{h::Generator unavailable(options,plan,epoch,true,key);});
    options.callsign="N0CALL";
    rejects([&]{h::Generator unavailable_with_callsign(options,plan,epoch,true,key);});
    const h::Generator callsign_without_cipher(options,plan,epoch);
    require(packet_at(callsign_without_cipher,0,callsign.count).symbols==callsign.symbols,
        "public callsign unexpectedly requires the timecode cipher");
}
void waveform_coordinates() {
    h::Options options;options.power_w=1500;options.timecode=false;
    const auto plan=h::plan(options,1000,200,8000);
    h::Generator generator(options,plan),odd(options,plan,1);
    const auto pilot=pilot_only(plan);
    const auto baseband=[&](long double time) {
        return auxiliary_baseband(generator,pilot,plan,time,8000);
    };
    require(std::abs(baseband(.05L)-std::complex<double>{1,0})<1e-12,"preceding auxiliary tone not pure");
    require(std::abs(baseband(.215L)-std::complex<double>{1,0})<1e-12,"20ms pre-punctuation guard not pure");
    for(const auto time:{.244L,.25L,.275999L,.284L,.292L,.304L,.32L,.396L})
        require(std::abs(baseband(time)+std::complex<double>{1,0})<1e-12,
                "single auxiliary punctuation did not retain its inverted underlying state");
    require(std::abs(baseband(.24L))<1e-12&&std::abs(baseband(.28L)+std::complex<double>{1,0})<1e-12,
            "auxiliary reversal moved from240ms or unexpectedly restored at280ms");
    const auto late_start=auxiliary_baseband(generator,pilot,plan,.24L,8000,{},.22L);
    require(std::abs(late_start-std::complex<double>{1,0})<1e-10,
        "partial dwell punctuated without20ms preceding tone");
    const auto early_end=auxiliary_baseband(generator,pilot,plan,.24L,8000,.26L,0.L);
    require(std::abs(early_end-std::complex<double>{1,0})<1e-10,
        "partial dwell emitted punctuation without its20ms following flank");
    h::Generator live_generator(options,plan,0,false);
    const auto before_stop=auxiliary_baseband(live_generator,pilot,plan,.249L,8000,.4L,0.L);
    const auto after_stop=auxiliary_baseband(live_generator,pilot,plan,.25L,8000,.26L,0.L);
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
        if(time>=.02L&&time<.22L) {
            const auto main=pilot.mix_analytic({},time,8000);
            const auto auxiliary=generator.mix_analytic({},time,8000)-main;
            pilot_power+=std::norm(main);auxiliary_power+=std::norm(auxiliary);
            straight_power+=std::norm(generator.mix_analytic({},time,8000));
            require(std::abs(main/(plan.helper_gain*h::pilot_amplitude_fraction*oscillator(plan.frequency_hz,time))-
                std::complex<double>{1,0})<1e-12,"pure pilot unexpectedly modulated");
        }
    }
    require(std::abs(pilot_power/auxiliary_power-9)<1e-11&&
            std::abs(straight_power-pilot_power-auxiliary_power)<1e-9,
            "straight interval did not preserve90/10 power or coherent total accounting");
    // Known auxiliary values, rather than a composite subtraction used as
    // truth, expose any pilot reversal at punctuation or metadata endpoints.
    options.callsign="ABCDEFGHIJKLMNOPQRS";
    h::Generator metadata(options,plan);
    const auto packet=h::callsign_packet(options.callsign);
    const auto check_pilot=[&](long double time,std::complex<double> auxiliary) {
        const auto total=metadata.mix_analytic({},time,8000);
        const auto aux=auxiliary*plan.helper_gain*h::auxiliary_amplitude_fraction*
            oscillator(plan.auxiliary_frequency_hz,time);
        require(std::abs((total-aux)/(plan.helper_gain*h::pilot_amplitude_fraction*oscillator(plan.frequency_hz,time))-
            std::complex<double>{1,0})<1e-11,"auxiliary punctuation or metadata reversed the pilot");
    };
    check_pilot(.235L,{1,0});check_pilot(.24L,{});check_pilot(.244L,{-1,0});
    check_pilot(.28L,{-1,0});check_pilot(.396L,{-1,0});
    for(std::size_t i=0;i<packet.count;++i)
        check_pilot(.388L-(packet.count-i-1)*.008L,-h::apsk_point(packet.symbols[i]));
    require(generator.mix(0,.15L,8000,.15L)==0,"finite transmission lacks final helper ramp");
    require(std::abs(generator.mix_analytic({},.148L,8000,.15L))<=peak*.500000001,
            "finite transmission final4ms envelope wrong");
    require(generator.mix(0,.15L,8000,{},.15L)==0,"non-dwell transmission start lacks helper ramp");
    require(std::abs(generator.mix_analytic({},.152L,8000,{},.15L))<=peak*.500000001,
            "non-dwell transmission start4ms envelope wrong");
}
void protected_streaming_endpoint() {
    for(const auto end:{0.L,.1L,.235999L,.268L,.3L,.4L,.635999L})
        require(h::protected_endpoint(0,0,end)==end,"helper extends an endpoint outside its punctuation guard");
    for(const auto end:{.236L,.24L,.25L,.26L,.267999L})
        require(std::abs(h::protected_endpoint(0,0,end)-.268L)<1e-15,
            "helper streaming stop did not protect the single reversal and its following straight-tone flank");
    require(std::abs(h::protected_endpoint(0,.212L,.24L)-.268L)<1e-15&&
        h::protected_endpoint(0,.212001L,.24L)==.24L,
        "helper endpoint ignored the preceding clean-tone requirement");
    require(std::abs(h::protected_endpoint(1,-1.L,.04L)-.068L)<1e-15,
        "protected endpoint lost the UTC dwell grid at odd epochs");
    require(std::abs(h::protected_endpoint(2,-2.L,-1.76L)+2-.268L)<1e-15,
        "protected endpoint depends on the physical epoch split");
    rejects([]{h::protected_endpoint(0,1,0);});
    rejects([]{h::protected_endpoint(0,0,std::numeric_limits<long double>::infinity());});
    h::Options options;options.power_w=100;options.timecode=false;
    const auto plan=h::plan(options,1000,200,8000);
    h::Generator generator(options,plan);
    const auto pilot=pilot_only(plan);
    const auto before=generator.mix_analytic({},.25L,8000,.4L,0);
    const auto after=generator.mix_analytic({},.25L,8000,h::protected_endpoint(0,0,.26L),0);
    require(std::abs(before-after)<1e-15,"streaming stop retroactively changed punctuation phase");
    for(const auto time:{.244L,.252L,.263999L}) {
        const auto clean=auxiliary_baseband(generator,pilot,plan,time,8000,.268L,0);
        require(std::abs(clean+std::complex<double>{1,0})<1e-11,
            "protected streaming endpoint lacks20ms inverted auxiliary carrier");
    }
    require(generator.mix(0,.268L,8000,.268L,0)==0,"protected endpoint did not finish its helper ramp");
    // The exact and immediately adjacent representable coordinates must
    // preserve the reversal under either UTC epoch split. Normalized-calendar
    // roundoff may conservatively include an adjacent onset, never erase it.
    for(const auto epoch:{0ULL,1ULL,1800000000ULL}) {
        const auto origin=epoch%2?-.2L:0.L,begin=origin+.236L,end=origin+.268L;
        const h::Generator dated(options,plan,epoch);
        long double previous=0;
        for(const auto stop:{std::nextafter(begin,-std::numeric_limits<long double>::infinity()),
                             begin,std::nextafter(begin,std::numeric_limits<long double>::infinity())}) {
            const auto protected_end=h::protected_endpoint(epoch,origin,stop);
            require(protected_end>=stop&&std::abs(protected_end-end)<1e-15L&&
                    (!previous||protected_end+1e-15L>=previous),
                    "adjacent punctuation onset lost monotone reversal protection");
            previous=protected_end;
            const auto emitted=origin+.25L;
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
        require(std::abs(h::protected_endpoint(epoch,origin+.212L,begin)-end)<1e-15L&&
                h::protected_endpoint(epoch,origin+.212001L,begin)==begin,
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
    // One complete dwell has zero endpoints; no analysis window hides ramps,
    // punctuation or modulation sidebands. 2.5Hz FFT bins at10240Hz.
    std::array<std::uint8_t,32> master{};
    for(std::size_t i=0;i<master.size();++i)master[i]=static_cast<std::uint8_t>(i+23);
    const datapump::Crypto key(master);
    struct MetadataCase {std::string_view callsign;bool encrypted,timecode;};
    for(const auto& test:std::array<MetadataCase,5>{{{"",false,false},{"",false,true},{"",true,true},
            {"N0CALL",false,true},{"ABCDEFGHIJKLMNOPQRS",false,true}}}) {
        h::Options options;options.power_w=100;options.callsign=test.callsign;options.timecode=test.timecode;
        const auto plan=h::plan(options,1000,200,10240);
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
    // Exercise the actual planned guard, both real carrier images, and broad
    // data bands. The worst allowed ratio must expand the helper guard rather
    // than change data bandwidth, power or sample rate.
    for(const auto bandwidth:{200.,6000.})for(const auto ratio_db:{0.,40.,60.,90.})for(const auto encrypted:{false,true}) {
        constexpr std::uint32_t rate=40960;
        h::Options options;options.data_power_w=1e-6;
        options.power_w=options.data_power_w*std::pow(10.,ratio_db/10);
        options.callsign=encrypted?"":"ABCDEFGHIJKLMNOPQRS";
        const auto plan=h::plan(options,4500,bandwidth,rate);
        h::Generator generator(options,plan,0,true,encrypted?std::optional<datapump::Crypto>{key}:std::nullopt);
        std::vector<std::complex<double>> samples(16384);
        for(std::size_t i=0;i<samples.size();++i)
            samples[i]=generator.mix(0,static_cast<long double>(i)/rate,rate)/plan.helper_gain;
        fft(samples);
        double leaked=0;
        for(std::size_t i=0;i<samples.size();++i) {
            const auto frequency=(i<=samples.size()/2?static_cast<double>(i):
                static_cast<double>(i)-static_cast<double>(samples.size()))*2.5;
            if(std::abs(std::abs(frequency)-4500)<=bandwidth/2)leaked+=std::norm(samples[i]);
        }
        const auto fraction=2*leaked/static_cast<double>(samples.size()*samples.size());
        const auto relative_to_data=fraction*std::pow(10.,ratio_db/10);
        std::cout<<"helper guard encrypted="<<encrypted<<" ratio_dB="<<ratio_db<<" data_bandwidth="<<bandwidth
                 <<" carrier="<<plan.frequency_hz<<" leakage/data_dB="<<10*std::log10(relative_to_data)<<'\n';
        require(relative_to_data<1e-3,"power-dependent guard exceeded data leakage mask");
    }
    // A finite transmission may begin in the middle of a dwell. Test both
    // sides of a hop boundary; explicit start/end ramps must keep the same
    // occupied-bandwidth target without adding any payload samples.
    for(const auto start:{.153L,.3995L}) {
        constexpr std::uint32_t rate=40960;
        h::Options options;options.power_w=100;options.callsign="N0CALL";
        const auto plan=h::plan(options,1000,200,rate);
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
        constexpr std::uint32_t rate=40960;
        h::Options options;options.data_power_w=1e-6;
        options.power_w=options.data_power_w*std::pow(10.,ratio_db/10);
        options.callsign="ABCDEFGHIJKLMNOPQRS";
        const auto plan=h::plan(options,4500,200,rate);
        h::Generator generator(options,plan);
        std::vector<std::complex<double>> samples(32768);
        for(std::size_t i=0;i<samples.size();++i)
            samples[i]=generator.mix(0,static_cast<long double>(i)/rate,rate,
                endpoints.second,endpoints.first)/plan.helper_gain;
        fft(samples);double leaked=0;
        for(std::size_t i=0;i<samples.size();++i) {
            const auto frequency=(i<=samples.size()/2?static_cast<double>(i):
                static_cast<double>(i)-static_cast<double>(samples.size()))*1.25;
            if(std::abs(std::abs(frequency)-4500)<=100)leaked+=std::norm(samples[i]);
        }
        // Average over active source duration, not the zero-padded FFT record.
        const auto active_fraction=static_cast<double>((endpoints.second-endpoints.first)/.8L);
        const auto relative_to_data=2*leaked/static_cast<double>(samples.size()*samples.size())*
            std::pow(10.,ratio_db/10)/active_fraction;
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
        const auto plan=h::plan(options,500,100,rate);
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
    try {power_and_placement();metadata_vectors();timecode_generation();waveform_coordinates();protected_streaming_endpoint();spectrum();tracker();tracker_lifetime_admission();}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
    std::cout<<"helper carrier checks passed\n";return 0;
}
