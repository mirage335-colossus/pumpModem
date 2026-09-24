#include "../src/pattern_correlator_batch.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <numbers>
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
            check(same_fit(x.fits[group][bit],y.fits[group][bit]),"worker count or block tiling changed correlation arithmetic");
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
        std::cout<<"pattern_correlator_batch ok\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
