#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "helper_reference_search.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <string>
using namespace datapump;
namespace {
using Grid=modem::PatternUtcClockGrid;
using Bank=modem::OscillatorPatternSearch;
void check(bool value,const char* message){if(!value)throw Error(message);}
void same(const Bank& a,const Bank& b) {
    check(a.hypotheses==b.hypotheses && a.frequency.count==b.frequency.count &&
        a.frequency.step_hz==b.frequency.step_hz && a.frequency.half_width_hz==b.frequency.half_width_hz &&
        a.frequency.requested_half_width_hz==b.frequency.requested_half_width_hz && a.frequency.limited==b.frequency.limited &&
        a.requested_clock_half_width_ppm==b.requested_clock_half_width_ppm && a.clock_half_width_ppm==b.clock_half_width_ppm &&
        a.clock_step_ppm==b.clock_step_ppm && a.limited==b.limited &&
        a.transmit_rate_correction==b.transmit_rate_correction && a.timing_correction_unavailable==b.timing_correction_unavailable,
        "default ordinary UTC catalogue changed");
}
modem::Config config() {
    modem::Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=1;c.spreading_factor=64;
    c.integration_seconds=128;c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;c.dsss=false;
    c.stream_epoch=1800000000;c.spreading_seed[3]=51;c.dsss_seed[13]=171;c.data_key=Crypto(c.spreading_seed);
    modem::OscillatorSearchConfig policy;policy.lf.accuracy_ppm=policy.rf.accuracy_ppm=.0001;
    policy.margin=3;policy.rf_shift_hz=5.8e9;policy.reference=modem::OscillatorReference::independent_audio;
    c.oscillator_search=policy;return c;
}
void covered(const Bank& bank,long double frequency,long double ppm) {
    const auto tolerance=64*std::numeric_limits<double>::epsilon()*std::max(1.,bank.frequency.requested_half_width_hz);
    check(std::any_of(bank.hypotheses.begin(),bank.hypotheses.end(),[&](auto p) {
        return std::abs(frequency-p.frequency_offset_hz)<=bank.frequency.step_hz/2+tolerance &&
            std::abs(ppm-p.clock_error_ppm)<=bank.clock_step_ppm/2+tolerance;
    }),"expanded-only grid lost an original/UTC physical cell");
}
Bank reference(const modem::Config& c,double uncertainty,double helper,double correction,Grid grid) {
    return modem::helper_reference_search(c,uncertainty,helper,0,correction,
        modem::HelperReferenceClockRelation::common_corrected_source,{},grid).catalogue;
}
void geometry() {
    auto c=config();constexpr double U=.031563893057749072,H=2450.3125;
    const auto correction=modem::utc_transmit_rate_limit(c);
    auto legacy=reference(c,U,H,correction,Grid::preserve_original_lanes);
    const auto expanded=reference(c,U,H,correction,Grid::complete_expanded_domain);
    check(legacy.hypotheses.size()==69 && expanded.hypotheses.size()==35,
        "actual EME old/new RF-grid union geometry changed");
    check(expanded.requested_clock_half_width_ppm==legacy.requested_clock_half_width_ppm &&
        expanded.clock_half_width_ppm==legacy.clock_half_width_ppm && !expanded.limited &&
        expanded.transmit_rate_correction==correction && !expanded.timing_correction_unavailable,
        "helper grid coalescing removed physical clock or UTC uncertainty");
    // Include true composed corners, interior a/c/r and every original
    // numerical representative. Clock bounds are not an inferred clock lock.
    for(const auto lf:{.0001,100.})for(const auto k:{-950.3125,0.,950.3125}) {
        auto model=c;model.oscillator_search->lf.accuracy_ppm=lf;
        if(lf==100){model.oscillator_search->rf_shift_hz=3e7;model.oscillator_search->rf.accuracy_ppm=1;}
        const auto C=modem::utc_transmit_rate_limit(model);
        const auto old=reference(model,U,model.carrier_hz-k,C,Grid::preserve_original_lanes);
        const auto current=reference(model,U,model.carrier_hz-k,C,Grid::complete_expanded_domain);
        check(!old.limited && !current.limited && current.hypotheses.size()<=old.hypotheses.size(),
            "GPSDO/HF complete grid control lost finite coverage");
        const auto A=static_cast<long double>(model.oscillator_search->margin)*lf*1e-6L;
        for(int i=0;i<=8;++i)for(int j=0;j<=8;++j)for(int r=0;r<=8;++r) {
            const auto a=-A+2*A*i/8,steering=-static_cast<long double>(C)+2*static_cast<long double>(C)*j/8;
            const auto b=(a+steering+a*steering)*1e6L;
            covered(current,k*b*1e-6L-U+2*U*r/8,b);
        }
        for(const auto p:old.hypotheses)covered(current,p.frequency_offset_hz,p.clock_error_ppm);
        const auto maximum_step=.25L*modem::pattern_chip_samples(model)/modem::symbol_sample_count(model)*1e6L;
        check(current.clock_step_ppm<=maximum_step,"coalesced grid exceeded ordinary quarter-chip resolution");
        modem::PatternSearchResolution resolution;resolution.clock_frequency_hz=k;
        same(modem::oscillator_pattern_search(model,C,resolution),
            modem::oscillator_pattern_search(model,C,resolution,Grid::preserve_original_lanes));
        same(modem::oscillator_pattern_search(model,0,resolution),
            modem::oscillator_pattern_search(model,0,resolution,Grid::complete_expanded_domain));
    }
    // An unrepresentable expanded domain keeps the existing fallback/status.
    auto limited=c;limited.oscillator_search->rf.accuracy_ppm=100;
    const auto C=modem::utc_transmit_rate_limit(limited);
    same(modem::oscillator_pattern_search(limited,C,{}),
        modem::oscillator_pattern_search(limited,C,{},Grid::complete_expanded_domain));
    bool rejected=false;try{(void)modem::oscillator_pattern_search(c,correction,{},static_cast<Grid>(99));}
    catch(const Error&){rejected=true;}check(rejected,"unknown UTC catalogue policy accepted");
    std::cout<<"actual EME69->35, HF/GPSDO/zero-correction/default/fallback full-domain coverage passed\n";
}
struct Seen {Bytes bits;bool complete=false,full_pending=false;std::uint64_t completion=0;};
using Identity=std::pair<std::uint64_t,std::uint64_t>;using Rows=std::map<Identity,Seen>;
void poll(modem::PatternCorrelator& receiver,Rows& rows,const Bytes& expected,std::uint64_t frontier) {
    for(const auto& b:receiver.take_bursts()) {
        auto& row=rows[{b.stream_first_sample,b.stream_first_symbol}];
        check(!row.complete,"coalesced catalogue appended after END");
        if(b.first_stream_symbol-b.stream_first_symbol!=row.bits.size())continue;
        check(!b.missing_slots,"coalesced catalogue inserted a missing payload slot");
        row.bits.insert(row.bits.end(),b.bits.begin(),b.bits.end());row.complete=b.complete;
        check(row.bits.size()<=expected.size() && std::equal(row.bits.begin(),row.bits.end(),expected.begin()),
            "coalesced catalogue changed an ordinary accepted prefix");
        row.full_pending|=!b.complete && row.bits==expected;if(b.complete)row.completion=frontier;
    }
}
void payload() {
    auto c=config();c.sample_rate=256;c.carrier_hz=64;c.bandwidth_hz=4;
    c.oscillator_search->rf_shift_hz=1e6;c.oscillator_search->rf.accuracy_ppm=10;
    const auto S=modem::symbol_sample_count(c),delay=2*S+37,origin=delay+modem::pattern_pulse_padding_samples(c);
    const auto correction=modem::utc_transmit_rate_limit(c),U=2.3*.25*c.sample_rate/static_cast<double>(S);
    const auto make=[&](Grid grid) {
        modem::PatternSearch s;s.compact_clock_search=true;s.start_offset_seconds=static_cast<double>(origin)/c.sample_rate;
        s.start_uncertainty_seconds=0;s.search_stream_phases=false;s.worker_threads=1;s.bit_limit=128;s.chunk_bits=1;
        s.track_limit=8;s.candidate_limit=128;s.drift_tolerant=false;s.retain_score=0;
        auto r=modem::helper_reference_search(c,U,90,0,correction,
            modem::HelperReferenceClockRelation::common_corrected_source,{},grid);
        modem::apply_helper_reference_search(std::move(r),s);return s;
    };
    const auto before=make(Grid::preserve_original_lanes),after=make(Grid::complete_expanded_domain);
    check(after.hypotheses.size()<before.hypotheses.size(),"payload discriminator omitted no original duplicate grid work");
    constexpr std::size_t grant=16*1024*1024;
    modem::PatternCorrelator off(c,before,grant),on(c,after,grant);
    const Bytes bits{0,0,1,0};modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(source.total_samples());
    check(source.read_analytic(analytic)==analytic.size(),"payload source did not drain");
    const auto end=delay+analytic.size();std::vector<float> pcm(end+3*S);
    std::mt19937 rng(529);std::normal_distribution<float> noise(0,.025F);const auto phase=std::polar(1.7,.83);
    for(std::size_t i=0;i<pcm.size();++i)pcm[i]=noise(rng)+(i>=delay&&i-delay<analytic.size()?static_cast<float>((phase*analytic[i-delay]).real()):0.F);
    Rows a,b;
    for(std::size_t at=0;at<pcm.size();) {
        const auto n=std::min<std::size_t>(113,pcm.size()-at);const auto chunk=std::span(pcm).subspan(at,n);
        off.push(chunk);on.push(chunk);at+=n;poll(off,a,bits,at);poll(on,b,bits,at);
    }
    const auto exact=[&](const Rows& rows){return std::find_if(rows.begin(),rows.end(),[&](const auto& p){return p.second.complete&&p.second.bits==bits;});};
    const auto x=exact(a),y=exact(b);
    check(x!=a.end() && y!=b.end() && x->first==y->first && x->second.full_pending && y->second.full_pending,
        "ordinary128s grid comparison lost exact full pending payload, identity or END");
    check(x->second.completion+2>=end+6*c.sample_rate && y->second.completion+2>=end+6*c.sample_rate,
        "clock catalogue coalescing implied physical completion");
    const auto w0=off.compact_acquisition_state(),w1=on.compact_acquisition_state();
    check(off.work().samples==pcm.size() && on.work().samples==pcm.size() &&
        w1.projected_carrier_samples>0 && w1.projected_carrier_samples<w0.projected_carrier_samples,
        "coalesced grid did not reduce actual ordinary carrier projection work");
    check(off.working_bytes()<=grant && on.working_bytes()<=grant,"grid comparison exceeded identical grants");
    std::cout<<"ordinary128s4bits exactprefix/fullpending/identity/scoredEND and projected "<<w0.projected_carrier_samples<<"->"<<w1.projected_carrier_samples<<" passed\n";
}
}
int main(int argc,char** argv){try{
    const std::string mode=argc>1?argv[1]:"all";
    if(mode=="geometry"||mode=="all")geometry();if(mode=="payload"||mode=="all")payload();
    check(mode=="geometry"||mode=="payload"||mode=="all","unknown helper grid mode");return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
