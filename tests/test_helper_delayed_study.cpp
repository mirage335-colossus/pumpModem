#define main helper_continuation_included_main
#include "test_helper_continuation_study.cpp"
#undef main
#include "datapump/helper_delayed_buffer.hpp"
#include "datapump/helper_delayed_smoother.hpp"
#include "helper_complete_apsk_metrics.hpp"
#include "helper_complete_stream_metrics.hpp"
#include <deque>

namespace {
struct DelayedRun {
    struct Item {hp::Dwell d;Phase phase;bool authority=false,missing=false;double reference=0,latest_observation_end=0;
        std::array<h::HelperJointEstimator::Estimate,5> causal_predictions{};};
    Chain chain;
    h::HelperDelayedBuffer buffer;
    h::HelperDelayedSmoother smoother;Coeff retained;double retained_rf=0;bool have_retained=false;
    std::array<h::HelperDelayedBuffer::CaptureBlock,100> captures{};
    struct TxMap {bool valid=false;long double reference_sample=0,anchor_sample=0,source_at_anchor=0;
        double audio_scale=1,source_half_width_seconds=0,audio_half_width_ppm=0;};
    std::array<TxMap,100> tx_maps{};
    std::array<h::HelperDelayedBuffer::ReleasedBlock,100> released_metadata{};
    // 0=unreleased, 1=raw delivered without correction authority,
    // 2=conditioner output with per-sample support in the ordered trace.
    std::array<std::uint8_t,100> released_block_kind{};
    // phase_kind: 0=no fresh same-hop phase, 1=fresh same-hop phase applied,
    // 2=filter footprint crosses a nominal hop boundary. It describes what
    // the receiver did, not whether the physical RF boundary was correct.
    struct ScheduleSegment {long double first=0,last=0;std::uint64_t epoch=1,slot=0,count=0,processing_release_ns=0;
        std::uint8_t phase_kind=0;bool valid=false,supported=false,boundary_ambiguous=true;};
    std::array<ScheduleSegment,4096> output_schedule{};std::size_t output_schedule_count=0;
    std::array<bool,100> helper_missing{};
    std::deque<Item> items;
    std::vector<S> raw;
    bool extended,causal=false,latequal=false,full=false;std::size_t edge_inner=120;std::uint64_t delay_ns;unsigned released=0,short_future=0,metadata_errors=0,smooth_rejected=0;
    // Receiver-consumer capture for blocks released before clock authority.
    // These actual samples retain their CaptureBlock metadata and are marked
    // uncorrected; this is test-consumer storage, not buffer capacity.
    std::vector<S> startup_uncorrected;
    // Captured bytes at the two conditioner boundaries are retained to avoid
    // presenting zero-padding-dependent FIR output as physical input. Bounded
    // test-consumer storage; never populated with generated replacement data.
    static constexpr std::size_t edge_capture_capacity=128;
    std::array<S,edge_capture_capacity> first_conditioned_raw{},last_capture_raw{};
    std::uint64_t first_conditioned_index=0,last_capture_index=0,active_processing_release_ns=0;
    bool first_conditioned_raw_valid=false,last_capture_raw_valid=false;
    double buffer_cpu=0,smoother_cpu=0,conditioning_cpu=0,drain_cpu=0,drain_twin_cpu=0;std::size_t highwater=0,drained_samples=0;
    bool conditioner_finished=false;
    double cpu36_total=0,cpu36_conditioned_seconds=0;
    std::array<unsigned,9> smoother_failures{};
    double min_first_age=1e9,max_first_age=0,min_last_age=1e9,max_last_age=0;
    std::uint64_t startup_raw_unsupported=0,raw_sink_hash=1469598103934665603ULL;unsigned startup_unsupported_blocks=0,clock_discontinuities=0;
    double raw_sink_cpu=0;
    bool system_step=false;double capture_rate_scale=1,capture_fraction=0;
    bool schedule_at(long double pos,std::uint64_t& slot)const{
        if(!std::isfinite(pos)||pos<0||pos>=100.L*hp::dwell_samples)return false;
        const auto block=static_cast<std::size_t>(pos/hp::dwell_samples);const auto& b=captures[block];
        if(!b.sample_count)return false;
        const long double mono=b.capture_monotonic_nanoseconds+(pos-b.first_sample)*b.sample_period_nanoseconds;
        constexpr long double epoch=1'000'000'000.L,span=400'000'000.L,uncertainty=8'000'000.L;
        const long double uncertainty_total=uncertainty+b.capture_monotonic_uncertainty_nanoseconds+
            (pos-b.first_sample)*b.sample_period_uncertainty_nanoseconds+2;
        if(mono-uncertainty_total<epoch)return false;
        const auto first=static_cast<std::uint64_t>(std::floor((mono-uncertainty_total-epoch)/span));
        const auto last=static_cast<std::uint64_t>(std::floor((mono+uncertainty_total-epoch)/span));
        if(first!=last)return false;slot=first;return true;
    }
    std::pair<std::uint8_t,bool> provenance(long double pos)const{
        if(!full||!chain.stream.noisy||!std::isfinite(pos)||pos<0||pos>=100.L*hp::dwell_samples)return {0,true};
        const long double halo=std::ceil(chain.stream.noisy->lookahead_seconds()*fs);
        std::uint64_t left=0,right=0;
        const bool ambiguous=!schedule_at(pos-halo,left)||!schedule_at(pos+halo,right)||left!=right;
        if(pos-halo<0||pos+halo>=100.L*hp::dwell_samples)return {2,ambiguous};
        const auto first=static_cast<std::size_t>((pos-halo)/hp::dwell_samples);
        const auto last=static_cast<std::size_t>((pos+halo)/hp::dwell_samples);
        if(first!=last)return {2,ambiguous};
        return {chain.stream.fresh_phase_by_dwell[first]?std::uint8_t{1}:std::uint8_t{0},ambiguous};
    }
    DelayedRun(const std::string& name,const h::HelperMeasurementConfig& mc,double rf,double delay,bool ext)
        :chain(name,h::HelperAidedFrequencyMethod::subaperture_slope,true,true,mc,rf,true),
         buffer({12*hp::dwell_samples,12,static_cast<std::uint64_t>(std::llround(delay*1e9)),1'000'000}),
         smoother({delay,3.4,2400,980}),raw(hp::dwell_samples),extended(ext),delay_ns(static_cast<std::uint64_t>(std::llround(delay*1e9))) {
        chain.receiver.use_joint_dwell=true;chain.stream.conditional_guard=true;chain.stream.store_input_dates=true;chain.stream.count_output_rejections=true;
        // Prevent correction authority on an output whose exact fractional
        // input date cannot be mapped to the original capture-clock epoch.
        chain.stream.output_metadata_valid=[this](long double pos){
            if(!std::isfinite(pos)||pos<0||pos>=100.L*hp::dwell_samples)return false;
            const auto slot=static_cast<std::size_t>(pos/hp::dwell_samples);const auto& b=captures[slot];long double t=0,u=0;
            std::uint64_t schedule_slot=0;
            if(!tx_maps[slot].valid||!schedule_at(pos,schedule_slot)||schedule_slot!=slot)return false;
            if(full){
                const auto halo=std::ceil(chain.stream.noisy->lookahead_seconds()*fs);
                std::uint64_t left=0,right=0;
                if(!schedule_at(pos-halo,left)||!schedule_at(pos+halo,right)||left!=schedule_slot||right!=schedule_slot)return false;
            }
            if(h::HelperDelayedBuffer::capture_system_time_at(b,pos,t,u))return true;
            if(slot+1>=captures.size()||!b.sample_count)return false;
            const auto& next=captures[slot+1];const auto tail=pos-static_cast<long double>(b.first_sample+b.sample_count-1);
            if(!(tail>0&&tail<1)||next.first_sample!=b.first_sample+b.sample_count||
               b.capture_clock_epoch!=next.capture_clock_epoch)return false;
            long double last=0,lu=0,first=0,fu=0;
            if(!h::HelperDelayedBuffer::capture_system_time_at(b,b.first_sample+b.sample_count-1,last,lu)||
               !h::HelperDelayedBuffer::capture_system_time_at(next,next.first_sample,first,fu))return false;
            return std::abs((first-last)-b.sample_period_nanoseconds)<=lu+fu+b.sample_period_uncertainty_nanoseconds;
        };
        // Consumer-facing bounded schedule metadata, filled AFTER all support
        // gates. Actual fractional capture coordinates remain in input_dates.
        chain.stream.output_metadata_deliver=[this](long double pos,bool supported){
            std::uint64_t slot=0;const bool valid=schedule_at(pos,slot);
            const auto [phase_kind,boundary_ambiguous]=provenance(pos);
            if(supported&&!valid)throw std::runtime_error("supported output lacks schedule metadata");
            if(!output_schedule_count||output_schedule[output_schedule_count-1].valid!=valid||
               output_schedule[output_schedule_count-1].supported!=supported||
               output_schedule[output_schedule_count-1].phase_kind!=phase_kind||
               output_schedule[output_schedule_count-1].boundary_ambiguous!=boundary_ambiguous||
               output_schedule[output_schedule_count-1].processing_release_ns!=active_processing_release_ns||
               (valid&&output_schedule[output_schedule_count-1].slot!=slot)){
                if(output_schedule_count>=output_schedule.size())throw std::runtime_error("schedule metadata capacity exceeded");
                auto& seg=output_schedule[output_schedule_count++];seg.first=pos;seg.slot=slot;seg.valid=valid;seg.supported=supported;
                seg.phase_kind=phase_kind;seg.boundary_ambiguous=boundary_ambiguous;seg.processing_release_ns=active_processing_release_ns;
            }
            auto& seg=output_schedule[output_schedule_count-1];seg.last=pos;++seg.count;
        };
    }
    static h::HelperRelativeClock::Estimate coordinate(const h::HelperJointEstimator::Estimate& j){
        h::HelperRelativeClock::Estimate c;c.valid=j.valid;c.reference_preserved=j.reference_preserved;c.reference_initialized=j.reference_initialized;
        c.reference_input_seconds=j.reference_input_seconds;c.input_seconds=j.time_seconds;c.source_seconds=j.source_seconds;c.audio_scale=j.audio_scale;
        c.source_half_width_seconds=3*j.source_stochastic_sigma_seconds+j.source_model_half_width_seconds;
        c.audio_half_width_ppm=3*j.audio_stochastic_sigma_ppm+j.audio_model_half_width_ppm;c.audio_drift_ppm_per_second=j.audio_drift_ppm_per_second;return c;
    }
    void observe(const hp::Dwell& d,bool missing,unsigned index,const ContinuationSource& source){
        chain.observe(d,missing,index);const auto eval=cpu();chain.evaluate(source,d,index);chain.evaluator_cpu+=cpu()-eval;
        const auto start=cpu();double excluded_prediction_cpu=0;h::HelperDelayedBuffer::CaptureBlock b;
        b.first_sample=static_cast<std::uint64_t>(index)*hp::dwell_samples;b.sample_count=hp::dwell_samples;
        const long double physical=(static_cast<long double>(b.first_sample)+capture_fraction)*1e9L/(fs*capture_rate_scale);
        b.capture_monotonic_nanoseconds=1'000'000'000ULL+static_cast<std::uint64_t>(std::llround(physical));
        b.capture_system_nanoseconds=1'700'000'000'000'000'000LL+static_cast<std::int64_t>(std::llround(physical));
        b.sample_period_nanoseconds=1e9L/(fs*capture_rate_scale);b.capture_uncertainty_nanoseconds=100;
        b.capture_monotonic_uncertainty_nanoseconds=100;b.sample_period_uncertainty_nanoseconds=b.sample_period_nanoseconds*1e-7;b.capture_clock_epoch=1;
        if(system_step&&index>=50){b.capture_system_nanoseconds-=2'000'000'000LL;b.capture_clock_epoch=2;}
        // A block can cross an actual retune: a per-block schedule slot would
        // overstate knowledge. Retain sample indices and compute the separate
        // nominal schedule hypothesis only for evaluator comparison.
        b.schedule_valid=false;
        require(buffer.push(b,d.samples)==h::HelperDelayedBuffer::PushResult::accepted,"delayed push failed");
        captures[index]=b;helper_missing[index]=missing||!chain.current_joint_accepted;Item x;x.d.first_seconds=d.first_seconds;x.d.shift_hz=d.shift_hz;x.d.clean_probe=d.clean_probe;x.phase=chain.receiver.phase;
        const auto& js=chain.joint_state;
        // Optional bounded retrospective qualification: an earlier raw dwell
        // must have been freshly accepted on the same initialized unique
        // reference; delay may complete a fit, not invent the observation.
        const bool viable=js.valid||(latequal&&js.reference_initialized&&js.reference_preserved&&
            js.model_consistent&&!js.ambiguity_limited&&!js.work_limited&&!js.post_gap_qualification_pending&&js.alternatives==1);
        x.authority=viable&&chain.current_joint_accepted&&!chain.joint_suspended;x.missing=missing;
        x.reference=chain.joint_state.reference_input_seconds;
        if(causal&&x.phase.valid){const auto work=cpu();
            const double start=d.first_seconds+static_cast<double>(settle+edge_inner)/fs;
            const double end=d.first_seconds+static_cast<double>(settle+aperture-edge_inner)/fs;
            x.causal_predictions={chain.joint.predict(2400,d.shift_hz,d.first_seconds),chain.joint.predict(2400,d.shift_hz,start),
                chain.joint.predict(2400,d.shift_hz,x.phase.date),chain.joint.predict(2400,d.shift_hz,end),
                chain.joint.predict(2400,d.shift_hz,d.first_seconds+.4)};
            x.latest_observation_end=std::isfinite(chain.latest_joint_input)?chain.latest_joint_input+static_cast<double>(aperture+1)/(2*fs):0;
            excluded_prediction_cpu=cpu()-work;smoother_cpu+=excluded_prediction_cpu;
        }
        items.push_back(std::move(x));
        highwater=std::max(highwater,buffer.queued_samples());buffer_cpu+=cpu()-start-excluded_prediction_cpu;
    }
    void release(std::uint64_t now,std::uint64_t frontier,double input_frontier,bool eof,const ContinuationSource& source){
        for(;;){h::HelperDelayedBuffer::ReleasedBlock out;bool contiguous=!items.empty();
            if(contiguous){const auto target=static_cast<unsigned>(std::llround(items.front().d.first_seconds/.4));
                const auto n=std::min(100U,static_cast<unsigned>(std::floor(input_frontier/.4+1e-7)));
                for(unsigned i=target;i<n;++i)if(helper_missing[i])contiguous=false;}
            const auto before=cpu();auto status=buffer.pop(now,frontier,raw,out,contiguous);buffer_cpu+=cpu()-before;
            if(status==h::HelperDelayedBuffer::PopResult::empty||status==h::HelperDelayedBuffer::PopResult::not_ready)break;
            require(status==h::HelperDelayedBuffer::PopResult::released&&!items.empty(),"delayed release failed");
            auto item=std::move(items.front());items.pop_front();++released;
            const auto index=static_cast<unsigned>(std::llround(item.d.first_seconds/.4));
            if(out.capture_time_discontinuity)++clock_discontinuities;
            if(out.capture.first_sample!=static_cast<std::uint64_t>(index)*hp::dwell_samples||
               out.capture.capture_system_nanoseconds!=captures[index].capture_system_nanoseconds||
               out.release_monotonic_nanoseconds<out.eligible_monotonic_nanoseconds)++metadata_errors;
            const double first_age=static_cast<double>(out.release_monotonic_nanoseconds-out.capture.capture_monotonic_nanoseconds)*1e-9;
            const double last_age=first_age-static_cast<double>((out.capture.sample_count-1)*out.capture.sample_period_nanoseconds)*1e-9;
            min_first_age=std::min(min_first_age,first_age);max_first_age=std::max(max_first_age,first_age);
            min_last_age=std::min(min_last_age,last_age);max_last_age=std::max(max_last_age,last_age);
            if(!out.configured_helper_lookahead_available)++short_future;
            item.d.samples.assign(raw.begin(),raw.end());Phase p=item.phase;
            if(extended&&p.valid&&p.support_end==0){p.support_first=settle+edge_inner;p.support_end=settle+aperture-edge_inner;}
            const auto t0=cpu();
            const double start=item.d.first_seconds+static_cast<double>(p.support_end>p.support_first?p.support_first:settle+phase_margin)/fs;
            const double end=item.d.first_seconds+static_cast<double>(p.support_end>p.support_first?p.support_end:settle+aperture-phase_margin)/fs;
            const auto a=causal?item.causal_predictions[2]:chain.joint.predict(2400,item.d.shift_hz,p.date);
            const auto first=causal?item.causal_predictions[0]:chain.joint.predict(2400,item.d.shift_hz,item.d.first_seconds);
            const auto j0=causal?item.causal_predictions[1]:chain.joint.predict(2400,item.d.shift_hz,start);
            const auto j1=causal?item.causal_predictions[3]:chain.joint.predict(2400,item.d.shift_hz,end);
            const auto last=causal?item.causal_predictions[4]:chain.joint.predict(2400,item.d.shift_hz,item.d.first_seconds+.4);
            h::HelperDelayedSmootherInput si;
            si.fresh_phase_valid=p.valid;si.target_observation_qualified=item.authority&&!item.missing&&(causal||!chain.joint_suspended);
            si.capture_first_seconds=item.d.first_seconds;si.capture_end_seconds=item.d.first_seconds+.4;
            si.support_first_seconds=start;si.support_end_seconds=end;si.anchor_seconds=p.date;
            si.phase_first_seconds=item.d.first_seconds+static_cast<double>(settle)/fs;
            si.phase_end_seconds=item.d.first_seconds+static_cast<double>(settle+aperture)/fs;
            si.capture_frontier_seconds=input_frontier;
            si.latest_observation_end_seconds=causal?item.latest_observation_end:(std::isfinite(chain.latest_joint_input)?chain.latest_joint_input+static_cast<double>(aperture+1)/(2*fs):0);
            si.retained_reference_seconds=item.reference;si.support_first=j0;si.anchor=a;si.support_end=j1;
            si.end_of_stream=eof;
            const auto sm=smoother.assess(si);
            if(!sm.valid){const auto reason=static_cast<std::size_t>(sm.failure);if(reason<smoother_failures.size())++smoother_failures[reason];}
            const bool ok=sm.valid&&first.valid&&last.valid&&first.reference_preserved&&last.reference_preserved&&
                std::abs(first.reference_input_seconds-item.reference)<1e-9&&std::abs(last.reference_input_seconds-item.reference)<1e-9;
            Coeff c;
            if(ok){c={sm.audio_scale,sm.common_frequency_offset_hz};retained=c;retained_rf=item.d.shift_hz;have_retained=true;
                auto& map=tx_maps[index];map.valid=true;map.reference_sample=static_cast<long double>(sm.reference_seconds)*fs;
                map.anchor_sample=static_cast<long double>(p.date)*fs;map.source_at_anchor=sm.source_seconds_at_anchor;
                map.audio_scale=sm.audio_scale;map.audio_half_width_ppm=sm.audio_half_width_ppm;
                double max_rate=0;for(const auto& j:{first,a,last})max_rate=std::max(max_rate,
                    std::abs(j.audio_scale-sm.audio_scale)+1e-6*(3*j.audio_stochastic_sigma_ppm+j.audio_model_half_width_ppm));
                map.source_half_width_seconds=sm.source_half_width_seconds+max_rate*std::max(p.date-item.d.first_seconds,item.d.first_seconds+.4-p.date);
                auto& tx=out.capture.transmitter_time;const long double whole=std::floor(map.reference_sample);
                require(whole>=0&&whole<static_cast<long double>(UINT64_MAX),"invalid fractional Helper reference");
                tx.valid=true;tx.reference_sample=static_cast<std::uint64_t>(whole);tx.reference_fractional_sample=map.reference_sample-whole;
                tx.seconds_at_first_sample=map.source_at_anchor+(out.capture.first_sample-map.anchor_sample)*map.audio_scale/fs;
                tx.seconds_per_sample=map.audio_scale/fs;
                tx.uncertainty_seconds=map.source_half_width_seconds;
                p.transport_budget_valid=true;p.transport_rate=sm.transport_rate_radians_per_second;
                p.diffusion_q=chain.receiver.aided_config.phase_noise_radians_per_sqrt_second;
            }else{++smooth_rejected;if(have_retained){c=retained;c.offset*=item.d.shift_hz/retained_rf;}}
            smoother_cpu+=cpu()-t0;
            released_metadata[index]=out;
            const auto input=static_cast<std::uint64_t>(index)*hp::dwell_samples;
            if(!chain.stream.initialized&&ok){
                if(full){first_conditioned_index=input;std::copy_n(item.d.samples.begin(),edge_capture_capacity,first_conditioned_raw.begin());first_conditioned_raw_valid=true;}
                try{chain.stream.initialize(input,c,first.source_seconds,first.reference_input_seconds,source);}catch(const std::exception&){std::cerr<<"init failed method="<<chain.name<<" audio="<<c.audio<<" offset="<<c.offset<<" ref="<<first.reference_input_seconds<<" src="<<first.source_seconds<<"\n";throw;}}
            if(full&&index==99&&chain.stream.initialized){last_capture_index=input+hp::dwell_samples-edge_capture_capacity;
                std::copy_n(item.d.samples.end()-static_cast<std::ptrdiff_t>(edge_capture_capacity),edge_capture_capacity,last_capture_raw.begin());last_capture_raw_valid=true;}
            active_processing_release_ns=out.release_monotonic_nanoseconds;
            if(chain.stream.initialized){released_block_kind[index]=2;const auto cc=cpu();try{chain.stream.feed(item.d,c,p,ok,source,coordinate(a));}catch(const std::exception&){std::cerr<<"condition failure method="<<chain.name<<" index="<<index<<" audio="<<c.audio<<" offset="<<c.offset<<" ok="<<ok<<"\n";throw;}conditioning_cpu+=cpu()-cc;}
            else{
                // Test consumer receives the actual bytes + unchanged release
                // metadata, explicitly flagged unsupported; no data is dropped.
                released_block_kind[index]=1;const auto sink=cpu();for(const auto sample:item.d.samples){
                    const auto b=std::as_bytes(std::span<const S>(&sample,1));
                    for(const auto v:b){raw_sink_hash^=std::to_integer<unsigned char>(v);raw_sink_hash*=1099511628211ULL;}}
                if(full)startup_uncorrected.insert(startup_uncorrected.end(),item.d.samples.begin(),item.d.samples.end());
                raw_sink_cpu+=cpu()-sink;startup_raw_unsupported+=out.capture.sample_count;++startup_unsupported_blocks;}
        }
    }
    void drain_conditioner(){
        auto& stream=chain.stream;if(!stream.initialized)return;
        // Synthetic right-hand FIR padding is never qualified. The consumer
        // receives appended output with explicit false validity; no source
        // reference, phase or sample clock is reset.
        const auto start=cpu();double excluded=0;
        for(unsigned iteration=0;iteration<128&&!stream.noisy->finished();++iteration){
            std::array<S,256> x{},y{};std::array<long double,256> dates{},other{};
            const auto p=stream.noisy->process({},x,stream.noisy->next_input_sample(),true,dates);
            const auto twin_start=cpu();const auto q=stream.clean->process({},y,stream.clean->next_input_sample(),true,other);excluded+=cpu()-twin_start;
            require(p.consumed==q.consumed&&p.produced==q.produced&&p.output_first_sample==q.output_first_sample,"drain twin mismatch");
            if(!p.produced&&!stream.noisy->finished())throw std::runtime_error("conditioner drain stalled");
            for(std::size_t i=0;i<p.produced;++i){require(p.output_first_sample+i==stream.trace.first+stream.trace.noisy.size(),"drain output discontinuity");
                if(stream.output_metadata_deliver)stream.output_metadata_deliver(dates[i],false);
                stream.trace.noisy.push_back(x[i]);stream.trace.input_dates.push_back(dates[i]);stream.trace.supported.push_back(0);
                ++stream.output_samples;++drained_samples;}
            const auto eval=cpu();stream.trace.clean.insert(stream.trace.clean.end(),y.begin(),y.begin()+static_cast<std::ptrdiff_t>(q.produced));
            stream.trace.physically_disturbed.insert(stream.trace.physically_disturbed.end(),q.produced,2);excluded+=cpu()-eval;
        }
        conditioner_finished=stream.noisy->finished()&&stream.clean->finished();require(conditioner_finished,"conditioner drain work limit");
        drain_cpu=cpu()-start-excluded;drain_twin_cpu=excluded;stream.cpu_seconds+=drain_cpu;stream.evaluator_stream_cpu_seconds+=excluded;
    }
};

// A deferred standalone consumer over the captured stream. Capture and
// processing-release metadata are kept distinct from the final time when the
// iterator actually delivers the sample. The latter is a drain-time test
// consumer, not a claim that this particular iterator streams at 2 s latency.
// It retains actual raw samples at the conditioner start/end, skips all output
// produced by the zero-padded EOF drain, and reports every such raw sample as
// uncorrected. Resampled and raw samples have different grids: capture_input
// is the authoritative coordinate, not the event's ordinal.
struct UnifiedConsumerEvent {
    S sample{};long double capture_input=0,capture_system_ns=0,capture_uncertainty_ns=0,capture_monotonic_ns=0;
    long double transmitter_seconds=0,transmitter_reference_sample=0;double transmitter_uncertainty_seconds=0;
    std::uint64_t capture_epoch=0,processing_release_ns=0,delivery_ns=0,slot=0;
    std::size_t source_index=0;
    enum class Source : std::uint8_t {startup_raw,start_edge_raw,conditioned,end_edge_raw} source=Source::startup_raw;
    std::uint8_t phase_kind=0;bool timestamp_valid=false,transmitter_time_valid=false,schedule_valid=false,boundary_ambiguous=true,supported=false;
};
struct UnifiedConsumerStats {
    std::uint64_t count=0,startup_raw=0,start_edge_raw=0,conditioned=0,end_edge_raw=0;
    std::uint64_t timestamp_unavailable=0,transmitter_time_unavailable=0,unsupported=0,skipped_drain=0,skipped_filter_edges=0;
    std::uint64_t hash=1469598103934665603ULL,startup_hash=1469598103934665603ULL;
    long double last_coordinate=-1,max_step=0;double cpu_seconds=0;
};
bool unified_capture_timestamp(const std::array<h::HelperDelayedBuffer::CaptureBlock,100>& captures,
    long double input,long double& system_ns,long double& uncertainty_ns,bool next_discontinuity){
    if(!std::isfinite(input)||input<0||input>=100.L*hp::dwell_samples)return false;
    const auto block=static_cast<std::size_t>(input/hp::dwell_samples);const auto& cap=captures[block];
    if(h::HelperDelayedBuffer::capture_system_time_at(cap,input,system_ns,uncertainty_ns))return true;
    if(block+1>=captures.size()||!cap.sample_count||next_discontinuity)return false;
    const auto& next=captures[block+1];const auto tail=input-static_cast<long double>(cap.first_sample+cap.sample_count-1);
    if(!(tail>0&&tail<1)||cap.capture_clock_epoch!=next.capture_clock_epoch||next.first_sample!=cap.first_sample+cap.sample_count)return false;
    long double a=0,au=0,b=0,bu=0;
    if(!h::HelperDelayedBuffer::capture_system_time_at(cap,cap.first_sample+cap.sample_count-1,a,au)||
       !h::HelperDelayedBuffer::capture_system_time_at(next,next.first_sample,b,bu)||
       std::abs((b-a)-cap.sample_period_nanoseconds)>au+bu+cap.sample_period_uncertainty_nanoseconds)return false;
    system_ns=a+tail*(b-a);uncertainty_ns=std::max(au,bu)+1;return true;
}
template<class Consumer>
UnifiedConsumerStats deliver_unified(const DelayedRun& r,std::uint64_t delivery_ns,Consumer&& consume){
    require(r.full,"unified consumer requires the full-dwell path");const auto begin=cpu();UnifiedConsumerStats stats;
    const auto deliver=[&](UnifiedConsumerEvent e){
        require(std::isfinite(e.capture_input)&&e.capture_input>=0&&e.capture_input<100.L*hp::dwell_samples,
            "consumer tried to deliver a nonexistent capture coordinate");
        require(e.capture_input>stats.last_coordinate,"unified consumer lost capture ordering");
        if(stats.count)stats.max_step=std::max(stats.max_step,e.capture_input-stats.last_coordinate);
        stats.last_coordinate=e.capture_input;
        const auto block=static_cast<std::size_t>(e.capture_input/hp::dwell_samples);const auto& cap=r.captures[block];
        e.capture_epoch=cap.capture_clock_epoch;
        e.capture_monotonic_ns=cap.capture_monotonic_nanoseconds+(e.capture_input-cap.first_sample)*cap.sample_period_nanoseconds;
        const bool next_discontinuity=block+1<r.released_metadata.size()&&r.released_metadata[block+1].capture_time_discontinuity;
        e.timestamp_valid=unified_capture_timestamp(r.captures,e.capture_input,e.capture_system_ns,e.capture_uncertainty_ns,next_discontinuity);
        if(!e.timestamp_valid)++stats.timestamp_unavailable;
        const auto& tx=r.tx_maps[block];if(tx.valid){e.transmitter_time_valid=true;e.transmitter_reference_sample=tx.reference_sample;
            e.transmitter_seconds=tx.source_at_anchor+(e.capture_input-tx.anchor_sample)*tx.audio_scale/fs;
            e.transmitter_uncertainty_seconds=tx.source_half_width_seconds;}
        else ++stats.transmitter_time_unavailable;
        e.delivery_ns=delivery_ns;require(e.capture_monotonic_ns<=e.processing_release_ns&&e.processing_release_ns<=delivery_ns,
            "consumer delivery preceded capture or processing release");
        if(!e.supported)++stats.unsupported;
        const auto bytes=std::as_bytes(std::span<const S>(&e.sample,1));for(const auto b:bytes){stats.hash^=std::to_integer<unsigned char>(b);stats.hash*=1099511628211ULL;
            if(e.source==UnifiedConsumerEvent::Source::startup_raw){stats.startup_hash^=std::to_integer<unsigned char>(b);stats.startup_hash*=1099511628211ULL;}}
        ++stats.count;consume(e);
    };
    const auto raw=[&](S sample,std::uint64_t input,UnifiedConsumerEvent::Source kind){
        const auto block=static_cast<std::size_t>(input/hp::dwell_samples);UnifiedConsumerEvent e;e.sample=sample;e.capture_input=input;e.source=kind;
        e.processing_release_ns=r.released_metadata[block].release_monotonic_nanoseconds;e.source_index=static_cast<std::size_t>(input);
        e.schedule_valid=r.schedule_at(e.capture_input,e.slot);e.boundary_ambiguous=!e.schedule_valid;
        deliver(e);
    };
    for(std::size_t i=0;i<r.startup_uncorrected.size();++i){raw(r.startup_uncorrected[i],i,UnifiedConsumerEvent::Source::startup_raw);++stats.startup_raw;}
    require(stats.startup_hash==r.raw_sink_hash,"startup raw payload differs from originally released samples");
    if(!r.chain.stream.initialized){stats.cpu_seconds=cpu()-begin;return stats;}
    require(r.first_conditioned_raw_valid&&r.last_capture_raw_valid,"raw filter-boundary fallback unavailable");
    const auto halo=static_cast<std::size_t>(std::ceil(r.chain.stream.noisy->lookahead_seconds()*fs));
    require(halo>0&&halo<=r.edge_capture_capacity,"filter halo exceeds captured edge storage");
    const auto raw_end=static_cast<std::uint64_t>(100)*hp::dwell_samples;
    for(std::size_t i=0;i<halo;++i){raw(r.first_conditioned_raw[i],r.first_conditioned_index+i,UnifiedConsumerEvent::Source::start_edge_raw);++stats.start_edge_raw;}
    const auto& trace=r.chain.stream.trace;require(trace.noisy.size()==trace.input_dates.size()&&trace.noisy.size()>=r.drained_samples,
        "incomplete conditioned consumer trace");
    const auto valid_outputs=trace.noisy.size()-r.drained_samples;stats.skipped_drain=r.drained_samples;
    std::size_t segment=0,segment_end=r.output_schedule_count?r.output_schedule[0].count:0;
    for(std::size_t i=0;i<valid_outputs;++i){
        while(i>=segment_end&&segment+1<r.output_schedule_count)segment_end+=r.output_schedule[++segment].count;
        require(segment<r.output_schedule_count&&i<segment_end,"missing conditioned output metadata");
        const auto pos=trace.input_dates[i];
        if(pos<r.first_conditioned_index+halo||pos>=raw_end-halo){++stats.skipped_filter_edges;continue;}
        const auto& m=r.output_schedule[segment];UnifiedConsumerEvent e;e.sample=trace.noisy[i];e.capture_input=pos;e.source=UnifiedConsumerEvent::Source::conditioned;
        e.source_index=i;e.processing_release_ns=m.processing_release_ns;e.phase_kind=m.phase_kind;e.schedule_valid=m.valid;e.slot=m.slot;
        e.boundary_ambiguous=m.boundary_ambiguous;e.supported=m.supported;deliver(e);++stats.conditioned;
    }
    const auto tail_begin=raw_end-halo;
    require(tail_begin>=r.last_capture_index,"raw end fallback is too short");
    for(std::uint64_t i=tail_begin;i<raw_end;++i){raw(r.last_capture_raw[static_cast<std::size_t>(i-r.last_capture_index)],i,UnifiedConsumerEvent::Source::end_edge_raw);++stats.end_edge_raw;}
    stats.cpu_seconds=cpu()-begin;return stats;
}
struct EdgeStats{std::uint64_t admitted=0,supported_total=0,clean_withheld=0,disturbed_admitted=0,unknown_withheld=0;double square=0,worst=0;std::vector<double> errors;};
EdgeStats edge_stats(const Stream& stream,bool start_side,const Trace* common=nullptr){
    EdgeStats out;const auto& t=stream.trace;const double fixed=wrap(tau*(2400-hp::probe_frequency_hz)*stream.gauge);
    for(std::size_t i=0;i<t.input_dates.size();++i){const auto n=t.first+i;const auto pos=t.input_dates[i];
        if(pos<38*fs||pos>=40*fs)continue;const auto dwell=static_cast<std::size_t>(pos/hp::dwell_samples);
        const double local=static_cast<double>(pos-dwell*hp::dwell_samples)/fs;
        if(start_side?local>=.06:local<.34)continue;
        const auto classification=i<t.physically_disturbed.size()?t.physically_disturbed[i]:2;
        const bool disturbed=classification==1,unknown=classification==2;
        const bool support=t.support(n)&&(!common||common->support(n));
        if(!support){if(unknown)++out.unknown_withheld;else if(!disturbed)++out.clean_withheld;continue;}
        require(!unknown,"unknown physical classification admitted");
        ++out.supported_total;if(disturbed)++out.disturbed_admitted;const auto z=t.at(n,true);if(std::norm(z)<1e-12)continue;
        const double err=std::abs(wrap(std::arg(z)-tau*std::remainder(hp::probe_frequency_hz*n/fs,1.)-fixed));
        ++out.admitted;out.square+=err*err;out.worst=std::max(out.worst,err);out.errors.push_back(err);
    }return out;
}
}
#ifndef HELPER_DELAYED_STUDY_NO_MAIN
int main(int argc,char** argv){try{
    hp::Config cfg;cfg.path_loss_db=argc>1?std::stod(argv[1]):180;cfg.seed=argc>2?std::stoull(argv[2]):1710000001;cfg.expanded_schedule_key=true;
    const std::string mode=argc>3?argv[3]:"clean",probe=argc>4?argv[4]:"tone";const double delay=argc>5?std::stod(argv[5]):2.;
    if(mode=="drift"){cfg.tx_audio_drift_ppm_per_second=.04;cfg.relative_shift_drift_ppm_per_second=.00002;}
    if(mode=="negative-clock"){cfg.tx_audio_ppm=-37;cfg.rx_audio_ppm=13;}
    if(probe=="apsk")cfg.probe=hp::Probe::apsk64;else require(probe=="tone","unknown probe");
    if(argc>6){const std::string rf=argv[6];if(rf=="microwave"||rf=="microwave-eu"){cfg.shift_center_hz=5.8e9;cfg.relative_shift_ppm=.00008;cfg.phase_noise_degrees_per_sqrt_second=.005;cfg.microwave_eu20=rf=="microwave-eu";}else require(rf=="hf","unknown RF");}
    // v8 reserves a separate reproducible thermal-noise key namespace. The
    // schedule seed, random hop phase and oscillator phase noise are unchanged.
    for(int i=7;i<argc;++i)if(std::string_view(argv[i])=="full")cfg.noise_seed=cfg.seed+10'000'000ULL;
    if(mode=="schedule-outside")cfg.arrival_offset_seconds=.0098;
    const auto imp=(mode=="retune-erasure"||mode=="retune-erasure-45"||mode=="clock-adjust"||mode=="schedule-outside"||mode=="transient-end-phase")?ht::Impairments{}:impairment_named(mode);const auto setup=cpu();ContinuationSource source(cfg,imp,mode=="frequency-pulse",mode=="scaled-step",36.8,
        mode=="gap-evolution"?.04:(mode=="gap-evolution-outside"?.6:0),mode=="gap-clock-cycle"?1./1850:0,mode=="second-step",mode=="step-invalid"?95:100,
        mode.starts_with("transient-")&&mode!="transient-end-phase"?std::string_view(mode).substr(10):std::string_view{});
    const double setup_cpu=cpu()-setup,max_rf=cfg.shift_center_hz>1e9?(cfg.microwave_eu20?5868.4e6:5846.8e6):31.225e6;
    const double prior_ppm=cfg.shift_center_hz>1e9?.0001:1,width=3*(max_rf*prior_ppm+2400*100)*1e-6;
    h::HelperMeasurementConfig mc{fs,550,2400,2400-width,2400+width,300,cfg.phase_noise_degrees_per_sqrt_second*std::numbers::pi/180/std::sqrt(source.base().rx_scale())};
    DelayedRun baseline("delayed-baseline",mc,cfg.shift_center_hz,delay,false),edge("delayed-edge20",mc,cfg.shift_center_hz,delay,true),edge30("delayed-edge30",mc,cfg.shift_center_hz,delay,true);
    edge30.edge_inner=360;
    bool want_causal=false,want_latequal=false,scalar=false,want_full=false;
    for(int i=7;i<argc;++i){const std::string flag=argv[i];if(flag=="causal")want_causal=true;
        else if(flag=="latequal")want_latequal=true;else if(flag=="scalar")scalar=true;else if(flag=="full")want_full=true;else throw std::invalid_argument("unknown delayed study flag");}
    std::unique_ptr<DelayedRun> causal,latequal,full;
    if(want_causal){causal=std::make_unique<DelayedRun>("causal-edge30",mc,cfg.shift_center_hz,delay,true);causal->edge_inner=360;causal->causal=true;}
    if(want_latequal){latequal=std::make_unique<DelayedRun>("latequal-edge30",mc,cfg.shift_center_hz,delay,true);latequal->edge_inner=360;latequal->latequal=true;}
    if(want_full){full=std::make_unique<DelayedRun>("full-dwell",mc,cfg.shift_center_hz,delay,true);full->edge_inner=360;full->full=true;full->chain.stream.best_effort_full_dwell=true;}
    std::vector<DelayedRun*> runs{&baseline,&edge,&edge30};if(causal)runs.push_back(causal.get());if(latequal)runs.push_back(latequal.get());
    if(full)runs.push_back(full.get());
    for(auto* run:runs){run->system_step=mode=="clock-adjust";run->capture_rate_scale=source.base().rx_scale();run->capture_fraction=cfg.fractional_input_sample;
        run->chain.stream.use_recursive_oscillator=!scalar;}
    std::unique_ptr<h::Generator> erasure_generator;
    if(mode=="retune-erasure"||mode=="retune-erasure-45"){
        h::Options options;options.power_w=cfg.helper_w;options.data_power_w=1;options.callsign=cfg.callsign;
        erasure_generator=std::make_unique<h::Generator>(options,h::Plan{2400,.4,0,cfg.helper_w,950,550,cfg.hopping},cfg.epoch,true);
    }
    std::unique_ptr<helper_transient::Model> end_phase;
    if(mode=="transient-end-phase")end_phase=std::make_unique<helper_transient::Model>(cfg,helper_transient::Profile{99,.36,.04,1.,1.3,0.,0.,false});
    const double erasure_seconds=mode=="retune-erasure-45"?.045:.03;
    double generation=0,oracle_cpu=0,synthetic_metrology_cpu=0;Stream oracle;
    for(unsigned i=0;i<100;++i){const auto t=cpu();auto d=source.generate(i);
        if(erasure_generator)for(std::size_t j=0;j<static_cast<std::size_t>(std::llround(erasure_seconds*fs));++j){
            const double date=d.first_seconds+static_cast<double>(j)/fs;
            const C helper=erasure_generator->mix_analytic({},source.tx_time(date),fs,40.L,0.L)/(.4*h::pilot_amplitude_fraction);
            // Channel fixture only: remove physical Helper and probe while
            // retaining the generated thermal noise realization.
            d.samples[j]=S(C(d.samples[j])-helper*C(d.common_phasor[j])-C(d.clean_probe[j]));d.clean_probe[j]={};
        }
        if(end_phase)end_phase->apply(d,source.base(),i);
        generation+=cpu()-t;const bool missing=source.missing(i);
        // Evaluator-only continuous clean tone. Prior to 38 s the actual
        // waveform carries no probe; changing clean_probe affects only the
        // separate clean conditioner, never receiver samples or decisions.
        // Restrict this diagnostic to the clean tone case; it is not noisy
        // probe metrology and is not used to choose correction or support.
        if(want_full&&mode=="clean"&&probe=="tone"&&i<95){
            const auto sm=cpu();for(std::size_t j=0;j<d.clean_probe.size();++j){
                const double date=d.first_seconds+static_cast<double>(j)/fs;
                const C z=source.base().probe_amplitude()*std::polar(1.,tau*std::remainder(hp::probe_frequency_hz*source.tx_time(date),1.))*C(d.common_phasor[j]);
                d.clean_probe[j]=S(z);
            }synthetic_metrology_cpu+=cpu()-sm;
        }
        // Evaluator-only truth oracle, on 37.6–40 s of the same capture.
        if(i>=94&&!imp.noise_only){const auto o=cpu();const auto first=static_cast<std::uint64_t>(i)*hp::dwell_samples;
            const double date=d.first_seconds+static_cast<double>(settle)/fs+(aperture-1)/(2*fs);
            const Coeff truth{source.audio_scale_at(date),source.shift_error_hz(d.shift_hz,date)};
            const double gauge=36-source.tx_time(36);
            if(!oracle.initialized){try{oracle.initialize(first,truth,source.tx_time(d.first_seconds)+gauge,36,source);}catch(const std::exception&){std::cerr<<"oracle init failed audio="<<truth.audio<<" offset="<<truth.offset<<"\n";throw;}}
            const double sample=(date-d.first_seconds)*fs;const auto k=static_cast<std::size_t>(std::floor(sample));
            const double f=sample-static_cast<double>(k);
            const double common=std::arg(C(d.common_phasor[k]))+f*wrap(std::arg(C(d.common_phasor[k+1]))-std::arg(C(d.common_phasor[k])));
            const Phase phase{true,date,wrap(tau*std::remainder(2400*source.tx_time(date),1.)+common),0,0,0};
            oracle.feed(d,truth,phase,true,source);oracle_cpu+=cpu()-o;}
        for(auto* run:runs)run->observe(d,missing,i,source);
        const auto& cap=baseline.captures[i];const auto now=cap.capture_monotonic_nanoseconds+
            static_cast<std::uint64_t>(std::ceil((cap.sample_count-1)*cap.sample_period_nanoseconds));
        for(auto* run:runs)run->release(now,now,(i+1)*.4,i==99,source);
        if(i==89)for(auto* run:runs){run->cpu36_total=run->chain.receiver.total_cpu()+run->chain.extra_cpu+run->chain.stream.cpu_seconds+run->buffer_cpu+run->smoother_cpu+run->raw_sink_cpu;
            run->cpu36_conditioned_seconds=static_cast<double>(run->chain.stream.input_samples)/fs;}
    }
    const auto& final_capture=baseline.captures[99];const std::uint64_t frontier=final_capture.capture_monotonic_nanoseconds+
        static_cast<std::uint64_t>(std::ceil((final_capture.sample_count-1)*final_capture.sample_period_nanoseconds));std::uint64_t now=frontier;
    for(unsigned i=0;i<12&&std::any_of(runs.begin(),runs.end(),[](const auto* r){return r->buffer.queued_blocks()!=0;});++i){
        now+=400'000'000ULL;for(auto* run:runs)run->release(now,frontier,40,true,source);}

    for(auto* run:runs){require(!run->buffer.queued_blocks(),"buffer failed to drain");run->drain_conditioner();}
    if(full){require(full->startup_uncorrected.size()==full->startup_raw_unsupported,"startup consumer lost captured samples");
        require(full->startup_raw_unsupported+full->chain.stream.input_samples==100ULL*hp::dwell_samples,"capture inputs not fully released");
        std::uint64_t raw_end=0;bool entered_conditioned=false;std::uint64_t previous_release=0,previous_capture=0;
        for(unsigned i=0;i<100;++i){
            const auto& release=full->released_metadata[i];const auto& b=release.capture;
            require(b.first_sample==static_cast<std::uint64_t>(i)*hp::dwell_samples,"consumer capture order changed");
            require((!i||b.capture_monotonic_nanoseconds>previous_capture)&&release.release_monotonic_nanoseconds>=previous_release,
                "consumer capture/release clock order changed");
            if(i&&b.capture_clock_epoch==full->released_metadata[i-1].capture.capture_clock_epoch)
                require(b.capture_system_nanoseconds>full->released_metadata[i-1].capture.capture_system_nanoseconds,
                    "system capture timestamp regressed without epoch change");
            previous_capture=b.capture_monotonic_nanoseconds;previous_release=release.release_monotonic_nanoseconds;
            if(full->released_block_kind[i]==1){require(!entered_conditioned&&b.first_sample==raw_end,"raw consumer block order changed");raw_end+=b.sample_count;}
            else{require(full->released_block_kind[i]==2,"missing consumer block");entered_conditioned=true;}
        }
        require(raw_end==full->startup_uncorrected.size(),"raw consumer index/payload mismatch");
        if(!full->chain.stream.trace.input_dates.empty()){const auto first=full->chain.stream.trace.input_dates.front();
            require(first>=static_cast<long double>(raw_end)-1&&first<=static_cast<long double>(raw_end)+1,
                "raw-to-conditioned transition has an unreported source-date discontinuity");}
    }
    if(full){
        // Falsify interpolation for an unannounced wall-clock step, a
        // declared epoch change, and a buffer-reported discontinuity.
        {auto capture=full->captures;const long double cross=hp::dwell_samples-.5L;long double stamp=0,uncertainty=0;
            require(unified_capture_timestamp(capture,cross,stamp,uncertainty,false),"continuous fractional capture timestamp unavailable");
            capture[1].capture_system_nanoseconds+=1'000'000;
            require(!unified_capture_timestamp(capture,cross,stamp,uncertainty,false),"unannounced system-clock step was interpolated");
            capture=full->captures;++capture[1].capture_clock_epoch;
            require(!unified_capture_timestamp(capture,cross,stamp,uncertainty,false),"cross-epoch capture timestamp was interpolated");
            require(!unified_capture_timestamp(full->captures,cross,stamp,uncertainty,true),"reported capture discontinuity was interpolated");}
        std::uint64_t event_count=0;long double last=-1;std::optional<long double> transmitter_reference;
        const auto us=deliver_unified(*full,now,[&](const UnifiedConsumerEvent& e){
            require(e.capture_input>last,"consumer callback changed capture ordering");last=e.capture_input;++event_count;
            if(e.source==UnifiedConsumerEvent::Source::startup_raw)
                require(e.source_index<full->startup_uncorrected.size()&&e.sample==full->startup_uncorrected[e.source_index],"startup content delivery mismatch");
            else if(e.source==UnifiedConsumerEvent::Source::start_edge_raw)
                require(e.sample==full->first_conditioned_raw[e.source_index-full->first_conditioned_index],"start-edge content delivery mismatch");
            else if(e.source==UnifiedConsumerEvent::Source::end_edge_raw)
                require(e.sample==full->last_capture_raw[e.source_index-full->last_capture_index],"end-edge content delivery mismatch");
            else{
                require(e.source_index<full->chain.stream.trace.noisy.size()&&e.sample==full->chain.stream.trace.noisy[e.source_index]&&
                    e.capture_input==full->chain.stream.trace.input_dates[e.source_index],"conditioned content delivery mismatch");
                if(e.supported)require(e.phase_kind==1&&!e.boundary_ambiguous&&e.timestamp_valid,"supported consumer metadata inconsistent");
            }
            if(e.source!=UnifiedConsumerEvent::Source::conditioned)require(!e.supported&&e.phase_kind==0,"raw sample was falsely phase-qualified");
            if(e.transmitter_time_valid){
                require(std::isfinite(e.transmitter_seconds)&&std::isfinite(e.transmitter_uncertainty_seconds)&&e.transmitter_uncertainty_seconds>=0,
                    "invalid conditional transmitter-time mapping");
                if(!transmitter_reference)transmitter_reference=e.transmitter_reference_sample;
                else require(*transmitter_reference==e.transmitter_reference_sample,"consumer transmitter-time reference changed");
            }else require(!e.supported,"supported sample lacks transmitter-time mapping");
        });
        require(event_count==us.count&&us.count>0&&us.max_step<3,"unified consumer callback count or source-coordinate spacing smoke failed");
        std::cout<<std::setprecision(10)<<"helper_unified_consumer method=full-dwell mode="<<mode<<" seed="<<cfg.seed
            <<" delivery=deferred_until_drain resampled_grid_not_one_to_one=1 no_probe_feedback=1"
            <<" delivered="<<us.count<<" startup_raw="<<us.startup_raw<<" start_edge_raw="<<us.start_edge_raw
            <<" conditioned="<<us.conditioned<<" end_edge_raw="<<us.end_edge_raw<<" unsupported="<<us.unsupported
            <<" skipped_zero_padded_drain="<<us.skipped_drain<<" skipped_filter_edge_outputs="<<us.skipped_filter_edges
            <<" timestamp_unavailable="<<us.timestamp_unavailable<<" transmitter_time_unavailable="<<us.transmitter_time_unavailable
            <<" max_capture_step_samples="<<static_cast<double>(us.max_step)<<" spacing_is_smoke_not_completeness_proof=1"
            <<" last_capture_coordinate="<<static_cast<double>(us.last_coordinate)<<" delivered_payload_hash="<<us.hash
            <<" original_startup_hash_match=1 timestamp_step_and_epoch_controls=passed consumer_delivery_monotonic_ns="<<now
            <<" raw_edge_storage_bytes="<<sizeof(full->first_conditioned_raw)+sizeof(full->last_capture_raw)
            <<" consumer_cpu_seconds="<<us.cpu_seconds<<" consumer_cpu_includes_test_validation=1 cpu36_excludes_deferred_consumer=1"
            <<" receiver_plus_instrumented_consumer_cpu40_seconds="<<(full->chain.receiver.total_cpu()+full->chain.extra_cpu+
                full->chain.stream.cpu_seconds+full->buffer_cpu+full->smoother_cpu+full->raw_sink_cpu+us.cpu_seconds)<<"\n";
    }
    const auto oracle_started=cpu();
    const auto oracle_tone=cfg.probe==hp::Probe::tone?tone_metrics(oracle.trace,oracle.gauge):Metrics{};
    const auto oracle_evm=cfg.probe==hp::Probe::apsk64?evm_metrics(oracle.trace,source.base(),oracle.gauge):Evm{};
    std::cout<<"helper_delayed_oracle scope=evaluator_only_true_clock_frequency_phase no_receiver_feedback=1 conditioning_signal_seconds="<<oracle.input_samples/fs
      <<" clean_tone_rms_hz="<<oracle_tone.frequency_rms<<" clean_apsk_evm="<<oracle_evm.clean
      <<" oracle_cpu_seconds="<<oracle_cpu<<" oracle_metrology_cpu_seconds="<<cpu()-oracle_started<<"\n";
    for(auto* x:runs){
        const auto total_metrology_started=cpu();
        if(erasure_generator&&x->chain.stream.initialized){auto& t=x->chain.stream.trace;
            const double halo=x->chain.stream.noisy->lookahead_seconds();
            for(std::size_t i=0;i<t.input_dates.size();++i){const auto pos=t.input_dates[i];const auto slot=static_cast<std::size_t>(pos/hp::dwell_samples);
                const double within=static_cast<double>(pos-slot*hp::dwell_samples)/fs;
                if(t.physically_disturbed[i]!=2&&(within<erasure_seconds+halo||within>.4-halo))t.physically_disturbed[i]=1;
            }
        }
        if(end_phase&&x->chain.stream.initialized){auto& t=x->chain.stream.trace;const double halo=x->chain.stream.noisy->lookahead_seconds();
            for(std::size_t j=0;j<t.input_dates.size();++j){const auto pos=t.input_dates[j];
                if(t.physically_disturbed[j]!=2&&end_phase->disturbed_during(99,static_cast<double>(pos/fs)-39.6-halo,static_cast<double>(pos/fs)-39.6+halo))t.physically_disturbed[j]=1;}}
        for(unsigned i=0;i<100;++i)require(x->released_block_kind[i]!=0&&x->released_metadata[i].capture.first_sample==static_cast<std::uint64_t>(i)*hp::dwell_samples,"missing released block metadata");
        const auto mt=cpu();const auto tone=cfg.probe==hp::Probe::tone?tone_metrics(x->chain.stream.trace,x->chain.stream.gauge):Metrics{};
        const auto evm=cfg.probe==hp::Probe::apsk64?evm_metrics(x->chain.stream.trace,source.base(),x->chain.stream.gauge):Evm{};const auto met=cpu()-mt;
        print_chain(x->chain,source,mode,generation,setup_cpu,met,tone,evm,true);
        const auto& tr=x->chain.stream.trace;require(tr.input_dates.size()==tr.supported.size(),"missing actual source coordinates");
        double full_metrology_cpu=0;
        if(want_full&&probe=="tone"&&x->chain.stream.initialized){
            const auto full_start=cpu();
            for(unsigned scope=0;scope<(mode=="clean"?2U:1U);++scope){
                helper_complete_stream_metrics::Options opt;opt.fixed_gauge_seconds=x->chain.stream.gauge;
                opt.other_fixed_gauge_seconds=baseline.chain.stream.gauge;
                if(scope==1){opt.capture_begin_seconds=x->chain.stream.first_input_seconds;opt.capture_end_seconds=40;}
                const auto all=helper_complete_stream_metrics::evaluate(tr,opt,&baseline.chain.stream.trace);
                constexpr std::array<const char*,4> regions{"beginning","interior","end","boundary"};
                constexpr std::array<const char*,4> selections{"all","supported","uncertain","common"};
                for(std::size_t r=0;r<regions.size();++r)for(std::size_t s=0;s<selections.size();++s){
                    const auto& z=all.region[r][s];
                    std::cout<<std::setprecision(10)<<"helper_complete_stream method="<<x->chain.name<<" mode="<<mode<<" seed="<<cfg.seed
                        <<" noise_seed="<<cfg.noise_seed.value_or(cfg.seed)
                        <<" scope="<<(scope?"continuous_synthetic_clean_twin":"heldout_actual_tone")
                        <<" noisy_phase_metric="<<(scope?"not_applicable":"unfiltered_helper_probe_noise_composite")
                        <<" receiver_feedback=0 region_geometry=nominal_capture_schedule region="<<regions[r]<<" selection="<<selections[s]
                        <<" passed="<<z.passed<<" clean="<<z.physically_clean<<" disturbed="<<z.physically_disturbed
                        <<" physically_missing="<<z.physically_missing<<" physical_unknown="<<z.physically_unknown
                        <<" phase_samples="<<z.phase_samples<<" clean_phase_rms_deg="<<z.phase_rms_deg
                        <<" clean_phase_p95_deg="<<z.phase_p95_deg<<" clean_phase_p99_deg="<<z.phase_p99_deg
                        <<" clean_phase_worst_deg="<<z.phase_worst_deg<<" noisy_phase_rms_deg="<<(scope?0:z.noisy_phase_rms_deg)
                        <<" frequency_dwells="<<z.frequency_dwells<<" clean_frequency_rms_hz="<<z.frequency_rms_hz
                        <<" clean_frequency_worst_hz="<<z.frequency_worst_hz<<" frequency_lag_unaliased_abs_hz_lt=93.75"
                        <<" invalid_capture_coordinates="<<all.invalid_capture_coordinates<<" common_map_compared="<<all.common_map_compared
                        <<" common_map_mismatch="<<all.common_map_mismatch<<" common_gauge_mismatch="<<all.common_gauge_mismatch<<"\n";
                }
            }
            full_metrology_cpu=cpu()-full_start;
        }
        if(want_full&&probe=="apsk"&&x->chain.stream.initialized){
            // Metrology only: score the actual held-out 38--40 s waveform
            // without choosing timing, phase, alignment or support from it.
            // In particular, `all` includes uncertain output; only samples
            // with missing/unknown filter footprints are excluded.
            const auto full_start=cpu();
            helper_complete_apsk_metrics::Options opt;
            opt.fixed_gauge_seconds=x->chain.stream.gauge;
            opt.other_fixed_gauge_seconds=baseline.chain.stream.gauge;
            const auto pulse=[](double t){return datapump::fast::root_raised_cosine(t,.2);};
            const auto symbol=[&](unsigned k){return source.base().symbol(k);};
            const auto all=helper_complete_apsk_metrics::evaluate(
                tr,opt,source.base().probe_amplitude(),pulse,symbol,&baseline.chain.stream.trace);
            constexpr std::array<const char*,5> regions{"beginning","interior","end","boundary","all"};
            constexpr std::array<const char*,4> selections{"all","supported","uncertain","common"};
            for(std::size_t r=0;r<regions.size();++r)for(std::size_t s=0;s<selections.size();++s){
                const auto& z=all.region[r][s];
                std::cout<<std::setprecision(10)<<"helper_complete_apsk method="<<x->chain.name<<" mode="<<mode<<" seed="<<cfg.seed
                    <<" noise_seed="<<cfg.noise_seed.value_or(cfg.seed)
                    <<" scope=heldout_actual_apsk receiver_feedback=0 region_geometry=nominal_capture_schedule"
                    <<" noisy_evm_metric=matched_filter_helper_probe_noise_composite"
                    <<" region="<<regions[r]<<" selection="<<selections[s]
                    <<" considered_symbols="<<all.considered<<" unavailable_footprint_symbols="<<all.footprint_unavailable
                    <<" symbols="<<z.symbols<<" clean_twin_evm="<<z.clean_evm<<" noisy_composite_evm="<<z.noisy_evm
                    <<" footprint_crosses_nominal_hop="<<z.footprint_crosses_nominal_hop
                    <<" footprint_boundary_ambiguous="<<z.footprint_boundary_ambiguous
                    <<" common_date_mismatch="<<all.common_date_mismatch<<" common_gauge_mismatch="<<all.common_gauge_mismatch<<"\n";
            }
            full_metrology_cpu+=cpu()-full_start;
        }
        std::uint64_t near=0,near_supported=0,interior=0,interior_supported=0,timestamp_fail=0,timestamp_unavailable=0,schedule_fail=0,schedule_supported_mismatch=0,schedule_ambiguous=0,schedule_valid=0,tx_unavailable=0,tx_reference_changes=0,schedule_segment_failures=0;
        std::size_t segment_offset=0;std::array<std::uint64_t,3> phase_provenance{};std::uint64_t boundary_ambiguous_outputs=0;
        for(std::size_t k=0;k<x->output_schedule_count;++k){const auto& seg=x->output_schedule[k];
            if(!seg.count||segment_offset+seg.count>tr.input_dates.size()){++schedule_segment_failures;break;}
            if(seg.first!=tr.input_dates[segment_offset]||seg.last!=tr.input_dates[segment_offset+seg.count-1])++schedule_segment_failures;
            for(std::size_t i=segment_offset;i<segment_offset+seg.count;++i){std::uint64_t slot=0;const bool valid=x->schedule_at(tr.input_dates[i],slot);
                const auto [phase_kind,ambiguous]=x->provenance(tr.input_dates[i]);
                if(valid!=seg.valid||(valid&&slot!=seg.slot)||static_cast<bool>(tr.supported[i])!=seg.supported||
                   phase_kind!=seg.phase_kind||ambiguous!=seg.boundary_ambiguous)++schedule_segment_failures;}
            if(seg.phase_kind<phase_provenance.size())phase_provenance[seg.phase_kind]+=seg.count;
            else ++schedule_segment_failures;
            if(seg.boundary_ambiguous)boundary_ambiguous_outputs+=seg.count;
            segment_offset+=static_cast<std::size_t>(seg.count);
        }
        if(segment_offset!=tr.input_dates.size())++schedule_segment_failures;
        std::optional<long double> tx_reference;double tx_uncertainty_max=0;long double previous=-1,max_ns=0;
        for(std::size_t i=0;i<tr.input_dates.size();++i){const auto pos=tr.input_dates[i];if(pos<0||pos>=100.L*hp::dwell_samples){if(tr.supported[i])++timestamp_fail;else ++timestamp_unavailable;continue;}
            const auto slot=static_cast<std::size_t>(pos/hp::dwell_samples);long double ts=0,unc=0;
            if(!h::HelperDelayedBuffer::capture_system_time_at(x->captures[slot],pos,ts,unc)){
                const auto& b=x->captures[slot];const long double tail=pos-static_cast<long double>(b.first_sample+b.sample_count-1);
                if(slot+1<100&&tail>0&&tail<1&&b.capture_clock_epoch==x->captures[slot+1].capture_clock_epoch){
                    long double last=0,last_unc=0,next=0,next_unc=0;
                    if(h::HelperDelayedBuffer::capture_system_time_at(b,b.first_sample+b.sample_count-1,last,last_unc)&&
                       h::HelperDelayedBuffer::capture_system_time_at(x->captures[slot+1],x->captures[slot+1].first_sample,next,next_unc))
                        {ts=last+tail*(next-last);unc=std::max(last_unc,next_unc)+1;}
                    else ++timestamp_fail;
                }else {++timestamp_unavailable;continue;}
            }
            const long double expected=1'700'000'000'000'000'000.L+(pos+cfg.fractional_input_sample)*1e9L/(fs*source.base().rx_scale())-(x->system_step&&slot>=50?2'000'000'000.L:0);
            max_ns=std::max(max_ns,std::abs(ts-expected));if(std::abs(ts-expected)>unc||pos<previous)++timestamp_fail;previous=pos;
            // Output schedule is only a nominal receiver-input slot hypothesis.
            // Compare it to the fixture's physical TX schedule for metrology;
            // this truth never affects capture, support or correction.
            const double true_tx=source.tx_time(static_cast<double>(pos/fs));
            const long long true_slot=static_cast<long long>(std::floor(true_tx/.4));
            std::uint64_t receiver_slot=0;
            if(!x->schedule_at(pos,receiver_slot))++schedule_ambiguous;
            else{++schedule_valid;if(static_cast<long long>(receiver_slot)!=true_slot){++schedule_fail;if(tr.supported[i])++schedule_supported_mismatch;}}
            const auto& map=x->tx_maps[slot];
            if(!map.valid){if(tr.supported[i])++tx_unavailable;}
            else{const auto& released=x->released_metadata[slot];const auto& txmeta=released.capture.transmitter_time;
                const long double metadata_reference=static_cast<long double>(txmeta.reference_sample)+txmeta.reference_fractional_sample;
                if(!txmeta.valid||metadata_reference!=map.reference_sample)++tx_unavailable;
                if(!tx_reference)tx_reference=map.reference_sample;else if(*tx_reference!=map.reference_sample)++tx_reference_changes;
                const long double tx=map.source_at_anchor+(pos-map.anchor_sample)*map.audio_scale/fs;
                const double u=map.source_half_width_seconds;
                if(!std::isfinite(tx)||!std::isfinite(u)||u<0)++tx_unavailable;else tx_uncertainty_max=std::max(tx_uncertainty_max,u);}
            const double within=static_cast<double>(pos-slot*hp::dwell_samples)/fs;const bool boundary=within<.06||within>=.34;
            if(boundary){++near;if(tr.supported[i])++near_supported;}else{++interior;if(tr.supported[i])++interior_supported;}
        }
        require(!timestamp_fail&&!x->metadata_errors&&!tx_unavailable&&!tx_reference_changes&&!schedule_segment_failures&&(!x->system_step||x->clock_discontinuities==1),"capture timestamp or schedule mismatch");
        const auto early=cfg.probe==hp::Probe::tone?edge_stats(x->chain.stream,true):EdgeStats{};
        const auto late=cfg.probe==hp::Probe::tone?edge_stats(x->chain.stream,false):EdgeStats{};
        const auto deg=180/std::numbers::pi;
        std::cout<<std::setprecision(10)<<"helper_delayed_study method="<<x->chain.name<<" mode="<<mode<<" seed="<<cfg.seed<<" loss_db="<<cfg.path_loss_db
          <<" noise_seed="<<cfg.noise_seed.value_or(cfg.seed)
          <<" probe="<<probe<<" oscillator="<<(scalar?"scalar":"recursive")<<" configured_delay_seconds="<<delay<<" block_seconds=.4 nominal_first_age_lower_bound_seconds="<<delay+.4
          <<" captured_seconds=40 lead_seconds=38 observation_seconds=36 erasure_seconds="<<(erasure_generator?erasure_seconds:0)
          <<" release_last_monotonic_seconds="<<static_cast<double>(now)*1e-9<<" helper_frontier_monotonic_seconds="<<static_cast<double>(frontier)*1e-9<<""
          <<" actual_first_age_min_seconds="<<x->min_first_age<<" actual_first_age_max_seconds="<<x->max_first_age
          <<" actual_last_age_min_seconds="<<x->min_last_age<<" actual_last_age_max_seconds="<<x->max_last_age
          <<" conditioner_finished="<<x->conditioner_finished<<" drained_unsupported_samples="<<x->drained_samples
          <<" conditioner_drain_cpu_seconds="<<x->drain_cpu<<" evaluator_twin_drain_cpu_seconds="<<x->drain_twin_cpu
          <<" blocks_released="<<x->released<<" short_future_blocks="<<x->short_future<<" metadata_errors="<<x->metadata_errors
          <<" timestamp_unavailable="<<timestamp_unavailable<<" capture_clock_discontinuities="<<x->clock_discontinuities
          <<" timestamp_failures="<<timestamp_fail<<" schedule_valid="<<schedule_valid<<" schedule_ambiguous="<<schedule_ambiguous
          <<" schedule_nominal_mismatches="<<schedule_fail<<" schedule_supported_mismatches="<<schedule_supported_mismatch<<" timestamp_max_ns="<<static_cast<double>(max_ns)
          <<" timestamp_mapping=capture_sample_clock_not_release schedule_mapping=monotonic_epoch_1s_plusminus_8ms_conditional tx_time=helper_relative_ambiguous"
          <<" tx_reference_sample="<<(tx_reference?static_cast<double>(*tx_reference):0)<<" tx_reference_changes="<<tx_reference_changes
          <<" tx_unavailable_supported="<<tx_unavailable<<" tx_conditional_uncertainty_max_seconds="<<tx_uncertainty_max
          <<" capture_sample_period_uncertainty_ppm=.1"
          <<" buffer_capacity_bytes="<<x->buffer.allocated_bytes()<<" buffer_highwater_samples="<<x->highwater
          <<" raw_scratch_bytes="<<x->raw.capacity()*sizeof(S)<<" plan_queue_max_bytes="<<(x->highwater/hp::dwell_samples)*sizeof(DelayedRun::Item)
          <<" static_metadata_bytes="<<sizeof(x->captures)+sizeof(x->tx_maps)+sizeof(x->released_metadata)+sizeof(x->released_block_kind)+sizeof(x->helper_missing)+sizeof(x->output_schedule)
          <<" delivered_schedule_segments="<<x->output_schedule_count<<" schedule_segment_failures="<<schedule_segment_failures
          <<" conditioner_workspace_bytes="<<(x->chain.stream.noisy?x->chain.stream.noisy->workspace_bytes():0)
          <<" evaluator_twin_queue_max_bytes="<<x->highwater*sizeof(S)
          <<" heldout_early_supported_total="<<early.supported_total<<" heldout_early_admitted="<<early.admitted<<" heldout_early_rms_deg="<<(early.admitted?std::sqrt(early.square/early.admitted)*deg:0)
          <<" heldout_early_p95_deg="<<quantile(early.errors,.95)*deg<<" heldout_early_worst_deg="<<early.worst*deg
          <<" heldout_early_unknown_withheld="<<early.unknown_withheld<<" heldout_early_clean_withheld="<<early.clean_withheld<<" heldout_early_disturbed_admitted="<<early.disturbed_admitted
          <<" heldout_late_supported_total="<<late.supported_total<<" heldout_late_admitted="<<late.admitted<<" heldout_late_rms_deg="<<(late.admitted?std::sqrt(late.square/late.admitted)*deg:0)
          <<" heldout_late_p95_deg="<<quantile(late.errors,.95)*deg<<" heldout_late_worst_deg="<<late.worst*deg
          <<" heldout_late_unknown_withheld="<<late.unknown_withheld<<" heldout_late_clean_withheld="<<late.clean_withheld<<" heldout_late_disturbed_admitted="<<late.disturbed_admitted
          <<" output_phase_rejected="<<x->chain.stream.output_phase_rejected<<" output_timing_rejected="<<x->chain.stream.output_timing_rejected
          <<" output_metadata_rejected="<<x->chain.stream.output_metadata_rejected<<" output_guard_rejected="<<x->chain.stream.output_guard_rejected
          <<" boundary_samples="<<near<<" boundary_supported="<<near_supported<<" interior_samples="<<interior<<" interior_supported="<<interior_supported
          <<" startup_raw_unsupported_samples="<<x->startup_raw_unsupported<<" startup_raw_unsupported_blocks="<<x->startup_unsupported_blocks<<" unsupported_sink_hash="<<x->raw_sink_hash<<" unsupported_sink_cpu_seconds="<<x->raw_sink_cpu
          <<" startup_uncorrected_stored_samples="<<x->startup_uncorrected.size()<<" captured_input_accounted="<<(x->startup_raw_unsupported+x->chain.stream.input_samples)
          <<" startup_consumer_capacity_bytes="<<x->startup_uncorrected.capacity()*sizeof(S)
          <<" first_conditioned_capture_sample="<<(tr.input_dates.empty()?-1:static_cast<double>(tr.input_dates.front()))
          <<" raw_consumer_capture_end_exclusive="<<x->startup_raw_unsupported
          <<" conditioned_phase_provenance_available="<<x->full
          <<" conditioned_phase_unknown_samples="<<(x->full?phase_provenance[0]:0)<<" conditioned_fresh_phase_samples="<<(x->full?phase_provenance[1]:0)
          <<" conditioned_mixed_boundary_samples="<<(x->full?phase_provenance[2]:0)<<" conditioned_boundary_ambiguous_samples="<<(x->full?boundary_ambiguous_outputs:0)
          <<" input_gap_handling=not_exercised boundary_estimate=nominal_schedule boundary_uncertainty_ms=8"
          <<" synthetic_clean_twin_cpu_seconds="<<synthetic_metrology_cpu
          <<" receiver_cpu36_inclusive_seconds="<<x->cpu36_total<<" cpu36_conditioning_signal_seconds="<<x->cpu36_conditioned_seconds
          <<" receiver_cpu40_drain_inclusive_seconds="<<(x->chain.receiver.total_cpu()+x->chain.extra_cpu+x->chain.stream.cpu_seconds+x->buffer_cpu+x->smoother_cpu+x->raw_sink_cpu)
          <<" smoother_rejected="<<x->smooth_rejected
          <<" smoother_invalid_input="<<x->smoother_failures[2]<<" smoother_order="<<x->smoother_failures[3]
          <<" smoother_future_observation="<<x->smoother_failures[4]<<" smoother_missing_phase="<<x->smoother_failures[5]
          <<" smoother_unavailable_model="<<x->smoother_failures[6]<<" smoother_reference_mismatch="<<x->smoother_failures[7]
          <<" smoother_conflicting_history="<<x->smoother_failures[8]<<" buffer_cpu_seconds="<<x->buffer_cpu<<" smoothing_cpu_seconds="<<x->smoother_cpu
          <<" stream_instrumented_cpu_seconds="<<x->chain.stream.cpu_seconds<<" conditioning_signal_seconds="<<x->chain.stream.input_samples/fs
          <<" outer_stream_cpu_seconds_includes_evaluator="<<x->conditioning_cpu<<" simulation_generation_cpu_seconds="<<generation
          <<" metrology_cpu_seconds="<<met<<" full_stream_metrology_cpu_seconds="<<full_metrology_cpu<<" evaluator_input_dates_bytes="<<tr.input_dates.size()*sizeof(long double)<<"\n";
        std::cout<<"helper_delayed_metrology method="<<x->chain.name<<" total_including_reporting_cpu_seconds="<<cpu()-total_metrology_started<<"\n";
    }
    for(auto* candidate:runs)if(candidate!=&baseline){
    const auto& candidate_stream=candidate->chain.stream;
    const auto ta=cfg.probe==hp::Probe::tone?tone_metrics(baseline.chain.stream.trace,baseline.chain.stream.gauge,&candidate_stream.trace):Metrics{};
    const auto tb=cfg.probe==hp::Probe::tone?tone_metrics(candidate_stream.trace,candidate_stream.gauge,&baseline.chain.stream.trace):Metrics{};
    const auto ea=cfg.probe==hp::Probe::apsk64?evm_metrics(baseline.chain.stream.trace,source.base(),baseline.chain.stream.gauge,&candidate_stream.trace):Evm{};
    const auto eb=cfg.probe==hp::Probe::apsk64?evm_metrics(candidate_stream.trace,source.base(),candidate_stream.gauge,&baseline.chain.stream.trace):Evm{};
    const auto common_early=cfg.probe==hp::Probe::tone?edge_stats(candidate_stream,true,&baseline.chain.stream.trace):EdgeStats{};
    const auto common_late=cfg.probe==hp::Probe::tone?edge_stats(candidate_stream,false,&baseline.chain.stream.trace):EdgeStats{};
    std::cout<<"helper_delayed_common candidate="<<candidate->chain.name<<" mode="<<mode<<" seed="<<cfg.seed<<" delay="<<delay<<" tone_samples="<<ta.sample_count
      <<" baseline_twin_phase_deg="<<ta.sample_phase_rms*180/std::numbers::pi<<" edge_twin_phase_deg="<<tb.sample_phase_rms*180/std::numbers::pi
      <<" baseline_twin_frequency_hz="<<ta.frequency_rms<<" edge_twin_frequency_hz="<<tb.frequency_rms
      <<" common_early_samples="<<common_early.admitted<<" common_late_samples="<<common_late.admitted
      <<" apsk_symbols="<<ea.symbols<<" baseline_evm="<<ea.noisy<<" edge_evm="<<eb.noisy<<"\n";
    }
    return 0;
}catch(const std::exception& e){std::cerr<<"delayed study: "<<e.what()<<"\n";return 1;}}
#endif
