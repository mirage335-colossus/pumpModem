#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/symbol_schedule.hpp"
#include "search_parallel.hpp"
#include "pattern_fft_batch.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>

namespace datapump::modem {
namespace {
using Complex = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;
void cancelled(std::stop_token stop) { if(stop.stop_requested()) throw Error("pattern search cancelled"); }
using detail::pattern_fft;
using detail::pattern_evidence;
std::size_t power_two(std::size_t value) {
    std::size_t result=1;
    while(result<value) {
        if(result>std::numeric_limits<std::size_t>::max()/2)throw Error("pattern transform size overflow");
        result*=2;
    }
    return result;
}
std::vector<double> local_frequency_offsets(const Config& config) {
    const auto step=.25*config.sample_rate/static_cast<double>(symbol_sample_count(config));
    std::vector<double> result{0,-step,step,-2*step,2*step};
    const auto limit=pattern_frequency_offset_limit(config);
    std::erase_if(result,[&](double offset){return std::abs(offset)>limit;});
    return result;
}
}
struct PatternReceiver::Impl {
    Config config;
    PatternSearch search;
    PatternCode code;
    std::unique_ptr<PatternCorrelator> fallback;
    std::size_t budget=0,fixed_reservation=0,configured_bit_limit=0,bin_samples=0,length=0,transform=0,hop=0;
    std::vector<Complex> ring,work,spectrum,product,reference;
    std::vector<double> energy_prefix;
    std::vector<std::array<std::vector<Complex>,2>> templates;
    std::vector<std::array<double,2>> template_energy;
    std::vector<std::array<Complex,2>> template_square;
    struct CachedTemplates {
        std::vector<std::array<std::vector<Complex>,2>> rows;
        std::vector<std::array<double,2>> energy;
        std::vector<std::array<Complex,2>> square;
        bool valid=false;
    };
    std::vector<CachedTemplates> cached_templates;
    std::size_t cache_reservation=0;
    struct ScoringStorage {
        std::vector<detail::FftSearchJob> jobs;
        std::vector<detail::FftPreparedTemplate> templates;
        std::vector<detail::FftSearchScore> outputs;
        std::vector<detail::FftSearchWorkspace> workspaces;
        std::vector<PatternCode> tracking_workers;
        std::size_t tracking_reservation=0;
        std::vector<std::array<Complex,2>> nominal_reference;
    };
    // Exactly one push-scoped owner. Keeping only the original vector-sized
    // handle here preserves the idle footprint visible to the receiver bank.
    std::vector<ScoringStorage> scoring;
    std::size_t scoring_reservation=0;
    std::vector<std::array<Complex,2>> tracking_reference;
    std::uint64_t tracking_reference_index=0;
    std::size_t tracking_reference_frequency=0;
    bool tracking_reference_valid=false;
    std::uint64_t prepared_template_index=0;
    bool templates_valid=false,streamed_templates=false,local_search_fallback=false;
    bool long_symbol=false,required_tracking_reference=false;
    std::uint64_t active_stream_phase=0,phase_step=1,phase_upper=0;
    std::uint64_t sample=0,bins=0,next_start=0;
    Complex sum{},oscillator{1,0},rotation{},previous_chip{};
    double noise_condition=1;
    bool real_rank=false,sample_fit=false,shaped=false;
    std::size_t partial=0;
    bool finished=false,oscillator_valid=true;
    std::uint64_t trials=0;
    std::vector<PatternEvidence> history,peaks;
    struct Completed {std::uint64_t first=0,end=0;double frequency=0;};
    std::vector<Completed> completed;
    std::vector<PatternBurst> bursts;
    std::vector<Complex> points;
    std::size_t point_cursor=0;
    struct Track {
        PatternBurst burst;
        std::uint64_t next=0,index=1,unconfirmed_symbols=0;
        std::uint64_t phase_lower=0,phase_upper=0;
        double total_score=0,penalty=0;
        double pending_score=0,confirmed_score=0;
        double total_support=0,confirmed_support=0;
        std::vector<double> frequency_scores;
        std::size_t confirmed=0,gap_slots=0;
        std::uint64_t confirmed_end=0;
        long double absent_samples=0;
        std::size_t frequency=0;
        bool admitted=false,established=false,pending_gap=false;
    };
    std::vector<Track> tracks;
    PatternBurst latest;

