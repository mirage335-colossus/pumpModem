#pragma once
#include "scheduled_acquisition_certificate.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <vector>
#include <openssl/crypto.h>

namespace datapump::modem::detail::scheduled_residual_prefix {
namespace cert=scheduled_acquisition_certificate;
class Error:public std::runtime_error {public:using std::runtime_error::runtime_error;};
inline constexpr std::size_t maximum_input=40960,maximum_rows=32768,maximum_starts=8192;
inline void stop(std::stop_token token){if(token.stop_requested())throw Error("residual prefix cancelled");}
inline std::size_t add(std::size_t a,std::size_t b){if(b>std::numeric_limits<std::size_t>::max()-a)throw Error("residual prefix size overflow");return a+b;}
inline std::size_t mul(std::size_t a,std::size_t b){if(a && b>std::numeric_limits<std::size_t>::max()/a)throw Error("residual prefix size overflow");return a*b;}
// Local virtual coordinates alone are insufficient if this prefix is cached.
// The immutable capture binding, version, actual physical first and virtual
// first must match the original input tile. No frequency belongs in this key.
struct Identity {
    std::uint64_t capture_owner=0,capture_generation=0,virtual_first=0;
    std::int64_t physical_first=0;
    friend bool operator==(const Identity&,const Identity&)=default;
};
enum class SampleState:std::uint8_t {unknown=0,observed=1,excluded=2};
struct Bound {double upper=cert::infinity;bool valid=false;};
// Prefix arithmetic is ALWAYS outward FP64. A wider search policy does not
// remove the need for this error certificate, nor does it change timestamps.
class AbsolutePrefix {
    Identity identity_;
    std::vector<cert::Interval> absolute_;
    std::vector<std::uint64_t> unknown_;
public:
    static std::size_t reservation(std::size_t n){if(n>maximum_input)throw Error("residual prefix input cap exceeded");
        return add(sizeof(AbsolutePrefix)+128,mul(2*(n+1),sizeof(cert::Interval)+sizeof(std::uint64_t)));}
    AbsolutePrefix(Identity identity,std::span<const float> pcm,std::span<const SampleState> states,
                   std::size_t workspace,std::stop_token token={}) :identity_(identity) {
        if(!cert::environment<double>() || !identity.capture_owner || !identity.capture_generation ||
           (!states.empty() && states.size()!=pcm.size()) || pcm.size()>maximum_input ||
           pcm.size()>std::numeric_limits<std::uint64_t>::max()-identity.virtual_first ||
           reservation(pcm.size())>workspace)throw Error("unsupported residual prefix input contract");
        stop(token);absolute_.resize(pcm.size()+1);unknown_.resize(pcm.size()+1);
        if(working_bytes()>workspace)throw Error("residual prefix capacity exceeds workspace");
        for(std::size_t i=0;i<pcm.size();++i){if((i&255U)==0)stop(token);
            const auto state=states.empty()?SampleState::observed:states[i];
            if(state!=SampleState::observed && state!=SampleState::excluded && state!=SampleState::unknown)
                throw Error("invalid residual prefix sample state");
            absolute_[i+1]=absolute_[i];unknown_[i+1]=unknown_[i];
            if(state==SampleState::unknown){++unknown_[i+1];continue;}
            // excluded is a CALLER PROOF about the original observation set,
            // never an inference from q==0, a null selected lane, or EOF.
            if(state==SampleState::excluded)continue;
            if(!std::isfinite(pcm[i]))throw Error("nonfinite residual prefix PCM");
            const auto x=std::abs(static_cast<double>(pcm[i])); // exact float->double and sign removal
            absolute_[i+1]=cert::plus(absolute_[i],cert::Interval::point(x));
            if(!absolute_[i+1].bounded())throw Error("residual prefix arithmetic overflow");
        }
        stop(token);
    }
    ~AbsolutePrefix(){if(!absolute_.empty())OPENSSL_cleanse(absolute_.data(),absolute_.size()*sizeof(absolute_[0]));}
    AbsolutePrefix(const AbsolutePrefix&)=delete;
    AbsolutePrefix& operator=(const AbsolutePrefix&)=delete;
    std::size_t working_bytes()const{return sizeof(*this)+128+absolute_.capacity()*sizeof(absolute_[0])+unknown_.capacity()*sizeof(unknown_[0]);}
    std::size_t size()const{return absolute_.empty()?0:absolute_.size()-1;}
    const Identity& identity()const{return identity_;}
    Bound absolute_upper(Identity expected,std::size_t first,std::size_t end)const {
        if(!cert::environment<double>() || expected!=identity_ || first>end || end>size() ||
           unknown_[end]!=unknown_[first])return {};
        // Difference of TWO intervals: an upward-only cumulative prefix is
        // insufficient after subtraction (and can underestimate catastrophically).
        const auto value=cert::minus(absolute_[end],absolute_[first]);
        if(!value.bounded())return {};
        return {std::max(0.,value.hi),true};
    }
};
class ResidualRuns {
public:
    struct Run {std::size_t first=0,end=0;double upper=0;};
private:
    std::vector<Run> runs_;
    std::size_t expected_=0,rows_=0,limit_=0,bin_width_=0;
    bool valid_=true,sealed_=false,coarsened_=false;
public:
    static std::size_t reservation(std::size_t runs){if(!runs || runs>maximum_rows)throw Error("residual run cap exceeded");
        return add(sizeof(ResidualRuns)+128,mul(2*runs,sizeof(Run)));}
    ResidualRuns(std::size_t rows,std::size_t maximum_runs,std::size_t workspace):expected_(rows),limit_(maximum_runs) {
        if(!cert::environment<double>() || rows>maximum_rows || reservation(maximum_runs)>workspace)
            throw Error("unsupported residual run contract");
        runs_.reserve(maximum_runs);if(working_bytes()>workspace)throw Error("residual run capacity exceeds workspace");
    }
    ~ResidualRuns(){if(!runs_.empty())OPENSSL_cleanse(runs_.data(),runs_.size()*sizeof(runs_[0]));}
    ResidualRuns(const ResidualRuns&)=delete;
    ResidualRuns& operator=(const ResidualRuns&)=delete;
    std::size_t working_bytes()const{return sizeof(*this)+128+2*runs_.capacity()*sizeof(Run);} // reserve bounded coarsening replacement too
    std::size_t rows()const{return rows_;}
    bool coarsened()const{return coarsened_;}
    std::span<const Run> runs()const{return runs_;}
    bool append(double outward_residual) {
        if(!valid_ || sealed_ || rows_>=expected_ || !std::isfinite(outward_residual) || outward_residual<0){valid_=false;return false;}
        if(coarsened_){auto& bin=runs_[rows_/bin_width_];bin.upper=std::max(bin.upper,outward_residual);++rows_;return true;}
        if(!runs_.empty() && std::bit_cast<std::uint64_t>(runs_.back().upper)==std::bit_cast<std::uint64_t>(outward_residual))
            runs_.back().end=++rows_;
        else if(runs_.size()<limit_)runs_.push_back({rows_,++rows_,outward_residual});
        else {
            // Bound the representation without replacing every quiet row by
            // one fragment-wide jump maximum. Fixed contiguous bins retain the
            // MAX of every overlapping old/new row. No support is discarded.
            bin_width_=1+(expected_-1)/limit_;
            try {
                std::vector<Run> bins;bins.reserve(limit_);
                if((bins.capacity()+runs_.capacity())*sizeof(Run)+sizeof(*this)+128>reservation(limit_)) {valid_=false;return false;}
                for(std::size_t first=0;first<expected_;first+=bin_width_)
                    bins.push_back({first,std::min(expected_,first+bin_width_),0});
                for(const auto& run:runs_)for(auto j=run.first/bin_width_;j<bins.size() && bins[j].first<run.end;++j)
                    bins[j].upper=std::max(bins[j].upper,run.upper);
                bins[rows_/bin_width_].upper=std::max(bins[rows_/bin_width_].upper,outward_residual);
                if(!runs_.empty())OPENSSL_cleanse(runs_.data(),runs_.size()*sizeof(Run));
                runs_.swap(bins);++rows_;coarsened_=true;
            }catch(const std::bad_alloc&){valid_=false;return false;}
        }
        return true;
    }
    bool seal()noexcept {sealed_=true;valid_=valid_ && rows_==expected_;return valid_;}
    Bound dot_upper(const AbsolutePrefix& prefix,Identity expected,std::size_t start,
                    double original_carrier_bound,bool complete_union_support,std::stop_token token={})const {
        if(!valid_ || !sealed_ || !complete_union_support || !cert::environment<double>() ||
           !std::isfinite(original_carrier_bound) || original_carrier_bound<0 || start>prefix.size() || rows_>prefix.size()-start)return {};
        double sum=0;
        for(const auto& run:runs_){stop(token);if(run.upper==0)continue;
            const auto mass=prefix.absolute_upper(expected,start+run.first,start+run.end);if(!mass.valid)return {};
            sum=cert::add(sum,cert::mul(run.upper,mass.upper));if(!std::isfinite(sum))return {};
        }
        const auto result=cert::mul(original_carrier_bound,sum);return {result,std::isfinite(result)};
    }
};
// Optional original-order fragment reduction. It exposes nothing until every
// fragment finished. A partial cancellation/unknown/overflow invalidates the
// ENTIRE vector; caller retains its preceding conservative bound.
class DotTotals {
    std::vector<double> totals_;
    bool valid_=true,sealed_=false;
public:
    static std::size_t reservation(std::size_t starts){if(starts>maximum_starts)throw Error("residual start cap exceeded");
        return add(sizeof(DotTotals)+128,mul(2*starts,sizeof(double)));}
    DotTotals(std::size_t starts,std::size_t workspace){if(reservation(starts)>workspace)throw Error("residual totals exceed workspace");
        totals_.resize(starts);if(working_bytes()>workspace)throw Error("residual totals capacity exceeds workspace");}
    ~DotTotals(){if(!totals_.empty())OPENSSL_cleanse(totals_.data(),totals_.size()*sizeof(totals_[0]));}
    std::size_t working_bytes()const{return sizeof(*this)+128+totals_.capacity()*sizeof(double);}
    bool add_fragment(const AbsolutePrefix& prefix,const ResidualRuns& residual,Identity expected,
            std::span<const std::size_t> starts,double carrier_bound,bool complete_union_support,std::stop_token token={}) {
        if(!valid_ || sealed_ || starts.size()!=totals_.size()){valid_=false;return false;}
        try {for(std::size_t j=0;j<starts.size();++j){stop(token);
            const auto bound=residual.dot_upper(prefix,expected,starts[j],carrier_bound,complete_union_support,token);
            if(!bound.valid){valid_=false;return false;}
            totals_[j]=cert::add(totals_[j],bound.upper);if(!std::isfinite(totals_[j])){valid_=false;return false;}}
        }catch(...){valid_=false;throw;}
        return true;
    }
    std::span<const double> finish(bool complete_fragment_cover,std::stop_token token={}) {
        if(token.stop_requested()){valid_=false;stop(token);}sealed_=true;
        return valid_ && complete_fragment_cover?std::span<const double>{totals_}:std::span<const double>{};
    }
};
} // namespace datapump::modem::detail::scheduled_residual_prefix
