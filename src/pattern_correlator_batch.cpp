#include "pattern_correlator_batch.hpp"
#include "datapump/symbol_schedule.hpp"
#include "search_parallel.hpp"
#include <limits>
#include <numbers>
#include <openssl/crypto.h>

namespace datapump::modem::detail {
namespace {
constexpr double tau=2*std::numbers::pi;
void require(bool condition,const char* text) {if(!condition)throw Error(text);}
void cancelled(std::stop_token stop) {if(stop.stop_requested())throw Error("pattern correlation cancelled");}
struct PhaseRange {std::uint64_t lower=0,upper=0;};
std::array<PhaseRange,3> phase_groups(const CorrelationLane& lane,const CorrelationGeometry& g,std::size_t& count) {
    std::array<PhaseRange,3> groups{};count=0;
    auto lower=lane.phase_lower;
    while(lower<=lane.phase_upper) {
        const auto address=symbol_stream_address(g.epoch,lower,lane.index,g.symbol_samples,g.sample_rate);
        auto low=std::uint64_t{0},high=(lane.phase_upper-lower)/g.phase_step;
        while(low<high) {
            const auto mid=low+(high-low+1)/2;
            const auto candidate=symbol_stream_address(g.epoch,lower+mid*g.phase_step,lane.index,g.symbol_samples,g.sample_rate);
            if(candidate.epoch==address.epoch && candidate.ordinal==address.ordinal)low=mid;
            else high=mid-1;
        }
        const auto end=lower+low*g.phase_step;
        require(count<groups.size(),"pattern phase groups exceed finite symbol interval");
        groups[count++]={lower,end};
        if(end==lane.phase_upper)break;
        lower=end+g.phase_step;
    }
    return groups;
}
void accumulate_lane(const CorrelationBatch& batch,CorrelationLane& lane,PatternCode& pattern,std::stop_token stop) {
    const auto& g=batch.geometry;
    require(lane.frequency<g.frequency_count && std::isfinite(lane.origin) &&
        std::isfinite(lane.rate) && lane.rate>0 && lane.phase_lower<=lane.phase_upper && lane.phase_upper<g.sample_rate &&
        (!g.tone || lane.rate_index<batch.bank_frequencies.size()/(2*g.frequency_count)),
        "invalid correlation lane");
    std::size_t group_count=0;
    const auto groups=phase_groups(lane,g,group_count);
    for(const auto& block:batch.blocks) {
        cancelled(stop);
        const auto end=block.sample+block.count;
        const auto cursor=lane.origin>static_cast<long double>(block.sample)?
            static_cast<std::uint64_t>(std::min(static_cast<long double>(end),std::ceil(lane.origin))):block.sample;
        if(cursor>=end)continue;
        const auto symbol_start=lane.origin+static_cast<long double>(lane.index)*g.symbol_samples/lane.rate;
        const auto symbol_end=lane.origin+(static_cast<long double>(lane.index)+1)*g.symbol_samples/lane.rate;
        require(std::isfinite(symbol_start) && std::isfinite(symbol_end) &&
                static_cast<long double>(end)<std::ceil(symbol_end),"correlation batch crosses a symbol completion");
        if(!lane.fits[0][0].count)lane.observed_start=cursor;
        for(std::size_t group=0;group<group_count;++group) {
            pattern.set_stream_phase_samples(groups[group].lower);
            auto& fit=lane.fits[group];
            auto observed=cursor;
            while(observed<end) {
                const auto within=std::max(0.L,(static_cast<long double>(observed)-symbol_start)*lane.rate);
                if(g.shaped) {
                    require(lane.index<=std::numeric_limits<std::uint64_t>::max()/g.chips_per_symbol,
                            "pattern chip coordinate overflow");
                    const auto first_chip=lane.index*g.chips_per_symbol;
                    const auto left=static_cast<std::size_t>(observed-block.sample);
                    const auto row=block.projection_offset+lane.frequency*(block.count+1);
                    const auto projection=batch.projections[row+left+1]-batch.projections[row+left];
                    for(unsigned bit=0;bit<2;++bit) {
                        const auto phase=pattern.shaped_value(first_chip,bit,static_cast<double>(within));
                        fit[bit].add(projection,phase,1);
                    }
                    ++observed;continue;
                }
                const auto local_coordinate=std::floor(within/g.chip_samples);
                require(std::isfinite(local_coordinate) && local_coordinate<std::ldexp(1.L,64),
                        "pattern chip coordinate overflow");
                const auto local=static_cast<std::uint64_t>(local_coordinate);
                require(lane.index<=(std::numeric_limits<std::uint64_t>::max()-local)/g.chips_per_symbol,
                        "pattern chip coordinate overflow");
                const auto chip=lane.index*g.chips_per_symbol+local;
                const auto fraction=std::clamp(static_cast<double>(within/g.chip_samples-local),0.,std::nextafter(1.,0.));
                const auto chip_end=symbol_start+(static_cast<long double>(local)+1)*g.chip_samples/lane.rate;
                const auto boundary=std::min(static_cast<long double>(end),std::ceil(std::min(chip_end,symbol_end)));
                const auto until=static_cast<std::uint64_t>(std::max(static_cast<long double>(observed+1),boundary));
                const auto left=static_cast<std::size_t>(observed-block.sample),right=static_cast<std::size_t>(until-block.sample);
                for(unsigned bit=0;bit<2;++bit) {
                    const auto bank=g.tone?(lane.rate_index*g.frequency_count+lane.frequency)*2+bit:lane.frequency;
                    require(bank<batch.bank_frequencies.size(),"correlation lane frequency exceeds bank");
                    auto phase=pattern.value(chip,bit,fraction);
                    if(g.tone) {
                        const auto tone=batch.bank_frequencies[bank]-g.carrier_hz-batch.frequency_offsets[lane.frequency];
                        phase*=std::polar(1.,-static_cast<double>(std::remainder(static_cast<long double>(observed)*tau*tone/g.sample_rate,
                            static_cast<long double>(tau))));
                    }
                    const auto row=block.projection_offset+bank*(block.count+1);
                    const auto projection=batch.projections[row+right]-batch.projections[row+left];
                    fit[bit].add(projection,phase,right-left);
                }
                observed=until;
            }
        }
    }
}
} // namespace

CorrelationBatch::~CorrelationBatch() {
    OPENSSL_cleanse(geometry.pattern.spreading_seed.data(),geometry.pattern.spreading_seed.size());
    OPENSSL_cleanse(geometry.pattern.dsss_seed.data(),geometry.pattern.dsss_seed.size());
}
void accumulate_correlator_cpu(const CorrelationBatch& batch,std::span<CorrelationLane> lanes,
                              std::span<PatternCode> workers,std::stop_token stop) {
    cancelled(stop);
    const auto& g=batch.geometry;
    require(g.symbol_samples && g.chip_samples && g.chips_per_symbol && g.phase_step && g.sample_rate &&
            g.phase_step<=g.sample_rate && std::isfinite(g.carrier_hz) &&
            g.frequency_count && g.frequency_count<=65 && g.frequency_count==batch.frequency_offsets.size() &&
            batch.bank_frequencies.size()>=g.frequency_count &&
            (!g.tone || batch.bank_frequencies.size()%(2*g.frequency_count)==0) && !workers.empty(),
            "invalid correlation batch geometry");
    for(const auto frequency:batch.bank_frequencies)
        require(std::isfinite(frequency),"invalid correlation bank frequency");
    for(const auto frequency:batch.frequency_offsets)
        require(std::isfinite(frequency),"invalid correlation frequency offset");
    for(const auto& worker:workers)
        require(worker.symbol_samples()==g.symbol_samples && worker.chip_samples()==g.chip_samples &&
                worker.chips_per_symbol()==g.chips_per_symbol,"invalid correlation worker geometry");
    std::uint64_t previous_end=0;
    bool first=true;
    for(const auto& block:batch.blocks) {
        require(block.count && block.count<std::numeric_limits<std::size_t>::max() &&
                block.count<=std::numeric_limits<std::uint64_t>::max()-block.sample,
                "invalid correlation block extent");
        require(block.projection_offset<=batch.projections.size() &&
                batch.bank_frequencies.size()<=(batch.projections.size()-block.projection_offset)/(block.count+1),
                "correlation block exceeds projection storage");
        require(first || block.sample==previous_end,"correlation blocks are not contiguous");
        first=false;previous_end=block.sample+block.count;
    }
    // Claim contiguous ranges, amortizing scheduling across neighboring lanes.
    // This is a CPU policy only; logical lane identity never depends on it.
    parallel_search_ranges(lanes.size(),workers.size(),16,[&](std::size_t worker,std::size_t begin,std::size_t end) {
        for(auto i=begin;i<end;++i)accumulate_lane(batch,lanes[i],workers[worker],stop);
    });
}
} // namespace datapump::modem::detail
