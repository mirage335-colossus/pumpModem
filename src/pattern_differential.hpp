#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>

namespace datapump::modem::detail {

// Window size comes from local receiver settings and complete transmitted
// chips, never a received length or a fraction of a long symbol. A sufficient
// number of independent pairs is necessary for this bounded statistic to
// provide useful evidence after its selection and coverage penalties.
inline std::uint64_t differential_window_samples(std::uint64_t total,std::uint64_t chip,
                                                  std::uint32_t sample_rate,double seconds) {
    if(!chip || !sample_rate || !(seconds>0) || !std::isfinite(seconds))return 0;
    const auto chips=std::max(16.L,std::ceil(static_cast<long double>(seconds)*sample_rate/chip));
    if(!std::isfinite(chips) || chips>std::numeric_limits<std::uint64_t>::max()/chip)return 0;
    const auto window=static_cast<std::uint64_t>(chips)*chip;
    return window && total/window>=256?window:0;
}

// A real PCM projection generally has unequal, correlated I/Q noise. Applying
// the symmetric inverse square root of its deterministic 2x2 Gram matrix makes
// its null noise circular without imposing a preferred phase axis. Cholesky
// whitening alone would add a window-dependent rotation to the comparison.
inline std::complex<double> differential_whiten(double xc,double xs,double cc,double ss,double cs) {
    if(!std::isfinite(xc) || !std::isfinite(xs) || !std::isfinite(cc) ||
            !std::isfinite(ss) || !std::isfinite(cs) || !(cc>0) || !(ss>0))return {};
    const auto scale=std::max(cc,ss);
    const auto a=cc/scale,b=ss/scale,c=cs/scale;
    const auto determinant=a*b-c*c;
    if(!(determinant>1e-12))return {};
    const auto root=std::sqrt(determinant);
    const auto denominator=std::sqrt(scale)*root*std::sqrt(a+b+2*root);
    if(!(denominator>0) || !std::isfinite(denominator))return {};
    const std::complex<double> result{((b+root)*xc-c*xs)/denominator,
                                      ((a+root)*xs-c*xc)/denominator};
    return std::isfinite(result.real()) && std::isfinite(result.imag())?result:std::complex<double>{};
}

struct DifferentialAccumulator {
    std::array<std::complex<double>,4> sums{};
    double squared_magnitudes=0;
    std::complex<double> previous{};
    std::uint64_t previous_index=0,pairs=0;
    bool pending=false;

    void add(std::complex<double> z,std::uint64_t index,std::uint64_t total,std::uint64_t window) {
        // Disjoint pairs make phase products independent under circular white
        // Gaussian noise. Reusing every overlapping adjacent product would not
        // support the same finite-sample null bound without more reasoning.
        if(!window || index>=total/window || !std::isfinite(z.real()) ||
                !std::isfinite(z.imag()) || z==std::complex<double>{}) {
            pending=false;return;
        }
        if(!(index&1)) {
            previous=z;previous_index=index;pending=true;return;
        }
        const auto adjacent=pending && previous_index==index-1;
        pending=false;
        if(!adjacent)return;
        const auto product=z*std::conj(previous);
        const auto magnitude=std::norm(product);
        if(!std::isfinite(product.real()) || !std::isfinite(product.imag()) ||
                !std::isfinite(magnitude) || !(magnitude>0))return;
        const auto midpoint=index*window; // index < total/window bounds this multiplication.
        unsigned quarter=0;
        for(unsigned boundary=1;boundary<4;++boundary)
            if(midpoint>=total/4*boundary+(total%4)*boundary/4)++quarter;
        sums[quarter]+=product;squared_magnitudes+=magnitude;++pairs;
    }

    double score() const {
        if(pairs<128 || !(squared_magnitudes>0) || !std::isfinite(squared_magnitudes))return 0;
        for(const auto z:sums)if(!std::isfinite(z.real()) || !std::isfinite(z.imag()))return 0;
        constexpr double diagonal=std::numbers::sqrt2/2;
        constexpr std::array<std::complex<double>,8> directions{{
            {1,0},{diagonal,diagonal},{0,1},{-diagonal,diagonal},
            {-1,0},{-diagonal,-diagonal},{0,-1},{diagonal,-diagonal}}};
        double best=0;
        for(const auto direction:directions) {
            double sum=0,largest=0;
            for(const auto z:sums) {
                const auto projected=(z*std::conj(direction)).real();
                sum+=projected;largest=std::max(largest,projected);
            }
            // Discarding the strongest quarter's positive contribution can
            // only lower the tested one-sided statistic. Evidence confined to
            // one fixed quarter cannot supply a complete-symbol match by itself.
            const auto retained=std::max(0.,sum-largest);
            const auto normalized=retained/std::sqrt(squared_magnitudes);
            best=std::max(best,normalized*normalized);
        }
        // Conditional on each product's magnitude, its null phase is uniform.
        // E exp(t a cos(phi)) = I0(t a) <= exp(t^2 a^2/4), hence the one-sided
        // tail is <= exp(-S^2/sum(a^2)). A union bound charges all eight fixed
        // phase directions. No short window needs an individual hard decision.
        return std::max(0.,std::min(best,static_cast<double>(pairs))-std::log(8.));
    }
};

inline double combine_differential_evidence(double existing,double differential,bool eligible) {
    return eligible?std::max(0.,std::max(existing,differential)-std::numbers::ln2):existing;
}

static_assert(std::is_trivially_copyable_v<DifferentialAccumulator> &&
              std::is_standard_layout_v<DifferentialAccumulator>);

} // namespace datapump::modem::detail
