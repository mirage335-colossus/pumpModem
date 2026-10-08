#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "search_parallel.hpp"
#include "pattern_correlator_batch.hpp"
#include "pattern_projection_cache.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <chrono>
#include <ctime>

namespace datapump::modem {
namespace {
using Complex = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;
constexpr std::size_t block_size = 128;
void require(bool condition, const char* message) { if (!condition) throw Error(message); }
void cancelled(std::stop_token stop) { if (stop.stop_requested()) throw Error("pattern correlation cancelled"); }
using Projection=detail::CorrelationProjection;
using Fit=detail::CorrelationFit;
using DriftFit=detail::CorrelationDriftFit;
using DifferentialFit=detail::CorrelationDifferentialFit;
struct Bank {
    double frequency=0;
    std::vector<Projection> prefix;
    std::vector<Complex> first_moment;
    std::vector<detail::CorrelationCarrierMoments> affine_carrier;
    std::array<std::uint64_t,4> affine_lengths{};
    std::span<const Projection> shared_prefix;
    std::span<const Complex> shared_moment;
    std::span<const Projection> observations() const {return shared_prefix.empty()?std::span<const Projection>(prefix):shared_prefix;}
    std::span<const Complex> moments() const {return shared_moment.empty()?std::span<const Complex>(first_moment):shared_moment;}
    void prepare_carrier(std::size_t block_samples,long double knot_duration,std::uint32_t sample_rate) {
        // Immutable during parallel lane scoring. A complete oscillator block
        // is the dominant length at large chips; retain three common knot
        // clips too, and compute any other length exactly in caller scratch.
        affine_carrier.resize(affine_lengths.size());affine_lengths[0]=block_samples;
        std::size_t used=1;
        const auto remainder=std::fmod(knot_duration,static_cast<long double>(block_samples));
        const auto lower=static_cast<std::uint64_t>(std::floor(remainder));
        const auto upper=static_cast<std::uint64_t>(std::ceil(remainder));
        const auto retain=[&](std::uint64_t count) {
            if(!count || count>block_samples || used==affine_lengths.size() ||
               std::find(affine_lengths.begin(),affine_lengths.begin()+static_cast<std::ptrdiff_t>(used),count)!=
                   affine_lengths.begin()+static_cast<std::ptrdiff_t>(used))return;
            affine_lengths[used++]=count;
        };
        retain(lower);retain(upper);
        if(knot_duration<block_samples)retain(lower?lower-1:0);
        else retain(block_samples-upper);
        for(std::uint64_t count=1;used<affine_lengths.size();++count)retain(count);
        for(std::size_t i=0;i<affine_lengths.size();++i)
            affine_carrier[i]=detail::correlation_carrier_moments(affine_lengths[i],2*tau*frequency/sample_rate);
    }
    const detail::CorrelationCarrierMoments* carrier(std::uint64_t count) const {
        for(std::size_t i=0;i<affine_carrier.size();++i)
            if(affine_lengths[i]==count)return &affine_carrier[i];
        return nullptr;
    }
};
struct WorkTimer {
    bool enabled;
    double& wall;
    double& cpu;
    std::chrono::steady_clock::time_point start{};
    std::clock_t cpu_start=0;
    WorkTimer(bool active,double& seconds,double& cpu_seconds):enabled(active),wall(seconds),cpu(cpu_seconds) {
        if(enabled){start=std::chrono::steady_clock::now();cpu_start=std::clock();}
    }
    ~WorkTimer(){finish();}
    void finish() {
        if(!enabled)return;
        wall+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        cpu+=static_cast<double>(std::clock()-cpu_start)/CLOCKS_PER_SEC;enabled=false;
    }
};
}

struct PatternCorrelator::Impl {
    Config config;
    PatternSearch search;
    PatternCode code;
    // Mutable keystream/chip caches belong to one scoring worker. Completions
    // still run in hypothesis order on the caller after these workers finish.
    std::vector<PatternCode> worker_codes;
    std::size_t budget=0,bit_limit=0,accounted_bytes=0,drift_reserved=0;
    std::uint64_t sample=0,trials=0;
    std::uint64_t phase_step=1;
    std::size_t alternate_groups=0;
    std::size_t block_samples=block_size;
    unsigned drift_sections=1;
    std::uint64_t differential_window=0;
    long double origin_lower=0;
    bool finished=false,shaped=false,pulse_segmented=false;
    PatternCorrelatorOptions options;
    PatternCorrelatorWork work;
    struct Hypothesis {
        long double origin=0,rate=1;
        std::uint64_t index=0,observed_start=0,phase_lower=0,phase_upper=0;
        std::size_t frequency=0,rate_index=0,tone_bank_base=0;
        std::array<Fit,2> fits{};
        PatternBurst burst;
        // The burst stores committed score/support; only tentative totals need
        // separate accumulators. Its committed endpoint is also the last find.
        double sum_score=0,pending_score=0,sum_support=0;
        std::size_t committed=0,gap_slots=0;
        std::uint64_t committed_end=0;
        bool admitted=false,pending_gaps=false;
    };
    std::vector<Hypothesis> hypotheses;
    struct Emission {
        const Hypothesis* owner=nullptr;
        std::uint64_t first_symbol=0,last_symbol=0;
        long double origin=0;
        double frequency=0,reported_frequency=0,carrier_score=0;
        bool ended=false;
    };
    std::vector<Emission> emissions;
    // Only unresolved subsecond schedules need extra fits. Ordinary explicit
    // schedules retain the original pair of per-hypothesis accumulators.
    std::vector<std::array<Fit,2>> alternate_fits;
    std::vector<std::array<detail::CorrelationChipEvidence,2>> chip_evidence;
    std::vector<double> pending_chip_scores;
    // Allocated only for eligible long patterns; one active section per bit
    // and schedule, independent of symbol duration and input chunk length.
    std::vector<std::array<DriftFit,2>> drift_fits;
    std::vector<std::array<DifferentialFit,2>> differential_fits;
    std::vector<Bank> banks;
    // Only the partial-symbol affine path needs these per-lane diagnostics.
    // Separate counters keep worker accumulation independent and bounded.
    std::vector<std::uint64_t> affine_counts;
    struct PulseLattice {
        long double anchor=0,rate=1,start=0,first_offset=0;
        std::size_t frequency=0;
        std::uint64_t next=0;
        Complex carrier_square{};
        double carrier_norm=1;
        detail::CorrelationPulseKernel kernel;
        detail::CorrelationPulseCell pending;
        // An affine pulse piece survives original oscillator blocks and caller
        // pushes. Only its four measured moments are accumulated until the next
        // table knot; private coefficients never enter this shared frontend.
        detail::CorrelationPulseSegment segment;
        std::uint64_t segment_first=0,segment_observed=0;
        std::complex<long double> segment_sum{},segment_moment{};
        std::vector<detail::CorrelationPulseCell> cells;
        PulseLattice(long double origin,long double ratio,std::size_t bank,
                     std::uint64_t chip,double frequency_hz,std::uint32_t sample_rate)
            :anchor(origin),rate(ratio),frequency(bank),kernel(chip,ratio,frequency_hz,sample_rate) {
            const auto position=std::floor(-anchor*rate/chip);
            require(position>=std::numeric_limits<std::int64_t>::min() &&
                    position<std::numeric_limits<std::int64_t>::max(),"pulse cell coordinate overflow");
            pending.chip=static_cast<std::int64_t>(position);
            start=anchor+static_cast<long double>(pending.chip)*chip/rate;
            next=static_cast<std::uint64_t>(std::max(0.L,std::ceil(anchor+
                static_cast<long double>(pending.chip+1)*chip/rate)));
        }
    };
    std::vector<PulseLattice> pulse_lattices;
    struct PulseAddress {std::size_t lattice=0;std::int64_t shift=0;};
    std::vector<PulseAddress> pulse_addresses;
    std::vector<PatternEvidence> history;
    std::vector<PatternBurst> bursts;
    std::vector<Complex> points;
    std::size_t point_begin=0,point_count=0;
    Complex previous_point{};

