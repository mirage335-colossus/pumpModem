#include "acoustic_ofdm.hpp"
#include "datapump/fast/codec.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <iostream>

namespace datapump::fast::acoustic_ofdm {
namespace {
using C=std::complex<double>;
constexpr double pi=std::numbers::pi;
constexpr std::size_t training_blocks=16;
constexpr std::size_t signature_tones=128;
constexpr unsigned signature_errors=16;
Profile checked(Profile p) {
    validate(p);if(!p.acoustic_ofdm)throw Error("Acoustic OFDM engine requires its matching profile");return p;
}
void fft(std::span<C> a,bool inverse=false) {
    const auto n=a.size();
    for(std::size_t i=1,j=0;i<n;++i) {
        auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j)std::swap(a[i],a[j]);
    }
    for(std::size_t length=2;length<=n;length*=2) {
        const auto step=std::polar(1.,(inverse?2:-2)*pi/length);
        for(std::size_t start=0;start<n;start+=length) {
            C w=1.;
            for(std::size_t j=0;j<length/2;++j) {
                const auto u=a[start+j],v=a[start+j+length/2]*w;
                a[start+j]=u+v;a[start+j+length/2]=u-v;w*=step;
            }
        }
        if(length==n)break;
    }
    if(inverse)for(auto& value:a)value/=static_cast<double>(n);
}
std::uint64_t mix(std::uint64_t x) {
    x+=0x9e3779b97f4a7c15ULL;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;return x^(x>>31);
}
C known(std::size_t bin,std::uint64_t block) {
    const auto v=mix(bin^mix(block+0x70696c6f747333ULL));
    constexpr double a=.7071067811865475244;
    return {(v&1)?a:-a,(v&2)?a:-a};
}
C qam(unsigned order,unsigned label) {
    const unsigned axis=std::countr_zero(order)/2,side=1u<<axis;
    unsigned i=label>>axis,q=label&(side-1);
    for(unsigned s=1;s<axis;s*=2){i^=i>>s;q^=q>>s;}
    const auto scale=std::sqrt(2.*(order-1)/3.);
    return {(2.*i+1-side)/scale,(2.*q+1-side)/scale};
}
double interpolation_weight(double coordinate) {
    constexpr std::size_t resolution=2048;
    static const auto table=[] {
        std::array<double,16*resolution+2> values{};
        for(std::size_t i=0;i<values.size();++i) {
            const auto t=static_cast<double>(i)/resolution;
            values[i]=(i?std::sin(pi*t)/(pi*t):1.)*(.42+.5*std::cos(pi*t/16)+.08*std::cos(2*pi*t/16));
        }
        return values;
    }();
    const auto x=std::abs(coordinate)*resolution;const auto i=static_cast<std::size_t>(x);
    return table[i]+(x-i)*(table[i+1]-table[i]);
}
struct Geometry {
    std::size_t n,cp,length,first,last,bps,cycle_bits,cycle_intervals,blocks;
    std::vector<std::size_t> data,track,verify,active;
    explicit Geometry(const Profile& p):n(p.ofdm_fft_size),cp(p.ofdm_prefix_samples),length(n+cp),
        first(static_cast<std::size_t>(std::ceil(p.ofdm_low_hz*n/p.sample_rate))),
        last(static_cast<std::size_t>(std::floor(p.ofdm_high_hz*n/p.sample_rate))),
        bps(std::countr_zero(p.constellation)),cycle_bits(0),cycle_intervals(fast::cycle_intervals(p)),blocks(0) {
        if(last<=first||last>=n/2||last-first+1<512)throw Error("Acoustic OFDM requires at least 512 active frequency bins");
        const auto stride=std::max<std::size_t>(2,std::min<std::size_t>(p.ofdm_pilot_stride,(last-first+1)/(2*signature_tones+2)));
        for(auto k=first;k<=last;++k) {
            active.push_back(k);
            if((k-first)%stride==0) {
                (((k-first)/stride)%2?verify:track).push_back(k);
            } else data.push_back(k);
        }
        if(verify.size()<signature_tones)throw Error("Acoustic OFDM has insufficient independent marker tones");
        cycle_bits=cycle_intervals*physical_interval_bits;
        blocks=(cycle_bits+data.size()*bps-1)/(data.size()*bps);
    }
};
void observe(const SymbolObserver& callback,C value) {if(callback)try{callback(static_cast<std::complex<float>>(value));}catch(...) {}}
std::uint64_t checked_product(std::uint64_t a,std::uint64_t b) {
    if(b&&a>std::numeric_limits<std::uint64_t>::max()/b)throw Error("Acoustic OFDM duration overflow");
    return a*b;
}
}

