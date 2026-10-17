#include "eme_probability.hpp"
#include "estimate_cancellation.hpp"
#include "pattern_drift.hpp"
#include "pattern_fft_batch.hpp"
#include "probability_random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <random>
namespace datapump::simulation::detail {
namespace {
using Complex=std::complex<double>;
constexpr std::size_t bin=750,max_radius=32,max_starts=2*max_radius+1;
constexpr double tau=2*std::numbers::pi;
struct Row {
    modem::PatternFrequencyRateHypothesis hypothesis;
    std::array<std::vector<Complex>,2> kernel;
    std::array<std::array<double,4>,2> norms{};
    std::array<std::size_t,5> edges{};
};
struct Source {
    std::vector<Complex> co,image;
    double mean_energy=0;
};
struct Geometry {
    std::uint64_t samples=0,epoch=0;
    unsigned radius=0,carriers=0,subdivisions=0;
    std::size_t length=0,starts=0,source_begin=0,source_step=0;
    double threshold=0,condition=1,frequency_step=0,frequency_center=0,source_frequency=0,source_clock=0;
    std::array<Source,2> source;
    std::vector<Row> rows;
};
void require(bool value,const char* message) {if(!value)throw Error(message);}
void validate_eme_geometry(const modem::Config& c) {
    require(c.pattern_symbols&&c.spreading_mode==modem::SpreadingMode::pattern&&!c.scramble&&!c.dsss&&
        !c.data_key&&!c.fake_hopping&&!c.scheduled_hopping&&c.dsss_factor==1&&
        c.sample_rate==6000&&c.carrier_hz==1500&&c.bandwidth_hz==1&&
        c.frequency_search_multiplier==1&&c.stream_phase_samples==0&&modem::pattern_pulse_enabled(c)&&
        modem::pattern_chip_samples(c)==12000,"unsupported EME probability waveform");
    const auto n=modem::symbol_sample_count(c);
    require(n==768000||n==1536000,"unsupported EME probability symbol");
    modem::OscillatorSearchConfig policy;policy.lf={.0001,.005};policy.rf=policy.lf;
    policy.rf_shift_hz=5.8e9;policy.margin=3;policy.reference=modem::OscillatorReference::independent_audio;
    require(c.oscillator_search&&*c.oscillator_search==policy,"unsupported EME probability oscillator bank");
}
Geometry make_geometry(const modem::Config& c,const EmeProbabilitySettings& settings,std::stop_token stop) {
    validate_eme_geometry(c);estimate_detail::check(stop);
    require(settings.timing_radius<=max_radius&&settings.carrier_radius<=16&&
        settings.source_subdivisions>=1&&settings.source_subdivisions<=125&&bin%settings.source_subdivisions==0,
        "invalid EME probability neighborhood");
    Geometry g;g.samples=modem::symbol_sample_count(c);g.epoch=c.stream_epoch;
    g.radius=settings.timing_radius;g.carriers=settings.carrier_radius;g.subdivisions=settings.source_subdivisions;
    g.starts=2*g.radius+1;
    const auto bank=modem::oscillator_pattern_search(c);
    require(!bank.limited&&bank.hypotheses.size()==(g.samples==768000?1783:3565)&&
        modem::pattern_projection_bin_samples(c,bank.frequency.half_width_hz)==bin,"EME bank changed");
    double minimum_rate=1;for(const auto& h:bank.hypotheses)minimum_rate=std::min(minimum_rate,1+h.clock_error_ppm*1e-6);
    g.length=static_cast<std::size_t>(std::ceil(static_cast<long double>(g.samples)/minimum_rate/bin));
    g.frequency_step=bank.frequency.step_hz;
    require(std::isfinite(settings.frequency_offset_hz)&&std::isfinite(settings.clock_error_ppm)&&
        std::abs(settings.clock_error_ppm)<=.0001,"unsupported EME source offset");
    g.source_frequency=settings.frequency_offset_hz;g.source_clock=settings.clock_error_ppm;
    const auto physical_frequency=g.source_frequency+c.carrier_hz*g.source_clock*1e-6;
    const auto nearest=std::min_element(bank.hypotheses.begin(),bank.hypotheses.end(),[&](const auto& a,const auto& b) {
        return std::abs(a.frequency_offset_hz-physical_frequency)<std::abs(b.frequency_offset_hz-physical_frequency);
    });
    g.frequency_center=nearest->frequency_offset_hz;
    const auto initial=c.sample_rate/bin;
    const auto hop=g.samples/bin/2;
    const auto trials=static_cast<long double>(initial+hop)*bank.hypotheses.size();
    g.threshold=static_cast<double>(-std::log(1e-10L)+2*std::log(trials+1)+std::log(2.L*bank.hypotheses.size()));
    const auto image=std::abs(modem::detail::pattern_projection_image_ratio(bin,c.carrier_hz,c.sample_rate));
    g.condition=(1+image)/(1-image);
    modem::PatternCode code(c,c.stream_epoch);code.prepare_symbol(0,stop);
    for(const auto& h:bank.hypotheses) {
        if(std::abs(h.frequency_offset_hz-g.frequency_center)>g.carriers*g.frequency_step+1e-12)continue;
        estimate_detail::check(stop);Row row;row.hypothesis=h;row.edges.back()=g.length;
        const auto rate=1+h.clock_error_ppm*1e-6;
        for(unsigned q=1;q<4;++q) {
            const auto boundary=modem::detail::drift_boundary(q,g.samples,4);
            const auto edge=std::ceil((static_cast<long double>(boundary)/rate-(bin-1)/2.L)/bin);
            row.edges[q]=static_cast<std::size_t>(std::clamp(edge,static_cast<long double>(row.edges[q-1]),static_cast<long double>(g.length)));
        }
        for(auto& k:row.kernel)k.resize(g.length);
        for(unsigned q=0;q<4;++q)for(auto i=row.edges[q];i<row.edges[q+1];++i) {
            const auto position=static_cast<long double>(i)*bin+(bin-1)/2.L;
            const auto source_position=position*rate;
            const auto values=source_position>=g.samples?std::array<Complex,2>{}:
                code.shaped_values(0,static_cast<double>(source_position));
            const auto rotation=std::polar(1.,tau*h.frequency_offset_hz*static_cast<double>(position)/c.sample_rate);
            for(unsigned bit=0;bit<2;++bit) {
                row.kernel[bit][i]=std::conj(values[bit]*rotation);
                row.norms[bit][q]+=std::norm(values[bit]);
            }
        }
        g.rows.push_back(std::move(row));
    }
    require(!g.rows.empty()&&g.rows.size()<=3*(2*g.carriers+1),"EME local clock bank exceeded bound");
    // Fixed outer extent keeps random coordinates identical when the local
    // search radius changes during convergence checks.
    const auto padding=modem::pattern_pulse_padding_samples(c);
    g.source_begin=padding-(max_radius+1)*bin;g.source_step=bin/g.subdivisions;
    const auto source_end=padding+(g.length+max_radius+2)*bin;
    const auto points=(source_end-g.source_begin)/g.source_step+1;
    const auto amplitude=std::sqrt(2*modem::nominal_signal_power);
    constexpr std::array<Complex,4> inverse_carrier{Complex{1,0},{0,-1},{-1,0},{0,1}};
    for(unsigned bit=0;bit<2;++bit) {
        estimate_detail::check(stop);auto& source=g.source[bit];
        source.co.assign(points,{});source.image.assign(points,{});
        modem::PatternTransmitter tx(Bytes{static_cast<std::uint8_t>(bit)},c,c.stream_epoch,0,false);
        std::array<Complex,4096> block{};std::uint64_t emitted=0;std::size_t next=1;
        Complex co{},conjugate{};long double physical=0;
        while(emitted<source_end) {
            estimate_detail::check(stop);const auto count=tx.read_analytic(block,stop);
            require(count>0,"EME source ended before complete projection");
            for(std::size_t j=0;j<count&&emitted<source_end;++j,++emitted) {
                if(emitted>=padding&&emitted-padding<g.samples)physical+=std::norm(block[j]/amplitude);
                if(emitted<g.source_begin)continue;
                // The bounded EME clock skew moves the envelope by <0.00018
                // sample including pulse support. Retain carrier rotation exactly; qualify
                // the negligible envelope approximation against channel PCM.
                const auto shifted=block[j]*std::polar(1.,tau*physical_frequency*double(emitted)/c.sample_rate);
                const auto rotation=inverse_carrier[emitted%4];
                co+=shifted*rotation/amplitude;conjugate+=std::conj(shifted)*rotation/amplitude;
                if((emitted+1-g.source_begin)%g.source_step==0) {
                    require(next<points,"EME prefix exceeded source bound");
                    source.co[next]=co;source.image[next]=conjugate;++next;
                }
            }
        }
        require(next==points&&physical>0,"EME source projection is incomplete");
        source.mean_energy=static_cast<double>(physical);
    }
    return g;
}
const Geometry& geometry(const modem::Config& c,const EmeProbabilitySettings& settings,std::stop_token stop) {
    validate_eme_geometry(c);
    thread_local std::vector<Geometry> cache;
    const auto n=modem::symbol_sample_count(c);
    for(const auto& g:cache)if(g.samples==n&&g.epoch==c.stream_epoch&&g.radius==settings.timing_radius&&
        g.carriers==settings.carrier_radius&&g.subdivisions==settings.source_subdivisions&&
        g.source_frequency==settings.frequency_offset_hz&&g.source_clock==settings.clock_error_ppm)return g;
    auto g=make_geometry(c,settings,stop);estimate_detail::check(stop);
    if(cache.size()==2)cache.erase(cache.begin());cache.push_back(std::move(g));return cache.back();
}
Complex prefix_at(const std::vector<Complex>& values,double coordinate) {
    const auto i=static_cast<std::size_t>(coordinate);const auto fraction=coordinate-static_cast<double>(i);
    require(i+1<values.size(),"fractional EME source outside projected support");
    return values[i]+fraction*(values[i+1]-values[i]);
}
void score(const Geometry& g,std::span<const double> re,std::span<const double> im,
        std::span<EmeCellScores> cells,std::stop_token stop) {
    require(re.size()>=g.length+g.starts-1&&im.size()==re.size()&&cells.size()==g.rows.size()*g.starts,
        "invalid EME joint observation span");
    std::array<double,max_starts> energy{};
    // Each shifted start uses its own energy from the common observation.
    std::vector<double> prefix(re.size()+1);
    for(std::size_t i=0;i<re.size();++i)prefix[i+1]=prefix[i]+re[i]*re[i]+im[i]*im[i];
    for(std::size_t j=0;j<g.starts;++j)energy[j]=prefix[j+g.length]-prefix[j];
    for(std::size_t f=0;f<g.rows.size();++f) {
        estimate_detail::check(stop);const auto& row=g.rows[f];
        for(unsigned bit=0;bit<2;++bit) {
            std::array<double,max_starts> full_re{},full_im{},explained{},strongest{};double full_norm=0;
            for(unsigned q=0;q<4;++q) {
                std::array<double,max_starts> dot_re{},dot_im{};
                for(auto i=row.edges[q];i<row.edges[q+1];++i) {
                    const auto kr=row.kernel[bit][i].real(),ki=row.kernel[bit][i].imag();
                    for(std::size_t j=0;j<g.starts;++j) {
                        dot_re[j]+=re[i+j]*kr-im[i+j]*ki;
                        dot_im[j]+=re[i+j]*ki+im[i+j]*kr;
                    }
                }
                const auto norm=row.norms[bit][q];full_norm+=norm;
                require(norm>0,"EME template quarter has no energy");
                for(std::size_t j=0;j<g.starts;++j) {
                    full_re[j]+=dot_re[j];full_im[j]+=dot_im[j];
                    const auto e=(dot_re[j]*dot_re[j]+dot_im[j]*dot_im[j])/(norm*g.condition);
                    explained[j]+=e;strongest[j]=std::max(strongest[j],e);
                }
            }
            for(std::size_t j=0;j<g.starts;++j) {
                const auto full=modem::detail::pattern_evidence({full_re[j],full_im[j]},energy[j],full_norm,
                    static_cast<double>(g.length),g.condition,false,false);
                const auto drift=modem::detail::drift_evidence(explained[j]-strongest[j],energy[j],
                    static_cast<double>(g.length),4,false);
                auto& out=cells[f*g.starts+j];out.coherent[bit]=full;
                out.combined[bit]=modem::detail::combine_drift_evidence(full,drift,4);
            }
        }
    }
}
}
EmeScoreGrid eme_score_grid(const modem::Config& config,std::span<const Complex> input,
        unsigned radius,unsigned carriers,std::stop_token stop) {
    const auto& g=geometry(config,{256,radius,carriers,25},stop);
    std::vector<double> re(input.size()),im(input.size());
    for(std::size_t i=0;i<input.size();++i){re[i]=input[i].real();im[i]=input[i].imag();}
    EmeScoreGrid result;result.length=g.length;result.starts=g.starts;
    for(const auto& row:g.rows)result.hypotheses.push_back(row.hypothesis);
    result.cells.resize(g.rows.size()*g.starts);score(g,re,im,result.cells,stop);return result;
}
EmeSourceProjection eme_source_projection(const modem::Config& config,unsigned bit,double arrival,
        double phase,unsigned radius,unsigned subdivisions,double frequency,double clock,std::stop_token stop) {
    require(bit<=1&&arrival>=0&&arrival<1&&std::isfinite(phase),"invalid EME source projection");
    const auto& g=geometry(config,{256,radius,0,subdivisions,frequency,clock},stop);const auto& source=g.source[bit];
    EmeSourceProjection result;result.mean_energy=source.mean_energy;
    result.observations.resize(g.length+2*radius);
    const auto rotation=std::polar(1.,phase);
    const auto image_rotation=std::polar(1.,-phase-2*(tau*config.carrier_hz/config.sample_rate)*arrival*bin);
    const auto padding=modem::pattern_pulse_padding_samples(config);
    for(std::size_t i=0;i<result.observations.size();++i) {
        const auto first=static_cast<double>(padding)+(static_cast<double>(i)-radius-arrival)*bin;
        const auto a=(first-g.source_begin)/g.source_step,b=(first+bin-g.source_begin)/g.source_step;
        result.observations[i]=((prefix_at(source.co,b)-prefix_at(source.co,a))*rotation+
            (prefix_at(source.image,b)-prefix_at(source.image,a))*image_rotation)/double(bin);
    }
    return result;
}
EmeProbabilityResult eme_receiver_probability(const transfer::Options& options,ReceiverWorkMode mode,
        double symbol_db,double diffusion,EmeProbabilitySettings settings,std::stop_token stop) {
    estimate_detail::check(stop);require(!options.key,"EME probability cannot borrow keyed waveform identity");
    require(settings.trials>=256&&settings.trials<=4096&&std::isfinite(symbol_db)&&
        std::isfinite(diffusion)&&diffusion>=0,"invalid EME probability trials or strength");
    auto config=options.modem;if(options.timestamp)config.stream_epoch=options.timestamp;
    const auto& g=geometry(config,settings,stop);
    struct Key {std::uint64_t samples,epoch;ReceiverWorkMode mode;double db,diffusion;EmeProbabilitySettings settings;bool operator==(const Key&)const=default;};
    struct Entry {Key key;EmeProbabilityResult value;};
    thread_local std::vector<Entry> cache;const Key key{g.samples,g.epoch,mode,symbol_db,diffusion,settings};
    for(const auto& entry:cache)if(entry.key==key)return entry.value;
    EmeProbabilityResult result;result.full_bank_threshold=g.threshold;result.local_jobs=g.rows.size();result.local_starts=g.starts;
    auto& p=result.probability;p.trials=settings.trials;p.frequency_search_approximation=true;p.frequency_candidates=g.rows.size();
    const auto field_length=g.length+2*max_radius;
    std::vector<double> re(field_length),im(field_length);
    std::vector<EmeCellScores> cells(g.rows.size()*g.starts);
    std::mt19937_64 generator(0x7e6a9b1403c82d51ULL);ProbabilityNormal normal;
    const auto sigma=diffusion*std::numbers::pi/180*std::sqrt(double(bin)/config.sample_rate);
    const auto nominal_energy=std::pow(10.,symbol_db/10);
    require(std::isfinite(nominal_energy)&&nominal_energy>=0,"EME source energy exceeds numeric range");
    result.trial_categories.reserve(settings.trials);result.winning_start.reserve(settings.trials);
    result.winning_carrier.reserve(settings.trials);
    const auto padding=modem::pattern_pulse_padding_samples(config);
    for(std::size_t trial=0;trial<settings.trials;++trial) {
        estimate_detail::check(stop);
        const auto bit=static_cast<unsigned>(generator()&1U);
        const auto arrival=probability_uniform(generator);
        auto phase=tau*probability_uniform(generator);
        const auto& source=g.source[bit];
        const auto denominator=mode==ReceiverWorkMode::sampled_simulation?static_cast<double>(g.samples):source.mean_energy;
        const auto amplitude=std::sqrt(nominal_energy*bin/denominator);
        const auto image_rotation=std::polar(1.,-2*(tau*config.carrier_hz/config.sample_rate)*arrival*bin);
        for(std::size_t i=0;i<field_length;++i) {
            const auto first=static_cast<double>(padding)+(static_cast<double>(i)-max_radius-arrival)*bin;
            const auto a=(first-g.source_begin)/g.source_step,b=(first+bin-g.source_begin)/g.source_step;
            const auto co=(prefix_at(source.co,b)-prefix_at(source.co,a))/double(bin);
            const auto image=(prefix_at(source.image,b)-prefix_at(source.image,a))/double(bin);
            const auto rotation=std::polar(1.,phase);
            const auto value=amplitude*(co*rotation+image*std::conj(rotation)*image_rotation);
            re[i]=value.real()+normal(generator)/std::sqrt(2.);
            im[i]=value.imag()+normal(generator)/std::sqrt(2.);
            phase+=sigma*normal(generator);
        }
        const auto first=max_radius-g.radius;
        score(g,std::span(re).subspan(first,g.length+g.starts-1),
            std::span(im).subspan(first,g.length+g.starts-1),cells,stop);
        double best=-1,other=0,coherent_best=-1,coherent_other=0;unsigned chosen=0,coherent_bit=0;
        std::size_t chosen_cell=0;
        for(std::size_t i=0;i<cells.size();++i) {
            const auto& s=cells[i];
            const auto winner=s.combined[1]>s.combined[0]?1U:0U;
            if(s.combined[winner]>best){best=s.combined[winner];other=s.combined[1-winner];chosen=winner;chosen_cell=i;}
            const auto coherent=s.coherent[1]>s.coherent[0]?1U:0U;
            if(s.coherent[coherent]>coherent_best){coherent_best=s.coherent[coherent];coherent_other=s.coherent[1-coherent];coherent_bit=coherent;}
        }
        std::uint8_t category=0;
        if(best>=g.threshold&&best-other>=1) {
            category=chosen==bit?1U:2U;
            if(chosen==bit)++p.acquired_correct;else ++p.acquired_wrong;
            const auto start=chosen_cell%g.starts;
            if(start==0||start+1==g.starts)++result.timing_boundary_admissions;
            const auto& h=g.rows[chosen_cell/g.starts].hypothesis;
            if(std::abs(h.frequency_offset_hz-g.frequency_center)>=(g.carriers-.5)*g.frequency_step)++result.carrier_boundary_admissions;
        }
        result.trial_categories.push_back(category);
        result.winning_start.push_back(static_cast<std::int16_t>(static_cast<int>(chosen_cell%g.starts)-static_cast<int>(g.radius)));
        result.winning_carrier.push_back(static_cast<std::int16_t>(std::llround(g.rows[chosen_cell/g.starts].hypothesis.frequency_offset_hz/g.frequency_step)));
        if(coherent_best>=g.threshold&&coherent_best-coherent_other>=1) {
            if(coherent_bit==bit)++p.coherent_acquired_correct;else ++p.coherent_acquired_wrong;
        }
    }
    p.acquired_correct/=settings.trials;p.acquired_wrong/=settings.trials;
    p.coherent_acquired_correct/=settings.trials;p.coherent_acquired_wrong/=settings.trials;
    estimate_detail::check(stop);if(cache.size()==8)cache.erase(cache.begin());cache.push_back({key,result});return result;
}
}
