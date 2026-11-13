#include "dense_acoustic_receiver.hpp"

#include "single_carrier_waveform.hpp"
#include "ssb_inverse.hpp"
#include "ssb_quiet.hpp"
#include "datapump/execution.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace datapump::fast::detail {
namespace {

using Complex = std::complex<double>;
constexpr auto tau = 2. * std::numbers::pi;
constexpr unsigned input_rate = 48000;
constexpr unsigned ticks_per_symbol = 48;
constexpr unsigned stride = 4;
constexpr unsigned frontend_radius = dense_acoustic_frontend_radius;
constexpr unsigned frontend_size = 2 * frontend_radius + 1;
constexpr unsigned pulse_steps = 64;
constexpr unsigned knot_rows = 48;
constexpr unsigned pilot_rows = 48;
constexpr unsigned path_count = 2;
constexpr unsigned parameters_per_knot = 2 * path_count;
constexpr unsigned parameter_band = 3 * parameters_per_knot - 1;
constexpr unsigned data_band = 47;
constexpr unsigned maximum_columns_per_row = 32;
constexpr unsigned tile_groups = 16;
constexpr unsigned context_symbols = 40;
constexpr unsigned maximum_marker_recovery_intervals=3;
// The slow edge of the public clock allowance can extend a nominal
// 4,800-row tile by two rows, plus endpoint rounding on the capture grid.
constexpr unsigned maximum_rows_per_tile = 4803;
constexpr double maximum_delay_ms = 22.;
constexpr double maximum_clock_ratio = 300.045e-6;
constexpr unsigned maximum_marker_correction_ticks=512;
constexpr unsigned training_duration = 2048 * ticks_per_symbol;
constexpr unsigned startup_period = 64 * ticks_per_symbol;
constexpr unsigned marker_duration = 192 * ticks_per_symbol;
constexpr unsigned startup_attempt_step = 4 * startup_period;
constexpr unsigned startup_context = training_duration + marker_duration + startup_attempt_step + 2048;
constexpr unsigned history_capacity = startup_context / stride + 1024;
constexpr unsigned startup_window_rows = 3 * pilot_rows;
constexpr double coarse_carrier_spacing = 75.;
// The declared dense receiver admits at most 160 Hz in either direction.
constexpr unsigned maximum_coarse_carrier_steps = 2;
constexpr unsigned maximum_coarse_carriers = 2*maximum_coarse_carrier_steps+1;
constexpr unsigned witness_first = 5 * startup_window_rows / 12;
constexpr unsigned witness_last = 7 * startup_window_rows / 12;
constexpr unsigned witness_gap = 2 * frontend_radius / stride;
constexpr std::size_t data_per_interval = physical_interval_bits / 2;
constexpr double unit_coordinate = .7071067811865475244;

using Bandpass = DenseAcousticBandpass;

Bandpass bandpass(double center) {
    constexpr double half_width=1050.;
    Bandpass taps{};
    double normalization=0.;
    for(int offset=-int(frontend_radius);offset<=int(frontend_radius);++offset) {
        const auto angle=std::numbers::pi*offset/frontend_radius;
        const auto window=.42+.5*std::cos(angle)+.08*std::cos(2.*angle);
        const auto sinc=offset?std::sin(tau*half_width*offset/input_rate)/(std::numbers::pi*offset):
            2.*half_width/input_rate;
        const auto mixing=std::cos(tau*center*offset/input_rate);
        const auto i=offset+frontend_radius;
        taps[i]=2.*window*sinc*mixing;
        normalization+=taps[i]*mixing;
    }
    for(auto& tap:taps)tap/=normalization;
    return taps;
}

struct Observation {
    double signal=0.;
    std::array<double,2> reference{};
};

struct Prediction {
    double residual=0., power=0., predicted_power=0.;
    std::size_t count=0;
};

struct AcquisitionPrior {
    double gain=0.,slope=0.;
};

struct Candidate {
    double error = std::numeric_limits<double>::infinity();
    double epoch = 0.;
    double delay = 0.;
    double clock = 0.;
    double frequency = 0.;
};

struct PublicSymbol {
    Complex known{};
    std::optional<std::size_t> data;
    bool witness = false;
};

struct DataCoefficient {
    unsigned column;
    Complex value{};
};

struct Row {
    std::array<Complex, path_count> known{}, witness{};
    std::array<std::array<DataCoefficient, maximum_columns_per_row>, path_count> unknown;
    std::array<unsigned, path_count> count{};
    double observed = 0.;
    double unknown_power = 0.;
    Complex rotation{};
};

double mean_square(std::span<const Complex> samples) {
    double power=0.;
    for(const auto sample:samples)power+=std::norm(sample);
    return samples.empty()?0.:power/samples.size();
}

bool fit_row(std::size_t i) { return i+witness_gap<witness_first || i>=witness_last+witness_gap; }

void report_symbol(const SymbolObserver& sink, Complex value) noexcept {
    if(sink)try {sink(static_cast<std::complex<float>>(value));}catch(...) {}
}

Complex interpolate(std::span<const Complex> samples,double position) {
    if(!(position>=1. && position+2.<samples.size()))return {};
    const auto i=static_cast<std::size_t>(position);
    const auto f=position-i;
    const auto a=samples[i-1],b=samples[i],c=samples[i+1],d=samples[i+2];
    return b+.5*f*(c-a+f*(2.*a-5.*b+4.*c-d+f*(3.*(b-c)+d-a)));
}

void add_normal(HermitianBand& matrix,std::span<Complex> rhs,std::size_t base,
                std::span<const double> features,double observed,double weight=1.) {
    for(std::size_t k=0;k<features.size();++k) {
        const auto at=base+k;
        rhs[at]+=weight*features[k]*observed;
        const auto row=matrix.lower_row(at);
        const auto first=matrix.first_column(at);
        for(std::size_t l=0;l<=k;++l)row[base+l-first]+=weight*features[k]*features[l];
    }
}

} // namespace

DenseAcousticBandpass dense_acoustic_bandpass(double center_hz) { return bandpass(center_hz); }

namespace {
double projected_marker(const Profile& p,std::uint64_t ordinal,DenseTiming timing) {
    const SingleCarrierSchedule schedule(p);
    if(ordinal>std::numeric_limits<std::size_t>::max())
        throw std::length_error("Fast dense public ordinal exhausted");
    const auto before=schedule.total_interval_symbols(static_cast<std::size_t>(ordinal));
    const auto symbol=double(schedule.training_count)+before+single_carrier_pulse_radius(p);
    return timing.epoch+symbol*timing.samples_per_symbol;
}
}

std::uint64_t dense_public_marker_tick(const Profile& p,std::uint64_t ordinal,DenseTiming timing) {
    const auto tick=projected_marker(p,ordinal,timing);
    if(!(tick>=0.&&tick<double(std::numeric_limits<std::uint64_t>::max()-p.sample_rate)))
        throw std::length_error("Fast dense public timeline exhausted");
    return static_cast<std::uint64_t>(std::ceil(tick));
}

bool dense_marker_timing_matches(const Profile& p,std::uint64_t ordinal,DenseTiming reference,
                                  DenseTiming observed,double allowance_samples) {
    const auto prior=projected_marker(p,ordinal,reference);
    const auto actual=projected_marker(p,ordinal,observed);
    const auto nominal=double(p.sample_rate)/p.symbol_rate;
    return std::abs(actual-prior)<allowance_samples&&
        std::abs(observed.samples_per_symbol-reference.samples_per_symbol)<.002*nominal;
}

std::uint64_t dense_acoustic_end_silence_samples(const Profile& p) {
    const SingleCarrierSchedule schedule(p);
    const auto group_symbols=schedule.pilot_stride+schedule.pilot_count;
    // A trailing pulse/path can spoil the first complete marker. Then allow
    // for the drain guard, a whole initial quiet unit, and a whole final tile
    // with its real future observation context.
    const auto extra_symbols=schedule.marker_span+single_carrier_pulse_radius(p)+
        2*context_symbols+maximum_delay_ms+(ssb_quiet_minimum_groups+tile_groups)*group_symbols+
        dense_quiet_veto_lookahead;
    return 6*input_rate+static_cast<std::uint64_t>(std::ceil(
        extra_symbols*ticks_per_symbol/(1.-maximum_clock_ratio)))+256+2*frontend_radius+
        maximum_marker_correction_ticks;
}

