#include "datapump/triad_modem_v3.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <string_view>
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
Crypto child(const Crypto& key,std::string_view purpose,const Format& f,std::uint64_t tile){
    const std::string_view prefix="datapump/experimental-triad/v3/tiled-1/";
    Bytes id(prefix.begin(),prefix.end());id.insert(id.end(),purpose.begin(),purpose.end());id.push_back(0);
    append64(id,f.epoch);append64(id,f.symbol_ordinal);append64(id,tile);
    if(f.fhss_center_aligned){const std::string_view alignment="/fhss-center-aligned";
        id.insert(id.end(),alignment.begin(),alignment.end());}
    return Crypto(key.mac(id));
}
double smooth(double x){x=std::clamp(x,0.,1.);return x*x*x*(10+x*(-15+6*x));}
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
Chip pattern(const std::uint8_t* bytes,unsigned bit,const std::uint8_t* outer){
    constexpr std::array<std::array<unsigned,3>,6> orders{{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}}};
    constexpr std::array<double,3> amplitude{.6324555320336758664,1.,1.2649110640673517328};
    const auto& order=orders[bytes[0]%6];Chip result{};
    for(unsigned c=0;c<3;++c){const auto phase=(bytes[1+bit*3+c]%8)*std::numbers::pi/4+
        (outer?outer[c]%4:0)*std::numbers::pi/2;
        result[c]=std::polar(amplitude[order[(c+bit)%3]],phase);}
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
        const auto mask=child(master,"payload",f,0).stream(StreamPurpose::Data,f.epoch,0,1);
        bit=source_bit^(f.encrypt?(mask[0]&1U):0U);
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
        const auto cover=child(master,"cover",format,ordinal).stream(StreamPurpose::Scrambler,format.epoch,0,count*8);
        std::vector<std::size_t> payload;payload.reserve(count);
        for(std::size_t i=0;i<count;++i){tile.chips[i]=pattern(cover.data()+i*8,0,nullptr);
            const auto t=(static_cast<long double>(tile.first+i)+center_offset())/geom.fine_chip_rate_hz;
            if(eligible(tile.first+i,t,start,end))payload.push_back(i);}
        const auto n=(payload.size()/format.added_symbols)*format.added_symbols;
        require(n>=geom.payload_fine_cells_per_tile,"triad v3 tile violated protected allocation bound");
        const auto inner=child(master,"inner",format,ordinal).stream(StreamPurpose::Scrambler,format.epoch,0,n/format.added_symbols*8);
        const auto outer=child(master,"outer",format,ordinal).stream(StreamPurpose::Dsss,format.epoch,0,n*3);
        std::vector<std::size_t> order;
        if(format.added_interleave)order=permutation(n,child(master,"permutation",format,ordinal),format.epoch);
        for(std::size_t i=0;i<n;++i){const auto logical=format.added_interleave?order[i]:i;
            tile.chips[payload[i]]=pattern(inner.data()+(logical/format.added_symbols)*8,bit,
                format.added_symbols>1?outer.data()+logical*3:nullptr);}
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
        if(t<0||t>=geom.bit_seconds)return 0;
        const long double edge=std::max(format.ramp_seconds,8/geom.fine_chip_rate_hz);
        double result=smooth(static_cast<double>(t/edge))*smooth(static_cast<double>((geom.bit_seconds-t)/edge));
        if(format.burst_wait_tiles){const auto id=static_cast<std::uint64_t>(std::floor(t/geom.tile_seconds));
            if(!active_tile(id))return 0;
            const auto within=t-id*static_cast<long double>(geom.tile_seconds);
            result*=smooth(static_cast<double>(within/edge))*smooth(static_cast<double>((geom.tile_seconds-within)/edge));}
        if(format.fhss){const auto within=t-std::floor(t/format.dwell_seconds)*format.dwell_seconds;
            const auto distance=std::min(within,format.dwell_seconds-within);
            result*=smooth(static_cast<double>((distance-format.quiet_seconds)/format.ramp_seconds));}
        return result;
    }
};
PatternSource::PatternSource(const Format& f,const Crypto& key,std::uint8_t bit):impl_(std::make_unique<Impl>(f,key,bit)){}
PatternSource::~PatternSource()=default;
PatternSource::PatternSource(PatternSource&&) noexcept=default;
PatternSource& PatternSource::operator=(PatternSource&&) noexcept=default;
const Format& PatternSource::format()const{return impl_->format;}
const Geometry& PatternSource::geometry()const{return impl_->geom;}
std::size_t PatternSource::working_bytes()const{return impl_->geom.source_workspace_bytes;}
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
    for(auto fine=first;fine<=last;++fine){const auto pulse=modem::pattern_pulse(static_cast<double>(t*g.fine_chip_rate_hz-(static_cast<long double>(fine)+offset)));
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
} // namespace datapump::triad::v3
