#include "../src/live_helper_prior.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/receiver_strategy.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <string_view>
#include <vector>
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#include <immintrin.h>
#include <cstring>
#endif

using namespace datapump;
namespace {
void check(bool value,const char* reason){if(!value)throw Error(reason);}
using Pair=modem::PatternFrequencyRateHypothesis;
using Mask=live::detail::HelperPriorMask;

// Independent exact-input geometry check: long-double rectangle endpoints and
// allocated interval lists, rather than the production directed-double helpers.
// This verifies complete slab envelopes, not just retained LF-center presence.
bool covers(std::span<const Pair> pairs,const Mask& mask,double d,double h,double delta,
            double c,double beta,double physical) {
    std::vector<long double> cuts{-physical,physical};
    for(const auto& p:pairs) {
        const auto lo=std::max(-static_cast<long double>(physical),static_cast<long double>(p.clock_error_ppm)-beta);
        const auto hi=std::min(static_cast<long double>(physical),static_cast<long double>(p.clock_error_ppm)+beta);
        if(lo<=hi){cuts.push_back(lo);cuts.push_back(hi);}
    }
    std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
    const auto slab=[&](long double lo,long double hi) {
        const auto f=[&](long double q){return static_cast<long double>(d)+static_cast<long double>(delta)*q/1000000;};
        const auto required_lo=std::min(f(lo),f(hi))-h,required_hi=std::max(f(lo),f(hi))+h;
        std::vector<std::pair<long double,long double>> intervals;
        for(std::size_t i=0;i<pairs.size();++i)if(mask.cells[i]) {
            const auto& p=pairs[i];
            if(static_cast<long double>(p.clock_error_ppm)-beta>lo ||
               static_cast<long double>(p.clock_error_ppm)+beta<hi)continue;
            const auto center=static_cast<long double>(p.frequency_offset_hz)+mask.origin_hz;
            intervals.emplace_back(center-c,center+c);
        }
        std::sort(intervals.begin(),intervals.end());auto through=required_lo;
        for(const auto& [a,b]:intervals) {
            if(b<required_lo)continue;
            if(a>through)return false;
            through=std::max(through,b);if(through>=required_hi)return true;
        }
        return false;
    };
    if(cuts.size()==1)return slab(cuts[0],cuts[0]);
    for(std::size_t i=1;i<cuts.size();++i)if(!slab(cuts[i-1],cuts[i]))return false;
    return true;
}
void all_lf(std::span<const Pair> pairs,const Mask& mask) {
    std::set<double> original,selected;
    for(std::size_t i=0;i<pairs.size();++i) {
        original.insert(pairs[i].clock_error_ppm);
        if(mask.cells[i])selected.insert(pairs[i].clock_error_ppm);
    }
    check(original==selected,"certified helper removed an original LF lane");
}
modem::Config base() {
    modem::Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=200;
    c.spreading_factor=32;c.scramble=true;c.stream_epoch=1800000003;
    c.receiver_strategy=modem::ReceiverStrategy::automatic;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,.5},{1,.05},3e7,3};
    for(std::size_t i=0;i<c.spreading_seed.size();++i)c.spreading_seed[i]=static_cast<std::uint8_t>(7+11*i);
    return c;
}
modem::Config hf() {
    auto c=base();c.sample_rate=6120;c.bandwidth_hz=36;c.spreading_factor=256;
    c.dsss=true;c.dsss_factor=10;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.data_key.emplace(c.spreading_seed);c.dsss_seed=c.spreading_seed;
    return c;
}
double helper_frequency(const modem::Config& c) {
    helper::Options options;options.power_w=1500;
    return helper::plan(options,c.carrier_hz,helper::occupied_bandwidth(c),120000000).frequency_hz;
}
Mask select(const modem::Config& c,const modem::OscillatorPatternSearch& domain,double d,double h) {
    return live::detail::helper_prior_mask(domain.hypotheses,d,h,c.carrier_hz-helper_frequency(c),
        .125*c.sample_rate/static_cast<double>(modem::symbol_sample_count(c)),
        domain.clock_step_ppm/2,domain.requested_clock_half_width_ppm);
}
void hf_geometry() {
    const auto c=hf();const auto domain=modem::oscillator_pattern_search(c);
    check(modem::symbol_sample_count(c)==87040 && domain.hypotheses.size()==8181 && domain.limited,
          "edge fixture changed the user's exact limited HF catalogue");
    const auto original=domain.hypotheses;
    const auto high=std::max_element(original.begin(),original.end(),[](const auto& a,const auto& b) {
        return a.frequency_offset_hz<b.frequency_offset_hz;
    })->frequency_offset_hz;
    const auto cell=.125*c.sample_rate/static_cast<double>(modem::symbol_sample_count(c));
    constexpr double h=.04;
    for(const double d:{0.,30.15,-30.15,high-.01,-high+.01}) {
        const auto mask=select(c,domain,d,h);
        check(mask.admitted && mask.selected>0 && mask.selected<original.size(),
              "valid HF common-link strip lost certified narrowing");
        all_lf(original,mask);
        check(covers(original,mask,d,h,c.carrier_hz-helper_frequency(c),cell,
                     domain.clock_step_ppm/2,domain.requested_clock_half_width_ppm),
              "selected HF rectangles do not cover the entire measured physical strip");
        if(d==0)check(mask.origin_hz==0,"centered HF strip was unnecessarily recentered");
        else check(mask.origin_hz==d,"HF far/edge strip did not recenter the original catalogue");
        check(domain.hypotheses==original,"helper certificate mutated original HF cells");
    }
    // Source-only causal witness for the old center-inside-extrema decision.
    const auto d=high-.01,actual=high+.02;
    check(actual>=d-h && actual<=d+h && actual>high+cell,
          "HF edge witness no longer lies in the CI outside the old RF support");
    std::cout<<"HF edge witness: highest="<<high<<" measured="<<d<<" actual="<<actual<<" halfcell="<<cell<<'\n';
}
void normal_positive_geometry() {
    for(const int profile:{0,1,2}) {
        auto c=base();c.bandwidth_hz=profile==2?1500:1000;
        c.spreading_factor=profile==0?256:32;
        c.oscillator_search->rf_shift_hz=profile==0?1e6:3e7;
        const auto domain=modem::oscillator_pattern_search(c);
        check(!domain.limited && domain.hypotheses.size()==(profile==0?49:profile==1?49:33),
              "normal Auto positive changed its original catalogue");
        const auto d=(profile==0?1.:30.)+helper_frequency(c)*100e-6;
        const auto mask=select(c,domain,d,.5);
        check(mask.admitted && mask.selected<domain.hypotheses.size(),
              "normal Auto full-source profile lost qualified helper narrowing");
        all_lf(domain.hypotheses,mask);
        check(covers(domain.hypotheses,mask,d,.5,c.carrier_hz-helper_frequency(c),
                     .125*c.sample_rate/static_cast<double>(modem::symbol_sample_count(c)),
                     domain.clock_step_ppm/2,domain.requested_clock_half_width_ppm),
              "normal Auto positive retains centers but loses physical strip coverage");
    }
}
void boundary_geometry() {
    using live::detail::helper_prior_mask;
    using live::detail::helper_prior_detail::sum;
    const auto positive=sum(1.,0x1p-53),negative=sum(-1.,-0x1p-53),exact=sum(1.,-1.);
    check(positive.lower==1. && positive.upper==std::nextafter(1.,2.) &&
          negative.lower==std::nextafter(-1.,-2.) && negative.upper==-1. &&
          exact.lower==0 && exact.upper==0,"directed sum lost exact/tie endpoint bounds");
    const auto tiny=std::numeric_limits<double>::denorm_min();
    const auto subnormal=sum(tiny,tiny);
    check(std::bit_cast<std::uint64_t>(subnormal.lower)==2 &&
          std::bit_cast<std::uint64_t>(subnormal.upper)==2,"subnormal directed sums lost exact boundaries");
    const std::array<Pair,3> touching_rf{{{-.5,0},{.5,0},{4,0}}};
    const auto exact_rf=helper_prior_mask(touching_rf,0,.75,0,.5,0,0);
    check(exact_rf.admitted && covers(touching_rf,exact_rf,0,.75,0,.5,0,0),
          "exact RF touching cells lost their complete point-clock strip");
    auto gapped_rf=touching_rf;gapped_rf[0].frequency_offset_hz-=0x1p-30;gapped_rf[1].frequency_offset_hz+=0x1p-30;
    check(!helper_prior_mask(gapped_rf,0,.75,0,.5,0,0).admitted,"RF gap was bridged by a numerical tolerance");
    const std::array<Pair,3> touching_clock{{{0,-1},{0,1},{4,-1}}};
    const auto exact_clock=helper_prior_mask(touching_clock,0,.1,0,1,1,1);
    check(exact_clock.admitted && covers(touching_clock,exact_clock,0,.1,0,1,1,1),
          "exact touching clock endpoints were spuriously separated");
    auto gapped_clock=touching_clock;gapped_clock[0].clock_error_ppm-=0x1p-30;
    gapped_clock[2].clock_error_ppm=gapped_clock[0].clock_error_ppm;gapped_clock[1].clock_error_ppm+=0x1p-30;
    check(!helper_prior_mask(gapped_clock,0,.1,0,1,1,1).admitted,"clock gap was bridged by a numerical tolerance");
    const std::array<Pair,2> point{{{0,0},{4,0}}};
    check(helper_prior_mask(point,0,.1,0,1,0,0).admitted,"known zero LF/beta was mistaken for missing provenance");
    const auto rounding=std::fegetround();
    if(std::fesetround(FE_UPWARD)==0) {
        const auto unsupported=helper_prior_mask(point,0,.1,0,1,0,0);
        std::fesetround(rounding);
        check(!unsupported.admitted,"altered rounding retained a helper coverage certificate");
    }
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP>=2)
    const auto control=_mm_getcsr();
    _mm_setcsr(control|(1U<<15));
    const auto flushed=helper_prior_mask(point,0,.1,0,1,0,0);
    _mm_setcsr(control);
    check(!flushed.admitted,"flush-to-zero retained a helper coverage certificate");
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    // DAZ was optional on early SSE hardware. Check its architectural writable
    // mask before setting bit6; never set an unsupported MXCSR control bit.
    alignas(16) std::array<unsigned char,512> saved{};_fxsave(saved.data());
    std::uint32_t writable=0;std::memcpy(&writable,saved.data()+28,sizeof(writable));
    if(writable&(1U<<6)) {
        _mm_setcsr(control|(1U<<6));
        const auto daz=helper_prior_mask(point,0,.1,0,1,0,0);
        _mm_setcsr(control);
        check(!daz.admitted,"denormals-are-zero retained a helper coverage certificate");
    }
