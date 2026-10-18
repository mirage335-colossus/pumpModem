#include "datapump/channel.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/receiver_strategy.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/tuning.hpp"
#include "../src/eme_probability.hpp"
#include "../src/pattern_fft_batch.hpp"
#include <bit>
#include <numbers>
#include <random>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <openssl/evp.h>
#include <stdexcept>
#include <string_view>
using namespace datapump;
namespace {
bool diagnostic_capture=false;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
auto evidence_key(const modem::PatternEvidence& e) {
    return std::array<std::uint64_t,13>{e.first_sample,e.end_sample,e.stream_symbol,
        std::bit_cast<std::uint64_t>(e.frequency_hz),std::bit_cast<std::uint64_t>(e.score),
        std::bit_cast<std::uint64_t>(e.alternative_score),e.bit,e.stream_phase_samples,
        std::bit_cast<std::uint64_t>(e.admission_threshold),e.frequency_hypothesis,
        e.outer_presence_active,std::bit_cast<std::uint64_t>(e.outer_presence_score),e.completed_event};
}
void diagnostic_pcm(const std::vector<float>& pcm,const Bytes& expected) {
    std::array<unsigned char,EVP_MAX_MD_SIZE> digest{};unsigned size=0;
    require(EVP_Digest(pcm.data(),pcm.size()*sizeof(float),digest.data(),&size,EVP_sha256(),nullptr)==1,
        "diagnostic PCM digest failed");
    std::cout<<"diagnostic_pcm samples="<<pcm.size()<<" float_bytes="<<sizeof(float)<<" sha256=";
    for(unsigned i=0;i<size;++i)std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(digest[i]);
    std::cout<<std::dec<<std::setfill(' ')<<" expected=";
    for(auto bit:expected)std::cout<<unsigned(bit);
    std::cout<<'\n';
}
transfer::Options eme(unsigned seconds) {
    transfer::Options o;
    o.modem=tuning::resolve(1,-3.0720996964786846,tuning::PatternMode::auto_pattern,false,1500).config;
    o.modem.integration_seconds=seconds;o.timestamp=o.modem.stream_epoch=1800000041;
    o.search_seconds=6;o.dsp_workspace_bytes=128*1024*1024;
    modem::OscillatorSearchConfig osc;osc.lf={.0001,.005};osc.rf=osc.lf;
    osc.rf_shift_hz=5.8e9;osc.margin=3;osc.reference=modem::OscillatorReference::independent_audio;
    o.modem.oscillator_search=osc;
    return o;
}
modem::ChannelConfig channel(const modem::Config& c,double es,std::uint64_t seed) {
    modem::ChannelConfig ch;ch.snr_db=es-10*std::log10(modem::symbol_sample_count(c)/2.);
    ch.seed=100003+104729*seed;
    // Independent startup, phase and noise streams in the production channel.
    // Spread true arrivals across the configured positive six-second window;
    // neither the true arrival nor the seed is supplied to receiver search.
    ch.delay_samples=137+(seed*1543)%35000;
    ch.clock_error_ppm=modem::oscillator_effects(c).clock_error_ppm;
    ch.frequency_offset_hz=modem::oscillator_effects(c).frequency_offset_hz;
    ch.phase_noise_degrees_per_sqrt_second=modem::oscillator_effects(c).phase_noise_degrees_per_sqrt_second;
    return ch;
}
std::uint64_t bit_draw(std::uint64_t value) {
    value+=0x9e3779b97f4a7c15ULL;value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value=(value^(value>>27))*0x94d049bb133111ebULL;return value^(value>>31);
}
double source_power(const modem::Config& c,unsigned bit) {
    struct Entry {std::uint64_t samples;unsigned bit;double mean;};
    static std::vector<Entry> cache;
    const auto n=modem::symbol_sample_count(c);
    for(const auto& e:cache)if(e.samples==n&&e.bit==bit)return e.mean;
    modem::PatternTransmitter tx(Bytes{static_cast<std::uint8_t>(bit)},c,c.stream_epoch,0,false);
    std::array<std::complex<double>,4096> block{};std::uint64_t emitted=0,counted=0;
    const auto begin=modem::pattern_pulse_padding_samples(c);
    long double energy=0,real_energy=0;std::complex<long double> image{};
    while(counted<n) {
        const auto got=tx.read_analytic(block);require(got>0,"source ended before useful-symbol power measurement");
        for(std::size_t j=0;j<got;++j,++emitted) {
            if(emitted<begin||emitted-begin>=n)continue;
            const std::complex<long double> z{block[j].real(),block[j].imag()};
            energy+=std::norm(z);real_energy+=z.real()*z.real();image+=z*z;++counted;
        }
    }
    const auto mean=double(energy/(2*n*modem::nominal_signal_power));
    const auto fixed=double(real_energy/(n*modem::nominal_signal_power));
    const auto ratio=double(std::abs(image)/energy);
    std::cout<<"source seconds="<<double(n)/c.sample_rate<<" bit="<<bit<<" mean_power="<<mean
        <<" fixed_power="<<fixed<<" image_ratio="<<ratio<<'\n';
    require(ratio<1e-6&&std::abs(fixed/mean-1)<1e-6,"fixed-phase source normalization needs separate qualification");
    cache.push_back({n,bit,mean});return mean;
}
struct Event {
    Bytes bits;std::uint64_t first=0,end=0;bool complete=false;std::uint64_t polled_at=0;
    bool operator==(const Event&)const=default;
};
struct Outcome {
    Bytes bits;std::vector<Event> events;std::size_t complete=0;
    std::uint64_t completion_at=0;std::uint64_t axis=0,tracking=0,first_trials=0,first_starts=0;
    double first_threshold=0;
};
Outcome receive(modem::Config c,const transfer::Options& o,const std::vector<float>& pcm,
                const auto& bank,modem::ReceiverStrategy strategy) {
    c.receiver_strategy=strategy;
    modem::PatternSearch search;search.hypotheses=bank.hypotheses;
    search.start_offset_seconds=double(modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c))/c.sample_rate;
    search.start_uncertainty_seconds=o.search_seconds+1;search.worker_threads=1;search.chunk_bits=1;
    const auto plan=modem::resolve_receiver_strategy(c,search,o.dsp_workspace_bytes);
    require(plan.supported&&plan.backend==(strategy==modem::ReceiverStrategy::existing_only?
        modem::ReceiverBackend::existing_complex:modem::ReceiverBackend::frequency_axis),"capture selected another backend");
    modem::PatternReceiver rx(c,o.dsp_workspace_bytes,search);Outcome out;
    const auto backend=strategy==modem::ReceiverStrategy::existing_only?"existing":"frequency-axis";
    std::vector<modem::PatternEvidence> previous_history;
    bool history_saturated=false;
    for(std::size_t at=0;at<pcm.size();) {
        const auto n=std::min<std::size_t>(4096,pcm.size()-at);
        rx.push(std::span(pcm).subspan(at,n));at+=n;
        // Consume every next progress poll, including single pending bits.
        for(const auto& e:rx.take_bursts()) {
            if(diagnostic_capture) {
                std::cout<<"diagnostic_event backend="<<backend<<" poll="<<at<<" first="<<e.first_sample
                    <<" end="<<e.end_sample<<" first_symbol="<<e.first_stream_symbol
                    <<" stream_first="<<e.stream_first_sample<<" stream_symbol="<<e.stream_first_symbol
                    <<" phase="<<e.stream_phase_samples<<" frequency="<<e.frequency_hz
                    <<" score="<<e.score<<" support="<<e.support_samples<<" missing="<<e.missing_slots
                    <<" complete="<<e.complete<<" bits=";
                for(auto bit:e.bits)std::cout<<unsigned(bit);
                std::cout<<'\n';
            }
            if(out.events.empty()&&!e.bits.empty()) {
                const auto work=rx.fft_work();out.first_trials=work.threshold_trials;out.first_starts=work.retained_start_trials;
                for(const auto& candidate:rx.candidates())
                    if(candidate.first_sample==e.first_sample&&candidate.bit==e.bits.front())
                        out.first_threshold=candidate.admission_threshold;
            }
            out.events.push_back({e.bits,e.first_sample,e.end_sample,e.complete,at});
            out.bits.insert(out.bits.end(),e.bits.begin(),e.bits.end());
            if(e.complete){++out.complete;out.completion_at=at;}
        }
        if(diagnostic_capture) {
            auto history=rx.candidates();
            const auto prefix=previous_history.size()<=history.size() &&
                std::equal(previous_history.begin(),previous_history.end(),history.begin(),
                    [](const auto& a,const auto& b){return evidence_key(a)==evidence_key(b);});
            const auto changed=previous_history.size()!=history.size() || !prefix;
            if(!prefix || (history.size()==search.candidate_limit && !history_saturated))
                std::cout<<"diagnostic_history backend="<<backend<<" poll="<<at<<" size="<<history.size()
                    <<" ordered_prefix="<<prefix<<" candidate_limited="<<rx.candidate_limited()
                    <<" saturated="<<(history.size()==search.candidate_limit)
                    <<" note=history_cannot_prove_unretained_scores_or_rejection_reason\n";
            if(changed)for(std::size_t i=prefix?previous_history.size():0;i<history.size();++i) {
                const auto& e=history[i];
                std::cout<<"diagnostic_candidate backend="<<backend<<" poll="<<at<<" history_index="<<i
                    <<" first="<<e.first_sample<<" end="<<e.end_sample<<" symbol="<<e.stream_symbol
                    <<" phase="<<e.stream_phase_samples<<" frequency="<<e.frequency_hz<<" lane="<<e.frequency_hypothesis
                    <<" bit="<<e.bit<<" score="<<e.score<<" alternative="<<e.alternative_score
                    <<" threshold="<<e.admission_threshold<<" outer_active="<<e.outer_presence_active
                    <<" outer_score="<<e.outer_presence_score<<" completed_event="<<e.completed_event<<'\n';
            }
            history_saturated=history_saturated || history.size()==search.candidate_limit;
            previous_history=std::move(history);
        }
        require(rx.working_bytes()<=o.dsp_workspace_bytes,"receiver exceeded workspace");
        // This is a one-burst fixture. Stop only after the receiver publishes
        // physical completion; source EOF never substitutes for that event.
        if(out.complete)break;
    }
    out.axis=rx.fft_work().frequency_axis_batches;out.tracking=rx.fft_work().frequency_axis_tracking_batches;
    return out;
}
void exact_score_algebra() {
    for(const auto seconds:{128U,256U}) {
        auto c=eme(seconds).modem;const auto length=modem::symbol_sample_count(c)/750+1;
        std::vector<std::complex<double>> obs(length+2);
        std::mt19937_64 rng(913+seconds);std::normal_distribution<double> normal;
        for(auto& z:obs)z={normal(rng),normal(rng)};
        // A real finite source contribution also exercises unequal row norms,
        // guard-bin energy and both candidate branches near physical signals.
        const auto source=simulation::detail::eme_source_projection(c,1,.37,.61,1,25);
        for(std::size_t j=0;j<obs.size();++j)obs[j]+=source.observations[j]*.3;
        const auto grid=simulation::detail::eme_score_grid(c,obs,1,2);
        modem::detail::FftSearchBatch batch;auto& g=batch.geometry;
        g.pattern={c.stream_epoch,modem::pattern_chip_samples(c),modem::pattern_chips_per_symbol(c),
            modem::symbol_sample_count(c),c.sample_rate,static_cast<std::uint32_t>(c.spreading_mode),1,0,0,
            c.spreading_seed,c.dsss_seed,c.dsss_factor,static_cast<std::uint32_t>(c.outer_dsss_version)};
        g.pattern.legacy_public=modem::legacy_public_pattern(c);g.bin_samples=750;g.bins_per_symbol=length;
        g.carrier_hz=c.carrier_hz;g.evidence_count=double(length);g.drift_sections=4;g.extended_clock_window=1;
        const auto image=std::abs(modem::detail::pattern_projection_image_ratio(750,c.carrier_hz,c.sample_rate));
        g.noise_condition=(1+image)/(1-image);
        const auto transform=std::bit_ceil(2*length);
        std::vector<std::complex<double>> spectrum(transform),product(transform);
        std::vector<double> energy(obs.size()+1);
        for(std::size_t j=0;j<obs.size();++j)energy[j+1]=energy[j]+std::norm(obs[j]);
        batch.observations=obs;batch.spectrum=spectrum;batch.energy_prefix=energy;batch.starts=batch.score_stride=3;
        modem::PatternCode code(c,c.stream_epoch);
        std::vector<modem::detail::FftSearchScore> expected(3);
        std::vector<modem::detail::FftDriftAccumulator> scratch(3);
        double worst=0;
        for(std::size_t f=0;f<grid.hypotheses.size();++f) {
            const auto h=grid.hypotheses[f];modem::detail::FftSearchJob job{0,0,f,h.frequency_offset_hz};
            job.clock_ratio=1+h.clock_error_ppm*1e-6;
            modem::detail::execute_drift_search_job(batch,job,expected,product,scratch,code);
            for(unsigned j=0;j<3;++j)for(unsigned bit=0;bit<2;++bit)
                worst=std::max(worst,std::abs(grid.cells[3*f+j].combined[bit]-(bit?expected[j].one:expected[j].zero)));
        }
        std::cout<<"score_algebra seconds="<<seconds<<" max_error="<<worst<<'\n';
        require(worst<1e-9,"EME joint model disagrees with production direct coherent/quarter score");
    }
}
void source_projection_checks() {
    for(const auto seconds:{128U,256U})for(unsigned bit=0;bit<2;++bit) {
        const auto o=eme(seconds);const auto& c=o.modem;constexpr unsigned radius=2;
        const auto padding=modem::pattern_pulse_padding_samples(c);
        for(const auto phase:{0.,std::numbers::pi/4,std::numbers::pi/2}) {
            constexpr std::uint64_t delay=30;
            const auto modeled=simulation::detail::eme_source_projection(c,bit,double(delay)/750,phase,radius,25);
            modem::PatternTransmitter tx(Bytes{static_cast<std::uint8_t>(bit)},c,c.stream_epoch,0,false);
            std::array<std::complex<double>,4096> block{};
            std::vector<std::complex<double>> direct(modeled.observations.size());
            const auto begin=padding-radius*750,end=begin+direct.size()*750;
            const auto rotation=std::polar(1.,phase+2*std::numbers::pi*c.carrier_hz*delay/c.sample_rate);
            constexpr std::array<std::complex<double>,4> inverse{{{1,0},{0,-1},{-1,0},{0,1}}};
            const auto amplitude=std::sqrt(2*modem::nominal_signal_power);std::uint64_t emitted=0;
            while(emitted+delay<end) {
                const auto n=tx.read_analytic(block);require(n>0,"direct source projection ended early");
                for(std::size_t j=0;j<n;++j,++emitted) {
                    const auto at=emitted+delay;if(at<begin||at>=end)continue;
                    direct[(at-begin)/750]+=2*(block[j]*rotation).real()*inverse[at%4]/amplitude/750.;
                }
            }
            double error=0;for(std::size_t j=0;j<direct.size();++j)error=std::max(error,std::abs(direct[j]-modeled.observations[j]));
            require(error<1e-9,"EME source prefix changed exact grid real-PCM phase/image projection");
        }
        // Independent production fractional resampler and random initial phase.
        for(const auto seed:{17ULL,29ULL}) {
            auto ch=channel(c,30,seed);ch.snr_db=300;ch.phase_noise_degrees_per_sqrt_second=0;
            modem::StreamingTransmitter tx(modem::RawBits{Bytes{static_cast<std::uint8_t>(bit)}},c,o.dsp_workspace_bytes);
            modem::SampledSimulationChannel sampled(c,ch);
            const auto startup=sampled.startup_offset_samples();const auto arrival=(padding+startup)/750;
            const auto frequency=ch.frequency_offset_hz+c.carrier_hz*ch.clock_error_ppm*1e-6;
            const auto phase=sampled.carrier_phase_radians()+2*std::numbers::pi*frequency*startup/c.sample_rate;
            const auto anchor=static_cast<std::uint64_t>(std::floor(arrival))-radius;
            const auto fraction=arrival-std::floor(arrival);
            const auto coarse=simulation::detail::eme_source_projection(c,bit,fraction,phase,radius,75,ch.frequency_offset_hz,ch.clock_error_ppm);
            const auto fine=simulation::detail::eme_source_projection(c,bit,fraction,phase,radius,125,ch.frequency_offset_hz,ch.clock_error_ppm);
            std::vector<float> pcm;std::array<float,4096> block{};
            const auto until=(anchor+coarse.observations.size())*750;
            while(pcm.size()<until) {const auto n=sampled.read(tx,block);require(n>0,"fractional source capture ended early");pcm.insert(pcm.end(),block.begin(),block.begin()+n);}
            constexpr std::array<std::complex<double>,4> inverse{{{1,0},{0,-1},{-1,0},{0,1}}};
            const auto amplitude=std::sqrt(2*modem::nominal_signal_power);
            double projection_error=0,resolution_error=0;
            for(std::size_t j=0;j<coarse.observations.size();++j) {
                std::complex<double> direct{};
                for(std::uint64_t at=(anchor+j)*750;at<(anchor+j+1)*750;++at)
                    direct+=2.*pcm[at]*inverse[at%4]/amplitude/750.;
                projection_error=std::max(projection_error,std::abs(direct-coarse.observations[j]));
                resolution_error=std::max(resolution_error,std::abs(fine.observations[j]-coarse.observations[j]));
            }
            std::cout<<"source_projection seconds="<<seconds<<" bit="<<bit<<" seed="<<seed
                <<" channel_error="<<projection_error<<" resolution_error="<<resolution_error<<'\n';
            require(projection_error<.003&&resolution_error<.0001,"fractional EME source projection exceeds bounded approximation");
            require(std::abs(coarse.mean_energy/modem::symbol_sample_count(c)-source_power(c,bit))<1e-9,
                "EME joint source power differs from independent full PCM integration");
        }
    }
}
void convergence(unsigned seconds,double es,std::size_t trials) {
    auto o=eme(seconds);const auto diffusion=modem::oscillator_effects(o.modem).phase_noise_degrees_per_sqrt_second;
    const auto run=[&](unsigned r,unsigned k,unsigned sub) {
        const auto effects=modem::oscillator_effects(o.modem);
        return simulation::detail::eme_receiver_probability(o,simulation::ReceiverWorkMode::hardware_fallback,
            es,diffusion,{trials,r,k,sub,effects.frequency_offset_hz,effects.clock_error_ppm});
    };
    const auto base=run(8,4,75),wide=run(16,8,75),outer=run(32,16,75),fine=run(8,4,125);
    std::size_t grid_changes=0,outer_changes=0,source_changes=0;
    for(std::size_t i=0;i<trials;++i) {
        grid_changes+=base.trial_categories[i]!=wide.trial_categories[i];
        source_changes+=base.trial_categories[i]!=fine.trial_categories[i];
        outer_changes+=base.trial_categories[i]!=outer.trial_categories[i];
    }
    std::cout<<"convergence seconds="<<seconds<<" es="<<es<<" trials="<<trials<<" p="<<base.probability.acquired_correct
        <<" wide_p="<<wide.probability.acquired_correct<<" fine_p="<<fine.probability.acquired_correct
        <<" outer_p="<<outer.probability.acquired_correct<<" grid_changes="<<grid_changes<<" outer_changes="<<outer_changes<<" source_changes="<<source_changes
        <<" timing_boundary="<<base.timing_boundary_admissions<<" carrier_boundary="<<base.carrier_boundary_admissions<<'\n';
    require(grid_changes<=trials/100&&outer_changes<=trials/100&&source_changes<=trials/1000,"EME local model has not converged at its declared bounds");
}
void prediction(unsigned seconds,double es,bool hardware=false,bool existing=false) {
    auto o=eme(seconds);o.modem.receiver_strategy=existing?modem::ReceiverStrategy::existing_only:modem::ReceiverStrategy::complex_only;
    transfer::Estimate wire;wire.wire_bits=1;wire.total_seconds=seconds+35;
    const auto ch=channel(o.modem,es,1);
    const auto p=simulation::estimate(wire,o,true,ch,{},1,true,100,4096,hardware?
        simulation::ReceiverWorkMode::hardware_fallback:simulation::ReceiverWorkMode::sampled_simulation);
    std::cout<<"prediction seconds="<<seconds<<" es="<<es<<" hardware="<<hardware<<" existing="<<existing<<" p="<<p.one_bit_success_probability
        <<" low="<<p.one_bit_probability_low<<" high="<<p.one_bit_probability_high
        <<" trials="<<p.probability_trials<<" confidence="<<p.one_bit_confidence_available
        <<" reference="<<p.one_bit_reference_available<<" modeled_es="<<p.modeled_symbol_snr_db<<'\n';
    require(p.one_bit_confidence_available||p.one_bit_reference_available,"EME source model unavailable");
}
void probability_scope() {
    auto o=eme(128);auto ch=channel(o.modem,18,1);
    transfer::Estimate wire;wire.wire_bits=1;wire.total_seconds=163;
    const auto estimate=[&](const transfer::Options& options,const modem::ChannelConfig& impairment,
            std::span<const modem::Config> profiles={},std::size_t keys=1,std::size_t draws=256,
            simulation::ReceiverWorkMode mode=simulation::ReceiverWorkMode::hardware_fallback,bool raw=true) {
        return simulation::estimate(wire,options,raw,impairment,profiles,keys,true,100,draws,mode);
    };
    const auto first=estimate(o,ch);
    require(!first.confidence_available&&!first.one_bit_confidence_available&&first.probability_reference_only&&
        first.reference_probability_available&&first.one_bit_reference_available&&!first.probability_interval_available&&
        first.success_probability==first.one_bit_success_probability,"EME first-bit reference flags changed");
    wire.wire_bits=2;const auto two=estimate(o,ch);wire.wire_bits=1;
    require(!two.confidence_available&&!two.reference_probability_available&&two.one_bit_reference_available&&
        !two.probability_interval_available&&two.success_probability==0&&two.coherent_success_probability==0&&
        two.success_probability_low==0&&two.success_probability_high==1&&
        two.one_bit_success_probability==first.one_bit_success_probability&&two.probability_trials==first.probability_trials&&
        two.probability_model_limit.find("continuation")!=std::string::npos,"EME borrowed a multi-bit probability");
    const auto unavailable=[&](const auto& value) {
        require(!value.confidence_available&&!value.one_bit_confidence_available&&!value.reference_probability_available&&
            !value.one_bit_reference_available,"unsupported EME configuration borrowed probability");
    };
    auto profile=o.modem;profile.oscillator_search->margin=2;
    unavailable(estimate(o,ch,std::span(&profile,1)));
    profile=o.modem;profile.frequency_search_multiplier=2;unavailable(estimate(o,ch,std::span(&profile,1)));
    profile=o.modem;profile.stream_phase_samples=1;unavailable(estimate(o,ch,std::span(&profile,1)));
    const std::array profiles{o.modem,o.modem};
    auto changed=ch;changed.frequency_offset_hz=.1;unavailable(estimate(o,changed));
    changed=ch;changed.clock_error_ppm=.00005;unavailable(estimate(o,changed));
    auto other=o;other.modem.oscillator_search->sideband=modem::OscillatorSideband::lower;
    unavailable(estimate(other,ch));
    const auto reference=[&](const auto& value) {
        require(!value.confidence_available&&!value.one_bit_confidence_available&&value.probability_reference_only&&
            value.reference_probability_available&&value.one_bit_reference_available&&!value.probability_interval_available,
            "untested EME conditions gained qualified confidence");
    };
    reference(estimate(o,ch,profiles));reference(estimate(o,ch,{},2));
    reference(estimate(o,ch,profiles,1,4096));reference(estimate(o,ch,{},2,4096));
    const auto qualified=[&](const auto& value) {
        require(value.confidence_available&&value.one_bit_confidence_available&&!value.probability_reference_only&&
            !value.reference_probability_available&&!value.one_bit_reference_available&&value.probability_interval_available&&
            value.probability_trials==4096&&value.success_probability==value.one_bit_success_probability&&
            value.probability_model_limit.find("tested signal level")!=std::string::npos,
            "tested one-bit EME anchor lost qualified scope");
    };
    using Mode=simulation::ReceiverWorkMode;
    const auto full=estimate(o,ch,{},1,4096);qualified(full);
    qualified(estimate(o,channel(o.modem,1.0926996097583093,1),{},1,4096));
    reference(estimate(o,channel(o.modem,18.001,1),{},1,4096));
    reference(estimate(o,ch,{},1,512));
    changed=ch;changed.clock_error_ppm=changed.frequency_offset_hz=0;
    reference(estimate(o,changed,{},1,4096));
    changed=ch;changed.phase_noise_degrees_per_sqrt_second/=2;
    reference(estimate(o,changed,{},1,4096));
    reference(estimate(o,ch,{},1,4096,Mode::sampled_simulation));
    reference(estimate(o,ch,{},1,4096,Mode::hardware_timing_model));
    other=o;other.modem.search_arithmetic=modem::SearchArithmetic::fp32;
    reference(estimate(other,ch,{},1,4096));
    profile=o.modem;profile.search_arithmetic=modem::SearchArithmetic::fp64;
    reference(estimate(o,ch,std::span(&profile,1),1,4096));
    const auto longer=eme(256);wire.total_seconds=291;
    reference(estimate(longer,channel(longer.modem,1.0926996097583093,1),{},1,4096));wire.total_seconds=163;
    const auto whole_unavailable=[&](const auto& value) {
        require(!value.confidence_available&&!value.one_bit_confidence_available&&!value.reference_probability_available&&
            value.one_bit_reference_available&&!value.probability_interval_available&&value.success_probability==0&&
            value.coherent_success_probability==0&&value.success_probability_low==0&&value.success_probability_high==1&&
            value.one_bit_success_probability==full.one_bit_success_probability,
            "EME first-bit qualification leaked into another draft format");
    };
    whole_unavailable(estimate(o,ch,{},1,4096,Mode::hardware_fallback,false));
    wire.wire_bits=2;whole_unavailable(estimate(o,ch,{},1,4096));wire.wire_bits=1;
    std::cout<<"frequency-axis probability scope checks passed\n";
}
void existing_eme_scope() {
    for(const auto seconds:{128U,256U}) {
        auto o=eme(seconds);o.modem.receiver_strategy=modem::ReceiverStrategy::existing_only;
        const auto ch=channel(o.modem,18,1);
        transfer::Estimate wire;wire.wire_bits=1;wire.total_seconds=seconds+35;
        const auto run=[&](bool raw=true,std::size_t draws=256) {
            return simulation::estimate(wire,o,raw,ch,{},1,true,100,draws,simulation::ReceiverWorkMode::hardware_fallback);
        };
        const auto first=run();
        std::cout<<"existing_eme seconds="<<seconds<<" p="<<first.one_bit_success_probability
            <<" confidence="<<first.one_bit_confidence_available<<" reference="<<first.one_bit_reference_available
            <<" trials="<<first.probability_trials<<'\n'<<std::flush;
        require(first.carrier_in_search&&first.clock_in_search&&first.receiver_workspace_supported&&
            !first.confidence_available&&!first.one_bit_confidence_available&&first.probability_reference_only&&
            first.reference_probability_available&&first.one_bit_reference_available&&!first.probability_interval_available&&
            first.success_probability==first.one_bit_success_probability&&
            first.probability_model_limit.find("second-batch")!=std::string::npos&&
            first.probability_model_limit.find("no frequency-axis qualification")!=std::string::npos,
            "Existing-only EME borrowed confidence instead of reporting its corrected reference");
        const auto whole=[&](const auto& value) {
            require(!value.confidence_available&&!value.one_bit_confidence_available&&!value.reference_probability_available&&
                value.one_bit_reference_available&&!value.probability_interval_available&&value.success_probability==0&&
                value.coherent_success_probability==0&&value.success_probability_low==0&&value.success_probability_high==1&&
                value.one_bit_success_probability==first.one_bit_success_probability,
                "Existing-only EME borrowed a whole-draft continuation probability");
        };
        whole(run(false));wire.wire_bits=2;whole(run());wire.wire_bits=1;
        if(seconds==128) {
            const auto precise=run(true,4096);
            require(!precise.confidence_available&&!precise.one_bit_confidence_available&&precise.one_bit_reference_available,
                "Existing-only EME inherited a qualified frequency-axis anchor");
        }
    }
    std::cout<<"Existing-only EME reference scope checks passed\n";
}
void capture(unsigned seconds,double es,std::uint64_t seed,bool compare,unsigned bits=1,bool noise=false,bool require_success=false,bool hardware=false,bool require_oracle_end=true) {
    auto o=eme(seconds);auto& c=o.modem;auto ch=channel(c,es,seed);
    const auto bank=modem::oscillator_pattern_search(c);
    require(!bank.limited&&bank.hypotheses.size()==(seconds==128?1783:3565),"EME bank coverage changed");
    Bytes expected;for(unsigned i=0;i<bits;++i)expected.push_back(static_cast<std::uint8_t>(bit_draw(seed^0x2f614c5ab7213d89ULL^i)&1));
    if(hardware) {
        require(bits==1,"hardware calibration currently qualifies one bit only");
        // Reference the specified Es/N0 to actual mean transmitted power.
        // Keep channel interpolation and phase diffusion as real impairments.
        ch.snr_db-=10*std::log10(source_power(c,expected.front()));
    }
    modem::StreamingTransmitter tx(modem::RawBits{expected},c,o.dsp_workspace_bytes);
    modem::SampledSimulationChannel sampled(c,ch);std::vector<float> pcm;std::array<float,4096> block{};
    const auto support_end=tx.source_valid_payload_end();
    if(noise) {
        auto count=tx.total_samples()+c.sample_rate*7;
        while(count) {const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(count,block.size()));
            sampled.read_noise(std::span(block).first(n));pcm.insert(pcm.end(),block.begin(),block.begin()+n);count-=n;}
    } else while(const auto n=sampled.read(tx,block))pcm.insert(pcm.end(),block.begin(),block.begin()+n);
    const auto startup=sampled.startup_offset_samples();
    auto tail=modem::pattern_absence_capture_samples(c)+c.sample_rate*(o.search_seconds+2)+
        2*modem::pattern_pulse_padding_samples(c);
    while(tail) {const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(tail,block.size()));
        sampled.read_noise(std::span(block).first(n));pcm.insert(pcm.end(),block.begin(),block.begin()+n);tail-=n;}
    if(diagnostic_capture) {
        diagnostic_pcm(pcm,expected);
        std::cout<<"diagnostic_source startup="<<startup<<" support_end="<<support_end
            <<" symbol_samples="<<modem::symbol_sample_count(c)<<" sample_rate="<<c.sample_rate
            <<" training="<<modem::training_sample_count(c)<<" padding="<<modem::pattern_pulse_padding_samples(c)
            <<" clock_ppm="<<ch.clock_error_ppm<<" frequency_offset="<<ch.frequency_offset_hz
            <<" phase_diffusion="<<ch.phase_noise_degrees_per_sqrt_second<<'\n';
    }
    const auto axis=receive(c,o,pcm,bank,modem::ReceiverStrategy::complex_only);
    const auto check_charge=[&](const Outcome& result,const char* backend) {
        const auto starts=c.sample_rate/750+modem::symbol_sample_count(c)/750/2;
        const auto trials=starts*bank.hypotheses.size();
        const auto threshold=double(-std::log(1e-10L)+2*std::log(static_cast<long double>(trials)+1)+
            std::log(2.L*bank.hypotheses.size()));
        std::cout<<"admission backend="<<backend<<" seconds="<<seconds<<" trials="<<result.first_trials
            <<" retained_starts="<<result.first_starts<<" expected_trials="<<trials<<" threshold="<<result.first_threshold
            <<" expected_threshold="<<threshold<<'\n'<<std::flush;
        require(result.first_trials==trials&&result.first_starts==trials&&std::abs(result.first_threshold-threshold)<1e-9,
            "strong EME acquisition did not use the independently derived second-batch charge");
    };
    if(require_success&&bits==1)check_charge(axis,"frequency-axis");
    if(compare) {
        const auto old=receive(c,o,pcm,bank,modem::ReceiverStrategy::existing_only);
        if(require_success&&bits==1)check_charge(old,"existing");
        require(old.events==axis.events,"FFT backends disagree on pending bits/timing/completion");
    }
    // Derive the last nominal grid endpoint from the transmitter's independent
    // support endpoint. source_valid_payload_end includes trailing pulse padding.
    const auto nominal_end=support_end-modem::pattern_pulse_padding_samples(c)+startup;
    const auto earliest_complete=nominal_end+modem::pattern_absence_samples(c);
    const bool early=axis.complete&&axis.completion_at<earliest_complete;
    // The receiver contract dates absence from its own accepted grid. Public
    // FFT acquisition has no qualified start window; the transmitter oracle
    // remains a stricter timing diagnostic, not an admission lower bound.
    std::uint64_t accepted_end=0;for(const auto& event:axis.events)if(!event.bits.empty())accepted_end=event.end;
    long double fastest=1;for(const auto& h:bank.hypotheses)fastest=std::max(fastest,1+h.clock_error_ppm*1e-6L);
    const auto accepted_bound=accepted_end+static_cast<std::uint64_t>(std::floor(modem::pattern_absence_samples(c)/fastest));
    const bool accepted_early=axis.complete&&(!accepted_end||axis.completion_at<accepted_bound);
    const bool decoded=!noise&&axis.bits==expected;
    const bool accepted_correct=decoded&&axis.complete==1&&!accepted_early;
    const bool correct=decoded&&axis.complete==1&&!early;
    const bool first=!noise&&!axis.bits.empty()&&axis.bits.front()==expected.front();
    std::cout<<"capture seconds="<<seconds<<" es="<<es<<" seed="<<seed<<" bits="<<bits
        <<" hardware="<<hardware<<" noise="<<noise<<" first="<<first<<" correct="<<correct<<" complete="<<axis.complete
        <<" observed="<<axis.bits.size()<<" early="<<early<<" complete_at="<<axis.completion_at
        <<" earliest_complete="<<earliest_complete<<" axis="<<axis.axis<<" tracking="<<axis.tracking
        <<" first_sample="<<(axis.events.empty()?0:axis.events.front().first)<<" accepted_end="<<accepted_end
        <<" accepted_bound="<<accepted_bound<<" accepted_early="<<accepted_early<<" accepted_correct="<<accepted_correct
        <<" first_polled_at="<<(axis.events.empty()?0:axis.events.front().polled_at)<<" first_threshold="<<axis.first_threshold
        <<" first_trials="<<axis.first_trials<<" first_starts="<<axis.first_starts
        <<" paired="<<compare<<'\n'<<std::flush;
    if(noise)require(axis.bits.empty()&&axis.complete==0,"noise-only capture admitted a bit or completed reception");
    if(require_success)require(correct,"strong signal did not complete the exact expected bits");
    require(!accepted_early,"receiver completed before a full failed duration on its accepted symbol grid");
    if(require_oracle_end)require(!early,"receiver completed before independent payload endpoint plus fully scored absence");
    require(axis.axis>0,"fixture did not exercise frequency-axis acquisition");
    if(decoded)require(axis.tracking>0&&axis.complete==1,"correct capture omitted continuation or physical end");
}
}
int main(int argc,char** argv) {try {
    std::cout<<std::setprecision(12);
    if(argc==2&&std::string_view(argv[1])=="--algebra-source-only") {exact_score_algebra();source_projection_checks();return 0;}
    if(argc==2&&std::string_view(argv[1])=="--scope-only") {probability_scope();return 0;}
    if(argc==2&&std::string_view(argv[1])=="--existing-eme-only") {existing_eme_scope();return 0;}
    if(argc==6&&std::string_view(argv[1])=="--diagnose-end") {
        diagnostic_capture=true;std::cout<<std::setprecision(17);
        const auto seconds=std::stoul(argv[2]),bits=std::stoul(argv[5]);
        require((seconds==128||seconds==256)&&bits>=1&&bits<=4,"invalid end diagnostic scope");
        capture(seconds,std::stod(argv[3]),std::stoull(argv[4]),true,bits,false,false,false,false);
        return 0; // Timing diagnostic; neither source-grid alignment nor full-bit success is asserted.
    }
    if(argc==2&&std::string_view(argv[1])=="--strong-controls") {
        capture(128,30,1,true,1,false,true);capture(256,30,2,true,1,false,true);return 0;
    }
    if(argc==5&&std::string_view(argv[1])=="--convergence") {
        convergence(std::stoul(argv[2]),std::stod(argv[3]),std::stoul(argv[4]));return 0;
    }
    if(argc>=2&&std::string_view(argv[1])=="--batch") {
        require(argc==8||argc==9,"usage: --batch seconds EsN0 first-seed count paired bits [hardware]");
        const auto seconds=std::stoul(argv[2]);const auto es=std::stod(argv[3]);
        const auto first=std::stoull(argv[4]);const auto count=std::stoul(argv[5]);
        const bool paired=std::stoul(argv[6]);const auto bits=std::stoul(argv[7]);
        require((seconds==128||seconds==256)&&bits>0&&bits<=4&&count>0,"invalid capture scope");
        const bool hardware=argc==9&&std::stoul(argv[8]);
        prediction(seconds,es,hardware);
        for(std::uint64_t i=0;i<count;++i)capture(seconds,es,first+i,paired,bits,false,false,hardware);
        return 0;
    }
    if(argc==4&&std::string_view(argv[1])=="--noise") {
        const auto seconds=std::stoul(argv[2]);capture(seconds,18,std::stoull(argv[3]),false,1,true);return 0;
    }
    if(argc==4&&std::string_view(argv[1])=="--predict-existing") {
        prediction(std::stoul(argv[2]),std::stod(argv[3]),true,true);return 0;
    }
    if(argc==4&&(std::string_view(argv[1])=="--predict"||std::string_view(argv[1])=="--predict-hardware")) {
        prediction(std::stoul(argv[2]),std::stod(argv[3]),std::string_view(argv[1])=="--predict-hardware");return 0;
    }
    require(argc==1,"unknown frequency-axis probability arguments");
    exact_score_algebra();source_projection_checks();probability_scope();existing_eme_scope();
    prediction(128,20);prediction(256,20);
    capture(128,30,1,true,1,false,true);capture(256,30,2,true,1,false,true);capture(128,30,3,false,3,false,true);
    std::cout<<"frequency-axis probability focused checks passed\n";
}catch(const std::exception& e){std::cerr<<"frequency-axis probability failed: "<<e.what()<<'\n';return 1;}}
