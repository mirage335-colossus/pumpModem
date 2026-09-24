#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "search_parallel.hpp"
#include "pattern_correlator_batch.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>

namespace datapump::modem {
namespace {
using Complex = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;
constexpr std::size_t block_size = 128;
void require(bool condition, const char* message) { if (!condition) throw Error(message); }
void cancelled(std::stop_token stop) { if (stop.stop_requested()) throw Error("pattern correlation cancelled"); }
using Projection=detail::CorrelationProjection;
using Fit=detail::CorrelationFit;
struct Bank {
    double frequency=0;
    std::vector<Projection> prefix;
};
}

struct PatternCorrelator::Impl {
    Config config;
    PatternSearch search;
    PatternCode code;
    // Mutable keystream/chip caches belong to one scoring worker. Completions
    // still run in hypothesis order on the caller after these workers finish.
    std::vector<PatternCode> worker_codes;
    std::size_t budget=0,bit_limit=0,accounted_bytes=0;
    std::uint64_t sample=0,trials=0;
    std::uint64_t phase_step=1;
    std::size_t alternate_groups=0;
    std::size_t block_samples=block_size;
    bool finished=false,shaped=false;
    struct Hypothesis {
        long double origin=0,rate=1;
        std::uint64_t index=0,observed_start=0,phase_lower=0,phase_upper=0;
        std::size_t frequency=0,rate_index=0;
        std::array<Fit,2> fits{};
        PatternBurst burst;
        // The burst stores committed score/support; only tentative totals need
        // separate accumulators. Its committed endpoint is also the last find.
        double sum_score=0,pending_score=0,sum_support=0;
        std::size_t committed=0,gap_slots=0;
        std::uint64_t committed_end=0;
        bool admitted=false,pending_gaps=false;
    };
    std::vector<Hypothesis> hypotheses;
    struct Emission {
        const Hypothesis* owner=nullptr;
        std::uint64_t first_symbol=0,last_symbol=0;
        long double origin=0;
        double frequency=0,reported_frequency=0,carrier_score=0;
        bool ended=false;
    };
    std::vector<Emission> emissions;
    // Only unresolved subsecond schedules need extra fits. Ordinary explicit
    // schedules retain the original pair of per-hypothesis accumulators.
    std::vector<std::array<Fit,2>> alternate_fits;
    std::vector<Bank> banks;
    std::vector<PatternEvidence> history;
    std::vector<PatternBurst> bursts;
    std::vector<Complex> points;
    std::size_t point_begin=0,point_count=0;
    Complex previous_point{};

