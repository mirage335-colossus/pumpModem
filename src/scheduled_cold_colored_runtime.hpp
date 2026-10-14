#pragma once
#include "scheduled_cold_colored_certificate.hpp"

namespace datapump::modem::detail::scheduled_cold_colored {
// Exact-r1 ORIGINAL raw/prefix producer adapter. Keep a retained max q for the
// current SOURCE cell, independently of PCM/caller/fragment/null boundaries.
// A prospective carrier cap is used during parallel preparation; the serial
// cold consumer must validate its actual restart radius/magnitude against it.
class RuntimeDirect {
    Direct direct_;
    std::vector<Direct::Errors> errors_;
    std::array<double,2> qmax_{};
    std::uint64_t owner_=no_owner;
    double union_energy_=0,carrier_radius_=0,carrier_magnitude_=0;
    std::size_t prefix_samples_=0;
    std::uint64_t maximum_cell_=0;
    void prepare_errors() {
        namespace c=scheduled_acquisition_certificate;
        for(std::size_t j=0;j<direct_.lanes.size();++j)for(unsigned bit=0;bit<2;++bit) {
            const auto n=direct_.lanes[j].cell[bit].count;
            if(!n){errors_[j][bit]={0,0,0,0,true};continue;}
            if(n>maximum_cell_ || n>(std::numeric_limits<std::size_t>::max()-128*prefix_samples_-128)/128) {
                errors_[j][bit]={};continue;
            }
            const auto rho=c::add(c::gamma(128*static_cast<std::size_t>(n)+128*prefix_samples_+128,
                std::numeric_limits<double>::epsilon()/2),c::mul(4*prefix_samples_,
                c::gamma(8*prefix_samples_+8,std::numeric_limits<double>::epsilon()/2)));
            const auto length=static_cast<double>(n),q=qmax_[bit],q2=c::mul(q,q);
            const auto dot=c::mul(c::mul(8*rho,q),c::root(c::mul(length,union_energy_)));
            const auto gram=c::mul(c::mul(32*rho,length),q2);
            const auto carrier_dot=c::mul(c::mul(carrier_radius_,q),c::root(c::mul(length,union_energy_)));
            const auto carrier_gram=c::mul(c::mul(length,q2),c::add(
                c::mul(2*carrier_magnitude_,carrier_radius_),c::mul(carrier_radius_,carrier_radius_)));
            const auto amplify=c::add(1,c::gamma(12*static_cast<std::size_t>(n)+32,
                std::numeric_limits<double>::epsilon()/2));
            const auto tiny=c::mul(c::mul(128*length*(prefix_samples_+1)+128,
                std::numeric_limits<double>::denorm_min()),c::add(1,c::add(q,c::add(q2,c::root(union_energy_)))));
            errors_[j][bit]={c::add(c::mul(amplify,c::add(dot,carrier_dot)),tiny),
                c::add(c::mul(amplify,c::add(gram,carrier_gram)),tiny),
                c::add(c::mul(amplify,c::mul(4*rho,union_energy_)),tiny),0,true};
        }
    }
    void flush() {
        if(owner_==no_owner)return;
        prepare_errors();
        for(std::size_t j=0;j<direct_.lanes.size();++j){direct_.flush(j,errors_[j]);direct_.lanes[j].owner=no_owner;}
        owner_=no_owner;qmax_={};
    }
public:
    explicit RuntimeDirect(std::size_t starts,std::uint64_t maximum_cell,
            double union_energy,double carrier_radius,double carrier_magnitude,std::size_t prefix_samples)
        :direct_(starts,true,true),errors_(starts),union_energy_(union_energy),
         carrier_radius_(carrier_radius),carrier_magnitude_(carrier_magnitude),
         prefix_samples_(prefix_samples),maximum_cell_(maximum_cell) {
        if(!starts || !maximum_cell || maximum_cell>1048576 || !prefix_samples || prefix_samples>512 ||
           !std::isfinite(union_energy) || union_energy<0 || !std::isfinite(carrier_radius) || carrier_radius<0 ||
           !std::isfinite(carrier_magnitude) || carrier_magnitude<0)
            throw Error("invalid original colored runtime contract");
        for(auto& lane:errors_)for(auto& error:lane)error={0,0,0,0,true};
    }
    std::size_t working_bytes()const{return sizeof(*this)+direct_.working_bytes()+errors_.capacity()*sizeof(Direct::Errors);}
    std::uint64_t sample_pairs()const{return direct_.sample_pairs;}
    void add(scheduled_acquisition_tile::Input input,scheduled_acquisition_tile::Fragment fragment,
             std::span<const std::size_t> starts,std::span<const std::uint64_t> owners,std::stop_token stop={}) {
        namespace t=scheduled_acquisition_tile;
        t::validate(input,fragment,starts,stop);
        if(owners.size()!=fragment.rows.size())throw Error("original colored owner extent mismatch");
        for(std::size_t first=0;first<fragment.rows.size();) {
            t::check_stop(stop);
            if(!t::selected(fragment,first)){++first;continue;}
            const auto owner=owners[first];
            if(owner==no_owner)throw Error("original colored sample has no source owner");
            if(owner_!=owner){if(owner_!=no_owner && owner<owner_)throw Error("original colored owner reversed");flush();owner_=owner;}
            auto end=first+1;
            while(end<fragment.rows.size() && (!t::selected(fragment,end) || owners[end]==owner))++end;
            for(std::size_t i=first;i<end;++i)if(t::selected(fragment,i))for(unsigned bit=0;bit<2;++bit)
                qmax_[bit]=std::max(qmax_[bit],cert::norm(fragment.rows[i][bit]));
            const auto length=end-first+starts.back();
            t::Input slice{input.pcm.subspan(first,length),input.oscillator.subspan(first,length),
                input.observed.empty()?std::span<const std::uint8_t>{}:input.observed.subspan(first,length),
                t::add_coordinate(input.physical_first,first)};
            t::Fragment rows{fragment.rows.subspan(first,end-first),
                fragment.mask.empty()?std::span<const std::uint8_t>{}:fragment.mask.subspan(first,end-first),
                t::add_coordinate(fragment.physical_offset,first)};
            direct_.add(slice,rows,starts,owners.subspan(first,end-first),errors_,stop);first=end;
        }
    }
    std::vector<std::array<Summary,2>> finish() {prepare_errors();return direct_.finish(errors_);}
};
} // namespace datapump::modem::detail::scheduled_cold_colored