struct DenseAcousticReceiver::Impl {
    const Profile profile;
    const SingleCarrierSchedule schedule;
    const RrcPulseTable pulse;
    const double filtered_radius;
    const Bandpass signal_filter, lower_reference, upper_reference;
    const std::array<double,2> reference_scale;
    const ChannelBounds bounds;
    const std::optional<double> variation_hz;
    ValidatedIntervalSink sink;
    SymbolObserver observer,input_observer;
    DenseReceiverObservers observers;
    std::stop_token stop;
    SsbQuietRun quiet{input_rate,stride};
    std::vector<Observation> history;
    std::vector<Complex> periodic, marker, filtered_pulse;
    std::array<double,2*frontend_size> frontend_history{};
    std::size_t frontend_at=0;
    std::array<float,physical_interval_bits> llrs{};
    std::array<DensePilotReceipt,dense_units_per_interval> units{};
    std::array<std::optional<std::complex<float>>,dense_points_per_interval> points{};
    ModemProgress state;
    std::optional<Candidate> lock;
    std::size_t received=0, next_search=startup_context, ordinal=0, group=0, pending_holes=0;
    mutable std::size_t peak_workspace=0;
    std::uint64_t quiet_after=0;
    double demapping_offset=0.;
    std::size_t last_timing_marker=0;
    bool eof=false, interval_present=false;
    bool marker_scored=false;
    double tracked_power=0.;
    std::optional<double> noise_floor;
    bool timing_adopted=false;
    bool verified_cadence=false;
    struct TimingHint {
        DenseTiming timing;
        std::uint64_t ordinal;
        std::uint64_t first_tick;
        bool cadence_verified;
    };
    std::optional<TimingHint> timing_hint;

    Impl(const Profile& p,ValidatedIntervalSink callback,SymbolObserver out,SymbolObserver in,
         std::stop_token token,std::optional<ChannelBounds> declared,DenseReceiverObservers receipts,
         std::optional<ChannelAssumptions> assumptions)
        :profile(p),schedule(p),pulse(p.rolloff,single_carrier_pulse_radius(p)),
         filtered_radius(pulse.radius()+double(frontend_radius)/ticks_per_symbol),
         signal_filter(dense_acoustic_bandpass(p.carrier_hz)),lower_reference(bandpass(5200.)),
         upper_reference(bandpass(8600.)),
         reference_scale{filter_energy(signal_filter)/filter_energy(lower_reference),
             filter_energy(signal_filter)/filter_energy(upper_reference)},
         bounds(declared.value_or(ChannelBounds{300.,100.,maximum_delay_ms})),
         variation_hz(assumptions?std::optional(declared_variation_hz(*assumptions)):std::nullopt),
         sink(std::move(callback)),
         observer(std::move(out)),input_observer(std::move(in)),observers(std::move(receipts)),stop(token),
         history(history_capacity) {
        if(!dense_acoustic_framing(p)||(!sink&&!observers.interval))
            throw std::invalid_argument("Invalid dense Fast acoustic receiver");
        Transmitter source(profile,[](auto bits){std::fill(bits.begin(),bits.end(),0);return true;});
        std::vector<std::complex<float>> pcm(training_duration+marker_duration+256);
        if(source.read_analytic(pcm)!=pcm.size())throw std::logic_error("Dense Fast public waveform is incomplete");
        periodic.reserve(startup_period);
        marker.resize(pcm.size());
        std::array<Complex,frontend_size> tap_response{};
        for(int offset=-int(frontend_radius);offset<=int(frontend_radius);++offset)
            tap_response[offset+frontend_radius]=signal_filter[offset+frontend_radius]*
                std::polar(1.,tau*p.carrier_hz*offset/input_rate);
        for(std::size_t n=0;n<pcm.size();++n)
            pcm[n]*=std::polar(1.f,static_cast<float>(-tau*std::remainder(p.carrier_hz*n/input_rate,1.)));
        for(std::size_t n=0;n<pcm.size();++n) {
            for(int offset=-int(frontend_radius);offset<=int(frontend_radius);++offset) {
                const auto index=static_cast<std::int64_t>(n)+offset;
                if(index>=0 && index<static_cast<std::int64_t>(pcm.size()))
                    marker[n]+=tap_response[offset+frontend_radius]*static_cast<Complex>(pcm[index]);
            }
            if(n>=10*startup_period&&n<11*startup_period)periodic.push_back(marker[n]);
        }
        filtered_pulse.resize(2*static_cast<std::size_t>(filtered_radius)*pulse_steps+2);
        for(std::size_t i=0;i+1<filtered_pulse.size();++i) {
            const auto u=double(i)/pulse_steps-filtered_radius;
            for(int offset=-int(frontend_radius);offset<=int(frontend_radius);++offset)
                filtered_pulse[i]+=tap_response[offset+frontend_radius]*pulse(u+double(offset)/ticks_per_symbol);
        }
        peak_workspace=workspace();
    }

    static double filter_energy(const Bandpass& taps) {
        return std::inner_product(taps.begin(),taps.end(),taps.begin(),0.);
    }

    static double declared_variation_hz(const ChannelAssumptions& assumptions) {
        const auto spread=assumptions.doppler_hz*assumptions.margin;
        const auto am_rate=spread>0.?spread:.1;
        // Use the local two-sided spread declaration and the slow/fast
        // selective-AM ranges as a conservative local slope scale.
        return std::hypot(spread/2.,am_rate*assumptions.selective_fast_depth,
            .1*am_rate*assumptions.selective_slow_depth);
    }

    AcquisitionPrior weak_marker_prior(double observed_training_power,double floor) const {
        if(!variation_hz||!(floor>0.))return {};
        const auto public_power=.5*mean_square(periodic);
        const auto gain_variance=std::max(observed_training_power-floor,floor)/(4.*public_power);
        const auto half_seconds=.5*startup_window_rows*stride/input_rate;
        const auto slope_ratio=std::max(.05,std::pow(tau*half_seconds*variation_hz.value(),2));
        // The sixfold allowance also used by the predictive-improvement
        // check accounts approximately for correlation after the frontend.
        const auto gain_penalty=6.*floor/gain_variance;
        return {gain_penalty,gain_penalty/slope_ratio};
    }

    Complex pulse_at(double u) const {
        const auto x=(u+filtered_radius)*pulse_steps;
        if(x<0.||x>(filtered_pulse.size()-2))return {};
        const auto i=static_cast<std::size_t>(x);
        return filtered_pulse[i]+(x-i)*(filtered_pulse[i+1]-filtered_pulse[i]);
    }

    std::size_t workspace() const noexcept {
        return sizeof(*this)+history.capacity()*sizeof(Observation)+
            (marker.capacity()+periodic.capacity()+filtered_pulse.capacity())*sizeof(Complex)+
            pulse.workspace_bytes();
    }

    const Observation& at(std::uint64_t tick) const {
        const auto index=tick/stride;
        const auto available=received>frontend_radius?(received-frontend_radius+stride-1)/stride:0;
        if(tick<frontend_radius||index>=available||available-index>history.size())
            throw std::logic_error("Dense Fast receiver history lost supported audio");
        return history[index%history.size()];
    }

    double coordinate(const Candidate& c,double symbol) const noexcept {
        return c.epoch+(symbol+pulse.radius())*ticks_per_symbol/(1.+c.clock);
    }

    std::uint64_t center(const Candidate& c,double symbol) const {
        const auto tick=coordinate(c,symbol);
        if(!(tick>=0. && tick<double(std::numeric_limits<std::uint64_t>::max()-stride)))
            throw std::length_error("Dense Fast receiver timeline exhausted");
        return static_cast<std::uint64_t>(std::ceil(tick/stride))*stride;
    }

    Complex template_at(const Candidate& c,std::uint64_t tick,double delay,bool training) const {
        auto position=(double(tick)-c.epoch)*(1.+c.clock)-delay;
        if(training) {
            position=std::fmod(position,double(startup_period));
            if(position<0.)position+=startup_period;
            if(position<1.)position+=startup_period;
            if(position+2.>=startup_period) {
                const auto i=static_cast<std::size_t>(position)%startup_period;
                const auto f=position-std::floor(position);
                return (1.-f)*periodic[i]+f*periodic[(i+1)%startup_period];
            }
            return interpolate(periodic,position);
        }
        return interpolate(marker,position);
    }

    std::optional<double> estimate_floor(std::uint64_t first,std::size_t count) const {
        std::array<double,2> power{};
        for(std::size_t i=0;i<count;++i) {
            const auto& value=at(first+i*stride);
            for(unsigned band=0;band<power.size();++band)
                power[band]+=value.reference[band]*value.reference[band]*reference_scale[band];
        }
        const auto [lower,upper]=std::minmax(power[0],power[1]);
        if(upper>std::max(2.5*lower,1e-25))return {};
        return lower/count;
    }

    double noise_adjusted_error(Prediction fit,double floor,bool admission) const {
        const auto noise=floor*fit.count;
        const auto improvement=fit.power-fit.residual;
        if(!(fit.predicted_power>0. && fit.power>noise && improvement>0.))
            return std::numeric_limits<double>::infinity();
        // Compare independent predictive improvement with a margin for
        // correlated filtered noise. This is deliberately conservative
        // when the two observed reference bands overestimate the floor.
        if(admission && noise>0. && improvement<5.*std::sqrt(6.*floor*fit.predicted_power))
            return std::numeric_limits<double>::infinity();
        return std::max(0.,fit.residual-noise)/std::max(fit.power-noise,fit.power*.10);
    }

