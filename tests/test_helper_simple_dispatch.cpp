#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <complex>
#include <iostream>
#include <map>
#include <random>
using namespace datapump;
namespace {
void check(bool v,const char* s){if(!v)throw Error(s);}
modem::Config config(double seconds=1) {
    modem::Config c;c.sample_rate=256;c.carrier_hz=64;c.bandwidth_hz=8;c.spreading_factor=8;
    c.integration_seconds=seconds;c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;c.dsss=true;
    c.stream_epoch=1800000000;c.spreading_seed[3]=51;c.dsss_seed[13]=171;c.data_key=Crypto(c.spreading_seed);return c;
}
modem::PatternSearch search(const modem::Config& c,double origin) {
    modem::PatternSearch s;s.start_offset_seconds=origin;s.start_uncertainty_seconds=.125;
    s.search_stream_phases=false;s.worker_threads=1;s.bit_limit=128;s.chunk_bits=1;s.track_limit=8;
    s.candidate_limit=128;s.drift_tolerant=false;s.retain_score=0;
    s.admitted_clock_half_width_ppm=100;s.admitted_clock_cell_half_width_ppm=50;
    const auto step=.25*c.sample_rate/modem::symbol_sample_count(c);
    for(const auto f:{0.,-step,step})for(const auto ppm:{0.,-100.,100.})s.hypotheses.push_back({f,ppm});return s;
}
struct Row {Bytes bits;bool complete=false,full_pending=false;std::uint64_t completion=0;};
using Rows=std::map<std::pair<std::uint64_t,std::uint64_t>,Row>;
void poll(modem::PatternCorrelator& r,Rows& rows,const Bytes& expected,std::uint64_t at) {
    for(const auto& b:r.take_bursts()) {
        auto& row=rows[{b.stream_first_sample,b.stream_first_symbol}];check(!row.complete,"appended after physical END");
        if(b.first_stream_symbol-b.stream_first_symbol!=row.bits.size())continue;
        check(!b.missing_slots,"dispatch pause introduced payload gaps");row.bits.insert(row.bits.end(),b.bits.begin(),b.bits.end());
        row.complete=b.complete;row.full_pending|=!b.complete && row.bits==expected;if(b.complete)row.completion=at;
    }
}
void plain_128() {
    const auto c=config(128);auto s=search(c,0);s.compact_clock_search=true;
    modem::PatternCorrelator receiver(c,s,4*1024*1024);const auto before=receiver.working_bytes();
    check(receiver.helper_acquisition_dispatch().supported && !receiver.compact_acquisition_state().prior_active,
        "plain 128s receiver requires optional control storage for pause");
    check(receiver.set_helper_acquisition_dispatch(false) && receiver.working_bytes()==before,"pause allocated optional mask state");
    const auto count=3*modem::symbol_sample_count(c)/2;std::vector<float> zeros(count,0);
    for(std::size_t at=0;at<zeros.size();) {const auto n=std::min<std::size_t>(113,zeros.size()-at);receiver.push(std::span(zeros).subspan(at,n));at+=n;}
    const auto work=receiver.work();
    check(work.samples==zeros.size() && !work.projected_carrier_samples && !work.accumulated_lane_samples &&
        receiver.take_bursts().empty(),"paused plain compact receiver scored cold input or fabricated payload/END");
    check(receiver.set_helper_acquisition_dispatch(true) && receiver.helper_acquisition_dispatch().permitted,
        "plain compact receiver could not resume without an optional prior");
}
void payload() {
    const auto c=config();const Bytes bits{0,0,1,0,1,1,0,1};const auto S=modem::symbol_sample_count(c);
    const auto delay=2*S+37,origin=delay+modem::pattern_pulse_padding_samples(c);
    modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);std::vector<std::complex<double>> analytic(source.total_samples());
    check(source.read_analytic(analytic)==analytic.size(),"source did not drain complete waveform");const auto end=delay+analytic.size();
    std::vector<float> pcm(end+std::max<std::uint64_t>(3*S,8*c.sample_rate));std::mt19937 random(529);std::normal_distribution<float> noise(0,.025F);
    const auto phase=std::polar(1.7,.83);
    for(std::size_t i=0;i<pcm.size();++i)pcm[i]=noise(random)+(i>=delay && i-delay<analytic.size()?static_cast<float>((phase*analytic[i-delay]).real()):0.F);
    const auto s=search(c,static_cast<double>(origin)/c.sample_rate);
    modem::PatternCorrelator off(c,s,4*1024*1024),on(c,s,4*1024*1024);Rows a,b;
    check(on.set_helper_acquisition_dispatch(false),"plain ordinary compact pause unsupported");bool resumed=false,lost=false;
    for(std::size_t at=0;at<pcm.size();) {
        const auto n=std::min<std::size_t>(17,pcm.size()-at);const auto block=std::span(pcm).subspan(at,n);
        off.push(block);on.push(block);at+=n;poll(off,a,bits,at);poll(on,b,bits,at);
        if(!resumed && at>=S){check(on.set_helper_acquisition_dispatch(true),"plain compact resume failed");resumed=true;}
        if(!lost && at>=end-S/2){check(on.synchronized() && on.set_helper_acquisition_dispatch(false),"could not pause cold work during established final symbol");lost=true;}
    }
    const auto exact=[&](const auto& rows){return std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.second.complete && row.second.bits==bits;});};
    const auto x=exact(a),y=exact(b);check(resumed && lost && x!=a.end() && y!=b.end(),"pause/resume/loss truncated ordinary payload or END");
    check(x->first==y->first && x->second.full_pending && y->second.full_pending,"pause changed pending identity or withheld final bits");
    check(y->second.completion+2>=end+6*c.sample_rate,"dispatch pause implied physical END without six seconds scored absence");
}
}
int main(){try{plain_128();payload();std::cout<<"plain compact dispatch and exact payload/END passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
