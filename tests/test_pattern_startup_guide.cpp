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
             <<t.anchor_start<<' '<<t.anchor_index<<' '<<t.absent_samples<<' '<<t.frequency<<' '
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
        check(!s.fallback && s.fft_workload && s.long_symbol && s.shaped && s.drift_sections==4,
            "startup guide test lost its original public FFT geometry");
        const auto initial=std::min(s.hop,std::max<std::size_t>(1,c.sample_rate/s.bin_samples));
        check(s.bin_samples==750 && s.length==1025 && s.transform==2048 && s.hop==512 && initial==8 &&
            s.peaks.capacity()>=2,"startup guide test lost its exact original cohorts/storage");
        s.trials=s.acquisition_trials(initial);
        PatternEvidence first{s.bin_samples,(1+s.length)*s.bin_samples,0,c.carrier_hz,s.threshold()+.25,1,0,c.stream_phase_samples};
        s.admit(first,0,first.score);
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
        check(t.next==expected_next && t.anchor_start==guide.first_sample/s.bin_samples && t.anchor_index==0 &&
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
int main(){try{datapump::modem::PatternReceiverStartupGuideTestAccess::run();return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