bool enabled(const Profile& p){return p.acoustic_ofdm;}
std::uint64_t transmission_samples(const Profile& p,std::size_t intervals) {
    validate(p);Geometry g(p);if(!intervals)return 0;
    if(intervals%g.cycle_intervals)throw Error("Acoustic OFDM requires complete fixed coding cycles");
    const auto cycles=intervals/g.cycle_intervals;
    const auto blocks=checked_product(cycles,g.blocks+1)-1;
    if(blocks>std::numeric_limits<std::uint64_t>::max()-training_blocks)throw Error("Acoustic OFDM duration overflow");
    return checked_product(blocks+training_blocks,g.length);
}
std::size_t interval_symbols(const Profile& p,std::size_t){Geometry g(p);return (g.blocks+g.cycle_intervals-1)/g.cycle_intervals;}
std::size_t preamble_symbols(const Profile&){return training_blocks;}
std::size_t pulse_tail_symbols(const Profile&){return 0;}
double occupied_lower(const Profile& p){return std::ceil(p.ofdm_low_hz*p.ofdm_fft_size/p.sample_rate)*p.sample_rate/p.ofdm_fft_size;}
double occupied_upper(const Profile& p){return std::floor(p.ofdm_high_hz*p.ofdm_fft_size/p.sample_rate)*p.sample_rate/p.ofdm_fft_size;}
double gross_bitrate(const Profile& p){Geometry g(p);return static_cast<double>(g.cycle_bits)*p.sample_rate/((g.blocks+1)*g.length);}

struct Transmitter::Impl {
    Profile p;Geometry g;IntervalReader source;SymbolObserver observer;
    Bytes bits;std::vector<float> pcm;std::size_t position=0,block_in_cycle=0;
    std::uint64_t block=0,samples=0;bool done=false,have_cycle=false,refresh=false;
    Impl(Profile config,IntervalReader reader,SymbolObserver cb):p(checked(config)),g(p),source(std::move(reader)),observer(std::move(cb)),bits(g.cycle_bits) {
        validate(p);if(!source)throw Error("Missing acoustic OFDM source");
    }
    bool load_cycle() {
        for(std::size_t i=0;i<g.cycle_intervals;++i) {
            auto part=std::span(bits).subspan(i*physical_interval_bits,physical_interval_bits);
            if(!source(part)) {
                if(i)throw Error("Acoustic OFDM source ended within a fixed coding cycle");
                return false;
            }
            if(std::any_of(part.begin(),part.end(),[](auto b){return b>1;}))throw Error("Non-bit acoustic source");
        }
        have_cycle=true;block_in_cycle=0;refresh=block>=training_blocks;return true;
    }
    bool next() {
        if(!have_cycle&&!load_cycle()){done=true;return false;}
        std::vector<C> spectrum(g.n);
        if(block<training_blocks) {
            for(const auto k:g.active)spectrum[k]=known(k,block<2?0:block);
        } else if(refresh) {
            for(const auto k:g.active)spectrum[k]=known(k,block);
            refresh=false;
        } else {
            for(const auto k:g.track)spectrum[k]=known(k,block);
            for(const auto k:g.verify)spectrum[k]=known(k,block);
            auto offset=block_in_cycle*g.data.size()*g.bps;
            for(const auto k:g.data) {
                if(offset>=bits.size()) {
                    // Unused cycle coordinates still need a noise-like waveform;
                    // a bank of identical QAM corners creates a clipping impulse.
                    spectrum[k]=known(k,block+0x66696c6cULL);continue;
                }
                unsigned label=0;
                const auto fill=mix(k^mix(block+0x66696c6cULL));
                for(unsigned b=0;b<g.bps;++b,++offset)label=(label<<1)|(offset<bits.size()?bits[offset]:((fill>>b)&1));
                spectrum[k]=qam(p.constellation,label);observe(observer,spectrum[k]);
            }
            if(++block_in_cycle==g.blocks)have_cycle=false;
        }
        for(const auto k:g.active)spectrum[g.n-k]=std::conj(spectrum[k]);
        fft(spectrum,true);
        // One fixed scale for every block; amplitude/4.5 is nominal RMS.
        const auto scale=p.amplitude*g.n/(4.5*std::sqrt(2.*g.active.size()));
        pcm.resize(g.length);
        for(std::size_t i=0;i<g.length;++i)pcm[i]=static_cast<float>(spectrum[(i+g.n-g.cp)%g.n].real()*scale);
        ++block;position=0;return true;
    }
    std::size_t read(std::span<float> out) {
        if(done)return 0;
        std::size_t count=0;
        while(count<out.size()) {
            if(position==pcm.size()&&!next())break;
            const auto amount=std::min(out.size()-count,pcm.size()-position);
            std::copy_n(pcm.begin()+static_cast<std::ptrdiff_t>(position),amount,out.begin()+static_cast<std::ptrdiff_t>(count));
            count+=amount;position+=amount;samples+=amount;
        }
        return count;
    }
};