    Impl(Config c,PatternSearch search_options,std::size_t bytes,PatternCorrelatorOptions work_options)
        :config(c),search(std::move(search_options)),code(c,c.stream_epoch),budget(bytes),options(work_options) {
        validate(c);
        drift_sections=detail::drift_section_count(c,search.drift_tolerant);
        require(std::isfinite(search.differential_window_seconds) && search.differential_window_seconds>=0,
                "invalid differential window duration");
        if(drift_sections>1)differential_window=detail::differential_window_samples(
            code.symbol_samples(),code.chip_samples(),c.sample_rate,search.differential_window_seconds);
        if(search.compact_clock_search) {
            search.candidate_limit=std::min<std::size_t>(search.candidate_limit,32);
            block_samples=32;
        }
        const std::size_t point_capacity=search.compact_clock_search?64:2048;
        phase_step=std::gcd(code.symbol_samples(),static_cast<std::uint64_t>(c.sample_rate));
        const auto phase_upper=search.search_stream_phases && (c.scramble || c.dsss)?
            (std::min(code.symbol_samples(),static_cast<std::uint64_t>(c.sample_rate))-1)/phase_step*phase_step:
            c.stream_phase_samples;
        const auto phase_lower=search.search_stream_phases && (c.scramble || c.dsss)?0:c.stream_phase_samples;
        if(phase_lower<phase_upper)alternate_groups=code.symbol_samples()>=c.sample_rate?1:2;
        shaped=pattern_pulse_enabled(c);
        require(c.pattern_symbols,"streaming pattern correlator requires binary pattern transport");
        require(search.start_offset_seconds && std::isfinite(*search.start_offset_seconds) &&
                std::isfinite(search.start_uncertainty_seconds) && search.start_uncertainty_seconds>=0,
                "long pattern correlation requires a finite system-clock start window");
        require(std::isfinite(search.false_alarm_probability) && search.false_alarm_probability>0 && search.false_alarm_probability<1 &&
                std::isfinite(search.retain_score) && search.retain_score>=0 && search.bit_limit && search.chunk_bits &&
                search.candidate_limit && search.candidate_limit<=65536 && search.track_limit && search.track_limit<=128,
                "invalid long pattern search limits");
        if(c.oscillator_search && search.hypotheses.empty() && search.frequency_offsets_hz.empty() &&
           search.clock_errors_ppm==std::vector<double>{0} && !search.couple_clock_to_carrier)
            search.hypotheses=oscillator_pattern_search(c).hypotheses;
        const bool paired=!search.hypotheses.empty();
        long double highest_rate=1,lowest_rate=1;
        if(paired) {
            require(search.hypotheses.size()<=maximum_pattern_frequency_rate_hypotheses,
                    "frequency/rate bank exceeds finite hypothesis limit");
            search.frequency_offsets_hz.clear();search.clock_errors_ppm={0};
            search.couple_clock_to_carrier=false;
            for(const auto& pair:search.hypotheses) {
                require(std::isfinite(pair.frequency_offset_hz) && std::isfinite(pair.clock_error_ppm) &&
                        std::abs(pair.clock_error_ppm)<=10000,"invalid paired frequency/rate hypothesis");
                highest_rate=std::max(highest_rate,1+static_cast<long double>(pair.clock_error_ppm)*1e-6L);
                lowest_rate=std::min(lowest_rate,1+static_cast<long double>(pair.clock_error_ppm)*1e-6L);
                if(std::find(search.frequency_offsets_hz.begin(),search.frequency_offsets_hz.end(),pair.frequency_offset_hz)==
                   search.frequency_offsets_hz.end())search.frequency_offsets_hz.push_back(pair.frequency_offset_hz);
            }
            require(search.frequency_offsets_hz.size()<=maximum_pattern_frequency_hypotheses,
                    "frequency bank exceeds finite hypothesis limit");
        } else {
            require(!search.clock_errors_ppm.empty() && search.clock_errors_ppm.size()<=65,"clock-rate bank must contain 1..65 hypotheses");
            for(auto ppm:search.clock_errors_ppm) {
                require(std::isfinite(ppm) && std::abs(ppm)<=10000,"clock-rate hypotheses must fit +/-10000 ppm");
                highest_rate=std::max(highest_rate,1+static_cast<long double>(ppm)*1e-6L);
                lowest_rate=std::min(lowest_rate,1+static_cast<long double>(ppm)*1e-6L);
            }
            if(search.frequency_offsets_hz.empty()) {
                const auto step=.25*c.sample_rate/static_cast<double>(code.symbol_samples());
                require(!search.expand_clock_search || default_pattern_frequency_search(c).count<=5,
                        "expanded carrier competition requires FFT workspace");
                search.frequency_offsets_hz={0,-step,step,-2*step,2*step};
            }
            require(!search.couple_clock_to_carrier && (!search.expand_clock_search || search.frequency_offsets_hz.size()<=5),
                    "coupled carrier competition requires FFT workspace");
            require(search.frequency_offsets_hz.size()<=65,"frequency bank exceeds 65 hypotheses");
        }
        const auto tone_limit=static_cast<double>(c.sample_rate)/(4*static_cast<double>(code.chip_samples()));
        const auto offset_limit=paired?pattern_frequency_offset_limit(c):c.bandwidth_hz/8;
        for(auto frequency:search.frequency_offsets_hz) {
            require(std::isfinite(frequency) && std::abs(frequency)<=offset_limit,"frequency hypothesis exceeds occupied band");
            if(c.spreading_mode==SpreadingMode::tone)
                require(std::abs(frequency)<tone_limit,"tone frequency uncertainty aliases binary labels");
        }
        const auto lower=(static_cast<long double>(*search.start_offset_seconds)-search.start_uncertainty_seconds)*c.sample_rate;
        const auto upper=(static_cast<long double>(*search.start_offset_seconds)+search.start_uncertainty_seconds)*c.sample_rate;
        origin_lower=lower;
        require(std::isfinite(lower) && std::isfinite(upper) && std::abs(lower)<1e15L && std::abs(upper)<1e15L,
                "clock start window exceeds precise sample-coordinate range");
        const auto step=std::max(1.L,std::floor(static_cast<long double>(code.chip_samples())/(2*highest_rate)));
        const auto pair_count=paired?search.hypotheses.size():search.frequency_offsets_hz.size()*search.clock_errors_ppm.size();
        const auto origin_step=[&](long double ratio) {
            return paired?static_cast<long double>(code.chip_samples())/(2*ratio):step;
        };
        long double total=0;
        for(std::size_t pair=0;pair<pair_count;++pair) {
            const auto ppm=paired?search.hypotheses[pair].clock_error_ppm:
                search.clock_errors_ppm[pair/search.frequency_offsets_hz.size()];
            total+=std::ceil((upper-lower)/origin_step(1+static_cast<long double>(ppm)*1e-6L))+1;
        }
        const auto bank_count=c.spreading_mode==SpreadingMode::tone?2*pair_count:search.frequency_offsets_hz.size();
        long double fixed=sizeof(PatternCorrelator)+sizeof(Impl)+code.working_bytes()+
            total*(sizeof(Hypothesis)+alternate_groups*sizeof(std::array<Fit,2>))+
            bank_count*(sizeof(Bank)+(block_samples+1)*sizeof(Projection))+point_capacity*sizeof(Complex)+
            static_cast<long double>(search.candidate_limit)*sizeof(PatternEvidence)+search.track_limit*(sizeof(PatternBurst)+sizeof(Emission))+
            (search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double)+
            search.hypotheses.capacity()*sizeof(PatternFrequencyRateHypothesis);
        // A symbol at least six seconds long cannot form an unconfirmed
        // multi-symbol acquisition chain before physical absence expires it.
        const bool guard_chains=config.spreading_mode==SpreadingMode::pattern && (c.scramble || c.dsss) &&
            static_cast<long double>(code.symbol_samples())/highest_rate<pattern_absence_seconds*c.sample_rate;
        if(guard_chains)fixed+=total*((alternate_groups+1)*sizeof(decltype(chip_evidence)::value_type)+sizeof(double));
        require(total>=1 && total<=std::numeric_limits<std::size_t>::max() && fixed<bytes,
                "complete half-chip clock/frequency/rate coverage exceeds DSP workspace");
        const auto count=static_cast<std::size_t>(total);
        if(guard_chains){chip_evidence.resize(count*(alternate_groups+1));pending_chip_scores.resize(count);}
        const auto denominator=2*count+2*search.track_limit+2;
        if(drift_sections>1) {
            const auto extra=total*(alternate_groups+1)*sizeof(std::array<DriftFit,2>);
            // Drift tolerance is optional; it must not displace an otherwise
            // affordable complete clock bank or its minimum bit retention.
            if(fixed+extra+denominator<=bytes) {
                drift_reserved=static_cast<std::size_t>(extra);fixed+=extra;
            } else drift_sections=1;
        }
        if(drift_sections==1)differential_window=0;
        if(differential_window) {
            const auto extra=total*(alternate_groups+1)*sizeof(std::array<DifferentialFit,2>);
            if(fixed+extra+denominator<=bytes)fixed+=extra;
            else differential_window=0;
        }
        // Full chip cells are sufficient statistics for this exact finite
        // pulse. Two half-chip origin parities share each projection. Affine
        // spans below cover partial chips or unaffordable shared-cell kernels;
        // unsupported legacy fractional grids retain the raw reference path.
        const bool pulse_geometry=!options.raw_reference && shaped && code.symbol_samples()>=16ULL*c.sample_rate &&
            // Historical low-level compact banks promise a sub-64-KiB idle
            // footprint. Application oscillator banks provide explicit pairs;
            // retain the bounded raw fallback for legacy long-chip callers.
            (code.chip_samples()<=4096 || paired) &&
            code.symbol_samples()%(4*code.chip_samples())==0 &&
            (paired || (code.chip_samples()%2==0 &&
                std::all_of(search.clock_errors_ppm.begin(),search.clock_errors_ppm.end(),[](double ppm){return ppm==0;})));
        const auto pulse_cell_capacity=static_cast<std::size_t>(std::ceil(block_samples*highest_rate/code.chip_samples()))+2;
        const auto pulse_count=3*pair_count;
        const auto pulse_extra=static_cast<long double>(pulse_count)*
            (sizeof(PulseLattice)+pulse_cell_capacity*sizeof(detail::CorrelationPulseCell))+
            total*sizeof(PulseAddress)+detail::correlation_pulse_scratch_bytes+
            (code.chip_samples()>4096?bank_count*(block_samples+1)*sizeof(Complex):0);
        const bool pulse_enabled=pulse_geometry && fixed+pulse_extra+denominator<=bytes;
        if(pulse_enabled) {fixed+=pulse_extra;pulse_lattices.reserve(pulse_count);pulse_addresses.reserve(count);}
        // Affine spans also retain a whole symbol when its shared-cell kernels
        // cannot fit. They keep the same complete search and pulse geometry
        // without retaining a kernel or observations per lane.
        const auto affine_capacity=std::tuple_size_v<decltype(Bank::affine_lengths)>;
        const auto segment_extra=bank_count*((block_samples+1)*sizeof(Complex)+
            affine_capacity*sizeof(detail::CorrelationCarrierMoments))+total*sizeof(std::uint64_t);
        pulse_segmented=!options.raw_reference && shaped && paired &&
            code.symbol_samples()>=16ULL*c.sample_rate &&
            code.chip_samples()>=1024 &&
            !pulse_enabled &&
            fixed+segment_extra+denominator<=bytes;
        if(pulse_segmented){fixed+=segment_extra;affine_counts.resize(count);}
        const auto remaining=bytes-static_cast<std::size_t>(std::ceil(fixed));
        bit_limit=std::min(search.bit_limit,remaining/denominator);
        require(bit_limit>0,"clock-search workspace cannot retain symbol evidence");
        hypotheses.reserve(count);emissions.reserve(search.track_limit);banks.resize(bank_count);
        for(auto& bank:banks) {
            bank.prefix.resize(block_samples+1);
            if(pulse_segmented || (pulse_enabled && code.chip_samples()>4096))bank.first_moment.resize(block_samples+1);
        }
        points.resize(point_capacity);
        require(!alternate_groups || count<=std::numeric_limits<std::size_t>::max()/alternate_groups,
                "pattern phase fits exceed address space");
        alternate_fits.resize(count*alternate_groups);
        if(differential_window)differential_fits.resize(count*(alternate_groups+1));
        if(drift_sections>1) {
            require(count<=std::numeric_limits<std::size_t>::max()/(alternate_groups+1),
                    "pattern drift fits exceed address space");
        }
        history.reserve(search.candidate_limit);bursts.reserve(search.track_limit);
        for(std::size_t pair=0;pair<pair_count;++pair) {
            const auto rate=paired?pair:pair/search.frequency_offsets_hz.size();
            const auto f=paired?static_cast<std::size_t>(std::find(search.frequency_offsets_hz.begin(),search.frequency_offsets_hz.end(),
                search.hypotheses[pair].frequency_offset_hz)-search.frequency_offsets_hz.begin()):pair%search.frequency_offsets_hz.size();
            const auto ppm=paired?search.hypotheses[pair].clock_error_ppm:search.clock_errors_ppm[rate];
            const auto ratio=1+static_cast<long double>(ppm)*1e-6L;
            const auto pair_step=origin_step(ratio);
            const auto origins=static_cast<std::size_t>(std::ceil((upper-lower)/pair_step)+1);
            const auto lattice_base=pulse_lattices.size();
            const auto make_lattice=[&](long double anchor) {
                pulse_lattices.emplace_back(anchor,ratio,f,code.chip_samples(),
                    c.carrier_hz+search.frequency_offsets_hz[f],c.sample_rate);
                pulse_lattices.back().cells.reserve(pulse_cell_capacity);
            };
            if(pulse_enabled) {
                make_lattice(lower);
                // A clipped second origin has its own lattice below. Do not
                // project an unused half-chip parity outside the search window.
                if(origins>1 && lower+pair_step<=upper)make_lattice(lower+pair_step);
            }
            if(c.spreading_mode==SpreadingMode::tone)for(unsigned bit=0;bit<2;++bit)
                banks[2*pair+bit].frequency=c.carrier_hz+search.frequency_offsets_hz[f]+
                    (bit?1.:-1.)*tone_limit*static_cast<double>(ratio);
            else banks[f].frequency=c.carrier_hz+search.frequency_offsets_hz[f];
            for(std::size_t i=0;i<origins;++i) {
                Hypothesis h;h.origin=std::min(upper,lower+static_cast<long double>(i)*pair_step);
                h.rate=ratio;h.frequency=f;h.rate_index=rate;h.tone_bank_base=2*pair;
                if(pulse_enabled) {
                    PulseAddress address;
                    if(h.origin==upper && h.origin!=lower+static_cast<long double>(i)*pair_step) {
                        make_lattice(h.origin);address.lattice=pulse_lattices.size()-1;
                    } else {address.lattice=lattice_base+i%2;address.shift=static_cast<std::int64_t>(i/2);}
                    pulse_addresses.push_back(address);
                }
                h.phase_lower=phase_lower;h.phase_upper=phase_upper;
                const auto elapsed=std::max(0.L,-h.origin)*ratio;
                const auto index=std::floor(elapsed/code.symbol_samples());
                require(index<std::numeric_limits<std::uint64_t>::max(),"clock hint exceeds stream symbol counter");
                h.index=static_cast<std::uint64_t>(index);hypotheses.push_back(std::move(h));
            }
        }
        if(pulse_segmented)for(auto& bank:banks)
            bank.prepare_carrier(block_samples,code.chip_samples()/(256*lowest_rate),c.sample_rate);
        accounted_bytes=working_bytes()+drift_reserved;
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"clock-search state exceeds DSP workspace");
        work.backend=pulse_segmented?PatternCorrelationBackend::pulse_segments:pulse_lattices.empty()?PatternCorrelationBackend::raw:
            code.chip_samples()>4096?PatternCorrelationBackend::pulse_moments:PatternCorrelationBackend::pulse;
        work.hypotheses=hypotheses.size();work.lattices=pulse_lattices.size();work.phase_groups=alternate_groups+1;
        work.peak_workspace_bytes=sizeof(PatternCorrelator)+accounted_bytes;
    }
    long double clock_boundary(const Hypothesis& h,std::uint64_t within=0) const {
        if(pulse_lattices.empty() && search.hypotheses.empty()) {
            if(within==code.symbol_samples())return h.origin+(static_cast<long double>(h.index)+1)*code.symbol_samples()/h.rate;
            return h.origin+static_cast<long double>(h.index)*code.symbol_samples()/h.rate+within/h.rate;
        }
        long double anchor=h.origin,shift=0;
        if(!pulse_lattices.empty()) {
            const auto& address=pulse_addresses[static_cast<std::size_t>(&h-hypotheses.data())];
            anchor=pulse_lattices[address.lattice].anchor;shift=address.shift;
        } else if(!search.hypotheses.empty()) {
            // Recover the exact half-chip origin address, including the final
            // clipped endpoint, without enlarging every legacy raw lane.
            const auto step=static_cast<long double>(code.chip_samples())/(2*h.rate);
            const auto address=std::round((h.origin-origin_lower)/step);
            if(h.origin==origin_lower+address*step) {
                const auto parity=std::fmod(address,2.L);
                anchor=origin_lower+parity*step;shift=std::floor(address/2);
            }
        }
        return anchor+(shift*code.chip_samples()+static_cast<long double>(h.index)*code.symbol_samples()+within)/h.rate;
    }
    long double symbol_start(const Hypothesis& h) const {return clock_boundary(h);}
    void advance_drift(DriftFit& fit,const Hypothesis& h,std::uint64_t observed) const {
        while(fit.section+1<drift_sections && static_cast<long double>(observed)>=
              std::ceil(clock_boundary(h,detail::drift_boundary(fit.section+1,code.symbol_samples(),drift_sections)))) {
            const auto explained=fit.active.explained();fit.explained_sum+=explained;
            fit.largest_explained=std::max(fit.largest_explained,explained);fit.active={};++fit.section;
        }
    }
    void advance_differential(DifferentialFit& fit,const Hypothesis& h,std::uint64_t observed) const {
        const auto total=code.symbol_samples(),width=differential_window;
        const auto position=std::max(0.L,(static_cast<long double>(observed)-symbol_start(h))*h.rate);
        auto next=static_cast<std::uint64_t>(std::min(std::floor(position/width),static_cast<long double>(total/width)));
        while(next<total/width && static_cast<long double>(observed)>=std::ceil(clock_boundary(h,(next+1)*width)))++next;
        while(next && static_cast<long double>(observed)<std::ceil(clock_boundary(h,next*width)))--next;
        if(fit.initialized && next==fit.window_index)return;
        if(fit.initialized)fit.finish(total,width);
        fit.active={};fit.window_index=next;fit.initialized=true;fit.expected_count=0;
        if(next<total/width)fit.expected_count=static_cast<std::uint64_t>(
            std::ceil(clock_boundary(h,(next+1)*width))-std::ceil(clock_boundary(h,next*width)));
    }
    void prepare_drift(std::uint64_t end) {
        if(!drift_reserved)return;
        const auto first_boundary=detail::drift_boundary(1,code.symbol_samples(),drift_sections);
        const auto needed=std::any_of(hypotheses.begin(),hypotheses.end(),[&](const auto& h) {
            return static_cast<long double>(end)>std::ceil(clock_boundary(h,first_boundary));
        });
        if(!needed)return;
        // Until the first boundary the ordinary whole-symbol fit is also the
        // first section fit. Reserve its eventual state from construction, but
        // keep idle long-symbol searches at their historical physical size.
        drift_fits.resize(hypotheses.size()*(alternate_groups+1));
        for(std::size_t i=0;i<hypotheses.size();++i)for(std::size_t group=0;group<=alternate_groups;++group) {
            auto& drift=section_fits(i,group);const auto& whole=fits(hypotheses[i],i,group);
            for(unsigned bit=0;bit<2;++bit)drift[bit].active=whole[bit];
        }
        drift_reserved=0;accounted_bytes=working_bytes();
        room_for(0);
    }
    void prepare_workers() {
        if(!pulse_lattices.empty())return;
        if(!worker_codes.empty())return;
        const auto concurrency=std::min(hypotheses.size(),detail::search_concurrency(search.worker_threads));
        const auto workers=std::min(concurrency,(budget-sizeof(PatternCorrelator)-accounted_bytes)/code.working_bytes());
        if(workers>1) {
            std::vector<PatternCode> prepared;prepared.reserve(workers);
            for(std::size_t worker=0;worker<workers;++worker)prepared.emplace_back(config,config.stream_epoch);
            worker_codes=std::move(prepared);
            accounted_bytes=working_bytes()+drift_reserved;
            require(sizeof(PatternCorrelator)+accounted_bytes<=budget,"parallel pattern caches exceed DSP workspace");
            work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
        }
    }
    std::size_t working_bytes() const {
        auto value=sizeof(Impl)+code.working_bytes()+hypotheses.capacity()*sizeof(Hypothesis)+banks.capacity()*sizeof(Bank)+
            emissions.capacity()*sizeof(Emission)+
            alternate_fits.capacity()*sizeof(decltype(alternate_fits)::value_type)+
            chip_evidence.capacity()*sizeof(decltype(chip_evidence)::value_type)+pending_chip_scores.capacity()*sizeof(double)+
            drift_fits.capacity()*sizeof(decltype(drift_fits)::value_type)+
            differential_fits.capacity()*sizeof(decltype(differential_fits)::value_type)+
            points.capacity()*sizeof(Complex)+
            history.capacity()*sizeof(PatternEvidence)+bursts.capacity()*sizeof(PatternBurst)+
            (search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double)+
            search.hypotheses.capacity()*sizeof(PatternFrequencyRateHypothesis);
        value+=pulse_lattices.capacity()*sizeof(PulseLattice);
        value+=pulse_addresses.capacity()*sizeof(PulseAddress);
        value+=affine_counts.capacity()*sizeof(std::uint64_t);
        // Kernel preparation has bounded wide polynomial scratch. Retain its
        // reservation across workspace changes and payload allocation too.
        if(!pulse_lattices.empty())value+=detail::correlation_pulse_scratch_bytes;
        for(const auto& lattice:pulse_lattices)value+=lattice.cells.capacity()*sizeof(detail::CorrelationPulseCell);
        for(const auto& h:hypotheses)value+=h.burst.bits.capacity();
        for(const auto& burst:bursts)value+=burst.bits.capacity();
        for(const auto& bank:banks)value+=bank.prefix.capacity()*sizeof(Projection)+bank.first_moment.capacity()*sizeof(Complex)+
            bank.affine_carrier.capacity()*sizeof(detail::CorrelationCarrierMoments);
        value+=worker_bytes();
        return value;
    }
    std::size_t projection_cache_headroom(std::size_t input_samples) const {
        // This is an allocation bound for one finite push and its next drain,
        // not a signal-dependent prediction. Optional worker caches may be
        // dropped by room_for(); promised detectors are reserved separately.
        constexpr auto maximum=std::numeric_limits<std::size_t>::max();
        if(input_samples>std::numeric_limits<std::uint64_t>::max()-sample)return maximum;
        std::size_t total=0,transient=0,output=0;
        const auto add=[&](std::size_t n){total=n>maximum-total?maximum:total+n;};
        const auto product=[&](std::size_t a,std::size_t b){return a && b>maximum/a?maximum:a*b;};
        for(const auto& h:hypotheses) {
            // A partly accumulated symbol can finish immediately. Each later
            // completion appends at most one bit; gap events carry no bits.
            const auto slots=1+std::ceil(static_cast<long double>(input_samples)*h.rate/code.symbol_samples());
            if(!std::isfinite(slots) || slots>=static_cast<long double>(maximum))return maximum;
            const auto added=static_cast<std::size_t>(slots);
            const auto required=std::min(bit_limit,added>maximum-h.burst.bits.size()?maximum:h.burst.bits.size()+added);
            auto capacity=h.burst.bits.capacity();const auto initial=capacity;
            while(capacity<required) {
                transient=std::max(transient,capacity);
                capacity=capacity? (capacity>bit_limit/2?bit_limit:capacity*2):1;
            }
            // A failed prefix can clear its buffer and regrow from zero in
            // this same push. Include that doubling trajectory's old buffer
            // in the transient bound even if the initial capacity was ample.
            auto reset_capacity=std::size_t{0};
            const auto reset_required=std::min(bit_limit,added);
            while(reset_capacity<reset_required) {
                transient=std::max(transient,reset_capacity);
                reset_capacity=reset_capacity? (reset_capacity>bit_limit/2?bit_limit:reset_capacity*2):1;
            }
            capacity=std::max(capacity,reset_capacity);
            add(capacity-initial);
            output=std::max(output,std::min(required,std::min(search.chunk_bits,bit_limit)));
        }
        // reserve(wanted) checks the complete new allocation while the old
        // payload remains charged. Publishing likewise creates one temporary
        // copy before queue insertion/replacement, alongside retained events.
        add(transient);add(product(search.track_limit,output));add(output);
        // take_bursts() moves the existing event vector and reserves its next
        // bounded queue before the caller replaces its previous drained vector.
        add(product(search.track_limit,sizeof(PatternBurst)));
        return total;
    }
    std::size_t worker_bytes() const {
        auto bytes=worker_codes.capacity()*sizeof(PatternCode);
        for(const auto& worker:worker_codes)bytes+=worker.working_bytes()-sizeof(PatternCode);
        return bytes;
    }
    void drop_workers() {
        accounted_bytes-=worker_bytes();
        std::vector<PatternCode>().swap(worker_codes);
    }
    void room_for(std::size_t extra) {
        if(!worker_codes.empty() && (sizeof(PatternCorrelator)+accounted_bytes>budget ||
                extra>budget-sizeof(PatternCorrelator)-accounted_bytes))drop_workers();
        require(sizeof(PatternCorrelator)+accounted_bytes<=budget && extra<=budget-sizeof(PatternCorrelator)-accounted_bytes,
                "pattern evidence exceeds DSP workspace");
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes+extra);
    }
    double threshold() const {
        const auto t=static_cast<double>(trials);
        return -std::log(search.false_alarm_probability)+std::log(t)+std::log(t+1)+std::log(2.);
    }
    void remember(PatternEvidence evidence) {
        if(evidence.score<search.retain_score)return;
        evidence.admission_threshold=threshold();
        if(history.size()==search.candidate_limit)history.erase(history.begin());
        history.push_back(evidence);
    }
    struct PhaseGroup {std::uint64_t lower=0,upper=0;};
    std::array<PhaseGroup,3> phase_groups(const Hypothesis& h,std::size_t& count)const {
        std::array<PhaseGroup,3> groups{};count=0;
        auto lower=h.phase_lower;
        while(lower<=h.phase_upper) {
            const auto address=symbol_stream_address(config.stream_epoch,lower,h.index,code.symbol_samples(),config.sample_rate);
            auto low=std::uint64_t{0},high=(h.phase_upper-lower)/phase_step;
            while(low<high) {
                const auto mid=low+(high-low+1)/2;
                const auto candidate=symbol_stream_address(config.stream_epoch,lower+mid*phase_step,h.index,
                    code.symbol_samples(),config.sample_rate);
                if(candidate.epoch==address.epoch && candidate.ordinal==address.ordinal)low=mid;
                else high=mid-1;
            }
            const auto end=lower+low*phase_step;
            require(count<groups.size(),"pattern phase groups exceed finite symbol interval");
            groups[count++]={lower,end};
            if(end==h.phase_upper)break;
            lower=end+phase_step;
        }
        return groups;
    }
    std::array<Fit,2>& fits(Hypothesis& h,std::size_t hypothesis,std::size_t group) {
        if(!group)return h.fits;
        require(group<=alternate_groups,"pattern phase fit exceeds its allocated bank");
        return alternate_fits[hypothesis*alternate_groups+group-1];
    }
    std::array<DriftFit,2>& section_fits(std::size_t hypothesis,std::size_t group) {
        return drift_fits[hypothesis*(alternate_groups+1)+group];
    }
    std::array<DifferentialFit,2>& local_fits(std::size_t hypothesis,std::size_t group) {
        return differential_fits[hypothesis*(alternate_groups+1)+group];
    }
    Emission& output_stream(Hypothesis& h) {
        const auto same_clock=[&](long double origin,double frequency) {
            return std::abs(origin-h.origin)<static_cast<long double>(code.symbol_samples())/3 &&
                ((!search.hypotheses.empty() && search.frequency_rate_competition) ||
                 std::abs(frequency-h.burst.frequency_hz)<=config.sample_rate/static_cast<double>(code.symbol_samples()));
        };
        for(auto& emission:emissions)if(same_clock(emission.origin,emission.frequency) &&
            (!emission.ended || h.burst.stream_first_symbol<emission.last_symbol))return emission;
        // Choose once before immutable output starts. Other hypotheses retain
        // their evidence but cannot replace already delivered stream bytes.
        const Hypothesis* owner=&h;
        for(const auto& candidate:hypotheses)if(candidate.admitted && same_clock(candidate.origin,candidate.burst.frequency_hz) &&
            (candidate.burst.stream_first_symbol<owner->burst.stream_first_symbol ||
             (candidate.burst.stream_first_symbol==owner->burst.stream_first_symbol && candidate.sum_score>owner->sum_score)))owner=&candidate;
        Emission emission{owner,owner->burst.stream_first_symbol,owner->burst.first_stream_symbol,
            owner->origin,owner->burst.frequency_hz,owner->burst.frequency_hz,owner->burst.score,false};
        if(emissions.size()==search.track_limit) {
            const auto reusable=std::find_if(emissions.begin(),emissions.end(),[](const auto& item){return item.ended;});
            require(reusable!=emissions.end(),"pattern output stream quota exhausted");*reusable=emission;return *reusable;
        }
        emissions.push_back(emission);return emissions.back();
    }
    double carrier_estimate(const Hypothesis& owner,Emission& stream) const {
        // Keep immutable bit ownership, but report the strongest accumulated
        // carrier evidence over comparable observations. A startup distortion
        // must not lock the displayed frequency to its first winning grid bin.
        const auto* best=&owner;
        for(const auto& candidate:hypotheses) {
            if(!candidate.admitted || candidate.burst.stream_first_symbol!=owner.burst.stream_first_symbol)continue;
            const auto difference=candidate.committed_end>owner.committed_end?
                candidate.committed_end-owner.committed_end:owner.committed_end-candidate.committed_end;
            if(difference>code.symbol_samples() || std::abs(candidate.origin-owner.origin)>=code.symbol_samples()/3.L ||
               ((!search.hypotheses.empty() && search.frequency_rate_competition)?false:
                std::abs(candidate.burst.frequency_hz-owner.burst.frequency_hz)>
                    config.sample_rate/static_cast<double>(code.symbol_samples())))continue;
            if(candidate.burst.score>best->burst.score)best=&candidate;
        }
        // Nearby clocks may retire in a different order at the terminal
        // absence observation. Their retirement must not revert an already
        // stronger accumulated carrier estimate to the startup hypothesis.
        if(best->burst.score>=stream.carrier_score) {
            stream.carrier_score=best->burst.score;stream.reported_frequency=best->burst.frequency_hz;
        }
        return stream.reported_frequency;
    }
    void publish(Hypothesis& h,bool complete=false,bool flush=true,bool draining=false) {
        if(!h.admitted)return;
        if(draining && !h.committed)return;
        const auto chunk=std::min(search.chunk_bits,bit_limit);
        if(!complete && !flush && h.committed<chunk)return;
        auto& stream=output_stream(h);
        if(stream.owner!=&h) {
            h.burst.bits.erase(h.burst.bits.begin(),h.burst.bits.begin()+static_cast<std::ptrdiff_t>(h.committed));
            h.burst.first_stream_symbol+=h.committed;h.burst.first_sample=h.committed_end;h.committed=0;
            return;
        }
        do {
            const auto count=std::min(h.committed,chunk);
            if(!count && !complete)break;
            if(draining && bursts.size()==search.track_limit)break;
            room_for(count);
            PatternBurst result;
            result.first_sample=h.burst.first_sample;result.first_stream_symbol=h.burst.first_stream_symbol;
            result.stream_first_sample=h.burst.stream_first_sample;result.stream_first_symbol=h.burst.stream_first_symbol;
            result.frequency_hz=carrier_estimate(h,stream);result.stream_phase_samples=h.burst.stream_phase_samples;
            result.bits.assign(h.burst.bits.begin(),h.burst.bits.begin()+static_cast<std::ptrdiff_t>(count));
            result.complete=complete && count==h.committed;
            result.end_sample=count==h.committed?h.committed_end:
                result.first_sample+static_cast<std::uint64_t>(count*code.symbol_samples()/h.rate);
            result.score=h.burst.score;
            result.support_samples=h.burst.support_samples;
            const auto duplicate=std::find_if(bursts.begin(),bursts.end(),[&](const auto& prior) {
                const auto delta=prior.first_sample>result.first_sample?prior.first_sample-result.first_sample:result.first_sample-prior.first_sample;
                return prior.first_stream_symbol==result.first_stream_symbol && prior.complete==result.complete &&
                    delta<code.symbol_samples()/3 && ((!search.hypotheses.empty() && search.frequency_rate_competition) ||
                    std::abs(prior.frequency_hz-result.frequency_hz)<=config.sample_rate/static_cast<double>(code.symbol_samples()));
            });
            const auto emitted_end=result.end_sample;
            if(duplicate!=bursts.end()) {
                if(result.score>duplicate->score) {
                    accounted_bytes=accounted_bytes-duplicate->bits.capacity()+result.bits.capacity();*duplicate=std::move(result);
                }
            } else {
                require(bursts.size()<search.track_limit,"pattern output queue requires draining");
                accounted_bytes+=result.bits.capacity();bursts.push_back(std::move(result));
            }
            h.burst.bits.erase(h.burst.bits.begin(),h.burst.bits.begin()+static_cast<std::ptrdiff_t>(count));
            h.burst.first_stream_symbol+=count;h.burst.first_sample=emitted_end;h.committed-=count;
            stream.last_symbol=h.burst.first_stream_symbol;
            if(complete && !h.committed)stream.ended=true;
            if(!h.committed)break;
        } while(complete || flush || h.committed>=chunk);
    }
    void clear(Hypothesis& h) {
        // Keep the finite clock hypothesis, but release a terminated message's
        // payload allocation so later hypotheses can use the same workspace.
        accounted_bytes-=h.burst.bits.capacity();Bytes{}.swap(h.burst.bits);
        h.burst.complete=false;h.admitted=h.pending_gaps=false;
        h.sum_score=h.pending_score=h.burst.score=0;
        if(!pending_chip_scores.empty())pending_chip_scores[static_cast<std::size_t>(&h-hypotheses.data())]=0;
        h.committed=0;h.gap_slots=0;
        h.sum_support=h.burst.support_samples=0;
    }
    void append(Hypothesis& h,std::uint8_t bit) {
        require(h.burst.bits.size()<bit_limit,"pattern bit retention limit reached");
        if(h.burst.bits.size()==h.burst.bits.capacity()) {
            const auto capacity=h.burst.bits.capacity();const auto wanted=std::min(bit_limit,std::max<std::size_t>(1,capacity*2));
            room_for(wanted);h.burst.bits.reserve(wanted);accounted_bytes+=h.burst.bits.capacity()-capacity;
        }
        h.burst.bits.push_back(bit);
    }
    void mark_pending_gaps(Hypothesis& h) {
        if(!h.pending_gaps) {
            h.gap_slots=h.burst.bits.size()-h.committed;h.burst.bits.resize(h.committed);
        }
        h.sum_score=h.burst.score;h.pending_score=0;h.pending_gaps=true;
        if(!pending_chip_scores.empty())pending_chip_scores[static_cast<std::size_t>(&h-hypotheses.data())]=0;
        h.sum_support=h.burst.support_samples;
    }
    void emit_gap(Hypothesis& h,std::uint64_t resumed_sample) {
        if(!h.gap_slots)return;
        publish(h);
        auto& stream=output_stream(h);
        if(stream.owner!=&h) {
            h.gap_slots=0;h.burst.first_sample=resumed_sample;h.burst.first_stream_symbol=h.index;return;
        }
        require(bursts.size()<search.track_limit,"pattern output queue requires draining");
        PatternBurst event;
        event.first_sample=h.burst.first_sample;event.end_sample=resumed_sample;
        event.first_stream_symbol=h.burst.first_stream_symbol;
        event.stream_first_sample=h.burst.stream_first_sample;event.stream_first_symbol=h.burst.stream_first_symbol;
        event.frequency_hz=carrier_estimate(h,stream);event.score=h.burst.score;
        event.support_samples=h.burst.support_samples;
        event.stream_phase_samples=h.burst.stream_phase_samples;event.missing_slots=h.gap_slots;
        bursts.push_back(std::move(event));h.gap_slots=0;
        h.burst.first_sample=resumed_sample;h.burst.first_stream_symbol=h.index;stream.last_symbol=h.index;
    }
    void complete(Hypothesis& h,std::size_t hypothesis,std::uint64_t end) {
        std::size_t group_count=0;
        const auto groups=phase_groups(h,group_count);
        PatternEvidence e;e.score=-1;std::size_t selected=0;
        require(group_count<=std::numeric_limits<std::uint64_t>::max()-trials,"pattern trial counter overflow");
        trials+=group_count;
        for(std::size_t group=0;group<group_count;++group) {
            const auto& fit=fits(h,hypothesis,group);
            auto a=fit[0].score(),b=fit[1].score();
            if(drift_sections>1) {
                const auto& drift=section_fits(hypothesis,group);
                a=detail::combine_drift_evidence(a,drift[0].score(fit[0],drift_sections,code.chip_samples()),drift_sections);
                b=detail::combine_drift_evidence(b,drift[1].score(fit[1],drift_sections,code.chip_samples()),drift_sections);
            }
            if(differential_window) {
                const auto& local=local_fits(hypothesis,group);
                a=detail::combine_differential_evidence(a,local[0].score(code.symbol_samples(),differential_window),true);
                b=detail::combine_differential_evidence(b,local[1].score(code.symbol_samples(),differential_window),true);
            }
            if(std::max(a,b)>e.score) {
                e={h.observed_start,end,h.index,config.carrier_hz+search.frequency_offsets_hz[h.frequency],
                    std::max(a,b),std::min(a,b),b>a?1U:0U,groups[group].lower};
                selected=group;
            }
        }
        remember(e);
        const auto symbol_support=pattern_symbol_support(
            static_cast<double>(fits(h,hypothesis,selected)[e.bit].count),e.score,code.chip_samples());
        const auto standalone=e.score>=threshold();
        // Unknown slots never select a private phase. Keep the existing clock
        // until an independently confident later symbol resolves the schedule.
        const auto pending_count=h.burst.bits.size()-h.committed;
        const auto next_count=static_cast<double>(pending_count)+1;
        const auto next_score=h.pending_score+e.score;
        const auto next_chain=next_score>next_count?
            next_score-next_count-next_count*std::log(next_score/next_count)-next_count*std::log(2.):0;
        // Preserve a tail whose joint evidence justifies continuation. If it
        // does not, a confident current symbol must still start independently.
        // An unadmitted prefix cannot borrow that later symbol's confidence.
        const auto reliable=e.score>=search.retain_score && e.score-e.alternative_score>=1 &&
            (group_count==1 || standalone);
        const auto can_preserve=h.admitted;
        auto preserve_gap=can_preserve &&
            (h.pending_gaps || !reliable || (standalone && pending_count && next_chain<threshold()));
        if(preserve_gap) {
            // Retain only the admitted clock and symbol positions. Missing
            // slots provide neither bit evidence nor confidence to the track.
            mark_pending_gaps(h);
            h.burst.end_sample=end;
            if(reliable && standalone) {
                h.phase_lower=groups[selected].lower;h.phase_upper=groups[selected].upper;
                h.burst.stream_phase_samples=h.phase_lower;
                emit_gap(h,h.observed_start);append(h,static_cast<std::uint8_t>(e.bit));
                h.sum_score+=e.score;h.committed=h.burst.bits.size();h.committed_end=end;
                h.sum_support+=symbol_support;h.burst.support_samples=h.sum_support;
                h.burst.score=h.sum_score;h.pending_gaps=false;
            } else {
                require(h.gap_slots<std::numeric_limits<std::size_t>::max(),"pattern missing-slot count overflow");++h.gap_slots;
            }
        } else {
            if(standalone && pending_count && (!h.admitted || next_chain<threshold())) {publish(h);clear(h);}
            if(h.burst.bits.size()==bit_limit && e.score>=search.retain_score)
                throw Error("pattern bit retention limit reached");
            // Weak evidence from different possible schedules cannot be added as
            // though it supported one coherent stream. Resolve a split on the
            // current symbol's own evidence before extending its bit chain.
            if(reliable && h.burst.bits.size()<bit_limit) {
                if(standalone) {
                    h.phase_lower=groups[selected].lower;h.phase_upper=groups[selected].upper;
                    h.burst.stream_phase_samples=h.phase_lower;
                }
                if(h.burst.bits.empty()) {
                    if(!h.admitted) {h.burst.stream_first_sample=h.observed_start;h.burst.stream_first_symbol=h.index;}
                    h.burst.first_sample=h.observed_start;h.burst.first_stream_symbol=h.index;
                    h.burst.stream_phase_samples=h.phase_lower;
                    h.burst.frequency_hz=e.frequency_hz;
                    if(!h.admitted){h.sum_score=h.pending_score=h.burst.score=0;h.committed=0;h.gap_slots=0;
                        if(!pending_chip_scores.empty())pending_chip_scores[hypothesis]=0;
                        h.sum_support=h.burst.support_samples=0;}
                }
                append(h,static_cast<std::uint8_t>(e.bit));h.burst.end_sample=end;h.sum_score+=e.score;h.pending_score+=e.score;
                if(!chip_evidence.empty())pending_chip_scores[hypothesis]+=
                    chip_evidence[hypothesis*(alternate_groups+1)+selected][e.bit].score(fits(h,hypothesis,selected)[e.bit]);
                h.sum_support+=symbol_support;
                const auto pending=h.burst.bits.size()-h.committed;
                const auto n=static_cast<double>(pending);
                const auto pending_score=h.admitted || chip_evidence.empty()?h.pending_score:pending_chip_scores[hypothesis];
                const auto chain=pending_score>n?pending_score-n-n*std::log(pending_score/n)-n*std::log(2.):0;
                if((n==1 && standalone) || chain>=threshold()) {
                    h.admitted=true;h.committed=h.burst.bits.size();h.committed_end=end;
                    h.burst.score=h.sum_score;h.pending_score=0;
                    if(!pending_chip_scores.empty())pending_chip_scores[hypothesis]=0;
                    h.burst.support_samples=h.sum_support;
                }
                if(!h.admitted && static_cast<long double>(pending)*code.symbol_samples()/h.rate>=
                    static_cast<long double>(pattern_absence_seconds)*config.sample_rate)clear(h);
            } else {publish(h);clear(h);}
        }
        if(h.admitted && static_cast<long double>(end-h.committed_end)>=
            static_cast<long double>(pattern_absence_seconds)*config.sample_rate) {publish(h,true);clear(h);}
        else publish(h,false,false);
        h.fits={};
        if(!chip_evidence.empty())for(std::size_t group=0;group<=alternate_groups;++group)
            chip_evidence[hypothesis*(alternate_groups+1)+group]={};
        for(std::size_t group=0;group<alternate_groups;++group)
            alternate_fits[hypothesis*alternate_groups+group]={};
        if(drift_sections>1)for(std::size_t group=0;group<=alternate_groups;++group)
            section_fits(hypothesis,group)={};
        if(differential_window)for(std::size_t group=0;group<=alternate_groups;++group)
            local_fits(hypothesis,group)={};
        require(h.index<std::numeric_limits<std::uint64_t>::max(),"pattern stream symbol counter overflow");++h.index;
    }
    std::uint64_t initial_cursor(const Hypothesis& h,std::uint64_t end) const {
        if(h.origin>static_cast<long double>(sample))
            return static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(h.origin)));
        return sample;
    }
    std::uint64_t accumulate(Hypothesis& h,std::size_t hypothesis,std::uint64_t cursor,
                             std::uint64_t end,PatternCode& pattern) {
        const auto symbol_start=this->symbol_start(h);
        const auto symbol_end=clock_boundary(h,code.symbol_samples());
        const auto segment_end=static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(symbol_end)));
        std::size_t group_count=0;
        const auto groups=phase_groups(h,group_count);
        if(!h.fits[0].count)h.observed_start=cursor;
        for(std::size_t group=0;group<group_count;++group) {
            pattern.set_stream_phase_samples(groups[group].lower);
            auto& fit=fits(h,hypothesis,group);
            auto* drift=!drift_fits.empty()?&section_fits(hypothesis,group):nullptr;
            auto* differential=differential_window?&local_fits(hypothesis,group):nullptr;
            auto observed=cursor;
            while(observed<segment_end) {
                const auto within=std::max(0.L,(static_cast<long double>(observed)-symbol_start)*h.rate);
                auto section_end=symbol_end;
                if(drift) {
                    for(auto& item:*drift)advance_drift(item,h,observed);
                    section_end=clock_boundary(h,detail::drift_boundary(
                        (*drift)[0].section+1,code.symbol_samples(),drift_sections));
                }
                if(differential) {
                    for(auto& item:*differential)advance_differential(item,h,observed);
                    const auto boundary=(*differential)[0].window_index<code.symbol_samples()/differential_window?
                        ((*differential)[0].window_index+1)*differential_window:code.symbol_samples();
                    section_end=std::min(section_end,clock_boundary(h,boundary));
                }
                if(shaped) {
                    require(h.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),
                            "pattern chip coordinate overflow");
                    const auto first_chip=h.index*code.chips_per_symbol();
                    const auto left=static_cast<std::size_t>(observed-sample);
                    const auto& bank=banks[h.frequency];
                    if(pulse_segmented) {
                        const auto chip=code.chip_samples(),symbol=code.symbol_samples();
                        const auto local=static_cast<std::uint64_t>(std::floor(within/chip));
                        const auto chip_end=clock_boundary(h,local<symbol/chip?(local+1)*chip:symbol);
                        const auto boundary=std::min(static_cast<long double>(segment_end),
                            std::ceil(std::min(chip_end,section_end)));
                        const auto limit=static_cast<std::uint64_t>(
                            std::max(static_cast<long double>(observed+1),boundary))-observed;
                        const auto offset=(within-static_cast<long double>(local)*chip)/h.rate;
                        const auto duration=static_cast<long double>(chip)/h.rate;
                        auto piece=detail::correlation_pulse_segment(offset,limit,duration);
                        const auto tail=symbol%chip;
                        detail::CorrelationPulseSegment final_piece;
                        const auto final_chip=code.chips_per_symbol()-1;
                        const bool has_final=tail && final_chip>=local && final_chip-local<=8;
                        if(has_final) {
                            // pattern_pulse_each centers a partial final pulse
                            // in its actual duration and scales its energy. Its
                            // shifted table knots need their own span limit.
                            final_piece=detail::correlation_pulse_segment(
                                offset+static_cast<long double>(chip-tail)/(2*h.rate),limit,duration);
                            piece.count=std::min(piece.count,final_piece.count);
                        }
                        std::array<Complex,2> value{},slope{};
                        for(std::size_t j=0;j<detail::correlation_pulse_atoms;++j) {
                            if(j<8 && local<8-j)continue;
                            if(j>8 && local>std::numeric_limits<std::uint64_t>::max()-(j-8))continue;
                            const auto position=j<8?local-(8-j):local+(j-8);
                            if(position>=code.chips_per_symbol())continue;
                            auto a=piece.value[j],b=piece.slope[j];
                            if(has_final && position==final_chip) {
                                const auto scale=std::sqrt(static_cast<double>(tail)/static_cast<double>(chip));
                                a=final_piece.value[j]*scale;b=final_piece.slope[j]*scale;
                            }
                            if(a==0 && b==0)continue;
                            require(position<=std::numeric_limits<std::uint64_t>::max()-first_chip,
                                    "pattern chip address would overflow");
                            const auto pair=pattern.values(first_chip+position);
                            for(unsigned bit=0;bit<2;++bit) {
                                value[bit]+=static_cast<double>(a)*pair[bit];
                                slope[bit]+=static_cast<double>(b)*pair[bit];
                            }
                        }
                        const auto right=left+static_cast<std::size_t>(piece.count);
                        const auto prefix=bank.observations();const auto moments=bank.moments();
                        const auto projection=prefix[right]-prefix[left];
                        const Complex measured{projection.xc,projection.xs};
                        const auto moment=moments[right]-moments[left]-static_cast<double>(left)*measured;
                        const auto first=prefix[left+1]-prefix[left];
                        const Complex square{first.cc-first.ss,2*first.cs};
                        require(piece.count<=block_samples,"affine pulse span exceeds its oscillator block");
                        detail::CorrelationCarrierMoments uncached;
                        const auto* carrier=bank.carrier(piece.count);
                        if(!carrier) {
                            uncached=detail::correlation_carrier_moments(piece.count,2*tau*bank.frequency/config.sample_rate);
                            carrier=&uncached;
                        }
                        for(unsigned bit=0;bit<2;++bit) {
                            const auto contribution=detail::correlation_affine_fit(projection,moment,
                                piece.count,value[bit],slope[bit],square,first.cc+first.ss,
                                *carrier);
                            add_fit(fit[bit],contribution);
                            if(drift)add_fit((*drift)[bit].active,contribution);
                            if(differential)add_fit((*differential)[bit].active,contribution);
                        }
                        ++affine_counts[hypothesis];observed+=piece.count;continue;
                    }
                    const auto projection=bank.observations()[left+1]-bank.observations()[left];
                    // Each alternative schedule fits the same disjoint
                    // raw observations. Its score never borrows samples
                    // or evidence from another possible schedule.
                    const auto phases=pattern.shaped_values(first_chip,static_cast<double>(within));
                    for(unsigned bit=0;bit<2;++bit) {
                        const auto phase=phases[bit];
                        fit[bit].add(projection,phase,1);
                        if(!chip_evidence.empty())chip_evidence[hypothesis*(alternate_groups+1)+group][bit].add(
                            projection,phase,1,detail::CorrelationChipEvidence::index(observed,symbol_start,h.rate,code.chip_samples()));
                        if(drift)(*drift)[bit].active.add(projection,phase,1);
                        if(differential)(*differential)[bit].active.add(projection,phase,1);
                    }
                    ++observed;continue;
                }
                const auto local=static_cast<std::uint64_t>(std::floor(within/code.chip_samples()));
                require(h.index<=(std::numeric_limits<std::uint64_t>::max()-local)/code.chips_per_symbol(),"pattern chip coordinate overflow");
                const auto chip=h.index*code.chips_per_symbol()+local;
                const auto fraction=std::clamp(static_cast<double>(within/code.chip_samples()-local),0.,std::nextafter(1.,0.));
                const auto chip_end=clock_boundary(h,(local+1)*code.chip_samples());
                const auto boundary=std::min(static_cast<long double>(segment_end),std::ceil(std::min(chip_end,section_end)));
                const auto until=static_cast<std::uint64_t>(std::max(static_cast<long double>(observed+1),boundary));
                const auto left=static_cast<std::size_t>(observed-sample),right=static_cast<std::size_t>(until-sample);
                for(unsigned bit=0;bit<2;++bit) {
                    const auto bank_index=config.spreading_mode==SpreadingMode::tone?h.tone_bank_base+bit:h.frequency;
                    const auto& bank=banks[bank_index];
                    auto phase=pattern.value(chip,bit,fraction);
                    if(config.spreading_mode==SpreadingMode::tone) {
                        const auto tone=bank.frequency-config.carrier_hz-search.frequency_offsets_hz[h.frequency];
                        phase*=std::polar(1.,-static_cast<double>(std::remainder(static_cast<long double>(observed)*tau*tone/config.sample_rate,static_cast<long double>(tau))));
                    }
                    const auto projection=bank.observations()[right]-bank.observations()[left];
                    fit[bit].add(projection,phase,right-left);
                    if(!chip_evidence.empty())chip_evidence[hypothesis*(alternate_groups+1)+group][bit].add(projection,phase,right-left,local);
                    if(drift)(*drift)[bit].active.add(projection,phase,right-left);
                    if(differential)(*differential)[bit].active.add(projection,phase,right-left);
                }
                observed=until;
            }
        }
        return segment_end;
    }
    void record_point(const Projection& measured) {
        const Complex point{measured.xc,measured.xs};
        const auto delta=std::abs(previous_point)>1e-20?point*std::conj(previous_point)/std::abs(previous_point):point;
        if(point_count==points.size()){point_begin=(point_begin+1)%points.size();--point_count;}
        points[(point_begin+point_count++)%points.size()]=delta;previous_point=point;
    }
    void finish_pulse_cell(PulseLattice& lattice,std::stop_token stop) {
        auto& cell=lattice.pending;cell.end=lattice.next;
        {
            WorkTimer timer(options.measure_work,work.kernel_seconds,work.kernel_cpu_seconds);
            cell.gram=lattice.kernel.evaluate(lattice.first_offset,cell.count,
                lattice.carrier_square,lattice.carrier_norm,stop);
        }
        require(lattice.cells.size()<lattice.cells.capacity(),"pulse projection exceeds bounded cell tile");
        lattice.cells.push_back(cell);++work.cells;
        require(cell.chip<std::numeric_limits<std::int64_t>::max(),"pulse cell coordinate overflow");
        const auto next_chip=cell.chip+1;cell={};cell.chip=next_chip;
        require(next_chip<std::numeric_limits<std::int64_t>::max(),"pulse cell coordinate overflow");
        lattice.start=lattice.anchor+static_cast<long double>(next_chip)*code.chip_samples()/lattice.rate;
        lattice.next=static_cast<std::uint64_t>(std::ceil(lattice.anchor+
            static_cast<long double>(next_chip+1)*code.chip_samples()/lattice.rate));
    }
    void project_pulse_moments(std::size_t count,std::stop_token stop) {
        for(auto& lattice:pulse_lattices) {
            cancelled(stop);lattice.cells.clear();
            const auto& bank=banks[lattice.frequency];
            const auto prefix=bank.observations();const auto moments=bank.moments();
            const auto duration=static_cast<long double>(code.chip_samples())/lattice.rate;
            for(std::size_t i=0;i<count;) {
                const auto observed=sample+i;auto& cell=lattice.pending;
                if(!cell.count) {
                    cell.first=observed;lattice.first_offset=static_cast<long double>(observed)-lattice.start;
                    const auto projection=prefix[i+1]-prefix[i];
                    lattice.carrier_square={projection.cc-projection.ss,2*projection.cs};
                    lattice.carrier_norm=projection.cc+projection.ss;
                }
                if(!lattice.segment_observed) {
                    lattice.segment=detail::correlation_pulse_segment(
                        static_cast<long double>(observed)-lattice.start,lattice.next-observed,duration);
                    lattice.segment_first=observed;lattice.segment_sum={};lattice.segment_moment={};
                }
                const auto take=static_cast<std::size_t>(std::min<std::uint64_t>(
                    count-i,lattice.segment.count-lattice.segment_observed));
                require(take>0,"empty pulse moment segment");
                const auto projection=prefix[i+take]-prefix[i];
                const std::complex<long double> measured{projection.xc,projection.xs};
                const auto moment=moments[i+take]-moments[i];
                lattice.segment_sum+=measured;
                // Prefix moments use small, block-local coordinates. Shift to
                // the current pulse piece, never to an unbounded stream time.
                lattice.segment_moment+=std::complex<long double>{moment.real(),moment.imag()}+
                    (static_cast<long double>(sample)-lattice.segment_first)*measured;
                lattice.segment_observed+=take;cell.count+=take;cell.energy+=projection.energy;i+=take;
                if(lattice.segment_observed==lattice.segment.count) {
                    for(std::size_t j=0;j<detail::correlation_pulse_atoms;++j) {
                        const auto dot=lattice.segment.value[j]*lattice.segment_sum+
                            lattice.segment.slope[j]*lattice.segment_moment;
                        cell.dot[j]+=Complex{static_cast<double>(dot.real()),static_cast<double>(dot.imag())};
                    }
                    lattice.segment_observed=0;++work.segments;
                }
                if(sample+i==lattice.next) {
                    require(!lattice.segment_observed,"pulse moment crossed cell boundary");
                    finish_pulse_cell(lattice,stop);
                }
            }
        }
    }
    void project_pulse_cells(std::size_t count,std::stop_token stop) {
        if(work.backend==PatternCorrelationBackend::pulse_moments){project_pulse_moments(count,stop);return;}
        const auto chip=code.chip_samples();
        for(auto& lattice:pulse_lattices) {
            cancelled(stop);lattice.cells.clear();
            const auto prefix=banks[lattice.frequency].observations();
            for(std::size_t i=0;i<count;++i) {
                const auto observed=sample+i;
                auto& cell=lattice.pending;
                const auto projection=prefix[i+1]-prefix[i];
                if(!cell.count) {
                    cell.first=observed;lattice.first_offset=static_cast<long double>(observed)-lattice.start;
                    lattice.carrier_square={projection.cc-projection.ss,2*projection.cs};
                    lattice.carrier_norm=projection.cc+projection.ss;
                }
                const auto q=(static_cast<long double>(observed)-lattice.start)*lattice.rate/chip-.5L;
                const Complex measured{projection.xc,projection.xs};
                for(std::size_t j=0;j<detail::correlation_pulse_atoms;++j)
                    cell.dot[j]+=pattern_pulse(static_cast<double>(q+8-static_cast<long double>(j)))*measured;
                cell.energy+=projection.energy;++cell.count;
                if(observed+1==lattice.next) {
                    finish_pulse_cell(lattice,stop);
                }
            }
        }
    }
    static void add_fit(Fit& target,const Fit& value) {
        target.xc+=value.xc;target.xs+=value.xs;target.cc+=value.cc;target.ss+=value.ss;
        target.cs+=value.cs;target.energy+=value.energy;target.count+=value.count;
    }
    void accumulate_pulse_cells(std::stop_token stop) {
        const auto chips=code.chips_per_symbol();
        for(std::size_t hypothesis=0;hypothesis<hypotheses.size();++hypothesis) {
            cancelled(stop);auto& h=hypotheses[hypothesis];
            const auto& address=pulse_addresses[hypothesis];
            const auto& lattice=pulse_lattices[address.lattice];
            for(const auto& cell:lattice.cells) {
                require(h.index<=(static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())-
                    static_cast<std::uint64_t>(address.shift))/chips,"pattern chip coordinate overflow");
                const auto first=address.shift+static_cast<std::int64_t>(h.index*chips);
                if(cell.chip<first)continue;
                require(cell.chip-first<static_cast<std::int64_t>(chips),"pulse projection skipped a symbol endpoint");
                const auto local=cell.chip-first;
                if(!h.fits[0].count)h.observed_start=cell.first;
                std::size_t group_count=0;const auto groups=phase_groups(h,group_count);
                for(std::size_t group=0;group<group_count;++group) {
                    code.set_stream_phase_samples(groups[group].lower);
                    std::array<std::array<Complex,detail::correlation_pulse_atoms>,2> coefficients{};
                    for(std::size_t j=0;j<detail::correlation_pulse_atoms;++j) {
                        const auto position=local+static_cast<std::int64_t>(j)-8;
                        if(position<0 || position>=static_cast<std::int64_t>(chips))continue;
                        const auto values=code.values(h.index*chips+static_cast<std::uint64_t>(position));
                        for(unsigned bit=0;bit<2;++bit)coefficients[bit][j]=values[bit];
                    }
                    auto& fit=fits(h,hypothesis,group);
                    auto* drift=!drift_fits.empty()?&section_fits(hypothesis,group):nullptr;
                    auto* differential=differential_window?&local_fits(hypothesis,group):nullptr;
                    for(unsigned bit=0;bit<2;++bit) {
                        const auto contribution=cell.fit(coefficients[bit]);add_fit(fit[bit],contribution);
                        if(drift) {
                            advance_drift((*drift)[bit],h,cell.first);
                            add_fit((*drift)[bit].active,contribution);
                        }
                        if(differential) {
                            advance_differential((*differential)[bit],h,cell.first);
                            add_fit((*differential)[bit].active,contribution);
                        }
                    }
                }
                if(local+1==static_cast<std::int64_t>(chips))complete(h,hypothesis,cell.end);
            }
        }
    }
    std::size_t process_batch(std::span<const float> input,std::stop_token stop) {
        // Preserve the scalar path as a reference, and never collect samples
        // across a caller's progress poll. Small/tight-workspace pushes retain
        // the original one-block path. No completion is allowed in this batch.
        if(!pulse_lattices.empty() || !search.hypotheses.empty() || worker_codes.size()<2 || input.size()<=block_samples)return 0;
        constexpr std::size_t max_blocks=64,max_lanes=65536;
        using Lane=detail::CorrelationLane;
        using Block=detail::CorrelationBlock;
        const auto retained=sizeof(PatternCorrelator)+working_bytes()+drift_reserved;
        if(retained>=budget)return 0;
        const auto spare=budget-retained;
        const auto per_block=banks.size()*(block_samples+1)*sizeof(Projection)+sizeof(Block);
        const auto frequencies_bytes=banks.size()*sizeof(double);
        const auto minimum_lanes=std::min<std::size_t>(64,hypotheses.size());
        const auto minimum_bytes=frequencies_bytes+minimum_lanes*sizeof(Lane);
        if(spare<=minimum_bytes || (spare-minimum_bytes)/per_block<2)return 0;
        const auto block_capacity=std::min(max_blocks,(spare-minimum_bytes)/per_block);
        const auto proposed=std::min(input.size(),block_capacity*block_samples);
        auto boundary=std::numeric_limits<long double>::infinity();
        for(const auto& h:hypotheses) {
            const auto end=std::ceil(h.origin+(static_cast<long double>(h.index)+1)*code.symbol_samples()/h.rate);
            boundary=std::min(boundary,end);
        }
        std::size_t consumed=0,block_count=0;
        while(consumed<proposed) {
            const auto count=std::min(block_samples,proposed-consumed);
            if(static_cast<long double>(sample+consumed+count)>=boundary)break;
            consumed+=count;++block_count;
        }
        if(block_count<2)return 0;
        const auto projection_count=block_count*banks.size()*(block_samples+1);
        const auto fixed_bytes=projection_count*sizeof(Projection)+block_count*sizeof(Block)+frequencies_bytes;
        const auto lane_capacity=std::min({max_lanes,hypotheses.size(),(spare-fixed_bytes)/sizeof(Lane)});
        work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,retained+fixed_bytes+lane_capacity*sizeof(Lane));
        // Exact-size temporary arrays make the peak reservation independent of
        // vector growth policy. They disappear before ordered completion can
        // grow payloads; idle footprints and receiver-bank admission stay intact.
        auto projections=std::make_unique<Projection[]>(projection_count);
        auto blocks=std::make_unique<Block[]>(block_count);
        auto frequencies=std::make_unique<double[]>(banks.size());
        auto lanes=std::make_unique<Lane[]>(lane_capacity);
        WorkTimer frontend_timer(options.measure_work,work.frontend_seconds,work.frontend_cpu_seconds);
        for(std::size_t f=0;f<banks.size();++f)frequencies[f]=banks[f].frequency;
        std::size_t offset=0,row_offset=0;
        for(std::size_t b=0;b<block_count;++b) {
            cancelled(stop);
            const auto count=std::min(block_samples,consumed-offset);
            blocks[b]={sample+offset,count,row_offset};
            for(std::size_t f=0;f<banks.size();++f) {
                auto prefix=std::span(projections.get()+row_offset+f*(count+1),count+1);
                prefix[0]={};
                auto oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(sample+offset)*tau*
                    banks[f].frequency/config.sample_rate,static_cast<long double>(tau))));
                const auto step=std::polar(1.,tau*banks[f].frequency/config.sample_rate);
                for(std::size_t i=0;i<count;++i) {
                    const auto c=oscillator.real(),s=oscillator.imag(),x=static_cast<double>(input[offset+i]);
                    auto p=prefix[i];p.xc+=x*c;p.xs+=x*s;p.cc+=c*c;p.ss+=s*s;p.cs+=c*s;p.energy+=x*x;
                    prefix[i+1]=p;oscillator*=step;
                }
            }
            row_offset+=banks.size()*(count+1);offset+=count;
        }
        frontend_timer.finish();
        WorkTimer search_timer(options.measure_work,work.search_seconds,work.search_cpu_seconds);
        detail::CorrelationBatch batch{
            {config.stream_epoch,code.symbol_samples(),code.chip_samples(),code.chips_per_symbol(),phase_step,
             config.sample_rate,search.frequency_offsets_hz.size(),config.carrier_hz,shaped,
             config.spreading_mode==SpreadingMode::tone,
             {static_cast<std::uint32_t>(config.spreading_mode),config.scramble,config.dsss,
              config.spreading_seed,config.dsss_seed},drift_fits.empty()?1:drift_sections,differential_window,!chip_evidence.empty()},
            {blocks.get(),block_count},{projections.get(),row_offset},{frequencies.get(),banks.size()},search.frequency_offsets_hz};
        // Numeric tiles contain no PatternBurst, heap-owned input, or references
        // to peer admission state. Device implementations can operate on these
        // same indexed lanes without creating one host thread per hypothesis.
        for(std::size_t first=0;first<hypotheses.size();) {
            const auto count=std::min(lane_capacity,hypotheses.size()-first);
            for(std::size_t i=0;i<count;++i) {
                const auto& h=hypotheses[first+i];auto& lane=lanes[i];
                lane.origin=h.origin;lane.rate=h.rate;lane.index=h.index;lane.observed_start=h.observed_start;
                lane.phase_lower=h.phase_lower;lane.phase_upper=h.phase_upper;
                lane.frequency=h.frequency;lane.rate_index=h.rate_index;lane.tone_bank_base=h.tone_bank_base;lane.fits[0]=h.fits;
                for(std::size_t group=0;group<alternate_groups;++group)
                    lane.fits[group+1]=alternate_fits[(first+i)*alternate_groups+group];
                if(!chip_evidence.empty())for(std::size_t group=0;group<=alternate_groups;++group)
                    lane.chip_evidence[group]=chip_evidence[(first+i)*(alternate_groups+1)+group];
                if(!drift_fits.empty())for(std::size_t group=0;group<=alternate_groups;++group)
                    lane.drift_fits[group]=section_fits(first+i,group);
                if(differential_window)for(std::size_t group=0;group<=alternate_groups;++group)
                    lane.differential_fits[group]=local_fits(first+i,group);
            }
            detail::accumulate_correlator_cpu(batch,{lanes.get(),count},worker_codes,stop);
            for(std::size_t i=0;i<count;++i) {
                auto& h=hypotheses[first+i];const auto& lane=lanes[i];
                h.observed_start=lane.observed_start;h.fits=lane.fits[0];
                for(std::size_t group=0;group<alternate_groups;++group)
                    alternate_fits[(first+i)*alternate_groups+group]=lane.fits[group+1];
                if(!chip_evidence.empty())for(std::size_t group=0;group<=alternate_groups;++group)
                    chip_evidence[(first+i)*(alternate_groups+1)+group]=lane.chip_evidence[group];
                if(!drift_fits.empty())for(std::size_t group=0;group<=alternate_groups;++group)
                    section_fits(first+i,group)=lane.drift_fits[group];
                if(differential_window)for(std::size_t group=0;group<=alternate_groups;++group)
                    local_fits(first+i,group)=lane.differential_fits[group];
            }
            first+=count;
        }
        for(std::size_t b=0;b<block_count;++b)
            record_point(projections[blocks[b].projection_offset+blocks[b].count]);
        const auto& last=blocks[block_count-1];
        for(std::size_t f=0;f<banks.size();++f)
            std::copy_n(projections.get()+last.projection_offset+f*(last.count+1),last.count+1,banks[f].prefix.begin());
        sample+=consumed;work.samples+=consumed;
        return consumed;
    }
    void process(std::span<const float> input,std::stop_token stop,detail::CorrelationProjectionCache* cache) {
        struct ClearSharedViews {
            std::vector<Bank>& banks;
            ~ClearSharedViews(){for(auto& bank:banks){bank.shared_prefix={};bank.shared_moment={};}}
        } clear_shared{banks};
        WorkTimer frontend_timer(options.measure_work,work.frontend_seconds,work.frontend_cpu_seconds);
        for(std::size_t b=0;b<banks.size();++b) {
            cancelled(stop);auto& bank=banks[b];bank.shared_prefix={};bank.shared_moment={};
            if(cache && (work.backend==PatternCorrelationBackend::pulse_moments || pulse_segmented)) {
                const auto shared=cache->get(input,sample,config.sample_rate,bank.frequency,block_samples,true,stop);
                if(shared){bank.shared_prefix=shared.prefix;bank.shared_moment=shared.first_moment;continue;}
            }
            bank.prefix[0]={};
            if(!bank.first_moment.empty())bank.first_moment[0]={};
            auto oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(sample)*tau*bank.frequency/config.sample_rate,static_cast<long double>(tau))));
            const auto step=std::polar(1.,tau*bank.frequency/config.sample_rate);
            for(std::size_t i=0;i<input.size();++i) {
                const auto c=oscillator.real(),s=oscillator.imag(),x=static_cast<double>(input[i]);
                auto p=bank.prefix[i];p.xc+=x*c;p.xs+=x*s;p.cc+=c*c;p.ss+=s*s;p.cs+=c*s;p.energy+=x*x;
                if(!bank.first_moment.empty())bank.first_moment[i+1]=bank.first_moment[i]+static_cast<double>(i)*Complex{x*c,x*s};
                bank.prefix[i+1]=p;oscillator*=step;
            }
        }
        const auto end=sample+input.size();
        work.samples+=input.size();
        if(!pulse_lattices.empty()) {
            const auto cells_before=work.cells;
            project_pulse_cells(input.size(),stop);frontend_timer.finish();
            WorkTimer search_timer(options.measure_work,work.search_seconds,work.search_cpu_seconds);
            // There is no hypothesis work before a shared chip cell is ready.
            // In particular a high PCM rate must not rescan the timing/key
            // candidates once per tiny oscillator block of an hours-long bit.
            if(work.cells!=cells_before)accumulate_pulse_cells(stop);
            record_point(banks.front().observations()[input.size()]);sample=end;room_for(0);return;
        }
        frontend_timer.finish();
        WorkTimer search_timer(options.measure_work,work.search_seconds,work.search_cpu_seconds);
        const auto parallel=worker_codes.size()>1;
        if(parallel)detail::parallel_search(hypotheses.size(),worker_codes.size(),[&](std::size_t worker,std::size_t hypothesis) {
            cancelled(stop);auto& h=hypotheses[hypothesis];
            const auto cursor=initial_cursor(h,end);
            const auto symbol_end=clock_boundary(h,code.symbol_samples());
            if(cursor<end && static_cast<long double>(cursor)<symbol_end)
                accumulate(h,hypothesis,cursor,end,worker_codes[worker]);
        });
        for(std::size_t hypothesis=0;hypothesis<hypotheses.size();++hypothesis) {
            auto& h=hypotheses[hypothesis];
            cancelled(stop);auto cursor=initial_cursor(h,end);
            // Only independent fit accumulation was moved ahead. Trial counts,
            // phase selection, peer ownership and every publication retain the
            // original serial hypothesis/complete-symbol order.
            if(parallel && cursor<end) {
                const auto symbol_end=clock_boundary(h,code.symbol_samples());
                if(static_cast<long double>(cursor)<symbol_end) {
                    cursor=static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(symbol_end)));
                    if(static_cast<long double>(cursor)>=symbol_end)complete(h,hypothesis,cursor);
                }
            }
            while(cursor<end) {
                const auto symbol_end=clock_boundary(h,code.symbol_samples());
                if(static_cast<long double>(cursor)>=symbol_end) { complete(h,hypothesis,cursor);continue; }
                cursor=accumulate(h,hypothesis,cursor,end,code);
                if(static_cast<long double>(cursor)>=symbol_end)complete(h,hypothesis,cursor);
            }
        }
        if(pulse_segmented)work.segments=std::accumulate(affine_counts.begin(),affine_counts.end(),std::uint64_t{0});
        record_point(banks.front().observations()[input.size()]);
        sample=end;
        room_for(0);
    }
};