    Prediction acquisition_prediction(const Candidate& c,std::span<const std::uint64_t> starts,
                                      bool training,bool continuation=false,AcquisitionPrior prior={}) const {
        constexpr unsigned width=8;
        Prediction fit;
        for(const auto start:starts) {
            HermitianBand normal;
            normal.reset(width,width-1);
            std::array<Complex,8> rhs{};
            const auto features=[&](std::size_t i) {
                const auto tick=start+i*stride;
                const auto t=2.*i/startup_window_rows-1.;
                const auto phase=std::polar(1.,tau*(profile.carrier_hz*tick+
                    c.frequency*(double(tick)-start))/input_rate);
                const auto direct=phase*template_at(c,tick,0.,training||continuation);
                const auto delayed=phase*template_at(c,tick,c.delay,training||continuation);
                return std::array<double,8>{direct.real(),-direct.imag(),delayed.real(),-delayed.imag(),
                    t*direct.real(),-t*direct.imag(),t*delayed.real(),-t*delayed.imag()};
            };
            for(std::size_t i=0;i<startup_window_rows;++i) {
                if(!fit_row(i))continue;
                const auto f=features(i);
                add_normal(normal,std::span(rhs).first(width),0,std::span(f).first(width),at(start+i*stride).signal);
            }
            if(prior.gain>0.) {
                for(unsigned term=0;term<width;++term) {
                    auto& diagonal=normal.lower_row(term)[term-normal.first_column(term)];
                    diagonal+=term<width/2?prior.gain:prior.slope;
                }
            }
            if(normal.factor(.0002,stop)!=HermitianBand::FactorResult::complete||
               !normal.solve_in_place(std::span(rhs).first(width),stop))
                return {};
            for(std::size_t i=witness_first;i<witness_last;++i) {
                const auto f=features(i);
                const auto y=at(start+i*stride).signal;
                const auto prediction=std::inner_product(f.begin(),f.begin()+width,rhs.begin(),0.,
                    std::plus{},[](double x,Complex w){return x*w.real();});
                fit.residual+=(prediction-y)*(prediction-y);
                fit.power+=y*y;
                fit.predicted_power+=prediction*prediction;
                ++fit.count;
            }
        }
        return fit;
    }

    double acquisition_error(const Candidate& c,std::span<const std::uint64_t> starts,bool training,
                             double floor,bool continuation=false,bool admission=false,AcquisitionPrior prior={}) const {
        return noise_adjusted_error(acquisition_prediction(c,starts,training,continuation,prior),floor,admission);
    }

    template<std::size_t N>
    std::array<std::uint64_t,N> marker_windows(const Candidate& c,const std::array<unsigned,N>& offsets) const {
        std::array<std::uint64_t,N> starts{};
        for(std::size_t i=0;i<N;++i)starts[i]=center(c,schedule.training_count+offsets[i]);
        return starts;
    }

    std::vector<Candidate> repeated_word_phases(std::int64_t base,
        const std::array<std::uint64_t,10>& starts) const {
        // A per-window noncoherent proposal can accumulate power through
        // independently fading ray phases. It never authorizes lock.
        const auto steps=std::min(maximum_coarse_carrier_steps,
            static_cast<unsigned>(std::floor(bounds.frequency_hz/coarse_carrier_spacing+.5)));
        const auto count=2*steps+1;
        using Window=std::array<Complex,startup_window_rows>;
        std::array<double,maximum_coarse_carriers> frequencies{};
        std::array<Window,maximum_coarse_carriers> correction;
        std::array<Window,10> mixed;
        for(unsigned f=0;f<count;++f) {
            frequencies[f]=std::clamp((double(f)-steps)*coarse_carrier_spacing,
                -bounds.frequency_hz,bounds.frequency_hz);
            for(unsigned i=0;i<startup_window_rows;++i)
                correction[f][i]=std::polar(1.,-tau*frequencies[f]*i*stride/input_rate);
        }
        for(std::size_t w=0;w<starts.size();++w) {
            for(unsigned i=0;i<startup_window_rows;++i) {
                const auto tick=starts[w]+i*stride;
                mixed[w][i]=at(tick).signal*std::polar(1.,-tau*profile.carrier_hz*tick/input_rate);
            }
        }
        std::vector<Candidate> phases;
        phases.reserve(4*sync_symbols);
        peak_workspace=std::max(peak_workspace,workspace()+sizeof(correction)+sizeof(mixed)+
            phases.capacity()*sizeof(Candidate));
        for(unsigned phase=0;phase<startup_period;phase+=ticks_per_symbol/4) {
            if(stop.stop_requested())return {};
            Candidate c;
            c.epoch=base+phase;
            std::array<double,maximum_coarse_carriers> score{};
            for(std::size_t w=0;w<starts.size();++w) {
                std::array<Complex,maximum_coarse_carriers> cross{};
                double energy=0.;
                for(unsigned i=0;i<startup_window_rows;++i) {
                    const auto known=template_at(c,starts[w]+i*stride,0.,true);
                    const auto matched=mixed[w][i]*std::conj(known);
                    for(unsigned f=0;f<count;++f)cross[f]+=matched*correction[f][i];
                    energy+=std::norm(known);
                }
                for(unsigned f=0;f<count;++f)score[f]+=std::norm(cross[f])/std::max(energy,1e-12);
            }
            auto best=steps;
            for(unsigned f=0;f<count;++f)if(score[f]>score[best])best=f;
            c.error=-score[best];
            c.frequency=frequencies[best];
            phases.push_back(c);
        }
        return phases;
    }

    std::vector<Candidate> clock_candidates(std::span<const Candidate> ranked,
        std::int64_t base,std::uint64_t phase_anchor,double floor,bool weak) const {
        // The repeated word proposes the affine sample clock. The independent
        // alignment word still resolves its integer repetition and admits it.
        const std::array<std::uint64_t,8> windows{
            std::uint64_t(base+5*startup_period+640),std::uint64_t(base+12*startup_period+448),
            std::uint64_t(base+19*startup_period+512),std::uint64_t(base+27*startup_period+736),
            std::uint64_t(base+7*startup_period+416),std::uint64_t(base+13*startup_period+832),
            std::uint64_t(base+21*startup_period+320),std::uint64_t(base+26*startup_period+832)};
        const auto scoring=std::span(windows).first(weak?windows.size():4);
        const auto extent=bounds.clock_ppm*1e-6;
        const auto steps=std::max(1,static_cast<int>(std::ceil(bounds.clock_ppm/50.)));
        const auto trial_fit=[&](Candidate c,double clock,double offset,double frequency) {
            c.epoch+=(phase_anchor-c.epoch)*(clock-c.clock)/(1.+clock)+offset;
            c.clock=clock;
            c.frequency=frequency;
            c.error=acquisition_error(c,scoring,true,floor);
            return c;
        };
        std::vector<Candidate> candidates;
        for(const auto c:ranked|std::views::take(12)) {
            if(stop.stop_requested())return {};
            if(c.error>(weak?.82:.28))break;
            for(int i=-steps;i<=steps;++i)
                candidates.push_back(trial_fit(c,extent*i/steps,0.,c.frequency));
        }
        std::ranges::sort(candidates,{},&Candidate::error);
        if(candidates.size()>12)candidates.resize(12);
        for(auto& c:candidates) {
            for(const auto step:{20.,8.,3.}) {
                const auto prior=c;
                for(int rate=-1;rate<=1;++rate)for(int phase=-1;phase<=1;++phase) {
                    if(rate==0&&phase==0)continue;
                    const auto clock=std::clamp(prior.clock+rate*step*1e-6,-extent,extent);
                    const auto trial=trial_fit(prior,clock,phase*step*.12,prior.frequency);
                    if(trial.error<c.error)c=trial;
                }
                const auto centered=c;
                for(const int direction:{-1,1}) {
                    const auto frequency=std::clamp(centered.frequency+direction*step*.5,
                        -bounds.frequency_hz,bounds.frequency_hz);
                    const auto trial=trial_fit(centered,centered.clock,0.,frequency);
                    if(trial.error<c.error)c=trial;
                }
            }
        }
        std::ranges::sort(candidates,{},&Candidate::error);
        // Keep the simpler clock as a nested hypothesis. Optimizing short
        // fading windows can otherwise discard an exact nominal timing.
        unsigned nominal_candidates=0;
        for(const auto c:ranked) {
            if(c.frequency!=0.)continue;
            candidates.push_back(trial_fit(c,0.,0.,0.));
            if(++nominal_candidates==4)break;
        }
        return candidates;
    }

