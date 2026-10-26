// Actual scalar/cache-off and exact-row-reuse receivers see identical PCM.
// The shared existing fixture supplies the ordinary encrypted deferred-FHSS
// geometry; no expected payload length enters either receiver.
#define main helper_joint_component_main
#include "test_helper_joint_clock.cpp"
#undef main
#include "../src/audio_trace.hpp"
#include "../src/pattern_continuation_row.hpp"
#include "../src/pattern_fft_batch.hpp"
#include <numbers>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

namespace {
bool continuation_trace(const char* name)noexcept {
    const std::string_view event{name};
    return event=="receiver_continuation_fit" || event=="receiver_helper_continuation_rows" || event=="trace_begin";
}
std::string token(const std::string& line,const char* key) {
    const auto mark='"'+std::string(key)+"\":";
    const auto at=line.find(mark);check(at!=std::string::npos,"continuation trace omitted required field");
    const auto first=at+mark.size(),last=line.find_first_of(",}",first);
    return line.substr(first,last-first);
}
std::uint64_t integer(const std::string& line,const char* key) {return std::stoull(token(line,key));}
struct Fit {
    std::array<std::uint64_t,12> coordinates{};
    std::array<std::string,5> values{};
    bool operator==(const Fit&)const=default;
};
struct Progress {
    std::uint64_t frontier=0;Bytes prefix;bool complete=false;
    bool operator==(const Progress&)const=default;
};
struct Row {Bytes bits;bool complete=false;std::uint64_t completion=0;std::vector<Progress> progress;};
struct Run {
    std::map<Identity,Row> rows;
    std::vector<Fit> fits;
    std::uint64_t grouped=0,source_pairs=0,full_fits=0,threshold_trials=0;
    double cpu_seconds=0,wall_seconds=0;
};
void read_trace(const std::filesystem::path& path,Run& result) {
    std::ifstream file(path);check(bool(file),"continuation trace was not saved");
    bool finalized=false;std::string line;
    while(std::getline(file,line)) {
        if(line.find("\"event\":\"receiver_continuation_fit\"")!=std::string::npos) {
            Fit fit;std::size_t i=0;
            for(const auto key:{"epoch","sample_origin","sample","index","phase","source_epoch","source_ordinal",
                    "source_sample","start","observed_before","frequency_index","trials"})fit.coordinates[i++]=integer(line,key);
            i=0;for(const auto key:{"zero","one","threshold","clock_ratio","frequency_hz"})fit.values[i++]=token(line,key);
            check(integer(line,"shifted_trials")==0,"raw continuation acquired an unrelated phase/frequency basis");
            result.fits.push_back(std::move(fit));
        } else if(line.find("\"event\":\"receiver_helper_continuation_rows\"")!=std::string::npos) {
            ++result.grouped;result.source_pairs+=integer(line,"source_pair_evaluations");
            result.full_fits+=integer(line,"original_other_frequency_fits");
            check(integer(line,"all_original_fits_retained")==1 && integer(line,"new_retained_bytes")==0,
                "continuation reuse omitted original fits or added unfunded retained rows");
        } else if(line.find("\"event\":\"trace_end\"")!=std::string::npos) {
            finalized=integer(line,"dropped")==0 && token(line,"prior_io_failed")=="false" && token(line,"size_limited")=="false";
        }
    }
    check(finalized,"continuation trace lost records or failed finalization");
}
void collect_rows(modem::PatternReceiver& receiver,Run& result,std::uint64_t frontier) {
    for(const auto& burst:receiver.take_bursts()) {
        // These receivers may enable helper gap protection, but never execute
        // a conditional acquisition pass. Presence/enabling cannot invent birth.
        check(!burst.helper_conditioned_acquisition && !burst.helper_acquired,
            "unconditioned calendar birth acquired helper provenance or legacy overlap policy");
        auto& row=result.rows[{burst.stream_first_sample,burst.stream_first_symbol}];
        check(!row.complete,"continuation appended data after physical END");
        if(burst.first_stream_symbol-burst.stream_first_symbol!=row.bits.size())continue;
        check(!burst.missing_slots,"continuation introduced a timed payload hole");
        row.bits.insert(row.bits.end(),burst.bits.begin(),burst.bits.end());row.complete=burst.complete;
        if(burst.complete)row.completion=frontier;
    }
    for(auto& [id,row]:result.rows)row.progress.push_back({frontier,row.bits,row.complete});
}
Run receive_rows(const modem::Config& c,modem::PatternSearch s,std::span<const float> pcm,
        bool reuse,bool helper,std::uint64_t loss_at,const std::string& label) {
    Run result;s.reuse_deferred_templates=reuse;
    const auto path=std::filesystem::temp_directory_path()/("datapump-continuation-"+label+"-"+
        std::to_string(audio::trace::identity())+".jsonl");
    const auto cpu=std::clock();const auto wall=std::chrono::steady_clock::now();
    std::shared_ptr<audio::trace::State> trace_state;
    {
        audio::trace::Scope trace(path,false,&continuation_trace);
        trace_state=audio::trace::active.load();
        modem::PatternReceiver receiver(c,64*1024*1024,s);
        receiver.set_helper_gap_protection(helper);
        bool lost=!helper;
        for(std::size_t at=0;at<pcm.size();) {
            const auto count=std::min<std::size_t>(113,pcm.size()-at);
            receiver.push(pcm.subspan(at,count));at+=count;collect_rows(receiver,result,at);
            if(!lost && at>=loss_at) {
                receiver.set_helper_gap_protection(false);
                check(receiver.clear_acquisition_prior(),"helper loss discarded a closed ordinary batch");lost=true;
            }
        }
        check(lost,"continuation fixture did not withdraw helper qualification");
        result.threshold_trials=receiver.fft_work().threshold_trials;
    }
    result.cpu_seconds=static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC;
    result.wall_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count();
    {
        std::lock_guard lock(trace_state->mutex);
        check(trace_state->done && !trace_state->io_failed && !trace_state->dropped && !trace_state->size_limited,
            "continuation trace failed its actual joined final flush/close seal");
    }
    read_trace(path,result);std::filesystem::remove(path);return result;
}
// Directly exercise the production row builder with independently generated
// scalar carrier templates, including skips that need not arise in one PCM run.
void source_row_oracle() {
    using Complex=std::complex<double>;
    constexpr std::size_t bin=40;
    constexpr double tau=2*std::numbers::pi;
    auto c=config();
    const modem::FhssSymbolCalendar calendar(*c.fake_hopping,c.sample_rate,
        modem::pattern_pulse_padding_samples(c),modem::symbol_sample_count(c));
    const auto boundary=calendar.slot(calendar.first_rank(c.stream_epoch,0))%c.sample_rate;
    const auto length=static_cast<std::size_t>(std::ceil(modem::symbol_sample_count(c)/(bin*(1-.0006))))+2;
    std::vector<Complex> scratch(2*length),observed(length);
    for(std::size_t i=0;i<length;++i)observed[i]={std::sin(.37*i)+.1,std::cos(.23*i)-.2};
    std::size_t comparisons=0;
    for(const auto phase:{boundary-1,boundary,boundary+1,static_cast<std::uint64_t>(c.sample_rate-1)})
        for(const auto index:{0ULL,1ULL,4ULL})for(const auto ppm:{-600.,-400.,-200.,0.,200.,400.,600.}) {
            c.stream_phase_samples=phase;
            modem::PatternCode grouped(c,c.stream_epoch),scalar(c,c.stream_epoch);
            const auto first=index*grouped.chips_per_symbol();
            grouped.prepare_symbol(first);
            const auto ratio=1+ppm*1e-6;
            const auto valid=modem::detail::pattern_continuation_source_row(grouped,index,bin,length,ratio,
                true,modem::pattern_pulse_enabled(c),{},scratch);
            check(valid<length && valid>0,"continuation oracle failed to include padded variable-clock bins");
            for(const auto offset:{-2.8125,0.,2.8125}) {
                scalar.prepare_symbol(first);
                std::vector<std::array<Complex,2>> reference(length);
                for(std::size_t i=0;i<length;++i) {
                    const auto at=static_cast<long double>(i)*bin+static_cast<long double>(bin-1)/2;
                    const auto source=at*ratio;
                    if(source>=scalar.symbol_samples())continue;
                    const auto chip_position=source/scalar.chip_samples();
                    const auto local=static_cast<std::uint64_t>(chip_position);
                    const auto values=modem::pattern_pulse_enabled(c)?scalar.shaped_values(first,static_cast<double>(source)):
                        scalar.values(first+local,static_cast<double>(chip_position-local));
                    const auto carrier=std::polar(1.,tau*offset*static_cast<double>(at)/c.sample_rate);
                    reference[i]={values[0]*carrier,values[1]*carrier};
                    check(i<valid && reference[i][0]==scratch[2*i]*carrier && reference[i][1]==scratch[2*i+1]*carrier,
                        "continuation source row differs from exact scalar canonical/clock/carrier template");
                }
                for(const auto skip:{std::size_t{0},length/3,length}) {
                    std::array<Complex,2> scalar_dot{},grouped_dot{};
                    std::array<double,2> scalar_norm{},grouped_norm{};double energy=0;
                    for(std::size_t i=skip;i<length;++i) {
                        energy+=std::norm(observed[i]);
                        std::array<Complex,2> reused{};
                        if(i<valid) {
                            const auto at=static_cast<long double>(i)*bin+static_cast<long double>(bin-1)/2;
                            const auto carrier=std::polar(1.,tau*offset*static_cast<double>(at)/c.sample_rate);
                            reused={scratch[2*i]*carrier,scratch[2*i+1]*carrier};
                        }
                        for(unsigned bit=0;bit<2;++bit) {
                            scalar_dot[bit]+=observed[i]*std::conj(reference[i][bit]);scalar_norm[bit]+=std::norm(reference[i][bit]);
                            grouped_dot[bit]+=observed[i]*std::conj(reused[bit]);grouped_norm[bit]+=std::norm(reused[bit]);
                        }
                    }
                    check(scalar_dot==grouped_dot && scalar_norm==grouped_norm,
                        "continuation changed scalar dot/norm accumulation at a skip boundary");
                    for(unsigned bit=0;bit<2;++bit)check(
                        modem::detail::pattern_evidence(scalar_dot[bit],energy,scalar_norm[bit],length-skip,1.,false,false)==
                        modem::detail::pattern_evidence(grouped_dot[bit],energy,grouped_norm[bit],length-skip,1.,false,false),
                        "continuation changed either scalar bit score at a skip boundary");
                    ++comparisons;
                }
            }
        }
    std::stop_source cancellation;cancellation.request_stop();modem::PatternCode cancelled(c,c.stream_epoch);
    bool stopped=false;
    try{modem::detail::pattern_continuation_source_row(cancelled,0,bin,length,1.,true,true,{},scratch,cancellation.get_token());}
    catch(const Error&){stopped=true;}
    check(stopped,"continuation row ignored cancellation before scratch mutation");
    std::cout<<"continuation exact scalar row/score comparisons="<<comparisons<<" skip=0/interior/length\n";
}
void full_receiver_case(std::uint64_t source_phase,std::uint64_t epoch,std::size_t bit_count,bool helper,bool search_phases) {
    auto c=config();c.stream_epoch=epoch;c.stream_phase_samples=source_phase;
    Bytes bits(bit_count);for(std::size_t i=0;i<bits.size();++i)bits[i]=static_cast<std::uint8_t>((i*73+(i>>1)+19)%7<3);
    const auto delay=4ULL*c.sample_rate+137;
    constexpr long double ratio=1.0001L;
    modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(source.total_samples());
    check(source.read_analytic(analytic)==analytic.size(),"ordinary source did not emit its complete advertised waveform");
    const auto end=delay+static_cast<std::uint64_t>(std::ceil(analytic.size()/ratio));
    std::vector<float> pcm(end+modem::pattern_absence_capture_samples(c)+
        4*modem::symbol_sample_count(c)+2*modem::pattern_pulse_padding_samples(c));
    std::mt19937 random(1729);std::normal_distribution<float> noise(0,.02F);const auto phase=std::polar(2.,.83);
    for(std::size_t i=0;i<pcm.size();++i) {
        std::complex<double> value{};
        if(i>=delay && i<end) {
            const auto position=(i-delay)*ratio;const auto k=static_cast<std::size_t>(position);
            if(k<analytic.size()) {
                const auto next=k+1<analytic.size()?analytic[k+1]:std::complex<double>{};
                value=analytic[k]+static_cast<double>(position-k)*(next-analytic[k]);
            }
        }
        pcm[i]=noise(random)+static_cast<float>((phase*value).real());
    }
    auto s=search(c,delay+modem::pattern_pulse_padding_samples(c));s.search_stream_phases=search_phases;
    const auto label=std::to_string(epoch)+"-"+std::to_string(source_phase)+"-"+std::to_string(bit_count);
    const auto off=receive_rows(c,s,pcm,false,helper,end-modem::symbol_sample_count(c)/2,label+"-scalar");
    const auto on=receive_rows(c,s,pcm,true,helper,end-modem::symbol_sample_count(c)/2,label+"-grouped");
    check(!off.fits.empty() && off.fits==on.fits,"continuation changed an original carrier's exact zero/one score, address, trial or threshold");
    check(off.threshold_trials==on.threshold_trials,"continuation reuse changed original logical trial penalties");
    const auto exact=[&](const auto& rows) {
        return std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.second.complete && row.second.bits==bits;});
    };
    const auto a=exact(off.rows),b=exact(on.rows);
    check(a!=off.rows.end() && b!=on.rows.end(),"continuation truncated the complete ordinary binary payload or physical END");
    check(a->first==b->first && a->second.completion==b->second.completion,"continuation changed the pending identity or ordinary completion frontier");
    check(a->second.progress==b->second.progress,"continuation changed an immediate post-push pending prefix or completion poll");
    const auto progress=[&](const Row& row) {
        bool full_pending=false;std::size_t previous=0;std::uint64_t final_bit=0;
        for(const auto& p:row.progress) {
            check(p.prefix.size()>=previous && p.prefix.size()<=bits.size() &&
                  std::equal(p.prefix.begin(),p.prefix.end(),bits.begin()),"continuation retracted or changed an exact pending prefix");
            // One push can accept an acquisition batch. The immediately
            // following take_bursts poll must expose its entire exact prefix;
            // it need not restrict that batch to a single newly accepted bit.
            if(p.prefix.size()>previous)final_bit=p.frontier;
            full_pending|=!p.complete && p.prefix.size()==bits.size();previous=p.prefix.size();
        }
        check(full_pending && row.completion>final_bit,"continuation withheld final-bit progress until physical END");
    };
    progress(a->second);progress(b->second);
    check(b->second.completion+16>=end+6ULL*c.sample_rate,"helper loss or EOF manufactured physical completion");
    const modem::FhssSymbolCalendar calendar(*c.fake_hopping,c.sample_rate,
        modem::pattern_pulse_padding_samples(c),modem::symbol_sample_count(c));
    constexpr long double lower=1-.00060009L,upper=1+.00060009L;
    const auto absent=static_cast<std::uint64_t>(std::ceil(6.L*c.sample_rate*lower/modem::symbol_sample_count(c)));
    const auto base=delay+modem::pattern_pulse_padding_samples(c);
    const auto scored_absence=static_cast<long double>(base)+
        (calendar.offset(c.stream_epoch,source_phase,bits.size()+absent-1)+modem::symbol_sample_count(c))/upper;
    check(static_cast<long double>(b->second.completion)+16>=scored_absence,"continuation counted scheduled FHSS gaps as scored absence");
    check(!off.grouped && (helper?on.grouped>0:on.grouped==0),"continuation exact-row path or helper-OFF guard was not exercised");
    if(helper)check(on.source_pairs && on.full_fits>on.grouped,"continuation reused no actual private source-row generation");
    std::cout<<"continuation exact phase="<<source_phase<<" epoch="<<epoch<<" bits="<<bits.size()
        <<" helper="<<helper<<" fits="<<on.fits.size()<<" grouped="<<on.grouped<<" source_pairs="<<on.source_pairs
        <<" full_other_fits="<<on.full_fits<<" complete="<<b->second.completion<<" process_CPU_seconds="<<off.cpu_seconds
        <<"->"<<on.cpu_seconds<<" wall_seconds="<<off.wall_seconds<<"->"<<on.wall_seconds<<'\n';
}
}
int main(int argc,char** argv) {try {
    if(argc==2 && std::string_view(argv[1])=="--long")full_receiver_case(0,1800000000,64,true,true);
    else {
        check(argc==1,"unknown continuation-row test argument");
        source_row_oracle();
        const auto c=config();const modem::FhssSymbolCalendar calendar(*c.fake_hopping,c.sample_rate,
            modem::pattern_pulse_padding_samples(c),modem::symbol_sample_count(c));
        const auto boundary=calendar.slot(calendar.first_rank(c.stream_epoch,0))%c.sample_rate;
        check(boundary && boundary+1<c.sample_rate,"deferred calendar fixture lacks an interior rank boundary");
        for(const auto phase:{boundary-1,boundary,boundary+1})full_receiver_case(phase,c.stream_epoch,8,true,false);
        full_receiver_case(c.sample_rate-1,c.stream_epoch+1,8,true,false);
        full_receiver_case(0,c.stream_epoch,4,false,false);
    }
    std::cout<<"deferred continuation original-fit and physical-completion comparisons passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
