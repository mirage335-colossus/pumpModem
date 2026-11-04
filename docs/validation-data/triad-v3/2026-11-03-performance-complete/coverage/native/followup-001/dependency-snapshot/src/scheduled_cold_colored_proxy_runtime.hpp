#pragma once
#include "scheduled_cold_colored_runtime.hpp"

namespace datapump::modem::detail::scheduled_cold_colored {
// This producer is usable only after the caller has proved equality of the
// ORIGINAL nonempty source-owner set. Tiny source displacement alone does not
// prove that: a useful mask edge coincident with a chip edge can create a new
// singleton through the original source-piece rounding.
struct ProxyRow {
    enum : unsigned { transfer=1, mask_edge=2, window_left=4, window_right=8 };
    std::uint64_t owner=no_owner;
    unsigned membership=0;
    double residual=0;
};
inline bool fractional_membership_margin(long double source,long double displacement) {
    if(!std::isfinite(source) || !std::isfinite(displacement) || displacement<0)return false;
    const auto fraction=source-std::floor(source);
    return std::min(fraction,1-fraction)>displacement;
}

class RuntimeProxy {
public:
    // Indices in the current q fragment, independent of the lane input start.
    // An empty domains span retains the historical full-fragment contract.
    struct Domain {std::size_t first=0,end=0;};
private:
    struct Cell {
        Direct::Errors fields{{{0,0,0,0,true},{0,0,0,0,true}}};
        std::array<double,2> ordinary_dot{},ordinary_gram{};
        std::uint64_t ordinary_terms=0;
        bool ordinary_valid=true;
        std::uint64_t membership=0;
    };
    Direct direct_;
    std::vector<Cell> cells_;
    std::vector<Direct::Errors> errors_;
    std::array<double,2> qmax_{};
    std::uint64_t owner_=no_owner,maximum_cell_=0,next_fragment_=0;
    double union_energy_=0,carrier_radius_=0,carrier_magnitude_=0;
    std::size_t prefix_samples_=0;
    bool initialized_=false,finished_=false;
    bool bounded_error_planes_=false;

