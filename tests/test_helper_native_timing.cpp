#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "../src/pattern_fft_periodic_window.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <ctime>
#include <iostream>
#include <map>
#include <random>
#include <string_view>

using namespace datapump;
namespace {
modem::OuterDsssVersion outer_version=modem::OuterDsssVersion::legacy_v1;
void check(bool value,const char* text){if(!value)throw Error(text);}
bool includes(const modem::detail::PatternFftStartSelection& selected,std::size_t j) {
    return selected.full || std::any_of(selected.ranges.begin(),selected.ranges.begin()+selected.range_count,
        [&](const auto& r){return j>=r.first && j-r.first<r.count;});
}
void cells() {
    using namespace modem::detail;
    PatternFftStartSelection full{{},0,400,true};
    PatternFftPeriodicTiming timing{{0,0,40,0},1,1};
    const auto many=pattern_fft_periodic_select_starts(full,timing,0,0,400,1);
    check(!many.declined && !many.starts.full && many.starts.range_count>=10,"all periodic aliases were not retained");
    for(std::size_t j=0;j<400;j+=40)check(includes(many.starts,j),"periodic alias was selected by nearest-only timing");
    check(!includes(many.starts,20),"periodic selector retained unsupported midpoint");
    auto empty=pattern_fft_periodic_select_starts({{},0,1,true},{{0,0,40,0},1,1},20,0,1,1);
    check(!empty.declined && !empty.starts.selected_count,"empty periodic intersection was not a whole-job skip");
    const auto fractional=pattern_fft_periodic_select_starts({{},0,64,true},{{1.5L,0,80,.5L},1,1},0,0,64,4);
    check(includes(fractional.starts,3) && !includes(fractional.starts,4),"fractional cell/refinement endpoint rounded inward or to a new grid");
    PatternFftStartSelection utc;utc.full=false;utc.range_count=2;utc.selected_count=25;
    utc.ranges[0]={35,10};utc.ranges[1]={115,15};
    const auto intersection=pattern_fft_periodic_select_starts(utc,timing,0,0,400,1);
    for(std::size_t j=0;j<400;++j)check(!includes(intersection.starts,j) || includes(utc,j),"periodic selector widened original UTC selection");
    const auto capacity=pattern_fft_periodic_select_starts(full,{{0,0,1,0},1,1},0,0,400,1);
    check(capacity.declined && capacity.starts.full,"alias capacity overflow silently removed alternatives");
    const auto invalid=pattern_fft_periodic_select_starts(utc,{{0,0,40,0},0,1},0,0,400,1);
    check(invalid.declined && invalid.starts.selected_count==utc.selected_count,"invalid clock domain lost original selection");
    // Both signs of elapsed time and every clock in the original LF-cell hull.
    PatternFftPeriodicTiming clocks{{10000.25L,-14400,5760,.75L},.9997L,1.0003L};
    const auto unioned=pattern_fft_periodic_select_starts({{},0,7000,true},clocks,181,0,7000,4);
    check(!unioned.declined,"finite independent-clock alias union declined");
    for(const auto r:{.9997L,.9998L,.9999L,1.L,1.0001L,1.0002L,1.0003L})for(int k=-4;k<=3;++k) {
        const auto x=clocks.prior.observed_sample+(181-clocks.prior.source_phase_samples+k*5760)/r;
        const auto center=std::llround(x/4);
        for(int refine=-2;refine<=2;++refine)if(center+refine>=0 && center+refine<7000)
            check(includes(unioned.starts,static_cast<std::size_t>(center+refine)),"LF cell endpoint or negative alias lost original refinement");
    }
    constexpr std::uint64_t high=1000000000000ULL;
    const auto dated=pattern_fft_periodic_select_starts({{},0,400,true},{{4.L*high+1.25L,0,400,.25L},1,1},0,high,400,4);
    check(!dated.declined && includes(dated.starts,0) && includes(dated.starts,100),"large physical dates destroyed fractional aliases");
}
modem::Config config() {
    modem::Config c;c.sample_rate=14400;c.carrier_hz=1500;c.bandwidth_hz=360;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=true;c.dsss=true;c.dsss_factor=10;
    c.outer_dsss_version=outer_version;
    c.stream_epoch=1800000000;c.spreading_seed[3]=51;c.dsss_seed[13]=171;c.data_key=Crypto(c.spreading_seed);
    auto hops=std::make_shared<modem::FhssPlan>();hops->channels=3;hops->dwell_seconds=.4;
    hops->shifts[0]=1000000;hops->shifts[1]=1010000;hops->shifts[2]=1033000;
    hops->clock_error_seconds=hops->peer_clock_error_seconds=.001;hops->audio_error_seconds=.002;
    c.fake_hopping=modem::resolve_fhss_power(*hops,c);
    check(c.fake_hopping->defer_symbols && modem::symbol_sample_count(c)==5120 && modem::pattern_chip_samples(c)==8,
        "native timing fixture lost B waveform geometry");
    return c;
}
modem::PatternSearch search(const modem::Config& c,std::size_t base) {
    modem::PatternSearch s;s.start_offset_seconds=static_cast<double>(base)/c.sample_rate;s.start_uncertainty_seconds=7;
    s.search_stream_phases=true;s.initial_stream_symbols=4;s.worker_threads=1;s.prefer_streamed_templates=true;
    s.candidate_limit=128;s.track_limit=8;s.bit_limit=256;s.chunk_bits=1;s.drift_tolerant=false;s.retain_score=0;
    s.early_qualified_fft=false;s.component_qualified_search=false;
    s.admitted_clock_half_width_ppm=350;s.admitted_clock_cell_half_width_ppm=50;
    const auto step=.25*c.sample_rate/modem::symbol_sample_count(c);
    for(const auto f:{0.,-step,step})for(const auto ppm:{-300.,-200.,-100.,0.,100.,200.,300.})s.hypotheses.push_back({f,ppm});
    return s;
}
std::vector<std::uint8_t> mask(const modem::PatternSearch& s) {
    std::vector<std::uint8_t> result;for(const auto& h:s.hypotheses)result.push_back(h.frequency_offset_hz==0);return result;
}
struct Poll {std::uint64_t frontier=0;Bytes prefix;bool complete=false;};
struct Seen {Bytes bits;bool complete=false;std::size_t polls=0;std::uint64_t completion_frontier=0;std::vector<Poll> progress;};
using Identity=std::pair<std::uint64_t,std::uint64_t>;
void collect(modem::PatternReceiver& receiver,std::map<Identity,Seen>& rows,std::uint64_t frontier) {
    for(const auto& b:receiver.take_bursts()) {
        auto& row=rows[{b.stream_first_sample,b.stream_first_symbol}];
        check(!row.complete,"native timing changed a physically completed row");
        if(b.first_stream_symbol-b.stream_first_symbol!=row.bits.size())continue;
        check(!b.missing_slots,"native timing lost a payload bit");
        row.bits.insert(row.bits.end(),b.bits.begin(),b.bits.end());row.complete=b.complete;++row.polls;
        if(b.complete)row.completion_frontier=frontier;
    }
    for(auto& [identity,row]:rows)row.progress.push_back({frontier,row.bits,row.complete});
}
struct Work {std::uint64_t active=0,declined=0,direct=0,partitioned=0,points=0;double cpu_seconds=0,wall_seconds=0;};
void feed(modem::PatternReceiver& receiver,std::span<const float> input,bool timed,std::size_t grid,
          bool audit,Work& work) {
    struct Timing {Work& work;std::clock_t cpu=std::clock();std::chrono::steady_clock::time_point wall=std::chrono::steady_clock::now();
        ~Timing(){work.cpu_seconds+=static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC;
            work.wall_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count();}} measured{work};
    while(!input.empty() || receiver.acquisition_state().ready) {
        const auto state=receiver.acquisition_state();check(state.supported,"native timing split seam unavailable");
        if(state.ready) {
            if(state.prior_active) {
                if(timed) {
                    // This component fixture supplies a qualified modulo grid,
                    // never an expected message length or private TX epoch.
                    const auto p=.4L*14400;
                    const auto anchor=static_cast<long double>(grid)+std::floor((state.observed_samples-grid)/p)*p;
                    modem::PatternAcquisitionTimingResult result;
                    receiver.process_prior_acquisition({anchor,0,p,.75L},state.observed_samples,result);
                    check(result.completed,"timed native batch was not joined");
                    work.active+=result.narrowed;work.declined+=result.status==modem::PatternAcquisitionTimingStatus::bounded_scoring_declined;
                    work.direct+=result.direct_jobs;work.partitioned+=result.partitioned_jobs;work.points+=result.partitioned_transform_points;
                } else receiver.process_prior_acquisition();
                receiver.resolve_acquisition(!audit);
            } else receiver.process_acquisition();
        } else {
            const auto used=receiver.observe_acquisition(input);check(used!=0,"native timing observation stalled");input=input.subspan(used);
        }
    }
}
void trial_parity() {
    const auto c=config();const auto s=search(c,0);const auto selected=mask(s);
    modem::PatternReceiver frequency(c,64*1024*1024,s),timing(c,64*1024*1024,s),future(c,64*1024*1024,s);
    for(auto* receiver:{&frequency,&timing,&future}) {
        check(receiver->set_acquisition_prior(0,selected),"trial-parity frequency mask declined");
        while(!receiver->acquisition_state().ready) {
            const auto state=receiver->acquisition_state();
            const auto n=std::min<std::size_t>(113,state.input_room_samples);
            check(receiver->observe_acquisition(std::vector<float>(n))==n,"trial-parity observation stalled");
        }
    }
    frequency.process_prior_acquisition();
    modem::PatternAcquisitionTimingResult measured,rejected;
    const auto observed=timing.acquisition_state().observed_samples;
    timing.process_prior_acquisition({0,0,.4L*c.sample_rate,.75L},observed,measured);
    future.process_prior_acquisition({0,0,.4L*c.sample_rate,.75L},observed+1,rejected);
    check(measured.completed && measured.narrowed && (measured.direct_jobs || measured.partitioned_jobs),
        "qualified timing did not reduce actual no-admission acquisition work");
    check(rejected.status==modem::PatternAcquisitionTimingStatus::invalid_reference && !rejected.narrowed,
        "native timing accepted a future raw publication");
    for(auto* receiver:{&frequency,&timing,&future})receiver->resolve_acquisition(true);
    check(frequency.fft_work().threshold_trials==timing.fft_work().threshold_trials &&
          frequency.fft_work().threshold_trials==future.fft_work().threshold_trials,
        "periodic filtering changed original logical trial penalties");
    check(frequency.cold_acquisition_only() && timing.cold_acquisition_only() && future.cold_acquisition_only(),
        "periodic omitted cells manufactured payload or physical absence");
    check(frequency.fft_work().template_jobs==future.fft_work().template_jobs,
        "invalid optional timing changed ordinary frequency acquisition work");
}
void payload(bool audit) {
    const auto c=config();const Bytes bits{0,0,1,0,1,1,0,0,1,0,1,1};constexpr std::size_t delay=137;
    const auto base=delay+modem::pattern_pulse_padding_samples(c);const auto s=search(c,base);
    modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(source.total_samples());source.read_analytic(analytic);
    std::vector<float> pcm(delay+analytic.size()+modem::pattern_absence_capture_samples(c)+2*modem::symbol_sample_count(c)+2*modem::pattern_pulse_padding_samples(c));
    std::mt19937 random(529);std::normal_distribution<float> noise(0,.025F);const auto phase=std::polar(1.7,.83);
    for(std::size_t i=0;i<pcm.size();++i)pcm[i]=noise(random)+(i>=delay && i-delay<analytic.size()?static_cast<float>((phase*analytic[i-delay]).real()):0.F);
    modem::PatternReceiver frequency(c,64*1024*1024,s),timing(c,64*1024*1024,s);
    check(frequency.set_acquisition_prior(0,mask(s)) && timing.set_acquisition_prior(0,mask(s)),"native timing original LF mask declined");
    std::map<Identity,Seen> rows_f,rows_t;Work wf,wt;bool loss=false;
    const auto loss_at=delay+analytic.size()-modem::symbol_sample_count(c)/2;
    for(std::size_t at=0;at<pcm.size();) {
        const auto count=std::min<std::size_t>(113,pcm.size()-at);const auto input=std::span(pcm).subspan(at,count);
        feed(frequency,input,false,base,audit,wf);feed(timing,input,true,base,audit,wt);at+=count;
        collect(frequency,rows_f,at);collect(timing,rows_t,at);
        if(!audit && !loss && at>=loss_at) {
            check(frequency.clear_acquisition_prior() && timing.clear_acquisition_prior(),"native timing loss did not restore broad acquisition");loss=true;
        }
    }
    const auto exact=[&](const auto& rows){return std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.second.complete && row.second.bits==bits;});};
    const auto a=exact(rows_f),b=exact(rows_t);
    check(a!=rows_f.end() && b!=rows_t.end(),"native timing changed complete final bits/physical END");
    check(a->first==b->first && b->second.polls>=bits.size(),"native timing changed pending identity/per-bit progress");
    const auto verify_progress=[&](const Seen& row) {
        std::size_t previous=0;bool completed=false,full_pending=false;
        std::vector<std::uint64_t> accepted(bits.size());
        for(const auto& poll:row.progress) {
            check(poll.prefix.size()>=previous && poll.prefix.size()<=bits.size() &&
                  std::equal(poll.prefix.begin(),poll.prefix.end(),bits.begin()),
                "native timing changed or retracted an exact pending prefix");
            check(!completed || (poll.complete && poll.prefix.size()==previous),"native timing changed data after END");
            for(std::size_t k=previous;k<poll.prefix.size();++k)accepted[k]=poll.frontier;
            full_pending|=!poll.complete && poll.prefix.size()==bits.size();
            previous=poll.prefix.size();completed=poll.complete;
        }
        check(full_pending && row.completion_frontier>accepted.back(),"native timing withheld final-bit progress until END");
        return accepted;
    };
    const auto accepted_f=verify_progress(a->second),accepted_t=verify_progress(b->second);
    const auto bins=std::max(frequency.acquisition_state().bin_samples,timing.acquisition_state().bin_samples);
    // Both paths retain original +/-2-bin refinements and are drained every
    // <=113 input samples. Earlier timing-assisted acquisition is permitted.
    const auto poll_allowance=4*bins+113;
    for(std::size_t k=0;k<bits.size();++k)check(accepted_t[k]<=accepted_f[k]+poll_allowance,
        "timing assistance delayed matched accepted-bit progress beyond ordinary refinements/polling");
    const auto completion_difference=a->second.completion_frontier>b->second.completion_frontier?
        a->second.completion_frontier-b->second.completion_frontier:b->second.completion_frontier-a->second.completion_frontier;
    check(completion_difference<=poll_allowance,"native timing changed matched physical completion beyond original refinements/polling");
    const modem::FhssSymbolCalendar calendar(*c.fake_hopping,c.sample_rate,modem::pattern_pulse_padding_samples(c),modem::symbol_sample_count(c));
    constexpr long double lower_rate=1-.00035L,upper_rate=1+.00035L;
    const auto absent=static_cast<std::uint64_t>(std::ceil(6.L*c.sample_rate*lower_rate/modem::symbol_sample_count(c)));
    const auto final_boundary=base+calendar.offset(c.stream_epoch,0,bits.size()-1)+modem::symbol_sample_count(c);
    const auto scored_absence_boundary=static_cast<long double>(base)+
        (calendar.offset(c.stream_epoch,0,bits.size()+absent-1)+modem::symbol_sample_count(c))/upper_rate;
    for(const auto* row:{&a->second,&b->second}) {
        check(row->completion_frontier+4*bins>=final_boundary+6ULL*c.sample_rate,
            "native timing treated helper loss/source EOF as physical END");
        check(static_cast<long double>(row->completion_frontier)+4*bins>=scored_absence_boundary,
            "native timing counted planned FHSS gaps instead of complete absent-symbol durations");
    }
    check(!timing.fft_work().restricted_timing_active,"native timing altered UTC/early/component scheduling policy");
    check(wt.active && (wt.direct || wt.partitioned),"native timing only filtered completed FFT candidates");
    if(audit)check(timing.fft_work().original_audit_template_jobs>0,"timing prior survived raw same-window fallback");
    else check(loss,"native timing did not lose helper near final symbol");
    std::cout<<"native timing audit="<<audit<<" exact="<<bits.size()<<" narrowed="<<wt.active<<" direct="<<wt.direct
             <<" partitioned="<<wt.partitioned<<" points="<<wt.points<<" bounded_declined="<<wt.declined
             <<" completion="<<a->second.completion_frontier<<"->"<<b->second.completion_frontier
             <<" FFT_jobs="<<frequency.fft_work().fft_jobs<<"->"<<timing.fft_work().fft_jobs
             <<" direct_jobs="<<frequency.fft_work().direct_jobs<<"->"<<timing.fft_work().direct_jobs
             <<" partitioned_jobs="<<frequency.fft_work().partitioned_jobs<<"->"<<timing.fft_work().partitioned_jobs
             <<" CPU_seconds="<<wf.cpu_seconds<<"->"<<wt.cpu_seconds
             <<" wall_seconds="<<wf.wall_seconds<<"->"<<wt.wall_seconds<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string_view(argv[1])=="--interleaved-v2")outer_version=modem::OuterDsssVersion::interleaved_v2;
        else check(argc==1,"unknown native timing test argument");
        cells();trial_parity();payload(false);payload(true);std::cout<<"helper native periodic timing passed\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
