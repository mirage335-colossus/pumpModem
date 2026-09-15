#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
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
struct Projection {
    double xc=0,xs=0,cc=0,ss=0,cs=0,energy=0;
    Projection operator-(const Projection& b) const {
        return {xc-b.xc,xs-b.xs,cc-b.cc,ss-b.ss,cs-b.cs,energy-b.energy};
    }
};
struct Fit : Projection {
    std::uint64_t count=0;
    void add(const Projection& p, Complex phase, std::size_t n) {
        const auto a=phase.real(),b=phase.imag();
        xc+=a*p.xc-b*p.xs;xs+=b*p.xc+a*p.xs;
        cc+=a*a*p.cc+b*b*p.ss-2*a*b*p.cs;
        ss+=b*b*p.cc+a*a*p.ss+2*a*b*p.cs;
        cs+=a*b*(p.cc-p.ss)+(a*a-b*b)*p.cs;
        energy+=p.energy;count+=n;
    }
    double score() const {
        const auto determinant=cc*ss-cs*cs;
        if(count<=2 || energy<=1e-30 || determinant<=1e-12*std::max(1.,cc*ss))return 0;
        const auto explained=(ss*xc*xc+cc*xs*xs-2*cs*xc*xs)/determinant;
        const auto fraction=std::clamp(explained/energy,0.,1.-1e-15);
        // Two fitted real carrier bases in N independent real Gaussian samples:
        // R² ~ Beta(1,(N-2)/2). This includes the exact quadrature Gram matrix.
        return -.5*static_cast<double>(count-2)*std::log1p(-fraction);
    }
};
struct Bank {
    double frequency=0;
    std::vector<Projection> prefix;
};
}