struct Receiver::Impl {
    Profile p;Geometry g;IntervalSink sink;SymbolObserver observer,input_observer;DiagnosticObserver diagnostic;ModemProgress state;
    std::vector<float> pcm,cycle_soft,pending_soft;std::uint64_t first=0,received=0,next_search=0,block=training_blocks;
    std::vector<C> reference_fft,channel;std::vector<double> variance;
    std::vector<std::size_t> signature;
    bool eof=false,pending=false,locked=false,refresh=false;
    double candidate=0,next_window=0,period=1,absent=0,reference_energy=0;
    std::size_t block_in_cycle=0,soft_count=0;
    Impl(Profile config,IntervalSink target,SymbolObserver cb,SymbolObserver input,DiagnosticObserver diag):p(checked(config)),g(p),sink(std::move(target)),observer(std::move(cb)),input_observer(std::move(input)),diagnostic(std::move(diag)),
        cycle_soft(g.cycle_bits),reference_fft(2*g.n),channel(g.n),variance(g.n,1) {
        validate(p);if(!sink)throw Error("Missing acoustic OFDM sink");
        std::vector<C> ref(g.n);
        for(auto k:g.active){ref[k]=known(k,0);ref[g.n-k]=std::conj(ref[k]);}
        fft(ref,true);
        for(std::size_t i=0;i<g.n;++i){reference_fft[i]=ref[i];reference_energy+=std::norm(ref[i]);}
        fft(reference_fft);next_search=2*g.n;
    }
    double sample_at(double time) const {
        const auto center=static_cast<std::int64_t>(std::floor(time));
        double value=0,total=0;
        for(int j=-15;j<=16;++j) {
            const auto index=center+j;
            if(index<static_cast<std::int64_t>(first)||index>=static_cast<std::int64_t>(received))continue;
            const auto t=static_cast<double>(index)-time;
            const auto weight=interpolation_weight(t);
            value+=weight*pcm[static_cast<std::size_t>(index-static_cast<std::int64_t>(first))];total+=weight;
        }
        return total?value/total:0;
    }
    std::vector<C> spectrum(double start,double step) const {
        std::vector<C> out(g.n);
        for(std::size_t i=0;i<g.n;++i)out[i]=sample_at(start+i*step);
        fft(out);return out;
    }
    void search() {
        const auto start=next_search-2*g.n;
        if(start<first){next_search=first+2*g.n;return;}
        std::vector<C> work(2*g.n);std::vector<double> energy(2*g.n+1);
        for(std::size_t i=0;i<work.size();++i) {
            const auto v=pcm[static_cast<std::size_t>(start+i-first)];work[i]=v;energy[i+1]=energy[i]+v*v;
        }
        fft(work);
        // Display-only unsynchronized input bins. The search FFT has twice the
        // modem size, hence 2*k identifies the configured physical frequency.
        if(input_observer)for(std::size_t k=g.first;k<=g.last;k+=std::max<std::size_t>(1,g.active.size()/128))
            observe(input_observer,work[2*k]/static_cast<double>(g.n));
        for(std::size_t i=0;i<work.size();++i)work[i]*=std::conj(reference_fft[i]);fft(work,true);
        double best=0;std::size_t offset=0;
        for(std::size_t i=0;i<g.n;++i) {
            const auto score=std::norm(work[i])/(reference_energy*(energy[i+g.n]-energy[i])+1e-30);
            if(score>best){best=score;offset=i;}
        }
        if(best>.012) {
            // Measured nearby-speaker response has short precursor energy and
            // a long causal tail. Keep one millisecond of precursor allowance
            // instead of wasting a quarter of the cyclic prefix on precursors.
            // Capture may begin within the first cyclic prefix. Do not reject
            // a complete training body merely because its optional precursor
            // allowance precedes the first available sample.
            candidate=std::max(static_cast<double>(first),
                static_cast<double>(start+offset)-std::min(48.,g.cp*.1));
            pending=true;
        }
        next_search+=g.n/2;
    }
    // Known training/pilot coordinates select this timing correction. Held-out
    // signature tones never fit the channel, timing, common phase or gain.
    std::pair<double,C> delay_fit(const std::vector<C>& cross,std::span<const std::size_t> bins,double radius) const {
        auto score=[&](double delay) {C sum=0;for(auto k:bins)sum+=cross[k]*std::polar(1.,2*pi*k*delay/g.n);return sum;};
        double best=0;C sum=score(0);double power=std::norm(sum);
        for(int j=-40;j<=40;++j) {
            const auto d=radius*j/40;const auto v=score(d);
            if(std::norm(v)>power){power=std::norm(v);best=d;sum=v;}
        }
        const auto step=radius/40,left=std::norm(score(best-step)),right=std::norm(score(best+step));
        const auto curvature=left-2*power+right;
        if(curvature< -1e-20)best+=std::clamp(.5*(left-right)/curvature,-1.,1.)*step;
        return {best,score(best)};
    }
    std::vector<std::size_t> strongest(std::span<const std::size_t> bins) const {
        std::vector<std::size_t> order(bins.begin(),bins.end());
        std::sort(order.begin(),order.end(),[&](auto a,auto b){return std::norm(channel[a])/variance[a]>std::norm(channel[b])/variance[b];});
        order.resize(signature_tones);return order;
    }
    bool verify(const std::vector<C>& observed,std::uint64_t ordinal,std::span<const std::size_t> bins,C gain=1.,double delay=0) const {
        unsigned errors=0;double signal=0,residual=0;
        for(auto k:bins) {
            const auto expected=channel[k]*known(k,ordinal)*gain*std::polar(1.,-2*pi*k*delay/g.n);
            const auto normalized=std::norm(expected)>1e-30?observed[k]/(channel[k]*gain*std::polar(1.,-2*pi*k*delay/g.n)):C{};
            const auto symbol=known(k,ordinal);
            errors+=(normalized.real()>0)!=(symbol.real()>0);
            errors+=(normalized.imag()>0)!=(symbol.imag()>0);
            signal+=std::norm(expected);residual+=std::norm(observed[k]-expected);
        }
        std::cerr << "verify ordinal=" << ordinal << " errors=" << errors << " residual_ratio=" << residual/(signal+1e-30) << " gain=" << std::abs(gain) << " phase=" << std::arg(gain) << " delay=" << delay << '\n';
        return errors<=signature_errors&&signal>1e-16&&residual<signal;
    }
    void acquire() {
        auto a=spectrum(candidate,1.),b=spectrum(candidate+g.length,1.);
        std::vector<C> cross(g.n);
        for(auto k:g.active)cross[k]=b[k]*std::conj(a[k]);
        const auto [delay,unused]=delay_fit(cross,g.active,8.);(void)unused;
        period=std::clamp(1+delay/g.length,.999,1.001);
        // Independent phases expose channel memory and nonlinear residual that
        // two identical training waveforms can accidentally fit away.
        // Use the last fitting block as the amplitude/timing reference so
        // startup gain settling does not anchor reception to its weakest part.
        // Block 15 remains independent and is never included in this estimate.
        auto anchor=spectrum(candidate+(training_blocks-2)*g.length*period,period);
        for(auto k:g.active)anchor[k]/=known(k,training_blocks-2);
        std::vector<C> sum(g.n);
        std::vector<double> squared(g.n),raw_variance(g.n);
        const auto count=training_blocks-3;
        double weight_sum=0;
        for(std::size_t ordinal=2;ordinal<training_blocks-1;++ordinal) {
            auto current=spectrum(candidate+ordinal*g.length*period,period);
            for(auto k:g.active)current[k]/=known(k,ordinal);
            double residual_delay=0;C common=1.;
            if(ordinal!=training_blocks-2) {
                double power=0;
                for(auto k:g.active){cross[k]=current[k]*std::conj(anchor[k]);power+=std::norm(anchor[k]);}
                const auto fitted=delay_fit(cross,g.active,6.);residual_delay=fitted.first;
                if(power>1e-30&&std::abs(fitted.second)>1e-20)common=fitted.second/power;
            }
            // Dividing by a gain g multiplies stationary additive input-noise
            // variance by 1/|g|^2. Inverse-variance weighting prevents the weak
            // early blocks from dominating the channel estimate after settling.
            const auto weight=std::norm(common);weight_sum+=weight;
            for(auto k:g.active) {
                const auto aligned=current[k]*std::polar(1.,2*pi*k*residual_delay/g.n)/common;
                sum[k]+=weight*aligned;squared[k]+=weight*std::norm(aligned);
            }
        }
        for(auto k:g.active) {
            channel[k]=sum[k]/weight_sum;
            // Weighted residual/(count-1) estimates input-noise variance in
            // the reference block's units. The fitted-mean uncertainty is
            // noise/weight_sum; prediction adds both uncertainties. Do not
            // treat this whole predictive variance as channel-state variance.
            raw_variance[k]=std::max(0.,squared[k]-weight_sum*std::norm(channel[k]))/(count-1)*(1+1./weight_sum);
        }
        for(auto k:g.active) {
            double sum=0;std::size_t count=0;
            for(auto j=std::max(g.first,k>8?k-8:0);j<=std::min(g.last,k+8);++j){sum+=raw_variance[j];++count;}
            variance[k]=std::max(sum/count,std::norm(channel[k])*1e-6+1e-18);
        }
        const auto check=strongest(g.active);
        auto verification=spectrum(candidate+(training_blocks-1)*g.length*period,period);
        // Startup gain control or slow timing motion can change the final
        // block relative to the fitting blocks without changing its
        // marker. Fit those common quantities using only the OTHER tones: all
        // 128 marker tones remain held out of channel, gain and timing fitting.
        std::vector<bool> held_out(g.n,false);
        for(auto k:check)held_out[k]=true;
        std::vector<std::size_t> calibration;calibration.reserve(g.active.size()-check.size());
        double normalization=0;
        for(auto k:g.active)if(!held_out[k]) {
            calibration.push_back(k);
            cross[k]=verification[k]*std::conj(channel[k]*known(k,training_blocks-1));
            normalization+=std::norm(channel[k]);
        }
        const auto [final_delay,correlation]=delay_fit(cross,calibration,6.);
        const auto common=normalization>1e-30?correlation/normalization:C{};
        if(std::abs(common)<=.1||std::abs(common)>=10||
            !verify(verification,training_blocks-1,check,common,final_delay)){pending=false;return;}
        signature=strongest(g.verify);locked=true;pending=false;state.acquired=true;
        next_window=candidate+training_blocks*g.length*period+final_delay*.8;
        state.clock_error_ppm=(period-1)*1e6;state.symbols=training_blocks;
    }
    void process_block() {
        auto y=spectrum(next_window,period);std::vector<C> cross(g.n);
        double normalization=0;
        for(auto k:g.track) {
            cross[k]=y[k]*std::conj(channel[k]*known(k,block));normalization+=std::norm(channel[k]);
        }
        const auto [delay,correlation]=delay_fit(cross,g.track,6.);
        const auto common=normalization>1e-30?correlation/normalization:C{};
        const bool present=std::abs(common)>.1&&std::abs(common)<10&&verify(y,block,signature,common,delay);
        double tracking_energy=0;for(auto k:g.track)tracking_energy+=std::norm(y[k]);
        const double coherent=std::norm(correlation)/(normalization*tracking_energy+1e-30);
        std::cerr << "block=" << block << " capture_start=" << next_window/p.sample_rate << " pilot_coherence=" << coherent << " delay=" << delay << " gain=" << std::abs(common) << " admitted=" << present << " absent_before=" << absent << '\n';
        ++state.symbols;
        if(present)absent=0;else absent+=g.length*period/p.sample_rate;
        if(absent>=6){state.physical_complete=true;pending_soft.clear();return;}
        if(present) {
            for(std::size_t i=0;i<pending_soft.size();i+=physical_interval_bits) {
                auto part=std::span(pending_soft).subspan(i,physical_interval_bits);
                if(std::none_of(part.begin(),part.end(),[](float v){return v!=0;}))++state.erased_intervals;
                sink(part);++state.intervals;
            }
            pending_soft.clear();
        }
        std::vector<double> pilot_variance;pilot_variance.reserve(g.track.size());
        for(auto k:g.track) {
            const auto expected=channel[k]*known(k,block)*common*std::polar(1.,-2*pi*k*delay/g.n);
            pilot_variance.push_back(std::norm(y[k]-expected));
        }
        if(refresh) {
            // Full-band known symbols refresh frequency-selective response.
            // Presence was independently checked against the PREVIOUS channel;
            // fitting this block cannot make its own marker pass.
            if(present) {
                // A single noisy refresh must not replace the averaged estimate.
                // Align the prior gain before combining independently phased blocks.
                for(auto k:g.active)channel[k]=.75*channel[k]*common+
                    .25*y[k]*std::polar(1.,2*pi*k*delay/g.n)/known(k,block);
                signature=strongest(g.verify);
                next_window+=delay*.8;period=std::clamp(period+.03*delay/g.length,.999,1.001);
                state.clock_error_ppm=(period-1)*1e6;
            }
            refresh=false;++block;next_window+=g.length*period;return;
        }
        double error=0;std::size_t count=0;
        std::array<double,22> metrics{};
        auto offset=block_in_cycle*g.data.size()*g.bps;
        for(auto k:g.data) {
            if(offset>=g.cycle_bits)break;
            const auto h=channel[k]*common*std::polar(1.,-2*pi*k*delay/g.n);
            const auto norm=std::norm(h);
            C value=norm>1e-30?y[k]/h:C{};
            observe(input_observer,y[k]);
            if(present)observe(observer,value);
            // Per-bin channel equalization and its noise variance.
            // Deep fades receive small LLRs instead of amplified confidence.
            const auto nearest=static_cast<std::size_t>(std::lower_bound(g.track.begin(),g.track.end(),k)-g.track.begin());
            double local_error=0;std::size_t local_count=0;
            for(auto j=nearest>2?nearest-2:0;j<std::min(g.track.size(),nearest+3);++j) {
                local_error+=pilot_variance[j];++local_count;
            }
            // Estimate residual in RECEIVED-bin units before dividing by H.
            // A global post-equalization floor misses noise amplification in
            // deep frequency-selective fades and yields confidently wrong bits.
            const auto input_noise=std::max(variance[k]*std::norm(common),local_error/std::max<std::size_t>(local_count,1));
            const auto nv=input_noise/std::max(norm,1e-30);
            if(diagnostic)try{diagnostic({block,(block-training_blocks)/(g.blocks+1)*g.cycle_bits+offset,k,value,nv,norm,state.clock_error_ppm,delay,present});}catch(...){}
            unsigned closest=0;
            if(present&&std::isfinite(value.real())&&std::isfinite(value.imag()))closest=square_qam_soft_demodulate(p.constellation,value,std::span(metrics).first(g.bps));
            error+=std::norm(value-qam(p.constellation,closest));++count;
            for(std::size_t j=0;j<g.bps&&offset<g.cycle_bits;++j,++offset)
                cycle_soft[offset]=present?static_cast<float>(std::clamp(metrics[j]/std::max(nv,1e-8),-24.,24.)):0.F;
        }
        if(present) {
            state.evm=std::sqrt(error/std::max<std::size_t>(count,1));
            // Track slow relative ADC/DAC clock motion using independent pilots.
            next_window+=delay*.8;period=std::clamp(period+.03*delay/g.length,.999,1.001);
            state.clock_error_ppm=(period-1)*1e6;
        }
        ++block;++block_in_cycle;next_window+=g.length*period;
        // Deliver every full interval as soon as its complete fixed coordinates
        // have arrived, retaining only the incomplete interval/cycle positions.
        const auto ready=std::min(g.cycle_bits,block_in_cycle*g.data.size()*g.bps)/physical_interval_bits;
        while(soft_count<ready) {
            auto part=std::span(cycle_soft).subspan(soft_count*physical_interval_bits,physical_interval_bits);
            if(present) {
                if(std::none_of(part.begin(),part.end(),[](float v){return v!=0;}))++state.erased_intervals;
                sink(part);++state.intervals;
            } else pending_soft.insert(pending_soft.end(),part.begin(),part.end());
            ++soft_count;
        }
        if(block_in_cycle==g.blocks){block_in_cycle=0;soft_count=0;refresh=true;std::fill(cycle_soft.begin(),cycle_soft.end(),0);}
    }
    void ingest(std::span<const float> values) {
        pcm.insert(pcm.end(),values.begin(),values.end());received+=values.size();
        for(;;) {
            if(state.physical_complete)break;
            if(locked) {
                if(received<next_window+g.length*period+18)break;
                process_block();
            } else if(pending) {
                if(received<candidate+training_blocks*g.length*1.001+20)break;
                acquire();
            } else {
                if(received<next_search)break;
                search();
            }
        }
        const auto keep=locked?std::max(0.,next_window-32):pending?std::max(0.,candidate-32):static_cast<double>(next_search-2*g.n);
        const auto discard=static_cast<std::uint64_t>(std::min<double>(received,keep));
        if(discard>first+g.n) {pcm.erase(pcm.begin(),pcm.begin()+static_cast<std::ptrdiff_t>(discard-first));first=discard;}
    }
    void push(std::span<const float> values) {
        if(eof)throw Error("Acoustic samples supplied after EOF");
        const auto limit=std::numeric_limits<std::uint64_t>::max()-8*g.length;
        if(values.size()>limit||received>limit-values.size())throw Error("Acoustic OFDM sample coordinate overflow");
        for(auto v:values)if(!std::isfinite(v))throw Error("Nonfinite acoustic PCM");
        for(std::size_t offset=0;offset<values.size()&&!state.physical_complete;offset+=g.n/2)
            ingest(values.subspan(offset,std::min<std::size_t>(g.n/2,values.size()-offset)));
    }
};

