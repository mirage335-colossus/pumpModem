#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_shaping.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace datapump::triad::v3 {
namespace {
constexpr long double tau = 2 * std::numbers::pi_v<long double>;
constexpr double carrier_gain = .57735026918962576451;
constexpr std::uint64_t maximum_tile_cells = 1'000'000;
using Chip = std::array<Complex,3>;
void require(bool ok,const char* msg){if(!ok)throw Error(msg);}
std::uint64_t coordinate(long double x,const char* msg){
    require(std::isfinite(x)&&x>=0&&x<std::ldexp(1.L,63),msg);
    return static_cast<std::uint64_t>(std::ceil(x));
}
void append64(Bytes& b,std::uint64_t v){for(int s=56;s>=0;s-=8)b.push_back(static_cast<std::uint8_t>(v>>s));}
Bytes child_id(std::string_view purpose,const Format& f,std::uint64_t tile,
               unsigned alternative=2){
    const std::string_view prefix="datapump/experimental-triad/v3/independent-apsk-2/";
    Bytes id(prefix.begin(),prefix.end());
    append64(id,purpose.size());id.insert(id.end(),purpose.begin(),purpose.end());
    append64(id,f.epoch);append64(id,f.symbol_ordinal);append64(id,tile);
    // 0/1 are independently keyed encrypted alternatives; 2 is common cover,
    // payload masking or permutation. All variable fields are fixed-width.
    append64(id,alternative);append64(id,f.fhss_center_aligned?1:0);
    return id;
}

Crypto child(const Crypto& key,std::string_view purpose,const Format& f,std::uint64_t tile,
             unsigned alternative=2){
    return Crypto(key.mac(child_id(purpose,f,tile,alternative)));
}
std::uint64_t aligned_dwell_cells(const Format& f,double fine){
    require(f.fhss,"triad v3 center-aligned cells require FHSS");
    const long double cells=static_cast<long double>(f.dwell_seconds)*fine;
    const auto nearest=std::round(cells);
    require(nearest>=1&&std::abs(cells-nearest)<=4*std::numeric_limits<double>::epsilon()*nearest,
            "triad v3 center-aligned FHSS requires integer fine cells per dwell");
    return coordinate(nearest,"triad v3 aligned dwell extent overflow");
}
std::pair<std::uint64_t,std::uint64_t> aligned_protected_range(const Format& f,double fine){
    const auto cells=aligned_dwell_cells(f,fine);
    const long double guard=f.quiet_seconds+f.ramp_seconds+f.margin_seconds+8/fine;
    const auto first=std::ceil(guard*fine),last=std::floor(cells-guard*fine);
    require(first<=last&&first>=0&&last<cells,"triad v3 FHSS has no protected dwell cells");
    return {static_cast<std::uint64_t>(first),static_cast<std::uint64_t>(last)};
}
std::uint64_t lower_cells(const Format& f,double fine,std::uint64_t dwells){
    const long double edge=8/fine+std::max(f.ramp_seconds,8/fine)+f.margin_seconds;
    if(!f.fhss){const auto width=dwells*static_cast<long double>(f.dwell_seconds)-2*edge;
        return width>0?static_cast<std::uint64_t>(std::floor(width*fine)):0;}
    // A commensurate, phase-zero grid repeats the same exact protected center
    // set in every dwell. Retain the existing two endpoint-dwell exclusion.
    if(f.fhss_center_aligned){const auto [first,last]=aligned_protected_range(f,fine);
        return (dwells-2)*(last-first+1);}
    const long double guard=f.quiet_seconds+f.ramp_seconds+f.margin_seconds+8/fine;
    const auto width=(static_cast<long double>(f.dwell_seconds)-2*guard)*fine;
    require(width>=1,"triad v3 FHSS aperture cannot contain a protected fine cell");
    // The complete endpoint ramp plus pulse support lies within the first
    // and last dwell whenever the protected FHSS aperture above is nonempty.
    // Excluding both gives a phase-independent bound for all endpoint tiles.
    return (dwells-2)*static_cast<std::uint64_t>(std::floor(width));
}
std::vector<std::size_t> permutation(std::size_t n,const Crypto& key,std::uint64_t epoch){
    std::vector<std::size_t> map(n);std::iota(map.begin(),map.end(),0);
    const auto data=key.stream(StreamPurpose::Dsss,epoch,0,(n+64)*8);std::size_t pos=0;
    auto next=[&](){require(pos+4<=data.size(),"triad v3 shuffle rejection budget exhausted");
        std::uint32_t x=0;for(unsigned j=0;j<4;++j)x=(x<<8)|data[pos++];return x;};
    for(std::size_t i=n;i>1;--i){const auto limit=(std::uint64_t{1}<<32)/i*i;
        std::uint32_t x;do{x=next();}while(x>=limit);std::swap(map[i-1],map[x%i]);}
    return map;
}
// Rejection sampling avoids the old byte%6 permutation bias. Refill a bounded
// bulk AES stream only if needed; neither encryption nor MAC runs per sample.
Bytes amplitude_orders(std::size_t count,const Crypto& key,std::uint64_t epoch){
    Bytes result;result.reserve(count);
    const auto batch=std::min<std::size_t>(16384,std::max<std::size_t>(256,count+count/32+64));
    std::uint64_t offset=0;
    while(result.size()<count){
        require(offset<=UINT64_MAX-batch,"triad v3 amplitude stream offset overflow");
        const auto bytes=key.stream(StreamPurpose::Scrambler,epoch,offset,batch);offset+=batch;
        for(const auto byte:bytes){
            if(byte<252)result.push_back(byte%6);
            if(result.size()==count)break;
        }
    }
    return result;
}

Chip independent_pattern(std::uint8_t order_index,const std::uint8_t* phases,
                         const std::uint8_t* outer_radii,const std::uint8_t* outer_phases,
                         double depth){
    constexpr std::array<std::array<unsigned,3>,6> orders{{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}}};
    constexpr std::array<double,3> powers{.4,1.,1.6};
    const auto& order=orders[order_index];Chip result{};double total=0;
    for(unsigned c=0;c<3;++c){
        const auto power=powers[order[c]]*(outer_radii?1+((outer_radii[c]&1U)?depth:-depth):1);
        const auto phase=(phases[c]&7U)*std::numbers::pi/4;
        const auto sign=outer_phases&&(outer_phases[c]&1U)?-1.:1.;
        result[c]=sign*std::polar(std::sqrt(power),phase);total+=power;
    }
    // This balances coefficient power, not the beating envelope of the shaped
    // sum. It preserves every inter-carrier amplitude ratio and phase.
    const auto gain=std::sqrt(3/total);for(auto& value:result)value*=gain;
    return result;
}
}

