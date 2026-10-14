#include "scheduled_certificate_diagnostics.hpp"
#include "scheduled_acquisition_certificate.hpp"
#include "scheduled_acquisition_tile.hpp"
#include "scheduled_template_proxy_certificate.hpp"
#include <cfenv>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <optional>
#include <vector>
#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

namespace d=datapump::modem::detail;
namespace profile=d::scheduled_certificate_diagnostics;
namespace cert=d::scheduled_acquisition_certificate;
namespace tile=d::scheduled_acquisition_tile;
#define CHECK(value) do {if(!(value))throw std::runtime_error("check failed: " #value);} while(false)
std::uint64_t bits(double value){return std::bit_cast<std::uint64_t>(value);}
void histogram_bits() {
    const auto rounding=std::fegetround();const auto flags=std::fetestexcept(FE_ALL_EXCEPT);
#if defined(__SSE2__)
    const auto mxcsr=_mm_getcsr();
#endif
    const auto value=[](std::uint64_t raw){return std::bit_cast<double>(raw);};
    CHECK(profile::magnitude_bin(value(0))==1);CHECK(profile::magnitude_bin(value(1ULL<<63))==1);
    CHECK(profile::magnitude_bin(value(1))==4);CHECK(profile::magnitude_bin(value(0x0010000000000000ULL))==4);
    CHECK(profile::magnitude_bin(value(0x8000000000000001ULL))==0);
    CHECK(profile::magnitude_bin(value(0x7ff0000000000000ULL))==2);CHECK(profile::magnitude_bin(value(0xfff0000000000000ULL))==2);
    for(auto raw:{0x7ff0000000000001ULL,0x7ff8000000000000ULL,0xfff0000000000001ULL})CHECK(profile::magnitude_bin(value(raw))==3);
    for(int exponent=-64;exponent<=64;exponent+=8){
        const auto boundary=static_cast<std::uint64_t>(exponent+1023)<<52;
        const auto bin=exponent==64?21:5+static_cast<std::size_t>((exponent+64)/8);
        CHECK(profile::magnitude_bin(value(boundary))==bin);CHECK(profile::magnitude_bin(value(boundary-1))==bin-1);
    }
    CHECK(profile::score_bin(0)==1);CHECK(profile::score_bin(1)==3);CHECK(profile::score_bin(512)==9);
    CHECK(profile::score_bin(value(0x7ff0000000000000ULL))==10);CHECK(profile::score_bin(value(0x7ff0000000000001ULL))==11);
    CHECK(std::fegetround()==rounding);CHECK(std::fetestexcept(FE_ALL_EXCEPT)==flags);
#if defined(__SSE2__)
    CHECK(_mm_getcsr()==mxcsr);
#endif
    profile::Histogram histogram;histogram.counts[1]=UINT64_MAX-1;histogram.observe(0);histogram.observe(0);
    CHECK(histogram.counts[1]==UINT64_MAX && histogram.saturated);
    profile::Histogram merged;merged.merge(histogram);CHECK(merged.saturated && merged.counts[1]==UINT64_MAX);
    std::ostringstream out;out<<profile::view(histogram);CHECK(out.str().size()<=profile::magnitude_bins*21+2);
}
void bound_results() {
    d::CorrelationFit fit;fit.count=64;fit.cc=fit.ss=32;fit.energy=128;fit.xc=2;fit.xs=3;
    cert::FitCertificate certificate;certificate.valid=true;certificate.dot_radius={0,0};certificate.gram_radius={0,0};certificate.energy_lower=certificate.energy_upper=128;
    cert::ReferenceErrors original{0,0,0,0,true};
    const auto compare=[&](const auto& f,const auto& c,const auto& reference) {
        std::feclearexcept(FE_ALL_EXCEPT);const auto plain=cert::score_upper(f,c,0,reference);const auto flags=std::fetestexcept(FE_ALL_EXCEPT);
#if defined(__SSE2__)
        const auto mxcsr=_mm_getcsr();
#endif
        std::feclearexcept(FE_ALL_EXCEPT);profile::ScoreDetail detail;const auto observed=cert::score_upper(f,c,0,reference,&detail);
        CHECK(bits(plain)==bits(observed));CHECK(std::fetestexcept(FE_ALL_EXCEPT)==flags);
#if defined(__SSE2__)
        CHECK(_mm_getcsr()==mxcsr);
#endif
        return detail;
    };
    compare(fit,certificate,original);
    auto invalid=certificate;invalid.valid=false;CHECK(compare(fit,invalid,original).failure==profile::Failure::invalid_contract);
    auto empty=fit;empty.count=2;if(cert::environment<double>())CHECK(compare(empty,certificate,original).failure==profile::Failure::none);
    auto errors=original;errors.dot=cert::infinity;const auto nonfinite=compare(fit,certificate,errors);if(cert::environment<double>())CHECK(nonfinite.failure==profile::Failure::nonfinite_errors);
    auto singular=fit;singular.cs=32;const auto gram=compare(singular,certificate,original);if(cert::environment<double>())CHECK(gram.failure==profile::Failure::nonpositive_gram);
    auto depleted=certificate;depleted.energy_lower=0;const auto energy=compare(fit,depleted,original);if(cert::environment<double>())CHECK(energy.failure==profile::Failure::energy_floor);
    auto explained=fit;explained.xc=128;const auto fraction=compare(explained,certificate,original);if(cert::environment<double>())CHECK(fraction.failure==profile::Failure::fraction_not_below_one);
    auto overflow=original;overflow.log_error=std::numeric_limits<double>::max();const auto upper=compare(fit,certificate,overflow);if(cert::environment<double>())CHECK(upper.failure==profile::Failure::score_overflow);
    profile::ProxyDetail detail;
    const auto plain=d::scheduled_template_proxy_certificate::errors(fit,certificate,0,.01,2,8,.5,1,4);
    const auto observed=d::scheduled_template_proxy_certificate::errors(fit,certificate,0,.01,2,8,.5,1,4,&detail);
    CHECK(bits(plain.dot)==bits(observed.dot));CHECK(bits(plain.gram)==bits(observed.gram));CHECK(plain.valid==observed.valid);if(cert::environment<double>())CHECK(detail.available);
}
void pairing_gate() {
    constexpr const char* name="DATAPUMP_SCHED_CERTIFICATE_DIAGNOSTICS";
    const auto* old=std::getenv(name);const std::optional<std::string> saved=old?std::optional<std::string>(old):std::nullopt;
    const auto set=[&](const char* value) {
#if defined(_WIN32) && !defined(__linux__)
        CHECK(_putenv_s(name,value?value:"")==0);
#else
        CHECK(value?setenv(name,value,1)==0:unsetenv(name)==0);
#endif
    };
    struct Restore {decltype(set)& apply;const std::optional<std::string>& value;~Restore(){apply(value?value->c_str():nullptr);}} restore{set,saved};
    profile::Probe probe;probe.cohort=profile::Cohort::fp32_proxy;probe.upper=64;probe.threshold=32;probe.raw=16;probe.guarded=8;
    set(nullptr);profile::State disabled;probe.original_scored=true;disabled.completed(probe);CHECK(!disabled.enabled && disabled.pairs[1].completed==0);
    set("1");profile::State enabled;probe.original_scored=false;enabled.completed(probe);CHECK(enabled.enabled && enabled.pairs[1].completed==0);
    // The current event owns a single scalar copy while replay is cancelled.
    // No completed histogram changes until the original call returns.
    std::optional<profile::Probe> pending=probe;profile::Probe resumed=*pending;resumed.original_scored=true;
    enabled.completed(resumed);pending.reset();const auto& pair=enabled.pairs[1];CHECK(pair.completed==1 && !pending);
    CHECK(pair.raw_below_threshold==1 && pair.guarded_below_threshold==1 && pair.raw_exceeds_upper==0);
    CHECK(pair.upper_raw.counts[profile::score_bin(64)*profile::score_bins+profile::score_bin(16)]==1);
    CHECK(pair.upper_guarded.counts[profile::score_bin(64)*profile::score_bins+profile::score_bin(8)]==1);
}
template<class T> void native_optional() {
    constexpr std::size_t budget=4*1024*1024;const tile::Shape shape{4,6};tile::Plan<T> plan(shape);
    const std::array<std::size_t,2> starts{0,2};std::array<float,9> pcm{.1f,.2f,.3f,-.1f,.4f,-.3f,.2f,.1f,-.2f};
    std::array<std::complex<double>,9> oscillator;oscillator.fill({1,0});std::array<std::uint8_t,9> observed;observed.fill(1);
    std::array<std::array<std::complex<double>,2>,6> rows{};std::array<std::uint8_t,6> mask;mask.fill(1);
    for(std::size_t i=0;i<rows.size();++i){rows[i][0]=i%2?std::complex<double>{0,1}:std::complex<double>{1,0};rows[i][1]=i%3?std::complex<double>{.5,.5}:std::complex<double>{-.5,.5};}
    tile::PreparedInput<T> input(plan,tile::Input{pcm,oscillator,observed,0},budget);
    tile::PreparedFragment<T> fragment(plan,tile::Fragment{rows,mask,0},budget);
    tile::Accumulator<T> plain(plan,0,starts,budget),instrumented(plan,0,starts,budget,{},true);
    plain.add(input,fragment);instrumented.add(input,fragment);const auto a=plain.finish(),b=instrumented.finish();CHECK(a.size()==b.size());
    for(std::size_t i=0;i<a.size();++i){CHECK(a[i].physical_origin==b[i].physical_origin);CHECK(a[i].certificate.valid==b[i].certificate.valid);
        CHECK(bits(a[i].certificate.energy_lower)==bits(b[i].certificate.energy_lower));CHECK(bits(a[i].certificate.energy_upper)==bits(b[i].certificate.energy_upper));
        for(unsigned bit=0;bit<2;++bit){const auto& x=a[i].fits[bit];const auto& y=b[i].fits[bit];CHECK(x.count==y.count);
            CHECK(bits(x.xc)==bits(y.xc));CHECK(bits(x.xs)==bits(y.xs));CHECK(bits(x.cc)==bits(y.cc));CHECK(bits(x.ss)==bits(y.ss));CHECK(bits(x.cs)==bits(y.cs));CHECK(bits(x.energy)==bits(y.energy));
            CHECK(a[i].requires_refinement[bit]==b[i].requires_refinement[bit]);CHECK(bits(a[i].certificate.dot_radius[bit])==bits(b[i].certificate.dot_radius[bit]));CHECK(bits(a[i].certificate.gram_radius[bit])==bits(b[i].certificate.gram_radius[bit]));}}
    CHECK(plain.certificate_profile().candidates==0);CHECK(instrumented.certificate_profile().candidates==2*starts.size());
    CHECK(plain.work().inverse_transforms==instrumented.work().inverse_transforms);CHECK(plain.working_bytes()==instrumented.working_bytes());
}
int main(){try{histogram_bits();bound_results();pairing_gate();native_optional<float>();native_optional<double>();std::cout<<"scheduled certificate diagnostics fixtures passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
