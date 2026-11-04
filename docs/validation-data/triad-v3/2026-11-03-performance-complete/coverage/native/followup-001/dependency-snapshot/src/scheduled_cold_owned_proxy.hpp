#pragma once
#include "scheduled_cold_colored_proxy_runtime.hpp"

namespace datapump::modem::detail::scheduled_cold_colored {
// One event, exact ORIGINAL owner/mask, approximate stored coefficients. The
// caller must enumerate ALL Original observations, including source-window
// entrants. No mask/count-radius inference is made here. This intentionally
// remains a Direct producer: its additional whole-domain contraction is counted
// independently and must not be described as a shared FFT acceleration.
class OwnedProxy {
    RuntimeProxy chips_;
    std::array<CorrelationFit,2> whole_{};
    std::array<double,2> qmax_{},dot_{},gram_{};
    double union_energy_,carrier_radius_,carrier_magnitude_;
    std::size_t prefix_samples_;
    std::uint64_t rows_=0;
    bool valid_=true,finished_=false;

    FitIntervals whole_interval(unsigned bit) const {
        const auto& f=whole_[bit];
        if(!valid_ || !f.count || f.count!=rows_ || f.count>1048576 || !cert::environment<double>())return {};
        const auto count=static_cast<std::size_t>(f.count),n=count+2;
        const auto length=static_cast<double>(n),q=qmax_[bit],q2=cert::mul(q,q);
        // Same conservative Original prefix/reduction/carrier contract as
        // RuntimeProxy, now applied to the entire actual observation set.
        const auto rho=cert::add(cert::gamma(128*n+128*prefix_samples_+128,std::numeric_limits<double>::epsilon()/2),
            cert::mul(4*prefix_samples_,cert::gamma(8*prefix_samples_+8,std::numeric_limits<double>::epsilon()/2)));
        const auto root=cert::root(cert::mul(length,union_energy_));
        const auto rounding_dot=cert::mul(cert::mul(8*rho,q),root);
        const auto rounding_gram=cert::mul(cert::mul(32*rho,length),q2);
        const auto carrier_dot=cert::mul(cert::mul(carrier_radius_,q),root);
        const auto carrier_gram=cert::mul(cert::mul(length,q2),cert::add(
            cert::mul(2*carrier_magnitude_,carrier_radius_),cert::mul(carrier_radius_,carrier_radius_)));
        const auto amplify=cert::add(1,cert::gamma(12*n+32,std::numeric_limits<double>::epsilon()/2));
        const auto tiny=cert::mul(cert::mul(128*length*(prefix_samples_+1)+128,std::numeric_limits<double>::denorm_min()),
            cert::add(1,cert::add(q,cert::add(q2,cert::root(union_energy_)))));
        const auto g=cert::gamma(8*count+16,std::numeric_limits<double>::epsilon()/2);
        const auto denominator=cert::down(1-g),eta=std::numeric_limits<double>::denorm_min();
        const auto dot_absolute=cert::mul(cert::mul(2*static_cast<double>(count),eta),
            cert::add(static_cast<double>(std::numeric_limits<float>::max()),2));
        const auto gram_absolute=cert::mul(512*static_cast<double>(count),eta);
        const auto template_dot=cert::divide(cert::add(dot_[bit],dot_absolute),denominator);
        const auto template_gram=cert::divide(cert::add(gram_[bit],gram_absolute),denominator);
        const auto original_dot=cert::add(cert::mul(amplify,cert::add(template_dot,cert::add(rounding_dot,carrier_dot))),tiny);
        const auto original_gram=cert::add(cert::mul(amplify,cert::add(template_gram,cert::add(rounding_gram,carrier_gram))),tiny);
        const auto original_energy=cert::add(cert::mul(amplify,cert::mul(4*rho,union_energy_)),tiny);
        // Bound this producer's separate whole RN reduction as Direct does.
        const auto native_gamma=cert::gamma(12*count+32,std::numeric_limits<double>::epsilon()/2);
        const auto native_denominator=cert::down(1-native_gamma);
        const auto native_tiny=cert::mul(12*static_cast<double>(count)+32,eta);
        const auto energy_upper=cert::divide(cert::add(std::max(0.,f.energy),native_tiny),native_denominator);
        const auto trace_upper=cert::divide(cert::add(cert::up(std::max(0.,f.cc+f.ss)),native_tiny),native_denominator);
        const auto local=cert::scalar_reference_errors(count,energy_upper,trace_upper,0);
        if(!local.valid)return {};
        return enclose(f,cert::add(local.dot,original_dot),cert::add(local.gram,original_gram),
            cert::add(local.energy,original_energy));
    }
public:
    struct Result {std::array<Summary,2> chips;std::array<FitIntervals,2> whole;bool valid=false;};
    OwnedProxy(std::uint64_t maximum_cell,double union_energy,double carrier_radius,
            double carrier_magnitude,std::size_t prefix_samples)
        :chips_(1,maximum_cell,union_energy,carrier_radius,carrier_magnitude,prefix_samples,0,true),
         union_energy_(union_energy),carrier_radius_(carrier_radius),carrier_magnitude_(carrier_magnitude),prefix_samples_(prefix_samples) {
        if(!cert::environment<double>() || carrier_magnitude>2)throw Error("owned proxy requires qualified bounded binary64");
    }
    std::size_t working_bytes()const{return sizeof(*this)+chips_.working_bytes();}
    std::uint64_t selected_rows()const{return rows_;}
    std::uint64_t chip_pairs()const{return chips_.sample_pairs();}
    std::uint64_t whole_pairs()const{return rows_;}
    void add(scheduled_acquisition_tile::Input input,scheduled_acquisition_tile::Fragment fragment,
            std::span<const ProxyRow> exact_membership,std::span<const std::array<double,2>> norms,
            std::stop_token stop={}) {
        namespace tile=scheduled_acquisition_tile;
        if(finished_)throw Error("owned proxy appended after finish");
        if(norms.size()!=fragment.rows.size())throw Error("owned proxy norm extent mismatch");
        const std::array<std::size_t,1> start{0};
        chips_.add(input,fragment,start,exact_membership,stop,norms);
        for(std::size_t i=0;i<fragment.rows.size();++i) {
            if((i&255U)==0)tile::check_stop(stop);
            if(!tile::selected(fragment,i))continue;
            if(exact_membership[i].owner==no_owner || exact_membership[i].membership || !tile::present(input,i)) {
                valid_=false;continue;
            }
            if(rows_==1048576){valid_=false;continue;}
            ++rows_;
            const auto x=static_cast<double>(input.pcm[i]),absolute_x=std::abs(x),x2=x*x;
            const auto o=input.oscillator[i];const auto e=carrier_magnitude_*exact_membership[i].residual;
            if(!std::isfinite(x) || exact_membership[i].residual>32)valid_=false;
            for(unsigned bit=0;bit<2;++bit) {
                const auto q=fragment.rows[i][bit];const auto a=q.real(),b=q.imag();
                const auto c=a*o.real()-b*o.imag(),s=b*o.real()+a*o.imag();
                auto& f=whole_[bit];f.xc+=x*c;f.xs+=x*s;f.cc+=c*c;f.ss+=s*s;f.cs+=c*s;f.energy+=x2;++f.count;
                if(!std::isfinite(norms[i][bit]) || norms[i][bit]<0 || norms[i][bit]>32)valid_=false;
                const auto m=carrier_magnitude_*norms[i][bit];
                dot_[bit]+=absolute_x*e;gram_[bit]+=(2*m)*e+e*e;
                qmax_[bit]=std::max(qmax_[bit],cert::add(norms[i][bit],exact_membership[i].residual));
            }
        }
    }
    Result finish(bool complete_original_support,std::stop_token stop={}) {
        if(finished_)throw Error("owned proxy repeated finish");
        tile_check(stop);auto chips=chips_.finish();tile_check(stop);finished_=true;
        Result result{chips.front(),{whole_interval(0),whole_interval(1)},false};
        result.valid=valid_ && complete_original_support;
        for(unsigned bit=0;bit<2;++bit) {
            result.chips[bit].owner_set_certified=complete_original_support;
            result.valid=result.valid && result.chips[bit].valid && result.chips[bit].nested &&
                finite(result.whole[bit]) && result.chips[bit].count==rows_;
            if(!result.valid)result.chips[bit].valid=false;
        }
        return result;
    }
private:
    static void tile_check(std::stop_token stop){scheduled_acquisition_tile::check_stop(stop);}
};
} // namespace datapump::modem::detail::scheduled_cold_colored