    std::optional<Candidate> acquire() const {
        // Cover every public word phase and the declared physical delay grid
        // within the oldest pending launch range. Shifted candidates and
        // continuation are evaluated on separate non-fit raw samples.
        const auto base=static_cast<std::int64_t>(next_search-startup_context);
        const std::array<std::uint64_t,10> starts{
            std::uint64_t(base+4*startup_period),
            std::uint64_t(base+6*startup_period+424),
            std::uint64_t(base+8*startup_period+1000),
            std::uint64_t(base+10*startup_period+400),
            std::uint64_t(base+12*startup_period+1500),
            std::uint64_t(base+14*startup_period+192),
            std::uint64_t(base+16*startup_period+320),
            std::uint64_t(base+18*startup_period+192),
            std::uint64_t(base+20*startup_period+1100),
            std::uint64_t(base+24*startup_period+580)};
        double energy=0.;
        for(const auto tick:starts)
            for(unsigned i=0;i<startup_window_rows;++i)energy+=std::pow(at(tick+i*stride).signal,2);
        if(energy<1e-24)return {};
        // The reference uses physical support disjoint from all startup and
        // marker model windows. It can propose a floor but cannot establish
        // that any occupied-band sample contains this noise alone.
        const auto floor=estimate_floor(base+2*startup_period,2*startup_period/stride-frontend_size/stride).value_or(0.);
        const bool weak=floor>0. && energy<10.*floor*starts.size()*startup_window_rows;
        // Pool the public repeated-word proposals noncoherently across
        // separate physical windows. A single local fade must not choose
        // the phase shortlist as a function of capture startup position.
        auto phases=repeated_word_phases(base,starts);
        if(phases.empty())return {};
        std::ranges::sort(phases,{},&Candidate::error);
        if(-phases.front().error<.06*energy)return {};
        const std::array strong_starts{starts[0],starts[2],starts[4],starts[6],starts[8],starts[9]};
        const std::span<const std::uint64_t> delay_windows=weak?std::span<const std::uint64_t>(starts):
            std::span<const std::uint64_t>(strong_starts);
        std::vector<Candidate> ranked;
        const auto scan=[&](Candidate c,unsigned step) {
            const auto last_delay=static_cast<unsigned>(std::ceil(bounds.delay_ms*48./step))*step;
            for(unsigned delay=0;delay<=last_delay;delay+=step) {
                if(stop.stop_requested())return;
                c.delay=delay;
                c.error=acquisition_error(c,delay_windows,true,floor);
                ranked.push_back(c);
            }
        };
        for(const auto c:phases|std::views::take(weak?24U:16U))scan(c,ticks_per_symbol/2);
        if(ranked.empty())return {};
        std::ranges::sort(ranked,{},&Candidate::error);
        const auto initial=std::min<std::size_t>(8,ranked.size());
        const auto carrier_steps=std::max(1,static_cast<int>(std::ceil(bounds.frequency_hz/25.)));
        for(std::size_t i=0;i<initial;++i) {
            const auto c=ranked[i];
            if(c.error>(weak?.82:.55)||stop.stop_requested())break;
            for(int step=-carrier_steps;step<=carrier_steps;++step) {
                auto trial=c;
                trial.frequency=bounds.frequency_hz*step/carrier_steps;
                if(trial.frequency==c.frequency)continue;
                trial.error=acquisition_error(trial,delay_windows,true,floor);
                ranked.push_back(trial);
            }
        }
        std::ranges::sort(ranked,{},&Candidate::error);
        const auto candidates=clock_candidates(ranked,base,starts[1],floor,weak);
        const std::array<std::uint64_t,6> timing_holdout{
            std::uint64_t(base+6*startup_period+1248),std::uint64_t(base+9*startup_period+768),
            std::uint64_t(base+14*startup_period+928),std::uint64_t(base+18*startup_period+1152),
            std::uint64_t(base+23*startup_period+960),std::uint64_t(base+28*startup_period+1280)};
        std::vector<Candidate> markers;
        for(const auto c:candidates) {
            if(stop.stop_requested())return {};
            if(c.error>(weak?.82:.28))continue;
            for(unsigned word=0;word<4;++word) {
                for(int delta=-12;delta<=12;delta+=4) {
                    auto trial=c;
                    trial.epoch+=word*startup_period/(1.+c.clock)+delta;
                    if(trial.epoch<base-ticks_per_symbol/4.)continue;
                    const auto windows=marker_windows(trial,std::array{4U,25U,47U});
                    if(windows.back()+startup_window_rows*stride+frontend_radius>=received)continue;
                    const auto marker_error=acquisition_error(trial,windows,false,floor);
                    if(marker_error>(weak?.68:.16))continue;
                    const auto timing_error=acquisition_error(trial,timing_holdout,true,floor);
                    if(timing_error>(weak?.75:.28))continue;
                    trial.error=marker_error+timing_error;
                    markers.push_back(trial);
                }
            }
        }
        std::ranges::sort(markers,{},&Candidate::error);
        const auto prior=weak?weak_marker_prior(energy/(starts.size()*startup_window_rows),floor):AcquisitionPrior{};
        for(const auto c:markers | std::views::take(6)) {
            const auto windows=marker_windows(c,std::array{73U,90U,113U,101U,125U,137U,149U,161U});
            const auto holdout=std::span(windows).first(weak?windows.size():3);
            if(*std::ranges::max_element(holdout)+startup_window_rows*stride+frontend_radius>=received)continue;
            const auto correct=acquisition_error(c,holdout,false,floor,false,true,prior);
            const auto continued=acquisition_error(c,holdout,false,floor,true,false,prior);
            auto shifted=c;
            shifted.epoch+=2*ticks_per_symbol;
            const auto wrong=acquisition_error(shifted,holdout,false,floor,false,false,prior);
            const auto decoy=std::min(continued,wrong);
            const bool verified=weak?correct<=.52&&decoy>=std::max(.72,correct+.32):
                correct<=.14&&decoy>=std::max(.30,4.*correct);
            if(verified) {
                auto accepted=c;
                accepted.error=correct;
                return accepted;
            }
        }
        return {};
    }

    PublicSymbol symbol(std::size_t i) const {
        if(i<schedule.training_count)return {schedule.training_symbol(i),{},false};
        const auto interval=schedule.interval_symbols(0);
        const auto offset=i-schedule.training_count;
        const auto k=offset%interval;
        const auto value=schedule.position(offset/interval,k);
        if(value.kind==SingleCarrierSchedule::Kind::data)
            return {{},offset/interval*data_per_interval+value.bit_offset/2,false};
        return {value.known_value,{},value.kind==SingleCarrierSchedule::Kind::pilot&&k%5==3};
    }

    std::vector<Row> rows(const Candidate& c,std::uint64_t first,std::uint64_t last,
                          std::span<const std::size_t> data_symbols) const {
        if(last<=first||(last-first)/stride>maximum_rows_per_tile)
            throw std::length_error("Dense Fast observation exceeds the tile budget");
        std::vector<Row> result;
        result.reserve((last-first)/stride);
        for(auto tick=first;tick<last;tick+=stride) {
            Row row{};
            row.observed=at(tick).signal;
            row.rotation=std::polar(1.,tau*(profile.carrier_hz*tick+c.frequency*(double(tick)-c.epoch))/input_rate);
            for(unsigned path=0;path<path_count;++path) {
                const auto u=((double(tick)-c.epoch)*(1.+c.clock)-(path?c.delay:0.))/ticks_per_symbol-pulse.radius();
                const auto lo=std::max(0.,std::ceil(u-filtered_radius));
                const auto hi=std::floor(u+filtered_radius);
                for(auto k=static_cast<std::size_t>(lo);double(k)<=hi;++k) {
                    const auto coefficient=profile.amplitude*pulse_at(u-k);
                    const auto s=symbol(k);
                    if(s.data) {
                        row.unknown_power+=std::norm(coefficient);
                        const auto index=std::lower_bound(data_symbols.begin(),data_symbols.end(),*s.data);
                        if(index==data_symbols.end()||*index!=*s.data)continue;
                        if(row.count[path]>=maximum_columns_per_row)
                            throw std::length_error("Dense Fast inverse exceeds its data column budget");
                        row.unknown[path][row.count[path]++]={static_cast<unsigned>(index-data_symbols.begin()),coefficient};
                    } else if(s.witness) {
                        row.witness[path]+=coefficient*s.known;
                        row.unknown_power+=std::norm(coefficient);
                    } else row.known[path]+=coefficient*s.known;
                }
            }
            result.push_back(std::move(row));
        }
        return result;
    }

    using GainKnots=std::vector<std::array<Complex,path_count>>;

