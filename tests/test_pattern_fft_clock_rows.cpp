// Actual-class numerical control; this TU owns the receiver implementation,
// so the archive supplies its dependencies rather than a second receiver.o.
#include "../src/pattern_receiver.cpp"
#include "datapump/tuning.hpp"
#include <bit>
#include <iostream>
#include <random>

namespace datapump::modem {
struct PatternReceiverStartupGuideTestAccess {
    using State=PatternReceiver::Impl;
    using Rows=detail::PatternFftClockRows;
    static void check(bool value,const char* why){if(!value)throw Error(why);}
    static bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
    static bool bits(Complex a,Complex b){return bits(a.real(),b.real()) && bits(a.imag(),b.imag());}
    static Config config(unsigned key=0x57) {
        auto c=tuning::resolve(36,8,tuning::PatternMode::auto_keystream,true,1500,10,
            OuterDsssVersion::interleaved_v2).config;
        c.scramble=c.dsss=c.pulse_shaping=true;c.stream_epoch=1800000000;
        c.receiver_strategy=ReceiverStrategy::complex_only;c.search_arithmetic=SearchArithmetic::fp64;
        c.data_key.emplace(Bytes(32,static_cast<std::uint8_t>(key)));
        // PatternCode consumes the already-derived pattern/DSSS roots.
        // Changing only Data would not change this component's private rows.
        c.spreading_seed[3]=static_cast<std::uint8_t>(key^0x64U);
        c.dsss_seed[13]=static_cast<std::uint8_t>(key^0xfcU);return c;
    }
    static PatternSearch search() {
        PatternSearch s;s.search_stream_phases=true;s.initial_stream_symbols=4;s.worker_threads=2;
        s.prefer_streamed_templates=true;s.release_idle_fft_scratch=true;
        s.candidate_limit=16;s.track_limit=4;s.bit_limit=128;s.chunk_bits=1;
        s.drift_tolerant=false;s.differential_window_seconds=0;
        s.early_qualified_fft=false;s.component_qualified_search=false;s.retain_score=1e9;
        // Same nine LF lanes and 301-pair size as the supplied HF owner. This
        // is a numerical workload, not received Helper authority or a UTC prior.
        for(int clock=-4;clock<=4;++clock) {
            const auto count=clock==0?18:16;
            for(int rf=-count;rf<=count;++rf)
                s.hypotheses.push_back({rf*.017494647476655523-962.5*(clock*75.)*1e-6,clock*75.});
        }
        check(s.hypotheses.size()==301,"clock-row control lost original pair count");return s;
    }
    static std::size_t serial_grant(State& s,bool cache) {
        return s.fixed_reservation+std::max<std::size_t>(32768,(2*s.search.track_limit+2)*s.configured_bit_limit)+
            (cache?Rows::required_bytes(s.length):0);
    }
    static void prepare_input(State& s) {
        s.ensure_execution_scratch();
        std::mt19937_64 random(9234);std::normal_distribution<double> normal;
        s.energy_prefix[0]=0;
        for(std::size_t i=0;i<s.transform;++i) {
            s.work[i]={normal(random),normal(random)};
            s.energy_prefix[i+1]=s.energy_prefix[i]+std::norm(s.work[i]);
        }
        s.spectrum=s.work;detail::pattern_fft(s.spectrum,false,{});
    }
    static std::vector<detail::FftSearchScore> score(State& s,std::uint64_t symbol,std::size_t f,
            std::span<const Rows::Pair> rows) {
        std::vector<detail::FftSearchScore> result(s.hop);
        std::vector<Complex> product(s.transform);
        for(unsigned bit=0;bit<2;++bit) {
            std::fill(product.begin(),product.end(),Complex{});double norm=0;
            for(std::size_t i=0;i<s.length;++i) {
                const auto value=s.template_value(i,symbol,bit,f,rows);
                product[s.length-1-i]=std::conj(value);norm+=std::norm(value);
            }
            detail::pattern_fft(product,false,{});
            for(std::size_t i=0;i<s.transform;++i)product[i]=s.spectrum[i]*product[i];
            detail::pattern_fft(product,true,{});
            for(std::size_t j=0;j<s.hop;++j) {
                const auto value=detail::pattern_evidence(product[s.length-1+j],
                    s.energy_prefix[j+s.length]-s.energy_prefix[j],norm,
                    s.evidence_count(s.length),s.noise_condition,s.real_rank,false,{});
                if(bit)result[j].one=value;else result[j].zero=value;
            }
        }
        return result;
    }
    static void seven_cohorts() {
        PatternReceiver receiver(config(),32*1024*1024,search());auto& s=*receiver.impl_;
        check(!s.fallback && s.streamed_templates && s.length==5122 && s.transform==16384,
            "clock-row control lost actual HF projection geometry");
        receiver.set_workspace_bytes(serial_grant(s,true));prepare_input(s);
        s.prepare_scoring({});check(s.scoring.empty(),"cache grant displaced an affordable parallel worker");
        auto rows=s.prepare_serial_clock_rows(1);
        check(rows && s.arithmetic_reservation==rows->working_bytes() && receiver.working_bytes()<=s.budget,
            "exact row capacities/control were not fully charged");
        std::size_t cohorts=0,comparisons=0;
        for(std::uint64_t symbol=0;symbol<s.search.initial_stream_symbols;++symbol) {
            std::size_t count=0;const auto phases=s.phase_groups(symbol,0,s.phase_upper,count);
            for(std::size_t group=0;group<count;++group) {
                ++cohorts;s.stream_phase(phases[group].lower);s.prepare_templates(symbol,{});
                for(std::size_t f=0;f<s.templates.size();++f) {
                    const auto view=s.serial_clock_rows(rows.get(),symbol,f,{});
                    const auto original=score(s,symbol,f,{}),actual=score(s,symbol,f,view);
                    check(actual.size()==original.size(),"cache changed score extent");
                    for(std::size_t i=0;i<actual.size();++i) {
                        check(bits(actual[i].zero,original[i].zero) && bits(actual[i].one,original[i].one),
                            "exact row changed a score bit pattern (including signed zero)");comparisons+=2;
                    }
                }
            }
        }
        check(cohorts==7 && s.fft_workload->exact_clock_row_builds==63 &&
            s.fft_workload->exact_clock_row_hits==7*(301-9),"cache crossed a cohort/clock identity or failed to reuse RF siblings");
        std::cout<<"clock rows: cohorts="<<cohorts<<" clocks=9 pairs=301 score_bit_comparisons="<<comparisons
            <<" builds="<<s.fft_workload->exact_clock_row_builds<<" hits="<<s.fft_workload->exact_clock_row_hits
            <<" charged_bytes="<<rows->working_bytes()<<'\n';
        rows.reset();s.arithmetic_reservation=0;
    }
    static void identity_cancellation_and_grants() {
        PatternReceiver a(config(),32*1024*1024,search()),b(config(0x93),32*1024*1024,search());
        auto& s=*a.impl_;auto& other=*b.impl_;
        a.set_workspace_bytes(serial_grant(s,true));s.ensure_execution_scratch();s.prepare_scoring({});
        check(!s.prepare_serial_clock_rows(2),"multiworker decision admitted serial-only cache");
        auto rows=s.prepare_serial_clock_rows(1);check(bool(rows),"funded serial cache declined");
        auto first=s.serial_clock_rows(rows.get(),0,0,{});const auto saved=first[17];
        const auto builds=s.fft_workload->exact_clock_row_builds;
        (void)s.serial_clock_rows(rows.get(),0,1,{});
        check(s.fft_workload->exact_clock_row_builds==builds,"RF-only change rebuilt unrotated source");
        ++s.search.input_sample_origin;(void)s.serial_clock_rows(rows.get(),0,1,{});
        check(s.fft_workload->exact_clock_row_builds==builds+1,"sample-origin change reused old row");
        s.stream_phase(s.phase_step);s.prepare_templates(0,{});(void)s.serial_clock_rows(rows.get(),0,1,{});
        check(s.fft_workload->exact_clock_row_builds==builds+2,"exact phase change reused old row");
        const auto changed_key=other.serial_clock_rows(rows.get(),0,0,{});
        check(other.fft_workload->exact_clock_row_builds==1 &&
            (!bits(saved[0],changed_key[17][0]) || !bits(saved[1],changed_key[17][1])),
            "changed key/source owner borrowed another private row");
        Rows::Identity identity{&s,4,1,1800000056,0,92,17,std::bit_cast<std::uint64_t>(1.),0};
        std::stop_source stop;bool cancelled=false;
        try {rows->get(identity,[&](std::size_t i,unsigned bit) {
            if(i==7)stop.request_stop();return Complex{double(i),-double(bit)};
        },stop.get_token());}catch(const Error&){cancelled=true;}
        check(cancelled && !rows->matches(identity),"cancelled fill published a partial private pair");
        const auto retry=rows->get(identity,[](std::size_t i,unsigned bit){return Complex{double(i),-double(bit)};});
        check(rows->matches(identity) && bits(retry[7][1],Complex{7,-1}),"cancelled cache could not retry original pair");
        std::stop_source early;early.request_stop();cancelled=false;bool invoked=false;
        try {rows->get(identity,[&](std::size_t,unsigned){invoked=true;return Complex{};},early.get_token());}
        catch(const Error&){cancelled=true;}check(cancelled && !invoked,"pre-cancelled cache hit invoked source or ignored cancellation");
        rows.reset();s.arithmetic_reservation=0;
        a.set_workspace_bytes(serial_grant(s,false));
        check(!s.prepare_serial_clock_rows(1),"low grant admitted unreserved cache backing");
        check(!Rows::create(s.length,Rows::required_bytes(s.length)-1),"one-byte-short cache grant was ignored");
        std::cout<<"clock rows: exact owner/key/phase/origin identities, low grants and cancellation/retry passed\n";
    }
    static void executed_paths() {
        std::vector<PatternFftWork> work;
        std::vector<std::uint64_t> trials;
        std::vector<std::vector<PatternEvidence>> evidence;
        std::vector<std::vector<PatternBurst>> bursts;
        const auto c=config();
        PatternTransmitter transmitter(Bytes{0},c,c.stream_epoch,0,false);
        std::vector<Complex> analytic(static_cast<std::size_t>(transmitter.total_samples()));
        transmitter.read_analytic(analytic);
        const auto padding=pattern_pulse_padding_samples(c);
        auto actual_search=search();actual_search.retain_score=5;
        for(const bool cached:{false,true}) {
            PatternReceiver receiver(c,32*1024*1024,actual_search);auto& s=*receiver.impl_;
            receiver.set_workspace_bytes(serial_grant(s,cached));
            const auto room=receiver.acquisition_state().input_room_samples;
            std::vector<float> pcm(room);std::mt19937_64 random(781);std::normal_distribution<double> noise(0,.03);
            for(auto& sample:pcm)sample=static_cast<float>(noise(random));
            constexpr std::size_t delay=34;
            check(analytic.size()>=2*padding && room>=delay+analytic.size()-2*padding,"nonzero control PCM is too short");
            for(std::size_t i=padding;i<analytic.size()-padding;++i)
                pcm[delay+i-padding]+=static_cast<float>(.25*analytic[i].real());
            check(receiver.observe_acquisition(pcm)==room && receiver.acquisition_state().ready,
                "actual scoring control failed to observe its original batch");
            if(cached) {
                std::stop_source stop;stop.request_stop();bool cancelled=false;
                try {receiver.process_acquisition(stop.get_token());}catch(const Error&){cancelled=true;}
                check(cancelled && !s.trials && !s.arithmetic_reservation && receiver.acquisition_state().ready,
                    "pre-cancelled actual batch consumed observations or retained cache backing");
            }
            receiver.process_acquisition();work.push_back(receiver.fft_work());trials.push_back(s.trials);
            evidence.push_back(receiver.candidates());bursts.push_back(receiver.take_bursts());
            check(!s.arithmetic_reservation && receiver.working_bytes()<=s.budget,
                "private rows survived scoring/admission or exceeded the grant");
        }
        check(work[0].template_jobs==2107 && work[0].fft_jobs==2107 && work[0].input_transforms==1 &&
            work[0].template_jobs==work[1].template_jobs && work[0].fft_jobs==work[1].fft_jobs &&
            work[0].input_transforms==work[1].input_transforms && trials[0]==trials[1],
            "exact cache changed executed jobs, transforms or original trials");
        check(!work[0].exact_clock_row_builds && !work[0].exact_clock_row_peak_bytes &&
            work[1].exact_clock_row_builds==63 && work[1].exact_clock_row_hits==2044 &&
            work[1].coherent_fft_worker_slots_peak==1,"actual serial cache admission/use was not observed");
        check(!evidence[0].empty() && !bursts[0].empty() && evidence[0].size()==evidence[1].size() &&
            bursts[0].size()==bursts[1].size(),"nonzero actual collection comparison was empty or changed extent");
        for(std::size_t i=0;i<evidence[0].size();++i) {
            const auto& a=evidence[0][i];const auto& b=evidence[1][i];
            check(a.first_sample==b.first_sample && a.end_sample==b.end_sample && a.stream_symbol==b.stream_symbol &&
                bits(a.frequency_hz,b.frequency_hz) && bits(a.score,b.score) && bits(a.alternative_score,b.alternative_score) &&
                a.bit==b.bit && a.stream_phase_samples==b.stream_phase_samples && bits(a.admission_threshold,b.admission_threshold) &&
                a.frequency_hypothesis==b.frequency_hypothesis && a.outer_presence_active==b.outer_presence_active &&
                bits(a.outer_presence_score,b.outer_presence_score) && a.completed_event==b.completed_event,
                "cache changed ordered actual candidate fields/score bits");
        }
        for(std::size_t i=0;i<bursts[0].size();++i) {
            const auto& a=bursts[0][i];const auto& b=bursts[1][i];
            check(a.bits==b.bits && a.first_sample==b.first_sample && a.end_sample==b.end_sample &&
                a.first_stream_symbol==b.first_stream_symbol && bits(a.frequency_hz,b.frequency_hz) && bits(a.score,b.score) &&
                a.complete==b.complete && a.helper_acquired==b.helper_acquired &&
                a.helper_conditioned_acquisition==b.helper_conditioned_acquisition &&
                a.stream_phase_samples==b.stream_phase_samples && a.stream_first_sample==b.stream_first_sample &&
                a.stream_first_symbol==b.stream_first_symbol && a.missing_slots==b.missing_slots && bits(a.support_samples,b.support_samples),
                "cache changed ordered actual payload/progress fields or score bits");
        }
        PatternReceiver parallel(config(),32*1024*1024,search());auto& p=*parallel.impl_;
        const auto room=parallel.acquisition_state().input_room_samples;std::vector<float> zeros(room);
        parallel.observe_acquisition(zeros);parallel.process_acquisition();const auto w=parallel.fft_work();
        if(detail::search_concurrency(2)>=2)check(w.coherent_fft_worker_slots_peak==2 && !w.exact_clock_row_builds &&
            !w.exact_clock_row_peak_bytes && w.template_jobs==2107,"parallel scoring lost workers or used serial cache");
        std::cout<<"clock rows: actual jobs="<<work[1].template_jobs<<" trials="<<trials[1]
            <<" serial_slots="<<work[1].coherent_fft_worker_slots_peak<<" parallel_slots="<<w.coherent_fft_worker_slots_peak
            <<" builds="<<work[1].exact_clock_row_builds<<" hits="<<work[1].exact_clock_row_hits
            <<" peak_bytes="<<work[1].exact_clock_row_peak_bytes<<" ordered_candidates="<<evidence[0].size()
            <<" ordered_bursts="<<bursts[0].size()<<'\n';
    }
};
} // namespace datapump::modem
int main(){try {
    using Test=datapump::modem::PatternReceiverStartupGuideTestAccess;
    Test::seven_cohorts();Test::identity_cancellation_and_grants();Test::executed_paths();
    std::cout<<"exact serial clock rows PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
