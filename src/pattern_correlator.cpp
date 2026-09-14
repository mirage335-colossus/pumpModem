#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

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
    std::array<Projection,block_size+1> prefix{};
};
}

struct PatternCorrelator::Impl {
    Config config;
    PatternSearch search;
    PatternCode code;
    std::size_t budget=0,bit_limit=0,accounted_bytes=0;
    std::uint64_t sample=0,trials=0;
    bool finished=false,shaped=false;
    struct Hypothesis {
        long double origin=0,rate=1;
        std::uint64_t index=0,observed_start=0;
        std::size_t frequency=0,rate_index=0;
        std::array<Fit,2> fits{};
        PatternBurst burst;
        double sum_score=0,pending_score=0,committed_score=0;
        std::size_t committed=0;
        std::uint64_t committed_end=0;
        bool admitted=false;
    };
    std::vector<Hypothesis> hypotheses;
    std::vector<Bank> banks;
    std::vector<PatternEvidence> history;
    std::vector<PatternBurst> bursts;
    std::array<Complex,2048> points{};
    std::size_t point_begin=0,point_count=0;
    Complex previous_point{};

    Impl(Config c,PatternSearch options,std::size_t bytes):config(c),search(std::move(options)),code(c,c.stream_epoch),budget(bytes) {
        validate(c);
        shaped=pattern_pulse_enabled(c);
        require(c.pattern_symbols,"streaming pattern correlator requires binary pattern transport");
        require(search.start_offset_seconds && std::isfinite(*search.start_offset_seconds) &&
                std::isfinite(search.start_uncertainty_seconds) && search.start_uncertainty_seconds>=0,
                "long pattern correlation requires a finite system-clock start window");
        require(std::isfinite(search.false_alarm_probability) && search.false_alarm_probability>0 && search.false_alarm_probability<1 &&
                std::isfinite(search.retain_score) && search.retain_score>=0 && search.bit_limit &&
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
        const long double fixed=sizeof(PatternCorrelator)+sizeof(Impl)+code.working_bytes()+total*sizeof(Hypothesis)+bank_count*sizeof(Bank)+
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
            history.capacity()*sizeof(PatternEvidence)+bursts.capacity()*sizeof(PatternBurst)+
            (search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double);
        for(const auto& h:hypotheses)value+=h.burst.bits.capacity();
        for(const auto& burst:bursts)value+=burst.bits.capacity();
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
    void complete(Hypothesis& h,std::uint64_t end) {
        const auto a=h.fits[0].score(),b=h.fits[1].score();
        require(trials<std::numeric_limits<std::uint64_t>::max(),"pattern trial counter overflow");++trials;
        PatternEvidence e{h.observed_start,end,h.index,config.carrier_hz+search.frequency_offsets_hz[h.frequency],
            std::max(a,b),std::min(a,b),b>a?1U:0U};
        remember(e);
        // An individually confident symbol starts its own burst unless the
        // preceding weak chain already established confidence independently.
        if(!h.admitted && e.score>=threshold())h.burst.bits.clear();
        if(h.burst.bits.size()==bit_limit && e.score>=search.retain_score)
            throw Error("pattern bit retention limit reached");
        if(e.score>=search.retain_score && e.score-e.alternative_score>=1 && h.burst.bits.size()<bit_limit) {
            if(h.burst.bits.empty()) {
                h.burst.first_sample=h.observed_start;h.burst.first_stream_symbol=h.index;
                h.burst.frequency_hz=e.frequency_hz;h.sum_score=h.pending_score=h.committed_score=0;h.committed=0;
            }
            if(h.burst.bits.size()==h.burst.bits.capacity()) {
                const auto capacity=h.burst.bits.capacity();const auto wanted=std::min(bit_limit,std::max<std::size_t>(1,capacity*2));
                room_for(wanted);h.burst.bits.reserve(wanted);accounted_bytes+=h.burst.bits.capacity()-capacity;
            }
            h.burst.bits.push_back(static_cast<std::uint8_t>(e.bit));h.burst.end_sample=end;h.sum_score+=e.score;h.pending_score+=e.score;
            const auto n=static_cast<double>(h.burst.bits.size()-h.committed);
            const auto chain=h.pending_score>n?h.pending_score-n-n*std::log(h.pending_score/n)-n*std::log(2.):0;
            if((n==1 && e.score>=threshold()) || chain>=threshold()) {
                h.admitted=true;h.committed=h.burst.bits.size();h.committed_end=end;
                h.committed_score=h.sum_score;h.pending_score=0;
            }
            h.burst.score=h.committed_score;
        } else {
            publish(h);h.burst.bits.clear();h.burst.complete=false;h.admitted=false;
            h.sum_score=h.pending_score=h.committed_score=0;h.committed=0;
        }
        h.fits={};
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
        for(auto& h:hypotheses) {
            cancelled(stop);auto cursor=sample;
            if(h.origin>static_cast<long double>(cursor))cursor=static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(h.origin)));
            while(cursor<end) {
                const auto symbol_start=h.origin+static_cast<long double>(h.index)*code.symbol_samples()/h.rate;
                const auto symbol_end=h.origin+(static_cast<long double>(h.index)+1)*code.symbol_samples()/h.rate;
                if(static_cast<long double>(cursor)>=symbol_end) { complete(h,cursor);continue; }
                const auto within=std::max(0.L,(static_cast<long double>(cursor)-symbol_start)*h.rate);
                if(shaped) {
                    require(h.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),
                            "pattern chip coordinate overflow");
                    const auto first_chip=h.index*code.chips_per_symbol();
                    const auto left=static_cast<std::size_t>(cursor-sample);
                    const auto& bank=banks[h.frequency];
                    const auto projection=bank.prefix[left+1]-bank.prefix[left];
                    if(!h.fits[0].count)h.observed_start=cursor;
                    // Match the shaped symbol directly against independent raw
                    // real samples. Overlapping chip pulses change both fitted
                    // carrier bases, not N or the two-basis null distribution.
                    // Unknown adjacent bits are not used to acquire this symbol;
                    // their small boundary overlap remains in its residual.
                    for(unsigned bit=0;bit<2;++bit)
                        h.fits[bit].add(projection,code.shaped_value(first_chip,bit,static_cast<double>(within)),1);
                    ++cursor;
                    if(static_cast<long double>(cursor)>=symbol_end)complete(h,cursor);
                    continue;
                }
                const auto local=static_cast<std::uint64_t>(std::floor(within/code.chip_samples()));
                require(h.index<=(std::numeric_limits<std::uint64_t>::max()-local)/code.chips_per_symbol(),"pattern chip coordinate overflow");
                const auto chip=h.index*code.chips_per_symbol()+local;
                const auto fraction=std::clamp(static_cast<double>(within/code.chip_samples()-local),0.,std::nextafter(1.,0.));
                const auto chip_end=symbol_start+(static_cast<long double>(local)+1)*code.chip_samples()/h.rate;
                const auto boundary=std::min(static_cast<long double>(end),std::ceil(std::min(chip_end,symbol_end)));
                const auto until=static_cast<std::uint64_t>(std::max(static_cast<long double>(cursor+1),boundary));
                const auto left=static_cast<std::size_t>(cursor-sample),right=static_cast<std::size_t>(until-sample);
                if(!h.fits[0].count)h.observed_start=cursor;
                for(unsigned bit=0;bit<2;++bit) {
                    const auto bank_index=config.spreading_mode==SpreadingMode::tone?(h.rate_index*search.frequency_offsets_hz.size()+h.frequency)*2+bit:h.frequency;
                    const auto& bank=banks[bank_index];
                    auto phase=code.value(chip,bit,fraction);
                    if(config.spreading_mode==SpreadingMode::tone) {
                        const auto tone=bank.frequency-config.carrier_hz-search.frequency_offsets_hz[h.frequency];
                        phase*=std::polar(1.,-static_cast<double>(std::remainder(static_cast<long double>(cursor)*tau*tone/config.sample_rate,static_cast<long double>(tau))));
                    }
                    h.fits[bit].add(bank.prefix[right]-bank.prefix[left],phase,right-left);
                }
                cursor=until;
                if(static_cast<long double>(cursor)>=symbol_end)complete(h,cursor);
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
    for(std::size_t offset=0;offset<samples.size();offset+=std::min(block_size,samples.size()-offset))
        s.process(samples.subspan(offset,std::min(block_size,samples.size()-offset)),stop);
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
    auto result=best->burst;result.bits.resize(best->committed);result.end_sample=best->committed_end;return result;
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