    GainKnots fit(std::span<const Row> rows,std::span<const Complex> previous) const {
        const auto knots=(rows.size()+knot_rows-1)/knot_rows+1;
        HermitianBand normal;
        normal.reset(parameters_per_knot*knots,parameter_band);
        std::vector<Complex> rhs(parameters_per_knot*knots);
        for(std::size_t i=0;i<rows.size();++i) {
            if(i%128==0&&stop.stop_requested())return {};
            const auto& row=rows[i];
            const auto alpha=double(i%knot_rows)/knot_rows;
            std::array<double,2*parameters_per_knot> features{};
            double known_power=0.;
            for(unsigned path=0;path<path_count;++path) {
                auto baseband=row.known[path];
                for(const auto col:std::span(row.unknown[path]).first(row.count[path]))
                    baseband+=col.value*previous[col.column];
                known_power+=std::norm(baseband);
                const auto value=baseband*row.rotation;
                features[2*path]=(1.-alpha)*value.real();
                features[2*path+1]=-(1.-alpha)*value.imag();
                features[parameters_per_knot+2*path]=alpha*value.real();
                features[parameters_per_knot+2*path+1]=-alpha*value.imag();
            }
            const auto weight=1./(1.+6.*row.unknown_power/std::max(known_power,.0001));
            add_normal(normal,rhs,parameters_per_knot*(i/knot_rows),features,row.observed,weight);
        }
        constexpr std::array<double,3> curvature{1.,-2.,1.};
        for(std::size_t i=1;i+1<knots;++i) {
            for(unsigned j=0;j<parameters_per_knot;++j) {
                for(unsigned k=0;k<curvature.size();++k) {
                    const auto at=parameters_per_knot*(i-1+k)+j;
                    const auto r=normal.lower_row(at);
                    const auto b=normal.first_column(at);
                    for(unsigned l=0;l<=k;++l) {
                        const auto col=parameters_per_knot*(i-1+l)+j;
                        r[col-b]+=.002*curvature[k]*curvature[l];
                    }
                }
            }
        }
        if(normal.factor(.001,stop)!=HermitianBand::FactorResult::complete||!normal.solve_in_place(rhs,stop))return {};
        GainKnots gains(knots);
        for(std::size_t i=0;i<knots;++i)
            for(unsigned path=0;path<path_count;++path)
                gains[i][path]={rhs[parameters_per_knot*i+2*path].real(),
                    rhs[parameters_per_knot*i+2*path+1].real()};
        peak_workspace=std::max(peak_workspace,workspace()+normal.workspace_bytes()+
            rows.size_bytes()+rhs.capacity()*sizeof(Complex)+gains.capacity()*sizeof(GainKnots::value_type));
        return gains;
    }

    std::vector<float> invert(std::span<const Row> rows,const GainKnots& gains,std::span<Complex> posterior,
                              double& witness_error,double& witness_power,std::span<Complex> observed_points) const {
        HermitianBand normal;
        const auto columns=2*posterior.size();
        normal.reset(columns,data_band);
        std::vector<Complex> rhs(columns);
        for(std::size_t i=0;i<rows.size();++i) {
            if(i%128==0&&stop.stop_requested())return {};
            const auto& row=rows[i];
            const auto alpha=double(i%knot_rows)/knot_rows;
            std::array<DataCoefficient,2*maximum_columns_per_row> features{};
            unsigned count=0;
            double expected=0.,independent=0.;
            for(unsigned path=0;path<path_count;++path) {
                const auto left=gains[i/knot_rows][path],right=gains[i/knot_rows+1][path];
                const auto gain=(left+alpha*(right-left))*row.rotation;
                expected+=(gain*row.known[path]).real();
                independent+=(gain*row.witness[path]).real();
                for(const auto col:std::span(row.unknown[path]).first(row.count[path])) {
                    const auto end=features.begin()+count;
                    const auto match=std::find_if(features.begin(),end,[&](const auto& f){return f.column==col.column;});
                    if(match!=end)match->value+=col.value*gain;
                    else features[count++]={col.column,col.value*gain};
                }
            }
            const auto residual=row.observed-expected-independent;
            if(!count) {
                witness_error+=residual*residual;
                witness_power+=row.observed*row.observed;
            }
            std::ranges::sort(std::span(features).first(count),{},&DataCoefficient::column);
            for(unsigned j=0;j<2*count;++j) {
                const auto index=2*features[j/2].column+j%2;
                const auto feature=j%2?-features[j/2].value.imag():features[j/2].value.real();
                rhs[index]+=feature*residual;
                const auto r=normal.lower_row(index);
                const auto b=normal.first_column(index);
                for(unsigned k=0;k<=j;++k) {
                    const auto other=2*features[k/2].column+k%2;
                    if(other<b)throw std::logic_error("Dense Fast data inverse exceeded its band");
                    const auto v=k%2?-features[k/2].value.imag():features[k/2].value.real();
                    r[other-b]+=feature*v;
                }
            }
        }
        const auto ridge=std::max(6.*noise_floor.value_or(0.),.20*tracked_power);
        if(normal.factor(ridge,stop)!=HermitianBand::FactorResult::complete||!normal.solve_in_place(rhs,stop))return {};
        std::vector<double> diagonal(columns);
        if(!normal.inverse_diagonal(diagonal,stop))return {};
        std::vector<float> soft(columns);
        for(std::size_t i=0;i<posterior.size();++i) {
            std::array<double,2> metrics{};
            const Complex point{rhs[2*i].real(),rhs[2*i+1].real()};
            if(!observed_points.empty())observed_points[i]=point;
            square_qam_soft_demodulate(profile.constellation,point,metrics);
            for(unsigned axis=0;axis<2;++axis) {
                const auto index=2*i+axis;
                soft[index]=static_cast<float>(std::clamp(metrics[axis]/std::max(.12,ridge*diagonal[index]),-24.,24.));
            }
            posterior[i]={unit_coordinate*std::tanh(soft[2*i]*.5),unit_coordinate*std::tanh(soft[2*i+1]*.5)};
        }
        peak_workspace=std::max(peak_workspace,workspace()+normal.workspace_bytes()+rows.size_bytes()+
            rhs.capacity()*sizeof(Complex)+diagonal.capacity()*sizeof(double)+soft.capacity()*sizeof(float)+
            gains.capacity()*sizeof(GainKnots::value_type));
        return soft;
    }

    std::vector<std::size_t> data_columns(std::size_t first,std::size_t last) const {
        std::vector<std::size_t> result;
        for(auto k=first;k<last;++k)
            if(const auto s=symbol(k);s.data)result.push_back(*s.data);
        return result;
    }

    std::size_t first_symbol() const {
        return schedule.training_count+schedule.total_interval_symbols(ordinal);
    }

    std::size_t groups() const noexcept {
        return (schedule.data_symbols+schedule.pilot_stride-1)/schedule.pilot_stride;
    }

    std::size_t group_symbol(std::size_t index) const {
        return first_symbol()+schedule.marker_symbols(ordinal)+index*(schedule.pilot_count+schedule.pilot_stride);
    }

    struct PilotPrediction { Prediction fit; double context_error; };

    std::optional<PilotPrediction> pilot_prediction(const Candidate& c,std::size_t start_symbol,
        std::size_t word_symbols,std::uint64_t first,bool compare_context) const {
        if(first+startup_window_rows*stride+frontend_radius>=received)return {};
        HermitianBand normal;
        normal.reset(8,7);
        HermitianBand context_normal;
        if(compare_context)context_normal.reset(8,7);
        std::array<Complex,8> rhs{},context_rhs{};
        const auto features=[&](std::size_t i) {
            const auto tick=first+i*stride;
            const auto u=(double(tick)-c.epoch)*(1.+c.clock)/ticks_per_symbol-pulse.radius();
            const auto phase=std::polar(1.,tau*(profile.carrier_hz*tick+c.frequency*(double(tick)-c.epoch))/input_rate);
            std::array<Complex,path_count> predicted{},context{};
            for(unsigned path=0;path<path_count;++path) {
                const auto delayed=u-(path?c.delay/ticks_per_symbol:0.);
                const auto lo=std::max(0.,std::ceil(delayed-filtered_radius));
                const auto hi=std::floor(delayed+filtered_radius);
                for(auto k=static_cast<std::size_t>(lo);double(k)<=hi;++k) {
                    const auto known=symbol(k);
                    if(known.data)continue;
                    const auto contribution=known.known*(profile.amplitude*pulse_at(delayed-k));
                    predicted[path]+=contribution;
                    if(compare_context&&(k<start_symbol||k-start_symbol>=word_symbols))context[path]+=contribution;
                }
                predicted[path]*=phase;
                context[path]*=phase;
            }
            const auto t=2.*i/startup_window_rows-1.;
            const auto design=[t](Complex a,Complex b) {
                return std::array<double,8>{a.real(),-a.imag(),b.real(),-b.imag(),
                    t*a.real(),-t*a.imag(),t*b.real(),-t*b.imag()};
            };
            return std::pair{design(predicted[0],predicted[1]),design(context[0],context[1])};
        };
        for(std::size_t i=0;i<startup_window_rows;++i) {
            if(!fit_row(i))continue;
            const auto [full,context]=features(i);
            const auto sample=at(first+i*stride).signal;
            add_normal(normal,rhs,0,full,sample);
            if(compare_context)add_normal(context_normal,context_rhs,0,context,sample);
        }
        if(normal.factor(.001,stop)!=HermitianBand::FactorResult::complete||!normal.solve_in_place(rhs,stop)||
           (compare_context&&(context_normal.factor(.001,stop)!=HermitianBand::FactorResult::complete||
            !context_normal.solve_in_place(context_rhs,stop))))return {};
        Prediction prediction;
        double context_error=0.;
        for(std::size_t i=witness_first;i<witness_last;++i) {
            const auto [full,context]=features(i);
            const auto y=at(first+i*stride).signal;
            const auto dot=[](const auto& design,const auto& gains) {
                return std::inner_product(design.begin(),design.end(),gains.begin(),0.,std::plus{},
                    [](double value,Complex weight){return value*weight.real();});
            };
            const auto predicted=dot(full,rhs);
            prediction.residual+=std::pow(y-predicted,2);
            prediction.power+=y*y;
            prediction.predicted_power+=predicted*predicted;
            ++prediction.count;
            if(compare_context)context_error+=std::pow(y-dot(context,context_rhs),2);
        }
        return PilotPrediction{prediction,context_error};
    }

