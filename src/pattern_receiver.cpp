#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_correlator.hpp"
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
void cancelled(std::stop_token stop) { if(stop.stop_requested()) throw Error("pattern search cancelled"); }
void fft(std::vector<Complex>& a, bool inverse, std::stop_token stop) {
    const auto n=a.size();
    for(std::size_t i=1,j=0;i<n;++i) {
        auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j)std::swap(a[i],a[j]);
    }
    for(std::size_t length=2;length<=n;length*=2) {
        cancelled(stop);const auto step=std::polar(1.,(inverse?tau:-tau)/static_cast<double>(length));
        for(std::size_t begin=0;begin<n;begin+=length) {
            Complex w{1,0};
            for(std::size_t j=0;j<length/2;++j) {
                const auto u=a[begin+j],v=a[begin+j+length/2]*w;
                a[begin+j]=u+v;a[begin+j+length/2]=u-v;w*=step;
            }
        }
        if(length==n)break;
    }
    if(inverse)for(auto& value:a)value/=static_cast<double>(n);
}
double evidence(Complex dot,double energy,double template_energy,double count,double condition,bool real_rank,
                bool exact_real,Complex template_square={}) {
    if(count<4 || energy<=1e-30 || template_energy<=1e-30)return 0;
    auto fitted=std::norm(dot)/(template_energy*condition);
    if(exact_real) {
        // Individual real samples fit two real carrier bases. Their exact
        // Gram matrix is encoded by sum(|template|^2) and sum(template^2).
        // The trace-only bound loses half the energy even for an exact fit.
        const auto determinant=template_energy*template_energy-std::norm(template_square);
        if(determinant>1e-12*template_energy*template_energy)
            fitted=2*(template_energy*std::norm(dot)-(template_square*dot*dot).real())/determinant;
    }
    const auto fraction=std::clamp(fitted/energy,0.,1.-1e-15);
    // Nonsingular quadrature bins use a covariance-eigenratio bound. Individual
    // real samples use the exact rank-two fit above; an ill-conditioned Gram
    // matrix retains the conservative lambda_max<=trace bound. These scores
    // assume independent Gaussian input samples, with unknown common variance.
    return -(real_rank?(count-2)/2:count-1)*std::log1p(-fraction);
}
std::size_t power_two(std::size_t value) {
    std::size_t result=1;
    while(result<value) {
        if(result>std::numeric_limits<std::size_t>::max()/2)throw Error("pattern transform size overflow");
        result*=2;
    }
    return result;
}
}
struct PatternReceiver::Impl {
    Config config;
    PatternSearch search;
    PatternCode code;
    std::unique_ptr<PatternCorrelator> fallback;
    std::size_t budget=0,fixed_reservation=0,configured_bit_limit=0,bin_samples=0,length=0,transform=0,hop=0;
    std::vector<Complex> ring,work,spectrum,product,reference;
    std::vector<double> energy_prefix;
    std::vector<std::array<std::vector<Complex>,2>> templates;
    std::vector<std::array<double,2>> template_energy;
    std::vector<std::array<Complex,2>> template_square;
    std::uint64_t prepared_template_index=0;
    bool templates_valid=false;
    std::uint64_t sample=0,bins=0,next_start=0;
    Complex sum{},oscillator{1,0},rotation{},previous_chip{};
    double noise_condition=1;
    bool real_rank=false,sample_fit=false;
    std::size_t partial=0;
    bool finished=false,oscillator_valid=true;
    std::uint64_t trials=0;
    std::vector<PatternEvidence> history,peaks;
    struct Completed {std::uint64_t first=0,end=0;double frequency=0;};
    std::vector<Completed> completed;
    std::vector<PatternBurst> bursts;
    std::vector<Complex> points;
    std::size_t point_cursor=0;
    struct Track {
        PatternBurst burst;
        std::uint64_t next=0,index=1;
        double total_score=0,penalty=0;
        double pending_score=0,confirmed_score=0;
        std::size_t confirmed=0;
        std::uint64_t confirmed_end=0;
        std::size_t frequency=0;
        bool admitted=false;
    };
    std::vector<Track> tracks;
    PatternBurst latest;