    void invalidate(std::size_t j) {for(auto& summary:direct_.lanes[j].chips)summary.valid=false;}
    void member(std::size_t j,const scheduled_acquisition_tile::Fragment& fragment,
                const scheduled_acquisition_tile::Input& input,std::size_t i,std::size_t at,
                double radius) {
        auto& cell=cells_[j];
        if(++cell.membership>2){invalidate(j);return;}
        for(unsigned bit=0;bit<2;++bit) {
            const auto allowance=sample_allowance(input.pcm[at],fragment.rows[i][bit],input.oscillator[at],
                radius,carrier_radius_);
            if(!allowance.valid){invalidate(j);continue;}
            auto& field=cell.fields[bit];
            field.dot=cert::add(field.dot,allowance.dot);
            field.gram=cert::add(field.gram,allowance.gram);
            field.energy=cert::add(field.energy,allowance.energy);
        }
    }
    void prepare_errors() {
        for(std::size_t j=0;j<direct_.lanes.size();++j)for(unsigned bit=0;bit<2;++bit) {
            const auto native=direct_.lanes[j].cell[bit].count;
            if(!native){if(cells_[j].membership)invalidate(j);errors_[j][bit]={0,0,0,0,true};continue;}
            if(native>maximum_cell_ || native>(std::numeric_limits<std::size_t>::max()-128*prefix_samples_-384)/128) {
                errors_[j][bit]={};continue;
            }
            const auto n=static_cast<std::size_t>(native)+2;
            const auto length=static_cast<double>(n),q=qmax_[bit],q2=cert::mul(q,q);
            const auto rho=cert::add(cert::gamma(128*n+128*prefix_samples_+128,std::numeric_limits<double>::epsilon()/2),
                cert::mul(4*prefix_samples_,cert::gamma(8*prefix_samples_+8,std::numeric_limits<double>::epsilon()/2)));
            const auto root=cert::root(cert::mul(length,union_energy_));
            const auto dot=cert::mul(cert::mul(8*rho,q),root);
            const auto gram=cert::mul(cert::mul(32*rho,length),q2);
            const auto carrier_dot=cert::mul(cert::mul(carrier_radius_,q),root);
            const auto carrier_gram=cert::mul(cert::mul(length,q2),cert::add(
                cert::mul(2*carrier_magnitude_,carrier_radius_),cert::mul(carrier_radius_,carrier_radius_)));
            const auto amplify=cert::add(1,cert::gamma(12*n+32,std::numeric_limits<double>::epsilon()/2));
            const auto tiny=cert::mul(cert::mul(128*length*(prefix_samples_+1)+128,std::numeric_limits<double>::denorm_min()),
                cert::add(1,cert::add(q,cert::add(q2,cert::root(union_energy_)))));
            const auto& field=cells_[j].fields[bit];
            double template_dot=0,template_gram=0;
            if(bounded_error_planes_ && cells_[j].ordinary_terms) {
                const auto& cell=cells_[j];
                const auto terms=cell.ordinary_terms;
                if(!cell.ordinary_valid || terms!=native || terms>1048576 ||
                   !std::isfinite(cell.ordinary_dot[bit]) || !std::isfinite(cell.ordinary_gram[bit])) {
                    errors_[j][bit]={};continue;
                }
                // Positive RN reductions of the same row expressions. Bound
                // all multiply/add rounding and propagated subnormal error
                // once per cell; membership allowances stay outward-rounded.
                const auto g=cert::gamma(8*static_cast<std::size_t>(terms)+16,std::numeric_limits<double>::epsilon()/2);
                const auto denominator=cert::down(1-g);
                const auto eta=std::numeric_limits<double>::denorm_min();
                const auto dot_absolute=cert::mul(cert::mul(2*static_cast<double>(terms),eta),
                    cert::add(static_cast<double>(std::numeric_limits<float>::max()),2));
                const auto gram_absolute=cert::mul(512*static_cast<double>(terms),eta);
                template_dot=cert::divide(cert::add(cell.ordinary_dot[bit],dot_absolute),denominator);
                template_gram=cert::divide(cert::add(cell.ordinary_gram[bit],gram_absolute),denominator);
            }
            // Field sums below use outward-rounded nonnegative additions.
            // Original prefix subtraction and its different association are
            // bounded separately; native direct reduction is added by Direct.
            errors_[j][bit]={cert::add(cert::mul(amplify,cert::add(cert::add(field.dot,template_dot),cert::add(dot,carrier_dot))),tiny),
                cert::add(cert::mul(amplify,cert::add(cert::add(field.gram,template_gram),cert::add(gram,carrier_gram))),tiny),
                cert::add(cert::mul(amplify,cert::add(field.energy,cert::mul(4*rho,union_energy_))),tiny),0,true};
        }
    }
    void flush() {
        if(owner_==no_owner)return;
        prepare_errors();
        for(std::size_t j=0;j<direct_.lanes.size();++j){direct_.flush(j,errors_[j]);direct_.lanes[j].owner=no_owner;cells_[j]={};}
        owner_=no_owner;qmax_={};
    }
public:
    RuntimeProxy(std::size_t starts,std::uint64_t maximum_cell,double union_energy,
                 double carrier_radius,double carrier_magnitude,std::size_t prefix_samples,std::uint64_t count_radius=2,
                 bool bounded_error_planes=false)
        :direct_(starts,true,false,count_radius,true),cells_(starts),errors_(starts),maximum_cell_(maximum_cell),
         union_energy_(union_energy),carrier_radius_(carrier_radius),carrier_magnitude_(carrier_magnitude),prefix_samples_(prefix_samples),
         bounded_error_planes_(bounded_error_planes && cert::environment<double>()) {
        if(count_radius>2 || !starts || !maximum_cell || maximum_cell>1048576 || !prefix_samples || prefix_samples>512 ||
           !std::isfinite(union_energy) || union_energy<0 || !std::isfinite(carrier_radius) || carrier_radius<0 ||
           !std::isfinite(carrier_magnitude) || carrier_magnitude<0)throw Error("invalid original colored proxy contract");
    }
    std::size_t working_bytes()const{return sizeof(*this)+direct_.working_bytes()+cells_.capacity()*sizeof(Cell)+errors_.capacity()*sizeof(Direct::Errors);}
    std::uint64_t sample_pairs()const{return direct_.sample_pairs;}
    void add(scheduled_acquisition_tile::Input input,scheduled_acquisition_tile::Fragment fragment,
             std::span<const std::size_t> starts,std::span<const ProxyRow> geometry,std::stop_token stop={},
             std::span<const std::array<double,2>> norm_bounds={},std::span<const Domain> domains={}) {
        namespace tile=scheduled_acquisition_tile;
        if(finished_)throw Error("original colored proxy appended after finish");
        tile::validate(input,fragment,starts,stop);
        if(starts.size()!=direct_.lanes.size() || geometry.size()!=fragment.rows.size())throw Error("original colored proxy extent mismatch");
        if(!domains.empty()) {
            if(domains.size()!=starts.size())throw Error("original colored domain extent mismatch");
            for(const auto domain:domains)if(domain.first>domain.end || domain.end>fragment.rows.size())
                throw Error("original colored domain invalid");
        }
        const auto inside=[&](std::size_t j,std::size_t i){return domains.empty() || (domains[j].first<=i && i<domains[j].end);};
        // Optional norms must bound these EXACT stored native q rows. The
        // immutable SourceRows producer computes them once with cert::norm.
        if(!norm_bounds.empty() && norm_bounds.size()!=fragment.rows.size())throw Error("original colored norm extent mismatch");
        const auto norm=[&](std::size_t i,unsigned bit){const auto value=norm_bounds.empty()?cert::norm(fragment.rows[i][bit]):norm_bounds[i][bit];
            if(!std::isfinite(value) || value<0)throw Error("original colored norm bound invalid");return value;};
        if(initialized_ && fragment.physical_offset<next_fragment_)throw Error("original colored proxy fragments reverse");
        next_fragment_=tile::add_coordinate(fragment.physical_offset,fragment.rows.size());initialized_=true;
        for(std::size_t i=0;i<fragment.rows.size();++i) {
            if((i&255U)==0)tile::check_stop(stop);
            const auto& row=geometry[i];if(row.owner==no_owner)continue;
            if(!std::isfinite(row.residual) || row.residual<0 || row.membership>15)throw Error("invalid original colored proxy row");
            const auto selected=tile::selected(fragment,i);
            const std::array<double,2> row_norm{norm(i,0),norm(i,1)};
            // These are the SAME row expressions formerly evaluated for every
            // start. Preserve their FP64 product association before reduction.
            const auto ordinary_e=carrier_magnitude_*row.residual;
            const auto outward_e=bounded_error_planes_?0:cert::mul(carrier_magnitude_,row.residual);
            std::array<double,2> row_gram{};
            for(unsigned bit=0;bit<2;++bit) {
                if(bounded_error_planes_) {
                    const auto m=carrier_magnitude_*row_norm[bit];
                    row_gram[bit]=(2*m)*ordinary_e+ordinary_e*ordinary_e;
                } else {
                    const auto m=cert::mul(carrier_magnitude_,row_norm[bit]);
                    row_gram[bit]=cert::add(cert::mul(2*m,outward_e),cert::mul(outward_e,outward_e));
                }
            }
            if(owner_!=row.owner) {
                if(owner_!=no_owner && row.owner<owner_)throw Error("original colored proxy source reversed");
                // The first central row of the next chip may instead belong
                // to the previous ORIGINAL cell. Add it before closing that
                // old cell. Window endpoints are never interior transfers.
                if(owner_!=no_owner && row.owner==owner_+1 && (row.membership&ProxyRow::transfer)) {
                    for(std::size_t j=0;j<starts.size();++j) {
                        if(!inside(j,i))continue;
                        const auto at=i+starts[j];if(!tile::present(input,at)){invalidate(j);continue;}
                        member(j,fragment,input,i,at,row.residual);
                    }
                    for(unsigned bit=0;bit<2;++bit)qmax_[bit]=std::max(qmax_[bit],cert::add(row_norm[bit],row.residual));
                }
                flush();owner_=row.owner;for(auto& lane:direct_.lanes)lane.owner=owner_;
            }
            for(unsigned bit=0;bit<2;++bit)qmax_[bit]=std::max(qmax_[bit],cert::add(row_norm[bit],row.residual));
            for(std::size_t j=0;j<starts.size();++j) {
                if(!inside(j,i))continue;
                const auto at=i+starts[j];
                if(!tile::present(input,at)){if(selected || row.membership)invalidate(j);continue;}
                if(row.membership)member(j,fragment,input,i,at,row.residual);
                if(!selected)continue;
                const auto x=static_cast<double>(input.pcm[at]);const auto o=input.oscillator[at];
                const auto x2=x*x,absolute_x=std::abs(x);
                if(bounded_error_planes_) {
                    auto& cell=cells_[j];++cell.ordinary_terms;
                    if(carrier_magnitude_>2 || row.residual>32 || !std::isfinite(x))cell.ordinary_valid=false;
                }
                for(unsigned bit=0;bit<2;++bit) {
                    const auto q=fragment.rows[i][bit];const auto a=q.real(),b=q.imag(),c=a*o.real()-b*o.imag(),s=b*o.real()+a*o.imag();
                    auto& f=direct_.lanes[j].cell[bit];f.xc+=x*c;f.xs+=x*s;f.cc+=c*c;f.ss+=s*s;f.cs+=c*s;f.energy+=x2;++f.count;
                    auto& field=cells_[j].fields[bit];
                    const auto qnorm=row_norm[bit];
                    if(bounded_error_planes_) {
                        auto& cell=cells_[j];if(qnorm>32)cell.ordinary_valid=false;
                        cell.ordinary_dot[bit]+=absolute_x*ordinary_e;
                        cell.ordinary_gram[bit]+=row_gram[bit];
                    } else {
                        field.dot=cert::add(field.dot,cert::mul(absolute_x,outward_e));
                        field.gram=cert::add(field.gram,row_gram[bit]);
                    }
                }
                ++direct_.sample_pairs;
            }
        }
    }
    std::vector<std::array<Summary,2>> finish(){if(finished_)throw Error("original colored proxy repeated finish");prepare_errors();finished_=true;return direct_.finish(errors_);}
};
} // namespace datapump::modem::detail::scheduled_cold_colored