    // Neighboring known words may interrupt absence but cannot on their own
    // authorize payload LLRs for this interval.
    std::optional<bool> score_pilot(const Candidate& c,std::size_t start_symbol,std::size_t word_symbols) const {
        const auto result=pilot_prediction(c,start_symbol,word_symbols,center(c,start_symbol+2),true);
        if(!result)return {};
        return noise_adjusted_error(result->fit,noise_floor.value_or(0.),true)<.50&&
            result->context_error-result->fit.residual>.12*result->fit.power;
    }

    bool classify_quiet(std::uint64_t first,std::uint64_t last,unsigned present) {
        std::vector<Complex> samples;
        samples.reserve((last-first)/stride);
        for(auto tick=first;tick<last;tick+=stride)samples.emplace_back(at(tick).signal,0.);
        const auto energy=mean_square(samples);
        const auto ceiling=std::max(1.35*noise_floor.value_or(0.),tracked_power*.03);
        if(present||energy>ceiling) {
            if(observers.quiet&&verified_cadence)observers.quiet({first,last},false);
            quiet.interrupt();
            quiet_after=last+static_cast<std::uint64_t>((filtered_radius+context_symbols)*ticks_per_symbol+
                bounds.delay_ms*48.);
            if(present&&energy>0.)tracked_power=.97*tracked_power+.03*energy;
            return false;
        }
        if(first<quiet_after||!(tracked_power>0.)) {
            if(observers.quiet&&verified_cadence)observers.quiet({first,last},false);
            quiet.interrupt();
            return false;
        }
        // A completed public non-detection may count only under the
        // independently measured conservative input-floor ceiling.
        if(observers.quiet) {
            if(verified_cadence)observers.quiet({first,last},true);
        }
        else state.physical_complete=quiet.observe({first,stride,samples},{first,last},
                SsbPublicPresence::no_detection,ceiling);
        return true;
    }

    bool score_unit(std::size_t first_group,std::size_t last_group,const Candidate& payload_timing) {
        const auto& c=*lock;
        const auto first=center(c,group_symbol(first_group));
        const auto last=center(c,group_symbol(last_group));
        if(last<=first||last+frontend_radius>=received)return false;
        unsigned present=0;
        for(auto i=first_group;i<last_group;++i) {
            const auto symbol=group_symbol(i)+schedule.pilot_stride;
            const auto result=pilot_prediction(payload_timing,symbol,schedule.pilot_count,center(c,symbol+2),true);
            if(!result) {
                if(observers.quiet&&verified_cadence)observers.quiet({first,last},false);
                quiet.interrupt();
                return false;
            }
            present+=noise_adjusted_error(result->fit,noise_floor.value_or(0.),true)<.50&&
                result->context_error-result->fit.residual>.12*result->fit.power;
        }
        if(present&&observers.presence&&state.acquired)observers.presence({first,last});
        if(last_group-first_group<ssb_quiet_minimum_groups) {
            quiet.interrupt();
            return false;
        }
        const auto absent=classify_quiet(first,last,present);
        auto& unit=units[first_group/dense_pilot_groups_per_unit];
        unit={double(present)/(last_group-first_group),present>=2?DensePresence::present:
            absent?DensePresence::absent:DensePresence::unscored,ReceivedSignalRange{first,last},{}};
        return present>=2;
    }

    unsigned marker_hits(const Candidate& c,std::size_t begin,std::span<const unsigned> parts) const {
        return static_cast<unsigned>(std::ranges::count_if(parts,[&](unsigned part) {
            return score_pilot(c,begin+part*24,24).value_or(false);
        }));
    }

    bool authorize_adopted_marker(std::size_t begin) {
        constexpr std::array proposal{0U,2U,4U,6U};
        constexpr std::array holdout{1U,3U,5U,7U};
        const auto current=*lock;
        std::array<std::pair<Candidate,unsigned>,4> best{};
        best.front()={current,marker_hits(current,begin,proposal)};
        // The borrowed cadence identifies the ordinal but not the path.
        // Search a fixed delay grid on alternating parts, then admit only
        // the same candidate on the separate held-out parts.
        const auto limit=static_cast<unsigned>(std::floor(bounds.delay_ms*48.));
        for(unsigned delay=ticks_per_symbol;delay<=limit;delay+=ticks_per_symbol) {
            if(stop.stop_requested())return false;
            auto trial=current;
            trial.delay=delay;
            const auto count=marker_hits(trial,begin,proposal);
            const auto position=std::ranges::find_if(best,[&](const auto& entry){return entry.second<count;});
            if(position==best.end())continue;
            std::move_backward(position,best.end()-1,best.end());
            *position={trial,count};
        }
        for(const auto& [trial,count]:best) {
            if(count<2)break;
            if(marker_hits(trial,begin,holdout)>=3) {
                lock=trial;
                return true;
            }
        }
        return false;
    }

    double tracking_range(std::size_t interval_count) const {
        const auto ticks=schedule.interval_symbols(0)*double(ticks_per_symbol);
        const auto uncertainty=(bounds.clock_ppm*1e-6+std::abs(lock->clock))*ticks*interval_count;
        return 6.+std::ceil(uncertainty/(1.-maximum_clock_ratio));
    }

    std::optional<Candidate> track_recurring_marker(const Candidate& cadence,std::size_t begin) const {
        if(!ordinal||!state.acquired)return {};
        // Bound each scheduled attempt's aperture, while still testing a
        // later informative marker after any number of ambiguous markers.
        const auto elapsed=std::min(ordinal-last_timing_marker,std::size_t(maximum_marker_recovery_intervals));
        constexpr std::array<unsigned,4> proposal{0,2,4,6};
        constexpr std::array<unsigned,4> holdout{1,3,5,7};
        const auto first=center(cadence,begin),last=center(cadence,begin+schedule.marker_symbols(ordinal));
        double power=0.;
        for(auto tick=first;tick<last;tick+=stride)power+=std::pow(at(tick).signal,2);
        if(power*stride/(last-first)<=std::max(3.*noise_floor.value_or(0.),.09*tracked_power))return {};

        const auto metric=[&](const Candidate& candidate,std::span<const unsigned> parts,bool verify) {
            double squared_error=0.,measured=0.;
            unsigned matched=0;
            for(const auto part:parts) {
                const auto symbol=begin+part*24;
                const auto score=pilot_prediction(candidate,symbol,24,center(cadence,symbol+2),verify);
                if(!score)return std::pair{std::numeric_limits<double>::infinity(),0U};
                squared_error+=score->fit.residual;
                measured+=score->fit.power;
                if(verify&&noise_adjusted_error(score->fit,noise_floor.value_or(0.),true)<.50&&
                   score->context_error-score->fit.residual>.12*score->fit.power)++matched;
            }
            return std::pair{measured>0.?squared_error/measured:std::numeric_limits<double>::infinity(),matched};
        };
        const auto limit=static_cast<int>(std::ceil(tracking_range(elapsed)/8.))*8;
        auto best=cadence;
        const auto initial=metric(cadence,proposal,false).first;
        auto best_error=initial;
        auto try_offset=[&](double offset) {
            auto trial=cadence;
            trial.epoch+=std::clamp(offset,double(-limit),double(limit));
            const auto error=metric(trial,proposal,false).first;
            if(error<best_error) {best=trial;best_error=error;}
        };
        for(int offset=-limit;offset<=limit;offset+=8) {
            if(stop.stop_requested())return {};
            try_offset(offset);
        }
        const auto coarse=best.epoch-cadence.epoch;
        for(int offset=-6;offset<=6;offset+=2)try_offset(coarse+offset);
        if(!(best_error+.008<initial))return {};
        const auto before=metric(cadence,holdout,true);
        const auto after=metric(best,holdout,true);
        auto decoy=best;
        decoy.epoch+=2*ticks_per_symbol;
        if(after.second<3||!(after.first+.005<before.first)||metric(decoy,holdout,false).first<=after.first+.12)
            return {};
        return best;
    }