struct PatternCorrelator::Impl {
    Config config;
    PatternSearch search;
    PatternCode code;
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
        double sum_score=0,pending_score=0,committed_score=0;
        std::size_t committed=0;
        std::uint64_t committed_end=0;
        bool admitted=false,pending_gaps=false;
    };
    std::vector<Hypothesis> hypotheses;
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
                std::isfinite(search.retain_score) && search.retain_score>=0 && search.bit_limit &&
                std::isfinite(search.max_gap_seconds) && search.max_gap_seconds>=0 &&
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
            static_cast<long double>(search.candidate_limit)*sizeof(PatternEvidence)+search.track_limit*sizeof(PatternBurst)+
            (search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double);
        require(total>=1 && total<=std::numeric_limits<std::size_t>::max() && fixed<bytes,
                "complete half-chip clock/frequency/rate coverage exceeds DSP workspace");
        const auto count=static_cast<std::size_t>(total);
        const auto remaining=bytes-static_cast<std::size_t>(std::ceil(fixed));
        const auto denominator=2*count+2*search.track_limit+2;
        bit_limit=std::min(search.bit_limit,remaining/denominator);
        require(bit_limit>0,"clock-search workspace cannot retain symbol evidence");
        hypotheses.reserve(count);banks.resize(bank_count);
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
    std::size_t working_bytes() const {
        auto value=sizeof(Impl)+code.working_bytes()+hypotheses.capacity()*sizeof(Hypothesis)+banks.capacity()*sizeof(Bank)+
            alternate_fits.capacity()*sizeof(decltype(alternate_fits)::value_type)+
            points.capacity()*sizeof(Complex)+
            history.capacity()*sizeof(PatternEvidence)+bursts.capacity()*sizeof(PatternBurst)+
            (search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double);
        for(const auto& h:hypotheses)value+=h.burst.bits.capacity();
        for(const auto& burst:bursts)value+=burst.bits.capacity();
        for(const auto& bank:banks)value+=bank.prefix.capacity()*sizeof(Projection);
        return value;
    }
    void room_for(std::size_t extra) const {
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget && extra<=budget-sizeof(PatternCorrelator)-accounted_bytes,
                "pattern evidence exceeds DSP workspace");
    }
    double threshold() const {
        const auto t=static_cast<double>(trials);
        return -std::log(search.false_alarm_probability)+std::log(t)+std::log(t+1)+std::log(2.);
    }
    void remember(PatternEvidence evidence) {
        if(evidence.score<search.retain_score)return;
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
    void publish(Hypothesis& h) {
        if(!h.admitted || !h.committed)return;
        room_for(h.burst.bits.size());
        auto result=h.burst;result.bits.resize(h.committed);result.complete=true;
        result.end_sample=h.committed_end;result.score=h.committed_score;
        const auto same=std::find_if(bursts.begin(),bursts.end(),[&](const auto& b) {
            const auto difference=b.first_sample>h.burst.first_sample?b.first_sample-h.burst.first_sample:h.burst.first_sample-b.first_sample;
            return b.first_stream_symbol==h.burst.first_stream_symbol && difference<code.symbol_samples()/3 &&
                std::abs(b.frequency_hz-h.burst.frequency_hz)<=.5*config.sample_rate/static_cast<double>(code.symbol_samples());
        });
        if(same!=bursts.end()) {
            if(result.score>same->score){accounted_bytes=accounted_bytes-same->bits.capacity()+result.bits.capacity();*same=std::move(result);}
            return;
        }
        if(bursts.size()==search.track_limit) {
            const auto weakest=std::min_element(bursts.begin(),bursts.end(),[](const auto& a,const auto& b){return a.score<b.score;});
            if(weakest->score>=result.score)return;
            accounted_bytes=accounted_bytes-weakest->bits.capacity()+result.bits.capacity();*weakest=std::move(result);
        } else {accounted_bytes+=result.bits.capacity();bursts.push_back(std::move(result));}
    }
    void clear(Hypothesis& h) {
        h.burst.bits.clear();h.burst.complete=false;h.admitted=h.pending_gaps=false;
        h.sum_score=h.pending_score=h.committed_score=0;h.committed=0;
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
        if(!h.pending_gaps)
            std::fill(h.burst.bits.begin()+static_cast<std::ptrdiff_t>(h.committed),h.burst.bits.end(),missing_pattern_bit);
        h.sum_score=h.committed_score;h.pending_score=0;h.pending_gaps=true;
    }
    bool gap_expired(const Hypothesis& h,std::size_t additional=0) const {
        const auto pending=static_cast<long double>(h.burst.bits.size()-h.committed)+additional;
        const auto symbol=static_cast<long double>(code.symbol_samples())/h.rate;
        return pending>=2 && pending*symbol>static_cast<long double>(search.max_gap_seconds)*config.sample_rate;
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
        const auto standalone=e.score>=threshold();
        // A timing placeholder cannot disambiguate an unresolved stream
        // schedule. Resume that search from independently observed evidence.
        if(h.pending_gaps && group_count!=1) {publish(h);clear(h);}
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
        const auto can_preserve=search.preserve_symbol_gaps && h.admitted && group_count==1;
        auto preserve_gap=can_preserve &&
            (h.pending_gaps || !reliable || (standalone && pending_count && next_chain<threshold()));
        // A weak retained tail can also enter gap mode when its ordinary
        // evidence timeout fires before the rate-corrected timing timeout.
        const auto convert_weak_tail=can_preserve && reliable && !standalone && next_chain<threshold() &&
            next_count>=2 && static_cast<long double>(next_count)*code.symbol_samples()>
                static_cast<long double>(search.max_gap_seconds)*config.sample_rate && !gap_expired(h,1);
        if((preserve_gap || convert_weak_tail) && !h.pending_gaps && search.packet_complete &&
           search.packet_complete(h.burst,h.committed,config,search.packet_content_limit)) {
            // Packet validation only closes an already admitted buffer. The
            // current observation still follows the ordinary evidence gates.
            publish(h);clear(h);preserve_gap=false;
        }
        if(preserve_gap) {
            // Retain only the admitted clock and symbol positions. Missing
            // slots provide neither bit evidence nor confidence to the track.
            if(h.burst.bits.size()==bit_limit && !(reliable && standalone)) {publish(h);clear(h);}
            else {
                mark_pending_gaps(h);
                append(h,reliable && standalone?static_cast<std::uint8_t>(e.bit):missing_pattern_bit);
                h.burst.end_sample=end;
                if(reliable && standalone) {
                    h.sum_score+=e.score;h.committed=h.burst.bits.size();h.committed_end=end;
                    h.committed_score=h.sum_score;h.pending_gaps=false;
                } else if(gap_expired(h)) {publish(h);clear(h);}
            }
            h.burst.score=h.committed_score;
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
                    h.burst.first_sample=h.observed_start;h.burst.first_stream_symbol=h.index;
                    h.burst.stream_phase_samples=h.phase_lower;
                    h.burst.frequency_hz=e.frequency_hz;h.sum_score=h.pending_score=h.committed_score=0;h.committed=0;
                }
                append(h,static_cast<std::uint8_t>(e.bit));h.burst.end_sample=end;h.sum_score+=e.score;h.pending_score+=e.score;
                const auto pending=h.burst.bits.size()-h.committed;
                const auto n=static_cast<double>(pending);
                const auto chain=h.pending_score>n?h.pending_score-n-n*std::log(h.pending_score/n)-n*std::log(2.):0;
                if((n==1 && standalone) || chain>=threshold()) {
                    h.admitted=true;h.committed=h.burst.bits.size();h.committed_end=end;
                    h.committed_score=h.sum_score;h.pending_score=0;
                } else if(pending>=2 && static_cast<long double>(pending)*code.symbol_samples()>
                        static_cast<long double>(search.max_gap_seconds)*config.sample_rate) {
                    // Expire only this unconfirmed chain. The fixed clock
                    // hypotheses still examine later symbols independently.
                    if(search.preserve_symbol_gaps && h.admitted && group_count==1 && !gap_expired(h))mark_pending_gaps(h);
                    else {publish(h);clear(h);}
                }
                h.burst.score=h.committed_score;
            } else {publish(h);clear(h);}
        }
        h.fits={};
        for(std::size_t group=0;group<alternate_groups;++group)
            alternate_fits[hypothesis*alternate_groups+group]={};
        require(h.index<std::numeric_limits<std::uint64_t>::max(),"pattern stream symbol counter overflow");++h.index;
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
        for(std::size_t hypothesis=0;hypothesis<hypotheses.size();++hypothesis) {
            auto& h=hypotheses[hypothesis];
            cancelled(stop);auto cursor=sample;
            if(h.origin>static_cast<long double>(cursor))cursor=static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(h.origin)));
            while(cursor<end) {
                const auto symbol_start=h.origin+static_cast<long double>(h.index)*code.symbol_samples()/h.rate;
                const auto symbol_end=h.origin+(static_cast<long double>(h.index)+1)*code.symbol_samples()/h.rate;
                if(static_cast<long double>(cursor)>=symbol_end) { complete(h,hypothesis,cursor);continue; }
                const auto segment_end=static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(symbol_end)));
                std::size_t group_count=0;
                const auto groups=phase_groups(h,group_count);
                if(!h.fits[0].count)h.observed_start=cursor;
                for(std::size_t group=0;group<group_count;++group) {
                    code.set_stream_phase_samples(groups[group].lower);
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
                            for(unsigned bit=0;bit<2;++bit)
                                fit[bit].add(projection,code.shaped_value(first_chip,bit,static_cast<double>(within)),1);
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
                            auto phase=code.value(chip,bit,fraction);
                            if(config.spreading_mode==SpreadingMode::tone) {
                                const auto tone=bank.frequency-config.carrier_hz-search.frequency_offsets_hz[h.frequency];
                                phase*=std::polar(1.,-static_cast<double>(std::remainder(static_cast<long double>(observed)*tau*tone/config.sample_rate,static_cast<long double>(tau))));
                            }
                            fit[bit].add(bank.prefix[right]-bank.prefix[left],phase,right-left);
                        }
                        observed=until;
                    }
                }
                cursor=segment_end;
                if(static_cast<long double>(cursor)>=symbol_end)complete(h,hypothesis,cursor);
            }
        }
        const auto& measured=banks.front().prefix[input.size()];
        const Complex point{measured.xc,measured.xs};
        const auto delta=std::abs(previous_point)>1e-20?point*std::conj(previous_point)/std::abs(previous_point):point;
        if(point_count==points.size()){point_begin=(point_begin+1)%points.size();--point_count;}
        points[(point_begin+point_count++)%points.size()]=delta;previous_point=point;
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
    for(std::size_t offset=0;offset<samples.size();offset+=std::min(s.block_samples,samples.size()-offset))
        s.process(samples.subspan(offset,std::min(s.block_samples,samples.size()-offset)),stop);
}
void PatternCorrelator::finish(std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.finished)return;
    for(auto& h:s.hypotheses) {s.publish(h);h.admitted=false;}
    s.finished=true;
}
std::vector<PatternBurst> PatternCorrelator::take_bursts(){
    auto& s=*impl_;auto result=std::move(s.bursts);s.bursts={};s.bursts.reserve(s.search.track_limit);
    s.accounted_bytes=s.working_bytes();return result;
}
PatternBurst PatternCorrelator::provisional()const {
    const auto& h=impl_->hypotheses;
    const auto best=std::max_element(h.begin(),h.end(),[](const auto& a,const auto& b){return (a.admitted?a.sum_score:-1)<(b.admitted?b.sum_score:-1);});
    if(best==h.end() || !best->admitted)return {};
    auto result=best->burst;result.bits.resize(best->committed);result.end_sample=best->committed_end;
    result.complete=best->pending_gaps;return result;
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
bool PatternCorrelator::acquiring()const {return !impl_->finished && std::any_of(impl_->hypotheses.begin(),impl_->hypotheses.end(),[](const auto& h){return !h.burst.bits.empty();});}
bool PatternCorrelator::synchronized()const{return std::any_of(impl_->hypotheses.begin(),impl_->hypotheses.end(),[](const auto& h){return h.admitted;});}
std::size_t PatternCorrelator::working_bytes()const{return sizeof(PatternCorrelator)+impl_->working_bytes();}
void PatternCorrelator::set_workspace_bytes(std::size_t bytes) {
    require(bytes>=working_bytes(),"DSP workspace is smaller than streaming pattern state");impl_->budget=bytes;
}
Diagnostics PatternCorrelator::diagnostics()const {
    const auto burst=provisional();Diagnostics result;result.bit_rate=static_cast<double>(impl_->config.sample_rate)/static_cast<double>(impl_->code.symbol_samples());
    result.sample_offset=static_cast<std::size_t>(burst.first_sample);result.pattern_score=burst.score;return result;
}
}
