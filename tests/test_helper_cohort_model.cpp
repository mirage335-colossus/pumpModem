// Reuse the actual encrypted B configuration and immutable native catalogue.
#define main helper_joint_component_main
#include "test_helper_joint_clock.cpp"
#undef main
#include "../src/live_helper_cohort_model.hpp"
#include <cfenv>

namespace {
void cohort_model() {
    namespace d=live::detail;
    auto c=config();
    const auto original_search=search(c,0);
    modem::PatternReceiver receiver(c,64*1024*1024,original_search);
    const auto original=receiver.acquisition_state();
    check(original.supported && original.admitted_clock_half_width_ppm &&
          *original.admitted_clock_half_width_ppm>300,"cohort test lost actual immutable B DATA domain");
    const audio::CaptureTransportRecipe nominal{audio::CaptureTransportKind::fixed_nominal_audio,57600,c.sample_rate,0,0};
    constexpr double H=5275;
    const auto C=modem::utc_transmit_rate_limit(c);
    const auto physical=c.oscillator_search->lf.accuracy_ppm*c.oscillator_search->margin;
    const auto rf=c.oscillator_search->rf_shift_hz*c.oscillator_search->rf.accuracy_ppm*c.oscillator_search->margin*1e-6;
    check(C>0 && physical==300,"cohort test has no actual declared UTC steering envelope");
    for(const auto sign:{-1.,1.}) {
        const auto b=sign*550e-6,a=sign*275e-6,correction=(1+b)/(1+a)-1;
        check(std::abs(b)*1e6>physical && std::abs(a)*1e6<=physical &&
              correction!=0 && std::abs(correction)<=C,"cohort truth lies outside actual a/c producer policy");
        auto r=frequency(mapping(H),sign*rf+H*b,.001,14400);
        const auto old=d::helper_current_link_measurement(c,H,r,r.sample+1,1);
        check(old.authority==h::LinkAuthority::declined,"physical-a gate unexpectedly accepted an outside-box corrected source");
        const auto common=d::helper_common_source_current_measurement(c,H,r,r.sample+1,1,original,true,false,nominal);
        auto raw_reference=r;raw_reference.sample_rate=nominal.raw_rate;
        raw_reference.sample*=4;raw_reference.frequency_first_sample*=4;
        const auto actual_raw_mapping=mapping(H,nominal.raw_rate);
        const auto raw_common=d::helper_common_source_current_measurement(c,H,raw_reference,raw_reference.sample+1,1,
            original,true,false,nominal,d::HelperCohortReferenceTimebase::captured_raw,&actual_raw_mapping);
        check(d::helper_common_source_current_measurement(c,H,raw_reference,raw_reference.sample+1,1,
                original,true,false,nominal,d::HelperCohortReferenceTimebase::captured_raw).authority==h::LinkAuthority::declined,
            "failed owned raw mapping binding reached a synthetic fixed fallback");
        auto changed_mapping=actual_raw_mapping;changed_mapping.settled_first_sample=raw_reference.sample;
        check(d::helper_common_source_current_measurement(c,H,raw_reference,raw_reference.sample+1,1,
                original,true,false,nominal,d::HelperCohortReferenceTimebase::captured_raw,&changed_mapping).authority==h::LinkAuthority::declined,
            "common cohort accepted raw frequency support straddling its actual settled mapping");
        changed_mapping=actual_raw_mapping;++changed_mapping.capture_generation;
        check(d::helper_common_source_current_measurement(c,H,raw_reference,raw_reference.sample+1,1,
                original,true,false,nominal,d::HelperCohortReferenceTimebase::captured_raw,&changed_mapping).authority==h::LinkAuthority::declined,
            "common cohort crossed the owned capture domain");
        check(raw_common.authority==h::LinkAuthority::current_measurement &&
              d::helper_common_source_current_measurement(c,H,raw_reference,raw_reference.sample+1,1,
                original,true,false,nominal).authority==h::LinkAuthority::declined,
            "owned raw publication was reinterpreted as a logical-rate date");
        check(common.authority==h::LinkAuthority::current_measurement &&
              common.audio_fraction.lower<=b && common.audio_fraction.upper>=b,
            "typed common-source gate rejected feasible nonzero UTC-corrected producer truth");
        check(d::helper_current_link_measurement(c,H,r,r.sample+1,1).authority==h::LinkAuthority::declined,
            "common-source query mutated or globally widened the physical diagnostic model");
        check(d::helper_common_source_current_measurement(c,H,r,r.sample,1,original,true,false,nominal).authority==h::LinkAuthority::declined,
            "common-source gate admitted a future publication at an exclusive captured frontier");
        auto missing=original;missing.admitted_clock_half_width_ppm.reset();
        check(!d::helper_common_source_native_domain(c,missing,true,false,nominal),"common cohort invented a missing DATA clock domain");
        auto recentered=original;recentered.frequency_origin_hz=-0.;
        check(!d::helper_common_source_native_domain(c,recentered,true,false,nominal),"common cohort crossed an unproved signed-zero origin");
        for(const auto raw:{false,true})for(const auto phase:{false,true})if(!raw || phase)
            check(d::helper_common_source_current_measurement(c,H,r,r.sample+1,1,original,raw,phase,nominal).authority==h::LinkAuthority::declined,
                "common-only qualification reached translated/phase consumers");
        auto unknown=nominal;unknown.kind=audio::CaptureTransportKind::unknown;
        check(!d::helper_common_source_native_domain(c,original,true,false,unknown),"unknown/time-warped capture inherited common source authority");
        unknown=nominal;++unknown.logical_rate;
        check(!d::helper_common_source_native_domain(c,original,true,false,unknown),"common source proof crossed capture rate provenance");
        auto continuous=c;continuous.fake_hopping.reset();
        check(!d::helper_common_source_native_domain(continuous,original,true,false,nominal),"unsupported continuous/compact consumer borrowed native calendar proof");
        const auto discover=d::helper_cohort_discovery_radius(c,H,true);
        check(discover && *discover>=std::abs(sign*rf+H*b),"union discovery envelope lost feasible corrected source");
        const auto data_width=modem::oscillator_pattern_search(c,C).frequency.requested_half_width_hz;
        const auto tracker=d::helper_cohort_tracker_radius(c,H,data_width,true);
        check(data_width<std::abs(sign*rf+H*b) && tracker>=std::abs(sign*rf+H*b),
            "cohort preliminary capture gate retained DATA-only width");
        check(d::helper_cohort_tracker_radius(c,H,1000,true)>=1000,
            "cohort admission discarded the requested frequency range");
        std::cout<<"cohort B b_ppm="<<b*1e6<<" a_ppm="<<a*1e6<<" c_ppm="<<correction*1e6
            <<" physical_declined=1 common_current=1 original_DATA_halfwidth_ppm="<<*original.admitted_clock_half_width_ppm
            <<" discovery_halfwidth_hz="<<*discover<<'\n';
    }
    // A shares one oscillator; the actual UTC policy is zero. Its large RF
    // capture requirement is unchanged, rather than hidden by a wider a box.
    auto shared=c;shared.oscillator_search->reference=modem::OscillatorReference::shared_radio;
    shared.oscillator_search->rf.accuracy_ppm=100;
    check(modem::utc_transmit_rate_limit(shared)==0,"shared-radio A unexpectedly acquired UTC rate steering");
    const auto base=d::helper_cohort_discovery_radius(shared,H,false),expanded=d::helper_cohort_discovery_radius(shared,H,true);
    check(base && expanded && *base==*expanded && *base>=9000,
        "common discovery changed shared A's original clock or lost wide capture coverage");
    check(d::helper_cohort_tracker_radius(shared,H,1,true)>=*base,
        "cohort admission clipped a wide oscillator-derived reference");
    check(!d::helper_common_source_native_domain(shared,original,true,false,nominal),"shared A used independent common-b proof");
    // 128-second EME remains unsupported by this NEW typed native gate; its
    // original compact frequency/clock path and full UTC union remain intact.
    auto eme=c;eme.sample_rate=6000;eme.bandwidth_hz=1;eme.dsss=false;eme.dsss_factor=1;
    eme.oscillator_search=modem::OscillatorSearchConfig{{.0001,0},{.0001,0},5800000000.,3};
    eme.fake_hopping=modem::resolve_fhss_power(*c.fake_hopping,eme);
    const auto eme_domain=modem::oscillator_pattern_search(eme,modem::utc_transmit_rate_limit(eme));
    modem::PatternAcquisitionState compact_only;
    const audio::CaptureTransportRecipe eme_capture{audio::CaptureTransportKind::sampled_physical_halfband,48000,6000,0,0};
    check(modem::symbol_sample_count(eme)==128ULL*eme.sample_rate &&
          !d::helper_common_source_native_domain(eme,compact_only,true,false,eme_capture),
        "new native qualification silently claimed 128-second compact support");
    const auto E=2000.;
    const auto old=d::helper_cohort_discovery_radius(eme,E,false),full=d::helper_cohort_discovery_radius(eme,E,true);
    check(old && full && *full>=*old && eme_domain.requested_clock_half_width_ppm>.0003,
        "EME discovery failed to union the original declared DATA/UTC envelope");
    std::cout<<"cohort A UTC_c=0 discovery_hz="<<*base<<"; EME physical_halfwidth_ppm=.0003 DATA_halfwidth_ppm="
        <<eme_domain.requested_clock_half_width_ppm<<" discovery_extra_hz="<<*full-*old
        <<" native_common_only_eligible=0 compact_original_unchanged=1\n";
    const auto previous=std::fegetround();std::fesetround(FE_DOWNWARD);
    const auto unsupported=d::helper_cohort_discovery_radius(c,H,true);std::fesetround(previous);
    check(!unsupported,"unsupported arithmetic created a discovery certificate");
}
}
int main(){try{cohort_model();std::cout<<"typed common-source cohort gates passed\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
