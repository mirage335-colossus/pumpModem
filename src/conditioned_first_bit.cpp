#include "conditioned_first_bit.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "estimate_cancellation.hpp"
#include "probability_random.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>

namespace datapump::simulation::detail::conditioned_private {
namespace {
using Complex=std::complex<double>;
constexpr double tau=2*std::numbers::pi;
void require(bool condition,const char* message) {if(!condition)throw Error(message);}
std::size_t transform_size(std::size_t count) {
    std::size_t n=1;while(n<count)n*=2;return n;
}
struct Shape {
    std::size_t bins=0,bin=0,outputs=0,raw=0,delay=0,selected=0,rates=0;
    std::uint64_t filter_work=0,template_work=0,uncached_template_work=0,score_work=0;
    bool prepared_cache=false;
};
// Match the actual native Impl::scoring_geometry() metadata, not the separate
// shaped-source estimator's richer FHSS tags. The exported geometry remains
// untouched; this is only a Config/descriptor consistency check.
modem::detail::FftPatternParameters native_pattern_identity(const modem::Config& c) {
    modem::detail::FftPatternParameters p;
    p.epoch=c.stream_epoch;p.chip_samples=modem::pattern_chip_samples(c);
    p.chips_per_symbol=modem::pattern_chips_per_symbol(c);p.symbol_samples=modem::symbol_sample_count(c);
    p.sample_rate=c.sample_rate;p.spreading_mode=static_cast<std::uint32_t>(c.spreading_mode);
    p.shaped=modem::pattern_pulse_enabled(c);p.scramble=c.scramble;p.dsss=c.dsss;
    p.spreading_seed=c.spreading_seed;p.dsss_seed=c.dsss_seed;p.dsss_factor=c.dsss_factor;
    p.outer_dsss_version=static_cast<std::uint32_t>(c.outer_dsss_version);p.legacy_public=modem::legacy_public_pattern(c);
    if(c.fake_hopping && c.fake_hopping->matched_gaps) {
        const auto& h=*c.fake_hopping;
        p.fhss_matched_gaps=1;p.fhss_dwell=h.dwell_seconds;p.fhss_clock=h.clock_error_seconds;
        p.fhss_peer_clock=h.peer_clock_error_seconds;p.fhss_audio=h.audio_error_seconds;p.fhss_region=h.region_seconds;
    }
    return p;
}
Shape shape(const FirstBitRequest& r,bool monte_carlo) {
    const auto& c=r.config;const auto& g=r.geometry;
    require(c.sample_rate==14400 && modem::symbol_sample_count(c)==5120 &&
        c.pattern_symbols && c.spreading_mode==modem::SpreadingMode::pattern &&
        c.dsss_factor==10 && c.outer_dsss_version==modem::OuterDsssVersion::interleaved_v2,
        "Reference unsupported outside the native B 14400 Hz / 5120-sample Interleave10 geometry");
    require(!g.real_rank && !g.sample_fit && g.drift_sections==1 && !g.differential_window_samples &&
        g.bin_samples>=2 && g.pattern==native_pattern_identity(c) && g.carrier_hz==c.carrier_hz,
        "Reference unsupported for compact, differential, rank-deficient, phase-changing or mismatched template geometry");
    require(g.search_arithmetic==static_cast<std::uint32_t>(modem::SearchArithmetic::fp64),
        "First finite reference currently requires explicit production FP64 score arithmetic");
    require(r.conditioning.independent_prior &&
        r.conditioning.prior_evidence_end_sample<=r.conditioning.first_raw_sample &&
        std::isfinite(r.conditioning.frequency_correction_hz),
        "Frozen helper evidence must end before the first scored raw sample, including the entire FIR halo");
    require(r.starts && r.starts<=32 && g.bins_per_symbol && g.bins_per_symbol<=maximum_projected_samples &&
        r.starts<=maximum_projected_samples-g.bins_per_symbol+1,
        "Reference finite observation/start bound exceeded");
    require(!r.original_jobs.empty() && r.original_jobs.size()<=maximum_original_jobs &&
        r.selected_original_cells.size()==r.original_jobs.size(),"Reference needs the original bounded catalogue and same-size RF mask");
    Shape s;s.bins=static_cast<std::size_t>(g.bins_per_symbol);s.bin=static_cast<std::size_t>(g.bin_samples);
    require(s.bin<=maximum_raw_samples/(s.bins+r.starts-1),"Reference raw observation bound exceeded");
    require(r.starts-1<=UINT64_MAX/s.bin &&
        static_cast<std::uint64_t>(r.starts-1)*s.bin<std::max<std::uint64_t>(1,modem::symbol_sample_count(c)/2),
        "Reference selected starts do not form one native temporal peak cluster");
    s.outputs=s.bin*(s.bins+r.starts-1);
    helper::PayloadFilter filter;
    if(r.conditioning.filter_hz)filter=helper::PayloadFilter(c.sample_rate,*r.conditioning.filter_hz);
    s.delay=filter.delay();require(s.outputs<=maximum_raw_samples-2*s.delay,"Reference FIR halo exceeds raw bound");
    s.raw=s.outputs+2*s.delay;
    require(r.conditioning.first_raw_sample<=UINT64_MAX-s.raw,"Reference sample coordinate overflow");
    require(std::isfinite(g.evidence_count) && g.evidence_count>1 && std::isfinite(g.noise_condition) && g.noise_condition>=1,
        "Reference detector dimensions/conditioning are invalid");
    std::vector<double> rates;
    std::vector<std::pair<std::pair<std::uint64_t,std::uint64_t>,double>> phase_rates;
    double selected_low=std::numeric_limits<double>::infinity(),selected_high=-selected_low;
    for(std::size_t i=0;i<r.original_jobs.size();++i) {
        const auto& j=r.original_jobs[i];
        require(std::isfinite(j.frequency_hz) && std::isfinite(j.clock_ratio) && j.clock_ratio>0 &&
            j.symbol<modem::deferred_initial_symbols(c) && j.phase<c.sample_rate &&
            j.prepared_template==std::numeric_limits<std::uint64_t>::max() &&
            j.range_count==std::numeric_limits<std::size_t>::max(),
            "Reference catalogue must retain complete original first-symbol/phase jobs");
        require(r.selected_original_cells[i]<=1,"Reference RF mask is not Boolean");
        if(std::find(rates.begin(),rates.end(),j.clock_ratio)==rates.end())rates.push_back(j.clock_ratio);
        const auto group=std::pair{std::pair{j.symbol,j.phase},j.clock_ratio};
        if(std::find(phase_rates.begin(),phase_rates.end(),group)==phase_rates.end()) {
            require(phase_rates.size()<maximum_selected_jobs,"Original phase/LF groups exceed the bounded selectable grid");
            phase_rates.push_back(group);
        }
        s.selected+=r.selected_original_cells[i];
        if(r.selected_original_cells[i]) {selected_low=std::min(selected_low,j.frequency_hz);selected_high=std::max(selected_high,j.frequency_hz);}
    }
    require(s.selected && s.selected<=maximum_selected_jobs,"Reference selected-cell bound exceeded");
    require(r.competing_frequencies ||
        selected_high-selected_low<=static_cast<double>(c.sample_rate)/modem::symbol_sample_count(c),
        "Reference selected RF cells do not share the actual native peak-competition policy");
    for(const auto& [identity,rate]:phase_rates) {
        bool retained=false;
        for(std::size_t i=0;i<r.original_jobs.size();++i)
            retained|=r.selected_original_cells[i] && r.original_jobs[i].symbol==identity.first &&
                r.original_jobs[i].phase==identity.second && r.original_jobs[i].clock_ratio==rate;
        require(retained,"Reference RF mask removed an original initial-symbol/phase/LF alternative");
    }
    s.rates=rates.size();
    auto minimum_rate=1.;bool scaled=false;
    for(const auto& j:r.original_jobs) {minimum_rate=std::min(minimum_rate,j.clock_ratio);scaled|=j.clock_ratio!=1;}
    const auto expected_bins=std::ceil(static_cast<long double>(modem::symbol_sample_count(c))/minimum_rate/s.bin);
    const auto image=std::abs(modem::detail::pattern_projection_image_ratio(s.bin,c.carrier_hz,c.sample_rate));
    require(expected_bins==s.bins && g.extended_clock_window==static_cast<std::uint32_t>(scaled) && g.evidence_count==static_cast<double>(s.bins) &&
        image<1 && g.noise_condition==(1+image)/(1-image),
        "Reference must retain original complete clock-scaled observation dimensions and real-PCM conditioning");
    require(r.original_threshold_trials && std::isfinite(r.original_acquisition_threshold) &&
        r.original_acquisition_threshold>0,"Reference requires the unchanged original admission threshold/trial charge");
    if(monte_carlo) {
        require(r.trials>=256 && r.trials<=4096 && r.trials%2==0,"Reference needs 256-4096 even total paired trials");
        require(std::isfinite(r.signal_scale) && r.signal_scale>=0 &&
            std::isfinite(r.raw_noise_variance) && r.raw_noise_variance>0,"Reference signal/noise scales are invalid");
        for(const auto& source:r.source_pcm) {
            require(source.size()==s.raw,"Production source PCM does not cover the complete scored FIR halo");
            for(const auto x:source)require(std::isfinite(x),"Production source PCM is nonfinite");
        }
        require(r.helper_pcm.empty() || r.helper_pcm.size()==s.raw,"Helper PCM does not cover the same complete FIR halo");
        for(const auto x:r.helper_pcm)require(std::isfinite(x),"Helper PCM is nonfinite");
        // Symmetric production FIR evaluates delay+1 products per observed
        // output, including its center. Disabled filtering has no FIR cost.
        s.filter_work=filter.enabled()?static_cast<std::uint64_t>(s.outputs)*(s.delay+1)*r.trials:0;
        require(s.filter_work<=maximum_filter_multiply_adds,"Direct production FIR work exceeds bounded reference budget");
        s.uncached_template_work=2*static_cast<std::uint64_t>(s.bins)*s.selected*r.trials;
        s.score_work=s.uncached_template_work*r.starts;
        s.prepared_cache=s.uncached_template_work>maximum_template_samples;
        s.template_work=s.prepared_cache?2*static_cast<std::uint64_t>(s.bins)*s.selected:s.uncached_template_work;
        require(s.template_work<=maximum_template_samples,"Production template evaluation exceeds bounded reference budget");
        require(!s.prepared_cache || s.score_work<=maximum_cached_score_products,
            "Cached production direct-score work exceeds bounded reference budget");
    }
    return s;
}
std::size_t input_bytes(const FirstBitRequest& r) {
    std::size_t bytes=sizeof(r);
    const auto charge=[&](std::size_t count,std::size_t item) {
        if(count>maximum_owned_bytes/item || bytes>maximum_owned_bytes-count*item)bytes=maximum_owned_bytes+1;
        else bytes+=count*item;
    };
    charge(r.original_jobs.capacity(),sizeof(modem::detail::FftSearchJob));charge(r.selected_original_cells.capacity(),1);
    for(const auto& v:r.source_pcm)charge(v.capacity(),sizeof(float));
    charge(r.helper_pcm.capacity(),sizeof(float));return bytes;
}
}

static FiniteObservation condition_with_shape(const FirstBitRequest& r,const Shape& s,std::span<const float> raw,
                                              std::size_t chunk,std::stop_token stop) {
    estimate_detail::check(stop);
    require(raw.size()==s.raw && chunk,"Conditioned PCM must contain exactly the finite output plus observed FIR halo");
    helper::PayloadFilter filter;
    if(r.conditioning.filter_hz)filter=helper::PayloadFilter(r.config.sample_rate,*r.conditioning.filter_hz);
    std::vector<float> filtered(s.raw);std::size_t count=0;
    if(s.prepared_cache)require(filtered.capacity()<=s.raw &&
        filter.allocated_bytes()<=(2*s.delay+1)*(sizeof(double)+sizeof(float)),
        "Actual production FIR capacities exceed raw-cache allowance");
    for(std::size_t at=0;at<raw.size();) {
        estimate_detail::check(stop);const auto n=std::min(chunk,raw.size()-at);
        count+=filter.push(raw.subspan(at,n),std::span(filtered).subspan(count,n));at+=n;
    }
    require(count==s.outputs,"Production FIR output did not match its finite observed-window geometry");
    FiniteObservation result;result.first_raw_sample=r.conditioning.first_raw_sample;
    result.observed_raw_end_sample=result.first_raw_sample+s.raw;
    result.first_output_sample=result.first_raw_sample+s.delay;
    result.values.resize(s.bins+r.starts-1);result.energy_prefix.resize(result.values.size()+1);
    if(s.prepared_cache)require(result.values.capacity()<=result.values.size() &&
        result.energy_prefix.capacity()<=result.energy_prefix.size(),
        "Actual conditioned observation capacities exceed raw-cache allowance");
    auto oscillator=Complex{1,0};
    const auto step=std::polar(1.,-tau*r.config.carrier_hz/r.config.sample_rate);
    for(std::size_t i=0;i<result.values.size();++i) {
        estimate_detail::check(stop);Complex sum{};
        for(std::size_t j=0;j<s.bin;++j) {
            const auto sample=i*s.bin+j;sum+=static_cast<double>(filtered[sample])*oscillator;oscillator*=step;
            if((sample&65535U)==65535U)oscillator/=std::abs(oscillator);
        }
        auto value=sum/static_cast<double>(s.bin);
        const auto center=static_cast<double>(i*s.bin)+(static_cast<double>(s.bin)-1)/2;
        value*=std::polar(1.,-tau*r.conditioning.frequency_correction_hz*center/r.config.sample_rate);
        require(std::isfinite(value.real())&&std::isfinite(value.imag()),"Conditioned observation is nonfinite");
        result.values[i]=value;result.energy_prefix[i+1]=result.energy_prefix[i]+std::norm(value);
    }
    return result;
}

static std::vector<modem::detail::FftSearchScore> score_with_shape(const FirstBitRequest& r,const Shape& s,
        const FiniteObservation& observation,std::stop_token stop) {
    estimate_detail::check(stop);
    require(observation.values.size()==s.bins+r.starts-1 && observation.energy_prefix.size()==observation.values.size()+1,
        "Conditioned score observations/energy are incomplete");
    require(observation.first_raw_sample==r.conditioning.first_raw_sample &&
        observation.observed_raw_end_sample==r.conditioning.first_raw_sample+s.raw &&
        observation.first_output_sample==r.conditioning.first_raw_sample+s.delay,
        "Conditioned score dates do not match the actual observed raw support");
    // Recompute from the supplied vector: callers cannot substitute an
    // independent denominator or different noise realization.
    std::vector<double> energy(observation.values.size()+1);
    for(std::size_t i=0;i<observation.values.size();++i) {
        require(std::isfinite(observation.values[i].real())&&std::isfinite(observation.values[i].imag()),"Conditioned score input is nonfinite");
        energy[i+1]=energy[i]+std::norm(observation.values[i]);
        require(std::isfinite(energy[i+1]),"Conditioned score energy exceeded numeric range");
    }
    std::vector<modem::detail::FftSearchJob> jobs;jobs.reserve(s.selected);
    for(std::size_t i=0;i<r.original_jobs.size();++i)if(r.selected_original_cells[i]) {
        auto j=r.original_jobs[i];j.frequency_hz-=r.conditioning.frequency_correction_hz;jobs.push_back(j);
    }
    const auto transform=transform_size(observation.values.size());
    std::vector<Complex> spectrum(transform);
    modem::detail::FftSearchBatch batch;batch.geometry=r.geometry;batch.spectrum=spectrum;
    batch.observations=observation.values;batch.energy_prefix=energy;batch.starts=r.starts;batch.score_stride=r.starts;
    std::vector<modem::detail::FftSearchWorkspace> workspace;
    workspace.emplace_back(r.config,transform,true);
    std::vector<modem::detail::FftSearchScore> scores(jobs.size()*r.starts);
    // With <=32 starts and actual observations, generated native B uses the
    // established direct production contraction (no invented detector).
    require(modem::detail::pattern_fft_direct_eligible(batch.geometry,true,r.starts,true),"Reference direct score geometry unavailable");
    modem::detail::execute_fft_search_cpu(batch,jobs,scores,workspace,stop);
    require(workspace.front().working_bytes()<=1024*1024+transform*sizeof(Complex),
        "Actual production template/code cache exceeds the explicit finite-reference allowance");
    return scores;
}


namespace {
struct Peak {
    double best=-1,other=0;unsigned chosen=0;
    void collect(std::span<const modem::detail::FftSearchScore> scores) {
        for(const auto& score:scores) {
            const auto winner=score.one>score.zero?1U:0U;
            const auto value=winner?score.one:score.zero;
            if(value>best) {best=value;other=winner?score.zero:score.one;chosen=winner;}
        }
    }
};
struct RawPlan {
    std::size_t field=0,transform=0,tile=0,observation_bytes=0,owned_bytes=0,fixed_scoring_bytes=0,scorer_bytes=0;
};
RawPlan raw_plan(const FirstBitRequest& r,const Shape& s,std::size_t draws,std::size_t external_output_bytes=0) {
    require(draws && draws<=4096,"Raw cache draw count exceeds finite reference bound");
    RawPlan p;p.field=s.bins+r.starts-1;p.transform=transform_size(p.field);
    // At most 4096*8192 complex values: calculate before allocating, and
    // refuse rather than grow the unchanged 8 MiB storage budget.
    p.observation_bytes=draws*p.field*sizeof(Complex);
    const auto input=input_bytes(r);
    const auto controls=4096+draws*sizeof(Peak)+external_output_bytes;
    const auto generation=input+p.observation_bytes+controls+2*s.raw*sizeof(float)+s.raw*sizeof(double)+
        p.field*sizeof(Complex)+(p.field+1)*sizeof(double)+(2*s.delay+1)*(sizeof(double)+sizeof(float));
    p.fixed_scoring_bytes=input+p.observation_bytes+controls+s.selected*sizeof(modem::detail::FftSearchJob)+
        1024*1024+2*p.transform*sizeof(Complex)+(p.field+1)*sizeof(double);
    const auto per_job=2*s.bins*sizeof(Complex)+sizeof(modem::detail::FftPreparedRawTemplate)+
        sizeof(modem::detail::FftSearchJob)+r.starts*sizeof(modem::detail::FftSearchScore);
    require(p.fixed_scoring_bytes<maximum_owned_bytes,"Finite raw-cache observation/code storage exceeds bounded reference budget");
    p.tile=std::min({std::size_t{16},s.selected,(maximum_owned_bytes-p.fixed_scoring_bytes)/per_job});
    require(p.tile,"Finite raw-cache template tile exceeds bounded reference budget");
    p.scorer_bytes=4096+s.selected*sizeof(modem::detail::FftSearchJob)+1024*1024+
        2*p.transform*sizeof(Complex)+(p.field+1)*sizeof(double)+p.tile*per_job;
    p.owned_bytes=std::max(generation,p.fixed_scoring_bytes+p.tile*per_job);
    require(p.owned_bytes<=maximum_owned_bytes,"Finite raw-cache owned-buffer budget exceeded");
    return p;
}
class RawScorer {
    const FirstBitRequest& request_;
    Shape shape_;RawPlan plan_;
    modem::PatternCode code_;
    std::vector<modem::detail::FftSearchJob> original_,jobs_;
    std::vector<Complex> rows_,spectrum_;
    std::vector<double> energy_;
    std::vector<modem::detail::FftPreparedRawTemplate> prepared_;
    std::vector<modem::detail::FftSearchWorkspace> workspace_;
    std::vector<modem::detail::FftSearchScore> scores_;
public:
    RawScorer(const FirstBitRequest& r,const Shape& s,const RawPlan& p):request_(r),shape_(s),plan_(p),
        code_(r.config,r.geometry.pattern.epoch),rows_(2*p.tile*s.bins),spectrum_(p.transform),energy_(p.field+1),
        prepared_(p.tile),scores_(p.tile*r.starts) {
        original_.reserve(s.selected);jobs_.reserve(p.tile);
        for(std::size_t i=0;i<r.original_jobs.size();++i)if(r.selected_original_cells[i]) {
            auto job=r.original_jobs[i];job.frequency_hz-=r.conditioning.frequency_correction_hz;original_.push_back(job);
        }
        workspace_.emplace_back(r.config,p.transform,false);
    }
    void prepare(std::size_t first,std::stop_token stop) {
        jobs_.clear();const auto count=std::min(plan_.tile,original_.size()-first);
        for(std::size_t i=0;i<count;++i) {
            estimate_detail::check(stop);auto job=original_[first+i];
            const auto offset=2*i*shape_.bins;
            prepared_[i]=modem::detail::prepare_fft_raw_template(request_.geometry,job,code_,
                {std::span(rows_).subspan(offset,shape_.bins),std::span(rows_).subspan(offset+shape_.bins,shape_.bins)},stop);
            job.prepared_template=i;jobs_.push_back(job);
        }
        require(code_.working_bytes()<=1024*1024,"Actual production raw-template/code cache exceeds finite-reference allowance");
        // Account actual vector capacities too; reserve requests are not a
        // claim about a particular standard library's allocation strategy.
        const auto actual=original_.capacity()*sizeof(original_[0])+jobs_.capacity()*sizeof(jobs_[0])+
            rows_.capacity()*sizeof(Complex)+spectrum_.capacity()*sizeof(Complex)+energy_.capacity()*sizeof(double)+
            prepared_.capacity()*sizeof(prepared_[0])+scores_.capacity()*sizeof(scores_[0])+
            workspace_.capacity()*sizeof(workspace_[0])+workspace_.front().working_bytes()+code_.working_bytes();
        require(actual<=plan_.scorer_bytes,"Actual production raw-template capacities exceed finite-reference allowance");
    }
    std::span<const modem::detail::FftSearchScore> score(std::span<const Complex> values,std::stop_token stop) {
        estimate_detail::check(stop);require(values.size()==plan_.field,"Raw cached observation shape changed");
        energy_[0]=0;
        for(std::size_t i=0;i<values.size();++i) {
            require(std::isfinite(values[i].real())&&std::isfinite(values[i].imag()),"Conditioned score input is nonfinite");
            energy_[i+1]=energy_[i]+std::norm(values[i]);
            require(std::isfinite(energy_[i+1]),"Conditioned score energy exceeded numeric range");
        }
        modem::detail::FftSearchBatch batch;batch.geometry=request_.geometry;batch.spectrum=spectrum_;
        batch.observations=values;batch.energy_prefix=energy_;batch.prepared_raw=std::span(prepared_).first(jobs_.size());
        batch.starts=request_.starts;batch.score_stride=request_.starts;
        modem::detail::execute_fft_search_cpu(batch,jobs_,scores_,workspace_,stop);
        return std::span(scores_).first(jobs_.size()*request_.starts);
    }
};
void validate_observation_dates(const FirstBitRequest& r,const Shape& s,const FiniteObservation& o) {
    require(o.values.size()==s.bins+r.starts-1 && o.energy_prefix.size()==o.values.size()+1 &&
        o.first_raw_sample==r.conditioning.first_raw_sample &&
        o.observed_raw_end_sample==r.conditioning.first_raw_sample+s.raw &&
        o.first_output_sample==r.conditioning.first_raw_sample+s.delay,
        "Conditioned raw-cache score does not match complete observed support");
}
std::vector<Peak> cached_peaks(const FirstBitRequest& r,const Shape& s,const RawPlan& p,std::stop_token stop) {
    std::vector<Complex> observations(r.trials*p.field);
    std::vector<Peak> peaks(r.trials);
    require(observations.capacity()*sizeof(Complex)<=p.observation_bytes && peaks.capacity()<=r.trials,
        "Actual finite observation capacities exceed raw-cache allowance");
    {
        // Raw/noise/FIR scratch is released before the template/code cache
        // is built. Each bit pair shares exactly the same generated noise.
        std::mt19937_64 generator(r.seed);ProbabilityNormal normal;
        const auto sigma=std::sqrt(r.raw_noise_variance);
        std::vector<double> noise(s.raw);std::vector<float> raw(s.raw);
        require(noise.capacity()<=s.raw&&raw.capacity()<=s.raw,"Actual raw/noise capacities exceed raw-cache allowance");
        for(std::size_t pair=0;pair<r.trials/2;++pair) {
            estimate_detail::check(stop);for(auto& x:noise)x=sigma*normal(generator);
            for(unsigned bit=0;bit<2;++bit) {
                for(std::size_t i=0;i<raw.size();++i) {
                    const auto value=r.signal_scale*r.source_pcm[bit][i]+noise[i]+(r.helper_pcm.empty()?0:r.helper_pcm[i]);
                    require(std::isfinite(value)&&std::abs(value)<=std::numeric_limits<float>::max(),"Finite reference PCM exceeded binary32 range");
                    raw[i]=static_cast<float>(value);
                }
                const auto o=condition_with_shape(r,s,raw,4096,stop);
                std::copy(o.values.begin(),o.values.end(),observations.begin()+static_cast<std::ptrdiff_t>((2*pair+bit)*p.field));
            }
        }
    }
    RawScorer scorer(r,s,p);
    for(std::size_t first=0;first<s.selected;first+=p.tile) {
        scorer.prepare(first,stop);
        for(std::size_t draw=0;draw<r.trials;++draw)
            peaks[draw].collect(scorer.score(std::span(observations).subspan(draw*p.field,p.field),stop));
    }
    return peaks;
}
}

FiniteObservation condition_observations(const FirstBitRequest& r,std::span<const float> raw,
                                        std::size_t chunk,std::stop_token stop) {
    return condition_with_shape(r,shape(r,false),raw,chunk,stop);
}
std::vector<modem::detail::FftSearchScore> score_observations(const FirstBitRequest& r,
        const FiniteObservation& observation,std::stop_token stop) {
    return score_with_shape(r,shape(r,false),observation,stop);
}


std::vector<modem::detail::FftSearchScore> score_observations_prepared(const FirstBitRequest& r,
        const FiniteObservation& observation,std::stop_token stop) {
    const auto s=shape(r,false);validate_observation_dates(r,s,observation);
    require(observation.values.capacity()<=maximum_owned_bytes/sizeof(Complex) &&
        observation.energy_prefix.capacity()<=maximum_owned_bytes/sizeof(double),"Borrowed observation capacity exceeds finite budget");
    const auto bytes=s.selected*r.starts*sizeof(modem::detail::FftSearchScore)+
        (observation.values.capacity()-observation.values.size())*sizeof(Complex)+
        observation.energy_prefix.capacity()*sizeof(double)+sizeof(observation);
    const auto plan=raw_plan(r,s,1,bytes);
    RawScorer scorer(r,s,plan);std::vector<modem::detail::FftSearchScore> result;result.reserve(s.selected*r.starts);
    for(std::size_t first=0;first<s.selected;first+=plan.tile) {
        scorer.prepare(first,stop);const auto scores=scorer.score(observation.values,stop);
        result.insert(result.end(),scores.begin(),scores.end());
    }
    return result;
}

FirstBitReference conditioned_first_bit_reference(const FirstBitRequest& r,std::stop_token stop) {
    FirstBitReference result;
    result.limit="Conditional isolated FIRST-BIT logical-input-AWGN reference at a fixed production source phase/CFO/LF, "
        "given independent frozen helper evidence before every scored raw FIR-halo sample. "
        "Original LF alternatives and admission threshold/trial charge retained. "
        "Adaptive helper qualification, outer presence guard, unknown adjacent symbols, full rolling/key/epoch acquisition, "
        "physical preselector/decimator, continuation, physical END and hardware sensitivity are unqualified.";
    Shape s;
    try {s=shape(r,true);}catch(const Error& e) {result.limit=e.what();return result;}
    result.threshold=r.original_acquisition_threshold;result.original_threshold_trials=r.original_threshold_trials;
    result.original_jobs=r.original_jobs.size();result.selected_jobs=s.selected;result.retained_clock_rates=s.rates;
    result.raw_samples=s.raw;result.projected_samples=s.bins+r.starts-1;result.filter_delay=s.delay;
    result.filter_multiply_add_bound=s.filter_work;result.template_sample_bound=s.template_work;
    result.uncached_template_sample_bound=s.uncached_template_work;result.direct_complex_product_bound=s.score_work;
    result.first_raw_sample=r.conditioning.first_raw_sample;
    result.observed_raw_end_sample=result.first_raw_sample+s.raw;
    if(r.helper_pcm.empty())result.limit+=" Helper residual tone/punctuation/metadata leakage is not modeled.";
    // Fixed project-owned buffers only: no covariance matrix, PCM history,
    // receiver object, adaptive bank or complete-message state is retained.
    // Reserve an explicit upper allowance for one actual template/code cache
    // and the simultaneous direct-score worker buffers. Caller input included.
    result.owned_bytes=input_bytes(r)+3*s.raw*sizeof(float)+s.raw*sizeof(double)+
        2*(s.bins+r.starts)*sizeof(Complex)+2*(s.bins+r.starts)*sizeof(double)+
        2*transform_size(s.bins+r.starts-1)*sizeof(Complex)+
        2*s.selected*sizeof(modem::detail::FftSearchJob)+s.selected*r.starts*sizeof(modem::detail::FftSearchScore)+
        (2*s.delay+1)*(sizeof(double)+sizeof(float))+1024*1024;
    RawPlan plan;
    if(s.prepared_cache) {
        try {plan=raw_plan(r,s,r.trials);}catch(const Error& e) {result.limit=e.what();return result;}
        result.owned_bytes=plan.owned_bytes;result.prepared_template_cache=true;
        result.cached_observation_bytes=plan.observation_bytes;result.prepared_tile_jobs=plan.tile;
    }
    if(result.owned_bytes>maximum_owned_bytes) {result.limit="Finite reference owned-buffer budget exceeded";return result;}
    std::size_t correct=0,wrong=0;
    if(s.prepared_cache) {
        const auto peaks=cached_peaks(r,s,plan,stop);
        for(std::size_t draw=0;draw<peaks.size();++draw)if(peaks[draw].best>=r.original_acquisition_threshold &&
                peaks[draw].best-peaks[draw].other>=1) {
            correct+=peaks[draw].chosen==draw%2;wrong+=peaks[draw].chosen!=draw%2;
        }
    } else {
    std::mt19937_64 generator(r.seed);ProbabilityNormal normal;
    const auto sigma=std::sqrt(r.raw_noise_variance);
    std::vector<double> noise(s.raw);std::vector<float> raw(s.raw);
    for(std::size_t pair=0;pair<r.trials/2;++pair) {
        estimate_detail::check(stop);
        for(auto& x:noise)x=sigma*normal(generator);
        for(unsigned bit=0;bit<2;++bit) {
            for(std::size_t i=0;i<raw.size();++i) {
                const auto value=r.signal_scale*r.source_pcm[bit][i]+noise[i]+(r.helper_pcm.empty()?0:r.helper_pcm[i]);
                require(std::isfinite(value) && std::abs(value)<=std::numeric_limits<float>::max(),"Finite reference PCM exceeded binary32 range");
                raw[i]=static_cast<float>(value);
            }
            auto observation=condition_with_shape(r,s,raw,4096,stop);
            const auto scores=score_with_shape(r,s,observation,stop);
            // Complete finite selected grid shares the SAME observations.
            // Native collect_score()/overlapping-peak clustering keeps the
            // largest cell with its own paired alternative. As in the existing
            // EME finite-grid reference, ties retain original cell order.
            double best=-1,other=0;unsigned chosen=0;
            for(const auto& score:scores) {
                const auto winner=score.one>score.zero?1U:0U;
                const auto value=winner?score.one:score.zero;
                if(value>best) {best=value;other=winner?score.zero:score.one;chosen=winner;}
            }
            if(best>=r.original_acquisition_threshold && best-other>=1) {
                correct+=chosen==bit;wrong+=chosen!=bit;
            }
        }
    }
    }
    result.trials=r.trials;result.correct=static_cast<double>(correct)/r.trials;result.wrong=static_cast<double>(wrong)/r.trials;
    // Paired bit trials share noise; their two Bernoulli outcomes need not be
    // independent. Hoeffding on independent pair averages in [0,1] supplies a
    // conservative 95% interval without counting the pair as two fresh draws.
    const auto half=std::sqrt(std::log(40.)/(2*static_cast<double>(r.trials/2)));
    result.correct_low=std::max(0.,result.correct-half);result.correct_high=std::min(1.,result.correct+half);
    result.reference_probability_available=true;return result;
}
} // namespace datapump::simulation::detail::conditioned_private
