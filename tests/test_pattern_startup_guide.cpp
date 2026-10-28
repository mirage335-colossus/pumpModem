// Exercise the actual private predicate, collector and mutation, without a
// public control API or a second copy of the production eligibility rules.
#include "../src/pattern_receiver.cpp"
#include "datapump/tuning.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace datapump::modem {
struct PatternReceiverStartupGuideTestAccess {
    using State=PatternReceiver::Impl;
    static void check(bool ok,const char* why){if(!ok)throw Error(why);}
    // Exact original measure() is the oracle, not a copied score formula.
    static void continuation_clock_group_reuse() {
        for(const auto& [groups,hf]:{std::pair{1U,false},std::pair{3U,false},std::pair{9U,false},
                                   std::pair{18U,false},std::pair{9U,true}}) {
            Config c;c.sample_rate=6000;c.carrier_hz=1300;c.bandwidth_hz=200;c.spreading_factor=32;
            c.pulse_shaping=true;c.scramble=groups!=1;c.dsss_factor=c.scramble?10:1;
            if(hf) {
                c.sample_rate=6120;c.carrier_hz=1500;c.bandwidth_hz=36;c.spreading_factor=256;
                c.dsss=true;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;
            }
            c.stream_epoch=1800000041;c.stream_phase_samples=23;
            c.receiver_strategy=ReceiverStrategy::complex_only;
            for(std::size_t i=0;i<c.spreading_seed.size();++i)c.spreading_seed[i]=std::uint8_t(13*i+29);
            for(std::size_t i=0;i<c.dsss_seed.size();++i)c.dsss_seed[i]=std::uint8_t(7*i+113);
            if(hf)c.data_key=Crypto(c.spreading_seed);
            PatternSearch search;search.worker_threads=1;search.initial_stream_symbols=1;search.chunk_bits=1;
            search.track_limit=4;search.candidate_limit=16;search.drift_tolerant=false;
            search.differential_window_seconds=0;search.search_stream_phases=false;search.early_qualified_fft=false;
            for(int f=-4;f<=4;++f)for(unsigned r=0;r<groups;++r)
                search.hypotheses.push_back({.5*f,groups==1?0.:-300.+600.*r/(groups-1)});
            PatternPhaseCurve curve; // Borrowed view outlives the receiver.
            PatternReceiver receiver(c,(hf?64:32)*1024*1024,search);auto& state=*receiver.impl_;
            check(!state.fallback && state.drift_sections==1 && !state.sample_fit &&
                  !state.differential_window && !state.calendar_symbols() && state.product.size()/2>=state.length,
                  "clock-group fixture lost its original native coherent scratch geometry");
            if(hf)check(c.dsss && c.data_key && state.code.symbol_samples()==87040 && state.bin_samples==17 &&
                        state.length==5122 && state.transform==16384 && state.hop==11263,
                        "clock-group HF fixture lost actual DSSS/Interleave source and projection geometry");
            const auto current=std::size_t{4*groups+groups/2};
            const auto eligible=[](std::size_t){return true;};
            std::vector<std::array<double,2>> cached(state.templates.size(),{-101,-202});
            if(groups==18) {
                const auto before=cached;const auto tag=state.tracking_reference_valid;
                check(!state.measure_clock_groups(0,1,0,current,eligible,cached,{}) && cached==before &&
                      state.tracking_reference_valid==tag,"unsupported clock-group cache changed scores or template ownership");
                continue;
            }
            const auto index=std::uint64_t{7},start=std::uint64_t{13};
            state.stream_phase(c.stream_phase_samples);
            if(groups==1) {
                state.scoring.resize(1);auto& row=state.scoring.front().nominal_reference;row.resize(state.length);
                detail::prepare_fft_nominal_reference(state.scoring_geometry(),state.code,row,{});
            }
            if(groups==9) {
                state.frequency_origin_hz=state.acquisition_origin_hz=2.17;
                check(curve.configure(51,c.sample_rate,2.17,0,state.bin_samples),"clock-group phase configure failed");
                const PatternPhaseSegment segment{2*state.bin_samples,0,hf?.000017:.00017};
                check(curve.append(std::span(&segment,1),0,10000000),"clock-group phase append failed");
                state.execution_phase_view=&curve;
            }
            state.bins=std::min<std::uint64_t>(state.ring.size(),start+state.length+20);
            state.sample=state.bins*state.bin_samples;
            for(std::size_t i=0;i<state.ring.size();++i)state.ring[i]={.01*std::sin(.37*i),.01*std::cos(.23*i)};
            state.code.prepare_symbol(index*state.code.chips_per_symbol());
            for(std::size_t i=0;i<state.length;++i) {
                const auto pair=state.template_values(state.code,i,index,current);
                state.ring[(start+i)%state.ring.size()]+=.7*pair[1]*std::conj(state.phase_weight(start+i));
            }
            state.prepare_phase_window(start,state.length,{});
            const auto observed_before=start+3;
            std::vector<std::array<double,2>> original(state.templates.size());
            for(std::size_t f=0;f<original.size();++f) {
                const auto fit=state.measure(start,index,f,observed_before,{},{});
                original[f][fit.bit]=fit.score;original[f][1-fit.bit]=fit.alternative_score;
            }
            const auto trials=state.trials,shifted=state.shifted_trials;
            check(state.measure_clock_groups(start,index,observed_before,current,eligible,cached,{}),
                  "repeated original clock rows did not qualify for exact cache reuse");
            check(state.trials==trials && state.shifted_trials==shifted,"clock-row arithmetic spent admission trials");
            for(std::size_t f=0;f<original.size();++f)if(f!=current)
                check(cached[f]==original[f],"clock-row reuse changed an original zero/one cell score");
            check(cached[current]==std::array<double,2>{-101,-202},"clock-row reuse overwrote primary timing fit");
            if(groups>1) {
                bool padded=false;
                for(std::size_t f=0;f<original.size();++f)padded|=state.variable_clock &&
                    (static_cast<long double>(state.length-1)*state.bin_samples+(state.bin_samples-1)/2.L)*
                        state.clock_ratio(f)>=state.code.symbol_samples();
                check(padded,"clock-row fixture lost original fast-rate zero padding");
            } else {
                check(state.tracking_reference.empty(),"cancellation control no longer reuses product tracking storage");
                const auto expected=state.measure(start,index,current,observed_before,{},{});
                check(state.tracking_reference_valid,"cancellation control has no old product cache tag");
                std::stop_source stop;std::size_t calls=0;bool caught=false;
                const auto cancel_after_base=[&](std::size_t) {
                    if(++calls>state.templates.size()-1)stop.request_stop();return true;
                };
                try{(void)state.measure_clock_groups(start,index,observed_before,current,cancel_after_base,cached,stop.get_token());}
                catch(const Error& error){caught=std::string(error.what())=="pattern search cancelled";}
                check(caught && calls>state.templates.size()-1 && !state.tracking_reference_valid,
                      "cancelled source-row scratch retained a modulated-template cache tag");
                const auto actual=state.measure(start,index,current,observed_before,{},{});
                check(actual.score==expected.score && actual.alternative_score==expected.alternative_score && actual.bit==expected.bit,
                      "ordinary measure after cancellation reused an unmodulated source row");
            }
        }
        std::cout<<"exact repeated-clock cell scores, padding, phase, refusal and scratch reuse passed\n";
    }
    static void helper_owner_unknown_replacement() {
        for(unsigned scenario=0;scenario<3;++scenario) {
            Config c;c.sample_rate=2000;c.carrier_hz=500;c.bandwidth_hz=200;c.spreading_factor=128;
            c.pulse_shaping=false;c.scramble=true;c.stream_epoch=1800000041;
            PatternSearch search;search.hypotheses={{0,0},{1,0}};search.initial_stream_symbols=1;
            search.chunk_bits=1024;search.worker_threads=1;search.drift_tolerant=false;
            search.differential_window_seconds=0;search.search_stream_phases=false;
            PatternReceiver receiver(c,8*1024*1024,search);auto& state=*receiver.impl_;
            check(!state.fallback,"owner replacement control lost ordinary native geometry");
            const std::array<std::uint8_t,2> mask{1,0};
            if(scenario==0)check(receiver.set_acquisition_prior(0,mask),"helper-born owner setup failed");
            state.prior_dispatch=scenario==0;
            const auto first=4*state.bin_samples;
            PatternEvidence accepted{first,first+state.length*state.bin_samples,0,c.carrier_hz,
                state.threshold()+1,0,0,c.stream_phase_samples};
            state.admit(accepted,0,accepted.score,0,nullptr,{});
            check(state.tracks.size()==1 && state.tracks.front().established && state.completed.empty() && state.bursts.empty(),
                  "owner replacement control did not retain an unpublished accepted prefix");
            const auto serial=state.tracks.front().serial;
            if(scenario==0)check(receiver.clear_acquisition_prior(),"helper loss setup could not clear fresh acquisition");
            if(scenario==1)check(receiver.set_acquisition_prior(0,mask),"new prior setup failed");
            state.prior_dispatch=scenario==1;
            auto rival=accepted;rival.first_sample+=state.bin_samples;rival.end_sample+=state.bin_samples;
            rival.score=2*accepted.score+100;rival.bit=1;
            check(!state.independent_overlap(state.tracks.front().burst,rival,state.prior_dispatch),
                  "unknown replacement control accidentally proved distinct source domains");
            state.admit(rival,0,rival.score,0,nullptr,{});
            check(state.tracks.size()==1 && state.bursts.empty() && state.completed.empty() &&
                  !state.tracks.front().burst.complete,"replacement manufactured output or physical END");
            const auto& owner=state.tracks.front();
            if(scenario<2) {
                check(owner.serial==serial && owner.burst.bits==Bytes{0} && owner.confirmed==1 &&
                      owner.burst.stream_first_sample==first && owner.established,
                      "unknown helper-involved fresh peak withdrew an accepted owner");
            } else check(owner.serial!=serial && owner.burst.bits==Bytes{1} &&
                         owner.burst.stream_first_sample==rival.first_sample,
                         "ordinary stronger-start replacement policy changed");
            std::cout<<"unknown replacement scenario="<<scenario<<" original_serial="<<serial
                     <<" retained_serial="<<owner.serial<<" helper="<<owner.burst.helper_acquired<<'\n';
        }
    }
    static void burst(std::ostream& o,const PatternBurst& b) {
        o<<b.first_sample<<' '<<b.end_sample<<' '<<b.first_stream_symbol<<' '<<b.frequency_hz<<' '<<b.score<<' '
         <<b.complete<<' '<<b.stream_phase_samples<<' '<<b.stream_first_sample<<' '<<b.stream_first_symbol<<' '
         <<b.missing_slots<<' '<<b.support_samples<<' '<<b.bits.size()<<' ';
        for(auto bit:b.bits)o<<unsigned(bit)<<' ';
    }
    static std::string publication(const State& s) {
        std::ostringstream o;o<<std::hexfloat<<std::setprecision(std::numeric_limits<long double>::max_digits10);
        burst(o,s.latest);o<<s.bursts.size()<<' ';for(const auto& b:s.bursts)burst(o,b);return o.str();
    }
    static void evidence(std::ostream& o,const PatternEvidence& p) {
        o<<p.first_sample<<' '<<p.end_sample<<' '<<p.stream_symbol<<' '<<p.frequency_hz<<' '<<p.score<<' '
         <<p.alternative_score<<' '<<p.bit<<' '<<p.stream_phase_samples<<' '<<p.frequency_hypothesis<<' '
         <<p.admission_threshold<<' '<<p.outer_presence_active<<' '<<p.outer_presence_score<<' '<<p.completed_event<<' ';
    }
    static std::string history(const State& s) {
        std::ostringstream o;o<<std::hexfloat<<std::setprecision(std::numeric_limits<long double>::max_digits10);
        o<<s.history_insertions<<' '<<s.history.size()<<' ';for(const auto& p:s.history)evidence(o,p);return o.str();
    }
    static std::string snapshot(const State& s) {
        std::ostringstream o;o<<std::hexfloat<<std::setprecision(std::numeric_limits<long double>::max_digits10);
        o<<publication(s)<<' '<<s.next_start<<' '<<s.trials<<' '<<s.sample<<' '<<s.bins<<' '
         <<s.tracking_reference_valid<<' '<<s.active_stream_phase<<' '<<s.candidate_limit_reached<<' ';
        o<<s.tracks.size()<<' ';
        for(const auto& t:s.tracks) {
            burst(o,t.burst);o<<t.next<<' '<<t.index<<' '<<t.unconfirmed_symbols<<' '<<t.phase_lower<<' '<<t.phase_upper<<' '
             <<t.total_score<<' '<<t.penalty<<' '<<t.pending_score<<' '<<t.confirmed_score<<' '
             <<t.total_support<<' '<<t.confirmed_support<<' '<<t.confirmed<<' '<<t.gap_slots<<' '<<t.confirmed_end<<' '
             <<s.calendar_symbols()<<' ';
            if(s.calendar_symbols())o<<t.coordinates.calendar.start<<' '<<t.coordinates.calendar.index<<' ';
            else o<<t.coordinates.prefix.support<<' '<<t.coordinates.prefix.end<<' ';
            o<<t.absent_samples<<' '<<t.frequency<<' '
             <<t.admitted<<' '<<t.established<<' '<<t.pending_gap<<' '<<t.frequency_scores.size()<<' ';
            for(auto value:t.frequency_scores)o<<value<<' ';
        }
        o<<s.completed.size()<<' ';for(const auto& c:s.completed)o<<c.first<<' '<<c.end<<' '<<c.frequency<<' ';
        o<<s.peaks.size()<<' ';for(const auto& p:s.peaks)evidence(o,p);o<<history(s);
        return o.str();
    }
    static std::unique_ptr<PatternReceiver> make() {
        auto c=datapump::tuning::resolve(1,-3.0720996964786846,datapump::tuning::PatternMode::auto_pattern,false,1500).config;
        c.integration_seconds=128;c.stream_epoch=1800000041;c.receiver_strategy=ReceiverStrategy::existing_only;
        // Retain the original 750-sample projection and slow-lane padding
        // while reducing the number of independently paired alternatives.
        PatternSearch search;search.hypotheses={{0,-.0003},{1.75,.0003}};
        search.worker_threads=1;search.chunk_bits=1;search.track_limit=4;search.candidate_limit=8;
        auto receiver=std::make_unique<PatternReceiver>(c,8*1024*1024,search);auto& s=*receiver->impl_;
        check(!s.fallback && s.fft_workload && s.long_symbol && s.shaped && s.drift_sections==4 && !s.calendar_symbols(),
            "startup guide test lost its original public FFT geometry");
        const auto initial=std::min(s.hop,std::max<std::size_t>(1,c.sample_rate/s.bin_samples));
        check(s.bin_samples==750 && s.length==1025 && s.transform==2048 && s.hop==512 && initial==8 &&
            s.peaks.capacity()>=2,"startup guide test lost its exact original cohorts/storage");
        s.trials=s.acquisition_trials(initial);
        PatternEvidence first{s.bin_samples,(1+s.length)*s.bin_samples,0,c.carrier_hz,s.threshold()+.25,1,0,c.stream_phase_samples};
        s.admit(first,0,first.score,0,nullptr,std::stop_token{});
        s.next_start=initial;s.sample=(initial+s.hop-1+s.length)*s.bin_samples;s.bins=s.sample/s.bin_samples;
        check(s.startup_guide_owner(s.hop,false),"actual first-bit publication did not qualify the startup witness");
        return receiver;
    }
    static void collect(State& s,bool opposite=false,bool tie=false,bool reverse=false,bool poison=false) {
        const auto prior_history=history(s);
        s.peaks.clear();s.peaks.resize(2);for(unsigned b=0;b<2;++b){s.peaks[b].score=-1;s.peaks[b].bit=b;}
        const auto winning=s.latest.score+100;
        for(std::size_t rank=0;rank<s.hop;++rank) {
            const auto j=reverse?s.hop-1-rank:rank;
            for(std::size_t f=0;f<s.templates.size();++f) {
                double a=(j==17 && f==1)?winning:1,b=2;
                if((opposite||tie) && j==s.hop-1 && f==0)b=winning+(opposite?1:0);
                if(poison && rank==0 && f==0)a=std::numeric_limits<double>::quiet_NaN();
                s.collect_score(a,b,j,0,s.config.stream_phase_samples,f,true);
            }
        }
        check(s.trials==s.acquisition_trials(s.next_start)+s.acquisition_trials(s.hop),
            "startup collector changed the original full-cell trial charge");
        check(history(s)==prior_history,"startup collector inserted diagnostic candidates or changed history order");
    }
    template<class Change>static void refusal(const char* name,Change change) {
        auto rx=make();auto& s=*rx->impl_;collect(s);change(s);
        const auto before=snapshot(s);check(!s.adopt_startup_guide(s.hop,false),name);
        check(snapshot(s)==before,"refused startup guide mutated receiver state");
    }
    static std::unique_ptr<PatternReceiver> storage_fixture(std::size_t bytes) {
        auto c=datapump::tuning::resolve(1,-3.0720996964786846,datapump::tuning::PatternMode::auto_pattern,false,1500).config;
        c.integration_seconds=128;c.stream_epoch=1800000041;c.receiver_strategy=ReceiverStrategy::existing_only;
        PatternSearch search;search.hypotheses={{0,-.0003},{1.75,.0003}};
        search.worker_threads=1;search.chunk_bits=1;search.track_limit=16;search.candidate_limit=8;
        search.drift_tolerant=false;
        return std::make_unique<PatternReceiver>(c,bytes,search);
    }
    static void storage_boundary() {
        auto roomy=storage_fixture(8*1024*1024);auto& reference=*roomy->impl_;
        check(!reference.fallback && !reference.streamed_templates && reference.templates.size()==2,
            "storage fixture did not retain the optional transformed template rows");
        const auto row_bytes=2*reference.templates.size()*reference.transform*sizeof(Complex);
        check(row_bytes>0 && reference.fixed_reservation>row_bytes,
            "storage fixture did not expose fixed rows and transformed template rows separately");
        const auto full_fixed=reference.fixed_reservation;
        const auto minimum=2*reference.search.track_limit+2;
        check(minimum>0,"storage fixture has no minimum bit-candidate allowance");

        for(const int offset:{-1,0,1}) {
            const auto allowance=static_cast<std::size_t>(static_cast<std::ptrdiff_t>(minimum)+offset);
            check(full_fixed<=std::numeric_limits<std::size_t>::max()-allowance,
                "storage fixture grant arithmetic overflowed");
            const auto grant=full_fixed+allowance;
            std::unique_ptr<PatternReceiver> receiver;
            try { receiver=storage_fixture(grant); }
            catch(const Error& error) {
                if(offset==-1 && std::string(error.what())=="pattern workspace cannot retain bit candidates")
                    throw Error("optional rows were retained at the subminimum bit-storage boundary");
                throw;
            }
            auto& state=*receiver->impl_;
            check(!state.fallback && !state.local_search_fallback,
                "storage boundary reduced coverage through a local-clock fallback");
            check(state.search.hypotheses.size()==2 && state.search.frequency_offsets_hz.size()==2 &&
                state.search.hypotheses[0].frequency_offset_hz==0 && state.search.hypotheses[0].clock_error_ppm==-.0003 &&
                state.search.hypotheses[1].frequency_offset_hz==1.75 && state.search.hypotheses[1].clock_error_ppm==.0003,
                "storage boundary dropped or changed a configured frequency/rate hypothesis");
            check(state.search.bit_limit>0 && state.working_bytes()<=grant,
                "storage boundary did not retain positive bit capacity within its grant");
            check(state.streamed_templates==(offset<0),
                "optional template rows did not stream only below the minimum cached bit allowance");
            if(offset<0) {
                check(state.fixed_reservation<full_fixed && state.fixed_reservation+minimum<=grant,
                    "streamed fixed reservation did not leave the minimum bit-candidate allowance");
            } else {
                check(state.fixed_reservation==full_fixed && grant-state.fixed_reservation>=minimum,
                    "sufficient cached bit allowance changed the fixed reservation");
            }
        }
    }
    static void run() {
        auto rx=make();auto& s=*rx->impl_;const auto memory=s.working_bytes();collect(s);
        const auto published=publication(s);const auto old=s.tracks.front();const auto span=s.completed.front();
        const auto trials=s.trials;const auto guide=s.peaks[0];
        const auto expected_next=guide.first_sample/s.bin_samples+s.advance_bins(0,old.phase_lower,guide.frequency_hypothesis);
        check(s.adopt_startup_guide(s.hop,false),"complete same-bit startup guide was refused");
        const auto& t=s.tracks.front();
        check(publication(s)==published,"startup guide rewrote the first queued or latest publication");
        check(t.index==old.index && t.burst.first_stream_symbol==old.burst.first_stream_symbol &&
            t.burst.stream_first_sample==old.burst.stream_first_sample && t.burst.stream_first_symbol==old.burst.stream_first_symbol &&
            t.burst.stream_phase_samples==old.burst.stream_phase_samples && t.burst.bits==old.burst.bits &&
            t.total_score==old.total_score && t.confirmed_score==old.confirmed_score && t.pending_score==old.pending_score &&
            t.total_support==old.total_support && t.confirmed_support==old.confirmed_support && t.penalty==old.penalty &&
            t.confirmed==old.confirmed && t.gap_slots==old.gap_slots && t.pending_gap==old.pending_gap &&
            t.unconfirmed_symbols==old.unconfirmed_symbols && t.absent_samples==old.absent_samples && s.trials==trials,
            "startup guide emitted a bit, reset absence or borrowed prefix admission evidence");
        // A timing/frequency guide consumes this already-published prefix;
        // it supplies no independent pre-last support checkpoint to reuse.
        check(!s.calendar_symbols() && std::isnan(t.coordinates.prefix.support) && t.coordinates.prefix.end==UINT64_MAX &&
            t.next==expected_next &&
            t.frequency==guide.frequency_hypothesis && t.burst.frequency_hz==guide.frequency_hz &&
            t.confirmed_end==guide.end_sample && t.burst.first_sample==guide.end_sample && t.burst.end_sample==guide.end_sample,
            "startup guide lost the original pair or consumed-prefix fence");
        check(s.completed.size()==1 && s.completed.front().first==span.first && s.completed.front().frequency==span.frequency &&
            s.completed.front().end==guide.end_sample,"startup guide released ownership of its reused prefix tail");
        for(std::size_t lane=0;lane<t.frequency_scores.size();++lane)
            check(t.frequency_scores[lane]==(lane==guide.frequency_hypothesis?guide.score:0),
                "startup guide added its carrier evidence to the published prefix twice");
        check(s.working_bytes()==memory,"startup guide grew retained receiver storage");
        const auto after=snapshot(s);check(!s.adopt_startup_guide(s.hop,false) && snapshot(s)==after,
            "startup guide recentered the immutable domain a second time");

        refusal("complete prefix accepted",[](auto& x){x.latest.complete=true;});
        refusal("changed identity accepted",[](auto& x){++x.latest.stream_first_sample;});
        refusal("changed phase accepted",[](auto& x){++x.latest.stream_phase_samples;});
        refusal("multiple owners accepted",[](auto& x){x.tracks.push_back(x.tracks.front());});
        refusal("multiple spans accepted",[](auto& x){x.completed.push_back(x.completed.front());});
        refusal("pending gap accepted",[](auto& x){x.tracks.front().pending_gap=true;});
        refusal("failed duration accepted",[](auto& x){x.tracks.front().absent_samples=1;});
        refusal("unpublished tail accepted",[](auto& x){x.tracks.front().burst.bits.push_back(0);});
        refusal("partial trial charge accepted",[](auto& x){--x.trials;});
        refusal("later cohort accepted",[](auto& x){++x.next_start;});
        refusal("keyed owner accepted",[](auto& x){x.config.scramble=true;});
        refusal("DSSS owner accepted",[](auto& x){x.config.dsss=true;});
        refusal("outer DSSS owner accepted",[](auto& x){x.config.dsss_factor=2;});
        refusal("private phase search accepted",[](auto& x){x.search.search_stream_phases=true;});
        refusal("multiple initial ordinals accepted",[](auto& x){x.search.initial_stream_symbols=2;});
        refusal("undersized peak storage accepted",[](auto& x){x.search.candidate_limit=1;});
        refusal("differential owner accepted",[](auto& x){x.differential_window=1;});
        refusal("alternate arithmetic accepted",[](auto& x){x.config.search_arithmetic=SearchArithmetic::fp32;});
        refusal("restricted timing accepted",[](auto& x){x.fft_workload->restricted_timing_active=true;});
        refusal("component replay accepted",[](auto& x){x.component_hop=true;});
        refusal("EOF replay accepted",[](auto& x){x.early_eof_pending=true;});
        refusal("wrong lane accepted",[](auto& x){x.peaks[0].frequency_hypothesis=static_cast<std::uint32_t>(x.templates.size());});
        refusal("unobserved support accepted",[](auto& x){x.sample=x.peaks[0].end_sample-1;});
        refusal("incomplete support accepted",[](auto& x){--x.peaks[0].end_sample;});
        refusal("frozen radius endpoint accepted",[](auto& x){auto& g=x.peaks[0];g.first_sample=x.latest.first_sample+x.code.symbol_samples()/2;g.end_sample=g.first_sample+x.length*x.bin_samples;});
        refusal("unchanged weak guide accepted",[](auto& x){x.peaks[0].score=x.latest.score;});
        refusal("subthreshold guide accepted",[](auto& x){x.peaks[0].score=x.threshold()-1;
            check(x.peaks[0].score>x.latest.score && x.peaks[0].score>x.peaks[1].score &&
                x.peaks[0].score-x.peaks[0].alternative_score>=1,
                "subthreshold fixture failed before its independent admission gate");});
        refusal("wrong guide phase accepted",[](auto& x){++x.peaks[0].stream_phase_samples;});
        refusal("ambiguous guide cell accepted",[](auto& x){x.peaks[0].alternative_score=x.peaks[0].score-.5;});
        refusal("changed guide bit accepted",[](auto& x){x.peaks[0].bit=1;});
        for(const bool reverse:{false,true})for(const bool tied:{false,true}) {
            auto other=make();auto& x=*other->impl_;collect(x,!tied,tied,reverse);
            check(x.peaks[1].first_sample==(x.next_start+x.hop-1)*x.bin_samples,
                "opposite global maximum was lost through normal peak compression");
            const auto before=snapshot(x);check(!x.adopt_startup_guide(x.hop,false) && snapshot(x)==before,
                "conflicting or tied global bit maximum guided an emitted prefix");
        }
        auto poisoned=make();auto& p=*poisoned->impl_;collect(p,false,false,false,true);
        check(std::isnan(p.peaks[0].score) && std::isnan(p.peaks[1].score),"finite scores erased startup NaN poison");
        const auto before=snapshot(p);check(!p.adopt_startup_guide(p.hop,false) && snapshot(p)==before,
            "nonfinite original cell guided an emitted prefix");
        auto final=make();auto& f=*final->impl_;collect(f);const auto unchanged=snapshot(f);
        check(!f.adopt_startup_guide(f.hop,true) && !f.adopt_startup_guide(f.hop-1,false) && snapshot(f)==unchanged,
            "EOF or incomplete cohort guided a published prefix");
        std::cout<<"startup guide actual publication, collector, refusal and workspace checks passed\n";
    }
};
}
int main(){try{datapump::modem::PatternReceiverStartupGuideTestAccess::continuation_clock_group_reuse();datapump::modem::PatternReceiverStartupGuideTestAccess::storage_boundary();datapump::modem::PatternReceiverStartupGuideTestAccess::run();datapump::modem::PatternReceiverStartupGuideTestAccess::helper_owner_unknown_replacement();return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
