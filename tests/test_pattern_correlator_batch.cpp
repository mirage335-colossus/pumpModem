#include "../src/pattern_correlator_batch.hpp"
#include "datapump/pattern_pulse.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;

namespace {
void check(bool condition,const char* message) {if(!condition)throw Error(message);}
template<class Action> void rejects(Action action,const char* message) {
    try {action();}catch(const Error&){return;}
    throw Error(message);
}

struct Fixture {
    Config config;
    CorrelationGeometry geometry;
    std::vector<CorrelationBlock> blocks;
    std::vector<CorrelationProjection> projections;
    std::vector<double> frequencies,offsets{0,1.5};

    explicit Fixture(bool shaped=false,bool tone=false,bool keyed=false,double bandwidth=1200) {
        config.bandwidth_hz=bandwidth;
        config.pulse_shaping=shaped;
        config.spreading_mode=tone?SpreadingMode::tone:SpreadingMode::pattern;
        config.scramble=config.dsss=keyed;
        config.stream_epoch=8731;
        config.spreading_seed[2]=37;config.dsss_seed[3]=53;
        PatternCode pattern(config,config.stream_epoch);
        if(bandwidth==1) {
            const auto step=.25*config.sample_rate/static_cast<double>(pattern.symbol_samples());
            offsets={0,-step,step,-2*step,2*step};
        }
        geometry={config.stream_epoch,pattern.symbol_samples(),pattern.chip_samples(),
                  pattern.chips_per_symbol(),1,config.sample_rate,offsets.size(),
                  config.carrier_hz,shaped,tone,
                  {static_cast<std::uint32_t>(config.spreading_mode),config.scramble,config.dsss,
                   config.spreading_seed,config.dsss_seed}};
        geometry.guard_chains=keyed && !tone;
        if(tone) {
            const auto deviation=config.sample_rate/(4.*static_cast<double>(geometry.chip_samples));
            for(const auto rate:{1.,1.001})for(const auto offset:offsets)
                for(const auto sign:{-1.,1.})frequencies.push_back(config.carrier_hz+offset+sign*deviation*rate);
        } else for(const auto offset:offsets)frequencies.push_back(config.carrier_hz+offset);
        constexpr std::array<float,8> samples{.2F,-.1F,.7F,.125F,-.25F,.5F,-.625F,.375F};
        for(const auto count:{5U,3U}) {
            const auto sample=blocks.empty()?0:blocks.back().sample+blocks.back().count;
            blocks.push_back({sample,count,projections.size()});
            for(const auto frequency:frequencies) {
                CorrelationProjection prefix;
                projections.push_back(prefix);
                auto oscillator=std::polar(1.,static_cast<double>(std::remainder(
                    static_cast<long double>(sample)*2*std::numbers::pi*frequency/config.sample_rate,
                    static_cast<long double>(2*std::numbers::pi))));
                const auto step=std::polar(1.,2*std::numbers::pi*frequency/config.sample_rate);
                for(std::size_t i=0;i<count;++i) {
                    const auto c=oscillator.real(),s=oscillator.imag(),x=static_cast<double>(samples[sample+i]);
                    prefix.xc+=x*c;prefix.xs+=x*s;prefix.cc+=c*c;prefix.ss+=s*s;prefix.cs+=c*s;prefix.energy+=x*x;
                    projections.push_back(prefix);oscillator*=step;
                }
            }
        }
    }
    CorrelationBatch batch() const {return {geometry,blocks,projections,frequencies,offsets};}
    std::vector<PatternCode> workers(std::size_t count) const {
        std::vector<PatternCode> result;result.reserve(count);
        for(std::size_t i=0;i<count;++i)result.emplace_back(config,config.stream_epoch);
        return result;
    }
    std::vector<CorrelationLane> lanes(std::size_t count) const {
        std::vector<CorrelationLane> result(count);
        for(std::size_t i=0;i<count;++i) {
            auto& lane=result[i];lane.index=i;
            lane.rate=i%2?1.001L:1.L;
            lane.origin=-static_cast<long double>(i)*geometry.symbol_samples/lane.rate-static_cast<long double>(i%3);
            lane.frequency=i%offsets.size();lane.rate_index=geometry.tone?i%2:0;
            lane.phase_lower=lane.phase_upper=i%7;
            lane.observed_start=100000+i;
            for(auto& fit:lane.fits[0]) {fit.count=1;fit.xc=static_cast<double>(i)/32768.;}
            // Unused phase groups retain distinct markers for logical identity.
            lane.fits[2][1].energy=static_cast<double>(i)+.25;
        }
        return result;
    }
};

bool same_fit(const CorrelationFit& a,const CorrelationFit& b) {
    return a.xc==b.xc && a.xs==b.xs && a.cc==b.cc && a.ss==b.ss && a.cs==b.cs &&
           a.energy==b.energy && a.count==b.count && a.score()==b.score();
}
void same_lanes(std::span<const CorrelationLane> a,std::span<const CorrelationLane> b) {
    check(a.size()==b.size(),"correlation backend changed the logical lane count");
    for(std::size_t i=0;i<a.size();++i) {
        const auto& x=a[i];const auto& y=b[i];
        check(x.origin==y.origin && x.rate==y.rate && x.index==y.index && x.observed_start==y.observed_start &&
              x.phase_lower==y.phase_lower && x.phase_upper==y.phase_upper && x.frequency==y.frequency &&
              x.rate_index==y.rate_index,"correlation backend changed stable logical lane coordinates");
        for(std::size_t group=0;group<x.fits.size();++group)for(std::size_t bit=0;bit<2;++bit)
        {
            check(same_fit(x.fits[group][bit],y.fits[group][bit]),"worker count or block tiling changed correlation arithmetic");
            const auto& a=x.chip_evidence[group][bit];const auto& b=y.chip_evidence[group][bit];
            check(same_fit(a.cell,b.cell) && a.cell_index==b.cell_index && a.projected_rank==b.projected_rank &&
                  a.projected_energy==b.projected_energy,
                  "worker count or block tiling changed private chain evidence");
        }
    }
}

void exact_workers_and_tiles(bool shaped,bool tone,bool keyed,std::size_t count) {
    Fixture fixture(shaped,tone,keyed);
    const auto original=fixture.lanes(count);
    auto serial=original,parallel=original,tiled=original;
    auto one=fixture.workers(1),three=fixture.workers(3);
    const auto one_bytes=one[0].working_bytes();
    accumulate_correlator_cpu(fixture.batch(),serial,one,{});
    accumulate_correlator_cpu(fixture.batch(),parallel,three,{});
    auto batch=fixture.batch();
    for(const auto& block:fixture.blocks) {
        batch.blocks={&block,1};
        accumulate_correlator_cpu(batch,tiled,three,{});
    }
    same_lanes(serial,parallel);same_lanes(serial,tiled);
    check(one.size()==1 && three.size()==3 && one[0].working_bytes()==one_bytes,
          "logical lane count must not grow CPU worker scratch");
    for(std::size_t i=0;i<parallel.size();++i) {
        const auto& lane=parallel[i];
        check(lane.index==i && lane.observed_start==100000+i && lane.fits[2][1].energy==static_cast<double>(i)+.25,
              "range dispatch must retain logical output identity");
        check(lane.fits[0][0].count==9 && lane.fits[0][1].count==9,
              "every logical lane must accumulate every original sample exactly once");
    }
}

void phase_groups_and_observed_start() {
    Fixture fixture(false,false,true);
    CorrelationLane lane;lane.origin=.25L;lane.observed_start=999;
    lane.phase_lower=630;lane.phase_upper=1290;
    std::vector<CorrelationLane> serial(33,lane),parallel=serial;
    auto one=fixture.workers(1),three=fixture.workers(3);
    accumulate_correlator_cpu(fixture.batch(),serial,one,{});
    accumulate_correlator_cpu(fixture.batch(),parallel,three,{});
    same_lanes(serial,parallel);
    for(const auto& result:parallel) {
        check(result.observed_start==1,"new lanes must retain their actual first observed sample");
        for(const auto& group:result.fits)for(const auto& fit:group)
            check(fit.count==7,"all three clock phase groups must accumulate the same observed extent");
    }
}

void narrow_bank_with_future_origins() {
    Fixture fixture(true,false,false,1.);
    auto original=fixture.lanes(75);
    for(std::size_t i=0;i<original.size();++i) {
        auto& lane=original[i];
        lane.index=0;lane.phase_lower=lane.phase_upper=0;lane.frequency=i/15;
        lane.rate=1;
        // The one-hertz clock bank straddles the current sample. Contiguous
        // groups have very different amounts of work while origins activate.
        lane.origin=-7.L*fixture.config.sample_rate+static_cast<long double>(i%15)*6000;
    }
    auto serial=original,parallel=original,tiled=original;
    auto one=fixture.workers(1),eleven=fixture.workers(11);
    accumulate_correlator_cpu(fixture.batch(),serial,one,{});
    accumulate_correlator_cpu(fixture.batch(),parallel,eleven,{});
    for(std::size_t first=0;first<tiled.size();first+=7) {
        const auto count=std::min<std::size_t>(7,tiled.size()-first);
        accumulate_correlator_cpu(fixture.batch(),std::span(tiled).subspan(first,count),eleven,{});
    }
    same_lanes(serial,parallel);same_lanes(serial,tiled);
    for(std::size_t i=0;i<parallel.size();++i) {
        const auto count=original[i].origin>8?1U:9U;
        check(parallel[i].fits[0][0].count==count && parallel[i].fits[0][1].count==count,
              "narrow-bank scheduling must neither omit active samples nor score future origins");
    }
}

void invalid_batches_and_cancellation() {
    Fixture fixture;
    auto workers=fixture.workers(3);
    const auto original=fixture.lanes(1);
    auto lanes=original;
    const auto invalid=[&](CorrelationBatch batch,const char* message) {
        lanes=original;
        rejects([&]{accumulate_correlator_cpu(batch,lanes,workers,{});},message);
    };
    auto batch=fixture.batch();batch.projections=batch.projections.first(batch.projections.size()-1);
    invalid(batch,"truncated projection rows must be rejected");
    auto blocks=fixture.blocks;blocks[0].projection_offset=std::numeric_limits<std::size_t>::max();
    batch=fixture.batch();batch.blocks=blocks;
    invalid(batch,"out-of-range projection offsets must be rejected without pointer overflow");
    blocks=fixture.blocks;blocks[0].count=std::numeric_limits<std::size_t>::max();
    batch.blocks=blocks;
    invalid(batch,"overflowing prefix row extents must be rejected");
    blocks=fixture.blocks;blocks[0].sample=std::numeric_limits<std::uint64_t>::max();
    batch.blocks=blocks;
    invalid(batch,"overflowing sample endpoints must be rejected");
    blocks=fixture.blocks;++blocks[1].sample;batch.blocks=blocks;
    invalid(batch,"gaps between original projection blocks must be rejected");
    blocks=fixture.blocks;--blocks[1].sample;batch.blocks=blocks;
    invalid(batch,"overlapping original projection blocks must be rejected");
    batch=fixture.batch();batch.bank_frequencies=batch.bank_frequencies.first(1);
    invalid(batch,"frequency banks shorter than the logical frequency count must be rejected");
    batch=fixture.batch();batch.frequency_offsets=batch.frequency_offsets.first(1);
    invalid(batch,"frequency offset count must agree with geometry");
    batch=fixture.batch();batch.geometry.phase_step=0;
    invalid(batch,"zero phase step must be rejected");
    batch=fixture.batch();batch.geometry.phase_step=std::numeric_limits<std::uint64_t>::max();
    invalid(batch,"phase steps beyond the first second must be rejected without wraparound");
    batch=fixture.batch();++batch.geometry.symbol_samples;
    invalid(batch,"worker template geometry must agree with the batch");
    batch=fixture.batch();batch.geometry.carrier_hz=std::numeric_limits<double>::quiet_NaN();
    invalid(batch,"nonfinite carrier frequencies must be rejected");
    auto frequencies=fixture.frequencies;frequencies[0]=std::numeric_limits<double>::infinity();
    batch=fixture.batch();batch.bank_frequencies=frequencies;
    invalid(batch,"nonfinite projection bank frequencies must be rejected");
    auto offsets=fixture.offsets;offsets[0]=std::numeric_limits<double>::quiet_NaN();
    batch=fixture.batch();batch.frequency_offsets=offsets;
    invalid(batch,"nonfinite logical frequency offsets must be rejected");
    rejects([&]{accumulate_correlator_cpu(fixture.batch(),lanes,{},{});},"empty CPU workspace must be rejected");

    const auto invalid_lane=[&](CorrelationLane lane,const char* message) {
        std::array<CorrelationLane,1> input{lane};
        rejects([&]{accumulate_correlator_cpu(fixture.batch(),input,workers,{});},message);
    };
    auto lane=original[0];lane.frequency=fixture.geometry.frequency_count;
    invalid_lane(lane,"out-of-bank logical frequencies must be rejected");
    lane=original[0];lane.phase_lower=lane.phase_upper=fixture.geometry.sample_rate;
    invalid_lane(lane,"phase coordinates beyond the first second must be rejected");
    lane=original[0];lane.phase_lower=2;lane.phase_upper=1;
    invalid_lane(lane,"reversed phase intervals must be rejected");
    lane=original[0];lane.phase_upper=3*fixture.geometry.symbol_samples;
    invalid_lane(lane,"phase intervals requiring more than three fit groups must be rejected");
    for(const auto rate:{0.L,-1.L,std::numeric_limits<long double>::infinity(),
                         std::numeric_limits<long double>::quiet_NaN()}) {
        lane=original[0];lane.rate=rate;
        invalid_lane(lane,"nonpositive or nonfinite clock rates must be rejected");
    }
    lane=original[0];lane.origin=std::numeric_limits<long double>::quiet_NaN();
    invalid_lane(lane,"nonfinite lane origins must be rejected");
    lane=original[0];lane.origin=8-static_cast<long double>(fixture.geometry.symbol_samples);
    invalid_lane(lane,"a batch reaching symbol completion must remain in the host coordinator");

    Fixture tone(false,true);
    auto tone_batch=tone.batch();tone_batch.bank_frequencies=tone_batch.bank_frequencies.first(7);
    auto tone_workers=tone.workers(1);auto tone_lanes=tone.lanes(1);
    rejects([&]{accumulate_correlator_cpu(tone_batch,tone_lanes,tone_workers,{});},
            "tone bank count must contain complete bit and frequency pairs");
    tone_lanes[0].rate_index=2;
    rejects([&]{accumulate_correlator_cpu(tone.batch(),tone_lanes,tone_workers,{});},
            "tone rate index beyond the bank must be rejected");

    std::stop_source stop;stop.request_stop();lanes=original;
    rejects([&]{accumulate_correlator_cpu(fixture.batch(),lanes,workers,stop.get_token());},
            "cancelled correlation work must stop before updating lanes");
    same_lanes(lanes,original);
    accumulate_correlator_cpu(fixture.batch(),lanes,workers,{});
    check(lanes[0].fits[0][0].count==9,"cancellation must leave CPU workspace reusable");
}

void chip_chain_evidence_preserves_noise_integration() {
    for(unsigned count:{1U,2U}) {
        CorrelationFit fit;
        for(unsigned j=0;j<count;++j) {
            const double c=j?0:1,s=j?1:0,x=j?-.7:.3;
            fit.add({x*c,x*s,c*c,s*s,c*s,x*x},{1,0},1);
        }
        const auto [energy,rank]=CorrelationChipEvidence::projection(fit);
        check(rank==count && std::abs(energy-fit.energy)<1e-12,
              "one- and two-sample edge cells must keep their actual rank and projected energy");
    }
    std::array<double,2> integrated{};
    for(unsigned size=0;size<2;++size) {
        const unsigned samples=size?32:4;
        std::mt19937_64 random(892171);std::normal_distribution<double> noise(0,std::sqrt(samples/4.));
        for(unsigned trial=0;trial<256;++trial) {
            CorrelationFit whole;CorrelationChipEvidence cells;
            for(unsigned chip=0;chip<128;++chip)for(unsigned i=0;i<samples;++i) {
                const auto pattern=std::polar(1.,.713*chip);
                const auto oscillator=std::polar(1.,std::numbers::pi*i/2);
                const auto x=std::sqrt(.125)*(pattern*oscillator).real()+noise(random);
                const auto c=oscillator.real(),s=oscillator.imag();
                const CorrelationProjection observation{x*c,x*s,c*c,s*s,c*s,x*x};
                whole.add(observation,pattern,1);cells.add(observation,pattern,1,chip);
            }
            const auto [tail,rank]=CorrelationChipEvidence::projection(cells.cell);
            const auto projected=cells.projected_energy+tail;
            check(cells.projected_rank+rank==256 && whole.explained()<=projected+1e-8 && projected<=whole.energy+1e-8,
                  "whole template must remain nested inside disjoint chip spans and raw observations");
            check(cells.score(whole)<=whole.score(),"chain guard must never increase confidence");
            integrated[size]+=cells.score(whole)/256;
        }
    }
    check(integrated[0]>12 && integrated[0]<22 && std::abs(integrated[1]/integrated[0]-1)<.12,
          "increasing chip duration at equal integrated SNR must preserve weak-signal chain evidence");
}

void pulse_projection_preserves_real_sample_fit() {
    for(const auto chip:{8ULL,120ULL})for(const auto ppm:{0.,-.001,.001,-200.,200.,-8000.,8000.}) {
        const auto rate=1+static_cast<long double>(ppm)*1e-6L;
        const auto sample_rate=static_cast<std::uint32_t>(chip==8?400:6000);
        const auto frequency=chip==8?50.03125:1500.03125;
        CorrelationPulseKernel kernel(chip,rate,frequency,sample_rate);
        for(unsigned cell_index=0;cell_index<24;++cell_index) {
            const auto start=static_cast<long double>(cell_index)*chip/rate+.137L;
            const auto first=static_cast<std::uint64_t>(std::ceil(start));
            const auto end=static_cast<std::uint64_t>(std::ceil(start+chip/rate));
            CorrelationPulseCell cell;cell.first=first;cell.end=end;cell.count=end-first;
            std::array<std::complex<double>,correlation_pulse_atoms> coefficients;
            for(std::size_t j=0;j<coefficients.size();++j)
                coefficients[j]=std::polar(.3+.017*j,.43*j+.03*cell_index);
            CorrelationFit direct;
            for(auto sample=first;sample<end;++sample) {
                const auto oscillator=std::polar(1.,2*std::numbers::pi*frequency*sample/sample_rate);
                const auto x=std::sin(.71*sample)+.3*std::cos(.13*sample);
                const auto c=oscillator.real(),s=oscillator.imag();
                CorrelationProjection projection{x*c,x*s,c*c,s*s,c*s,x*x};
                const auto position=(static_cast<long double>(sample)-start)*rate/chip-.5L;
                std::complex<double> pattern{};
                for(std::size_t j=0;j<coefficients.size();++j) {
                    const auto pulse=pattern_pulse(static_cast<double>(position-j+8));
                    cell.dot[j]+=pulse*x*oscillator;pattern+=pulse*coefficients[j];
                }
                direct.add(projection,pattern,1);cell.energy+=x*x;
            }
            const auto carrier_square=std::polar(1.,4*std::numbers::pi*frequency*first/sample_rate);
            cell.gram=kernel.evaluate(static_cast<long double>(first)-start,cell.count,carrier_square,1);
            const auto actual=cell.fit(coefficients);
            check(actual.count==direct.count,"pulse projection changed independent observation count");
            const std::array<double,6> a{actual.xc,actual.xs,actual.cc,actual.ss,actual.cs,actual.energy},
                b{direct.xc,direct.xs,direct.cc,direct.ss,direct.cs,direct.energy};
            for(std::size_t i=0;i<a.size();++i)
                check(std::abs(a[i]-b[i])<1e-9*std::max(1.,std::abs(b[i])),
                      "pulse projection changed raw dots or carrier Gram");
            check(std::abs(actual.score()-direct.score())<1e-8*std::max(1.,direct.score()),
                  "pulse projection changed two-real-basis evidence");
        }
        // Exact support endpoints require the singleton cache; adjacent points
        // must rebuild rather than interpolate across the finite-pulse jump.
        for(const auto offset:{0.L,1e-13L,0.L,.25L,.250000000001L}) {
            const auto count=static_cast<std::uint64_t>(std::ceil(chip/rate-offset));
            const auto gram=kernel.evaluate(offset,count,{1,0},1);
            double expected=0;
            for(std::uint64_t n=0;n<count;++n) {
                const auto pulse=pattern_pulse(static_cast<double>((offset+n)*rate/chip+7.5L));
                expected+=pulse*pulse;
            }
            check(std::abs(gram.energy.front()-expected)<1e-10*std::max(1.,expected),
                  "pulse cache crossed a closed support endpoint");
        }
        // Repeated fractional cells exercise incremental polynomial changes,
        // sample-count transitions and phase wraps over many cache intervals.
        for(unsigned index=0;index<512;++index) {
            const auto start=static_cast<long double>(index)*chip/rate+.137L;
            const auto offset=std::ceil(start)-start;
            const auto count=static_cast<std::uint64_t>(std::ceil(chip/rate-offset));
            const auto gram=kernel.evaluate(offset,count,{1,0},1);
            if(index%31)continue;
            double energy=0;std::complex<double> square{};
            for(std::uint64_t n=0;n<count;++n) {
                const auto pulse=pattern_pulse(static_cast<double>((offset+n)*rate/chip-.5L));
                energy+=pulse*pulse;square+=pulse*pulse*std::polar(1.,4*std::numbers::pi*frequency*n/sample_rate);
            }
            // Packed upper-triangle diagonal for the central (j=8) atom.
            constexpr std::size_t center=8*correlation_pulse_atoms-8*7/2;
            check(std::abs(gram.energy[center]-energy)<1e-9*std::max(1.,energy) &&
                  std::abs(gram.square[center]-square)<1e-9*std::max(1.,std::abs(square)),
                  "incremental pulse Gram drifted across fractional cells or count transitions");
        }
    }
}

void long_pulse_segments_and_grams_match_sampled_reference() {
    struct Case {std::uint64_t chip;double ppm,frequency;long double offset;std::uint64_t trim;};
    const std::array cases{
        Case{4097,-200,.005,0,0},Case{4097,200,.5,.137L,1},
        Case{4097,8000,1500,.999L,17},Case{4097,0,2999.999999999,.25L,0},
        Case{12800,-200,.005,.137L,3},Case{12800,200,1500,.5L,0},
        Case{12800,-.001,2999.999999999,0,1},
        Case{12800,0,0,3200.137L,9599},Case{12800,0,3000,6400.25L,6398},
        Case{12800,0,3000.000000001,9600.9L,3183},
        Case{12800,0,.000000001,3200.5L,17},
        Case{1200000,200,.005,.137L,0},Case{1200000,-.001,1500,0,1}};
    constexpr std::uint32_t sample_rate=6000;
    for(const auto& item:cases) {
        const auto rate=1+static_cast<long double>(item.ppm)*1e-6L;
        const auto duration=item.chip/rate;
        const auto count=static_cast<std::uint64_t>(std::ceil(duration-item.offset))-item.trim;
        CorrelationPulseKernel kernel(item.chip,rate,item.frequency,sample_rate);
        const auto carrier_squared=std::polar(.49,.74);
        const auto actual=kernel.evaluate(item.offset,count,carrier_squared,.49);
        std::array<double,correlation_pulse_pairs> energy{},real{},imaginary{};
        const auto rotation=std::polar(1.L,2*static_cast<long double>(2*std::numbers::pi)*item.frequency/sample_rate);
        std::complex<long double> carrier{1,0};
        std::array<long double,correlation_pulse_atoms> values{};
        for(std::uint64_t n=0;n<count;++n) {
            const auto q=(item.offset+n)/duration-.5L;
            for(std::size_t j=0;j<values.size();++j)
                values[j]=pattern_pulse(static_cast<double>(q+8-static_cast<long double>(j)));
            std::size_t pair=0;
            for(std::size_t j=0;j<values.size();++j)for(std::size_t k=j;k<values.size();++k,++pair) {
                const auto product=values[j]*values[k];
                energy[pair]+=static_cast<double>(product);
                real[pair]+=static_cast<double>(product*carrier.real());
                imaginary[pair]+=static_cast<double>(product*carrier.imag());
            }
            carrier*=rotation;
        }
        for(std::size_t pair=0;pair<correlation_pulse_pairs;++pair) {
            // Absolute error is normalized by the unmodulated pair's available
            // energy; an image term close to zero must not conceal a bad fit.
            const auto scale=std::max(1.,static_cast<double>(count));
            check(std::abs(actual.energy[pair]-.49*energy[pair])<3e-11*scale &&
                  std::abs(actual.square[pair]-carrier_squared*std::complex<double>{real[pair],imaginary[pair]})<3e-11*scale,
                  "bounded long-chip Gram changed a sampled pulse pair or real carrier image");
        }
        std::uint64_t first=0,segments=0;
        while(first<count) {
            const auto span=correlation_pulse_segment(item.offset+first,count-first,duration);
            check(span.count && span.count<=count-first && (!span.endpoint || span.count==1),
                  "affine pulse span lost a sampled endpoint or exceeded its clipped extent");
            for(const auto local:std::array<std::uint64_t,3>{0,span.count/2,span.count-1}) {
                const auto q=(item.offset+first+local)/duration-.5L;
                for(std::size_t j=0;j<span.value.size();++j) {
                    const auto expected=pattern_pulse(static_cast<double>(q+8-static_cast<long double>(j)));
                    check(std::abs(span.value[j]+local*span.slope[j]-expected)<3e-14,
                          "affine moment pulse differs from the sampled finite table");
                }
            }
            first+=span.count;++segments;
        }
        check(segments<=260,"long-chip preparation reverted to original sample cadence");
        const auto clipped=correlation_pulse_segment(item.offset,std::min<std::uint64_t>(17,count),duration);
        check(clipped.count<=17,"affine segment helper crossed a caller's clipped block");
    }
    // A midpoint landing exactly on the closed support is a singleton, even
    // when rounding double pulse coordinates creates that endpoint nearby.
    for(const auto offset:{600000.L,600000.L-1e-13L,600000.L+1e-13L}) {
        const auto span=correlation_pulse_segment(offset,128,1200000);
        check(span.endpoint && span.count==1 && span.value.front()==pattern_pulse(8) &&
              span.value.back()==pattern_pulse(-8),"rounded support endpoint was spread over an affine span");
    }
    std::stop_source stop;stop.request_stop();
    rejects([&]{CorrelationPulseKernel kernel(1200000,1,.005,6000);
                kernel.evaluate(0,1200000,{1,0},1,stop.get_token());},
            "bounded long-chip Gram ignored cancellation");
    CorrelationPulseKernel reusable(12800,1,.005,6000);
    const auto before=reusable.evaluate(0,12800,{1,0},1);
    rejects([&]{reusable.evaluate(.137L,12800,{1,0},1,stop.get_token());},
            "cached long-chip rebuild ignored cancellation");
    const auto after=reusable.evaluate(0,12800,{1,0},1);
    check(before.energy==after.energy && before.square==after.square,
          "cancelled long-chip rebuild corrupted the previous cache geometry");
    rejects([]{correlation_pulse_segment(0,0,1200000);},"empty affine pulse extent was accepted");
}
void affine_spans_preserve_real_sample_fit() {
    constexpr double tau=2*std::numbers::pi;
    for(const auto frequency:{0.,.005,.05,1500.,2999.999999,3000.})
        for(const auto count:{1U,2U,5U,31U,32U,127U,128U})
            for(const auto norm:{.998,1.,1.003}) {
                const std::complex<double> value{.27,-.83},slope{.0031,.0017};
                auto oscillator=std::polar(std::sqrt(norm),.371);
                const auto first=oscillator,step=std::polar(1.,tau*frequency/6000);
                CorrelationProjection summed;
                std::complex<double> first_moment{};
                CorrelationFit reference;
                for(unsigned i=0;i<count;++i) {
                    const auto x=std::sin(.31*i)+.2*std::cos(.017*i);
                    const auto c=oscillator.real(),s=oscillator.imag();
                    const CorrelationProjection p{x*c,x*s,c*c,s*s,c*s,x*x};
                    summed.xc+=p.xc;summed.xs+=p.xs;summed.cc+=p.cc;
                    summed.ss+=p.ss;summed.cs+=p.cs;summed.energy+=p.energy;
                    first_moment+=static_cast<double>(i)*std::complex<double>{p.xc,p.xs};
                    reference.add(p,value+static_cast<double>(i)*slope,1);
                    oscillator*=step;
                }
                const auto actual=correlation_affine_fit(summed,first_moment,count,value,slope,
                    first*first,std::norm(first),2*tau*frequency/6000);
                const auto near=[](double a,double b) {
                    return std::abs(a-b)<=2e-11*std::max({1.,std::abs(a),std::abs(b)});
                };
                check(actual.count==reference.count && actual.energy==reference.energy &&
                    near(actual.xc,reference.xc) && near(actual.xs,reference.xs) &&
                    near(actual.cc,reference.cc) && near(actual.ss,reference.ss) &&
                    near(actual.cs,reference.cs),"affine span changed original I/Q fit or PCM energy");
                if(frequency==1500 && count>=5)
                    check(near(actual.score(),reference.score()),"affine span changed detection statistic");
            }
    rejects([] {correlation_affine_fit({}, {},0, {}, {}, {1,0},1,0);},
            "empty affine fit accepted");
    rejects([] {correlation_affine_fit({}, {},129, {}, {}, {1,0},1,0);},
            "affine fit crossed original bounded oscillator block");
}
void fractional_pulse_kernels_reuse_exact_intervals() {
    for(const auto chip:{1280ULL,4097ULL,12800ULL,1200000ULL}) {
        const auto rate=1.L+3e-10L;
        CorrelationPulseKernel cached(chip,rate,.05,6000);
        for(unsigned i=0;i<12;++i) {
            const auto offset=.137L+i*1e-5L;
            const auto count=static_cast<std::uint64_t>(std::ceil(chip/rate-offset));
            const auto a=cached.evaluate(offset,count,{.6,.8},1.);
            CorrelationPulseKernel fresh(chip,rate,.05,6000);
            const auto b=fresh.evaluate(offset,count,{.6,.8},1.);
            for(std::size_t p=0;p<correlation_pulse_pairs;++p) {
                const auto tolerance=2e-10*std::max(1.,std::abs(b.energy[p]));
                check(std::abs(a.energy[p]-b.energy[p])<=tolerance &&
                    std::abs(a.square[p]-b.square[p])<=tolerance,
                    "fractional pulse reuse crossed a table or changed its Gram");
            }
        }
        check(cached.preparation_count()==1,
            "stable GPSDO kernel rebuilt instead of reusing its valid interval");
        // Large displacements and support points must still use the proper
        // new geometry, including an interrupted preparation on each backend.
        const auto original=cached.evaluate(.137L,chip,{1,0},1.);
        std::stop_source stop;stop.request_stop();
        rejects([&]{cached.evaluate(.99L,chip-1,{1,0},1.,stop.get_token());},
                "fractional kernel ignored cancelled geometry change");
        const auto recovered=cached.evaluate(.137L,chip,{1,0},1.);
        check(original.energy==recovered.energy && original.square==recovered.square,
                "cancelled fractional preparation corrupted a cached Gram");
    }
}
} // namespace

int main() {
    try {
        exact_workers_and_tiles(false,false,false,10019);
        exact_workers_and_tiles(false,false,true,129);
        exact_workers_and_tiles(true,false,true,129);
        exact_workers_and_tiles(false,true,false,129);
        phase_groups_and_observed_start();
        narrow_bank_with_future_origins();
        invalid_batches_and_cancellation();
        pulse_projection_preserves_real_sample_fit();
        long_pulse_segments_and_grams_match_sampled_reference();
        affine_spans_preserve_real_sample_fit();
        fractional_pulse_kernels_reuse_exact_intervals();
        chip_chain_evidence_preserves_noise_integration();
        std::cout<<"pattern_correlator_batch ok\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
