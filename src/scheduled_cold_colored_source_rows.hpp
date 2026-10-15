#pragma once
#include "scheduled_cold_colored_proxy_runtime.hpp"
#include <openssl/crypto.h>
#include <optional>

namespace datapump::modem::detail::scheduled_cold_colored {
// Bound the executed quintic, without clamping its waveform value. For stored
// t in [0,1], t^3(10+t(-15+6t)) is in [0,1] in exact arithmetic. Its seven
// rounded operations have absolute-polynomial sum <=31; gamma(8) also covers
// their association. Two such factors cover combined Sched_Hop/FHSS gain.
inline double smooth_gain_upper() {
    static const double bound=[] {
        const auto u=std::numeric_limits<double>::epsilon()/2;
        const auto eta=std::numeric_limits<double>::denorm_min();
        const auto single=cert::add(1,cert::add(cert::mul(31,cert::gamma(8,u)),128*eta));
        return cert::add(cert::mul(cert::mul(single,single),cert::add(1,cert::gamma(1,u))),8*eta);
    }();
    return bound;
}

// Compact SOURCE-row storage: no PCM, carrier, FFT or candidate/H dimension.
// Masked active rows retain their original q/source/gain identity. Quiet runs
// have no stored rows. The serial consumer supplies its own capture/carrier.
class SourceRows {
public:
    struct Key {
        long double origin=0,anchor_fraction=0;
        std::uint64_t phase=0,index=0,anchor_sample=0;
        // Actual central source clock is part of every stored q/gain/mask row.
        // Trailing default preserves existing explicit rate-one test keys.
        long double rate=1;
        bool operator==(const Key&)const=default;
    };
    struct Run {std::uint64_t physical_offset=0;std::size_t first=0,count=0;};
    static constexpr std::size_t maximum_rows=1048576,maximum_runs=4096;
    Key key;
    std::uint64_t shift=0;
    std::vector<std::array<std::complex<double>,2>> rows;
    std::vector<long double> source;
    std::vector<double> gain;
    std::vector<std::uint8_t> mask;
    std::vector<std::array<double,2>> norms;
    std::vector<ProxyRow> geometry;
    std::vector<Run> runs;
    std::size_t limit=0;
    long double source_margin=1;
    bool sealed=false;
    // Immutable producer capability: active central source[-1,symbol+1] rows.
    bool window_halo=false;
    enum class GeometryMode : unsigned { unspecified, fractional_proxy, integral_original };
    bool geometry_ready=false;
    GeometryMode geometry_mode=GeometryMode::unspecified;
    std::uint64_t geometry_revision=0;
    long double geometry_delta=0;
    double geometry_square=0,geometry_l1=0,geometry_gram=0;
    explicit SourceRows(Key k,std::uint64_t coordinate_shift,std::size_t bound):key(k),shift(coordinate_shift),limit(bound) {
        if(!bound || bound>maximum_rows || !std::isfinite(k.origin) || !std::isfinite(k.anchor_fraction) ||
           k.anchor_fraction<0 || k.anchor_fraction>=1 || !std::isfinite(k.rate) || k.rate<=0)throw Error("colored source rows bound unsupported");
        rows.reserve(bound);source.reserve(bound);gain.reserve(bound);mask.reserve(bound);norms.reserve(bound);geometry.reserve(bound);runs.reserve(maximum_runs);
    }
    ~SourceRows(){if(!rows.empty())OPENSSL_cleanse(rows.data(),rows.size()*sizeof(rows[0]));}
    static std::size_t reservation(std::size_t bound) {
        namespace tile=scheduled_acquisition_tile;
        if(!bound || bound>maximum_rows)return std::numeric_limits<std::size_t>::max();
        return tile::add_size(sizeof(SourceRows)+8192,tile::add_size(
            tile::multiply_size(2*bound,sizeof(std::array<std::complex<double>,2>)+sizeof(long double)+sizeof(double)+sizeof(std::uint8_t)+sizeof(std::array<double,2>)+sizeof(ProxyRow)),
            tile::multiply_size(maximum_runs,sizeof(Run))));
    }
    std::size_t working_bytes()const{return sizeof(*this)+rows.capacity()*sizeof(rows[0])+source.capacity()*sizeof(source[0])+
        gain.capacity()*sizeof(gain[0])+mask.capacity()*sizeof(mask[0])+norms.capacity()*sizeof(norms[0])+geometry.capacity()*sizeof(geometry[0])+runs.capacity()*sizeof(Run);}
    void append(std::uint64_t physical,std::array<std::complex<double>,2> q,long double coordinate,double power,std::uint8_t selected) {
        if(sealed || rows.size()>=limit)throw Error("colored source-row producer exhausted its reservation");
        if(!std::isfinite(coordinate))throw Error("colored source-row producer coordinate is not finite");
        if(!std::isfinite(power) || power<0 || power>smooth_gain_upper())throw Error("colored source-row producer gain exceeds its numerical bound");
        if(!cert::finite(q[0]) || !cert::finite(q[1]))throw Error("colored source-row producer coefficient is not finite");
        if(!runs.empty() && physical<scheduled_acquisition_tile::add_coordinate(runs.back().physical_offset,runs.back().count))
            throw Error("colored source rows overlap or reverse");
        if(runs.empty() || physical!=scheduled_acquisition_tile::add_coordinate(runs.back().physical_offset,runs.back().count)) {
            if(runs.size()==maximum_runs)throw Error("colored source-run bound unsupported");
            runs.push_back({physical,rows.size(),0});
        }
        rows.push_back(q);source.push_back(coordinate);gain.push_back(power);mask.push_back(selected);
        norms.push_back({cert::norm(q[0]),cert::norm(q[1])});++runs.back().count;
    }
    void seal(){if(sealed || rows.empty())throw Error("invalid colored source-row finalization");geometry.resize(rows.size());sealed=true;}
    // Geometry arrays are mutable optional derived state. A consumer must bind
    // BOTH mode and revision; equal displacement alone does not identify proof.
    void invalidate_geometry() noexcept {geometry_ready=false;geometry_mode=GeometryMode::unspecified;}
    void publish_geometry(GeometryMode mode) {
        if(mode==GeometryMode::unspecified || geometry_revision==std::numeric_limits<std::uint64_t>::max())
            throw Error("colored geometry identity exhausted");
        geometry_mode=mode;++geometry_revision;geometry_ready=true;
    }
    // Call only after lazy_colored_eligible has established exact ORIGINAL
    // rate-one integer source arithmetic for EVERY translated target lane.
    // A rounded zero displacement or small fractional margin is insufficient.
    bool prepare_integral_geometry(std::uint64_t symbol_samples,std::uint64_t chip_samples,
            bool integral_membership_proven,std::stop_token stop={}) {
        invalidate_geometry();
        if(!integral_membership_proven || key.rate!=1 || !sealed || !cert::coordinate_environment() || !chip_samples ||
           !symbol_samples || geometry.size()!=rows.size() || mask.size()!=rows.size() || source.size()!=rows.size())return false;
        std::uint64_t previous=no_owner;
        for(std::size_t i=0;i<rows.size();++i) {
            if((i&255U)==0)scheduled_acquisition_tile::check_stop(stop);
            auto& result=geometry[i];result={};
            if(!mask[i])continue;
            const auto s=source[i];
            if(!std::isfinite(s) || s<0 || s>=symbol_samples || std::trunc(s)!=s || s>=0x1p52L)return false;
            const auto owner=static_cast<std::uint64_t>(s)/chip_samples;
            if(previous!=no_owner && owner<previous)return false;
            result={owner,0,0};previous=owner;
        }
        scheduled_acquisition_tile::check_stop(stop);
        geometry_delta=0;geometry_square=geometry_l1=geometry_gram=0;
        publish_geometry(GeometryMode::integral_original);return true;
    }
    // This object is owned by one immutable receiver (keys/calendar/config).
    // Rate-one central rows depend on p-origin. An exact integer translation
    // preserves the correctly rounded subtraction, hence every shaped double
    // argument, coefficient, gain and mask. Actual PCM/carrier is NOT cached.
    std::optional<long double> translation_to(const Key& next) const {
        if(!sealed || key.rate!=1 || next.rate!=1 || key.phase!=next.phase || key.index!=next.index ||
           key.anchor_sample!=next.anchor_sample || key.anchor_fraction!=next.anchor_fraction ||
           !cert::coordinate_environment() ||
           std::abs(key.origin)>=0x1p31L || std::abs(next.origin)>=0x1p31L)return {};
        const auto a=next.origin,b=-key.origin,d=a+b;
        const auto recovered=d-a;
        const auto residual=(a-(d-recovered))+(b-recovered); // Knuth TwoSum.
        if(!std::isfinite(d) || residual!=0 || std::trunc(d)!=d || std::abs(d)>=0x1p31L)return {};
        for(const auto& run:runs) {
            if(!run.count || run.physical_offset>=0x10000000000000ULL ||
               run.count>0x10000000000000ULL-run.physical_offset || shift>=0x10000000000000ULL)return {};
            const auto first=static_cast<long double>(run.physical_offset)-shift;
            const auto last=first+run.count-1;
            // Leaves ample room for all bounded admitted-start offsets.
            for(const auto p:{first,last,first+d,last+d})
                if(!std::isfinite(p) || std::trunc(p)!=p || std::abs(p)>=0x1p40L)return {};
        }
        return d;
    }
};
} // namespace datapump::modem::detail::scheduled_cold_colored
