#include "amplitude_probability.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "estimate_cancellation.hpp"
#include "pattern_envelope.hpp"
#include "probability_random.hpp"
#include <array>
#include <complex>
#include <numbers>
#include <random>
#include <vector>
namespace datapump::simulation::detail {
namespace {
using Wide=long double;
struct Row {
    std::array<double,2> weight{};
    std::array<std::vector<double>,2> spectrum;
    Wide multiple=0;
    unsigned quarter=0;
};
struct Quarter {
    modem::detail::EnvelopeTemplateMoments shape;
    std::vector<Wide> mean,covariance,factor;
};
// Same-lattice overlapping channels share the original independent Fourier
// atoms. In particular E_c^2 and E_d^2 must not be drawn independently.
Wide covariance(unsigned a,unsigned b,Wide ma,Wide mb,Wide v,Wide t,Wide u,
                const std::array<double,2>& w) {
    if(a==1&&b==1)return 4*ma*mb*v+2*(ma+mb)*t+u+2*v*v;
    if(a==1)return (2*ma*v+t)*(b>=2?w[b-2]:1);
    if(b==1)return (2*mb*v+t)*(a>=2?w[a-2]:1);
    return v*(a>=2?w[a-2]:1)*(b>=2?w[b-2]:1);
}
}
AmplitudeProbabilityResult amplitude_probability(const modem::Config& c,const modem::PatternSearch& search,
        double es,double frequency,std::size_t trials,std::stop_token stop) {
    estimate_detail::check(stop);
    AmplitudeProbabilityResult result;
    result.limit="Amplitude matched-envelope AWGN reference: ideal alignment and nominal clock. "
        "Source/statistical approximations, phase noise, resampling, other keys/epochs, continuation and physical "
        "completion remain unqualified. Continuous arrival and competing-bank probabilities are not calibrated; "
        "a reference above 99% is not qualified RX success";
    const auto g=modem::pattern_amplitude_geometry(c,search);
    const auto total=modem::symbol_sample_count(c);
    if(!g.supported||!trials||!std::isfinite(es)||es < -300||es > 300||!std::isfinite(frequency)||
       c.scramble||c.data_key||c.dsss||c.dsss_factor!=1||c.fake_hopping||modem::scheduled_bursts(c)||
       modem::legacy_public_pattern(c)||g.channel_count>4||g.bin_count>48||
       static_cast<Wide>(total)/c.sample_rate>3*365.25L*86400) {
        result.limit="Amplitude reference unavailable outside ordinary public shaped patterns, at most 4 channels/48 bins and 3 years per symbol; full-bank RX probability remains unqualified";
        return result;
    }
    const auto count=g.channel_count,dimension=4*count;
    std::vector<modem::PatternAmplitudeChannel> channels;
    for(std::size_t j=0;j<count;++j)channels.push_back(modem::pattern_amplitude_channel(g,j));
    std::vector<std::array<std::complex<double>,64>> phasors(g.bin_count);
    for(std::size_t bin=0;bin<g.bin_count;++bin) {
        const auto cycles=(c.carrier_hz+frequency)/g.bin_width_hz-static_cast<double>(g.first_bin+bin);
        if(std::abs(cycles)>24){result.limit="Amplitude source Fourier quadrature does not resolve this carrier/channel domain";return result;}
        for(unsigned node=0;node<64;++node)phasors[bin][node]=std::polar(1.,2*std::numbers::pi*cycles*(node+.5)/64);
    }
    modem::PatternCode code(c);
    try{code.prepare_symbol(0,stop);}catch(const Error&){estimate_detail::check(stop);throw;}
    std::vector<Row> rows;
    std::array<std::uint64_t,4> populations{};
    std::array<Wide,2> source_power{};
    const auto amplitude=std::sqrt(2*modem::nominal_signal_power);
    const auto quadrature=std::max(8U,static_cast<unsigned>(std::ceil(8.L*g.fft_samples/code.chip_samples())));
    for(unsigned q=0;q<4;++q) {
        const auto first=(q*total+4*g.fft_samples-1)/(4*g.fft_samples);
        const auto last=((q+1)*total)/(4*g.fft_samples);
        if(last<=first)return result;
        const auto population=last-first;populations[q]=population;result.physical_blocks+=population;
        const auto retained=std::min<std::uint64_t>(population,512);
        for(std::uint64_t j=0;j<retained;++j) {
            estimate_detail::check(stop);
            Row row;row.quarter=q;row.multiple=static_cast<Wide>(population)/retained;
            const auto block=first+static_cast<std::uint64_t>((j+.5L)*population/retained);
            for(unsigned node=0;node<quadrature;++node) {
                const auto z=code.shaped_values(0,static_cast<double>(block*g.fft_samples+(node+.5L)*g.fft_samples/quadrature));
                for(unsigned bit=0;bit<2;++bit)row.weight[bit]+=std::norm(modem::pattern_limit_pcm(amplitude*z[bit]))/quadrature;
            }
            std::array<std::array<std::complex<double>,64>,2> source{};
            for(unsigned node=0;node<64;++node) {
                const auto z=code.shaped_values(0,static_cast<double>(block*g.fft_samples+(node+.5L)*g.fft_samples/64));
                for(unsigned bit=0;bit<2;++bit) {
                    source[bit][node]=modem::pattern_limit_pcm(amplitude*z[bit]);
                    source_power[bit]+=row.multiple*std::norm(source[bit][node])/64;
                }
            }
            for(unsigned bit=0;bit<2;++bit) {
                row.spectrum[bit].resize(g.bin_count);
                for(std::size_t bin=0;bin<g.bin_count;++bin) {
                    // Midpoint baseband Fourier quadrature. The far real-carrier
                    // image and native sinc resampling remain explicit limits.
                    std::complex<double> z{};
                    for(unsigned node=0;node<64;++node)
                        z+=source[bit][node]*phasors[bin][node];
                    row.spectrum[bit][bin]=std::norm(z/64.);
                }
            }
            rows.push_back(std::move(row));
        }
    }
    result.source_blocks=rows.size();
    for(auto& p:source_power)p/=result.physical_blocks;
    if(!(source_power[0]>0&&source_power[1]>0))return result;
    const auto energy=std::pow(10.L,es/10);
    const auto per_block=energy*g.fft_samples/total;
    // Charge every original timing/rate completion at the first-symbol edge,
    // without borrowing the much larger complex carrier-bank penalty.
    const auto timing=std::ceil(4.L*search.start_uncertainty_seconds*c.sample_rate/code.chip_samples())+1;
    const auto hypotheses=std::max(1.L,timing*g.clock_hypotheses);
    result.threshold=static_cast<double>(-std::log(search.false_alarm_probability)+std::log(hypotheses)+std::log(hypotheses+1)+std::log(2.L));
    std::mt19937_64 random(0xb5f48d609a72e31cULL);ProbabilityNormal normal;
    std::size_t correct=0,wrong=0;
    for(unsigned source_bit=0;source_bit<2;++source_bit) {
        std::array<Quarter,4> quarters;
        for(auto& q:quarters){q.mean.resize(dimension);q.covariance.resize(dimension*dimension);q.factor.resize(dimension*dimension);}
        for(const auto& row:rows) {
            auto& q=quarters[row.quarter];const auto f=row.multiple;
            for(unsigned b=0;b<2;++b) {
                q.shape.weight[b]+=f*row.weight[b];q.shape.square[b]+=f*row.weight[b]*row.weight[b];
                q.shape.maximum[b]=std::max(q.shape.maximum[b],static_cast<Wide>(row.weight[b]));
            }
            q.shape.independent+=f*2*g.fft_samples/code.chip_samples();
            std::vector<Wide> lambda(g.bin_count),mu(count);
            for(std::size_t j=0;j<lambda.size();++j)lambda[j]=per_block*row.spectrum[source_bit][j]/source_power[source_bit];
            const auto region=[&](std::size_t lo,std::size_t hi) {Wide x=0;for(auto j=lo;j<hi;++j)x+=lambda[j-g.first_bin];return x;};
            for(std::size_t ch=0;ch<count;++ch) {
                const auto& c0=channels[ch];const auto l=region(c0.first_bin,c0.first_bin+c0.bin_count);
                mu[ch]=c0.bin_count+l;const std::array<Wide,4> means={mu[ch],mu[ch]*mu[ch]+c0.bin_count+2*l,row.weight[0]*mu[ch],row.weight[1]*mu[ch]};
                for(unsigned a=0;a<4;++a)q.mean[ch*4+a]+=f*means[a];
            }
            for(std::size_t ca=0;ca<count;++ca)for(std::size_t cb=0;cb<count;++cb) {
                const auto lo=std::max(channels[ca].first_bin,channels[cb].first_bin);
                const auto hi=std::min(channels[ca].first_bin+channels[ca].bin_count,channels[cb].first_bin+channels[cb].bin_count);
                const auto k=hi>lo?hi-lo:0;const auto l=k?region(lo,hi):0;
                for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)
                    q.covariance[(ca*4+a)*dimension+cb*4+b]+=f*covariance(a,b,mu[ca],mu[cb],k+2*l,2*k+6*l,6*k+24*l,row.weight);
            }
        }
        for(unsigned qi=0;qi<4;++qi) {
            auto& q=quarters[qi];q.shape.observations=populations[qi];
            if(populations[qi]>512)for(auto& m:q.shape.maximum)m=modem::pattern_pcm_radius_limit*modem::pattern_pcm_radius_limit;
            std::vector<Wide> scales(dimension);
            for(std::size_t a=0;a<dimension;++a) {
                const auto v=q.covariance[a*dimension+a];
                if(!(v>0)||!std::isfinite(v)){result.limit="Amplitude reference has invalid energy covariance";return result;}
                scales[a]=std::sqrt(v);
            }
            for(std::size_t a=0;a<dimension;++a)for(std::size_t b=0;b<=a;++b) {
                auto x=q.covariance[a*dimension+b]/(scales[a]*scales[b]);
                for(std::size_t k=0;k<b;++k)x-=q.factor[a*dimension+k]*q.factor[b*dimension+k];
                constexpr auto tolerance=1e-10L;
                if(!std::isfinite(x)||(a==b&&x < -tolerance)||
                   (a!=b&&q.factor[b*dimension+b]==0&&std::abs(x)>tolerance)) {
                    result.limit="Amplitude reference covariance is not numerically positive semidefinite";return result;
                }
                if(a==b)q.factor[a*dimension+b]=std::sqrt(std::max(0.L,x));
                else if(q.factor[b*dimension+b]>0)q.factor[a*dimension+b]=x/q.factor[b*dimension+b];
            }
            for(std::size_t a=0;a<dimension;++a)for(std::size_t b=0;b<=a;++b)q.factor[a*dimension+b]*=scales[a];
        }
        for(std::size_t trial=0;trial<trials;++trial) {
            if(trial%64==0)estimate_detail::check(stop);
            modem::detail::EnvelopeTemplateFit shape;
            std::vector<modem::detail::EnvelopeChannelFit> fits(count);
            for(unsigned qi=0;qi<4;++qi) {
                const auto& q=quarters[qi];shape.quarters[qi]=q.shape;
                for(unsigned b=0;b<2;++b){shape.whole.weight[b]+=q.shape.weight[b];shape.whole.square[b]+=q.shape.square[b];shape.whole.maximum[b]=std::max(shape.whole.maximum[b],q.shape.maximum[b]);}
                shape.whole.independent+=q.shape.independent;shape.whole.observations+=q.shape.observations;
                std::vector<Wide> z(dimension),values=q.mean;for(auto& x:z)x=normal(random);
                for(std::size_t a=0;a<dimension;++a)for(std::size_t b=0;b<=a;++b)values[a]+=q.factor[a*dimension+b]*z[b];
                for(std::size_t ch=0;ch<count;++ch) {
                    auto& m=fits[ch].quarters[qi];m.energy=values[ch*4];m.energy_square=values[ch*4+1];m.weighted_energy={values[ch*4+2],values[ch*4+3]};
                    auto& whole=fits[ch].whole;whole.energy+=m.energy;whole.energy_square+=m.energy_square;
                    for(unsigned b=0;b<2;++b)whole.weighted_energy[b]+=m.weighted_energy[b];
                }
            }
            std::array<double,2> scores{};
            for(unsigned b=0;b<2;++b)for(std::size_t ch=0;ch<count;++ch)
                scores[b]=std::max(scores[b],fits[ch].score(shape,b,channels[ch].bin_count)-std::log(static_cast<double>(count)));
            const auto winner=scores[1]>scores[0]?1U:0U;
            if(scores[winner]>=result.threshold&&scores[winner]-scores[1-winner]>=1) {
                if(winner==source_bit)++correct;else ++wrong;
            }
        }
    }
    result.available=true;result.trials=2*trials;result.correct=double(correct)/result.trials;result.wrong=double(wrong)/result.trials;return result;
}
}
