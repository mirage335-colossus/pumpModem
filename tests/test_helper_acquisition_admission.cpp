#include "../src/live_helper_acquisition_admission.hpp"
#include "../src/helper_reference_search.hpp"
#include "../src/live_helper_measurement_admission.hpp"
#include "../src/live_helper_constructor_radius.hpp"
#include "datapump/tuning.hpp"
#include <cmath>
#include <iostream>
using namespace datapump;
namespace {
void check(bool x,const char* text){if(!x)throw Error(text);}
void exercise() {
    auto c=tuning::resolve(36,8,tuning::PatternMode::auto_pattern,false,1500).config;
    c.oscillator_search.emplace();auto& o=*c.oscillator_search;
    o.lf={100,.1};o.rf={100,.1};o.margin=3;o.rf_shift_hz=30000000;
    o.reference=modem::OscillatorReference::independent_audio;
    constexpr double helper=2000;
    const auto original=modem::oscillator_pattern_search(c);
    auto candidate=modem::helper_reference_search(c,.15,helper,0,0,
        modem::HelperReferenceClockRelation::common_corrected_source,false);
    modem::PatternSearch reduced;modem::apply_helper_reference_search(std::move(candidate),reduced);
    check(live::detail::helper_acquisition_useful(c,helper,.15,reduced,original,false),
        "measured reduced RF search failed admission");
    check(reduced.hypotheses.size()<original.hypotheses.size(),"test did not reduce actual descriptor count");
    auto incomplete=reduced;incomplete.complex_clock_coverage=false;
    check(!live::detail::helper_acquisition_useful(c,helper,.15,incomplete,original,false),
        "incomplete clock coverage qualified a receiver");
    check(!live::detail::helper_acquisition_useful(c,helper,.15,reduced,original,true),
        "unchanged original broad prior qualified a new receiver");
    check(!live::detail::helper_acquisition_useful(c,helper,10000,reduced,original,false),
        "unhelpful reference qualified a reduced search");
    auto same_centers=original;modem::PatternSearch fewer_clock_pairs;
    same_centers.frequency.count=5;same_centers.hypotheses.clear();
    for(const auto f:{-2.,-1.,0.,1.,2.}) {
        same_centers.hypotheses.push_back({f,-1});same_centers.hypotheses.push_back({f,1});
        fewer_clock_pairs.hypotheses.push_back({f,0});
    }
    fewer_clock_pairs.complex_frequency_coverage=true;fewer_clock_pairs.complex_clock_coverage=true;
    check(!live::detail::helper_acquisition_useful(c,helper,.15,fewer_clock_pairs,same_centers,false),
        "fewer timing pairs with the same RF centers claimed RF narrowing");
    c.oscillator_search->rf.accuracy_ppm=0;c.oscillator_search->lf.accuracy_ppm=0;
    const auto minimum=modem::oscillator_pattern_search(c);
    check(minimum.frequency.count<=3,"minimum-search fixture has an unexpected frequency lattice");
    auto minimum_search=modem::helper_reference_search(c,0,helper,0,0,
        modem::HelperReferenceClockRelation::common_corrected_source,true);
    modem::PatternSearch min;modem::apply_helper_reference_search(std::move(minimum_search),min);
    const auto cell=modem::native_pattern_frequency_cell_radius(c);
    check(live::detail::helper_acquisition_useful(c,helper,cell/2,min,minimum,true),
        "precise reference failed the already-minimum lattice");
    check(!live::detail::helper_acquisition_useful(c,helper,2*cell,min,minimum,true),
        "poor reference qualified merely because the original lattice was small");
    auto oversized=min;oversized.hypotheses.push_back({1,0});
    check(!live::detail::helper_acquisition_useful(c,helper,cell/2,oversized,minimum,true),
        "minimum original allowed a larger conditional catalogue");
    live::detail::HelperMeasurementAdmission timer;
    constexpr std::uint32_t rate=8000;
    check(timer.observe(0,rate,rate,rate,true),"test detector failed to begin observation");
    check(!timer.ready(37ULL*rate-1,rate) && timer.ready(37ULL*rate,rate),
        "local observation deadline changed");
    auto short_profile=c;short_profile.integration_seconds=1;
    short_profile.oscillator_search->rf.accuracy_ppm=.001;
    short_profile.oscillator_search->lf.accuracy_ppm=0;
    auto long_profile=short_profile;long_profile.integration_seconds=128;
    const auto short_original=modem::oscillator_pattern_search(short_profile);
    const auto long_original=modem::oscillator_pattern_search(long_profile);
    check(short_original.frequency.count<=3 && long_original.frequency.count>3 &&
          live::detail::helper_profile_original_minimum(short_profile,0) &&
          !live::detail::helper_profile_original_minimum(long_profile,0),
        "profile fixture did not represent a short minimum and a broad long search");
    const auto minimum_cell=std::min(modem::native_pattern_frequency_cell_radius(short_profile),
        modem::native_pattern_frequency_cell_radius(long_profile));
    const auto preserve=[&](double uncertainty){return modem::helper_reference_prefers_original_prior(short_profile,
        uncertainty,helper,0,modem::HelperReferenceClockRelation::common_corrected_source,0);};
    check(preserve(.1) && !live::detail::helper_frontend_basis_useful(short_profile,.1,true,false,minimum_cell),
        "coarse actual reference froze a broad long-profile frontend at the deadline");
    check(!preserve(.001) && live::detail::helper_frontend_basis_useful(short_profile,.001,false,false,minimum_cell),
        "later actual precise reference could not select a narrowed frontend");
    check(timer.ready(38ULL*rate,rate),"later precision reset or backdated the observation timer");
    // Default sync with no RF error still has an independent LF/UTC domain.
    // It can use translated measured precision without discarding that domain.
    auto default_sync=tuning::resolve(36,8,tuning::PatternMode::auto_pattern,false,1500).config;
    default_sync.oscillator_search.emplace();auto& defaults=*default_sync.oscillator_search;
    defaults.lf={.001,.1};defaults.rf={0,0};defaults.margin=3;defaults.rf_shift_hz=30000000;
    defaults.reference=modem::OscillatorReference::independent_audio;
    const auto correction=modem::utc_transmit_rate_limit(default_sync);
    const auto default_original=modem::oscillator_pattern_search(default_sync,correction);
    const auto qualify_default=[&](double u) {
        auto result=modem::helper_reference_search(default_sync,
            live::detail::helper_score_constructor_radius(default_sync,u),2400,0,correction,
            modem::HelperReferenceClockRelation::common_corrected_source,false,
            modem::PatternUtcClockGrid::complete_expanded_domain);
        check(result.catalogue.requested_clock_half_width_ppm>=default_original.requested_clock_half_width_ppm &&
              !result.catalogue.limited && !result.catalogue.timing_correction_unavailable,
              "translated Default-sync search lost original UTC/clock coverage");
        modem::PatternSearch search;modem::apply_helper_reference_search(std::move(result),search);
        return live::detail::helper_acquisition_useful(default_sync,2400,u,search,default_original,false);
    };
    check(!qualify_default(.1) && qualify_default(.001),
        "Default sync either forced an imprecise bank or stalled after useful precision");
    c.oscillator_search.reset();
    check(!live::detail::helper_acquisition_useful(c,helper,2*cell,{},std::nullopt,false) &&
        !live::detail::helper_acquisition_useful(c,helper,cell/2,{},std::nullopt,false),
        "no-policy search gained an unproved default-clock catalogue");
    std::cout<<"helper acquisition precision and candidate coverage passed\n";
}
}
int main()try {exercise();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
