#include "datapump/helper_carrier.hpp"
#include "datapump/helper_receiver.hpp"
#include "helper_auxiliary_fixture.hpp"
#include "../src/punctuation_clock.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <vector>

using namespace datapump;
namespace h=datapump::helper;
namespace {
void check(bool value,const char* text){if(!value)throw Error(text);}
constexpr std::uint32_t rate=20000;
h::PunctuationClockDomain domain(std::uint64_t gauge=1) {return {1,gauge,0,1,rate};}
h::Reference marker(long double position,double uncertainty=1,std::uint64_t generation=1,std::uint64_t origin=0) {
    const auto whole=static_cast<std::uint64_t>(std::floor(position));
    h::Reference r;r.sample_rate=rate;r.frequency_valid=true;r.frequency_hz=2000;r.frequency_uncertainty_hz=.1;
    r.timing_valid=true;r.punctuation={generation,origin+whole,origin+whole-100,origin+whole+100,
        static_cast<double>(position-whole),uncertainty,true};
    r.sample=r.punctuation.last_sample+7;
    r.dwell_start_seconds=static_cast<double>(origin+whole)/rate-.24;r.dwell_uncertainty_seconds=uncertainty/rate;
    return r;
}
h::PunctuationClockRequest request(const h::Reference& first,const h::Reference& last,
        h::PunctuationClockModel model=h::PunctuationClockModel::constant_rate_over_requested_support) {
    return {first.punctuation.sample+2,last.punctuation.sample-2,last.sample+1,1000,model,{}};
}
void actual_dates_and_sign() {
    for(const auto ppm:{-175.,100.}) {
        h::PunctuationClock clock;h::Reference first,last;
        const auto ratio=1+static_cast<long double>(ppm)*1e-6L;
        for(unsigned i=0;i<12;++i) {
            auto r=marker(10000+(.24L+.4L*i)*rate/ratio);
            check(clock.observe(r,domain(),r.sample+1)==h::PunctuationClockReason::accepted,"actual marker was declined");
            if(!i)first=r;last=r;
        }
        const auto fit=clock.qualify({-300,300},request(first,last));
        check(fit.qualified_for_acquisition && fit.affine_aperture && fit.branch_count==1 &&
              fit.physical_audio_lower_ppm<=ppm && fit.physical_audio_upper_ppm>=ppm && fit.physical_audio_upper_ppm-fit.physical_audio_lower_ppm<50,
            "marker spacing used the inverse audio-clock sign or lost its uncertainty interval");
        std::size_t lanes=0;
        for(const auto center:{-300.,-200.,-100.,0.,100.,200.,300.})
            lanes+=center+50>=fit.physical_audio_lower_ppm && center-50<=fit.physical_audio_upper_ppm;
        check(lanes<=2,"precise actual markers did not narrow seven physical audio-clock cells");
        const auto unknown=clock.qualify({-300,300},request(first,last,h::PunctuationClockModel::unknown));
        check(unknown.aperture_mean_known && !unknown.qualified_for_acquisition &&
              unknown.reason==h::PunctuationClockReason::unknown_cross_time_model,
            "aperture mean inferred unknown thermal/future evolution");
        const auto count=clock.observation_count();
        auto carried=last;carried.sample+=8000;carried.dwell_start_seconds+=.4;
        check(clock.observe(carried,domain(),carried.sample+1)==h::PunctuationClockReason::duplicate_marker && clock.observation_count()==count,
            "nominal-grid republication manufactured an independent marker");
        auto rewritten=last;rewritten.punctuation.fraction+=.001;
        check(clock.observe(rewritten,domain(),rewritten.sample+1)==h::PunctuationClockReason::changed_marker,
            "marker identity could change its crossing without a new observation");
        check(last.frequency_valid && last.frequency_hz==2000,"optional clock work changed frequency qualification");
    }
}
void aliases_and_support() {
    h::PunctuationClock clock;
    auto first=marker(10000),last=marker(10000+200.L*rate);
    clock.observe(first,domain(),first.sample+1);clock.observe(last,domain(),last.sample+1);
    const auto ambiguous=clock.qualify({-10000,10000},request(first,last));
    check(ambiguous.aperture_mean_known && ambiguous.branch_count>1 && !ambiguous.qualified_for_acquisition &&
          ambiguous.reason==h::PunctuationClockReason::ambiguous_alias,
        "long-gap marker spacing silently chose a nearest dwell count");
    check(std::any_of(ambiguous.mean_branches.begin(),ambiguous.mean_branches.begin()+ambiguous.branch_count,
        [](const auto& branch){return branch.dwells==500 && branch.lower_ppm<=0 && branch.upper_ppm>=0;}),
        "all-compatible alias union discarded the actual dwell branch");
    auto huge=marker(10000+2000.L*rate);clock.observe(huge,domain(),huge.sample+1);
    check(clock.qualify({-10000,10000},request(first,huge)).reason==h::PunctuationClockReason::alias_capacity,
        "unbounded alias set was truncated instead of declining optional work");
    clock.reset(domain());first=marker(10000);last=marker(10000+4.L*rate);
    clock.observe(first,domain(),first.sample+1);clock.observe(last,domain(),last.sample+1);
    auto future=request(first,last);future.last_sample=last.punctuation.sample+10;
    check(clock.qualify({-300,300},future).reason==h::PunctuationClockReason::unsupported_future_model,
        "accuracy ppm was treated as a future rate-slew bound");
    future.declared_rate_slew_ppm_per_second=1;
    check(!clock.qualify({-300,300},future).qualified_for_acquisition,"unsupported nonzero future slew was silently treated as zero");
    future.declared_rate_slew_ppm_per_second=0;
    check(clock.qualify({-300,300},future).qualified_for_acquisition && clock.qualify({-300,300},future).explicit_zero_slew,
        "explicit conditional constant-rate use was not reported");
    future.first_sample=first.punctuation.sample-1;
    check(clock.qualify({-300,300},future).reason==h::PunctuationClockReason::uncovered_scored_support,
        "clock support was extended before its first actual marker");
    auto stale=request(first,last);stale.captured_end_sample+=2000;
    check(clock.qualify({-300,300},stale).reason==h::PunctuationClockReason::stale_marker,
        "carried punctuation supplied unlimited clock freshness");
    const auto hidden=clock.qualify({-300,300},{first.punctuation.sample+2,last.punctuation.sample-2,
        last.punctuation.last_sample,1000,h::PunctuationClockModel::constant_rate_over_requested_support,{}});
    check(!hidden.aperture_mean_known && hidden.observations==1,"future marker support entered an older receiver frontier");
    auto invalid=last;invalid.punctuation.last_sample=invalid.sample+1;
    check(clock.observe(invalid,domain(),invalid.sample+1)==h::PunctuationClockReason::future_marker,
        "a marker raw support endpoint escaped its publication");
    const auto old_count=clock.observation_count();
    check(clock.observe(last,domain(2),last.sample+1)==h::PunctuationClockReason::domain_mismatch &&
          clock.observation_count()==old_count,"unannounced RF gauge changed the owner domain");
    clock.reset(domain(2));
    check(clock.observe(last,domain(2),last.sample+1)==h::PunctuationClockReason::accepted && clock.observation_count()==1,
        "explicit RF gauge handoff retained the previous marker history");
    check(clock.observe(first,domain(),first.sample+1)==h::PunctuationClockReason::domain_mismatch && clock.observation_count()==1,
        "delayed old-domain publication rolled the owner back");
}
void endpoint_and_numeric_guards() {
    h::PunctuationClock clock;const auto first=marker(10000),last=marker(90000);
    check(clock.observe(first,domain(),first.sample)==h::PunctuationClockReason::future_marker && !clock.observation_count(),
        "publication at exclusive capture end entered the history");
    check(clock.observe(first,domain(),first.sample+1)==h::PunctuationClockReason::accepted,
        "last captured publication was not admitted");
    check(clock.observe(last,domain(),last.sample+1)==h::PunctuationClockReason::accepted,"second boundary marker was declined");
    auto at_end=request(first,last);at_end.last_sample=at_end.captured_end_sample;
    check(clock.qualify({-300,300},at_end).reason==h::PunctuationClockReason::uncovered_scored_support,
        "a scored sample at the exclusive capture end was treated as captured");
    auto evidence_at_end=request(first,last);evidence_at_end.captured_end_sample=last.punctuation.last_sample;
    check(clock.qualify({-300,300},evidence_at_end).observations==1,
        "actual marker evidence at exclusive capture end qualified an older frontier");
    const auto count=clock.observation_count();
    const auto previous=std::fegetround();check(std::fesetround(FE_DOWNWARD)==0,"test cannot set directed rounding");
    const auto observed=clock.observe(marker(98000),domain(),99000);
    const auto qualified=clock.qualify({-300,300},request(first,last));
    const auto restored=std::fesetround(previous);
    check(restored==0 && observed==h::PunctuationClockReason::unsupported_numeric_environment &&
          qualified.reason==h::PunctuationClockReason::unsupported_numeric_environment && clock.observation_count()==count,
        "unsupported rounding mode mutated marker history or qualified a clock");
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)
    const auto csr=_mm_getcsr();_mm_setcsr(csr|(1U<<15));
    const auto ftz=clock.qualify({-300,300},request(first,last));_mm_setcsr(csr);
    check(ftz.reason==h::PunctuationClockReason::unsupported_numeric_environment,
        "flush-to-zero arithmetic qualified punctuation intervals");
#endif
    auto replacement=domain();replacement.provider_identity=2;
    check(clock.observe(last,replacement,last.sample+1)==h::PunctuationClockReason::domain_mismatch && clock.observation_count()==count,
        "Receiver reconstruction silently reused its provider identity");
    clock.reset(replacement);
    check(clock.observe(last,replacement,last.sample+1)==h::PunctuationClockReason::accepted && clock.observation_count()==1,
        "explicit provider handoff did not reset the old aperture");
    check(clock.observe(first,domain(),first.sample+1)==h::PunctuationClockReason::domain_mismatch && clock.observation_count()==1,
        "delayed old provider reset a new provider's history");
}
void high_dates_and_fresh_window() {
    h::PunctuationClock clock;const auto origin=UINT64_MAX-10000000;
    auto first=marker(10000.25L,.1,1,origin),last=marker(10000.25L+4.L*rate,.1,1,origin);
    clock.observe(first,domain(),first.sample+1);clock.observe(last,domain(),last.sample+1);
    const auto dated=clock.qualify({-300,300},request(first,last));
    check(dated.qualified_for_acquisition && dated.physical_audio_lower_ppm<=0 && dated.physical_audio_upper_ppm>=0,
        "short marker spacing near UINT64_MAX lost integer date precision");
    clock.reset(domain());long double time=.5L;h::Reference start,end,second;
    for(unsigned i=0;i<16;++i) {
        auto r=marker(time*rate,.05);clock.observe(r,domain(),r.sample+1);if(!i)start=r;if(i==1)second=r;end=r;time+=.4L/1.0001L;
    }
    const auto old=clock.qualify({-300,300},request(start,end));check(old.qualified_for_acquisition,"initial affine window did not qualify");
    auto bend=marker(time*rate+10,.05);clock.observe(bend,domain(),bend.sample+1);
    const auto rejected=clock.qualify({-300,300},request(second,bend));
    check(!rejected.qualified_for_acquisition && rejected.aperture_mean_known &&
          rejected.reason==h::PunctuationClockReason::non_affine_aperture,
        "non-affine cross-time curvature supplied a constant-rate clock");
    // A genuinely fresh constant window can move away from the first interval.
    // Never intersect old output certificates forever.
    time+=.8L;
    for(unsigned i=0;i<17;++i) {
        auto r=marker(time*rate,.05);clock.observe(r,domain(),r.sample+1);if(i==1)start=r;end=r;time+=.4L/1.00015L;
    }
    const auto moved=clock.qualify({-300,300},request(start,end));
    check(moved.qualified_for_acquisition && moved.physical_audio_lower_ppm<=150 && moved.physical_audio_upper_ppm>=150 && moved.physical_audio_lower_ppm>old.physical_audio_upper_ppm,
        "fresh rolling markers were pinned to an obsolete initial clock interval");
    auto reset=end;reset.punctuation.generation=2;reset.punctuation.sample+=8000;reset.punctuation.first_sample+=8000;
    reset.punctuation.last_sample+=8000;reset.sample+=8000;
    check(clock.observe(reset,domain(),reset.sample+1)==h::PunctuationClockReason::accepted && clock.observation_count()==1,
        "provider/capture reset reused old marker evidence");
    check(clock.observe(end,domain(),end.sample+1)==h::PunctuationClockReason::nonincreasing_marker && clock.observation_count()==1,
        "delayed old-generation publication replaced a newer capture's marker history");
}
void actual_helper_publications() {
    h::Options options;options.power_w=15000;options.data_power_w=4;options.timecode=false;
    // Keep the 550/950 Hz Helper tones at their production positions while
    // placing the test DATA band outside their fixed separation guard.
    const auto plan=h::plan(options,1800,100,rate);
    // Isolate and normalize the actual Generator auxiliary for this Receiver
    // republication contract. Composite-pilot isolation is covered separately;
    // no reversal is added to the pure pilot and no paired legacy shape used.
    datapump::test::AuxiliaryFixture source(options,plan.auxiliary_frequency_hz,plan.helper_gain,plan.data_gain,1800000000);
    h::Receiver receiver(plan.auxiliary_frequency_hz,20,{},std::nullopt,{},h::PunctuationFormat::auxiliary_single);
    h::PunctuationClock clock;
    std::vector<float> input(113);std::map<std::pair<std::uint64_t,std::uint64_t>,h::PunctuationObservation> identities;
    std::size_t repeats=0,known=0;h::Reference first,last,last_distinct;
    constexpr long double ratio=1.0001L;
    for(std::uint64_t at=0;at<6ULL*rate;) {
        const auto n=std::min<std::uint64_t>(input.size(),6ULL*rate-at);
        for(std::size_t i=0;i<n;++i)input[i]=source.mix(ratio*(at+i)/rate,rate);
        receiver.push(std::span(input).first(n),rate,at);at+=n;
        if(auto update=receiver.take_update();update && update->punctuation.known) {
            const auto& p=update->punctuation;check(p.last_sample<=update->sample,"actual helper publication manufactured a future marker");
            // The Generator's physical reversal is now centered at 160 ms.
            // Use the known source/capture ratio, not decoded metadata, to
            // check the independently observed marker's dwell phase.
            const auto source_time=(static_cast<long double>(p.sample)+p.fraction)*ratio/rate;
            check(std::abs(std::remainder(source_time-.160L,.4L))<.008L,
                "actual auxiliary marker did not follow the 160ms reversal");
            const auto key=std::pair{p.generation,p.sample};
            if(const auto old=identities.find(key);old!=identities.end()) {
                check(h::punctuation_clock_detail::same(p,old->second),"tone republication changed actual marker support/identity");++repeats;
            } else identities.emplace(key,p);
            const auto reason=clock.observe(*update,domain(),at);
            check(reason==h::PunctuationClockReason::accepted || reason==h::PunctuationClockReason::duplicate_marker,
                "actual helper marker provenance failed clock ingestion");
            if(reason==h::PunctuationClockReason::accepted)last_distinct=*update;
            if(!known++)first=*update;last=*update;
        }
    }
    check(identities.size()>=8 && repeats>identities.size(),"helper did not expose distinct actual markers and carried publications");
    // Qualify at the last independently observed marker publication. Carried
    // tone updates retain their marker's dates; they cannot renew freshness.
    const auto fit=clock.qualify({-300,300},request(first,last_distinct));
    check(fit.qualified_for_acquisition && fit.physical_audio_lower_ppm<=100 && fit.physical_audio_upper_ppm>=100 && fit.physical_audio_upper_ppm-fit.physical_audio_lower_ppm<100,
        "actual Generator/Receiver punctuation did not bound the channel's signed audio clock");
    // The last carried publication can still be within the 50ms freshness
    // limit: the new post-reversal straight flank is only 44ms long. Score
    // freshness at the real, later end of the 6-second capture instead of
    // assuming the final tone publication itself is already stale.
    check(h::punctuation_clock_detail::same(last.punctuation,last_distinct.punctuation) &&
          last.sample<6ULL*rate,"carried publication changed marker or capture provenance");
    auto captured=request(first,last);captured.captured_end_sample=6ULL*rate;
    check(clock.qualify({-300,300},captured).reason==h::PunctuationClockReason::stale_marker,
        "carried tone publication renewed the actual marker's freshness");
    const auto generation=last.punctuation.generation;receiver.reset();
    bool poisoned=false;
    for(std::uint64_t at=0;at<2ULL*rate;) {
        const auto n=std::min<std::uint64_t>(input.size(),2ULL*rate-at);
        for(std::size_t i=0;i<n;++i)input[i]=source.mix(ratio*(at+i)/rate,rate);
        const auto previous=std::fegetround();
        const bool poison=!poisoned && at>=rate/2;
        if(poison)check(std::fesetround(FE_DOWNWARD)==0,"test cannot poison marker-support rounding");
        receiver.push(std::span(input).first(n),rate,at);
        if(poison){check(std::fesetround(previous)==0,"test cannot restore marker-support rounding");poisoned=true;}
        at+=n;
        if(auto r=receiver.take_update())check(!r->punctuation.known,
            "marker evidence accumulated under unsupported arithmetic became known after restoration");
    }
    check(poisoned,"numeric provenance fixture missed its support chunk");
    receiver.reset();
    bool new_generation=false;
    for(std::uint64_t at=0;at<2ULL*rate;) {
        const auto n=std::min<std::uint64_t>(input.size(),2ULL*rate-at);
        for(std::size_t i=0;i<n;++i)input[i]=source.mix(ratio*(at+i)/rate,rate);
        receiver.push(std::span(input).first(n),rate,at);at+=n;
        if(auto r=receiver.take_update();r && r->punctuation.known) {
            check(r->punctuation.generation>generation,"actual provider reset reused marker generation");new_generation=true;
        }
    }
    check(new_generation,"actual reset never reacquired punctuation provenance");
    std::cout<<"actual isolated auxiliary punctuation unique="<<identities.size()<<" carried="<<repeats<<" clock_ppm=["
             <<fit.physical_audio_lower_ppm<<','<<fit.physical_audio_upper_ppm<<"] history_bytes="<<clock.working_bytes()<<'\n';
}
}
int main() {
    try {actual_dates_and_sign();aliases_and_support();endpoint_and_numeric_guards();high_dates_and_fresh_window();actual_helper_publications();
        std::cout<<"punctuation audio-clock component passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