PatternCorrelator::PatternCorrelator(Config c,PatternSearch search,std::size_t bytes,PatternCorrelatorOptions options)
    :impl_(std::make_unique<Impl>(c,std::move(search),bytes,options)){}
PatternCorrelator::~PatternCorrelator()=default;
PatternCorrelator::PatternCorrelator(PatternCorrelator&&) noexcept=default;
PatternCorrelator& PatternCorrelator::operator=(PatternCorrelator&&) noexcept=default;
void PatternCorrelator::push(std::span<const float> samples,std::stop_token stop,detail::CorrelationProjectionCache* cache) {
    auto& s=*impl_;cancelled(stop);require(!s.finished,"pattern capture already finished");
    require(samples.size()<=std::numeric_limits<std::uint64_t>::max()-s.sample,"pattern sample counter overflow");
    if(s.options.raw_reference || !cache || !cache->covers(samples))
        for(auto value:samples)require(std::isfinite(value),"nonfinite pattern sample");
    // Idle epochs keep their original footprint so parallel scratch cannot
    // displace other clock/key hypotheses from a shared receiver bank.
    struct ReleaseWorkers {
        Impl& state;
        ~ReleaseWorkers() {state.drop_workers();}
    } release{s};
    if(!samples.empty()){s.prepare_drift(s.sample+samples.size());s.prepare_workers();}
    for(std::size_t offset=0;offset<samples.size();) {
        auto count=s.process_batch(samples.subspan(offset),stop);
        if(!count) {
            count=std::min(s.block_samples,samples.size()-offset);
            s.process(samples.subspan(offset,count),stop,cache);
        }
        offset+=count;
    }
}
void PatternCorrelator::finish(std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.finished)return;
    for(auto& h:s.hypotheses) {s.publish(h);s.clear(h);}
    s.finished=true;
}
std::vector<PatternBurst> PatternCorrelator::take_bursts(){
    auto& s=*impl_;
    // A consumer drain is also a presentation boundary. Publish every
    // accepted decision available now, even when a whole message is much
    // shorter than chunk_bits or each symbol takes hours. Draining does not
    // admit weak decisions, release the clock, or imply physical completion.
    for(auto& h:s.hypotheses)s.publish(h,false,true,true);
    auto result=std::move(s.bursts);s.bursts={};s.bursts.reserve(s.search.track_limit);
    s.accounted_bytes=s.working_bytes()+s.drift_reserved;return result;
}
PatternBurst PatternCorrelator::provisional()const {
    const auto& h=impl_->hypotheses;
    const auto best=std::max_element(h.begin(),h.end(),[](const auto& a,const auto& b){return (a.admitted?a.sum_score:-1)<(b.admitted?b.sum_score:-1);});
    if(best==h.end() || !best->admitted)return {};
    auto result=best->burst;result.bits.resize(best->committed);result.end_sample=best->committed_end;
    result.complete=false;return result;
}
std::vector<PatternEvidence> PatternCorrelator::candidates()const{return impl_->history;}
std::vector<PatternEvidence> PatternCorrelator::candidates(std::size_t limit)const{
    const auto& history=impl_->history;
    return {history.end()-static_cast<std::ptrdiff_t>(std::min(limit,history.size())),history.end()};
}
std::vector<Complex> PatternCorrelator::take_chip_constellation() {
    auto& s=*impl_;std::vector<Complex> result;result.reserve(s.point_count);
    for(std::size_t i=0;i<s.point_count;++i)result.push_back(s.points[(s.point_begin+i)%s.points.size()]);
    s.point_begin=s.point_count=0;return result;
}
bool PatternCorrelator::acquiring()const {return !impl_->finished && std::any_of(impl_->hypotheses.begin(),impl_->hypotheses.end(),[](const auto& h){return h.admitted || !h.burst.bits.empty();});}
bool PatternCorrelator::synchronized()const{return std::any_of(impl_->hypotheses.begin(),impl_->hypotheses.end(),[](const auto& h){return h.admitted;});}
bool PatternCorrelator::initial_search_complete()const {
    const auto& s=*impl_;
    return std::all_of(s.hypotheses.begin(),s.hypotheses.end(),[&](const auto& h) {
        // A negative origin can leave the first scored symbol truncated at
        // capture start. Wait for the next full symbol in that hypothesis's
        // own rate. index advances only after complete() scores every phase;
        // clearing noise candidates or flushing EOF never advances it.
        const auto first_full=std::ceil(std::max(0.L,-h.origin)*h.rate/s.code.symbol_samples());
        return static_cast<long double>(h.index)>first_full;
    });
}
bool PatternCorrelator::drift_tolerant()const{return impl_->drift_sections>1;}
std::size_t PatternCorrelator::working_bytes()const{return sizeof(PatternCorrelator)+impl_->working_bytes();}
std::size_t PatternCorrelator::projection_cache_headroom(std::size_t input_samples)const{return impl_->projection_cache_headroom(input_samples);}
std::size_t PatternCorrelator::reserved_workspace_bytes()const{return sizeof(PatternCorrelator)+impl_->accounted_bytes;}
void PatternCorrelator::set_workspace_bytes(std::size_t bytes) {
    if(bytes<working_bytes())impl_->drop_workers();
    if(impl_->differential_window && bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes) {
        decltype(impl_->differential_fits)().swap(impl_->differential_fits);
        impl_->differential_window=0;
        impl_->accounted_bytes=impl_->working_bytes()+impl_->drift_reserved;
    }
    if(impl_->drift_sections>1 && bytes<sizeof(PatternCorrelator)+impl_->accounted_bytes) {
        // Whole-symbol coherent fits have always been retained; reducing an
        // optional detector's reservation never resets physical reception.
        decltype(impl_->drift_fits)().swap(impl_->drift_fits);
        impl_->drift_reserved=0;impl_->drift_sections=1;
        impl_->accounted_bytes=impl_->working_bytes();
    }
    require(bytes>=working_bytes(),"DSP workspace is smaller than streaming pattern state");impl_->budget=bytes;
}
Diagnostics PatternCorrelator::diagnostics()const {
    const auto burst=provisional();Diagnostics result;result.bit_rate=static_cast<double>(impl_->config.sample_rate)/static_cast<double>(impl_->code.symbol_samples());
    result.sample_offset=static_cast<std::size_t>(burst.first_sample);result.pattern_score=burst.score;return result;
}
PatternCorrelatorWork PatternCorrelator::work()const {
    auto result=impl_->work;result.drift_sections=impl_->drift_sections;
    result.differential_window_samples=impl_->differential_window;
    return result;
}
}
