#include "helper_complete_stream_metrics.hpp"
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace m = helper_complete_stream_metrics;
struct Trace {
    std::uint64_t first = 0;
    std::vector<std::complex<float>> noisy, clean;
    std::vector<long double> input_dates;
    std::vector<unsigned char> supported, physically_disturbed;
    bool support(std::uint64_t n) const {return n >= first && n - first < supported.size() && supported[static_cast<std::size_t>(n-first)];}
};
void require(bool c, const char* msg){if(!c)throw std::runtime_error(msg);}
int main(){try{
    m::Options options; options.sample_rate=1000; options.dwell_seconds=1; options.edge_seconds=.1;
    options.probe_hz=40; options.mix_reference_hz=100; options.fixed_gauge_seconds=.002;
    options.capture_begin_seconds=0; options.capture_end_seconds=2; options.frequency_lag=5;
    options.minimum_frequency_pairs_per_dwell=10;options.other_fixed_gauge_seconds=options.fixed_gauge_seconds;
    Trace trace, other; const double fixed=m::tau*(options.mix_reference_hz-options.probe_hz)*options.fixed_gauge_seconds;
    for(std::size_t i=0;i<2000;++i){
        const auto z=std::polar(1.,m::tau*(40+.5)*static_cast<double>(i)/1000+fixed);
        trace.clean.emplace_back(static_cast<float>(z.real()),static_cast<float>(z.imag()));
        trace.noisy.push_back(trace.clean.back());trace.input_dates.push_back(i);
        trace.supported.push_back(static_cast<unsigned char>(i>=100&&i<1900));trace.physically_disturbed.push_back(0);
        other.supported.push_back(static_cast<unsigned char>(i>=200&&i<1800));other.input_dates.push_back(i);
    }
    trace.physically_disturbed[250]=1;trace.physically_disturbed[251]=2;
    trace.physically_disturbed[252]=3;trace.clean[252]={0,0};
    auto result=m::evaluate(trace,options,&other);
    const auto ri=[](m::Region r){return static_cast<std::size_t>(r);};
    const auto si=[](m::Selection s){return static_cast<std::size_t>(s);};
    const auto& start=result.region[ri(m::Region::beginning)][si(m::Selection::all)];
    const auto& end=result.region[ri(m::Region::end)][si(m::Selection::all)];
    const auto& interior=result.region[ri(m::Region::interior)][si(m::Selection::all)];
    const auto& boundary=result.region[ri(m::Region::boundary)][si(m::Selection::all)];
    require(start.passed==200&&end.passed==200&&interior.passed==1600&&boundary.passed==400,"incorrect region totals");
    require(interior.physically_disturbed==1&&interior.physically_unknown==1&&interior.physically_missing==1,"physical classes conflated");
    require(interior.phase_samples==1598,"missing or unknown signal used in phase error");
    require(interior.frequency_dwells==2&&std::abs(interior.frequency_rms_hz-.5)<1e-5,"frequency correlation error");
    require(interior.phase_worst_deg>150&&interior.phase_p99_deg>145,"per-sample phase was aligned away");
    const auto& common=result.region[ri(m::Region::interior)][si(m::Selection::common)];
    require(common.passed==1400,"common-support selection wrong");
    require(result.common_map_mismatch==0,"equal maps disagreed");
    other.input_dates[300] += .01L;
    const auto mismatch=m::evaluate(trace,options,&other);
    require(mismatch.common_map_mismatch==1 &&
        mismatch.region[ri(m::Region::interior)][si(m::Selection::common)].passed==1399,
        "different source dates included in common support");
    options.other_fixed_gauge_seconds+=.001;
    const auto gauge_mismatch=m::evaluate(trace,options,&other);
    require(gauge_mismatch.common_gauge_mismatch>0 &&
        gauge_mismatch.region[ri(m::Region::interior)][si(m::Selection::common)].passed==0,
        "different gauges included in common support");
    require(result.region[ri(m::Region::beginning)][si(m::Selection::uncertain)].passed==100,"uncertain samples disappeared");
    trace.input_dates[0]=std::numeric_limits<long double>::quiet_NaN();
    result=m::evaluate(trace,options);
    require(result.invalid_capture_coordinates==1,"invalid capture coordinate hidden");
    trace.clean.pop_back();bool rejected=false;
    try{(void)m::evaluate(trace,options);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"truncated trace accepted");
    std::cout<<"helper_complete_stream_metrics passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"helper_complete_stream_metrics: "<<e.what()<<'\n';return 1;}}