    Impl(Config c,std::size_t bytes,PatternSearch options)
        :config(c),search(std::move(options)),code(c,c.stream_epoch),budget(bytes) {
        validate(c);
        if(!c.pattern_symbols)throw Error("pattern receiver requires binary pattern transport");
        if(!std::isfinite(search.false_alarm_probability) || search.false_alarm_probability<=0 || search.false_alarm_probability>=1 ||
           !std::isfinite(search.retain_score) || search.retain_score<0 ||
           !search.candidate_limit || search.candidate_limit>65536 || !search.track_limit || search.track_limit>128 ||
           !search.bit_limit || !search.initial_stream_symbols || search.initial_stream_symbols>64)
            throw Error("invalid pattern search limits");
        if((search.start_offset_seconds&&!std::isfinite(*search.start_offset_seconds)) ||
           !std::isfinite(search.start_uncertainty_seconds) || search.start_uncertainty_seconds<0)
            throw Error("invalid pattern system-clock window");
        if(search.clock_errors_ppm.empty() || search.clock_errors_ppm.size()>65)
            throw Error("clock-rate bank must contain 1..65 hypotheses");
        for(auto ppm:search.clock_errors_ppm)
            if(!std::isfinite(ppm)||std::abs(ppm)>10000)throw Error("clock-rate hypotheses must fit +/-10000 ppm");
        configured_bit_limit=search.bit_limit;
        if(!c.scramble&&!c.dsss)search.initial_stream_symbols=1;
        const auto symbols=code.symbol_samples();
        // Chip and symbol boundaries must fall on bin boundaries. Rounding a
        // symbol to whole bins would accumulate timing drift and count edges
        // of adjacent symbols twice, especially with partial final chips.
        bin_samples=static_cast<std::size_t>(std::gcd(std::gcd(code.chip_samples(),symbols),
            std::max<std::uint64_t>(1,code.chip_samples()/2)));
        // Short symbols have too little evidence to tolerate bins that mix
        // adjacent chips. Keep their exact sample timing within a bounded FFT.
        if(symbols<=256 && !c.scramble && !c.dsss)bin_samples=1;
        sample_fit=bin_samples==1 && !c.scramble && !c.dsss;
        const auto omega=tau*c.carrier_hz/c.sample_rate;
        const auto sine=std::sin(omega);
        const auto image=std::abs(sine)>1e-12?std::abs(std::sin(static_cast<double>(bin_samples)*omega)/sine):static_cast<double>(bin_samples);
        const auto small=static_cast<double>(bin_samples)-image;
        real_rank=small<=1e-10*static_cast<double>(bin_samples);
        if(!real_rank)noise_condition=(static_cast<double>(bin_samples)+image)/small;
        if(symbols/bin_samples>std::numeric_limits<std::size_t>::max()-2)throw Error("pattern integration exceeds address space");
        length=static_cast<std::size_t>(symbols/bin_samples+(symbols%bin_samples!=0));
        if(length<4)length=4;
        if(length>std::numeric_limits<std::size_t>::max()/4)throw Error("pattern integration exceeds address space");
        transform=power_two(2*length);hop=transform-length+1;
        if(search.frequency_offsets_hz.empty()) {
            const auto step=.25*c.sample_rate/static_cast<double>(symbols);
            search.frequency_offsets_hz={0,-step,step,-2*step,2*step};
        }
        if(search.frequency_offsets_hz.size()>65)throw Error("pattern frequency bank exceeds 65 hypotheses");
        for(auto offset:search.frequency_offsets_hz) {
            if(!std::isfinite(offset) || std::abs(offset)>c.bandwidth_hz/8)
                throw Error("pattern carrier offsets must fit within bandwidth/8");
        }
        // Transform and baseband history allocations are checked before any
        // allocation. The RAM dropdown is a ceiling, not an allocation target.
        const auto arrays=5+2*search.frequency_offsets_hz.size();
        const long double required=sizeof(Impl)+static_cast<long double>(transform)*sizeof(Complex)*arrays+
            static_cast<long double>(4*length+2*hop)*sizeof(Complex)+
            static_cast<long double>(transform+1)*sizeof(double)+
            static_cast<long double>(2*search.candidate_limit)*sizeof(PatternEvidence)+
            static_cast<long double>(search.track_limit)*(sizeof(Track)+sizeof(PatternBurst)+sizeof(Completed))+
            static_cast<long double>(search.frequency_offsets_hz.size())*sizeof(decltype(templates)::value_type)+
            static_cast<long double>(search.frequency_offsets_hz.size())*sizeof(decltype(template_energy)::value_type)+
            static_cast<long double>(search.frequency_offsets_hz.size())*sizeof(decltype(template_square)::value_type)+
            static_cast<long double>(search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double)+
            4096*sizeof(Complex)+code.working_bytes()+sizeof(PatternReceiver);
        if(required>bytes && search.start_offset_seconds) {
            const auto wrapper=wrapper_bytes();
            if(wrapper>=bytes)throw Error("pattern workspace cannot retain correlator control state");
            fallback=std::make_unique<PatternCorrelator>(config,search,bytes-wrapper);
            return;
        }
        if(required>bytes)
            throw Error("pattern correlation and timing coverage exceed DSP workspace; reduce integration/bandwidth or increase the limit");
        fixed_reservation=static_cast<std::size_t>(required);
        // Active tracks, queued completions, latest completion and one
        // allocation/copy in flight can each retain an independent bit vector.
        search.bit_limit=std::min(search.bit_limit,(bytes-fixed_reservation)/(2*search.track_limit+2));
        if(!search.bit_limit)throw Error("pattern workspace cannot retain bit candidates");
        ring.resize(4*length+2*hop);work.resize(transform);spectrum.resize(transform);product.resize(transform);reference.resize(transform);
        energy_prefix.resize(transform+1);templates.resize(search.frequency_offsets_hz.size());
        template_energy.resize(templates.size());
        template_square.resize(templates.size());
        history.reserve(search.candidate_limit);peaks.reserve(search.candidate_limit);completed.reserve(search.track_limit);
        tracks.reserve(search.track_limit);points.reserve(2048);bursts.reserve(search.track_limit);
        rotation=std::polar(1.,-tau*c.carrier_hz/c.sample_rate);
        prepare_templates(0,{});
    }
    void prepare_templates(std::uint64_t index,std::stop_token stop) {
        if(templates_valid&&prepared_template_index==index)return;
        templates_valid=false;
        for(std::size_t f=0;f<templates.size();++f)for(unsigned bit=0;bit<2;++bit) {
            auto& row=templates[f][bit];row.resize(transform);
            std::fill(row.begin(),row.end(),Complex{});
            auto& norm=template_energy[f][bit];norm=0;
            auto& square=template_square[f][bit];square={};
            for(std::size_t i=0;i<length;++i) {
                const auto value=template_value(i,index,bit,f);
                row[length-1-i]=std::conj(value);norm+=std::norm(value);
                if(sample_fit)square+=value*value*carrier_square(i);
            }
            fft(row,false,stop);
        }
        prepared_template_index=index;templates_valid=true;
    }
    Complex template_value(std::size_t bin,std::uint64_t index,unsigned bit,std::size_t f) {
        const auto sample_position=static_cast<long double>(bin)*bin_samples+static_cast<long double>(bin_samples-1)/2;
        const auto chip_position=sample_position/code.chip_samples();
        const auto local=static_cast<std::uint64_t>(chip_position);
        if(index>(std::numeric_limits<std::uint64_t>::max()-local)/code.chips_per_symbol())throw Error("pattern stream coordinate overflow");
        const auto chip=index*code.chips_per_symbol()+local;
        const auto fraction=static_cast<double>(chip_position-local);
        return code.value(chip,bit,fraction)*std::polar(1.,tau*search.frequency_offsets_hz[f]*static_cast<double>(sample_position)/config.sample_rate);
    }
    Complex at(std::uint64_t i)const {
        if(i>=bins || bins-i>ring.size())throw Error("pattern observation expired before refinement");
        return ring[static_cast<std::size_t>(i%ring.size())];
    }
    Complex carrier_square(std::uint64_t bin)const {
        const auto phase=std::remainder(2*static_cast<long double>(tau)*config.carrier_hz*
            (static_cast<long double>(bin)*bin_samples+(bin_samples-1)/2.L)/config.sample_rate,
            static_cast<long double>(tau));
        return std::polar(1.,static_cast<double>(phase));
    }
    double evidence_count(std::size_t observed)const {
        const auto count=static_cast<double>(observed);
        // Finer timing must not turn a held wrong-key chip into many fresh
        // random observations. Preserve the former half-chip evidence scale
        // (two complex bins, or four real dimensions, per chip) while fitting
        // the waveform against every available sample.
        return sample_fit?std::min(count,4*count/static_cast<double>(code.chip_samples())):count;
    }
    double threshold()const {
        // Alpha-spending-style threshold under the reference noise model.
        // Actual PCM quadrature covariance and adaptive paths need calibration;
        // this tuning parameter is not a certified lifetime false-alarm bound.
        const auto t=static_cast<double>(trials)+1;
        return -std::log(search.false_alarm_probability)+2*std::log(t)+
            std::log(2.*static_cast<double>(templates.size()*search.initial_stream_symbols));
    }
    void remember(PatternEvidence item) {
        if(item.score<search.retain_score)return;
        if(history.size()==search.candidate_limit)history.erase(history.begin());
        history.push_back(item);
    }
    PatternEvidence measure(std::uint64_t start,std::uint64_t index,std::size_t f,std::uint64_t observed_before=0) {
        std::array<Complex,2> dot{},square{};std::array<double,2> norm{};double energy=0;
        const auto skip=static_cast<std::size_t>(observed_before>start?std::min<std::uint64_t>(length,observed_before-start):0);
        for(std::size_t i=skip;i<length;++i) {
            const auto value=at(start+i);energy+=std::norm(value);
            for(unsigned b=0;b<2;++b) {
                const auto pattern=template_value(i,index,b,f);
                dot[b]+=value*std::conj(pattern);norm[b]+=std::norm(pattern);
                if(sample_fit)square[b]+=pattern*pattern*carrier_square(start+i);
            }
        }
        const auto count=evidence_count(length-skip);
        const auto zero=evidence(dot[0],energy,norm[0],count,noise_condition,real_rank,sample_fit,square[0]),
            one=evidence(dot[1],energy,norm[1],count,noise_condition,real_rank,sample_fit,square[1]);
        return {start*bin_samples,(start+length)*bin_samples,index,
            config.carrier_hz+search.frequency_offsets_hz[f],std::max(zero,one),std::min(zero,one),one>zero?1U:0U};
    }
    void publish(Track& track,bool complete) {
        if(!track.admitted)return;
        track.burst.complete=complete;track.burst.score=track.confirmed_score;
        if(complete) {
            track.burst.bits.resize(track.confirmed);track.burst.end_sample=track.confirmed_end;
            latest=track.burst;
            if(bursts.size()==search.track_limit)bursts.erase(bursts.begin());
            bursts.push_back(track.burst);
            if(completed.size()==search.track_limit)completed.erase(completed.begin());
            completed.push_back({track.burst.first_sample,track.burst.end_sample,track.burst.frequency_hz});
        }
    }
    void append_bit(Bytes& bits,std::uint8_t bit) {
        if(bits.size()==search.bit_limit)throw Error("pattern burst exceeds bounded bit capacity");
        if(bits.size()==bits.capacity())bits.reserve(std::max<std::size_t>(1,
            bits.size()>search.bit_limit/2?search.bit_limit:2*bits.size()));
        bits.push_back(bit);
    }
    void continue_tracks(std::stop_token stop,bool final=false) {
        for(auto it=tracks.begin();it!=tracks.end();) {
            cancelled(stop);auto& track=*it;bool ended=false;
            while(track.next+length+(final?0:2)<=bins) {
                PatternEvidence best;best.score=-1;std::uint64_t chosen=track.next;
                const auto low=track.next>2?track.next-2:0;
                const auto high=std::min(track.next+2,bins-length);
                for(auto start=low;start<=high;++start) {
                    if(bins-start>ring.size())continue;
                    auto fit=measure(start,track.index,track.frequency,track.burst.end_sample/bin_samples);++trials;
                    if(fit.score>best.score){best=fit;chosen=start;}
                }
                remember(best);
                if(best.score<search.retain_score || best.score-best.alternative_score<1) {
                    publish(track,true);ended=true;break;
                }
                const auto standalone=best.score>=threshold();
                if(!track.admitted && standalone) {
                    // A confident new start cannot lend its evidence to an
                    // earlier unconfirmed noise candidate. Keep weak chains
                    // that already established their own confidence.
                    track.burst.bits.clear();track.burst.first_sample=best.first_sample;
                    track.burst.first_stream_symbol=best.stream_symbol;
                    track.total_score=track.pending_score=track.penalty=0;
                }
                append_bit(track.burst.bits,static_cast<std::uint8_t>(best.bit));track.burst.end_sample=best.end_sample;
                track.total_score+=best.score;track.pending_score+=best.score;track.penalty+=std::log(10.);
                const auto count=static_cast<double>(track.burst.bits.size()-track.confirmed);
                // Chernoff bound for the sum of independent Exp(1) null scores,
                // with a union penalty for bit/timing alternatives. Overlapping
                // bins at the start of this window were excluded by measure,
                // so an earlier timing correction never counts evidence twice.
                const auto bound=track.pending_score>count?track.pending_score-count-count*std::log(track.pending_score/count)-track.penalty:0;
                if(bound>=threshold() || standalone) {
                    track.admitted=true;track.confirmed=track.burst.bits.size();track.confirmed_end=best.end_sample;
                    track.confirmed_score=track.total_score;track.pending_score=0;track.penalty=0;
                }
                ++track.index;
                track.next=chosen+length;
                publish(track,false);
            }
            if(final && !ended) { publish(track,true);ended=true; }
            if(ended)it=tracks.erase(it);else ++it;
        }
    }
    void admit(const PatternEvidence& item,std::size_t f) {
        if(item.score<search.retain_score || item.score-item.alternative_score<1)return;
        remember(item);
        const auto same_frequency=[&](double frequency) {
            return std::abs(item.frequency_hz-frequency)<=static_cast<double>(config.sample_rate)/static_cast<double>(code.symbol_samples());
        };
        for(const auto& span:completed)
            if(same_frequency(span.frequency)&&item.first_sample<span.end&&item.end_sample>span.first)return;
        for(auto it=tracks.begin();it!=tracks.end();) {
            const auto& track=*it;
            const auto margin=2*bin_samples;
            if(same_frequency(track.burst.frequency_hz)&&item.first_sample+margin>=track.burst.first_sample && item.first_sample<=track.next*bin_samples+margin) {
                if(track.admitted && item.first_sample+margin<track.confirmed_end &&
                   item.score>track.total_score && item.score>=threshold()) {
                    // Admission is not a permanent timing lock. A stronger
                    // overlapping pattern replaces the weaker hypothesis;
                    // allow normal timing overlap between adjacent symbols.
                    it=tracks.erase(it);continue;
                }
                if(track.admitted && track.confirmed<track.burst.bits.size() &&
                   item.first_sample>=track.confirmed_end && item.score>=threshold()) {
                    // Confidence belongs only to the confirmed span. Weak
                    // pending extensions cannot veto an independently strong
                    // later start; finish the old span and consider this one.
                    publish(*it,true);it=tracks.erase(it);continue;
                }
                if(track.admitted||track.total_score>=item.score)return;
                it=tracks.erase(it);
            } else ++it;
        }
        if(tracks.size()==search.track_limit) {
            const auto worst=std::min_element(tracks.begin(),tracks.end(),[](const auto& a,const auto& b){return a.total_score<b.total_score;});
            if(worst->admitted || worst->total_score>=item.score)return;
            tracks.erase(worst);
        }
        Track track;append_bit(track.burst.bits,static_cast<std::uint8_t>(item.bit));
        track.burst.first_sample=item.first_sample;track.burst.end_sample=item.end_sample;track.burst.frequency_hz=item.frequency_hz;
        track.burst.first_stream_symbol=item.stream_symbol;track.index=item.stream_symbol+1;
        track.next=item.end_sample/bin_samples;track.frequency=f;track.total_score=item.score;track.penalty=std::log(2.);
        track.admitted=item.score>=threshold();
        if(track.admitted){track.confirmed=1;track.confirmed_end=item.end_sample;track.confirmed_score=item.score;track.penalty=0;}
        else track.pending_score=item.score;
        tracks.push_back(std::move(track));publish(tracks.back(),false);
    }
    void process(std::stop_token stop,bool final=false) {
        while(bins>=length+next_start && (final || bins-length+1-next_start>=hop)) {
            cancelled(stop);const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(hop,bins-length+1-next_start));
            std::fill(work.begin(),work.end(),Complex{});energy_prefix[0]=0;
            for(std::size_t i=0;i<count+length-1;++i) {
                work[i]=at(next_start+i);energy_prefix[i+1]=energy_prefix[i]+std::norm(work[i]);
            }
            spectrum=work;fft(spectrum,false,stop);
            peaks.clear();
            for(std::size_t stream_index=0;stream_index<search.initial_stream_symbols;++stream_index) {
            prepare_templates(stream_index,stop);
            for(std::size_t f=0;f<templates.size();++f) {
                for(unsigned b=0;b<2;++b) {
                    for(std::size_t i=0;i<transform;++i)product[i]=spectrum[i]*templates[f][b][i];
                    fft(product,true,stop);
                    for(std::size_t j=0;j<count;++j) {
                        const auto score=evidence(product[length-1+j],energy_prefix[j+length]-energy_prefix[j],
                            template_energy[f][b],evidence_count(length),noise_condition,real_rank,
                            sample_fit,sample_fit?template_square[f][b]*carrier_square(next_start+j):Complex{});
                        if(b==0)reference[j]={score,0};else reference[j].imag(score);
                    }
                }
                for(std::size_t j=0;j<count;++j) {
                    ++trials;const auto a=reference[j].real(),b=reference[j].imag();
                    if(std::max(a,b)<search.retain_score)continue;
                    PatternEvidence item{(next_start+j)*bin_samples,(next_start+j+length)*bin_samples,stream_index,
                        config.carrier_hz+search.frequency_offsets_hz[f],std::max(a,b),std::min(a,b),b>a?1U:0U};
                    const auto existing=std::find_if(peaks.begin(),peaks.end(),[&](const auto& p){
                        const auto distance=p.first_sample>item.first_sample?p.first_sample-item.first_sample:item.first_sample-p.first_sample;
                        return distance<std::max<std::uint64_t>(1,code.symbol_samples()/2) &&
                            std::abs(p.frequency_hz-item.frequency_hz)<=static_cast<double>(config.sample_rate)/static_cast<double>(code.symbol_samples());
                    });
                    if(existing!=peaks.end()) { if(item.score>existing->score)*existing=item; }
                    else if(peaks.size()<search.candidate_limit)peaks.push_back(item);
                }
            }
            }
            std::sort(peaks.begin(),peaks.end(),[](const auto& a,const auto& b){return a.first_sample<b.first_sample;});
            for(const auto& peak:peaks) {
                // Existing tracks must consume available observations before
                // overlap checks: an FFT hop can expose a later symbol while
                // its established track still points to the preceding one.
                continue_tracks(stop);
                const auto f=static_cast<std::size_t>(std::min_element(search.frequency_offsets_hz.begin(),search.frequency_offsets_hz.end(),[&](double a,double b){
                    return std::abs(config.carrier_hz+a-peak.frequency_hz)<std::abs(config.carrier_hz+b-peak.frequency_hz);
                })-search.frequency_offsets_hz.begin());
                admit(peak,f);
            }
            next_start+=count;continue_tracks(stop);
        }
        if(final)continue_tracks(stop,true);
    }
    void bin(Complex value,std::stop_token stop) {
        ring[static_cast<std::size_t>(bins%ring.size())]=value;++bins;
        const auto point=std::abs(previous_chip)>1e-20?value*std::conj(previous_chip)/std::abs(previous_chip):value;
        if(points.size()==2048){points[point_cursor]=point;point_cursor=(point_cursor+1)%points.size();}
        else points.push_back(point);
        previous_chip=value;
        process(stop);
    }
    std::vector<Complex> chip_points()const {
        std::vector<Complex> result;result.reserve(points.size());
        for(std::size_t i=0;i<points.size();++i)result.push_back(points[(point_cursor+i)%points.size()]);
        return result;
    }
    const Track* active_track()const {
        const Track* best=nullptr;
        for(const auto& track:tracks)if(track.admitted&&(!best||track.total_score>best->total_score))best=&track;
        return best;
    }
    const PatternBurst& current_burst()const {
        const auto* track=active_track();return track?track->burst:latest;
    }
    PatternBurst provisional()const {
        const auto* track=active_track();
        if(!track)return latest;
        PatternBurst result=track->burst;result.bits.resize(track->confirmed);result.end_sample=track->confirmed_end;
        return result;
    }
    std::size_t working_bytes()const {
        if(fallback)return wrapper_bytes()+fallback->working_bytes();
        std::size_t total=sizeof(Impl)+sizeof(PatternReceiver)+code.working_bytes();
        for(const auto* v:{&ring,&work,&spectrum,&product,&reference,&points})total+=v->capacity()*sizeof(Complex);
        total+=energy_prefix.capacity()*sizeof(double)+(history.capacity()+peaks.capacity())*sizeof(PatternEvidence)+
            tracks.capacity()*sizeof(Track)+bursts.capacity()*sizeof(PatternBurst)+completed.capacity()*sizeof(Completed)+
            templates.capacity()*sizeof(decltype(templates)::value_type)+
            template_energy.capacity()*sizeof(decltype(template_energy)::value_type)+
            template_square.capacity()*sizeof(decltype(template_square)::value_type)+
            (search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double);
        for(const auto& row:templates)for(const auto& v:row)total+=v.capacity()*sizeof(Complex);
        for(const auto& track:tracks)total+=track.burst.bits.capacity();
        for(const auto& burst:bursts)total+=burst.bits.capacity();
        return total+latest.bits.capacity();
    }
    std::size_t wrapper_bytes()const {
        return sizeof(Impl)+sizeof(PatternReceiver)+code.working_bytes()+
            search.frequency_offsets_hz.capacity()*sizeof(double)+search.clock_errors_ppm.capacity()*sizeof(double);
    }
    void set_workspace_bytes(std::size_t bytes) {
        if(fallback) {
            const auto wrapper=wrapper_bytes();
            if(wrapper>=bytes)throw Error("pattern workspace cannot retain correlator control state");
            fallback->set_workspace_bytes(bytes-wrapper);budget=bytes;return;
        }
        if(bytes<fixed_reservation||working_bytes()>bytes)throw Error("pattern workspace cannot retain current correlation state");
        const auto limit=std::min(configured_bit_limit,(bytes-fixed_reservation)/(2*search.track_limit+2));
        if(!limit)throw Error("pattern workspace cannot retain bit candidates");
        const auto fits=[&](const Bytes& bits){return bits.capacity()<=limit;};
        if(!fits(latest.bits)||std::any_of(tracks.begin(),tracks.end(),[&](const auto& track){return !fits(track.burst.bits);})||
            std::any_of(bursts.begin(),bursts.end(),[&](const auto& burst){return !fits(burst.bits);}))
            throw Error("pattern workspace cannot retain existing bit candidates");
        search.bit_limit=limit;budget=bytes;
    }
};
PatternReceiver::PatternReceiver(Config c,std::size_t bytes,PatternSearch search):impl_(std::make_unique<Impl>(c,bytes,std::move(search))){}
PatternReceiver::~PatternReceiver()=default;
PatternReceiver::PatternReceiver(PatternReceiver&&) noexcept=default;
PatternReceiver& PatternReceiver::operator=(PatternReceiver&&) noexcept=default;
void PatternReceiver::push(std::span<const float> input,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.finished)throw Error("pattern capture already finished");
    if(s.fallback){s.fallback->push(input,stop);return;}
    if(!s.oscillator_valid) {
        s.oscillator=std::polar(1.,static_cast<double>(std::remainder(-static_cast<long double>(tau)*s.config.carrier_hz*s.sample/s.config.sample_rate,
                                                                   static_cast<long double>(tau))));
        s.oscillator_valid=true;
    }
    for(auto x:input) {
        if((s.sample&4095U)==0)cancelled(stop);
        if(!std::isfinite(x))throw Error("pattern input contains a nonfinite sample");
        s.sum+=static_cast<double>(x)*s.oscillator;s.oscillator*=s.rotation;++s.sample;
        if((s.sample&65535U)==0)s.oscillator/=std::abs(s.oscillator);
        if(++s.partial==s.bin_samples) {
            s.bin(s.sum/static_cast<double>(s.partial),stop);s.sum={};s.partial=0;
        }
    }
}
void PatternReceiver::push(std::span<const float> input,std::span<const std::complex<double>> projected,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.finished)throw Error("pattern capture already finished");
    if(input.size()!=projected.size())throw Error("shared pattern projection length mismatch");
    if(s.fallback){s.fallback->push(input,stop);return;}
    if(s.sample_fit) {
        // The real Gram fit needs a known carrier phase convention. Shared
        // projections may have an arbitrary fixed rotation, so reconstruct
        // these short/sample-resolution observations from their raw samples.
        for(const auto value:projected)
            if(!std::isfinite(value.real())||!std::isfinite(value.imag()))
                throw Error("pattern input contains a nonfinite sample");
        push(input,stop);return;
    }
    s.oscillator_valid=false;
    for(std::size_t i=0;i<input.size();++i) {
        if((s.sample&4095U)==0)cancelled(stop);
        if(!std::isfinite(input[i])||!std::isfinite(projected[i].real())||!std::isfinite(projected[i].imag()))
            throw Error("pattern input contains a nonfinite sample");
        s.sum+=projected[i];++s.sample;
        if(++s.partial==s.bin_samples){s.bin(s.sum/static_cast<double>(s.partial),stop);s.sum={};s.partial=0;}
    }
}
void PatternReceiver::finish(std::stop_token stop) {
    auto& s=*impl_;if(s.finished)return;cancelled(stop);
    if(s.fallback)s.fallback->finish(stop);else s.process(stop,true);
    s.finished=true;
}
std::vector<PatternBurst> PatternReceiver::take_bursts(){
    auto& s=*impl_;if(s.fallback)return s.fallback->take_bursts();
    std::vector<PatternBurst> result;result.reserve(s.bursts.size());
    for(auto& burst:s.bursts)result.push_back(std::move(burst));
    s.bursts.clear();return result;
}
PatternBurst PatternReceiver::provisional()const{return impl_->fallback?impl_->fallback->provisional():impl_->provisional();}
std::vector<PatternEvidence> PatternReceiver::candidates()const{return impl_->fallback?impl_->fallback->candidates():impl_->history;}
std::vector<PatternEvidence> PatternReceiver::candidates(std::size_t limit)const{
    if(impl_->fallback)return impl_->fallback->candidates(limit);
    const auto& history=impl_->history;
    return {history.end()-static_cast<std::ptrdiff_t>(std::min(limit,history.size())),history.end()};
}
std::vector<Complex> PatternReceiver::take_chip_constellation(){if(impl_->fallback)return impl_->fallback->take_chip_constellation();auto result=impl_->chip_points();impl_->points.clear();impl_->point_cursor=0;return result;}
bool PatternReceiver::acquiring()const{return impl_->fallback?impl_->fallback->acquiring():!impl_->tracks.empty();}
bool PatternReceiver::synchronized()const{return impl_->fallback?impl_->fallback->synchronized():std::any_of(impl_->tracks.begin(),impl_->tracks.end(),[](const auto& track){return track.admitted;});}
bool PatternReceiver::clock_windowed()const{return static_cast<bool>(impl_->fallback);}
std::size_t PatternReceiver::working_bytes()const {
    return impl_->working_bytes();
}
void PatternReceiver::set_workspace_bytes(std::size_t bytes){impl_->set_workspace_bytes(bytes);}
Diagnostics PatternReceiver::diagnostics()const {
    if(impl_->fallback)return impl_->fallback->diagnostics();
    const auto& s=*impl_;const auto& burst=s.current_burst();Diagnostics d;d.bit_rate=bit_rate(s.config);d.sample_offset=static_cast<std::size_t>(burst.first_sample);
    if(!burst.bits.empty())d.pattern_score=burst.score;
    d.constellation=s.chip_points();return d;
}
}
