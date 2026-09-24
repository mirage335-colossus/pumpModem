#include "../src/pattern_differential.hpp"
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>

using namespace datapump::modem::detail;
namespace {
constexpr double tau=2*std::numbers::pi;
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void close(double value,double expected) {
    check(std::isfinite(value) && std::abs(value-expected)<2e-12*std::max(1.,std::abs(expected)),
          "differential value differs from independent reference");
}

void fixed_local_geometry() {
    constexpr std::uint64_t sample_rate=48000,chip=80,hundred_hours=100ULL*3600*sample_rate;
    const auto window=differential_window_samples(hundred_hours,chip,sample_rate,100);
    check(window==100*sample_rate,"hundred-hour bit must use locally configured hundred-second windows");
    check(differential_window_samples(10*hundred_hours,chip,sample_rate,100)==window,
          "longer symbols changed the absolute differential coherence interval");
    check(differential_window_samples(256*window,chip,sample_rate,100)==window &&
          !differential_window_samples(256*window-1,chip,sample_rate,100),
          "differential eligibility must require 256 complete local windows");
    check(differential_window_samples(256*1600,100,256,.01)==1600,
          "short windows must still contain sixteen complete chips");
    check(differential_window_samples(256*1700,100,256,6.3)==1700,
          "configured time must round up to complete nominal chips");
    for(const auto seconds:{0.,-1.,std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()})
        check(!differential_window_samples(hundred_hours,chip,sample_rate,seconds),
              "invalid or disabled local windows supplied evidence geometry");
    check(!differential_window_samples(hundred_hours,0,sample_rate,100) &&
          !differential_window_samples(hundred_hours,chip,0,100) &&
          !differential_window_samples(std::numeric_limits<std::uint64_t>::max(),
                                        std::numeric_limits<std::uint64_t>::max(),sample_rate,100) &&
          !differential_window_samples(hundred_hours,chip,sample_rate,std::numeric_limits<double>::max()),
          "invalid or overflowing geometry supplied differential windows");
}

void independent_whitening_reference() {
    // Build G and sqrt(G) independently from known eigenvectors/eigenvalues.
    // G^-1/2 sqrt(G) must return the original vector, including its phase.
    for(const auto angle:{0.,.3,1.2,2.8})for(const auto scale:{1e-100,1.,1e100}) {
        const auto c=std::cos(angle),s=std::sin(angle);
        const auto cc=scale*(9*c*c+s*s),ss=scale*(9*s*s+c*c),cs=scale*8*c*s;
        const auto a=std::sqrt(scale)*(3*c*c+s*s),b=std::sqrt(scale)*(3*s*s+c*c),d=std::sqrt(scale)*2*c*s;
        const std::complex<double> original{1.2,-2.3};
        const auto result=differential_whiten(a*original.real()+d*original.imag(),
                                              d*original.real()+b*original.imag(),cc,ss,cs);
        close(result.real(),original.real());close(result.imag(),original.imag());
    }
    for(const auto invalid:{0.,-1.,std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()})
        check(differential_whiten(1,2,invalid,1,0)==std::complex<double>{},
              "invalid Gram matrices supplied circular evidence");
    check(differential_whiten(1,2,1,1,1)==std::complex<double>{} &&
          differential_whiten(1,2,1,1,2)==std::complex<double>{} &&
          differential_whiten(std::numeric_limits<double>::infinity(),2,1,1,0)==std::complex<double>{},
          "singular or nonfinite projections supplied differential evidence");
}

void many_short_comparisons() {
    constexpr std::uint64_t windows=512,window=16,total=windows*window;
    for(const auto increment:{.21,.7,1.6,2.7,3.5,4.4,5.5,6.1}) {
        DifferentialAccumulator fit;std::complex<double> coherent{};
        for(std::uint64_t i=0;i<windows;++i) {
            const auto z=std::polar(1.,.73+increment*static_cast<double>(i));
            fit.add(z,i,total,window);coherent+=z;
        }
        check(fit.pairs==windows/2 && fit.score()>110,
              "local comparisons failed when phase repeatedly turned within each symbol quarter");
        check(std::norm(coherent)/windows<1,
              "phase-winding fixture accidentally retained whole-symbol coherent evidence");
    }
    // Identical products, evenly divided across quarters, retain exactly three
    // quarters of their sum while retaining all four quarters' null variance.
    DifferentialAccumulator equal;
    for(std::uint64_t i=0;i<windows;++i)equal.add({1,0},i,total,window);
    close(equal.score(),144-std::log(8.));
    DifferentialAccumulator scaled;
    for(std::uint64_t i=0;i<windows;++i)scaled.add({17,0},i,total,window);
    close(scaled.score(),equal.score());
}

void weak_soft_windows() {
    constexpr std::uint64_t windows=16384,window=16,total=windows*window;
    std::mt19937_64 random(314159);std::normal_distribution<double> noise;
    DifferentialAccumulator fit;std::complex<double> coherent{};
    double noise_energy=0;
    for(std::uint64_t i=0;i<windows;++i) {
        const std::complex<double> n{noise(random),noise(random)};
        noise_energy+=std::norm(n);
        const auto z=std::polar(.5,.73+.021*static_cast<double>(i))+n;
        fit.add(z,i,total,window);coherent+=z;
    }
    check(.25*windows/noise_energy<.15,"soft fixture windows must have signal well below their noise");
    check(fit.score()>20,"weak local comparisons were hardened or lost before whole-symbol accumulation");
    check(std::norm(coherent)/noise_energy<5,"soft fixture unexpectedly has strong coherent evidence");
}

void isolated_quarters_gaps_and_nonfinite() {
    constexpr std::uint64_t windows=1024,window=16,total=windows*window;
    for(unsigned quarter=0;quarter<4;++quarter) {
        DifferentialAccumulator fit;
        for(std::uint64_t i=0;i<windows;++i)fit.add(i/(windows/4)==quarter?
            std::complex<double>{1,0}:std::complex<double>{},i,total,window);
        check(fit.pairs==128 && fit.score()==0,"one isolated quarter was promoted to a full symbol");
    }
    DifferentialAccumulator missing;
    missing.add({1,0},0,total,window);missing.add({1,0},3,total,window);
    check(missing.pairs==0,"local comparisons crossed a missing window");
    missing.add({1,0},4,total,window);missing.add({},5,total,window);missing.add({1,0},7,total,window);
    check(missing.pairs==0,"an invalid local window was bridged by a later phase comparison");
    missing.add({1,0},8,total,window);missing.add({std::numeric_limits<double>::infinity(),0},9,total,window);
    missing.add({1,0},10,total,window);missing.add({1,0},11,total,window);
    check(missing.pairs==1 && missing.score()==0,"invalid windows or too few pairs supplied evidence");
    missing.add({1,0},windows,total,window);missing.add({1,0},windows+1,total,window);
    check(missing.pairs==1,"partial or out-of-range windows were admitted to a complete symbol");
    missing.squared_magnitudes=std::numeric_limits<double>::infinity();missing.pairs=128;
    check(missing.score()==0,"nonfinite product energy supplied differential evidence");
}

void null_phase_bound() {
    // Independent uniform phases with arbitrary known magnitudes are a wider
    // conditional null family than constant-variance Gaussian window outputs.
    // Strong power changes and symbol-quarter-dependent magnitudes must not
    // become evidence for a pattern. Monte Carlo supplements the analytic bound
    // in the helper; it does not calibrate or relax the acceptance threshold.
    constexpr std::uint64_t windows=512,window=16,total=windows*window,trials=4096;
    constexpr std::array thresholds{.5,1.,2.,3.};
    std::array<std::uint64_t,thresholds.size()> exceeded{};
    std::mt19937_64 random(271828);std::uniform_real_distribution<double> phase(0,tau);
    for(std::uint64_t trial=0;trial<trials;++trial) {
        DifferentialAccumulator fit;
        for(std::uint64_t i=0;i<windows;++i) {
            const auto amplitude=.1+static_cast<double>((i*17+trial)%23)/7;
            fit.add(std::polar(amplitude,phase(random)),i,total,window);
        }
        const auto score=fit.score();
        check(std::isfinite(score)&&score>=0,"noise supplied an invalid differential score");
        for(std::size_t j=0;j<thresholds.size();++j)exceeded[j]+=score>=thresholds[j];
    }
    for(std::size_t j=0;j<thresholds.size();++j) {
        const auto bound=std::exp(-thresholds[j]);
        const auto margin=6*std::sqrt(trials*bound*(1-bound))+1;
        check(exceeded[j]<=trials*bound+margin,"seeded circular noise exceeded the conservative analytic tail bound");
    }
}

void selection_penalty() {
    close(combine_differential_evidence(3,4,true),4-std::numbers::ln2);
    close(combine_differential_evidence(5,2,true),5-std::numbers::ln2);
    check(combine_differential_evidence(.1,.2,true)==0 &&
          combine_differential_evidence(.123456789,100,false)==.123456789,
          "detector selection omitted its trial penalty or changed an ineligible baseline");
}
}

int main() {
    try {
        fixed_local_geometry();independent_whitening_reference();many_short_comparisons();weak_soft_windows();
        isolated_quarters_gaps_and_nonfinite();null_phase_bound();selection_penalty();
        std::cout<<"Bounded local differential evidence tests passed\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Bounded local differential evidence tests failed: "<<error.what()<<'\n';return 1;
    }
}
