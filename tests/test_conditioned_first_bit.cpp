// PRIVATE finite-reference checks. No keyfiles/devices/GUI/full draft decoding.
#include "conditioned_first_bit.hpp"
#include "estimate_cancellation.hpp"
#include "datapump/channel.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include "gui/fhss_profile.hpp"
#include "pattern_observation_layout.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <string_view>

using namespace datapump;
namespace p=simulation::detail::conditioned_private;
namespace {
constexpr double tau=2*std::numbers::pi;
void check(bool value,const char* text) {if(!value)throw Error(text);}
template<class F> void rejects(F&& f,const char* text) {
    bool rejected=false;try {f();}catch(const Error&){rejected=true;}check(rejected,text);
}
modem::detail::FftPatternParameters pattern(const modem::Config& c) {
    modem::detail::FftPatternParameters v;
    v.epoch=c.stream_epoch;v.chip_samples=modem::pattern_chip_samples(c);v.chips_per_symbol=modem::pattern_chips_per_symbol(c);
    v.symbol_samples=modem::symbol_sample_count(c);v.sample_rate=c.sample_rate;v.spreading_mode=static_cast<std::uint32_t>(c.spreading_mode);
    v.shaped=modem::pattern_pulse_enabled(c);v.scramble=c.scramble;v.dsss=c.dsss;
    v.spreading_seed=c.spreading_seed;v.dsss_seed=c.dsss_seed;v.dsss_factor=c.dsss_factor;
    v.outer_dsss_version=static_cast<std::uint32_t>(c.outer_dsss_version);v.legacy_public=modem::legacy_public_pattern(c);
    // Native Impl::scoring_geometry() has its own exact operator metadata;
    // estimator smooth-ramp tags/ramp fields must not replace that identity.
    if(c.fake_hopping&&c.fake_hopping->matched_gaps) {
        const auto& h=*c.fake_hopping;
        v.fhss_matched_gaps=1;v.fhss_dwell=h.dwell_seconds;v.fhss_clock=h.clock_error_seconds;
        v.fhss_peer_clock=h.peer_clock_error_seconds;v.fhss_audio=h.audio_error_seconds;v.fhss_region=h.region_seconds;
    }
    return v;
}
p::FirstBitRequest request() {
    transfer::Options o;o.audio_timing_error_seconds=0; // Actual B acceptance uses --audio-error0ms.
    o.modem=tuning::resolve(360,40,tuning::PatternMode::auto_keystream,true,1500,10,
        modem::OuterDsssVersion::interleaved_v2).config;
    o.modem.stream_epoch=1800000006;o.modem.stream_phase_samples=0;
    std::array<std::uint8_t,32> key{};for(unsigned i=0;i<key.size();++i)key[i]=static_cast<std::uint8_t>(17+3*i);
    o.key=Crypto(key);
    auto hops=gui::fhss::resolve(gui::fhss::Preference::fcc,o.modem);check(hops.available,"Native B FakeFCC profile unavailable");
    modem::FhssPlan h;h.channels=hops.channels;h.dwell_seconds=hops.dwell_seconds;
    for(unsigned i=0;i<h.channels;++i)h.shifts[i]=hops.center(i)-o.modem.carrier_hz;
    o.modem.fake_hopping=transfer::transmit_hopping(h,o);
    o.modem=transfer::seeded_config(o,o.modem.stream_epoch);
    p::FirstBitRequest r;r.config=o.modem;
    check(r.config.sample_rate==14400 && modem::symbol_sample_count(r.config)==5120&&modem::deferred_symbols(r.config),"Native B production tuning/calendar geometry changed");
    // Three explicit ORIGINAL LF lanes, each with the same finite RF cells.
    // This is a conditional score fixture, not the complete Controller cohort.
    for(std::uint64_t symbol=0;symbol<modem::deferred_initial_symbols(r.config);++symbol)
    for(double clock:{0.,-100.,100.})for(double frequency:{0.,-.703125,.703125,-1.40625,1.40625}) {
        modem::detail::FftSearchJob j;j.symbol=symbol;j.phase=r.config.stream_phase_samples;j.clock_ratio=1+clock*1e-6;
        j.frequency_hz=frequency;j.frequency_index=r.original_jobs.size();r.original_jobs.push_back(j);
        r.selected_original_cells.push_back(std::abs(frequency)<=.704);
    }
    const auto bin=modem::pattern_projection_bin_samples(r.config,1.40625);
    const auto layout=modem::detail::pattern_observation_layout(r.config,bin,.9999);
    auto& g=r.geometry;g.pattern=pattern(r.config);g.bins_per_symbol=layout.length;g.bin_samples=bin;g.carrier_hz=r.config.carrier_hz;
    g.evidence_count=static_cast<double>(layout.length);g.extended_clock_window=1;g.drift_sections=1;
    const auto image=std::abs(modem::detail::pattern_projection_image_ratio(bin,r.config.carrier_hz,r.config.sample_rate));
    g.noise_condition=(1+image)/(1-image);g.search_arithmetic=static_cast<std::uint32_t>(modem::SearchArithmetic::fp64);
    r.starts=1;r.original_threshold_trials=layout.hop*r.original_jobs.size();
    r.original_acquisition_threshold=-std::log(1e-10)+2*std::log(static_cast<double>(r.original_threshold_trials)+1)+
        std::log(2.*r.original_jobs.size());
    r.conditioning.filter_hz=4000.;r.conditioning.frequency_correction_hz=.2;
    r.conditioning.first_raw_sample=144000;r.conditioning.prior_evidence_end_sample=143000;r.conditioning.independent_prior=true;
    const helper::PayloadFilter filter(r.config.sample_rate,*r.conditioning.filter_hz);
    const auto raw=layout.length*bin+2*filter.delay();
    for(auto& source:r.source_pcm)source.resize(raw);
    // Actual production analytic transmitter, including normal FakeFCC
    // envelope and deferred-symbol calendar. Only the isolated first source
    // and its known silence outside support enter this conditional reference.
    const auto begin=modem::pattern_pulse_padding_samples(r.config)+modem::scheduled_symbol_offset(
        r.config,r.config.stream_epoch,r.config.stream_phase_samples,0);
    check(begin>=filter.delay(),"Native source prefix does not cover the observed left FIR halo");
    const auto first=begin-filter.delay(),end=first+raw;
    check(end<=p::maximum_raw_samples+8ULL*r.config.sample_rate,"Native source generation preparation bound exceeded");
    for(unsigned bit=0;bit<2;++bit) {
        modem::PatternTransmitter tx(Bytes{static_cast<std::uint8_t>(bit)},r.config,r.config.stream_epoch,0,false);
        std::array<std::complex<double>,1024> block{};std::uint64_t emitted=0;
        while(emitted<end) {
            const auto count=tx.read_analytic(block);if(!count)break;
            for(std::size_t i=0;i<count&&emitted<end;++i,++emitted)if(emitted>=first) {
                // A fixed off-grid physical CFO, shared by source and its
                // ordinary templates' uncertain catalogue. No helper oracle.
                const auto z=block[i]*std::polar(1.,tau*.137*static_cast<double>(emitted-begin)/r.config.sample_rate);
                r.source_pcm[bit][emitted-first]=static_cast<float>(z.real());
            }
        }
    }
    r.raw_noise_variance=1e-4;r.signal_scale=1;r.trials=256;
    return r;
}

void direct_and_chunk_parity() {
    auto r=request();std::vector<float> raw(r.source_pcm[0].size());
    std::mt19937 generator(27191);std::normal_distribution<float> normal;
    for(std::size_t i=0;i<raw.size();++i)raw[i]=r.source_pcm[0][i]+.03f*normal(generator);
    const auto whole=p::condition_observations(r,raw,raw.size());
    for(const auto chunk:{1U,7U,127U,4096U}) {
        const auto split=p::condition_observations(r,raw,chunk);
        check(split.values==whole.values && split.energy_prefix==whole.energy_prefix,"FIR/projector chunking changed finite observations");
        check(split.first_output_sample==r.conditioning.first_raw_sample+288 &&
            split.observed_raw_end_sample==r.conditioning.first_raw_sample+raw.size(),"Finite observation dates lost actual halo/frontier");
    }
    helper::PayloadFilter filter(r.config.sample_rate,*r.conditioning.filter_hz);
    std::vector<float> filtered(raw.size());const auto count=filter.push(raw,filtered);
    check(count==whole.values.size()*r.geometry.bin_samples,"Direct finite filter length mismatch");
    std::complex<double> oscillator{1,0};const auto step=std::polar(1.,-tau*r.config.carrier_hz/r.config.sample_rate);
    double energy=0;
    for(std::size_t i=0;i<whole.values.size();++i) {
        std::complex<double> sum{};
        for(std::size_t j=0;j<r.geometry.bin_samples;++j) {sum+=static_cast<double>(filtered[i*r.geometry.bin_samples+j])*oscillator;oscillator*=step;}
        const auto center=i*r.geometry.bin_samples+(r.geometry.bin_samples-1)/2.;
        auto value=sum/static_cast<double>(r.geometry.bin_samples)*std::polar(1.,-tau*r.conditioning.frequency_correction_hz*center/r.config.sample_rate);
        check(value==whole.values[i],"Finite transform differs from direct production FIR/projector contraction");energy+=std::norm(value);
    }
    check(energy==whole.energy_prefix.back(),"Total energy came from a different transformed observation");
    const auto scores=p::score_observations(r,whole);
    const auto cached=p::score_observations_prepared(r,whole);
    check(cached.size()==scores.size(),"Raw prepared finite score count changed");
    for(std::size_t i=0;i<scores.size();++i)
        check(cached[i].zero==scores[i].zero&&cached[i].one==scores[i].one,
            "Raw prepared finite path changed direct FP64 cell scores");
    auto forged=whole;for(auto& e:forged.energy_prefix)e=1e200;
    const auto protected_scores=p::score_observations(r,forged);
    const auto protected_cached=p::score_observations_prepared(r,forged);
    check(scores.size()==protected_scores.size(),"Score count changed");
    for(std::size_t i=0;i<scores.size();++i)
        check(scores[i].zero==protected_scores[i].zero&&scores[i].one==protected_scores[i].one &&
            scores[i].zero==protected_cached[i].zero&&scores[i].one==protected_cached[i].one,
            "Independent/forged energy denominator was accepted");
    std::cout<<"finite_parity raw="<<raw.size()<<" bins="<<whole.values.size()<<" scores="<<scores.size()<<" energy="<<energy<<'\n';
}

void identity_and_frequency_translation() {
    auto r=request();r.conditioning.filter_hz.reset();r.conditioning.frequency_correction_hz=0;
    for(auto& source:r.source_pcm) {
        source.erase(source.begin(),source.begin()+288);
        source.resize(r.geometry.bins_per_symbol*r.geometry.bin_samples);
    }
    r.conditioning.first_raw_sample+=288;
    const auto original=p::condition_observations(r,r.source_pcm[0]);const auto a=p::score_observations(r,original);
    r.conditioning.frequency_correction_hz=.173;
    const auto rotated=p::condition_observations(r,r.source_pcm[0]);const auto b=p::score_observations(r,rotated);
    check(original.energy_prefix.size()==rotated.energy_prefix.size(),"NCO changed finite energy dimensions");
    for(std::size_t i=0;i<a.size();++i) {
        const auto tolerance=1e-8*std::max({1.,std::abs(a[i].zero),std::abs(a[i].one)});
        check(std::abs(a[i].zero-b[i].zero)<=tolerance && std::abs(a[i].one-b[i].one)<=tolerance,
            "A common frozen NCO/selected-template translation manufactured detector gain");
    }
}

void authority_and_bounds() {
    auto r=request();auto raw=r.source_pcm[0];
    r.conditioning.prior_evidence_end_sample=r.conditioning.first_raw_sample+1;
    rejects([&]{p::condition_observations(r,raw);},"Overlapping raw-halo helper evidence was accepted");
    r=request();r.conditioning.independent_prior=false;
    rejects([&]{p::condition_observations(r,raw);},"Unknown helper evidence independence was accepted");
    r=request();for(std::size_t i=0;i<r.original_jobs.size();++i)if(r.original_jobs[i].clock_ratio<1)r.selected_original_cells[i]=0;
    rejects([&]{p::condition_observations(r,raw);},"RF selection removed an original LF lane");
    r=request();r.config.sample_rate=6120;
    check(!p::conditioned_first_bit_reference(r).reference_probability_available,"Unsupported wide A received an invented B reference");
    r=request();r.geometry.differential_window_samples=32;
    check(!p::conditioned_first_bit_reference(r).reference_probability_available,"Unsupported differential geometry received a reference");
    r=request();r.geometry.pattern.legacy_public^=1;
    rejects([&]{p::condition_observations(r,raw);},"Mismatched public/private template identity was accepted");
    r=request();r.geometry.pattern.fhss_ramp+=.0001;
    rejects([&]{p::condition_observations(r,raw);},"Mismatched FHSS envelope identity was accepted");
    r=request();for(auto& j:r.original_jobs)j.frequency_hz*=3;
    rejects([&]{p::condition_observations(r,raw);},"Separated native RF peak clusters were reduced to one global peak");
    r.competing_frequencies=true;
    check(!p::condition_observations(r,raw).values.empty(),"Actual competing-frequency policy failed to admit its finite grid");
    r=request();r.starts=32;r.geometry.bin_samples=128;r.geometry.bins_per_symbol=41;
    const auto nonoverlap=p::conditioned_first_bit_reference(r);
    check(!nonoverlap.reference_probability_available && nonoverlap.limit.find("temporal")!=std::string::npos,
        "Disjoint temporal native peaks were reduced to one global peak");
}

void actual_channel_lf_controls() {
    // These are direct transform/score controls on the production transport,
    // not extra probability draws. The transport's 300 dB noise floor remains
    // present; no noiseless/resampling-equivalence claim is made.
    for(const auto ppm:{-100.,100.})for(unsigned bit=0;bit<2;++bit) {
        auto r=request();modem::ChannelConfig channel;channel.snr_db=300;
        channel.clock_error_ppm=ppm;channel.frequency_offset_hz=.137;
        channel.phase_noise_degrees_per_sqrt_second=0;channel.seed=8117;
        modem::StreamingTransmitter tx(modem::RawBits{Bytes{static_cast<std::uint8_t>(bit)}},r.config);
        modem::SampledSimulationChannel transport(r.config,channel);
        std::vector<float> capture;capture.reserve(8*r.config.sample_rate);
        std::array<float,257> block{};
        // payload_begin_sample() dates the CHANNEL source-stream onset, not
        // the shaped symbol inside its training/pulse/FHSS calendar prefix.
        // Use the same source-coordinate map as the actual native factory.
        const auto rate=1+static_cast<long double>(channel.clock_error_ppm)*1e-6L;
        const auto prefix=static_cast<long double>(modem::training_sample_count(r.config))+
            modem::pattern_pulse_padding_samples(r.config)+modem::scheduled_symbol_offset(
                r.config,r.config.stream_epoch,r.config.stream_phase_samples,0);
        const auto begin=static_cast<std::uint64_t>(std::floor(
            static_cast<long double>(transport.startup_offset_samples())+prefix/rate));
        while(capture.size()<8ULL*r.config.sample_rate) {
            const auto count=transport.read(tx,block);if(!count)break;
            capture.insert(capture.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
            if(capture.size()>=begin+r.source_pcm[bit].size())break;
        }
        check(begin>=288 && capture.size()>=begin-288+r.source_pcm[bit].size(),
            "Production clock-offset control did not cover the complete observed FIR halo");
        const auto first=begin-288;r.conditioning.first_raw_sample=first;
        r.conditioning.prior_evidence_end_sample=first;r.conditioning.independent_prior=true;
        const auto raw=std::span(capture).subspan(first,r.source_pcm[bit].size());
        const auto observation=p::condition_observations(r,raw,127);
        const auto scores=p::score_observations(r,observation);
        const auto cached=p::score_observations_prepared(r,observation);
        check(scores.size()==cached.size(),"Production LF raw prepared cell count changed");
        for(std::size_t i=0;i<scores.size();++i)
            check(scores[i].zero==cached[i].zero&&scores[i].one==cached[i].one,
                "Production transport off-grid/LF raw prepared score lost bitwise parity");
        double best=-1;unsigned chosen=0;
        for(const auto& score:scores)if(std::max(score.zero,score.one)>best) {
            best=std::max(score.zero,score.one);chosen=score.one>score.zero;
        }
        std::cout<<"logical_channel_lf ppm="<<ppm<<" bit="<<bit<<" chosen="<<chosen
            <<" score="<<best<<" raw="<<raw.size()<<" onset="<<begin<<" source_prefix="<<prefix<<'\n';
        check(std::isfinite(best)&&best>0&&chosen==bit,"Actual off-grid/LF production control lost its bit template");
    }
}

p::FirstBitRequest cache_request() {
    auto r=request();const auto jobs=r.original_jobs;const auto mask=r.selected_original_cells;
    while(std::count(r.selected_original_cells.begin(),r.selected_original_cells.end(),std::uint8_t{1})<128) {
        for(std::size_t i=0;i<jobs.size();++i) {
            auto job=jobs[i];job.frequency_index=r.original_jobs.size();
            r.original_jobs.push_back(job);r.selected_original_cells.push_back(mask[i]);
        }
    }
    check(std::count(r.selected_original_cells.begin(),r.selected_original_cells.end(),std::uint8_t{1})<=p::maximum_selected_jobs,
        "Expanded finite fixture exceeded selected-cell contract");
    // This catalogue is an exact score-cache regression, not the actual GUI
    // descriptor. Retain its own original full-grid threshold charge.
    const auto multiplier=r.original_jobs.size()/jobs.size();r.original_threshold_trials*=multiplier;
    r.original_acquisition_threshold=-std::log(1e-10)+2*std::log(static_cast<double>(r.original_threshold_trials)+1)+
        std::log(2.*r.original_jobs.size());
    return r;
}
void cached_catalogue_parity_and_bounds() {
    auto r=cache_request();const auto observation=p::condition_observations(r,r.source_pcm[1]);
    const auto expected=p::score_observations(r,observation),actual=p::score_observations_prepared(r,observation);
    check(expected.size()==actual.size(),"Expanded raw prepared cell count changed");
    for(std::size_t i=0;i<expected.size();++i)
        check(expected[i].zero==actual[i].zero&&expected[i].one==actual[i].one,
            "Expanded tiled raw prepared path changed exact original job order/scores");
    auto unsupported=r;unsupported.trials=4096;const auto too_large=p::conditioned_first_bit_reference(unsupported);
    check(!too_large.reference_probability_available &&
        (too_large.limit.find("storage")!=std::string::npos||too_large.limit.find("FIR work")!=std::string::npos),
        "Oversized cached observation draw count exceeded unchanged budgets");
    std::stop_source stopped;stopped.request_stop();
    bool cancelled=false;
    try {p::score_observations_prepared(r,observation,stopped.get_token());}
    catch(const estimate_detail::Cancelled&) {cancelled=true;}
    check(cancelled,"Prepared finite scorer ignored the distinct estimator cancellation contract");
    std::cout<<"raw_prepared_catalogue original="<<r.original_jobs.size()<<" selected="<<actual.size()
        <<" bins="<<observation.values.size()<<'\n';
}
void cached_paired_monte_carlo() {
    auto r=cache_request();const auto result=p::conditioned_first_bit_reference(r);
    check(result.reference_probability_available&&result.prepared_template_cache &&
        !result.confidence_available&&!result.draft_probability_available,
        "Expanded first-bit cache lost conditional-only reference availability");
    check(result.uncached_template_sample_bound>p::maximum_template_samples &&
        result.template_sample_bound<=p::maximum_template_samples &&
        result.direct_complex_product_bound<=p::maximum_cached_score_products &&
        result.owned_bytes<=p::maximum_owned_bytes&&result.prepared_tile_jobs&&result.cached_observation_bytes,
        "Raw cache generation, repeated score work or unchanged storage budget was miscounted");
    check(result.threshold==r.original_acquisition_threshold&&result.original_threshold_trials==r.original_threshold_trials&&
        result.retained_clock_rates==3&&result.trials==r.trials&&result.correct>result.wrong,
        "Raw cache reduced original threshold/LF/trials or lost paired source alternatives");
    // Cache expansion duplicates exact original cells, so its peak for every
    // noise draw must match the smaller original direct fixture. Use the SAME
    // expanded full-grid threshold in both; no trial penalty is reduced.
    auto base=request();base.original_threshold_trials=r.original_threshold_trials;
    base.original_acquisition_threshold=r.original_acquisition_threshold;
    const auto direct=p::conditioned_first_bit_reference(base);
    check(direct.reference_probability_available&&!direct.prepared_template_cache&&
        result.correct==direct.correct&&result.wrong==direct.wrong&&
        result.correct_low==direct.correct_low&&result.correct_high==direct.correct_high,
        "Raw cached paired observations/reduction differ from the unchanged direct MC path");
    r.signal_scale=0;const auto noise=p::conditioned_first_bit_reference(r);
    check(noise.reference_probability_available&&noise.correct==0&&noise.wrong==0,
        "Expanded raw-cache seeded noise admitted an isolated bit");
    std::cout<<"cached_mc correct="<<result.correct<<" wrong="<<result.wrong<<" original="<<result.original_jobs
        <<" selected="<<result.selected_jobs<<" bins="<<result.projected_samples<<" template_once="<<result.template_sample_bound
        <<" original_template_work="<<result.uncached_template_sample_bound<<" direct_products="<<result.direct_complex_product_bound
        <<" observation_bytes="<<result.cached_observation_bytes<<" tile="<<result.prepared_tile_jobs
        <<" owned="<<result.owned_bytes<<'\n';
}
void paired_monte_carlo() {
    auto r=request();const auto first=p::conditioned_first_bit_reference(r),same=p::conditioned_first_bit_reference(r);
    check(first.reference_probability_available && !first.confidence_available && !first.draft_probability_available,
        "First-bit reference was presented as full RX/draft qualification");
    check(first.correct==same.correct && first.wrong==same.wrong && first.correct_low==same.correct_low,
        "Paired finite reference lost deterministic common random numbers");
    check(first.correct>first.wrong && first.correct>0,"Strong actual production source did not distinguish its two bit alternatives");
    check(first.threshold==r.original_acquisition_threshold && first.original_threshold_trials==r.original_threshold_trials,
        "RF selection reduced original threshold/trial charges");
    check(first.owned_bytes<=p::maximum_owned_bytes && first.filter_multiply_add_bound<=p::maximum_filter_multiply_adds,
        "Finite reference exceeded its explicit work/storage bounds");
    auto null=r;null.signal_scale=0;const auto noise=p::conditioned_first_bit_reference(null);
    check(noise.reference_probability_available && noise.correct==0 && noise.wrong==0,
        "Seeded noise control admitted a bit (this finite test is not a rare-FAP proof)");
    std::cout<<"paired_mc correct="<<first.correct<<" wrong="<<first.wrong<<" low="<<first.correct_low<<" high="<<first.correct_high
        <<" trials="<<first.trials<<" storage_allowance="<<first.owned_bytes<<" filter_work="<<first.filter_multiply_add_bound<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        direct_and_chunk_parity();identity_and_frequency_translation();authority_and_bounds();actual_channel_lf_controls();cached_catalogue_parity_and_bounds();
        if(argc==2&&std::string_view(argv[1])=="--monte-carlo") {paired_monte_carlo();cached_paired_monte_carlo();}
        else check(argc==1,"Only --monte-carlo is supported");
        std::cout<<"private finite first-bit checks passed\n";return 0;
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