double effective_sample_rate(const Format& f){
    require(f.sample_rate>0&&f.sample_rate_denominator>0&&
            (f.sample_rate_denominator==1||f.sample_rate==1),
            "triad v3 sample rate must be an integer or reciprocal integer Hz");
    return static_cast<double>(f.sample_rate)/f.sample_rate_denominator;
}

Geometry geometry(const Format& f){
    (void)pulse_shape_name(f.pulse_shape);
    require(std::isfinite(f.added_amplitude_depth)&&f.added_amplitude_depth>0&&f.added_amplitude_depth<1,
            "triad v3 Added amplitude depth must be strictly between zero and one");
    const auto sample_rate=effective_sample_rate(f);
    require(std::isfinite(f.inner_rate_hz)&&f.inner_rate_hz>0&&f.inner_rate_hz<=30e6,"triad v3 invalid Rate");
    require(f.added_symbols>=1&&f.added_symbols<=1000&&f.minimum_inner_coefficients>=64,
            "triad v3 invalid Added Symbols or minimum complexity");
    require(std::isfinite(f.bit_seconds)&&f.bit_seconds>0,"triad v3 invalid duration/sample rate");
    require(sample_rate>=64||!f.fhss,"triad v3 sub-64-Hz source requires FHSS off");
    require(std::isfinite(f.dwell_seconds)&&std::abs(f.dwell_seconds-.4)<1e-12,"triad v3 dwell must be400ms");
    require(std::isfinite(f.quiet_seconds)&&f.quiet_seconds>=0&&std::isfinite(f.ramp_seconds)&&f.ramp_seconds>0&&
        std::isfinite(f.margin_seconds)&&f.margin_seconds>=.001&&f.burst_wait_tiles<=1000,"triad v3 invalid margins/burst");
    Geometry g;g.inner_chip_rate_hz=f.inner_rate_hz/6;g.fine_chip_rate_hz=g.inner_chip_rate_hz*f.added_symbols;
    if(f.fhss_center_aligned)(void)aligned_dwell_cells(f,g.fine_chip_rate_hz);
    g.carrier_spacing_hz=1.25*g.fine_chip_rate_hz;g.occupied_bandwidth_hz=3*g.carrier_spacing_hz;
    require(sample_rate>=4*g.fine_chip_rate_hz&&sample_rate>1.1*g.occupied_bandwidth_hz,
            "triad v3 sample rate cannot contain shaped adjacent carriers");
    require(f.minimum_inner_coefficients<=maximum_tile_cells/f.added_symbols,"triad v3 tile complexity exceeds workspace cap");
    const auto needed=f.minimum_inner_coefficients*f.added_symbols;
    const long double endpoint=8/g.fine_chip_rate_hz+
        std::max(f.ramp_seconds,8/g.fine_chip_rate_hz)+f.margin_seconds;
    const long double hop_guard=f.quiet_seconds+f.ramp_seconds+f.margin_seconds+8/g.fine_chip_rate_hz;
    const long double guaranteed_per_dwell=f.fhss_center_aligned?lower_cells(f,g.fine_chip_rate_hz,3):
        std::floor((f.fhss?f.dwell_seconds-2*hop_guard:f.dwell_seconds)*g.fine_chip_rate_hz);
    if(f.fhss)require(guaranteed_per_dwell>=1,"triad v3 FHSS has no protected dwell cells");
    std::uint64_t tile_dwells=20;
    if(guaranteed_per_dwell>=1){
        const auto lost_endpoints=f.fhss?2.L:2.L*coordinate(endpoint/f.dwell_seconds,"triad v3 endpoint extent overflow");
        const auto lost_truncation=coordinate((f.added_symbols-1)/guaranteed_per_dwell,"triad v3 Added extent overflow");
        tile_dwells=coordinate(tile_dwells+lost_endpoints+lost_truncation,"triad v3 initial tile dwell overflow");
    }
    const auto cell_extent=[&](std::uint64_t dwells){
        return static_cast<long double>(dwells)*f.dwell_seconds*g.fine_chip_rate_hz;
    };
    const auto within_workspace=[&](std::uint64_t dwells){
        const auto cells=cell_extent(dwells);
        return std::isfinite(cells)&&cells<=maximum_tile_cells;
    };
    // Start at exactly the established conservative count. Both protected-cell
    // count and raw extent are monotone, so a workspace boundary can serve as
    // an upper bracket without converting an out-of-range cell count to uint64.
    const auto reached=[&](std::uint64_t dwells){
        return !within_workspace(dwells)||lower_cells(f,g.fine_chip_rate_hz,dwells)>=needed;
    };
    if(!reached(tile_dwells)){
        constexpr auto last_dwell=(std::uint64_t{1}<<63)-1;
        auto low=tile_dwells,high=tile_dwells;
        do{
            require(high<last_dwell,"triad v3 protected tile dwell coordinate overflow");
            high=high>last_dwell/2?last_dwell:2*high;
        }while(!reached(high));
        while(high-low>1){
            const auto middle=low+(high-low)/2;
            if(reached(middle))high=middle;else low=middle;
        }
        tile_dwells=high;
    }
    require(within_workspace(tile_dwells),"triad v3 protected tile exceeds bounded source workspace");
    g.tile_seconds=static_cast<double>(tile_dwells)*f.dwell_seconds;
    const auto max_cells=coordinate(g.tile_seconds*g.fine_chip_rate_hz+3,"triad v3 tile extent overflow");
    require(max_cells<=maximum_tile_cells,"triad v3 tile exceeds bounded source workspace");
    g.payload_fine_cells_per_tile=(lower_cells(f,g.fine_chip_rate_hz,tile_dwells)/f.added_symbols)*f.added_symbols;
    // Preserve complete-tile requests produced by the duration planner. A
    // double product N*tile can divide back to N plus a rounding ulp; that
    // must not append an entire extra private tile. Only this logical count
    // is snapped: sample/buffer bounds retain their conservative ceiling.
    auto tile_ratio=static_cast<long double>(f.bit_seconds)/g.tile_seconds;
    const auto nearest_tile=std::round(tile_ratio);
    const auto tile_roundoff=std::min(1e-8L,4.L*std::numeric_limits<double>::epsilon()*
        std::max(1.L,std::abs(tile_ratio)));
    if(std::abs(tile_ratio-nearest_tile)<=tile_roundoff)tile_ratio=nearest_tile;
    g.tile_count=std::max<std::uint64_t>(1,coordinate(tile_ratio,"triad v3 tile count overflow"));
    // bit_seconds requests active observation time; burst waits extend elapsed time.
    const long double active=g.tile_count*static_cast<long double>(g.tile_seconds);
    const long double elapsed=active+(g.tile_count-1)*static_cast<long double>(f.burst_wait_tiles)*g.tile_seconds;
    g.bit_seconds=static_cast<double>(elapsed);
    g.dwells=coordinate(elapsed/f.dwell_seconds,"triad v3 dwell extent overflow");
    g.fine_cells=coordinate(elapsed*g.fine_chip_rate_hz,"triad v3 fine calendar overflow");
    if(f.fhss_center_aligned){
        const auto physical_tiles=static_cast<long double>(g.tile_count)+(g.tile_count-1)*static_cast<long double>(f.burst_wait_tiles);
        g.dwells=coordinate(physical_tiles*tile_dwells,"triad v3 aligned dwell extent overflow");
        g.fine_cells=coordinate(static_cast<long double>(g.dwells)*aligned_dwell_cells(f,g.fine_chip_rate_hz),
                                "triad v3 aligned fine calendar overflow");
    }
    g.fine_coefficients=coordinate(static_cast<long double>(g.tile_count)*g.payload_fine_cells_per_tile,"triad v3 pattern extent overflow");
    g.inner_coefficients=g.fine_coefficients/f.added_symbols;
    // A guaranteed allocation bound, not measured energy after finite shaping.
    g.nominal_payload_energy_seconds=static_cast<double>(g.fine_coefficients)/g.fine_chip_rate_hz;
    const auto halo=coordinate(8.L*sample_rate/g.fine_chip_rate_hz,"triad v3 pulse halo overflow")+2;
    g.calendar_origin_sample=halo;
    g.source_samples=coordinate(elapsed*sample_rate+2*halo,"triad v3 source extent overflow");
    // Two full coefficient tiles, integer calendars/permutation, temporary
    // keystreams and allocator/crypto overhead. No term grows with bit duration.
    g.source_workspace_bytes=static_cast<std::size_t>(max_cells*176+128*1024);
    return g;
}