    Impl(Config c,PatternSearch options,std::size_t bytes):config(c),search(std::move(options)),code(c,c.stream_epoch),budget(bytes) {
        validate(c);
        if(search.compact_clock_search) {
            search.candidate_limit=std::min<std::size_t>(search.candidate_limit,32);
            block_samples=32;
        }
        const std::size_t point_capacity=search.compact_clock_search?64:2048;
        phase_step=std::gcd(code.symbol_samples(),static_cast<std::uint64_t>(c.sample_rate));
        const auto phase_upper=search.search_stream_phases && (c.scramble || c.dsss)?
            (std::min(code.symbol_samples(),static_cast<std::uint64_t>(c.sample_rate))-1)/phase_step*phase_step:
            c.stream_phase_samples;
        const auto phase_lower=search.search_stream_phases && (c.scramble || c.dsss)?0:c.stream_phase_samples;
        if(phase_lower<phase_upper)alternate_groups=code.symbol_samples()>=c.sample_rate?1:2;
        shaped=pattern_pulse_enabled(c);
        require(c.pattern_symbols,"streaming pattern correlator requires binary pattern transport");
        require(search.start_offset_seconds && std::isfinite(*search.start_offset_seconds) &&
                std::isfinite(search.start_uncertainty_seconds) && search.start_uncertainty_seconds>=0,
                "long pattern correlation requires a finite system-clock start window");
        require(std::isfinite(search.false_alarm_probability) && search.false_alarm_probability>0 && search.false_alarm_probability<1 &&
                std::isfinite(search.retain_score) && search.retain_score>=0 && search.bit_limit && search.chunk_bits &&
                search.candidate_limit && search.candidate_limit<=65536 && search.track_limit && search.track_limit<=128,
                "invalid long pattern search limits");
        require(!search.clock_errors_ppm.empty() && search.clock_errors_ppm.size()<=65,"clock-rate bank must contain 1..65 hypotheses");
        long double highest_rate=1;
        for(auto ppm:search.clock_errors_ppm) {
            require(std::isfinite(ppm) && std::abs(ppm)<=10000,"clock-rate hypotheses must fit +/-10000 ppm");
            highest_rate=std::max(highest_rate,1+static_cast<long double>(ppm)*1e-6L);
        }
        if(search.frequency_offsets_hz.empty()) {
            const auto step=.25*c.sample_rate/static_cast<double>(code.symbol_samples());
            search.frequency_offsets_hz={0,-step,step,-2*step,2*step};
        }
        require(search.frequency_offsets_hz.size()<=65,"frequency bank exceeds 65 hypotheses");
        const auto tone_limit=static_cast<double>(c.sample_rate)/(4*static_cast<double>(code.chip_samples()));
        for(auto frequency:search.frequency_offsets_hz) {
            require(std::isfinite(frequency) && std::abs(frequency)<=c.bandwidth_hz/8,"frequency hypothesis exceeds occupied band");
            if(c.spreading_mode==SpreadingMode::tone)
                require(std::abs(frequency)<tone_limit,"tone frequency uncertainty aliases binary labels");
        }
        const auto lower=(static_cast<long double>(*search.start_offset_seconds)-search.start_uncertainty_seconds)*c.sample_rate;
        const auto upper=(static_cast<long double>(*search.start_offset_seconds)+search.start_uncertainty_seconds)*c.sample_rate;
        require(std::isfinite(lower) && std::isfinite(upper) && std::abs(lower)<1e15L && std::abs(upper)<1e15L,
                "clock start window exceeds precise sample-coordinate range");
        const auto step=std::max(1.L,std::floor(static_cast<long double>(code.chip_samples())/(2*highest_rate)));
        const auto origins=std::ceil((upper-lower)/step)+1;
        const auto total=origins*search.frequency_offsets_hz.size()*search.clock_errors_ppm.size();
        const auto bank_count=search.frequency_offsets_hz.size()*(c.spreading_mode==SpreadingMode::tone?2*search.clock_errors_ppm.size():1);
        const long double fixed=sizeof(PatternCorrelator)+sizeof(Impl)+code.working_bytes()+
            total*(sizeof(Hypothesis)+alternate_groups*sizeof(std::array<Fit,2>))+
            bank_count*(sizeof(Bank)+(block_samples+1)*sizeof(Projection))+point_capacity*sizeof(Complex)+
            static_cast<long double>(search.candidate_limit)*sizeof(PatternEvidence)+search.track_limit*(sizeof(PatternBurst)+sizeof(Emission))+
            (search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double);
        require(total>=1 && total<=std::numeric_limits<std::size_t>::max() && fixed<bytes,
                "complete half-chip clock/frequency/rate coverage exceeds DSP workspace");
        const auto count=static_cast<std::size_t>(total);
        const auto remaining=bytes-static_cast<std::size_t>(std::ceil(fixed));
        const auto denominator=2*count+2*search.track_limit+2;
        bit_limit=std::min(search.bit_limit,remaining/denominator);
        require(bit_limit>0,"clock-search workspace cannot retain symbol evidence");
        hypotheses.reserve(count);emissions.reserve(search.track_limit);banks.resize(bank_count);
        for(auto& bank:banks)bank.prefix.resize(block_samples+1);
        points.resize(point_capacity);
        require(!alternate_groups || count<=std::numeric_limits<std::size_t>::max()/alternate_groups,
                "pattern phase fits exceed address space");
        alternate_fits.resize(count*alternate_groups);
        history.reserve(search.candidate_limit);bursts.reserve(search.track_limit);
        for(std::size_t rate=0;rate<search.clock_errors_ppm.size();++rate) {
            const auto ratio=1+static_cast<long double>(search.clock_errors_ppm[rate])*1e-6L;
            for(std::size_t f=0;f<search.frequency_offsets_hz.size();++f) {
                if(c.spreading_mode==SpreadingMode::tone)for(unsigned bit=0;bit<2;++bit)
                    banks[(rate*search.frequency_offsets_hz.size()+f)*2+bit].frequency=c.carrier_hz+search.frequency_offsets_hz[f]+
                        (bit?1.:-1.)*tone_limit*static_cast<double>(ratio);
                else banks[f].frequency=c.carrier_hz+search.frequency_offsets_hz[f];
                for(std::size_t i=0;i<static_cast<std::size_t>(origins);++i) {
                    Hypothesis h;h.origin=std::min(upper,lower+static_cast<long double>(i)*step);
                    h.rate=ratio;h.frequency=f;h.rate_index=rate;
                    h.phase_lower=phase_lower;h.phase_upper=phase_upper;
                    const auto elapsed=std::max(0.L,-h.origin)*ratio;
                    const auto index=std::floor(elapsed/code.symbol_samples());
                    require(index<std::numeric_limits<std::uint64_t>::max(),"clock hint exceeds stream symbol counter");
                    h.index=static_cast<std::uint64_t>(index);hypotheses.push_back(std::move(h));
                }
            }
        }
        accounted_bytes=working_bytes();
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"clock-search state exceeds DSP workspace");
    }
    void prepare_workers() {
        if(!worker_codes.empty())return;
        const auto concurrency=std::min(hypotheses.size(),detail::search_concurrency(search.worker_threads));
        const auto workers=std::min(concurrency,(budget-sizeof(PatternCorrelator)-accounted_bytes)/code.working_bytes());
        if(workers>1) {
            std::vector<PatternCode> prepared;prepared.reserve(workers);
            for(std::size_t worker=0;worker<workers;++worker)prepared.emplace_back(config,config.stream_epoch);
            worker_codes=std::move(prepared);
            accounted_bytes=working_bytes();
            require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"parallel pattern caches exceed DSP workspace");
        }
    }
    std::size_t working_bytes() const {
        auto value=sizeof(Impl)+code.working_bytes()+hypotheses.capacity()*sizeof(Hypothesis)+banks.capacity()*sizeof(Bank)+
            emissions.capacity()*sizeof(Emission)+
            alternate_fits.capacity()*sizeof(decltype(alternate_fits)::value_type)+
            points.capacity()*sizeof(Complex)+
            history.capacity()*sizeof(PatternEvidence)+bursts.capacity()*sizeof(PatternBurst)+
            (search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double);
        for(const auto& h:hypotheses)value+=h.burst.bits.capacity();
        for(const auto& burst:bursts)value+=burst.bits.capacity();
        for(const auto& bank:banks)value+=bank.prefix.capacity()*sizeof(Projection);
        value+=worker_bytes();
        return value;
    }
    std::size_t worker_bytes() const {
        auto bytes=worker_codes.capacity()*sizeof(PatternCode);
        for(const auto& worker:worker_codes)bytes+=worker.working_bytes()-sizeof(PatternCode);
        return bytes;
    }
    void drop_workers() {
        accounted_bytes-=worker_bytes();
        std::vector<PatternCode>().swap(worker_codes);
    }
    void room_for(std::size_t extra) {
        if(!worker_codes.empty() && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
                extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_workers();
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget && extra<=budget-sizeof(PatternCorrelator)-accounted_bytes,
                "pattern evidence exceeds DSP workspace");
    }
    double threshold() const {
        const auto t=static_cast<double>(trials);
        return -std::log(search.false_alarm_probability)+std::log(t)+std::log(t+1)+std::log(2.);
    }
    void remember(PatternEvidence evidence) {
        if(evidence.score<search.retain_score)return;
        evidence.admission_threshold=threshold();
        if(history.size()==search.candidate_limit)history.erase(history.begin());
        history.push_back(evidence);
    }
    struct PhaseGroup {std::uint64_t lower=0,upper=0;};
    std::array<PhaseGroup,3> phase_groups(const Hypothesis& h,std::size_t& count)const {
        std::array<PhaseGroup,3> groups{};count=0;
        auto lower=h.phase_lower;
        while(lower<=h.phase_upper) {
            const auto address=symbol_stream_address(config.stream_epoch,lower,h.index,code.symbol_samples(),config.sample_rate);
            auto low=std::uint64_t{0},high=(h.phase_upper-lower)/phase_step;
            while(low<high) {
                const auto mid=low+(high-low+1)/2;
                const auto candidate=symbol_stream_address(config.stream_epoch,lower+mid*phase_step,h.index,
                    code.symbol_samples(),config.sample_rate);
                if(candidate.epoch==address.epoch && candidate.ordinal==address.ordinal)low=mid;
                else high=mid-1;
            }
            const auto end=lower+low*phase_step;
            require(count<groups.size(),"pattern phase groups exceed finite symbol interval");
            groups[count++]={lower,end};
            if(end==h.phase_upper)break;
            lower=end+phase_step;
        }
        return groups;
    }
    std::array<Fit,2>& fits(Hypothesis& h,std::size_t hypothesis,std::size_t group) {
        if(!group)return h.fits;
        require(group<=alternate_groups,"pattern phase fit exceeds its allocated bank");
        return alternate_fits[hypothesis*alternate_groups+group-1];
    }
    Emission& output_stream(Hypothesis& h) {
        const auto same_clock=[&](long double origin,double frequency) {
            return std::abs(origin-h.origin)<static_cast<long double>(code.symbol_samples())/3 &&
                std::abs(frequency-h.burst.frequency_hz)<=config.sample_rate/static_cast<double>(code.symbol_samples());
        };
        for(auto& emission:emissions)if(same_clock(emission.origin,emission.frequency) &&
            (!emission.ended || h.burst.stream_first_symbol<emission.last_symbol))return emission;
        // Choose once before immutable output starts. Other hypotheses retain
        // their evidence but cannot replace already delivered stream bytes.
        const Hypothesis* owner=&h;
        for(const auto& candidate:hypotheses)if(candidate.admitted && same_clock(candidate.origin,candidate.burst.frequency_hz) &&
            (candidate.burst.stream_first_symbol<owner->burst.stream_first_symbol ||
             (candidate.burst.stream_first_symbol==owner->burst.stream_first_symbol && candidate.sum_score>owner->sum_score)))owner=&candidate;
        Emission emission{owner,owner->burst.stream_first_symbol,owner->burst.first_stream_symbol,
            owner->origin,owner->burst.frequency_hz,owner->burst.frequency_hz,owner->burst.score,false};
        if(emissions.size()==search.track_limit) {
            const auto reusable=std::find_if(emissions.begin(),emissions.end(),[](const auto& item){return item.ended;});
            require(reusable!=emissions.end(),"pattern output stream quota exhausted");*reusable=emission;return *reusable;
        }
        emissions.push_back(emission);return emissions.back();
    }
    double carrier_estimate(const Hypothesis& owner,Emission& stream) const {
        // Keep immutable bit ownership, but report the strongest accumulated
        // carrier evidence over comparable observations. A startup distortion
        // must not lock the displayed frequency to its first winning grid bin.
        const auto* best=&owner;
        for(const auto& candidate:hypotheses) {
            if(!candidate.admitted || candidate.burst.stream_first_symbol!=owner.burst.stream_first_symbol)continue;
            const auto difference=candidate.committed_end>owner.committed_end?
                candidate.committed_end-owner.committed_end:owner.committed_end-candidate.committed_end;
            if(difference>code.symbol_samples() || std::abs(candidate.origin-owner.origin)>=code.symbol_samples()/3.L ||
               std::abs(candidate.burst.frequency_hz-owner.burst.frequency_hz)>
                    config.sample_rate/static_cast<double>(code.symbol_samples()))continue;
            if(candidate.burst.score>best->burst.score)best=&candidate;
        }
        // Nearby clocks may retire in a different order at the terminal
        // absence observation. Their retirement must not revert an already
        // stronger accumulated carrier estimate to the startup hypothesis.
        if(best->burst.score>=stream.carrier_score) {
            stream.carrier_score=best->burst.score;stream.reported_frequency=best->burst.frequency_hz;
        }
        return stream.reported_frequency;
    }
    void publish(Hypothesis& h,bool complete=false,bool flush=true,bool draining=false) {
        if(!h.admitted)return;
        if(draining && !h.committed)return;
        const auto chunk=std::min(search.chunk_bits,bit_limit);
        if(!complete && !flush && h.committed<chunk)return;
        auto& stream=output_stream(h);
        if(stream.owner!=&h) {
            h.burst.bits.erase(h.burst.bits.begin(),h.burst.bits.begin()+static_cast<std::ptrdiff_t>(h.committed));
            h.burst.first_stream_symbol+=h.committed;h.burst.first_sample=h.committed_end;h.committed=0;
            return;
        }
        do {
            const auto count=std::min(h.committed,chunk);
            if(!count && !complete)break;
            if(draining && bursts.size()==search.track_limit)break;
            room_for(count);
            PatternBurst result;
            result.first_sample=h.burst.first_sample;result.first_stream_symbol=h.burst.first_stream_symbol;
            result.stream_first_sample=h.burst.stream_first_sample;result.stream_first_symbol=h.burst.stream_first_symbol;
            result.frequency_hz=carrier_estimate(h,stream);result.stream_phase_samples=h.burst.stream_phase_samples;
            result.bits.assign(h.burst.bits.begin(),h.burst.bits.begin()+static_cast<std::ptrdiff_t>(count));
            result.complete=complete && count==h.committed;
            result.end_sample=count==h.committed?h.committed_end:
                result.first_sample+static_cast<std::uint64_t>(count*code.symbol_samples()/h.rate);
            result.score=h.burst.score;
            result.support_samples=h.burst.support_samples;
            const auto duplicate=std::find_if(bursts.begin(),bursts.end(),[&](const auto& prior) {
                const auto delta=prior.first_sample>result.first_sample?prior.first_sample-result.first_sample:result.first_sample-prior.first_sample;
                return prior.first_stream_symbol==result.first_stream_symbol && prior.complete==result.complete &&
                    delta<code.symbol_samples()/3 && std::abs(prior.frequency_hz-result.frequency_hz)<=config.sample_rate/static_cast<double>(code.symbol_samples());
            });
            const auto emitted_end=result.end_sample;
            if(duplicate!=bursts.end()) {
                if(result.score>duplicate->score) {
                    accounted_bytes=accounted_bytes-duplicate->bits.capacity()+result.bits.capacity();*duplicate=std::move(result);
                }
            } else {
                require(bursts.size()<search.track_limit,"pattern output queue requires draining");
                accounted_bytes+=result.bits.capacity();bursts.push_back(std::move(result));
            }
            h.burst.bits.erase(h.burst.bits.begin(),h.burst.bits.begin()+static_cast<std::ptrdiff_t>(count));
            h.burst.first_stream_symbol+=count;h.burst.first_sample=emitted_end;h.committed-=count;
            stream.last_symbol=h.burst.first_stream_symbol;
            if(complete && !h.committed)stream.ended=true;
            if(!h.committed)break;
        } while(complete || flush || h.committed>=chunk);
    }
    void clear(Hypothesis& h) {
        // Keep the finite clock hypothesis, but release a terminated message's
        // payload allocation so later hypotheses can use the same workspace.
        accounted_bytes-=h.burst.bits.capacity();Bytes{}.swap(h.burst.bits);
        h.burst.complete=false;h.admitted=h.pending_gaps=false;
        h.sum_score=h.pending_score=h.burst.score=0;h.committed=0;h.gap_slots=0;
        h.sum_support=h.burst.support_samples=0;
    }
    void append(Hypothesis& h,std::uint8_t bit) {
        require(h.burst.bits.size()<bit_limit,"pattern bit retention limit reached");
        if(h.burst.bits.size()==h.burst.bits.capacity()) {
            const auto capacity=h.burst.bits.capacity();const auto wanted=std::min(bit_limit,std::max<std::size_t>(1,capacity*2));
            room_for(wanted);h.burst.bits.reserve(wanted);accounted_bytes+=h.burst.bits.capacity()-capacity;
        }
        h.burst.bits.push_back(bit);
    }
    void mark_pending_gaps(Hypothesis& h) {
        if(!h.pending_gaps) {
            h.gap_slots=h.burst.bits.size()-h.committed;h.burst.bits.resize(h.committed);
        }
        h.sum_score=h.burst.score;h.pending_score=0;h.pending_gaps=true;
        h.sum_support=h.burst.support_samples;
    }
    void emit_gap(Hypothesis& h,std::uint64_t resumed_sample) {
        if(!h.gap_slots)return;
        publish(h);
        auto& stream=output_stream(h);
        if(stream.owner!=&h) {
            h.gap_slots=0;h.burst.first_sample=resumed_sample;h.burst.first_stream_symbol=h.index;return;
        }
        require(bursts.size()<search.track_limit,"pattern output queue requires draining");
        PatternBurst event;
        event.first_sample=h.burst.first_sample;event.end_sample=resumed_sample;
        event.first_stream_symbol=h.burst.first_stream_symbol;
        event.stream_first_sample=h.burst.stream_first_sample;event.stream_first_symbol=h.burst.stream_first_symbol;
        event.frequency_hz=carrier_estimate(h,stream);event.score=h.burst.score;
        event.support_samples=h.burst.support_samples;
        event.stream_phase_samples=h.burst.stream_phase_samples;event.missing_slots=h.gap_slots;
        bursts.push_back(std::move(event));h.gap_slots=0;
        h.burst.first_sample=resumed_sample;h.burst.first_stream_symbol=h.index;stream.last_symbol=h.index;
    }
    void complete(Hypothesis& h,std::size_t hypothesis,std::uint64_t end) {
        std::size_t group_count=0;
        const auto groups=phase_groups(h,group_count);
        PatternEvidence e;e.score=-1;std::size_t selected=0;
        require(group_count<=std::numeric_limits<std::uint64_t>::max()-trials,"pattern trial counter overflow");
        trials+=group_count;
        for(std::size_t group=0;group<group_count;++group) {
            const auto& fit=fits(h,hypothesis,group);
            const auto a=fit[0].score(),b=fit[1].score();
            if(std::max(a,b)>e.score) {
                e={h.observed_start,end,h.index,config.carrier_hz+search.frequency_offsets_hz[h.frequency],
                    std::max(a,b),std::min(a,b),b>a?1U:0U,groups[group].lower};
                selected=group;
            }
        }
        remember(e);
        const auto symbol_support=pattern_symbol_support(
            static_cast<double>(fits(h,hypothesis,selected)[e.bit].count),e.score,code.chip_samples());
        const auto standalone=e.score>=threshold();
        // Unknown slots never select a private phase. Keep the existing clock
        // until an independently confident later symbol resolves the schedule.
        const auto pending_count=h.burst.bits.size()-h.committed;
        const auto next_count=static_cast<double>(pending_count)+1;
        const auto next_score=h.pending_score+e.score;
        const auto next_chain=next_score>next_count?
            next_score-next_count-next_count*std::log(next_score/next_count)-next_count*std::log(2.):0;
        // Preserve a tail whose joint evidence justifies continuation. If it
        // does not, a confident current symbol must still start independently.
        // An unadmitted prefix cannot borrow that later symbol's confidence.
        const auto reliable=e.score>=search.retain_score && e.score-e.alternative_score>=1 &&
            (group_count==1 || standalone);
        const auto can_preserve=h.admitted;
        auto preserve_gap=can_preserve &&
            (h.pending_gaps || !reliable || (standalone && pending_count && next_chain<threshold()));
        if(preserve_gap) {
            // Retain only the admitted clock and symbol positions. Missing
            // slots provide neither bit evidence nor confidence to the track.
            mark_pending_gaps(h);
            h.burst.end_sample=end;
            if(reliable && standalone) {
                h.phase_lower=groups[selected].lower;h.phase_upper=groups[selected].upper;
                h.burst.stream_phase_samples=h.phase_lower;
                emit_gap(h,h.observed_start);append(h,static_cast<std::uint8_t>(e.bit));
                h.sum_score+=e.score;h.committed=h.burst.bits.size();h.committed_end=end;
                h.sum_support+=symbol_support;h.burst.support_samples=h.sum_support;
                h.burst.score=h.sum_score;h.pending_gaps=false;
            } else {
                require(h.gap_slots<std::numeric_limits<std::size_t>::max(),"pattern missing-slot count overflow");++h.gap_slots;
            }
        } else {
            if(standalone && pending_count && (!h.admitted || next_chain<threshold())) {publish(h);clear(h);}
            if(h.burst.bits.size()==bit_limit && e.score>=search.retain_score)
                throw Error("pattern bit retention limit reached");
            // Weak evidence from different possible schedules cannot be added as
            // though it supported one coherent stream. Resolve a split on the
            // current symbol's own evidence before extending its bit chain.
            if(reliable && h.burst.bits.size()<bit_limit) {
                if(standalone) {
                    h.phase_lower=groups[selected].lower;h.phase_upper=groups[selected].upper;
                    h.burst.stream_phase_samples=h.phase_lower;
                }
                if(h.burst.bits.empty()) {
                    if(!h.admitted) {h.burst.stream_first_sample=h.observed_start;h.burst.stream_first_symbol=h.index;}
                    h.burst.first_sample=h.observed_start;h.burst.first_stream_symbol=h.index;
                    h.burst.stream_phase_samples=h.phase_lower;
                    h.burst.frequency_hz=e.frequency_hz;
                    if(!h.admitted){h.sum_score=h.pending_score=h.burst.score=0;h.committed=0;h.gap_slots=0;
                        h.sum_support=h.burst.support_samples=0;}
                }
                append(h,static_cast<std::uint8_t>(e.bit));h.burst.end_sample=end;h.sum_score+=e.score;h.pending_score+=e.score;
                h.sum_support+=symbol_support;
                const auto pending=h.burst.bits.size()-h.committed;
                const auto n=static_cast<double>(pending);
                const auto chain=h.pending_score>n?h.pending_score-n-n*std::log(h.pending_score/n)-n*std::log(2.):0;
                if((n==1 && standalone) || chain>=threshold()) {
                    h.admitted=true;h.committed=h.burst.bits.size();h.committed_end=end;
                    h.burst.score=h.sum_score;h.pending_score=0;
                    h.burst.support_samples=h.sum_support;
                }
                if(!h.admitted && static_cast<long double>(pending)*code.symbol_samples()/h.rate>=
                    static_cast<long double>(pattern_absence_seconds)*config.sample_rate)clear(h);
            } else {publish(h);clear(h);}
        }
        if(h.admitted && static_cast<long double>(end-h.committed_end)>=
            static_cast<long double>(pattern_absence_seconds)*config.sample_rate) {publish(h,true);clear(h);}
        else publish(h,false,false);
        h.fits={};
        for(std::size_t group=0;group<alternate_groups;++group)
            alternate_fits[hypothesis*alternate_groups+group]={};
        require(h.index<std::numeric_limits<std::uint64_t>::max(),"pattern stream symbol counter overflow");++h.index;
    }
    std::uint64_t initial_cursor(const Hypothesis& h,std::uint64_t end) const {
        if(h.origin>static_cast<long double>(sample))
            return static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(h.origin)));
        return sample;
    }
    std::uint64_t accumulate(Hypothesis& h,std::size_t hypothesis,std::uint64_t cursor,
                             std::uint64_t end,PatternCode& pattern) {
        const auto symbol_start=h.origin+static_cast<long double>(h.index)*code.symbol_samples()/h.rate;
        const auto symbol_end=h.origin+(static_cast<long double>(h.index)+1)*code.symbol_samples()/h.rate;
        const auto segment_end=static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(symbol_end)));
        std::size_t group_count=0;
        const auto groups=phase_groups(h,group_count);
        if(!h.fits[0].count)h.observed_start=cursor;
        for(std::size_t group=0;group<group_count;++group) {
            pattern.set_stream_phase_samples(groups[group].lower);
            auto& fit=fits(h,hypothesis,group);
            auto observed=cursor;
            while(observed<segment_end) {
                const auto within=std::max(0.L,(static_cast<long double>(observed)-symbol_start)*h.rate);
                if(shaped) {
                    require(h.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),
                            "pattern chip coordinate overflow");
                    const auto first_chip=h.index*code.chips_per_symbol();
                    const auto left=static_cast<std::size_t>(observed-sample);
                    const auto& bank=banks[h.frequency];
                    const auto projection=bank.prefix[left+1]-bank.prefix[left];
                    // Each alternative schedule fits the same disjoint
                    // raw observations. Its score never borrows samples
                    // or evidence from another possible schedule.
                    for(unsigned bit=0;bit<2;++bit) {
                        const auto phase=pattern.shaped_value(first_chip,bit,static_cast<double>(within));
                        fit[bit].add(projection,phase,1);
                    }
                    ++observed;continue;
                }
                const auto local=static_cast<std::uint64_t>(std::floor(within/code.chip_samples()));
                require(h.index<=(std::numeric_limits<std::uint64_t>::max()-local)/code.chips_per_symbol(),"pattern chip coordinate overflow");
                const auto chip=h.index*code.chips_per_symbol()+local;
                const auto fraction=std::clamp(static_cast<double>(within/code.chip_samples()-local),0.,std::nextafter(1.,0.));
                const auto chip_end=symbol_start+(static_cast<long double>(local)+1)*code.chip_samples()/h.rate;
                const auto boundary=std::min(static_cast<long double>(segment_end),std::ceil(std::min(chip_end,symbol_end)));
                const auto until=static_cast<std::uint64_t>(std::max(static_cast<long double>(observed+1),boundary));
                const auto left=static_cast<std::size_t>(observed-sample),right=static_cast<std::size_t>(until-sample);
                for(unsigned bit=0;bit<2;++bit) {
                    const auto bank_index=config.spreading_mode==SpreadingMode::tone?(h.rate_index*search.frequency_offsets_hz.size()+h.frequency)*2+bit:h.frequency;
                    const auto& bank=banks[bank_index];
                    auto phase=pattern.value(chip,bit,fraction);
                    if(config.spreading_mode==SpreadingMode::tone) {
                        const auto tone=bank.frequency-config.carrier_hz-search.frequency_offsets_hz[h.frequency];
                        phase*=std::polar(1.,-static_cast<double>(std::remainder(static_cast<long double>(observed)*tau*tone/config.sample_rate,static_cast<long double>(tau))));
                    }
                    const auto projection=bank.prefix[right]-bank.prefix[left];
                    fit[bit].add(projection,phase,right-left);
                }
                observed=until;
            }
        }
        return segment_end;
    }
    void record_point(const Projection& measured) {
        const Complex point{measured.xc,measured.xs};
        const auto delta=std::abs(previous_point)>1e-20?point*std::conj(previous_point)/std::abs(previous_point):point;
        if(point_count==points.size()){point_begin=(point_begin+1)%points.size();--point_count;}
        points[(point_begin+point_count++)%points.size()]=delta;previous_point=point;
    }
    std::size_t process_batch(std::span<const float> input,std::stop_token stop) {
        // Preserve the scalar path as a reference, and never collect samples
        // across a caller's progress poll. Small/tight-workspace pushes retain
        // the original one-block path. No completion is allowed in this batch.
        if(worker_codes.size()<2 || input.size()<=block_samples)return 0;
        constexpr std::size_t max_blocks=64,max_lanes=65536;
        using Lane=detail::CorrelationLane;
        using Block=detail::CorrelationBlock;
        const auto retained=sizeof(PatternCorrelator)+working_bytes();
        if(retained>=budget)return 0;
        const auto spare=budget-retained;
        const auto per_block=banks.size()*(block_samples+1)*sizeof(Projection)+sizeof(Block);
        const auto frequencies_bytes=banks.size()*sizeof(double);
        const auto minimum_lanes=std::min<std::size_t>(64,hypotheses.size());
        const auto minimum_bytes=frequencies_bytes+minimum_lanes*sizeof(Lane);
        if(spare<=minimum_bytes || (spare-minimum_bytes)/per_block<2)return 0;
        const auto block_capacity=std::min(max_blocks,(spare-minimum_bytes)/per_block);
        const auto proposed=std::min(input.size(),block_capacity*block_samples);
        auto boundary=std::numeric_limits<long double>::infinity();
        for(const auto& h:hypotheses) {
            const auto end=std::ceil(h.origin+(static_cast<long double>(h.index)+1)*code.symbol_samples()/h.rate);
            boundary=std::min(boundary,end);
        }
        std::size_t consumed=0,block_count=0;
        while(consumed<proposed) {
            const auto count=std::min(block_samples,proposed-consumed);
            if(static_cast<long double>(sample+consumed+count)>=boundary)break;
            consumed+=count;++block_count;
        }
        if(block_count<2)return 0;
        const auto projection_count=block_count*banks.size()*(block_samples+1);
        const auto fixed_bytes=projection_count*sizeof(Projection)+block_count*sizeof(Block)+frequencies_bytes;
        const auto lane_capacity=std::min({max_lanes,hypotheses.size(),(spare-fixed_bytes)/sizeof(Lane)});
        // Exact-size temporary arrays make the peak reservation independent of
        // vector growth policy. They disappear before ordered completion can
        // grow payloads; idle footprints and receiver-bank admission stay intact.
        auto projections=std::make_unique<Projection[]>(projection_count);
        auto blocks=std::make_unique<Block[]>(block_count);
        auto frequencies=std::make_unique<double[]>(banks.size());
        auto lanes=std::make_unique<Lane[]>(lane_capacity);
        for(std::size_t f=0;f<banks.size();++f)frequencies[f]=banks[f].frequency;
        std::size_t offset=0,row_offset=0;
        for(std::size_t b=0;b<block_count;++b) {
            cancelled(stop);
            const auto count=std::min(block_samples,consumed-offset);
            blocks[b]={sample+offset,count,row_offset};
            for(std::size_t f=0;f<banks.size();++f) {
                auto prefix=std::span(projections.get()+row_offset+f*(count+1),count+1);
                prefix[0]={};
                auto oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(sample+offset)*tau*
                    banks[f].frequency/config.sample_rate,static_cast<long double>(tau))));
                const auto step=std::polar(1.,tau*banks[f].frequency/config.sample_rate);
                for(std::size_t i=0;i<count;++i) {
                    const auto c=oscillator.real(),s=oscillator.imag(),x=static_cast<double>(input[offset+i]);
                    auto p=prefix[i];p.xc+=x*c;p.xs+=x*s;p.cc+=c*c;p.ss+=s*s;p.cs+=c*s;p.energy+=x*x;
                    prefix[i+1]=p;oscillator*=step;
                }
            }
            row_offset+=banks.size()*(count+1);offset+=count;
        }
        detail::CorrelationBatch batch{
            {config.stream_epoch,code.symbol_samples(),code.chip_samples(),code.chips_per_symbol(),phase_step,
             config.sample_rate,search.frequency_offsets_hz.size(),config.carrier_hz,shaped,
             config.spreading_mode==SpreadingMode::tone,
             {static_cast<std::uint32_t>(config.spreading_mode),config.scramble,config.dsss,
              config.spreading_seed,config.dsss_seed}},
            {blocks.get(),block_count},{projections.get(),row_offset},{frequencies.get(),banks.size()},search.frequency_offsets_hz};
        // Numeric tiles contain no PatternBurst, heap-owned input, or references
        // to peer admission state. Device implementations can operate on these
        // same indexed lanes without creating one host thread per hypothesis.
        for(std::size_t first=0;first<hypotheses.size();) {
            const auto count=std::min(lane_capacity,hypotheses.size()-first);
            for(std::size_t i=0;i<count;++i) {
                const auto& h=hypotheses[first+i];auto& lane=lanes[i];
                lane.origin=h.origin;lane.rate=h.rate;lane.index=h.index;lane.observed_start=h.observed_start;
                lane.phase_lower=h.phase_lower;lane.phase_upper=h.phase_upper;
                lane.frequency=h.frequency;lane.rate_index=h.rate_index;lane.fits[0]=h.fits;
                for(std::size_t group=0;group<alternate_groups;++group)
                    lane.fits[group+1]=alternate_fits[(first+i)*alternate_groups+group];
            }
            detail::accumulate_correlator_cpu(batch,{lanes.get(),count},worker_codes,stop);
            for(std::size_t i=0;i<count;++i) {
                auto& h=hypotheses[first+i];const auto& lane=lanes[i];
                h.observed_start=lane.observed_start;h.fits=lane.fits[0];
                for(std::size_t group=0;group<alternate_groups;++group)
                    alternate_fits[(first+i)*alternate_groups+group]=lane.fits[group+1];
            }
            first+=count;
        }
        for(std::size_t b=0;b<block_count;++b)
            record_point(projections[blocks[b].projection_offset+blocks[b].count]);
        const auto& last=blocks[block_count-1];
        for(std::size_t f=0;f<banks.size();++f)
            std::copy_n(projections.get()+last.projection_offset+f*(last.count+1),last.count+1,banks[f].prefix.begin());
        sample+=consumed;
        return consumed;
    }
    void process(std::span<const float> input,std::stop_token stop) {
        for(std::size_t b=0;b<banks.size();++b) {
            cancelled(stop);auto& bank=banks[b];bank.prefix[0]={};
            auto oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(sample)*tau*bank.frequency/config.sample_rate,static_cast<long double>(tau))));
            const auto step=std::polar(1.,tau*bank.frequency/config.sample_rate);
            for(std::size_t i=0;i<input.size();++i) {
                const auto c=oscillator.real(),s=oscillator.imag(),x=static_cast<double>(input[i]);
                auto p=bank.prefix[i];p.xc+=x*c;p.xs+=x*s;p.cc+=c*c;p.ss+=s*s;p.cs+=c*s;p.energy+=x*x;
                bank.prefix[i+1]=p;oscillator*=step;
            }
        }
        const auto end=sample+input.size();
        const auto parallel=worker_codes.size()>1;
        if(parallel)detail::parallel_search(hypotheses.size(),worker_codes.size(),[&](std::size_t worker,std::size_t hypothesis) {
            cancelled(stop);auto& h=hypotheses[hypothesis];
            const auto cursor=initial_cursor(h,end);
            const auto symbol_end=h.origin+(static_cast<long double>(h.index)+1)*code.symbol_samples()/h.rate;
            if(cursor<end && static_cast<long double>(cursor)<symbol_end)
                accumulate(h,hypothesis,cursor,end,worker_codes[worker]);
        });
        for(std::size_t hypothesis=0;hypothesis<hypotheses.size();++hypothesis) {
            auto& h=hypotheses[hypothesis];
            cancelled(stop);auto cursor=initial_cursor(h,end);
            // Only independent fit accumulation was moved ahead. Trial counts,
            // phase selection, peer ownership and every publication retain the
            // original serial hypothesis/complete-symbol order.
            if(parallel && cursor<end) {
                const auto symbol_end=h.origin+(static_cast<long double>(h.index)+1)*code.symbol_samples()/h.rate;
                if(static_cast<long double>(cursor)<symbol_end) {
                    cursor=static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(symbol_end)));
                    if(static_cast<long double>(cursor)>=symbol_end)complete(h,hypothesis,cursor);
                }
            }
            while(cursor<end) {
                const auto symbol_end=h.origin+(static_cast<long double>(h.index)+1)*code.symbol_samples()/h.rate;
                if(static_cast<long double>(cursor)>=symbol_end) { complete(h,hypothesis,cursor);continue; }
                cursor=accumulate(h,hypothesis,cursor,end,code);
                if(static_cast<long double>(cursor)>=symbol_end)complete(h,hypothesis,cursor);
            }
        }
        record_point(banks.front().prefix[input.size()]);
        sample=end;
        room_for(0);
    }
};

