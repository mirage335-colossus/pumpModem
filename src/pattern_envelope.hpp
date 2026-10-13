#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace datapump::modem::detail {
// Nonoverlapping orthogonal real-PCM Fourier coordinates have independent
// Gamma(k, scale) energies: k is the number of retained complex bins. Their
// common unknown noise scale cancels from weighted_energy / energy.
// The weights are deterministic original-template envelope samples, never
// estimated from received data. No independence of shaped samples is assumed.
struct EnvelopeMoments {
    long double energy=0,energy_square=0,weighted_energy=0,shape=0,weighted_shape=0,square_shape=0,maximum=0,independent_shape=0;
    std::uint64_t observations=0;
    void add(double observed,double weight,std::size_t degrees,double independent=std::numeric_limits<double>::infinity()) {
        if(!(observed>=0) || !std::isfinite(observed) || !(weight>=0) ||
           !std::isfinite(weight) || !degrees)return;
        const auto k=static_cast<long double>(degrees),w=static_cast<long double>(weight);
        energy+=observed;energy_square+=static_cast<long double>(observed)*observed;weighted_energy+=w*observed;shape+=k;
        weighted_shape+=k*w;square_shape+=k*w*w;maximum=std::max(maximum,w);
        independent_shape+=std::min(k,std::max(0.L,static_cast<long double>(independent)));++observations;
    }
    EnvelopeMoments& operator+=(const EnvelopeMoments& other) {
        energy+=other.energy;energy_square+=other.energy_square;weighted_energy+=other.weighted_energy;shape+=other.shape;
        weighted_shape+=other.weighted_shape;square_shape+=other.square_shape;
        maximum=std::max(maximum,other.maximum);independent_shape+=other.independent_shape;
        observations+=other.observations;return *this;
    }
    double score()const {
        if(!(energy>0) || !(shape>0) || observations<4)return 0;
        const auto q=weighted_energy/energy,mean=weighted_shape/shape;
        if(!(q>mean) || !(maximum>q))return 0;
        const auto d=shape*(q-mean),b=maximum-q;
        // This arrangement avoids cancellation in the weighted second moment.
        const auto variance=std::max(0.L,square_shape-weighted_shape*mean);
        const auto v=variance+shape*(q-mean)*(q-mean);
        const auto denominator=v+b*d+std::sqrt(v*(v+2*b*d));
        if(!(denominator>0) || !std::isfinite(denominator))return 0;
        const auto gaussian=d*d/denominator;
        // White-noise Fourier bins really are independent. Multiplying their
        // certificate by chips/bins needlessly discards that evidence. Keep it
        // intact, and separately require a centered intensity-profile match.
        // Every channel uses a constant k per block; weighted_shape/shape is
        // therefore also the unweighted mean template intensity.
        const auto n=static_cast<long double>(observations),e=energy/n;
        const auto observed_variance=std::max(0.L,energy_square/n-e*e);
        const auto template_variance=variance/shape;
        const auto covariance=weighted_energy/n-e*mean;
        const auto effective=std::min(n,independent_shape/2);
        if(!(observed_variance>0 && template_variance>0 && covariance>0 && effective>3))return 0;
        const auto rho2=std::min(std::nextafter(1.L,0.L),covariance*covariance/(observed_variance*template_variance));
        // The count cannot exceed original chips or disjoint observations.
        // This Pearson-style structured-input guard is experimental: finite
        // shaped/limited private rows do not have a proven Gaussian-profile
        // wrong-key theorem. Taking the minimum cannot weaken the rigorous
        // white-Gaussian noise certificate above.
        const auto profile=-.5L*(effective-3)*std::log1p(-rho2);
        const auto result=std::min(gaussian,profile);
        return std::isfinite(result)?static_cast<double>(std::min(result,static_cast<long double>(std::numeric_limits<double>::max()))):0;
    }
};
// A predetermined strongest-quarter rejection guard. Taking the minimum of
// four scores bounded by conservative AWGN certificates cannot increase the
// white-noise admission bound; the additional profile guard stays empirical.
// One concentrated pulse/quarter must not supply the whole symbol's evidence.
struct EnvelopeFit {
    EnvelopeMoments whole;
    EnvelopeMoments quarters[4];
    void add(double energy,double weight,std::size_t degrees,unsigned quarter,
             double independent=std::numeric_limits<double>::infinity()) {
        whole.add(energy,weight,degrees,independent);quarters[std::min(3U,quarter)].add(energy,weight,degrees,independent);
    }
    double score()const {
        auto result=whole.score();
        for(unsigned omitted=0;omitted<4;++omitted) {
            EnvelopeMoments retained;
            for(unsigned q=0;q<4;++q)if(q!=omitted)retained+=quarters[q];
            result=std::min(result,retained.score());
        }
        return result;
    }
};
// Factored form of the same statistic. Template moments are independent of
// the coarse carrier channel, and observed energy is independent of bit 0/1.
// Keep whole and quarter sums separately so streaming/omission order is stable.
// Production k and independent count are constant within each clock/channel
// lane: min(k*n, n*independent) equals sum(min(k, independent)). This factoring
// must not be used with observation-varying degrees or independent counts.
struct EnvelopeTemplateMoments {
    std::array<long double,2> weight{},square{},maximum{};
    long double independent=0;
    std::uint64_t observations=0;
    void add(const std::array<double,2>& w,double count) {
        for(unsigned b=0;b<2;++b) {
            const auto x=static_cast<long double>(w[b]);
            weight[b]+=x;square[b]+=x*x;maximum[b]=std::max(maximum[b],x);
        }
        independent+=count;++observations;
    }
};
struct EnvelopeTemplateFit {
    EnvelopeTemplateMoments whole,quarters[4];
    void add(const std::array<double,2>& w,unsigned quarter,double independent) {
        whole.add(w,independent);quarters[std::min(3U,quarter)].add(w,independent);
    }
};
struct EnvelopeChannelMoments {
    long double energy=0,energy_square=0;
    std::array<long double,2> weighted_energy{};
    void add(double e,const std::array<double,2>& w) {
        const auto x=static_cast<long double>(e);
        energy+=x;energy_square+=x*x;
        for(unsigned b=0;b<2;++b)weighted_energy[b]+=static_cast<long double>(w[b])*x;
    }
    EnvelopeMoments expand(const EnvelopeTemplateMoments& t,unsigned bit,std::size_t degrees)const {
        const auto k=static_cast<long double>(degrees);
        EnvelopeMoments m;m.energy=energy;m.energy_square=energy_square;m.weighted_energy=weighted_energy[bit];
        m.shape=k*t.observations;m.weighted_shape=k*t.weight[bit];m.square_shape=k*t.square[bit];
        m.maximum=t.maximum[bit];m.independent_shape=std::min(m.shape,t.independent);m.observations=t.observations;
        return m;
    }
};
struct EnvelopeChannelFit {
    EnvelopeChannelMoments whole,quarters[4];
    void add(double e,const std::array<double,2>& w,unsigned quarter) {
        whole.add(e,w);quarters[std::min(3U,quarter)].add(e,w);
    }
    double score(const EnvelopeTemplateFit& t,unsigned bit,std::size_t degrees)const {
        auto result=whole.expand(t.whole,bit,degrees).score();
        for(unsigned omitted=0;omitted<4;++omitted) {
            EnvelopeMoments retained;
            for(unsigned q=0;q<4;++q)if(q!=omitted)retained+=quarters[q].expand(t.quarters[q],bit,degrees);
            result=std::min(result,retained.score());
        }
        return result;
    }
};
// Proof: for Q=sum(w E)/sum(E), c=w-q, independence gives
// P(Q>=q)<=inf_lambda product(1-lambda*c)^(-k).
// -log(1-z)<=z+z*z/[2*(1-lambda*b)], z=lambda*c, b=max(c).
// Minimizing the resulting upper MGF bound yields
// -log P >= d*d/[V+b*d+sqrt(V*(V+2*b*d))], with d=-sum(k*c).
// This is a conservative moment bound, not a Gaussian approximation or a
// claim about colored/interfering audio. No rare-event Monte Carlo is implied.
}
