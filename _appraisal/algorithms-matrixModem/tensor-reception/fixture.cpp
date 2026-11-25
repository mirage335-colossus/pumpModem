#include "fixture.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace matrix_rx {
namespace {
constexpr double pi = 3.141592653589793238462643383279502884;

std::uint64_t mix(std::uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

struct Rng {
    std::uint64_t state;
    std::uint64_t next() { state += UINT64_C(0x9e3779b97f4a7c15); return mix(state - UINT64_C(0x9e3779b97f4a7c15)); }
    double uniform() { return (static_cast<double>(next() >> 11) + 0.5) * (1.0 / 9007199254740992.0); }
    double normal() { return std::sqrt(-2.0 * std::log(uniform())) * std::cos(2.0 * pi * uniform()); }
};

struct C { double re, im; };
C add(C a, C b) { return {a.re+b.re, a.im+b.im}; }
C mul(C a, C b) { return {a.re*b.re-a.im*b.im, a.re*b.im+a.im*b.re}; }
C scale(C a, double b) { return {a.re*b,a.im*b}; }
C phasor(double p) { return {std::cos(p),std::sin(p)}; }
double abs2(C a) { return a.re*a.re+a.im*a.im; }

bool valid_channel(Channel c) {
    switch (c) {
    case Channel::flat: case Channel::moderate: case Channel::disturbed:
    case Channel::holdout: case Channel::noise: case Channel::interferer:
    case Channel::wrong_word: return true;
    }
    return false;
}

// Each path has a finite Gaussian-spectrum Doppler process represented by a
// small deterministic sum of sinusoids. The independent phases make the
// realization reproducible without pretending that a short fixture is a
// universal fading law.
C fading_tap(std::uint64_t seed, int path, double t, double rms_hz, double power) {
    Rng r{mix(seed ^ (UINT64_C(0x51ed2705) * (path + 1)))};
    C sum{0,0};
    constexpr int oscillators = 12;
    for (int j=0; j<oscillators; ++j) {
        const double f = rms_hz * r.normal();
        const double phase = 2*pi*r.uniform();
        sum = add(sum, phasor(2*pi*f*t+phase));
    }
    return scale(sum, std::sqrt(power/oscillators));
}

double smooth_frequency(double old_hz, double new_hz, int sample_in_pair) {
    if (sample_in_pair >= 8 || old_hz == new_hz) return new_hz;
    const double u = (sample_in_pair + 1.0) / 8.0;
    const double s = u*u*(3.0-2.0*u);
    return old_hz + (new_hz-old_hz)*s;
}

struct PairScore { double qh, ql, power; };

PairScore score_pair(const Shape& shape, const std::vector<Sample>& input,
                     int bit, int row, bool history) {
    const int start_index = row / shape.frequencies;
    const int frequency = row % shape.frequencies;
    const int start = start_index * timing_step;
    const double root_n = std::sqrt(static_cast<double>(symbol_samples));
    const double gamma = history ? history_gamma : 0.0;
    double q[2] = {0.0,0.0};
    double total_power = 0.0;
    for (int label=0; label<2; ++label) {
        const int sign = label == 1 ? 1 : -1;
        const int bin = tone_bin(shape, bit, frequency, sign);
        C z[2] = {{0,0},{0,0}};
        for (int look=0; look<2; ++look) {
            const int base = start + (2*bit+look)*symbol_samples;
            for (int n=0; n<symbol_samples; ++n) {
                const Sample x = input[static_cast<std::size_t>(base+n)];
                const double p = -2*pi*bin*n/fft_length;
                z[look] = add(z[look], mul({x.re,x.im}, phasor(p)));
            }
            z[look] = scale(z[look], 1.0/root_n);
            total_power += abs2(z[look]);
        }
        const C rot = phasor(-2*pi*bin/4.0);
        const C aligned = mul(rot,z[1]);
        const double p0=abs2(z[0]), p1=abs2(z[1]);
        const double cross=2.0*(z[0].re*aligned.re+z[0].im*aligned.im);
        const double assumed_power=likelihood_snr(shape);
        const double lp=assumed_power*(1.0+gamma), lm=assumed_power*(1.0-gamma);
        const double wp=lp/(1.0+lp), wm=lm/(1.0+lm);
        const double a=0.5*(wp+wm), b=0.5*(wp-wm);
        q[label] = a*(p0+p1)+b*cross-std::log((1.0+lp)*(1.0+lm));
    }
    return {q[1],q[0],total_power};
}
} // namespace

int code_sign(int word, int bit) {
    if (word < 0 || bit < 0) throw std::invalid_argument("negative code coordinate");
    const std::uint64_t key = mix(static_cast<std::uint64_t>(word) * UINT64_C(0xd6e8feb86659fd93) ^
                                  (static_cast<std::uint64_t>(bit) + UINT64_C(0xa0761d6478bd642f)));
    return (key & 1) ? 1 : -1;
}

int center_bin(int bit) {
    if (bit < 0) throw std::invalid_argument("negative bit index");
    // Nine separated 3.125 Hz grid centers, spanning 1.4 kHz.
    static constexpr int bins[] = {-224,-168,-112,-56,0,56,112,168,224};
    return bins[static_cast<unsigned>(bit) % (sizeof(bins)/sizeof(bins[0]))];
}

int tone_bin(const Shape& shape, int bit, int frequency, int sign) {
    if (frequency < 0 || frequency >= shape.frequencies || (sign != -1 && sign != 1))
        throw std::invalid_argument("invalid tone coordinate");
    return center_bin(bit) + (frequency - shape.frequencies/2) * tone_half_separation +
           sign * tone_half_separation;
}

const char* name(Channel channel) {
    switch (channel) {
    case Channel::flat: return "flat";
    case Channel::moderate: return "moderate";
    case Channel::disturbed: return "disturbed";
    case Channel::holdout: return "holdout";
    case Channel::noise: return "noise";
    case Channel::interferer: return "interferer";
    case Channel::wrong_word: return "wrong_word";
    }
    return "invalid";
}

void validate_shape(const Shape& s) {
    if (s.bits < 16 || s.bits > 2048 || s.words < 2 || s.words > 65536 ||
        s.starts < 1 || s.starts > 128 || s.frequencies < 1 || s.frequencies > 64)
        throw std::invalid_argument("fixture shape outside bounded experiment limits");
    const std::int64_t samples = 2LL*s.bits*symbol_samples + 1LL*(s.starts-1)*timing_step;
    const std::int64_t rows = 1LL*s.starts*s.frequencies;
    if (samples <= 0 || samples > 1100000 || rows <= 0 || rows > 8192)
        throw std::invalid_argument("fixture shape exceeds memory bounds");
    const int max_bin = 224 + std::max(s.frequencies/2, s.frequencies-s.frequencies/2) * tone_half_separation + tone_half_separation;
    if (max_bin >= fft_length/2) throw std::invalid_argument("tone grid aliases at Nyquist");
}

Scene render(const Shape& shape, Channel channel, double cn0_dbhz, std::uint64_t seed) {
    validate_shape(shape);
    if (!valid_channel(channel) || !std::isfinite(cn0_dbhz) || cn0_dbhz < -20.0 || cn0_dbhz > 30.0)
        throw std::invalid_argument("invalid channel or C/N0");
    Rng rng{mix(seed)};
    Scene scene;
    scene.input.resize(static_cast<std::size_t>(shape.opportunity_samples()));
    scene.word = static_cast<int>(rng.next() % static_cast<std::uint64_t>(shape.words));
    scene.start = static_cast<int>(rng.next() % static_cast<std::uint64_t>(shape.starts));
    scene.frequency = static_cast<int>(rng.next() % static_cast<std::uint64_t>(shape.frequencies));
    if (channel == Channel::noise || channel == Channel::interferer) scene.word = -1;
    if (channel == Channel::wrong_word) scene.word = shape.words; // deliberately outside codebook

    std::vector<C> tx(scene.input.size(), {0,0});
    if (channel != Channel::noise && channel != Channel::interferer) {
        const int first = scene.start*timing_step;
        const double amplitude = std::sqrt(std::pow(10.0,cn0_dbhz/10.0)/sample_rate);
        double phase=2*pi*rng.uniform();
        double old_hz=0;
        for (int bit=0; bit<shape.bits; ++bit) {
            const int sign=code_sign(scene.word,bit);
            const int bin=tone_bin(shape,bit,scene.frequency,sign);
            const double new_hz=bin*static_cast<double>(sample_rate)/fft_length;
            for (int n=0; n<2*symbol_samples; ++n) {
                const int pair_offset=2*bit*symbol_samples+n;
                const double f=smooth_frequency(old_hz,new_hz,n);
                phase += 2*pi*f/sample_rate;
                const int pos=first+pair_offset;
                if (pos >= 0 && static_cast<std::size_t>(pos)<tx.size())
                    tx[static_cast<std::size_t>(pos)]={amplitude*std::cos(phase),amplitude*std::sin(phase)};
            }
            old_hz=new_hz;
        }
        // Causal two-tap channel with preserved tails. The tap powers are
        // normalized, but a time-varying realization may fade locally.
        if (channel == Channel::moderate || channel == Channel::disturbed || channel == Channel::holdout) {
            const double rms= channel == Channel::moderate ? 1.0 : channel == Channel::disturbed ? 10.0 : 6.0;
            const int delay= channel == Channel::moderate ? 6 : channel == Channel::disturbed ? 22 : 13;
            const double echo_power= channel == Channel::moderate ? 0.18 : 0.35;
            std::vector<C> filtered(tx.size(),{0,0});
            for (std::size_t i=0; i<tx.size(); ++i) {
                const double t=static_cast<double>(i)/sample_rate;
                C h0=fading_tap(seed,0,t,rms,1.0-echo_power);
                C y=mul(h0,tx[i]);
                if (i>=static_cast<std::size_t>(delay)) {
                    C h1=fading_tap(seed,1,t,rms,echo_power);
                    y=add(y,mul(h1,tx[i-static_cast<std::size_t>(delay)]));
                }
                filtered[i]=y;
            }
            tx.swap(filtered);
            if (channel == Channel::holdout) {
                // Independent fractional carrier offset and phase ramp.
                for (std::size_t i=0; i<tx.size(); ++i)
                    tx[i]=mul(tx[i],phasor(2*pi*0.8*i/sample_rate));
            }
        }
    }

    const double interferer_amp=channel == Channel::interferer ? std::sqrt(std::pow(10.0,cn0_dbhz/10.0)/sample_rate)*0.6 : 0.0;
    const double interferer_phase=2*pi*rng.uniform();
    for (std::size_t i=0; i<scene.input.size(); ++i) {
        C x=tx[i];
        if (channel == Channel::interferer)
            x=add(x,scale(phasor(2*pi*(101.25*i/sample_rate)+interferer_phase),interferer_amp));
        // Proper complex noise with E|n|^2 = 1.
        x=add(x,{rng.normal()/std::sqrt(2.0),rng.normal()/std::sqrt(2.0)});
        scene.input[i]={static_cast<float>(x.re),static_cast<float>(x.im)};
    }
    return scene;
}

Evidence reference_row(const Shape& shape, const std::vector<Sample>& input, int row, bool history) {
    validate_shape(shape);
    if (input.size() != static_cast<std::size_t>(shape.opportunity_samples()) || row < 0 || row >= shape.rows())
        throw std::invalid_argument("reference input or row outside shape");
    Evidence out;
    out.contrast.resize(static_cast<std::size_t>(shape.bits));
    out.common.resize(1,0.0f);
    out.power.resize(1,0.0f);
    double common=0.0,power=0.0;
    for (int bit=0; bit<shape.bits; ++bit) {
        const PairScore s=score_pair(shape,input,bit,row,history);
        out.contrast[static_cast<std::size_t>(bit)]=static_cast<float>(s.qh-s.ql);
        common += s.qh+s.ql;
        power += s.power;
    }
    out.common[0]=static_cast<float>(common);
    out.power[0]=static_cast<float>(power);
    return out;
}

void check_fixture() {
    const Shape shape{16,32,3,5};
    validate_shape(shape);
    const Scene a=render(shape,Channel::flat,10.0,UINT64_C(0x123456789abcdef0));
    const Scene b=render(shape,Channel::flat,10.0,UINT64_C(0x123456789abcdef0));
    if (a.input.size()!=static_cast<std::size_t>(shape.opportunity_samples()) || a.word!=b.word || a.start!=b.start || a.frequency!=b.frequency)
        throw std::runtime_error("fixture reproducibility metadata failed");
    for (std::size_t i=0;i<a.input.size();++i) {
        if (a.input[i].re!=b.input[i].re || a.input[i].im!=b.input[i].im ||
            !std::isfinite(a.input[i].re) || !std::isfinite(a.input[i].im))
            throw std::runtime_error("fixture reproducibility/sample check failed");
    }
    const Evidence e=reference_row(shape,a.input,0,true);
    if (e.contrast.size()!=static_cast<std::size_t>(shape.bits) || e.common.size()!=1 || e.power.size()!=1 ||
        !std::isfinite(e.common[0]) || !std::isfinite(e.power[0]) || e.power[0] <= 0)
        throw std::runtime_error("reference feature finite/nontrivial check failed");
    for (float x:e.contrast) if (!std::isfinite(x)) throw std::runtime_error("non-finite reference contrast");
}

} // namespace matrix_rx