struct PatternSource::Impl {
    struct Tile {std::uint64_t id=0,first=0;std::vector<Chip> chips;};
    Format format;Geometry geom;Crypto master;unsigned bit=0;std::uint64_t halo=0;double sample_rate=0;
    std::uint64_t dwell_cells=0,tile_cells=0,protected_first=0,protected_last=0;
    std::array<std::optional<Tile>,2> tiles;
    Impl(const Format& f,const Crypto& key,std::uint8_t source_bit):format(f),geom(v3::geometry(f)),master(key){
        require(source_bit<=1,"triad v3 source must be one binary bit");
        halo=geom.calendar_origin_sample;sample_rate=effective_sample_rate(f);
        if(f.fhss_center_aligned){
            dwell_cells=aligned_dwell_cells(f,geom.fine_chip_rate_hz);
            tile_cells=static_cast<std::uint64_t>(std::llround(geom.tile_seconds/f.dwell_seconds))*dwell_cells;
            const auto range=aligned_protected_range(f,geom.fine_chip_rate_hz);
            protected_first=range.first;protected_last=range.second;
        }
        bit=source_bit;
        if(f.encrypt){
            const auto payload=master.confidentiality_child(child_id("payload",f,0));
            const auto mask=payload.confidentiality_stream(f.epoch,0,1);
            bit^=mask[0]&1U;
        }
    }
    bool active_tile(std::uint64_t physical_tile) const {
        return physical_tile%(format.burst_wait_tiles+1)==0&&
               physical_tile/(format.burst_wait_tiles+1)<geom.tile_count;
    }
    long double center_offset() const {return format.fhss_center_aligned?0.L:.5L;}
    bool eligible(std::uint64_t fine,long double t,long double start,long double end) const {
        const long double edge=8/geom.fine_chip_rate_hz+
            std::max(format.ramp_seconds,8/geom.fine_chip_rate_hz)+format.margin_seconds;
        // Burst starts/ends have their own source ramps. Contiguous tile
        // boundaries do not introduce a public envelope or reset the phase.
        if(format.burst_wait_tiles){if(t<start+edge||t>end-edge)return false;}
        else if(t<edge||t>geom.bit_seconds-edge)return false;
        if(format.fhss_center_aligned){const auto position=fine%dwell_cells;
            if(position<protected_first||position>protected_last)return false;}
        else if(format.fhss){const long double guard=format.quiet_seconds+format.ramp_seconds+
            format.margin_seconds+8/geom.fine_chip_rate_hz;
            const auto position=t-std::floor(t/format.dwell_seconds)*format.dwell_seconds;
            if(position<guard||position>format.dwell_seconds-guard)return false;}
        return true;
    }
    Tile make_tile(std::uint64_t id){
        Tile tile;tile.id=id;
        const long double start=id*static_cast<long double>(geom.tile_seconds),end=start+geom.tile_seconds;
        const auto first=format.fhss_center_aligned?static_cast<long double>(id)*tile_cells:
            std::max(0.L,std::ceil(start*geom.fine_chip_rate_hz-.5L));
        const auto last=format.fhss_center_aligned?first+tile_cells:
            std::max(first,std::ceil(end*geom.fine_chip_rate_hz-.5L));
        tile.first=coordinate(first,"triad v3 tile coordinate overflow");
        const auto count=static_cast<std::size_t>(last-first);require(count<=maximum_tile_cells,"triad v3 tile cell cap");
        tile.chips.resize(count);
        if(!active_tile(id))return tile;
        const auto ordinal=id/(format.burst_wait_tiles+1);
        const auto cover=child(master,"cover-phase",format,ordinal)
            .stream(StreamPurpose::Scrambler,format.epoch,0,count*3);
        const auto cover_orders=amplitude_orders(count,child(master,"cover-amplitude",format,ordinal),format.epoch);
        std::vector<std::size_t> payload;payload.reserve(count);
        for(std::size_t i=0;i<count;++i){
            tile.chips[i]=independent_pattern(cover_orders[i],cover.data()+i*3,nullptr,nullptr,0);
            const auto t=(static_cast<long double>(tile.first+i)+center_offset())/geom.fine_chip_rate_hz;
            if(eligible(tile.first+i,t,start,end))payload.push_back(i);}
        const auto n=(payload.size()/format.added_symbols)*format.added_symbols;
        require(n>=geom.payload_fine_cells_per_tile,"triad v3 tile violated protected allocation bound");
        std::vector<std::size_t> order;
        if(format.added_interleave)order=permutation(n,child(master,"permutation",format,ordinal),format.epoch);
        const auto inner_count=n/format.added_symbols;
        const auto inner_orders=amplitude_orders(inner_count,
            child(master,"inner-amplitude",format,ordinal,bit),format.epoch);
        const auto inner_phase=child(master,"inner-phase",format,ordinal,bit)
            .stream(StreamPurpose::Scrambler,format.epoch,0,inner_count*3);
        const auto outer_radii=format.added_symbols>1?child(master,"added-amplitude",format,ordinal,bit)
            .stream(StreamPurpose::Dsss,format.epoch,0,n*3):Bytes{};
        const auto outer_phase=format.added_symbols>1?child(master,"added-phase",format,ordinal,bit)
            .stream(StreamPurpose::Dsss,format.epoch,0,n*3):Bytes{};
        // Three consecutive byte addresses are the three carrier lanes;
        // logical/Added selects the inner symbol without splitting it.
        for(std::size_t i=0;i<n;++i){const auto logical=format.added_interleave?order[i]:i;
            tile.chips[payload[i]]=independent_pattern(inner_orders[logical/format.added_symbols],
                inner_phase.data()+(logical/format.added_symbols)*3,
                outer_radii.empty()?nullptr:outer_radii.data()+logical*3,
                outer_phase.empty()?nullptr:outer_phase.data()+logical*3,format.added_amplitude_depth);}

        return tile;
    }
    Chip chip(std::uint64_t fine){
        const long double center=(static_cast<long double>(fine)+center_offset())/geom.fine_chip_rate_hz;
        const auto id=format.fhss_center_aligned?fine/tile_cells:
            static_cast<std::uint64_t>(std::floor(center/geom.tile_seconds));
        if(!active_tile(id)||center>=geom.bit_seconds)return {};
        auto& slot=tiles[id%2];if(!slot||slot->id!=id){slot.reset();slot=make_tile(id);}
        require(fine>=slot->first&&fine-slot->first<slot->chips.size(),"triad v3 tile lookup mismatch");
        return slot->chips[static_cast<std::size_t>(fine-slot->first)];
    }
    double envelope(long double t) const {
        return shaping_envelope(format,geom,t);
    }
};
PatternSource::PatternSource(const Format& f,const Crypto& key,std::uint8_t bit):impl_(std::make_unique<Impl>(f,key,bit)){}
PatternSource::~PatternSource()=default;
PatternSource::PatternSource(PatternSource&&) noexcept=default;
PatternSource& PatternSource::operator=(PatternSource&&) noexcept=default;
const Format& PatternSource::format()const{return impl_->format;}
const Geometry& PatternSource::geometry()const{return impl_->geom;}
std::size_t PatternSource::working_bytes()const{return impl_->geom.source_workspace_bytes;}
std::array<Complex,3> PatternSource::pattern_coefficient_at(std::uint64_t fine){
    if(fine>=impl_->geom.fine_cells)return {};
    return impl_->chip(fine);
}
std::array<Complex,3> PatternSource::carrier_samples_at(long double sample){
    Chip output{};const auto& g=impl_->geom;
    if(!std::isfinite(sample)||sample<0||sample>=g.source_samples)return output;
    const long double t=(sample-impl_->halo)/impl_->sample_rate;
    const auto envelope=impl_->envelope(t);if(envelope==0)return output;
    const auto offset=impl_->center_offset();
    const auto first_real=std::max(0.L,std::ceil(t*g.fine_chip_rate_hz-(8+offset)));
    const auto last_real=std::min(static_cast<long double>(g.fine_cells)-1,std::floor(t*g.fine_chip_rate_hz+(8-offset)));
    if(last_real<first_real)return output;
    const auto first=static_cast<std::uint64_t>(first_real),last=static_cast<std::uint64_t>(last_real);
    for(auto fine=first;fine<=last;++fine){const auto pulse=shaping_pulse(impl_->format.pulse_shape,static_cast<double>(t*g.fine_chip_rate_hz-(static_cast<long double>(fine)+offset)));
        const auto chip=impl_->chip(fine);for(unsigned c=0;c<3;++c)output[c]+=chip[c]*(carrier_gain*pulse*envelope);}
    return output;
}
Complex PatternSource::sample_at(long double sample){
    if(!std::isfinite(sample)||sample<0||sample>=impl_->geom.source_samples)return {};
    const auto carriers=carrier_samples_at(sample);const auto t=(sample-impl_->halo)/impl_->sample_rate;
    Complex result{};for(unsigned c=0;c<3;++c){const auto cycles=(static_cast<int>(c)-1)*impl_->geom.carrier_spacing_hz*t;
        result+=carriers[c]*std::polar(1.,static_cast<double>(tau*std::remainder(cycles,1.L)));}return result;
}
void PatternSource::render(std::uint64_t first,std::span<Complex> output){
    require(output.size()<=UINT64_MAX-first,"triad v3 render coordinate overflow");
    for(std::size_t i=0;i<output.size();++i)output[i]=sample_at(static_cast<long double>(first+i));
}
void PatternSource::render_bank(std::span<PatternSource* const> sources,
        long double first_sample,long double sample_step,std::size_t count,
        std::span<Complex> output){
    require(std::isfinite(first_sample)&&std::isfinite(sample_step)&&sample_step>0,
            "triad v3 bank requires a finite positive sample lattice");
    require(!count||sources.size()<=std::numeric_limits<std::size_t>::max()/count,
            "triad v3 render bank extent overflow");
    require(output.size()==sources.size()*count,"triad v3 render bank output extent mismatch");
    require(!count||std::isfinite(first_sample+(count-1)*sample_step),
            "triad v3 render bank coordinate overflow");
    for(auto* source:sources)require(source&&source->impl_,"triad v3 render bank has an empty source");
    if(sources.empty()||!count)return;
    // Deliberately compare all geometry inputs, not key-dependent controls.
    // A conservative mismatch is an error rather than a silently wrong basis.
    const auto geometry_inputs=[](const Format& f){return std::tie(f.inner_rate_hz,
        f.added_symbols,f.sample_rate,f.sample_rate_denominator,f.bit_seconds,
        f.minimum_inner_coefficients,f.fhss,f.fhss_center_aligned,f.dwell_seconds,
        f.quiet_seconds,f.ramp_seconds,f.margin_seconds,f.burst_wait_tiles,f.pulse_shape);};
    const auto& ref=*sources.front()->impl_;const auto& g=ref.geom;
    for(auto* source:sources)require(geometry_inputs(source->impl_->format)==geometry_inputs(ref.format),
            "triad v3 render bank geometry differs");
    struct Point {
        std::uint64_t first=0;
        unsigned length=0;
        std::array<double,17> weights{};
        Chip rotations{};
    };
    static_assert(sizeof(Point)<=256,"render bank point exceeds declared scratch bound");
    constexpr std::size_t block_size=256,maximum_cached_chips=4096;
    std::array<Point,block_size> points;
    std::vector<Chip> chips;chips.reserve(maximum_cached_chips);
    for(std::size_t begin=0;begin<count;begin+=std::min(block_size,count-begin)){
        const auto n=std::min(block_size,count-begin);
        std::uint64_t minimum=UINT64_MAX,maximum=0;
        for(std::size_t i=0;i<n;++i){
            auto& point=points[i];point.length=0;
            const auto sample=first_sample+(begin+i)*sample_step;
            if(sample<0||sample>=g.source_samples)continue;
            const long double t=(sample-ref.halo)/ref.sample_rate;
            const auto envelope=ref.envelope(t);if(envelope==0)continue;
            const auto offset=ref.center_offset();
            const auto first=std::max(0.L,std::ceil(t*g.fine_chip_rate_hz-(8+offset)));
            const auto last=std::min(static_cast<long double>(g.fine_cells)-1,
                                    std::floor(t*g.fine_chip_rate_hz+(8-offset)));
            if(last<first)continue;
            point.first=static_cast<std::uint64_t>(first);
            point.length=static_cast<unsigned>(last-first)+1;
            require(point.length<=point.weights.size(),"triad v3 pulse bank support overflow");
            minimum=std::min(minimum,point.first);maximum=std::max(maximum,point.first+point.length-1);
            for(unsigned j=0;j<point.length;++j){
                const auto pulse=shaping_pulse(ref.format.pulse_shape,static_cast<double>(
                    t*g.fine_chip_rate_hz-(static_cast<long double>(point.first+j)+offset)));
                point.weights[j]=carrier_gain*pulse*envelope;
            }
            for(unsigned c=0;c<3;++c){const auto cycles=(static_cast<int>(c)-1)*g.carrier_spacing_hz*t;
                point.rotations[c]=std::polar(1.,static_cast<double>(tau*std::remainder(cycles,1.L)));}
        }
        const bool cached=minimum!=UINT64_MAX&&maximum-minimum<maximum_cached_chips;
        for(std::size_t row=0;row<sources.size();++row){
            auto& impl=*sources[row]->impl_;
            if(cached){
                chips.resize(static_cast<std::size_t>(maximum-minimum+1));
                for(std::size_t j=0;j<chips.size();++j)chips[j]=impl.chip(minimum+j);
            }
            for(std::size_t i=0;i<n;++i){
                const auto& point=points[i];Chip carriers{};
                for(unsigned j=0;j<point.length;++j){
                    const auto chip=cached?chips[static_cast<std::size_t>(point.first-minimum+j)]:impl.chip(point.first+j);
                    // Same ordered coefficient accumulation and final rotations
                    // as sample_at; no recurrence or pulse approximation.
                    for(unsigned c=0;c<3;++c)carriers[c]+=chip[c]*point.weights[j];
                }
                Complex value{};
                for(unsigned c=0;c<3;++c)value+=carriers[c]*point.rotations[c];
                output[row*count+begin+i]=value;
            }
        }
    }
}
} // namespace datapump::triad::v3
