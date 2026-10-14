#pragma once
#include "scheduled_cold_colored_proxy_runtime.hpp"
#include <openssl/crypto.h>
#include <optional>

namespace datapump::modem::detail::scheduled_cold_colored {
// Compact SOURCE-row storage: no PCM, carrier, FFT or candidate/H dimension.
// Masked active rows retain their original q/source/gain identity. Quiet runs
// have no stored rows. The serial consumer supplies its own capture/carrier.
class SourceRows {
public:
    struct Key {
        long double origin=0,anchor_fraction=0;
        std::uint64_t phase=0,index=0,anchor_sample=0;
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
    bool geometry_ready=false;
    long double geometry_delta=0;
    double geometry_square=0,geometry_l1=0,geometry_gram=0;
    explicit SourceRows(Key k,std::uint64_t coordinate_shift,std::size_t bound):key(k),shift(coordinate_shift),limit(bound) {
        if(!bound || bound>maximum_rows || !std::isfinite(k.origin) || !std::isfinite(k.anchor_fraction) ||
           k.anchor_fraction<0 || k.anchor_fraction>=1)throw Error("colored source rows bound unsupported");
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
        if(sealed || rows.size()>=limit || !std::isfinite(coordinate) || !std::isfinite(power) || power<0 || power>1 ||
           !cert::finite(q[0]) || !cert::finite(q[1]))throw Error("invalid colored source-row producer");
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
    // This object is owned by one immutable receiver (keys/calendar/config).
    // Rate-one central rows depend on p-origin. An exact integer translation
    // preserves the correctly rounded subtraction, hence every shaped double
    // argument, coefficient, gain and mask. Actual PCM/carrier is NOT cached.
    std::optional<long double> translation_to(const Key& next) const {
        if(!sealed || key.phase!=next.phase || key.index!=next.index ||
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