    void score_interval_marker() {
        const auto begin=first_symbol();
        const bool newly_verified=timing_adopted&&!state.acquired&&authorize_adopted_marker(begin);
        const auto c=*lock;
        const auto first=center(c,begin),last=center(c,begin+schedule.marker_symbols(ordinal));
        const auto reanchored=track_recurring_marker(c,begin);
        if(reanchored) {
            const auto corrected_last=center(*reanchored,begin+schedule.marker_symbols(ordinal));
            demapping_offset=std::clamp(demapping_offset+c.epoch-reanchored->epoch,
                -double(maximum_marker_correction_ticks),double(maximum_marker_correction_ticks));
            lock=*reanchored;
            last_timing_marker=ordinal;
            quiet.interrupt();
            if(observers.quiet&&verified_cadence)observers.quiet({first,last},false);
            if(observers.presence)observers.presence({first,std::max(last,corrected_last)});
            quiet_after=std::max(last,corrected_last)+static_cast<std::uint64_t>(
                (filtered_radius+context_symbols)*ticks_per_symbol+bounds.delay_ms*48.);
            if(observers.timing)observers.timing(ordinal,{lock->epoch,ticks_per_symbol/(1.+lock->clock),lock->frequency});
            marker_scored=true;
            return;
        }
        unsigned present=0;
        for(unsigned part=0;part<ssb_quiet_minimum_groups;++part) {
            const auto result=score_pilot(c,begin+part*24,24);
            if(!result) {
                if(observers.quiet&&verified_cadence)observers.quiet({first,last},false);
                quiet.interrupt();
                marker_scored=true;
                return;
            }
            present+=*result;
        }
        if(present>=2) {
            if(newly_verified) {
                state.acquired=true;
                timing_adopted=false;
                verified_cadence=true;
                last_timing_marker=ordinal;
            }
            if(state.acquired&&std::abs(demapping_offset)<6.)last_timing_marker=ordinal;
            if(ordinal && state.acquired && observers.timing)
                observers.timing(ordinal,{c.epoch,ticks_per_symbol/(1.+c.clock),c.frequency});
        }
        if(present&&observers.presence&&state.acquired)observers.presence({first,last});
        (void)classify_quiet(first,last,present);
        marker_scored=true;
    }

    void refine_delay(std::size_t last_group) {
        if(!state.acquired||last_group-group<dense_pilot_groups_per_unit)return;
        const auto current=*lock;
        const auto witness=[&](const Candidate& c,std::size_t offset) {
            return score_pilot(c,group_symbol(group+offset)+schedule.pilot_stride,schedule.pilot_count).value_or(false);
        };
        const auto hits=[&](const Candidate& c,const std::array<unsigned,4>& offsets) {
            return std::ranges::count_if(offsets,[&](auto i){return witness(c,i);});
        };
        constexpr std::array fit{0U,2U,4U,6U};
        constexpr std::array verify{1U,3U,5U,7U};
        const auto existing=hits(current,fit);
        if(existing>=3)return;
        // A full current public pilot unit must first be known present in
        // power. Below that level no alternative delay may turn silence into
        // a candidate by searching for accidental correlations.
        const auto first=center(current,group_symbol(group));
        const auto last=center(current,group_symbol(group+dense_pilot_groups_per_unit));
        double power=0.;
        for(auto tick=first;tick<last;tick+=stride)power+=std::pow(at(tick).signal,2);
        if(power*stride/(last-first)<=std::max(2.*noise_floor.value_or(0.),.06*tracked_power))return;
        Candidate best=current;
        auto best_hits=existing;
        const auto limit=static_cast<unsigned>(std::floor(bounds.delay_ms*48.));
        for(unsigned delay=0;delay<=limit;delay+=ticks_per_symbol) {
            if(stop.stop_requested())return;
            auto trial=current;
            trial.delay=delay;
            const auto count=hits(trial,fit);
            if(count>best_hits) {
                best_hits=count;
                best=trial;
            }
        }
        if(best_hits<3||best.delay==current.delay)return;
        const auto old=hits(current,verify),improved=hits(best,verify);
        if(improved>=3&&improved>old)lock=best;
    }

    Candidate refine_tile_timing(const Candidate& cadence,std::size_t last_group) {
        auto current=cadence;
        current.epoch+=demapping_offset;
        if(!state.acquired||last_group-group<tile_groups)return current;
        const auto first=center(cadence,group_symbol(group));
        const auto last=center(cadence,group_symbol(last_group));
        double power=0.;
        for(auto tick=first;tick<last;tick+=stride)power+=std::pow(at(tick).signal,2);
        if(power*stride/(last-first)<=std::max(3.*noise_floor.value_or(0.),.09*tracked_power))return current;

        constexpr std::array<unsigned,4> proposal{1,5,9,13};
        constexpr std::array<unsigned,4> holdout{3,7,11,15};
        const auto error=[&](const Candidate& c,std::span<const unsigned> offsets) {
            double residual=0.,observed=0.;
            for(const auto offset:offsets) {
                const auto symbol=group_symbol(group+offset)+schedule.pilot_stride;
                const auto score=pilot_prediction(c,symbol,schedule.pilot_count,center(cadence,symbol+2),false);
                if(!score)return std::numeric_limits<double>::infinity();
                residual+=score->fit.residual;
                observed+=score->fit.power;
            }
            return observed>0.?residual/observed:std::numeric_limits<double>::infinity();
        };
        auto best=current;
        const auto range=tracking_range(std::min(ordinal-last_timing_marker+1,
            std::size_t(maximum_marker_recovery_intervals)));
        const auto original=error(current,proposal);
        auto best_error=original;
        for(int delta=-6;delta<=6;delta+=2) {
            if(stop.stop_requested())return current;
            if(!delta)continue;
            auto trial=current;
            trial.epoch=cadence.epoch+std::clamp(demapping_offset+delta,-range,range);
            const auto measured=error(trial,proposal);
            if(measured<best_error) {
                best=trial;
                best_error=measured;
            }
        }
        if(!(best_error+.008<original))return current;
        const auto original_holdout=error(current,holdout);
        const auto improved=error(best,holdout);
        if(!(improved+.005<original_holdout))return current;
        demapping_offset=best.epoch-cadence.epoch;
        return best;
    }

    void process_tile(std::size_t last_group) {
        const auto c=*lock;
        const auto first_core=group_symbol(group),past_core=group_symbol(last_group);
        const auto first_observed=first_core-context_symbols;
        const auto past_observed=past_core+context_symbols;
        const auto first=center(c,first_observed),last=center(c,past_observed);
        if(last+frontend_radius>=received)return;
        refine_delay(last_group);
        const auto payload_timing=refine_tile_timing(*lock,last_group);
        bool present=false;
        for(auto i=group;i<last_group;) {
            const auto end=std::min(last_group,i+ssb_quiet_minimum_groups);
            present|=score_unit(i,end,payload_timing);
            if(state.physical_complete)return;
            i=end;
        }
        if(present&&state.acquired) {
                const auto timing_halo=static_cast<std::size_t>(std::ceil(std::abs(demapping_offset)/ticks_per_symbol));
                const auto columns=data_columns(first_observed-context_symbols-timing_halo,
                    past_observed+context_symbols+timing_halo);
            if(!columns.empty()) {
                const auto observations=rows(payload_timing,first,last,columns);
                std::vector<Complex> posterior(columns.size());
                std::vector<Complex> observed_points(observer?columns.size():0);
                std::vector<float> soft;
                double error=0.,power=0.;
                // Two locally bounded passes. Only the first uses public
                // predictors for the reported held-out quality estimate.
                for(unsigned pass=0;pass<2;++pass) {
                    const auto knots=fit(observations,posterior);
                    if(knots.empty())return;
                    double residual=0.,signal=0.;
                    soft=invert(observations,knots,posterior,residual,signal,observed_points);
                    if(soft.empty())return;
                    if(pass==0) {error=residual;power=signal;}
                }
                if(power>0.) {
                    state.evm=std::sqrt(error/power);
                    for(auto unit=group/dense_pilot_groups_per_unit;unit<last_group/dense_pilot_groups_per_unit;++unit)
                        if(units[unit].presence==DensePresence::present)units[unit].evm=state.evm;
                }
                const auto first_data=ordinal*data_per_interval+group*schedule.pilot_stride;
                const auto past_data=ordinal*data_per_interval+last_group*schedule.pilot_stride;
                for(std::size_t k=0;k<columns.size();++k) {
                    const auto data=columns[k];
                    if(data<first_data||data>=past_data)continue;
                    const auto position=2*(data%data_per_interval);
                    if(units[position/dense_bits_per_unit].presence!=DensePresence::present)continue;
                    llrs[position]=soft[2*k];
                    llrs[position+1]=soft[2*k+1];
                    if(observer) {
                        if(observers.interval)points[position/2]=static_cast<std::complex<float>>(observed_points[k]);
                        else report_symbol(observer,observed_points[k]);
                    }
                }
                interval_present=true;
                state.last_present_signal=ReceivedSignalRange{center(c,first_core),center(c,past_core)};
            }
        }
        group=last_group;
        if(group!=groups())return;
        if(observers.interval) {
            const ReceivedSignalRange support{center(c,first_symbol()),center(c,group_symbol(groups()))};
            const auto selected_points=observer?std::span<const std::optional<std::complex<float>>>(points):
                std::span<const std::optional<std::complex<float>>>{};
            observers.interval({ordinal,support,units,llrs,selected_points});
            ++state.intervals;
            state.erased_intervals+=!interval_present;
        } else if(interval_present) {
            const std::array<float,physical_interval_bits> erasure{};
            while(pending_holes&&!stop.stop_requested()) {
                (void)sink(erasure,false);
                --pending_holes;
                ++state.intervals;
                ++state.erased_intervals;
            }
            if(!stop.stop_requested()) {
                (void)sink(llrs,false);
                ++state.intervals;
            }
        } else ++pending_holes;
        ++ordinal;
        state.symbols+=schedule.interval_symbols(0);
        group=0;
        interval_present=false;
        units.fill({});
        points.fill({});
        marker_scored=false;
        llrs.fill(0);
    }

