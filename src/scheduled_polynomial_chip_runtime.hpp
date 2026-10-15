#pragma once
#include "scheduled_cold_colored_source_rows.hpp"
#include "scheduled_polynomial_chip_moments.hpp"
#include <openssl/crypto.h>

namespace datapump::modem::detail::scheduled_polynomial_chip {
namespace color=scheduled_cold_colored;
namespace cert=scheduled_acquisition_certificate;
namespace tile=scheduled_acquisition_tile;

// Private, source-only compression. Strict construction consumes an existing
// Original membership proof. NativeTemplateTag groups only stored C34 template
// rows selected by mask &&0<=source<S; it grants NO Original owner/mask proof.
class SourceModels {
public:
    struct NativeTemplateTag {explicit NativeTemplateTag()=default;};
    struct NativeOwnerSpan {
        std::uint64_t physical_first=0,physical_end=0,owner=color::no_owner;
        std::size_t row_first=0;
    };
    struct Cell {
        std::size_t first=0,end=0,run=0;
        std::uint64_t owner=color::no_owner,physical=0;
        SourceCell model{};
        std::array<double,2> qmax{},q_square{};
        double proxy_square=0;
        // Source-only enclosure of cast-double shaper argument minus local
        // integer-row * actual native rate; scanned once with the q model.
        cert::Interval argument_intercept{};
        bool unit_gain=false;
        bool full=false,native_template_only=false;
    };
    color::SourceRows::Key key;
    const color::SourceRows* source_identity=nullptr;
    std::size_t rows=0;
    bool native_template_only=false,source_sealed=false;
    long double geometry_delta=0;
    color::SourceRows::GeometryMode geometry_mode=color::SourceRows::GeometryMode::unspecified;
    std::uint64_t geometry_revision=0;
    std::vector<Cell> cells;
    std::vector<NativeOwnerSpan> native_spans;
    std::uint64_t native_selected_count=0;
    static std::size_t native_span_limit(std::uint64_t symbol_samples) {
        return std::min(color::SourceRows::maximum_rows,
            2*(static_cast<std::size_t>(symbol_samples)/34+2)+2*color::SourceRows::maximum_runs);
    }
    static std::size_t reservation(std::uint64_t symbol_samples) {
        if(!symbol_samples || symbol_samples>color::SourceRows::maximum_rows)
            return std::numeric_limits<std::size_t>::max();
        return tile::add_size(sizeof(SourceModels)+8192,
            tile::add_size(tile::multiply_size(2*(static_cast<std::size_t>(symbol_samples)/34+2),sizeof(Cell)),
                tile::multiply_size(2*native_span_limit(symbol_samples),sizeof(NativeOwnerSpan))));
    }
    std::size_t working_bytes()const{return sizeof(*this)+cells.capacity()*sizeof(Cell)+native_spans.capacity()*sizeof(NativeOwnerSpan)+128;}
    bool matches(const color::SourceRows& source)const {
        return !native_template_only && source_identity==&source && key==source.key && rows==source.rows.size() &&
            geometry_delta==source.geometry_delta && geometry_mode==source.geometry_mode &&
            geometry_revision==source.geometry_revision && source.geometry_ready;
    }
    bool matches_native(const color::SourceRows& source)const {
        // Native models bind sealed stored rows/key, including rate. Mutable
        // Original geometry is neither consumed nor certified by this API.
        return native_template_only && source_sealed && source.sealed &&
            source_identity==&source && key==source.key && rows==source.rows.size();
    }
    SourceModels(const color::SourceRows& source,std::uint64_t symbol_samples,std::stop_token stop={})
        :SourceModels(source,symbol_samples,false,stop) {}
    SourceModels(const color::SourceRows& source,std::uint64_t symbol_samples,NativeTemplateTag,std::stop_token stop={})
        :SourceModels(source,symbol_samples,true,stop) {}
private:
    SourceModels(const color::SourceRows& source,std::uint64_t symbol_samples,bool native,std::stop_token stop)
        :key(source.key),source_identity(&source),rows(source.rows.size()),native_template_only(native),source_sealed(source.sealed),
         geometry_delta(source.geometry_delta),geometry_mode(source.geometry_mode),geometry_revision(source.geometry_revision) {
        if((native?!source.sealed:(!source.geometry_ready || source.geometry.size()!=rows)) ||
           source.mask.size()!=rows || source.norms.size()!=rows || source.gain.size()!=rows || source.source.size()!=rows || (native && source.source.size()!=rows) ||
           !symbol_samples || symbol_samples>color::SourceRows::maximum_rows)
            throw Error("polynomial source geometry unavailable");
        if(!native && std::any_of(source.geometry.begin(),source.geometry.end(),[](const auto& row){return row.membership!=0;}))
            throw Error("polynomial source needs exact membership geometry");
        const auto geometry_at=[&](std::size_t i) {
            if(!native)return source.geometry[i];
            const auto s=source.source[i];
            if(!source.mask[i] || !std::isfinite(s) || s<0 || s>=symbol_samples)return color::ProxyRow{};
            return color::ProxyRow{static_cast<std::uint64_t>(s/34),0,0};
        };
        const auto maximum=static_cast<std::size_t>(symbol_samples)/34+2;
        cells.reserve(maximum);
        std::size_t at=0,run=0;
        while(at<rows) {
            tile::check_stop(stop);
            if(geometry_at(at).owner==color::no_owner){++at;continue;}
            const auto owner=geometry_at(at).owner;
            if(owner>=maximum || (!cells.empty() && owner<=cells.back().owner))
                throw Error("polynomial source owners reverse or overflow");
            auto end=at+1;
            while(end<rows && (geometry_at(end).owner==owner || geometry_at(end).owner==color::no_owner))++end;
            // Unselected trailing rows are unnecessary; the next cell retains
            // its own actual first selected row and physical provenance.
            while(end>at && geometry_at(end-1).owner==color::no_owner)--end;
            while(run<source.runs.size() && at>=source.runs[run].first+source.runs[run].count)++run;
            if(run==source.runs.size() || at<source.runs[run].first)throw Error("polynomial source run missing");
            Cell cell;cell.native_template_only=native;cell.first=at;cell.end=end;cell.owner=owner;cell.run=run;
            cell.physical=tile::add_coordinate(source.runs[run].physical_offset,at-source.runs[run].first);
            cell.full=end-at==34 && end<=source.runs[run].first+source.runs[run].count;
            if(cell.full){cell.argument_intercept={std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()};
                cell.unit_gain=true;}
            if(cell.full)for(auto i=at;i<end;++i) {
                const auto geometry=geometry_at(i);
                if(!source.mask[i] || geometry.owner!=owner || geometry.membership ||
                   !std::isfinite(geometry.residual) || geometry.residual<0){cell.full=false;break;}
                cell.proxy_square=cert::add(cell.proxy_square,cert::mul(geometry.residual,geometry.residual));
                cell.unit_gain=cell.unit_gain && source.gain[i]==1;
                if(!std::isfinite(source.source[i]) || (i>at && !(source.source[i]>source.source[i-1])))
                    throw Error("polynomial source arguments are not finite ordered");
                const cert::Interval rate{cert::down(static_cast<double>(source.key.rate)),cert::up(static_cast<double>(source.key.rate))};
                const auto intercept=cert::minus(cert::Interval::point(static_cast<double>(source.source[i])),
                    cert::times(cert::Interval::point(static_cast<double>(i-at)),rate));
                cell.argument_intercept.lo=std::min(cell.argument_intercept.lo,intercept.lo);
                cell.argument_intercept.hi=std::max(cell.argument_intercept.hi,intercept.hi);
                for(unsigned bit=0;bit<2;++bit) {
                    cell.qmax[bit]=std::max(cell.qmax[bit],cert::add(source.norms[i][bit],geometry.residual));
                    cell.q_square[bit]=cert::add(cell.q_square[bit],cert::mul(source.norms[i][bit],source.norms[i][bit]));
                }
            }
            if(cell.full)cell.model=build_source(std::span<const std::array<std::complex<double>,2>>(source.rows).subspan(at,34),stop);
            if(cells.size()==maximum)throw Error("polynomial source cell cap exceeded");
            cells.push_back(std::move(cell));at=end;
        }
        if(native) {
            const auto span_limit=native_span_limit(symbol_samples);native_spans.reserve(span_limit);
            std::size_t native_run=0;
            for(std::size_t i=0;i<rows;++i) {
                if((i&255U)==0)tile::check_stop(stop);
                const auto owner=geometry_at(i).owner;if(owner==color::no_owner)continue;
                while(native_run<source.runs.size() && i>=source.runs[native_run].first+source.runs[native_run].count)++native_run;
                if(native_run==source.runs.size() || i<source.runs[native_run].first)throw Error("native source span run missing");
                const auto physical=tile::add_coordinate(source.runs[native_run].physical_offset,i-source.runs[native_run].first);
                if(!native_spans.empty() && native_spans.back().owner==owner && native_spans.back().physical_end==physical &&
                   native_spans.back().row_first+(native_spans.back().physical_end-native_spans.back().physical_first)==i)
                    native_spans.back().physical_end=tile::add_coordinate(physical,1);
                else {
                    if(native_spans.size()==span_limit)throw Error("native source span cap exceeded");
                    native_spans.push_back({physical,tile::add_coordinate(physical,1),owner,i});
                }
                ++native_selected_count;
            }
        }
        tile::check_stop(stop);
        if(working_bytes()>reservation(symbol_samples))throw Error("polynomial source reservation exceeded");
    }
public:
    ~SourceModels(){if(!cells.empty())OPENSSL_cleanse(cells.data(),cells.size()*sizeof(Cell));}
};

// Error against the Original rounded q/processing-carrier/prefix evaluation.
// Polynomial reduction and q-to-polynomial residual are certified by contract()
// separately; no direct-row reassociation allowance is used for those fields.
inline std::array<cert::ReferenceErrors,2> original_errors(const SourceModels::Cell& cell,
        double union_energy,double carrier_radius,double carrier_magnitude,std::size_t prefix_samples) {
    std::array<cert::ReferenceErrors,2> result{};
    if(!cell.full || cell.native_template_only || !std::isfinite(union_energy) || union_energy<0 ||
       !std::isfinite(carrier_radius) || carrier_radius<0 || !std::isfinite(carrier_magnitude) ||
       carrier_magnitude<0 || carrier_magnitude>2 || !prefix_samples || prefix_samples>512)return result;
    constexpr std::size_t n=36;
    const auto length=static_cast<double>(n);
    const auto rho=cert::add(cert::gamma(128*n+128*prefix_samples+128,std::numeric_limits<double>::epsilon()/2),
        cert::mul(4*prefix_samples,cert::gamma(8*prefix_samples+8,std::numeric_limits<double>::epsilon()/2)));
    const auto root=cert::root(cert::mul(length,union_energy));
    const auto amplify=cert::add(1,cert::gamma(12*n+32,std::numeric_limits<double>::epsilon()/2));
    const auto residual=cert::root(cell.proxy_square);
    for(unsigned bit=0;bit<2;++bit) {
        const auto q=cell.qmax[bit],q2=cert::mul(q,q);
        const auto dot=cert::mul(cert::mul(8*rho,q),root);
        const auto gram=cert::mul(cert::mul(32*rho,length),q2);
        const auto carrier_dot=cert::mul(cert::mul(carrier_radius,q),root);
        const auto carrier_gram=cert::mul(cert::mul(length,q2),cert::add(
            cert::mul(2*carrier_magnitude,carrier_radius),cert::mul(carrier_radius,carrier_radius)));
        const auto template_dot=cert::mul(cert::mul(carrier_magnitude,cert::root(union_energy)),residual);
        const auto template_gram=cert::mul(cert::mul(carrier_magnitude,carrier_magnitude),cert::add(
            cert::mul(cert::mul(2,cert::root(cell.q_square[bit])),residual),cell.proxy_square));
        const auto tiny=cert::mul(cert::mul(128*length*(prefix_samples+1)+128,std::numeric_limits<double>::denorm_min()),
            cert::add(1,cert::add(q,cert::add(q2,cert::root(union_energy)))));
        result[bit]={cert::add(cert::mul(amplify,cert::add(template_dot,cert::add(dot,carrier_dot))),tiny),
            cert::add(cert::mul(amplify,cert::add(template_gram,cert::add(gram,carrier_gram))),tiny),
            cert::add(cert::mul(amplify,cert::mul(4*rho,union_energy)),tiny),0,true};
    }
    return result;
}

inline void join(color::Summary& target,const color::Summary& cell) {
    if(!target.valid)return;
    if(!cell.valid || !cell.nested || !cell.owner_set_certified ||
       cell.rank>std::numeric_limits<std::uint64_t>::max()-target.rank ||
       cell.count>std::numeric_limits<std::uint64_t>::max()-target.count){target.valid=false;return;}
    target.projected=cert::plus(target.projected,cell.projected);
    target.rank+=cell.rank;target.count+=cell.count;
    target.nested=target.nested && cell.nested;
    target.valid=target.projected.bounded();
}
} // namespace datapump::modem::detail::scheduled_polynomial_chip