#endif
#endif
    check(!helper_prior_mask(point,0,.1,0,1,0,1).admitted,"positive physical LF domain passed a zero-width timing cover");
    const std::array<Pair,4> cross_lf{{{-4,-1},{0,0},{4,1},{10,0}}};
    const auto union_mask=helper_prior_mask(cross_lf,0,.1,5e5,1.1,1,1);
    check(union_mask.admitted && covers(cross_lf,union_mask,0,.1,5e5,1.1,1,1),
          "valid overlapping clock cells were subjected to the over-sufficient same-LF test");
    all_lf(cross_lf,union_mask);
    for(const auto invalid:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        check(!helper_prior_mask(point,0,.1,0,1,invalid,0).admitted,"invalid represented beta was accepted");
        check(!helper_prior_mask(point,0,.1,0,1,0,invalid).admitted,"invalid physical LF domain was accepted");
    }
    check(!helper_prior_mask(point,0,10,0,1,0,0).admitted,"uncoverable helper uncertainty was truncated");
    const std::array<Pair,2> huge{{{1e308,0},{0,0}}};
    check(!helper_prior_mask(huge,1e308,1,1e308,1,1e308,1e308).admitted,
          "unrepresentable strip arithmetic invented coverage");
}
// Planned optional actual-PCM discriminator. The CI is a prequalified geometry
// input; no claim is made that this unit generates/decodes helper metadata.
void edge_waveform(bool negative) {
    const auto c=hf();const auto domain=modem::oscillator_pattern_search(c);
    const auto high=std::max_element(domain.hypotheses.begin(),domain.hypotheses.end(),[](const auto& a,const auto& b) {
        return a.frequency_offset_hz<b.frequency_offset_hz;
    })->frequency_offset_hz;
    const auto measured=(negative?-1.:1.)*(high-.01),actual=(negative?-1.:1.)*(high+.02);
    const auto mask=select(c,domain,measured,.04);
    check(mask.admitted && mask.origin_hz==measured,"edge waveform did not use a certified recentered prior");
    constexpr std::size_t workspace=256*1024*1024;
    modem::PatternSearch search;search.hypotheses=domain.hypotheses;search.worker_threads=1;
    search.admitted_frequency_half_width_hz=domain.frequency.requested_half_width_hz;
    search.admitted_clock_half_width_ppm=domain.requested_clock_half_width_ppm;
    search.admitted_clock_cell_half_width_ppm=domain.clock_step_ppm/2;
    search.initial_stream_symbols=2;search.chunk_bits=1;
    modem::StreamingReceiver receiver(c,workspace,search);
    check(receiver.set_acquisition_prior(mask.origin_hz,std::span(mask.cells).first(domain.hypotheses.size())),
          "actual native receiver rejected the edge certificate");
    const Bytes expected{0,0,1};modem::StreamingTransmitter source(modem::RawBits{expected},c,workspace);
    modem::ChannelConfig effects;effects.snr_db=35;effects.frequency_offset_hz=actual;
    effects.clock_error_ppm=0;effects.phase_noise_degrees_per_sqrt_second=0;effects.seed=5181;
    modem::SampledSimulationChannel channel(c,effects);
    struct Progress {Bytes bits;bool complete=false;std::uint64_t at=0,end=0;};
    std::map<std::pair<std::uint64_t,std::uint64_t>,Progress> streams;
    std::array<float,137> pcm{};std::uint64_t observed=0;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(180);
    const auto drain=[&] {
        for(const auto& burst:receiver.take_pattern_bursts()) {
            auto& row=streams[{burst.stream_first_sample,burst.stream_first_symbol}];
            row.bits.insert(row.bits.end(),burst.bits.begin(),burst.bits.end());
            if(burst.stream_first_symbol==0)check(row.bits.size()<=expected.size() &&
                std::equal(row.bits.begin(),row.bits.end(),expected.begin()),"edge waveform changed an accepted source prefix");
            if(burst.complete){row.complete=true;row.at=observed;row.end=burst.end_sample;}
        }
    };
    const auto feed=[&](std::span<const float> input) {
        for(std::size_t at=0;at<input.size();) {
            check(std::chrono::steady_clock::now()<deadline,"edge waveform exceeded its bounded diagnostic deadline");
            if(receiver.acquisition_state().ready) {
                receiver.process_prior_acquisition();receiver.resolve_acquisition(true);drain();continue;
            }
            const auto count=receiver.observe_acquisition(input.subspan(at));check(count,"edge waveform observation stalled");
            at+=count;observed+=count;drain();
        }
    };
    for(;;){const auto count=channel.read(source,pcm);if(!count)break;feed(std::span(pcm).first(count));}
    for(std::uint64_t remaining=2*modem::symbol_sample_count(c)+10ULL*c.sample_rate;remaining;) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(remaining,pcm.size()));
        channel.read_noise(std::span(pcm).first(count));feed(std::span(pcm).first(count));remaining-=count;
    }
    if(receiver.acquisition_state().ready){receiver.process_prior_acquisition();receiver.resolve_acquisition(true);drain();}
    bool exact=false;
    for(const auto& [id,row]:streams)if(id.second==0 && row.complete && row.bits==expected) {
        check(row.at>=row.end+modem::pattern_absence_samples(c),"edge waveform manufactured early physical END");exact=true;
    }
    check(exact,"edge waveform lost the full source-zero prefix or scored END");
}
}
int main(int argc,char** argv) {try {
    if(argc==2 && std::string_view(argv[1])=="--unsupported-fp") {
        const std::array<Pair,2> point{{{0,0},{4,0}}};
        check(!live::detail::helper_prior_detail::environment() &&
              !live::detail::helper_prior_mask(point,0,.1,0,1,0,0).admitted,
              "unsupported compile/runtime FP mode did not decline helper certification");
        return 0;
    }
    hf_geometry();normal_positive_geometry();boundary_geometry();
    if(argc==2 && std::string_view(argv[1])=="--hf-edge-waveform") {edge_waveform(false);edge_waveform(true);}
    else check(argc==1,"unknown edge coverage test mode");
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