Transmitter::Transmitter(Profile p,IntervalReader r,SymbolObserver o):impl_(std::make_unique<Impl>(p,std::move(r),std::move(o))){}
Transmitter::~Transmitter()=default;
std::size_t Transmitter::read(std::span<float> out){return impl_->read(out);}
bool Transmitter::finished()const{return impl_->done;}
std::uint64_t Transmitter::samples_generated()const{return impl_->samples;}
std::size_t Transmitter::workspace_bytes()const{return sizeof(Impl)+impl_->bits.capacity()+impl_->pcm.capacity()*sizeof(float)+impl_->g.n*sizeof(C);}
Receiver::Receiver(Profile p,IntervalSink s,SymbolObserver o,SymbolObserver i,DiagnosticObserver d):impl_(std::make_unique<Impl>(p,std::move(s),std::move(o),std::move(i),std::move(d))){}
Receiver::~Receiver()=default;
void Receiver::push(std::span<const float> values){impl_->push(values);}
void Receiver::finish(){impl_->eof=true;}
const ModemProgress& Receiver::progress()const{return impl_->state;}
std::size_t Receiver::workspace_bytes()const{return sizeof(Impl)+(impl_->pcm.capacity()+impl_->cycle_soft.capacity()+impl_->pending_soft.capacity())*sizeof(float)+(impl_->reference_fft.capacity()+impl_->channel.capacity())*sizeof(C)+impl_->variance.capacity()*sizeof(double)+6*impl_->g.n*sizeof(C);}
}
