#pragma once
#include "scheduled_fp32_coherent_prefit.hpp"
#include "scheduled_acquisition_catalog.hpp"
#include "scheduled_processing_journal.hpp"
#include "datapump/sched_hop.hpp"
#include <tuple>

namespace datapump::modem::detail::scheduled_native_cohort {
namespace proxy=scheduled_fp32_prefit;
using Lane=ScheduledAcquisitionCatalog::Lane;
inline constexpr std::size_t events=32,physical_rows=64,requests=events*physical_rows,cells=4096;
inline constexpr std::size_t allocator_allowance=64;
// Internal fault/cancellation boundary hook; no callback or allocation by default.
enum class Phase {allocation,enumeration,exported,deduplicated,native,scatter,ready,consume};
using Hook=void(*)(Phase,std::uint64_t,void*);
inline thread_local Hook test_hook=nullptr;
inline thread_local void* test_context=nullptr;
inline void checkpoint(Phase phase,std::uint64_t value,std::stop_token stop) {
    if(test_hook)test_hook(phase,value,test_context);
    if(stop.stop_requested())throw Error("native cohort preparation cancelled");
}
inline bool address_equal(SymbolStreamAddress a,SymbolStreamAddress b) {
    return a.epoch==b.epoch && a.ordinal==b.ordinal && a.sample_in_second==b.sample_in_second;
}
struct Key {
    SymbolStreamAddress address;
    std::uint64_t symbol=0,chip=0,argument=0;
    friend bool operator==(const Key& a,const Key& b) {
        return address_equal(a.address,b.address) && a.symbol==b.symbol && a.chip==b.chip && a.argument==b.argument;
    }
    auto order()const{return std::tuple{address.epoch,address.ordinal,address.sample_in_second,symbol,chip,argument};}
    bool same_source(const Key& b)const{return address_equal(address,b.address) && symbol==b.symbol && chip==b.chip;}
};
inline std::uint64_t hash(const Key& key) {
    auto value=std::uint64_t{0xcbf29ce484222325ULL};
    for(const auto word:{key.address.epoch,key.address.ordinal,static_cast<std::uint64_t>(key.address.sample_in_second),key.symbol,key.chip,key.argument}){
        value^=word;value*=0x100000001b3ULL;value^=value>>29;}
    return value;
}
struct Context {
    Lane lane;
    std::uint64_t index=0,first=0,end=0,limit=0,epoch_sample=0;
    long double symbol_source=0;
    std::array<SymbolStreamAddress,2> address{};
};
struct SourceRow {
    std::uint64_t observed=0,owner=0,target_chip=0;
    double argument=0,gain=0;
    bool carry=false;
};
// This continuation belongs to one Original processing-fence invocation. A
// physical64 scheduling yield NEVER changes its full Piece anchors or limit.
struct Exporter {
    std::uint64_t observed=0,limit=0,piece_first=0,piece_end=0;
    SchedHopCoordinate tick;
    SchedHopLocation location;
    long double source=0;
    bool piece=false;
    void begin(std::uint64_t first,std::uint64_t end){observed=first;limit=end;piece=false;}
    template<class Gain,class Consumer>
    bool visit(const Context& context,const SchedHopSourceMap& map,std::uint64_t window_end,
               std::uint64_t symbol,std::uint64_t chip,std::uint64_t chips,Gain gain_adjust,Consumer consume) {
        while(observed<std::min(window_end,limit)) {
            if(!piece) {
                tick=SchedHopCoordinate{context.epoch_sample,0}.add_samples(context.lane.phase_lower).
                    add_signed((static_cast<long double>(observed)-context.lane.origin)*context.lane.rate);
                location=map.locate(tick);
                if(!location.active){const auto next=map.source_to_physical(location.source,false);
                    const auto advance=std::max(1.L,std::ceil(next.distance(tick)/context.lane.rate));
                    observed+=static_cast<std::uint64_t>(std::min(advance,static_cast<long double>(limit-observed)));
                    continue;}
                const auto count=std::max(1.L,std::ceil(SchedHopCoordinate{location.physical_end,0}.distance(tick)/context.lane.rate));
                piece_first=observed;piece_end=observed+static_cast<std::uint64_t>(std::min(count,static_cast<long double>(limit-observed)));
                source=location.source.distance(context.lane.anchor)-context.symbol_source;piece=true;
            }
            const auto until=std::min(window_end,piece_end);
            for(;observed<until;++observed) {
                auto at=source+static_cast<long double>(observed-piece_first)*context.lane.rate;
                if(at<0)continue;if(at>=2.L*symbol)return false;
                const bool next=at>=symbol;if(next)at-=symbol;
                if(context.index==std::numeric_limits<std::uint64_t>::max() && next)return false;
                const auto target=context.index+(next?1:0);
                if(target>std::numeric_limits<std::uint64_t>::max()/chips)return false;
                const auto physical=tick.add_signed(static_cast<long double>(observed-piece_first)*context.lane.rate);
                const auto gain=gain_adjust(SchedHopSourceMap::gain(location,physical),physical);
                if(gain==0)continue;
                // Owner/carry come from Original long double, including values
                // below N which narrow TO N. The cast is only q's argument.
                if(!consume(SourceRow{observed,static_cast<std::uint64_t>(at/chip),target*chips,
                                      static_cast<double>(at),gain,next}))return false;
            }
            if(observed==piece_end)piece=false;
        }
        return true;
    }
};
struct Request {
    proxy::Projection p;
    std::uint64_t observed=0,owner=0;
    double gain=0;
    std::uint16_t event=0,unique=0;
    bool carry=false;
};
struct Unique {
    Key key;
    std::uint64_t phase=0,target_chip=0;
    proxy::Pair raw{};
    double radius=proxy::cert::infinity;
    bool valid=false;
};
struct Window {
    std::array<Request,requests> row{};
    std::array<Unique,requests> unique{};
    std::array<std::uint16_t,cells> table{};
    std::array<std::uint16_t,requests> order{};
    std::size_t count=0,unique_count=0;
    std::uint64_t collisions=0,reuses=0;
    void reset(){count=unique_count=0;collisions=reuses=0;table.fill(0xffffU);}
    bool add(std::size_t event,const Context& context,const SourceRow& source,std::uint64_t symbol,std::uint64_t chip) {
        if(count==row.size() || event>=events || !std::isfinite(source.argument) || source.argument<0 || !std::isfinite(source.gain))return false;
        const Key key{context.address[source.carry?1:0],symbol,chip,std::bit_cast<std::uint64_t>(source.argument)};
        auto at=static_cast<std::size_t>(hash(key))&(cells-1);std::size_t selected=0;
        for(std::size_t probes=0;;++probes){if(probes==cells)return false;
            const auto found=table[at];
            if(found==0xffffU){if(unique_count==unique.size())return false;
                selected=unique_count++;unique[selected]={key,context.lane.phase_lower,source.target_chip};
                table[at]=static_cast<std::uint16_t>(selected);break;}
            if(unique[found].key==key){selected=found;++reuses;break;}
            ++collisions;at=(at+1)&(cells-1);
        }
        row[count++]={{},source.observed,source.owner,source.gain,static_cast<std::uint16_t>(event),
                      static_cast<std::uint16_t>(selected),source.carry};return true;
    }
};
struct RawScratch {
    using Native=proxy::Scratch::Native;
    Native::Original::Plan plan;
    Native::PackedPlan packed;
    Native::CoefficientInput coefficient{};
    Native native;
    Native::Original::Statistics statistics;
    std::array<double,physical_rows> arguments{};
    std::uint64_t native_rows=0,native_tiles=0,splits=0;
    static constexpr auto callee_bytes=proxy::Scratch::callee_bytes;
    bool evaluate(Window& window,PatternCode& code,const Config& config,std::stop_token stop) {
        if(!proxy::cert::coordinate_environment() || !Native::supported() || code.transmit_trace_enabled())return false;
        for(std::size_t i=0;i<window.unique_count;++i)window.order[i]=static_cast<std::uint16_t>(i);
        std::sort(window.order.begin(),window.order.begin()+window.unique_count,[&](auto a,auto b){return window.unique[a].key.order()<window.unique[b].key.order();});
        checkpoint(Phase::deduplicated,window.unique_count,stop);
        for(std::size_t first=0;first<window.unique_count;) {
            if(!proxy::cert::coordinate_environment() || !Native::supported() || code.transmit_trace_enabled())return false;
            const auto& representative=window.unique[window.order[first]];
            auto group_end=first+1;
            while(group_end<window.unique_count && representative.key.same_source(window.unique[window.order[group_end]].key))++group_end;
            auto count=std::min(physical_rows,group_end-first);
            bool planned=false;
            while(count){
                for(std::size_t j=0;j<count;++j)arguments[j]=std::bit_cast<double>(window.unique[window.order[first+j]].key.argument);
                if(plan.build_original({arguments.data(),count},representative.key.symbol,representative.key.chip,statistics,stop)){planned=true;break;}
                if(count==1)break;count=(count+1)/2;++splits;
            }
            if(!planned){++first;continue;}
            code.set_stream_phase_samples(representative.phase);
            if(!address_equal(code.resolved_symbol_address(representative.target_chip),representative.key.address))return false;
            if(!packed.pack(plan,native.statistics,stop))return false;
            const Native::Lane lane{&code,&config,representative.phase,representative.target_chip,plan.first_local};
            if(!Native::gather(packed,lane,coefficient,native.statistics,stop) ||
               !native.prepare(packed,coefficient,Native::Accuracy{1e-5},stop) || !native.evaluate(stop))return false;
            native_rows+=count;++native_tiles;checkpoint(Phase::native,native_rows,stop);
            for(std::size_t j=0;j<count;++j){auto& out=window.unique[window.order[first+j]];
                if(!native.raw(j,out.raw,&out.radius) || !std::isfinite(out.radius) || out.radius<0)return false;
                for(const auto q:out.raw)if(!proxy::cert::finite(q))return false;out.valid=true;}
            first+=count;
        }
        return true;
    }
};
struct Member {
    Context context;
    Exporter exporter;
    proxy::State state;
    std::size_t request_first=0,request_end=0;
    bool valid=true,fence_active=false;
};
// All selected operands are copied before another carrier can overwrite prefix.
inline bool scatter(Member& member,const Window& window,double amplitude,std::stop_token stop) {
    if(!member.valid)return false;
    if(member.request_first==member.request_end)return true;
    double maximum=0,radius=0,gain=0;
    for(auto i=member.request_first;i<member.request_end;++i){const auto& request=window.row[i];const auto& source=window.unique[request.unique];
        if(!source.valid)return false;radius=std::max(radius,source.radius);gain=std::max(gain,std::abs(request.gain));
        for(const auto q:source.raw)maximum=std::max({maximum,std::abs(q.real()),std::abs(q.imag())});}
    const auto bound=proxy::unclipped(maximum,radius,amplitude,gain);if(!bound.valid)return false;
    for(auto i=member.request_first;i<member.request_end;++i){const auto& request=window.row[i];const auto& source=window.unique[request.unique];
        auto q=source.raw;for(auto& z:q)z=request.gain*pattern_limit_pcm(amplitude*z)/amplitude;
        member.state.append(request.carry,request.p,q,bound.component_upper,bound.radius,request.owner);
        if(!member.state.current.valid || !member.state.carry.valid)return false;}
    checkpoint(Phase::scatter,member.request_end-member.request_first,stop);return true;
}
struct Build {
    std::array<Member,events> member{};
    Window window;
    RawScratch raw;
    static constexpr std::size_t callee_bytes=RawScratch::callee_bytes+8192;
    ~Build(){static_assert(std::is_trivially_destructible_v<Member> && std::is_trivially_destructible_v<Window> &&
                          std::is_trivially_destructible_v<RawScratch>);OPENSSL_cleanse(this,sizeof(*this));}
};
struct Identity {
    scheduled_processing::Block block;
    const void* catalog=nullptr;const void* capture=nullptr;const void* family=nullptr;
    std::uint64_t pcm_generation=0,input_origin=0,environment=0;
    friend bool operator==(const Identity&,const Identity&)=default;
};
inline bool lane_equal(const Lane& a,const Lane& b) {
    return a.id==b.id && a.pair==b.pair && a.origin_index==b.origin_index && a.frequency==b.frequency &&
        a.rate_index==b.rate_index && a.tone_bank_base==b.tone_bank_base && a.origin==b.origin && a.rate==b.rate &&
        a.observed_begin==b.observed_begin && a.phase_lower==b.phase_lower && a.phase_upper==b.phase_upper &&
        a.index==b.index && a.first_full_index==b.first_full_index && a.anchor.sample==b.anchor.sample && a.anchor.fraction==b.anchor.fraction;
}
struct Batch {
    struct Event {Context context;proxy::Frozen frozen;bool consumed=false;};
    Identity identity;
    std::array<Event,events> event{};
    std::size_t count=0,inspected=0,from_id=0,through_id=0;
    std::uint64_t from_index=0,through_index=0;
    static constexpr std::size_t caller_bytes=8192;
    std::size_t working_bytes()const{return sizeof(*this)+allocator_allowance+caller_bytes;}
    ~Batch(){static_assert(std::is_trivially_destructible_v<Event> && std::is_trivially_destructible_v<Identity>);OPENSSL_cleanse(this,sizeof(*this));}
};
inline std::uint64_t environment() {
    if(!proxy::cert::coordinate_environment() || !RawScratch::Native::supported())return 0;
    auto value=std::uint64_t{1}|(static_cast<std::uint64_t>(std::fegetround())<<1);
#if defined(__SSE2__)
    value|=static_cast<std::uint64_t>(_mm_getcsr()&0xffc0U)<<16;
#endif
#if (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
    unsigned short control=0;asm volatile("fnstcw %0" : "=m"(control));value|=static_cast<std::uint64_t>(control)<<48;
#endif
    return value;
}
} // namespace datapump::modem::detail::scheduled_native_cohort