    Impl(Config c,std::size_t bytes,PatternSearch options)
        :config(c),search(std::move(options)),code(c,c.stream_epoch),budget(bytes) {
        validate(c);
        shaped=pattern_pulse_enabled(c);
        if(!c.pattern_symbols)throw Error("pattern receiver requires binary pattern transport");
        if(!std::isfinite(search.false_alarm_probability) || search.false_alarm_probability<=0 || search.false_alarm_probability>=1 ||
           !std::isfinite(search.retain_score) || search.retain_score<0 ||
           !search.candidate_limit || search.candidate_limit>65536 || !search.track_limit || search.track_limit>128 ||
           !search.bit_limit || !search.chunk_bits || !search.initial_stream_symbols || search.initial_stream_symbols>64)
            throw Error("invalid pattern search limits");
        if((search.start_offset_seconds&&!std::isfinite(*search.start_offset_seconds)) ||
           !std::isfinite(search.start_uncertainty_seconds) || search.start_uncertainty_seconds<0)
            throw Error("invalid pattern system-clock window");
        if(search.clock_errors_ppm.empty() || search.clock_errors_ppm.size()>65)
            throw Error("clock-rate bank must contain 1..65 hypotheses");
        for(auto ppm:search.clock_errors_ppm)
            if(!std::isfinite(ppm)||std::abs(ppm)>10000)throw Error("clock-rate hypotheses must fit +/-10000 ppm");
        const bool permit_local_fallback=search.allow_local_clock_fallback && search.expand_clock_search &&
            search.frequency_offsets_hz.empty() && !search.couple_clock_to_carrier &&
            search.clock_errors_ppm==std::vector<double>{0};
        if(search.frequency_offsets_hz.empty()) {
            search.frequency_offsets_hz=search.expand_clock_search?default_pattern_frequency_offsets(c):
                local_frequency_offsets(c);
            search.couple_clock_to_carrier=search.frequency_offsets_hz.size()>5 &&
                c.spreading_mode==SpreadingMode::pattern && search.clock_errors_ppm==std::vector<double>{0};
            if(search.couple_clock_to_carrier) {
                search.uncoupled_frequency_count=search.frequency_offsets_hz.size();
                const auto offsets=search.frequency_offsets_hz;
                search.frequency_offsets_hz.insert(search.frequency_offsets_hz.end(),offsets.begin(),offsets.end());
            }
        }
        if(search.frequency_offsets_hz.size()>2*maximum_pattern_frequency_hypotheses ||
           search.uncoupled_frequency_count>search.frequency_offsets_hz.size())
            throw Error("pattern frequency bank exceeds finite hypothesis limit");
        double maximum_offset=0;
        for(auto offset:search.frequency_offsets_hz) {
            if(!std::isfinite(offset) || std::abs(offset)>pattern_frequency_offset_limit(c))
                throw Error("pattern carrier offset exceeds sampled passband headroom");
            if(search.couple_clock_to_carrier && std::abs(offset/c.carrier_hz)>.01)
                throw Error("coupled clock hypotheses must fit +/-10000 ppm");
            maximum_offset=std::max(maximum_offset,std::abs(offset));
        }
        configured_bit_limit=search.bit_limit;
        if(!c.scramble&&!c.dsss)search.initial_stream_symbols=1;
        if(search.compact_clock_search && !search.couple_clock_to_carrier) {
            const auto wrapper=wrapper_bytes();
            if(wrapper>=bytes)throw Error("pattern workspace cannot retain correlator control state");
            fallback=std::make_unique<PatternCorrelator>(config,search,bytes-wrapper);
            return;
        }
        const auto symbols=code.symbol_samples();
        long_symbol=symbols>=16ULL*c.sample_rate;
        active_stream_phase=c.stream_phase_samples;
        phase_step=std::gcd(symbols,static_cast<std::uint64_t>(c.sample_rate));
        phase_upper=search.search_stream_phases?
            (std::min(symbols,static_cast<std::uint64_t>(c.sample_rate))-1)/phase_step*phase_step:c.stream_phase_samples;
        // Chip and symbol boundaries must fall on bin boundaries. Rounding a
        // symbol to whole bins would accumulate timing drift and count edges
        // of adjacent symbols twice, especially with partial final chips.
        bin_samples=static_cast<std::size_t>(pattern_projection_bin_samples(c,maximum_offset));
        const auto omega=tau*c.carrier_hz/c.sample_rate;
        const auto sine=std::sin(omega);
        const auto bin_image=[&](std::size_t count) {
            return std::abs(sine)>1e-12?std::abs(std::sin(static_cast<double>(count)*omega)/sine):static_cast<double>(count);
        };
        // Preserve compact private bins when their two carrier quadratures
        // are orthogonal: that fit already retains all projected energy.
        // Short nonorthogonal/singular private bins need exact sample fits
        // to avoid a high-SNR confidence ceiling. Public short patterns keep
        // their existing sample-resolution timing behavior.
        if(symbols<=256 && ((!c.scramble && !c.dsss) || bin_image(bin_samples)>1e-10*static_cast<double>(bin_samples)))
            bin_samples=1;
        sample_fit=bin_samples==1 && (symbols<=256 || (!c.scramble && !c.dsss));
        const auto image=bin_image(bin_samples);
        const auto small=static_cast<double>(bin_samples)-image;
        real_rank=small<=1e-10*static_cast<double>(bin_samples);
        if(!real_rank)noise_condition=(static_cast<double>(bin_samples)+image)/small;
        if(symbols/bin_samples>std::numeric_limits<std::size_t>::max()-2)throw Error("pattern integration exceeds address space");
        const auto observation_samples=static_cast<long double>(symbols)/
            (search.couple_clock_to_carrier?1-maximum_offset/c.carrier_hz:1.);
        if(observation_samples/bin_samples>std::numeric_limits<std::size_t>::max()-2)
            throw Error("pattern clock integration exceeds address space");
        length=static_cast<std::size_t>(std::ceil(observation_samples/bin_samples));
        if(length<4)length=4;
        if(length>std::numeric_limits<std::size_t>::max()/4)throw Error("pattern integration exceeds address space");
        const auto nominal_length=static_cast<std::size_t>(symbols/bin_samples+(symbols%bin_samples!=0));
        transform=power_two(2*std::max<std::size_t>(4,nominal_length));
        while(transform<length) {
            if(transform>std::numeric_limits<std::size_t>::max()/2)throw Error("pattern transform size overflow");
            transform*=2;
        }
        if(long_symbol) {
            // A full 2N transform can postpone the first decision until well
            // after both a long bit and its absent successor have arrived.
            // Use a smaller overlap-save transform when it still covers a
            // useful range of new starts; avoid near-zero hops at powers of two.
            // Tracking's five timing fits also need four bins of scratch.
            const auto compact=power_two(length+4);
            if(compact-length+1>=std::max<std::size_t>(4,length/4))
                transform=std::min(transform,compact);
        }
        hop=transform-length+1;
        if(long_symbol)hop=std::min(hop,std::max<std::size_t>(1,nominal_length/2));
        required_tracking_reference=search.couple_clock_to_carrier || transform<2*length;
        // Transform and baseband history allocations are checked before any
        // allocation. The RAM dropdown is a ceiling, not an allocation target.
        const auto arrays=5+2*search.frequency_offsets_hz.size();
        long double required=sizeof(Impl)+static_cast<long double>(transform)*sizeof(Complex)*arrays+
            static_cast<long double>(4*length+2*hop)*sizeof(Complex)+
            (required_tracking_reference?2.L*length*sizeof(Complex):0.L)+
            static_cast<long double>(transform+1)*sizeof(double)+
            static_cast<long double>(2*search.candidate_limit)*sizeof(PatternEvidence)+
            static_cast<long double>(search.track_limit)*(sizeof(Track)+sizeof(PatternBurst)+sizeof(Completed))+
            static_cast<long double>(search.track_limit+2)*search.frequency_offsets_hz.size()*sizeof(double)+
            static_cast<long double>(search.frequency_offsets_hz.size())*sizeof(decltype(templates)::value_type)+
            static_cast<long double>(search.frequency_offsets_hz.size())*sizeof(decltype(template_energy)::value_type)+
            static_cast<long double>(search.frequency_offsets_hz.size())*sizeof(decltype(template_square)::value_type)+
            static_cast<long double>(search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double)+
            4096*sizeof(Complex)+code.working_bytes()+sizeof(PatternReceiver);
        if(required>bytes || (search.prefer_streamed_templates && search.couple_clock_to_carrier)) {
            const auto without_rows=required-2.L*search.frequency_offsets_hz.size()*transform*sizeof(Complex);
            // A wide bank needs every hypothesis, but not every transformed
            // template at once. Generate rows in bounded worker scratch (or
            // the existing serial product buffer) when retaining them all
            // would force an unnecessarily expensive clock-window fallback.
            if(without_rows+2*search.track_limit+2<=bytes) {
                streamed_templates=true;required=without_rows;
            }
        }
        if(required>bytes && search.start_offset_seconds) {
            if(permit_local_fallback && search.couple_clock_to_carrier) {
                search.frequency_offsets_hz=local_frequency_offsets(c);
                search.expand_clock_search=false;search.couple_clock_to_carrier=false;
                search.uncoupled_frequency_count=0;
                const auto wrapper=wrapper_bytes();
                if(wrapper>=bytes)throw Error("pattern workspace cannot retain correlator control state");
                fallback=std::make_unique<PatternCorrelator>(config,search,bytes-wrapper);
                local_search_fallback=true;
                return;
            }
            if(search.couple_clock_to_carrier)
                throw Error("complete carrier/clock competition exceeds FFT workspace; increase the DSP limit");
            const auto wrapper=wrapper_bytes();
            if(wrapper>=bytes)throw Error("pattern workspace cannot retain correlator control state");
            fallback=std::make_unique<PatternCorrelator>(config,search,bytes-wrapper);
            return;
        }
        if(required>bytes)
            throw Error("pattern correlation and timing coverage exceed DSP workspace; reduce integration/bandwidth or increase the limit");
        fixed_reservation=static_cast<std::size_t>(required);
        // Active tracks, queued completions, latest completion and one
        // allocation/copy in flight can each retain an independent bit vector.
        search.bit_limit=std::min(search.bit_limit,(bytes-fixed_reservation)/(2*search.track_limit+2));
        if(!search.bit_limit)throw Error("pattern workspace cannot retain bit candidates");
        ring.resize(4*length+2*hop);work.resize(transform);spectrum.resize(transform);product.resize(transform);reference.resize(transform);
        energy_prefix.resize(transform+1);templates.resize(search.frequency_offsets_hz.size());
        if(required_tracking_reference)tracking_reference.resize(length);
        template_energy.resize(templates.size());
        template_square.resize(templates.size());
        history.reserve(search.candidate_limit);peaks.reserve(search.candidate_limit);completed.reserve(search.track_limit);
        tracks.reserve(search.track_limit);points.reserve(2048);bursts.reserve(search.track_limit);
        rotation=std::polar(1.,-tau*c.carrier_hz/c.sample_rate);
        // Short private acquisition revisits the same bounded initial stream
        // bank every hop. Cache its exact transforms only in spare workspace;
        // payload capacity and search coverage retain their original limits.
        const auto frequencies=search.frequency_offsets_hz.size();
        const long double cache_bytes=static_cast<long double>(search.initial_stream_symbols)*
            (sizeof(CachedTemplates)+static_cast<long double>(frequencies)*
             (sizeof(decltype(templates)::value_type)+sizeof(decltype(template_energy)::value_type)+
              sizeof(decltype(template_square)::value_type)+2*static_cast<long double>(transform)*sizeof(Complex)))+
            static_cast<long double>(length)*sizeof(decltype(tracking_reference)::value_type);
        bool initial_phases_equivalent=true;
        if(search.search_stream_phases && phase_upper)for(std::size_t index=0;index<search.initial_stream_symbols;++index) {
            const auto first=symbol_stream_address(c.stream_epoch,0,index,symbols,c.sample_rate);
            const auto last=symbol_stream_address(c.stream_epoch,phase_upper,index,symbols,c.sample_rate);
            if(first.epoch!=last.epoch || first.ordinal!=last.ordinal){initial_phases_equivalent=false;break;}
        }
        // Distinct sample phases often share every initial template address.
        // Cache those transforms even while later symbols must resolve phase;
        // changing phase cannot change an address proven equal over its range.
        if(!streamed_templates && initial_phases_equivalent && symbols<=256 &&
           search.initial_stream_symbols>1 && cache_bytes<=bytes-fixed_reservation) {
            cache_reservation=static_cast<std::size_t>(std::ceil(cache_bytes));
            cached_templates.resize(search.initial_stream_symbols);
            for(auto& bank:cached_templates) {
                bank.rows.resize(frequencies);bank.energy.resize(frequencies);bank.square.resize(frequencies);
                for(auto& row:bank.rows)for(auto& values:row)values.resize(transform);
            }
            tracking_reference.resize(length);
        }
        stream_phase(search.search_stream_phases?0:c.stream_phase_samples);prepare_templates(0,{});
    }
    void stream_phase(std::uint64_t phase) {
        if(active_stream_phase==phase)return;
        code.set_stream_phase_samples(phase);active_stream_phase=phase;
        templates_valid=false;tracking_reference_valid=false;
    }
    struct PhaseGroup {std::uint64_t lower=0,upper=0;};
    std::array<PhaseGroup,3> phase_groups(std::uint64_t index,std::uint64_t lower,std::uint64_t upper,
                                        std::size_t& count)const {
        std::array<PhaseGroup,3> groups{};count=0;
        while(lower<=upper) {
            const auto address=symbol_stream_address(config.stream_epoch,lower,index,code.symbol_samples(),config.sample_rate);
            auto low=std::uint64_t{0},high=(upper-lower)/phase_step;
            while(low<high) {
                const auto mid=low+(high-low+1)/2;
                const auto candidate=symbol_stream_address(config.stream_epoch,lower+mid*phase_step,index,
                    code.symbol_samples(),config.sample_rate);
                if(candidate.epoch==address.epoch && candidate.ordinal==address.ordinal)low=mid;
                else high=mid-1;
            }
            const auto end=lower+low*phase_step;
            if(count==groups.size())throw Error("pattern phase groups exceed finite symbol interval");
            groups[count++]={lower,end};
            if(end==upper)break;
            lower=end+phase_step;
        }
        return groups;
    }
    void prepare_templates(std::uint64_t index,std::stop_token stop) {
        if(streamed_templates)return;
        const auto cached=!cached_templates.empty();
        if(cached?cached_templates[index].valid:(templates_valid&&prepared_template_index==index))return;
        templates_valid=false;
        auto& rows=cached?cached_templates[index].rows:templates;
        auto& energies=cached?cached_templates[index].energy:template_energy;
        auto& squares=cached?cached_templates[index].square:template_square;
        for(std::size_t f=0;f<templates.size();++f)for(unsigned bit=0;bit<2;++bit) {
            auto& row=rows[f][bit];row.resize(transform);
            std::fill(row.begin(),row.end(),Complex{});
            auto& norm=energies[f][bit];norm=0;
            auto& square=squares[f][bit];square={};
            for(std::size_t i=0;i<length;++i) {
                const auto value=template_value(i,index,bit,f);
                row[length-1-i]=std::conj(value);norm+=std::norm(value);
                if(sample_fit)square+=value*value*carrier_square(i);
            }
            pattern_fft(row,false,stop);
        }
        prepared_template_index=index;templates_valid=true;
        if(cached)cached_templates[index].valid=true;
    }
    void drop_template_cache() {
        std::vector<CachedTemplates>().swap(cached_templates);
        if(!required_tracking_reference)std::vector<std::array<Complex,2>>().swap(tracking_reference);
        templates_valid=false;tracking_reference_valid=false;cache_reservation=0;
    }
    detail::FftSearchGeometry scoring_geometry()const {
        detail::FftSearchGeometry geometry;
        geometry.pattern={config.stream_epoch,code.chip_samples(),code.chips_per_symbol(),code.symbol_samples(),
            config.sample_rate,static_cast<std::uint32_t>(config.spreading_mode),
            shaped?1U:0U,config.scramble?1U:0U,config.dsss?1U:0U,
            config.spreading_seed,config.dsss_seed};
        geometry.bins_per_symbol=length;geometry.bin_samples=bin_samples;
        geometry.carrier_hz=config.carrier_hz;geometry.evidence_count=evidence_count(length);
        geometry.noise_condition=noise_condition;geometry.real_rank=real_rank;geometry.sample_fit=sample_fit;
        geometry.extended_clock_window=search.couple_clock_to_carrier;
        return geometry;
    }
    void prepare_scoring(std::stop_token stop) {
        if(!scoring.empty()) {
            if(!scoring.front().jobs.empty())return;
            // A tracking-only owner is optional scratch from an earlier
            // symbol in this push. Give acquisition its original allowance.
            drop_scoring();
        }
        const auto workers=detail::search_concurrency(search.worker_threads);
        if(workers<2)return;
        const auto jobs=search.initial_stream_symbols*templates.size()*(phase_upper?3:1);
        const bool needs_code=cached_templates.empty() && !reuse_single_template();
        const auto worker_bytes=sizeof(detail::FftSearchWorkspace)+
            (needs_code?code.working_bytes():0)+transform*sizeof(Complex);
        const auto job_bytes=sizeof(detail::FftSearchJob)+sizeof(detail::FftPreparedTemplate)+
            hop*sizeof(detail::FftSearchScore);
        long double retained=static_cast<long double>(fixed_reservation)+cache_reservation+latest.bits.capacity();
        for(const auto& track:tracks)retained+=track.burst.bits.capacity();
        for(const auto& burst:bursts)retained+=burst.bits.capacity();
        if(retained>=budget)return;
        if(budget-static_cast<std::size_t>(retained)<=sizeof(ScoringStorage))return;
        auto spare=budget-static_cast<std::size_t>(retained)-sizeof(ScoringStorage);
        const auto uncached_workers=std::min({workers,jobs,spare/(worker_bytes+job_bytes)});
        bool cache_nominal=false;
        if(long_symbol && streamed_templates && !config.scramble && !config.dsss &&
           config.spreading_mode==SpreadingMode::pattern && uncached_workers>=2) {
            std::size_t nominal_jobs=0;
            for(std::size_t f=0;f<templates.size();++f)if(clock_ratio(f)==1)++nominal_jobs;
            const auto bytes=static_cast<long double>(length)*sizeof(std::array<Complex,2>);
            // Reserve the optional waveform before filling spare memory with
            // FFT job outputs. Keep every affordable physical worker and at
            // least one job per worker; only logical batch capacity may shrink.
            if(nominal_jobs>uncached_workers && bytes<=spare-uncached_workers*(worker_bytes+job_bytes)) {
                spare-=static_cast<std::size_t>(bytes);cache_nominal=true;
            }
        }
        const auto worker_count=std::min({workers,jobs,spare/(worker_bytes+job_bytes)});
        if(worker_count<2)return;
        // Logical batch capacity depends on the search and RAM, not the CPU
        // count. Thousands of jobs can share one dispatch; only CPU-private
        // transform/cache storage is allocated per physical worker slot.
        const auto count=std::min(jobs,(spare-worker_count*worker_bytes)/job_bytes);
        scoring.resize(1);
        auto& storage=scoring.front();
        auto& scoring_jobs=storage.jobs;auto& scoring_templates=storage.templates;
        auto& scoring_outputs=storage.outputs;auto& scoring_workspaces=storage.workspaces;
        if(cache_nominal)storage.nominal_reference.resize(length);
        scoring_jobs.resize(count);scoring_templates.resize(count);
        scoring_outputs.resize(count*hop);scoring_workspaces.reserve(worker_count);
        for(std::size_t i=0;i<worker_count;++i)scoring_workspaces.emplace_back(config,transform,needs_code);
        scoring_reservation=scoring.capacity()*sizeof(ScoringStorage)+scoring_jobs.capacity()*sizeof(detail::FftSearchJob)+
            scoring_templates.capacity()*sizeof(detail::FftPreparedTemplate)+
            scoring_outputs.capacity()*sizeof(detail::FftSearchScore)+
            scoring_workspaces.capacity()*sizeof(detail::FftSearchWorkspace)+
            storage.nominal_reference.capacity()*sizeof(decltype(storage.nominal_reference)::value_type);
        for(const auto& workspace:scoring_workspaces)scoring_reservation+=workspace.working_bytes();
        // A vector implementation may reserve more than requested. Optional
        // batching must still fit its measured capacities on that platform.
        if(!cache_fits(budget,0)){drop_scoring();return;}
        if(cache_nominal)detail::prepare_fft_nominal_reference(scoring_geometry(),code,storage.nominal_reference,stop);
    }
    void drop_scoring() {
        decltype(scoring)().swap(scoring);scoring_reservation=0;
    }
    std::vector<PatternCode>* prepare_tracking(std::size_t jobs) {
        if(!long_symbol || jobs<2)return nullptr;
        const auto workers=std::min(jobs,detail::search_concurrency(search.worker_threads));
        if(workers<2)return nullptr;
        if(!scoring.empty() && !scoring.front().tracking_workers.empty())
            return &scoring.front().tracking_workers;
        const auto worker_bytes=code.working_bytes();
        const auto minimum=2*worker_bytes+(scoring.empty()?sizeof(ScoringStorage):0);
        // FFT jobs have joined before continuation starts. Their optional
        // transform scratch may leave no room even for two small private
        // pattern caches; release it only when that space is needed.
        if(!cache_fits(budget,minimum) && !scoring.empty())drop_scoring();
        long double retained=static_cast<long double>(fixed_reservation)+cache_reservation+
            scoring_reservation+latest.bits.capacity()+(scoring.empty()?sizeof(ScoringStorage):0);
        for(const auto& track:tracks)retained+=track.burst.bits.capacity();
        for(const auto& burst:bursts)retained+=burst.bits.capacity();
        if(retained>=budget)return nullptr;
        const auto count=std::min(workers,(budget-static_cast<std::size_t>(retained))/worker_bytes);
        if(count<2)return nullptr;
        if(scoring.empty()) {
            scoring.resize(1);
            scoring_reservation=scoring.capacity()*sizeof(ScoringStorage);
        }
        auto& storage=scoring.front();
        storage.tracking_workers.reserve(count);
        for(std::size_t i=0;i<count;++i)storage.tracking_workers.emplace_back(config,config.stream_epoch);
        storage.tracking_reservation=storage.tracking_workers.capacity()*sizeof(PatternCode);
        for(const auto& worker:storage.tracking_workers)
            storage.tracking_reservation+=worker.working_bytes()-sizeof(PatternCode);
        scoring_reservation+=storage.tracking_reservation;
        // Include measured capacities in the same optional-cache accounting
        // used by acquisition and payload growth. The push scope releases all
        // worker caches on normal return or cancellation, preserving idle RAM.
        if(!cache_fits(budget,0)){drop_scoring();return nullptr;}
        return &scoring.front().tracking_workers;
    }
    struct ScoringScope {
        Impl& state;
        ~ScoringScope() {state.drop_scoring();}
    };
    bool reuse_single_template() const {
        return !streamed_templates && search.initial_stream_symbols==1 &&
            (!search.search_stream_phases || !phase_upper || (!config.scramble&&!config.dsss));
    }
    bool cache_fits(std::size_t bytes,std::size_t extra)const {
        // Retain the original fixed reservation, including scratch/copy
        // headroom, when deciding whether optional caches can coexist with
        // payload growth. The original uncached bit-capacity proof still holds.
        long double required=static_cast<long double>(fixed_reservation)+cache_reservation+scoring_reservation+extra+latest.bits.capacity();
        for(const auto& track:tracks)required+=track.burst.bits.capacity();
        for(const auto& burst:bursts)required+=burst.bits.capacity();
        return required<=bytes;
    }
    void room_for_bits(std::size_t extra) {
        if(!scoring.empty() && !cache_fits(budget,extra))drop_scoring();
        if(!cached_templates.empty() && !cache_fits(budget,extra))drop_template_cache();
    }
    Complex template_value(std::size_t bin,std::uint64_t index,unsigned bit,std::size_t f) {
        return template_value(code,bin,index,bit,f);
    }
    Complex template_value(PatternCode& pattern,std::size_t bin,std::uint64_t index,unsigned bit,std::size_t f) const {
        const auto sample_position=static_cast<long double>(bin)*bin_samples+static_cast<long double>(bin_samples-1)/2;
        const auto source_position=sample_position*clock_ratio(f);
        if(search.couple_clock_to_carrier && source_position>=code.symbol_samples())return {};
        const auto chip_position=source_position/code.chip_samples();
        const auto local=static_cast<std::uint64_t>(chip_position);
        if(index>(std::numeric_limits<std::uint64_t>::max()-local)/code.chips_per_symbol())throw Error("pattern stream coordinate overflow");
        const auto chip=index*code.chips_per_symbol()+local;
        const auto fraction=static_cast<double>(chip_position-local);
        // The raw observations remain disjoint PCM bins. Match the shaped
        // symbol contribution, without adding a receive filter whose correlated
        // output would incorrectly count as independent noise observations.
        // Adjacent unknown symbols are not used as timing or bit evidence.
        // Public patterns repeat independently of stream index and phase.
        // Reuse an already-budgeted acquisition cache during continuation
        // when present; this path never allocates a cache just for tracking.
        const bool cached=clock_ratio(f)==1 && !scoring.empty() && !scoring.front().nominal_reference.empty();
        const auto value=cached?scoring.front().nominal_reference[bin][bit]:shaped?
            pattern.shaped_value(index*code.chips_per_symbol(),bit,static_cast<double>(source_position)):
            pattern.value(chip,bit,fraction);
        return value*std::polar(1.,tau*search.frequency_offsets_hz[f]*static_cast<double>(sample_position)/config.sample_rate);
    }
    Complex at(std::uint64_t i)const {
        if(i>=bins || bins-i>ring.size())throw Error("pattern observation expired before refinement");
        return ring[static_cast<std::size_t>(i%ring.size())];
    }
    Complex carrier_square(std::uint64_t bin)const {
        const auto phase=std::remainder(2*static_cast<long double>(tau)*config.carrier_hz*
            (static_cast<long double>(bin)*bin_samples+(bin_samples-1)/2.L)/config.sample_rate,
            static_cast<long double>(tau));
        return std::polar(1.,static_cast<double>(phase));
    }
    double evidence_count(std::size_t observed)const {
        const auto count=static_cast<double>(observed);
        // Finer timing must not turn a held wrong-key chip into many fresh
        // random observations. Preserve the former half-chip evidence scale
        // (two complex bins, or four real dimensions, per chip) while fitting
        // the waveform against every available sample.
        return sample_fit?std::min(count,4*count/static_cast<double>(code.chip_samples())):count;
    }
    double clock_ratio(std::size_t frequency)const {
        return search.couple_clock_to_carrier && frequency>=search.uncoupled_frequency_count?
            1+search.frequency_offsets_hz[frequency]/config.carrier_hz:1.;
    }
    std::uint64_t symbol_bins(std::size_t frequency)const {
        if(!search.couple_clock_to_carrier)return length;
        return std::max<std::uint64_t>(1,static_cast<std::uint64_t>(std::llround(
            static_cast<long double>(code.symbol_samples())/bin_samples/
            clock_ratio(frequency))));
    }
    double threshold()const {
        // Alpha-spending-style threshold under the reference noise model.
        // Actual PCM quadrature covariance and adaptive paths need calibration;
        // this tuning parameter is not a certified lifetime false-alarm bound.
        const auto t=static_cast<double>(trials)+1;
        return -std::log(search.false_alarm_probability)+2*std::log(t)+
            std::log(2.*static_cast<double>(templates.size()*search.initial_stream_symbols));
    }
    double continuation_penalty()const {
        // Expanded banks compare both bits, five timing refinements and all
        // carrier/clock alternatives. Do not inherit the old five-bin bound.
        return std::log(search.expand_clock_search && templates.size()>5?
            10.*static_cast<double>(templates.size()):10.);
    }
    void remember(PatternEvidence item) {
        if(item.score<search.retain_score)return;
        item.admission_threshold=threshold();
        if(history.size()==search.candidate_limit)history.erase(history.begin());
        history.push_back(item);
    }
    PatternEvidence measure(std::uint64_t start,std::uint64_t index,std::size_t f,std::uint64_t observed_before,
                            std::span<const Complex> carrier_phases) {
        std::array<Complex,2> dot{},square{};std::array<double,2> norm{};double energy=0;
        // Timing refinements of one stream symbol fit the same templates.
        // Reuse their exact values; only the received window and carrier Gram
        // phase change. Without optional cache space, the idle acquisition
        // product buffer has room for both interleaved reference rows.
        if(!tracking_reference_valid || tracking_reference_index!=index || tracking_reference_frequency!=f) {
            tracking_reference_valid=false;
            for(std::size_t i=0;i<length;++i)for(unsigned b=0;b<2;++b) {
                const auto value=template_value(i,index,b,f);
                if(tracking_reference.empty())product[2*i+b]=value;
                else tracking_reference[i][b]=value;
            }
            tracking_reference_index=index;tracking_reference_frequency=f;tracking_reference_valid=true;
        }
        const auto skip=static_cast<std::size_t>(observed_before>start?std::min<std::uint64_t>(length,observed_before-start):0);
        for(std::size_t i=skip;i<length;++i) {
            const auto value=at(start+i);energy+=std::norm(value);
            const auto carrier=sample_fit?carrier_phases[i]:Complex{};
            for(unsigned b=0;b<2;++b) {
                const auto pattern=tracking_reference.empty()?product[2*i+b]:tracking_reference[i][b];
                dot[b]+=value*std::conj(pattern);norm[b]+=std::norm(pattern);
                if(sample_fit)square[b]+=pattern*pattern*carrier;
            }
        }
        const auto count=evidence_count(length-skip);
        const auto zero=pattern_evidence(dot[0],energy,norm[0],count,noise_condition,real_rank,sample_fit,square[0]),
            one=pattern_evidence(dot[1],energy,norm[1],count,noise_condition,real_rank,sample_fit,square[1]);
        PatternEvidence result{start*bin_samples,(start+length)*bin_samples,index,
            config.carrier_hz+search.frequency_offsets_hz[f],std::max(zero,one),std::min(zero,one),one>zero?1U:0U,active_stream_phase};
        result.frequency_hypothesis=f;return result;
    }
    std::array<double,2> measure_streamed(PatternCode& pattern,std::uint64_t start,std::uint64_t index,
                                        std::size_t f,std::uint64_t observed_before,
                                        std::span<const Complex> carrier_phases,std::stop_token stop)const {
        std::array<Complex,2> dot{},square{};std::array<double,2> norm{};double energy=0;
        const auto skip=static_cast<std::size_t>(observed_before>start?std::min<std::uint64_t>(length,observed_before-start):0);
        // Preserve measure()'s sample/bit accumulation order, but generate a
        // template pair immediately before using it. Each worker owns only a
        // bounded PatternCode cache, not a symbol-sized reference or FFT row.
        for(std::size_t i=skip;i<length;++i) {
            if((i&4095U)==0)cancelled(stop);
            const auto value=at(start+i);energy+=std::norm(value);
            const auto carrier=sample_fit?carrier_phases[i]:Complex{};
            for(unsigned b=0;b<2;++b) {
                const auto reference_value=template_value(pattern,i,index,b,f);
                dot[b]+=value*std::conj(reference_value);norm[b]+=std::norm(reference_value);
                if(sample_fit)square[b]+=reference_value*reference_value*carrier;
            }
        }
        const auto count=evidence_count(length-skip);
        return {pattern_evidence(dot[0],energy,norm[0],count,noise_condition,real_rank,sample_fit,square[0]),
                pattern_evidence(dot[1],energy,norm[1],count,noise_condition,real_rank,sample_fit,square[1])};
    }
    void publish(Track& track,bool complete,bool flush=false,bool draining=false) {
        track.burst.score=track.confirmed_score;
        track.burst.support_samples=track.confirmed_support;
        if(!track.established)return;
        const auto chunk=std::min(search.chunk_bits,search.bit_limit);
        if(!complete && !flush && track.confirmed<chunk)return;
        do {
            const auto count=std::min(track.confirmed,chunk);
            if(!count && !complete)break;
            if(draining && bursts.size()==search.track_limit)break;
            if(bursts.size()>=search.track_limit)throw Error("pattern output queue requires draining");
            PatternBurst event;
            event.first_sample=track.burst.first_sample;event.first_stream_symbol=track.burst.first_stream_symbol;
            event.stream_first_sample=track.burst.stream_first_sample;event.stream_first_symbol=track.burst.stream_first_symbol;
            event.frequency_hz=track.burst.frequency_hz;event.stream_phase_samples=track.burst.stream_phase_samples;
            event.bits.assign(track.burst.bits.begin(),track.burst.bits.begin()+static_cast<std::ptrdiff_t>(count));
            event.complete=complete && count==track.confirmed;
            event.end_sample=count==track.confirmed?track.confirmed_end:
                event.first_sample+count*code.symbol_samples();
            event.score=track.confirmed_score;
            event.support_samples=track.confirmed_support;
            room_for_bits(3*event.bits.capacity());latest=event;
            if(count) {
                if(completed.size()==search.track_limit)completed.erase(completed.begin());
                completed.push_back({event.first_sample,event.end_sample,event.frequency_hz});
            }
            bursts.push_back(std::move(event));
            track.burst.bits.erase(track.burst.bits.begin(),track.burst.bits.begin()+static_cast<std::ptrdiff_t>(count));
            track.burst.first_stream_symbol+=count;track.burst.first_sample=bursts.back().end_sample;
            track.confirmed-=count;
            if(!track.confirmed)break;
        } while(complete || flush || track.confirmed>=chunk);
    }
    void emit_gap(Track& track,std::uint64_t resumed_sample,std::uint64_t resumed_symbol) {
        if(!track.gap_slots)return;
        publish(track,false,true);
        if(bursts.size()>=search.track_limit)throw Error("pattern output queue requires draining");
        PatternBurst event;
        event.first_sample=track.burst.first_sample;event.end_sample=resumed_sample;
        event.first_stream_symbol=track.burst.first_stream_symbol;
        event.stream_first_sample=track.burst.stream_first_sample;event.stream_first_symbol=track.burst.stream_first_symbol;
        event.frequency_hz=track.burst.frequency_hz;event.score=track.confirmed_score;
        event.support_samples=track.confirmed_support;
        event.stream_phase_samples=track.burst.stream_phase_samples;event.missing_slots=track.gap_slots;
        bursts.push_back(std::move(event));track.gap_slots=0;
        track.burst.first_sample=resumed_sample;track.burst.first_stream_symbol=resumed_symbol;
    }
    void append_bit(Bytes& bits,std::uint8_t bit) {
        if(bits.size()==search.bit_limit)throw Error("pattern burst exceeds bounded bit capacity");
        if(bits.size()==bits.capacity()) {
            const auto capacity=std::max<std::size_t>(1,bits.size()>search.bit_limit/2?search.bit_limit:2*bits.size());
            room_for_bits(capacity);bits.reserve(capacity);
        }
        bits.push_back(bit);
    }
    void continue_tracks(std::stop_token stop,bool final=false) {
        for(auto it=tracks.begin();it!=tracks.end();) {
            cancelled(stop);auto& track=*it;bool ended=false;
            while(track.next+length+(final?0:2)<=bins) {
                PatternEvidence best;best.score=-1;std::uint64_t chosen=track.next;
                std::size_t group_count=0;
                const auto groups=phase_groups(track.index,track.phase_lower,track.phase_upper,group_count);
                PhaseGroup selected{track.phase_lower,track.phase_upper};
                const auto low=track.next>2?track.next-2:0;
                const auto high=std::min(track.next+2,bins-length);
                // Acquisition is finished while tracks run. Its work buffer
                // fits all five overlapping timing windows, whose exact
                // carrier Gram phases also repeat across frequency fits.
                if(sample_fit)for(std::size_t i=0;i<length+high-low;++i)work[i]=carrier_square(low+i);
                const auto carrier_phases=[&](std::uint64_t start) {
                    return sample_fit?std::span<const Complex>(work).subspan(static_cast<std::size_t>(start-low),length):
                        std::span<const Complex>{};
                };
                for(std::size_t g=0;g<group_count;++g) {
                    stream_phase(groups[g].lower);
                    for(auto start=low;start<=high;++start) {
                        if(bins-start>ring.size())continue;
                        auto fit=measure(start,track.index,track.frequency,track.burst.end_sample/bin_samples,
                            carrier_phases(start));++trials;
                        if(fit.score>best.score){best=fit;chosen=start;selected=groups[g];}
                    }
                }
                // Acquisition can select a neighboring carrier from one
                // distorted startup symbol. Compare the same disjoint PCM
                // against the finite carrier bank as reception continues;
                // frequency is evidence accumulated over the stream, not a
                // permanent label inherited from its first admitted peak.
                std::vector<std::array<double,2>> frequency_scores(templates.size());
                stream_phase(selected.lower);
                const auto timing_fit=best;
                auto selected_frequency=track.frequency;
                const auto eligible=[&](std::size_t f) {
                    return track.established || std::abs(search.frequency_offsets_hz[f]-search.frequency_offsets_hz[track.frequency])<=
                        static_cast<double>(config.sample_rate)/static_cast<double>(code.symbol_samples());
                };
                std::size_t jobs=0;
                for(std::size_t f=0;f<templates.size();++f)if(f!=track.frequency && eligible(f))++jobs;
                auto* workers=prepare_tracking(jobs);
                if(workers) {
                    // Only disjoint score slots and private pattern caches are
                    // writable while the ring and carrier phases are shared.
                    // All trials, ties and admission stay in the serial loop.
                    detail::parallel_search_ranges(templates.size(),workers->size(),1,
                        [&](std::size_t worker,std::size_t begin,std::size_t end) {
                            auto& pattern=(*workers)[worker];pattern.set_stream_phase_samples(selected.lower);
                            for(auto f=begin;f<end;++f) {
                                cancelled(stop);
                                if(f==track.frequency || !eligible(f))continue;
                                frequency_scores[f]=measure_streamed(pattern,chosen,track.index,f,
                                    track.burst.end_sample/bin_samples,carrier_phases(chosen),stop);
                            }
                        });
                }
                for(std::size_t f=0;f<templates.size();++f) {
                    if(!eligible(f))continue;
                    auto fit=timing_fit;
                    if(f!=track.frequency) {
                        if(workers) {
                            const auto zero=frequency_scores[f][0],one=frequency_scores[f][1];
                            fit={chosen*bin_samples,(chosen+length)*bin_samples,track.index,
                                config.carrier_hz+search.frequency_offsets_hz[f],std::max(zero,one),std::min(zero,one),
                                one>zero?1U:0U,active_stream_phase};
                            fit.frequency_hypothesis=f;
                        } else fit=measure(chosen,track.index,f,track.burst.end_sample/bin_samples,carrier_phases(chosen));
                    }
                    if(f!=track.frequency)++trials;
                    frequency_scores[f][fit.bit]=fit.score;
                    frequency_scores[f][1-fit.bit]=fit.alternative_score;
                    if(fit.score>best.score){best=fit;selected_frequency=f;}
                }
                // A weak candidate may migrate onto an already established
                // carrier during continuation. Apply overlap arbitration here
                // as well as at acquisition, before it can publish a duplicate
                // suffix of the same physical stream.
                if(!track.established) {
                    const auto same_frequency=[&](double frequency) {
                        return search.couple_clock_to_carrier || std::abs(best.frequency_hz-frequency)<=
                            static_cast<double>(config.sample_rate)/static_cast<double>(code.symbol_samples());
                    };
                    const auto duplicate=std::any_of(tracks.begin(),tracks.end(),[&](const Track& other) {
                        return &other!=&track && other.established && !other.pending_gap && !other.unconfirmed_symbols &&
                            same_frequency(other.burst.frequency_hz) &&
                            track.burst.stream_first_sample>=other.burst.stream_first_sample &&
                            best.first_sample<=other.next*bin_samples+2*bin_samples &&
                            best.end_sample>other.burst.stream_first_sample;
                    }) || std::any_of(completed.begin(),completed.end(),[&](const Completed& span) {
                        return same_frequency(span.frequency) && track.burst.stream_first_sample>=span.first &&
                            best.first_sample<span.end && best.end_sample>span.first;
                    });
                    if(duplicate){ended=true;break;}
                }
                remember(best);
                const auto observed_begin=std::max(best.first_sample,track.burst.end_sample);
                const auto symbol_support=pattern_symbol_support(
                    static_cast<double>(best.end_sample>observed_begin?best.end_sample-observed_begin:0),
                    best.score,code.chip_samples());
                const auto standalone=best.score>=threshold();
                if(!track.unconfirmed_symbols)track.absent_samples=0;
                track.absent_samples+=static_cast<long double>(code.symbol_samples())/clock_ratio(track.frequency);
                ++track.unconfirmed_symbols;
                const auto gap_expired=search.couple_clock_to_carrier?
                    track.absent_samples>=pattern_absence_seconds*config.sample_rate:
                    static_cast<long double>(track.unconfirmed_symbols)*code.symbol_samples()>=
                        static_cast<long double>(pattern_absence_seconds)*config.sample_rate;
                if(best.score<search.retain_score || best.score-best.alternative_score<1 || (group_count>1 && !standalone) ||
                   (track.pending_gap && !standalone)) {
                    if(track.admitted && !gap_expired) {
                        if(!track.pending_gap) {
                            track.gap_slots=track.burst.bits.size()-track.confirmed;
                            track.burst.bits.resize(track.confirmed);
                        }
                        if(track.gap_slots==std::numeric_limits<std::size_t>::max())throw Error("pattern missing-slot count overflow");
                        ++track.gap_slots;
                        track.pending_gap=true;track.total_score=track.confirmed_score;
                        track.total_support=track.confirmed_support;
                        track.pending_score=track.penalty=0;
                        ++track.index;track.next+=symbol_bins(track.frequency);
                        continue;
                    }
                    publish(track,gap_expired,true);
                    if(!track.established || gap_expired){ended=true;break;}
                    // A lost symbol closes this contiguous span, but timing
                    // and the private stream advance without requiring it to
                    // decode. Do not refine timing from an obscured pattern.
                    track.burst.bits.clear();track.burst.complete=false;
                    track.admitted=false;track.confirmed=0;track.pending_gap=false;track.gap_slots=0;
                    track.total_score=track.pending_score=track.confirmed_score=track.penalty=0;
                    track.total_support=track.confirmed_support=0;
                    ++track.index;track.next+=symbol_bins(track.frequency);
                    continue;
                }
                if(standalone && !track.pending_gap && track.confirmed<track.burst.bits.size()) {
                    // Keep a pending tail when the combined evidence meets
                    // its own chain threshold. Standalone confidence alone
                    // must not confirm a tail whose joint bound still fails.
                    const auto n=static_cast<double>(track.burst.bits.size()-track.confirmed+1);
                    const auto score=track.pending_score+best.score;
                    const auto bound=score>n?score-n-n*std::log(score/n)-track.penalty-continuation_penalty():0;
                    if(bound<threshold()) {
                        if(track.admitted) {
                            track.gap_slots=track.burst.bits.size()-track.confirmed;track.burst.bits.resize(track.confirmed);
                            track.pending_gap=true;
                            track.total_score=track.confirmed_score;track.pending_score=track.penalty=0;
                            track.total_support=track.confirmed_support;
                        } else {
                            publish(track,false,true);track.burst.bits.clear();
                            track.burst.complete=false;track.admitted=false;
                        }
                    }
                }
                if(standalone) {
                    emit_gap(track,best.first_sample,best.stream_symbol);
                    track.phase_lower=selected.lower;track.phase_upper=selected.upper;
                    track.burst.stream_phase_samples=selected.lower;
                }
                if(track.burst.bits.empty() || (!track.admitted && standalone)) {
                    // A confident new start cannot lend its evidence to an
                    // earlier unconfirmed noise candidate. Keep weak chains
                    // that already established their own confidence.
                    track.burst.bits.clear();track.burst.first_sample=best.first_sample;
                    track.burst.first_stream_symbol=best.stream_symbol;
                    if(!track.admitted) {
                        track.burst.stream_first_sample=best.first_sample;
                        track.burst.stream_first_symbol=best.stream_symbol;
                        track.total_score=track.pending_score=track.penalty=0;
                        std::fill(track.frequency_scores.begin(),track.frequency_scores.end(),0.);
                        track.confirmed=0;track.confirmed_score=0;
                        track.total_support=track.confirmed_support=0;
                    }
                }
                if(track.burst.bits.size()==search.bit_limit && track.admitted && !standalone) {
                    track.gap_slots=track.burst.bits.size()-track.confirmed+1;track.burst.bits.resize(track.confirmed);
                    track.pending_gap=true;track.pending_score=track.penalty=0;track.total_score=track.confirmed_score;
                    track.total_support=track.confirmed_support;
                    ++track.index;track.next+=symbol_bins(track.frequency);continue;
                }
                append_bit(track.burst.bits,static_cast<std::uint8_t>(best.bit));track.burst.end_sample=best.end_sample;
                track.total_score+=best.score;track.pending_score+=best.score;track.penalty+=continuation_penalty();
                track.total_support+=symbol_support;
                const auto count=static_cast<double>(track.burst.bits.size()-track.confirmed);
                // Chernoff bound for the sum of independent Exp(1) null scores,
                // with a union penalty for bit/timing alternatives. Overlapping
                // bins at the start of this window were excluded by measure,
                // so an earlier timing correction never counts evidence twice.
                const auto bound=track.pending_score>count?track.pending_score-count-count*std::log(track.pending_score/count)-track.penalty:0;
                if(bound>=threshold() || standalone) {
                    for(std::size_t f=0;f<templates.size();++f)
                        track.frequency_scores[f]+=frequency_scores[f][best.bit];
                    const auto winner=static_cast<std::size_t>(std::max_element(track.frequency_scores.begin(),
                        track.frequency_scores.begin()+static_cast<std::ptrdiff_t>(templates.size()))-track.frequency_scores.begin());
                    track.burst.frequency_hz=config.carrier_hz+search.frequency_offsets_hz[winner];
                    track.frequency=selected_frequency;
                    track.admitted=track.established=true;track.unconfirmed_symbols=0;
                    track.pending_gap=false;
                    track.confirmed=track.burst.bits.size();track.confirmed_end=best.end_sample;
                    track.confirmed_score=track.total_score;track.pending_score=0;track.penalty=0;
                    track.confirmed_support=track.total_support;
                }
                ++track.index;
                track.next=chosen+symbol_bins(track.frequency);
                publish(track,false);
                if(gap_expired && track.unconfirmed_symbols){publish(track,true,true);ended=true;break;}
            }
            if(final && !ended) { publish(track,false,true);ended=true; }
            if(ended)it=tracks.erase(it);else ++it;
        }
    }
    void admit(const PatternEvidence& item,std::size_t f) {
        if(item.score<search.retain_score || item.score-item.alternative_score<1)return;
        remember(item);
        if(search.search_stream_phases && item.score<threshold()) {
            std::size_t group_count=0;
            phase_groups(item.stream_symbol,0,phase_upper,group_count);
            // A low-confidence start cannot select one cryptographic phase
            // and later borrow a different symbol's evidence to confirm it.
            if(group_count>1)return;
        }
        const auto same_frequency=[&](double frequency) {
            return search.couple_clock_to_carrier ||
                std::abs(item.frequency_hz-frequency)<=static_cast<double>(config.sample_rate)/static_cast<double>(code.symbol_samples());
        };
        for(const auto& span:completed)
            if(same_frequency(span.frequency)&&item.first_sample<span.end&&item.end_sample>span.first)return;
        for(auto it=tracks.begin();it!=tracks.end();) {
            const auto& track=*it;
            const auto margin=2*bin_samples;
            if(same_frequency(track.burst.frequency_hz)&&item.first_sample+margin>=track.burst.stream_first_sample && item.first_sample<=track.next*bin_samples+margin) {
                // A weak fresh noise candidate must not displace timing that
                // already decoded a symbol and is crossing a bounded gap.
                if(track.established && !track.admitted && item.score<threshold())return;
                if(track.admitted && item.first_sample+margin<track.confirmed_end &&
                   item.score>track.total_score && item.score>=threshold()) {
                    // Admission is not a permanent timing lock. A stronger
                    // overlapping pattern replaces the weaker hypothesis;
                    // allow normal timing overlap between adjacent symbols.
                    it=tracks.erase(it);continue;
                }
                // A fresh candidate on the retained gap's clock can await
                // normal continuation. Independently timed starts must still
                // be allowed to replace an obscured old track.
                bool follows_gap=false;
                if(track.pending_gap) {
                    const auto next=track.next*bin_samples,symbol=code.symbol_samples();
                    const auto distance=next>item.first_sample?next-item.first_sample:item.first_sample-next;
                    const auto slots=distance/symbol+(distance%symbol>symbol/2);
                    const auto residual=distance%symbol;
                    const auto same_clock=std::min(residual,symbol-residual)<=margin;
                    const auto same_index=(!config.scramble && !config.dsss) ||
                        (next>=item.first_sample && slots<=track.index && item.stream_symbol==track.index-slots) ||
                        (next<item.first_sample && slots<=std::numeric_limits<std::uint64_t>::max()-track.index &&
                         item.stream_symbol==track.index+slots);
                    follows_gap=same_clock && same_index;
                }
                if(track.admitted && !follows_gap && (track.pending_gap || track.confirmed<track.burst.bits.size()) &&
                   item.first_sample>=track.confirmed_end && item.score>=threshold()) {
                    // Confidence belongs only to the confirmed span. Weak
                    // pending extensions cannot veto an independently strong
                    // later start; finish the old span and consider this one.
                    publish(*it,false,true);it=tracks.erase(it);continue;
                }
                if(track.admitted||track.total_score>=item.score)return;
                it=tracks.erase(it);
            } else ++it;
        }
        if(tracks.size()==search.track_limit) {
            const auto worst=std::min_element(tracks.begin(),tracks.end(),[](const auto& a,const auto& b){
                if(a.established!=b.established)return !a.established;
                return a.total_score<b.total_score;
            });
            if(worst->established || worst->total_score>=item.score)return;
            tracks.erase(worst);
        }
        Track track;track.frequency_scores.resize(templates.size());
        append_bit(track.burst.bits,static_cast<std::uint8_t>(item.bit));
        track.burst.first_sample=item.first_sample;track.burst.end_sample=item.end_sample;track.burst.frequency_hz=item.frequency_hz;
        track.burst.first_stream_symbol=item.stream_symbol;track.index=item.stream_symbol+1;
        track.burst.stream_first_sample=item.first_sample;track.burst.stream_first_symbol=item.stream_symbol;
        track.phase_lower=track.phase_upper=config.stream_phase_samples;
        if(search.search_stream_phases) {
            std::size_t group_count=0;
            const auto groups=phase_groups(item.stream_symbol,0,phase_upper,group_count);
            for(std::size_t g=0;g<group_count;++g)if(item.stream_phase_samples>=groups[g].lower && item.stream_phase_samples<=groups[g].upper) {
                track.phase_lower=groups[g].lower;track.phase_upper=groups[g].upper;break;
            }
        }
        track.burst.stream_phase_samples=track.phase_lower;
        track.next=item.first_sample/bin_samples+symbol_bins(f);track.frequency=f;track.total_score=item.score;track.penalty=std::log(2.);
        track.total_support=pattern_symbol_support(static_cast<double>(item.end_sample-item.first_sample),item.score,code.chip_samples());
        track.frequency_scores[f]=item.score;
        track.admitted=track.established=item.score>=threshold();
        if(track.admitted){track.confirmed=1;track.confirmed_end=item.end_sample;track.confirmed_score=item.score;track.penalty=0;
            track.confirmed_support=track.total_support;}
        else track.pending_score=item.score;
        tracks.push_back(std::move(track));publish(tracks.back(),false);
    }
    void collect_score(double a,double b,std::size_t j,std::uint64_t index,std::uint64_t phase,std::size_t f) {
        ++trials;
        if(std::max(a,b)<search.retain_score)return;
        PatternEvidence item{(next_start+j)*bin_samples,(next_start+j+length)*bin_samples,index,
            config.carrier_hz+search.frequency_offsets_hz[f],std::max(a,b),std::min(a,b),b>a?1U:0U,phase};
        item.frequency_hypothesis=f;
        const auto existing=std::find_if(peaks.begin(),peaks.end(),[&](const auto& p){
            const auto distance=p.first_sample>item.first_sample?p.first_sample-item.first_sample:item.first_sample-p.first_sample;
            return distance<std::max<std::uint64_t>(1,code.symbol_samples()/2) &&
                (search.couple_clock_to_carrier ||
                 std::abs(p.frequency_hz-item.frequency_hz)<=static_cast<double>(config.sample_rate)/static_cast<double>(code.symbol_samples()));
        });
        if(existing!=peaks.end()) { if(item.score>existing->score)*existing=item; }
        else if(peaks.size()<search.candidate_limit)peaks.push_back(item);
    }
    void collect_scores(std::span<const Complex> scores,std::uint64_t index,std::uint64_t phase,std::size_t f) {
        for(std::size_t j=0;j<scores.size();++j)
            collect_score(scores[j].real(),scores[j].imag(),j,index,phase,f);
    }
    void score_parallel(std::size_t count,std::stop_token stop) {
        auto& storage=scoring.front();
        auto& scoring_jobs=storage.jobs;auto& scoring_templates=storage.templates;
        auto& scoring_outputs=storage.outputs;auto& scoring_workspaces=storage.workspaces;
        std::size_t queued=0;
        const bool reuse_templates=reuse_single_template();
        // Cache eviction originally materialized these ordinary transform
        // rows on the next hop. Keep the same retained footprint for shared
        // receiver-bank accounting even when workers generate private rows.
        if(!streamed_templates && cached_templates.empty() && !reuse_templates)
            for(auto& row:templates)for(auto& values:row)values.resize(transform);
        detail::FftSearchBatch batch;
        batch.geometry=scoring_geometry();
        batch.nominal_reference=storage.nominal_reference;
        batch.spectrum=spectrum;batch.carrier_square=work;batch.energy_prefix=energy_prefix;
        batch.starts=count;batch.score_stride=hop;
        const auto flush=[&] {
            batch.prepared=std::span<const detail::FftPreparedTemplate>(scoring_templates).first(queued);
            detail::execute_fft_search_cpu(batch,std::span<const detail::FftSearchJob>(scoring_jobs).first(queued),
                std::span(scoring_outputs).first(queued*hop),scoring_workspaces,stop);
            // Trials, tie-breaking and peak replacement use the original
            // stream/phase/frequency/start order regardless of backend order.
            for(std::size_t job=0;job<queued;++job) {
                const auto& descriptor=scoring_jobs[job];
                for(std::size_t start=0;start<count;++start) {
                    const auto& score=scoring_outputs[job*hop+start];
                    collect_score(score.zero,score.one,start,descriptor.symbol,descriptor.phase,
                                  static_cast<std::size_t>(descriptor.frequency_index));
                }
            }
            queued=0;
        };
        for(std::size_t index=0;index<search.initial_stream_symbols;++index) {
            std::size_t group_count=0;
            const auto groups=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,group_count);
            for(std::size_t g=0;g<group_count;++g) {
                stream_phase(groups[g].lower);
                if(!cached_templates.empty() || reuse_templates)prepare_templates(index,stop);
                for(std::size_t f=0;f<templates.size();++f) {
                    auto& job=scoring_jobs[queued];
                    job={index,groups[g].lower,f,search.frequency_offsets_hz[f]};
                    job.clock_ratio=clock_ratio(f);
                    if(!cached_templates.empty() || reuse_templates) {
                        const auto& rows=cached_templates.empty()?templates:cached_templates[index].rows;
                        const auto& energies=cached_templates.empty()?template_energy:cached_templates[index].energy;
                        const auto& squares=cached_templates.empty()?template_square:cached_templates[index].square;
                        scoring_templates[queued]={{rows[f][0],rows[f][1]},energies[f],squares[f]};
                        job.prepared_template=queued;
                    }
                    if(++queued==scoring_jobs.size())flush();
                }
            }
        }
        if(queued)flush();
    }
    void process(std::stop_token stop,bool final=false) {
        // The first long-symbol pass needs only a small range of fully
        // observed starts. Later passes retain efficient bounded batches.
        // Start coordinates, rather than polling/chunk boundaries, schedule
        // acquisition, and each start still competes across the entire bank.
        const auto next_count=[&] {
            return long_symbol && next_start==0?
                std::min(hop,std::max<std::size_t>(1,config.sample_rate/bin_samples)):hop;
        };
        while(bins>=length+next_start && (final || bins-length+1-next_start>=next_count())) {
            cancelled(stop);const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(hop,bins-length+1-next_start));
            // Drained decisions already veto overlapping acquisition in
            // admit(). If one retained span rejects this entire batch for
            // every carrier, its transforms cannot yield an admissible peak.
            // Keep charging the same trials before continuation; only the
            // diagnostic history of these rejected overlaps is omitted.
            // Ready continuation can rotate the retained span list before
            // admission, so let such batches follow the ordinary path.
            const auto stable_spans=std::none_of(tracks.begin(),tracks.end(),[&](const Track& track) {
                return track.next+length+2<=bins;
            });
            const auto covered=long_symbol && stable_spans && std::any_of(completed.begin(),completed.end(),[&](const Completed& span) {
                if((next_start+count-1)*bin_samples>=span.end ||
                   (next_start+length)*bin_samples<=span.first)return false;
                return search.couple_clock_to_carrier ||
                    std::all_of(search.frequency_offsets_hz.begin(),search.frequency_offsets_hz.end(),[&](double offset) {
                        return std::abs(config.carrier_hz+offset-span.frequency)<=
                            static_cast<double>(config.sample_rate)/static_cast<double>(code.symbol_samples());
                    });
            });
            if(covered) {
                for(std::size_t index=0;index<search.initial_stream_symbols;++index) {
                    std::size_t groups=0;
                    (void)phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,groups);
                    trials+=static_cast<std::uint64_t>(count)*templates.size()*groups;
                }
                next_start+=count;continue_tracks(stop);continue;
            }
            prepare_scoring(stop);
            std::fill(work.begin(),work.end(),Complex{});energy_prefix[0]=0;
            for(std::size_t i=0;i<count+length-1;++i) {
                work[i]=at(next_start+i);energy_prefix[i+1]=energy_prefix[i]+std::norm(work[i]);
            }
            spectrum=work;pattern_fft(spectrum,false,stop);
            // The input transform no longer needs work. Reuse it for the
            // identical carrier Gram phase shared by every bit/frequency/
            // stream hypothesis at a start, without new allocations or a
            // phase recurrence that would change the numerical calculation.
            if(sample_fit)for(std::size_t j=0;j<count;++j)work[j]=carrier_square(next_start+j);
            peaks.clear();
            if(!scoring.empty())score_parallel(count,stop);
            else {
            // Serial acquisition reuses the fallback tracking-cache buffer.
            // A later measure must rebuild it before any timing refinement.
            if(tracking_reference.empty())tracking_reference_valid=false;
            for(std::size_t stream_index=0;stream_index<search.initial_stream_symbols;++stream_index) {
            std::size_t group_count=0;
            const auto groups=phase_groups(stream_index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,group_count);
            for(std::size_t g=0;g<group_count;++g) {
            stream_phase(groups[g].lower);
            prepare_templates(stream_index,stop);
            const auto& rows=cached_templates.empty()?templates:cached_templates[stream_index].rows;
            const auto& energies=cached_templates.empty()?template_energy:cached_templates[stream_index].energy;
            const auto& squares=cached_templates.empty()?template_square:cached_templates[stream_index].square;
            for(std::size_t f=0;f<templates.size();++f) {
                for(unsigned b=0;b<2;++b) {
                    auto norm=energies[f][b];auto square=squares[f][b];
                    if(streamed_templates) {
                        std::fill(product.begin(),product.end(),Complex{});norm=0;square={};
                        for(std::size_t i=0;i<length;++i) {
                            const auto value=template_value(i,stream_index,b,f);
                            product[length-1-i]=std::conj(value);norm+=std::norm(value);
                            if(sample_fit)square+=value*value*carrier_square(i);
                        }
                        pattern_fft(product,false,stop);
                        for(std::size_t i=0;i<transform;++i)product[i]=spectrum[i]*product[i];
                    } else for(std::size_t i=0;i<transform;++i)product[i]=spectrum[i]*rows[f][b][i];
                    pattern_fft(product,true,stop);
                    for(std::size_t j=0;j<count;++j) {
                        const auto score=pattern_evidence(product[length-1+j],energy_prefix[j+length]-energy_prefix[j],
                            norm,evidence_count(length),noise_condition,real_rank,
                            sample_fit,sample_fit?square*work[j]:Complex{});
                        if(b==0)reference[j]={score,0};else reference[j].imag(score);
                    }
                }
                collect_scores(std::span(reference).first(count),stream_index,groups[g].lower,f);
            }
            }
            }
            }
            std::sort(peaks.begin(),peaks.end(),[](const auto& a,const auto& b){return a.first_sample<b.first_sample;});
            for(const auto& peak:peaks) {
                // Existing tracks must consume available observations before
                // overlap checks: an FFT hop can expose a later symbol while
                // its established track still points to the preceding one.
                continue_tracks(stop);
                admit(peak,peak.frequency_hypothesis);
            }
            next_start+=count;continue_tracks(stop);
        }
        if(final)continue_tracks(stop,true);
    }
    void bin(Complex value,std::stop_token stop) {
        ring[static_cast<std::size_t>(bins%ring.size())]=value;++bins;
        const auto point=std::abs(previous_chip)>1e-20?value*std::conj(previous_chip)/std::abs(previous_chip):value;
        if(points.size()==2048){points[point_cursor]=point;point_cursor=(point_cursor+1)%points.size();}
        else points.push_back(point);
        previous_chip=value;
        process(stop);
    }
    std::vector<Complex> chip_points()const {
        std::vector<Complex> result;result.reserve(points.size());
        for(std::size_t i=0;i<points.size();++i)result.push_back(points[(point_cursor+i)%points.size()]);
        return result;
    }
    const Track* active_track()const {
        const Track* best=nullptr;
        for(const auto& track:tracks)if(track.admitted&&(!best||track.total_score>best->total_score))best=&track;
        return best;
    }
    const PatternBurst& current_burst()const {
        const auto* track=active_track();return track?track->burst:latest;
    }
    PatternBurst provisional()const {
        const auto* track=active_track();
        if(!track)return latest;
        PatternBurst result=track->burst;result.bits.resize(track->confirmed);result.end_sample=track->confirmed_end;
        // A provisional observation never claims the physical stream ended.
        result.complete=false;
        return result;
    }
    std::size_t working_bytes()const {
        if(fallback)return wrapper_bytes()+fallback->working_bytes();
        std::size_t total=sizeof(Impl)+sizeof(PatternReceiver)+code.working_bytes();
        for(const auto* v:{&ring,&work,&spectrum,&product,&reference,&points})total+=v->capacity()*sizeof(Complex);
        total+=energy_prefix.capacity()*sizeof(double)+(history.capacity()+peaks.capacity())*sizeof(PatternEvidence)+
            tracks.capacity()*sizeof(Track)+bursts.capacity()*sizeof(PatternBurst)+completed.capacity()*sizeof(Completed)+
            templates.capacity()*sizeof(decltype(templates)::value_type)+
            template_energy.capacity()*sizeof(decltype(template_energy)::value_type)+
            template_square.capacity()*sizeof(decltype(template_square)::value_type)+
            (search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double);
        for(const auto& row:templates)for(const auto& v:row)total+=v.capacity()*sizeof(Complex);
        total+=cached_templates.capacity()*sizeof(CachedTemplates);
        total+=tracking_reference.capacity()*sizeof(decltype(tracking_reference)::value_type);
        for(const auto& bank:cached_templates) {
            total+=bank.rows.capacity()*sizeof(decltype(templates)::value_type)+
                bank.energy.capacity()*sizeof(decltype(template_energy)::value_type)+
                bank.square.capacity()*sizeof(decltype(template_square)::value_type);
            for(const auto& row:bank.rows)for(const auto& values:row)total+=values.capacity()*sizeof(Complex);
        }
        for(const auto& track:tracks)total+=track.burst.bits.capacity()+track.frequency_scores.capacity()*sizeof(double);
        for(const auto& burst:bursts)total+=burst.bits.capacity();
        return total+latest.bits.capacity()+scoring_reservation;
    }
    std::size_t wrapper_bytes()const {
        return sizeof(Impl)+sizeof(PatternReceiver)+code.working_bytes()+
            search.frequency_offsets_hz.capacity()*sizeof(double)+search.clock_errors_ppm.capacity()*sizeof(double);
    }
    void set_workspace_bytes(std::size_t bytes) {
        if(fallback) {
            const auto wrapper=wrapper_bytes();
            if(wrapper>=bytes)throw Error("pattern workspace cannot retain correlator control state");
            fallback->set_workspace_bytes(bytes-wrapper);budget=bytes;return;
        }
        if(!scoring.empty() && !cache_fits(bytes,0))drop_scoring();
        if(!cached_templates.empty() && !cache_fits(bytes,0))drop_template_cache();
        if(bytes<fixed_reservation||working_bytes()>bytes)throw Error("pattern workspace cannot retain current correlation state");
        const auto limit=std::min(configured_bit_limit,(bytes-fixed_reservation)/(2*search.track_limit+2));
        if(!limit)throw Error("pattern workspace cannot retain bit candidates");
        const auto fits=[&](const Bytes& bits){return bits.capacity()<=limit;};
        if(!fits(latest.bits)||std::any_of(tracks.begin(),tracks.end(),[&](const auto& track){return !fits(track.burst.bits);})||
            std::any_of(bursts.begin(),bursts.end(),[&](const auto& burst){return !fits(burst.bits);}))
            throw Error("pattern workspace cannot retain existing bit candidates");
        search.bit_limit=limit;budget=bytes;
    }
};
std::uint64_t pattern_absence_samples(const Config& config) {
    const auto symbol=symbol_sample_count(config);
    const std::uint64_t target=pattern_absence_seconds*config.sample_rate;
    const auto count=std::max<std::uint64_t>(1,target/symbol+(target%symbol!=0));
    if(count>std::numeric_limits<std::uint64_t>::max()/symbol)throw Error("pattern absence duration overflow");
    return count*symbol;
}
PatternReceiver::PatternReceiver(Config c,std::size_t bytes,PatternSearch search):impl_(std::make_unique<Impl>(c,bytes,std::move(search))){}
PatternReceiver::~PatternReceiver()=default;
PatternReceiver::PatternReceiver(PatternReceiver&&) noexcept=default;
PatternReceiver& PatternReceiver::operator=(PatternReceiver&&) noexcept=default;
void PatternReceiver::push(std::span<const float> input,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.finished)throw Error("pattern capture already finished");
    if(s.fallback){s.fallback->push(input,stop);return;}
    Impl::ScoringScope scoring{s};
    if(!s.oscillator_valid) {
        s.oscillator=std::polar(1.,static_cast<double>(std::remainder(-static_cast<long double>(tau)*s.config.carrier_hz*s.sample/s.config.sample_rate,
                                                                   static_cast<long double>(tau))));
        s.oscillator_valid=true;
    }
    for(auto x:input) {
        if((s.sample&4095U)==0)cancelled(stop);
        if(!std::isfinite(x))throw Error("pattern input contains a nonfinite sample");
        s.sum+=static_cast<double>(x)*s.oscillator;s.oscillator*=s.rotation;++s.sample;
        if((s.sample&65535U)==0)s.oscillator/=std::abs(s.oscillator);
        if(++s.partial==s.bin_samples) {
            s.bin(s.sum/static_cast<double>(s.partial),stop);s.sum={};s.partial=0;
        }
    }
    if(s.long_symbol)s.continue_tracks(stop);
}
void PatternReceiver::push(std::span<const float> input,std::span<const std::complex<double>> projected,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.finished)throw Error("pattern capture already finished");
    if(input.size()!=projected.size())throw Error("shared pattern projection length mismatch");
    if(s.fallback){s.fallback->push(input,stop);return;}
    if(s.sample_fit) {
        // The real Gram fit needs a known carrier phase convention. Shared
        // projections may have an arbitrary fixed rotation, so reconstruct
        // these short/sample-resolution observations from their raw samples.
        for(const auto value:projected)
            if(!std::isfinite(value.real())||!std::isfinite(value.imag()))
                throw Error("pattern input contains a nonfinite sample");
        push(input,stop);return;
    }
    s.oscillator_valid=false;
    Impl::ScoringScope scoring{s};
    for(std::size_t i=0;i<input.size();++i) {
        if((s.sample&4095U)==0)cancelled(stop);
        if(!std::isfinite(input[i])||!std::isfinite(projected[i].real())||!std::isfinite(projected[i].imag()))
            throw Error("pattern input contains a nonfinite sample");
        s.sum+=projected[i];++s.sample;
        if(++s.partial==s.bin_samples){s.bin(s.sum/static_cast<double>(s.partial),stop);s.sum={};s.partial=0;}
    }
    if(s.long_symbol)s.continue_tracks(stop);
}
void PatternReceiver::finish(std::stop_token stop) {
    auto& s=*impl_;if(s.finished)return;cancelled(stop);
    Impl::ScoringScope scoring{s};
    if(s.fallback)s.fallback->finish(stop);else s.process(stop,true);
    s.finished=true;
}
std::vector<PatternBurst> PatternReceiver::take_bursts(){
    auto& s=*impl_;if(s.fallback)return s.fallback->take_bursts();
    // The chunk limit bounds storage, not when an accepted symbol becomes
    // visible. Flush at consumer drains without claiming the stream ended.
    for(auto& track:s.tracks)s.publish(track,false,true,true);
    std::vector<PatternBurst> result;result.reserve(s.bursts.size());
    for(auto& burst:s.bursts)result.push_back(std::move(burst));
    s.bursts.clear();return result;
}
PatternBurst PatternReceiver::provisional()const{return impl_->fallback?impl_->fallback->provisional():impl_->provisional();}
std::vector<PatternEvidence> PatternReceiver::candidates()const{return impl_->fallback?impl_->fallback->candidates():impl_->history;}
std::vector<PatternEvidence> PatternReceiver::candidates(std::size_t limit)const{
    if(impl_->fallback)return impl_->fallback->candidates(limit);
    const auto& history=impl_->history;
    return {history.end()-static_cast<std::ptrdiff_t>(std::min(limit,history.size())),history.end()};
}
std::vector<Complex> PatternReceiver::take_chip_constellation(){if(impl_->fallback)return impl_->fallback->take_chip_constellation();auto result=impl_->chip_points();impl_->points.clear();impl_->point_cursor=0;return result;}
bool PatternReceiver::acquiring()const{return impl_->fallback?impl_->fallback->acquiring():!impl_->tracks.empty();}
bool PatternReceiver::synchronized()const{return impl_->fallback?impl_->fallback->synchronized():std::any_of(impl_->tracks.begin(),impl_->tracks.end(),[](const auto& track){return track.admitted;});}
bool PatternReceiver::clock_windowed()const{return static_cast<bool>(impl_->fallback);}
bool PatternReceiver::local_clock_fallback()const{return impl_->local_search_fallback;}
std::size_t PatternReceiver::working_bytes()const {
    return impl_->working_bytes();
}
void PatternReceiver::set_workspace_bytes(std::size_t bytes){impl_->set_workspace_bytes(bytes);}
Diagnostics PatternReceiver::diagnostics()const {
    if(impl_->fallback)return impl_->fallback->diagnostics();
    const auto& s=*impl_;const auto& burst=s.current_burst();Diagnostics d;d.bit_rate=bit_rate(s.config);d.sample_offset=static_cast<std::size_t>(burst.first_sample);
    if(!burst.bits.empty())d.pattern_score=burst.score;
    d.constellation=s.chip_points();return d;
}
}