PatternCorrelator::PatternCorrelator(Config c,PatternSearch search,std::size_t bytes):impl_(std::make_unique<Impl>(c,std::move(search),bytes)){}
PatternCorrelator::~PatternCorrelator()=default;
PatternCorrelator::PatternCorrelator(PatternCorrelator&&) noexcept=default;
PatternCorrelator& PatternCorrelator::operator=(PatternCorrelator&&) noexcept=default;
void PatternCorrelator::push(std::span<const float> samples,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);require(!s.finished,"pattern capture already finished");
    require(samples.size()<=std::numeric_limits<std::uint64_t>::max()-s.sample,"pattern sample counter overflow");
    for(auto value:samples)require(std::isfinite(value),"nonfinite pattern sample");
    // Idle epochs keep their original footprint so parallel scratch cannot
    // displace other clock/key hypotheses from a shared receiver bank.
    struct ReleaseWorkers {
        Impl& state;
        ~ReleaseWorkers() {state.drop_workers();}
    } release{s};
    if(!samples.empty())s.prepare_workers();
    for(std::size_t offset=0;offset<samples.size();) {
        auto count=s.process_batch(samples.subspan(offset),stop);
        if(!count) {
            count=std::min(s.block_samples,samples.size()-offset);
            s.process(samples.subspan(offset,count),stop);
        }
        offset+=count;
    }
}
void PatternCorrelator::finish(std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.finished)return;
    for(auto& h:s.hypotheses) {s.publish(h);s.clear(h);}
    s.finished=true;
}
std::vector<PatternBurst> PatternCorrelator::take_bursts(){
    auto& s=*impl_;
    // A consumer drain is also a presentation boundary. Publish every
    // accepted decision available now, even when a whole message is much
    // shorter than chunk_bits or each symbol takes hours. Draining does not
    // admit weak decisions, release the clock, or imply physical completion.
    for(auto& h:s.hypotheses)s.publish(h,false,true,true);
    auto result=std::move(s.bursts);s.bursts={};s.bursts.reserve(s.search.track_limit);
    s.accounted_bytes=s.working_bytes();return result;
}
PatternBurst PatternCorrelator::provisional()const {
    const auto& h=impl_->hypotheses;
    const auto best=std::max_element(h.begin(),h.end(),[](const auto& a,const auto& b){return (a.admitted?a.sum_score:-1)<(b.admitted?b.sum_score:-1);});
    if(best==h.end() || !best->admitted)return {};
    auto result=best->burst;result.bits.resize(best->committed);result.end_sample=best->committed_end;
    result.complete=false;return result;
}
std::vector<PatternEvidence> PatternCorrelator::candidates()const{return impl_->history;}
std::vector<PatternEvidence> PatternCorrelator::candidates(std::size_t limit)const{
    const auto& history=impl_->history;
    return {history.end()-static_cast<std::ptrdiff_t>(std::min(limit,history.size())),history.end()};
}
std::vector<Complex> PatternCorrelator::take_chip_constellation() {
    auto& s=*impl_;std::vector<Complex> result;result.reserve(s.point_count);
    for(std::size_t i=0;i<s.point_count;++i)result.push_back(s.points[(s.point_begin+i)%s.points.size()]);
    s.point_begin=s.point_count=0;return result;
}
bool PatternCorrelator::acquiring()const {return !impl_->finished && std::any_of(impl_->hypotheses.begin(),impl_->hypotheses.end(),[](const auto& h){return h.admitted || !h.burst.bits.empty();});}
bool PatternCorrelator::synchronized()const{return std::any_of(impl_->hypotheses.begin(),impl_->hypotheses.end(),[](const auto& h){return h.admitted;});}
std::size_t PatternCorrelator::working_bytes()const{return sizeof(PatternCorrelator)+impl_->working_bytes();}
void PatternCorrelator::set_workspace_bytes(std::size_t bytes) {
    if(bytes<working_bytes())impl_->drop_workers();
    require(bytes>=working_bytes(),"DSP workspace is smaller than streaming pattern state");impl_->budget=bytes;
}
Diagnostics PatternCorrelator::diagnostics()const {
    const auto burst=provisional();Diagnostics result;result.bit_rate=static_cast<double>(impl_->config.sample_rate)/static_cast<double>(impl_->code.symbol_samples());
    result.sample_offset=static_cast<std::size_t>(burst.first_sample);result.pattern_score=burst.score;return result;
}
}