    void service() {
        if(!lock&&received>=next_search&&(!timing_hint||received<timing_hint->first_tick)) {
            lock=acquire();
            next_search+=startup_attempt_step;
            if(!lock||stop.stop_requested())return;
            state.acquired=true;
            verified_cadence=true;
            state.symbols=schedule.training_count;
            state.carrier_error_hz=lock->frequency;
            state.clock_error_ppm=lock->clock*1e6;
            if(observers.timing)observers.timing(0,{lock->epoch,ticks_per_symbol/(1.+lock->clock),lock->frequency});
            const auto anchor=center(*lock,schedule.training_count);
            double energy=0.;
            for(unsigned i=0;i<8*pilot_rows;++i)energy+=std::pow(at(anchor+i*stride).signal,2);
            tracked_power=energy/(8*pilot_rows);
            const auto coarse_start=next_search-startup_attempt_step-startup_context;
            noise_floor=estimate_floor(coarse_start+2*startup_period,
                2*startup_period/stride-frontend_size/stride);
        }
        if(timing_hint&&received>=timing_hint->first_tick) {
            const auto hint=*timing_hint;
            timing_hint.reset();
            if(!lock)install_timing(hint.timing,hint.ordinal,hint.cadence_verified);
        }
        if(!lock)return;
        while(!state.physical_complete&&!stop.stop_requested()) {
            const auto past=std::min(group+tile_groups,groups());
            if(center(*lock,group_symbol(past)+context_symbols)+frontend_radius>=received)return;
            if(!marker_scored) {
                score_interval_marker();
                if(state.physical_complete)return;
            }
            const auto before=group;
            const auto old_ordinal=ordinal;
            process_tile(past);
            if(group==before&&ordinal==old_ordinal)return;
        }
    }

    bool adopt_timing(DenseTiming timing,std::uint64_t next_ordinal,bool cadence_verified) {
        if(lock||timing_hint||next_ordinal==0||next_ordinal>std::numeric_limits<std::size_t>::max()/schedule.interval_symbols(0)||
           !(std::isfinite(timing.epoch)&&std::isfinite(timing.samples_per_symbol)&&
               std::isfinite(timing.carrier_hz_offset))||timing.samples_per_symbol<=0.)return false;
        const auto first=schedule.training_count+schedule.total_interval_symbols(static_cast<std::size_t>(next_ordinal));
        const auto tick=timing.epoch+(first+pulse.radius())*timing.samples_per_symbol;
        if(tick<double(received)||tick>double(received)+input_rate*8.||
           std::abs(timing.carrier_hz_offset)>bounds.frequency_hz)return false;
        timing_hint=TimingHint{timing,next_ordinal,static_cast<std::uint64_t>(std::ceil(tick)),cadence_verified};
        return true;
    }

    void install_timing(DenseTiming timing,std::uint64_t next_ordinal,bool cadence_verified) {
        const auto requested_clock=ticks_per_symbol/timing.samples_per_symbol-1.;
        const auto clock=std::clamp(requested_clock,-bounds.clock_ppm*1e-6,bounds.clock_ppm*1e-6);
        Candidate candidate;
        const auto first=schedule.training_count+schedule.total_interval_symbols(static_cast<std::size_t>(next_ordinal));
        // Keep the copied marker center invariant if the other model's
        // startup rate has a looser admission envelope.
        candidate.epoch=timing.epoch+(first+pulse.radius())*
            (timing.samples_per_symbol-ticks_per_symbol/(1.+clock));
        candidate.clock=clock;
        candidate.frequency=timing.carrier_hz_offset;
        // Take the transferred floor from real pre-marker observations, not
        // from the held-out marker/pilots being classified. Keep the filter
        // right support inside the calibration extent.
        const auto calibration=received>input_rate/4?received-input_rate/4:0;
        const auto earliest=std::max<std::uint64_t>(frontend_radius,calibration/stride*stride);
        const auto reference_count=(received>earliest+2*frontend_radius)?(received-earliest-2*frontend_radius)/stride:0;
        if(reference_count)noise_floor=estimate_floor(earliest,reference_count);
        if(reference_count) {
            double power=0.;
            for(std::size_t i=0;i<reference_count;++i)power+=std::pow(at(earliest+i*stride).signal,2);
            tracked_power=power/reference_count;
        }
        lock=candidate;
        timing_adopted=true;
        verified_cadence=cadence_verified;
        demapping_offset=0.;
        ordinal=static_cast<std::size_t>(next_ordinal);
        last_timing_marker=ordinal;
        group=0;
        interval_present=false;
        marker_scored=false;
        units.fill({});
        points.fill({});
        llrs.fill(0);
        state.symbols=first;
        state.carrier_error_hz=candidate.frequency;
        state.clock_error_ppm=clock*1e6;
    }

    void filter(double sample) {
        frontend_history[frontend_at]=frontend_history[frontend_at+frontend_size]=sample;
        if(++frontend_at==frontend_size)frontend_at=0;
        if(received<2*frontend_radius || (received-frontend_radius)%stride!=0)return;
        const auto* input=frontend_history.data()+frontend_at;
        const auto output=[&](const Bandpass& taps) {
            double value=taps[frontend_radius]*input[frontend_radius];
            for(unsigned n=0;n<frontend_radius;++n)
                value+=taps[n]*(input[n]+input[2*frontend_radius-n]);
            return value;
        };
        const auto tick=received-frontend_radius;
        history[(tick/stride)%history.size()]={output(signal_filter),{output(lower_reference),output(upper_reference)}};
    }

    std::size_t consume(std::span<const float> samples) {
        if(eof)throw std::logic_error("samples supplied after Fast receiver EOF");
        std::size_t used=0;
        for(const auto sample:samples) {
            if(stop.stop_requested()||state.physical_complete)break;
            if(!std::isfinite(sample))throw std::invalid_argument("nonfinite Fast PCM input");
            filter(sample);
            if(input_observer&&received%(6*ticks_per_symbol)==0)
                report_symbol(input_observer,2.*double(sample)*std::polar(1.,-tau*profile.carrier_hz*received/input_rate));
            ++received;
            ++used;
            if(received%256==0)service();
        }
        if(!stop.stop_requested()&&!state.physical_complete)service();
        return used;
    }
};

DenseAcousticReceiver::DenseAcousticReceiver(const Profile& p,ValidatedIntervalSink sink,SymbolObserver observer,
    SymbolObserver input_observer,std::stop_token stop,std::optional<ChannelBounds> bounds,
    DenseReceiverObservers observers,std::optional<ChannelAssumptions> assumptions)
    :impl_(std::make_unique<Impl>(p,std::move(sink),std::move(observer),std::move(input_observer),stop,bounds,
        std::move(observers),assumptions)) {}
DenseAcousticReceiver::~DenseAcousticReceiver()=default;
std::size_t DenseAcousticReceiver::consume(std::span<const float> samples) {return impl_->consume(samples);}
void DenseAcousticReceiver::finish() noexcept {impl_->eof=true;}
const ModemProgress& DenseAcousticReceiver::progress() const noexcept {return impl_->state;}
std::optional<std::uint64_t> DenseAcousticReceiver::completion_tick() const noexcept {return impl_->quiet.completion_tick();}
std::size_t DenseAcousticReceiver::workspace_bytes() const noexcept {return impl_->peak_workspace;}
bool DenseAcousticReceiver::adopt_timing(DenseTiming timing,std::uint64_t next_ordinal,bool cadence_verified) {
    return impl_->adopt_timing(timing,next_ordinal,cadence_verified);
}

} // namespace datapump::fast::detail
