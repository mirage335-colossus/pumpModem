#pragma once
#include "datapump/types.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace datapump::helper {
// Optional receiver front end for an adjacent helper. This is a real linear
// bandstop filter, not tone subtraction. Its symmetric FIR returns only fully
// observed windows: output zero is input delay(), and no missing PCM or EOF
// zeros are inserted. The caller must retain that input-coordinate offset.
class PayloadFilter {
    std::vector<double> taps_;
    std::vector<float> history_;
    std::size_t next_=0,seen_=0;
    std::uint64_t output_=0;
public:
    PayloadFilter()=default;
    PayloadFilter(std::uint32_t rate,double center) {
        constexpr double half_width=225;
        if(!rate||!std::isfinite(center)||center<=half_width||center+half_width>=rate/2.)
            throw Error("adjacent helper filter is outside the receiver passband");
        const auto count=static_cast<std::size_t>(std::ceil(rate*.04))|1U;
        if(count<3||count>4097)throw Error("adjacent helper filter exceeds its optional workspace bound");
        taps_.resize(count);history_.resize(count);
        const auto middle=count/2;double sum=0;
        for(std::size_t i=0;i<count;++i) {
            const auto k=static_cast<double>(i)-middle;
            const auto window=.42-.5*std::cos(2*std::numbers::pi*i/(count-1))+
                .08*std::cos(4*std::numbers::pi*i/(count-1));
            const auto low=k==0?2*half_width/rate:
                std::sin(2*std::numbers::pi*half_width*k/rate)/(std::numbers::pi*k);
            taps_[i]=(i==middle?1.:0.)-2*low*std::cos(2*std::numbers::pi*center*k/rate)*window;
            sum+=taps_[i];
        }
        for(auto& value:taps_)value/=sum;
    }
    bool enabled()const noexcept{return !taps_.empty();}
    std::size_t delay()const noexcept{return taps_.size()/2;}
    std::size_t allocated_bytes()const noexcept {
        return taps_.capacity()*sizeof(double)+history_.capacity()*sizeof(float);
    }
    std::uint64_t output_samples()const noexcept{return output_;}
    std::size_t push(std::span<const float> input,std::span<float> output) {
        if(output.size()<input.size())throw Error("helper filter output span is too small");
        if(!enabled()) {std::copy(input.begin(),input.end(),output.begin());output_+=input.size();return input.size();}
        std::size_t count=0;
        for(const auto sample:input) {
            if(!std::isfinite(sample))throw Error("helper filter input contains a nonfinite sample");
            history_[next_]=sample;const auto newest=next_;
            next_=(next_+1)%history_.size();
            if(seen_<history_.size())++seen_;
            if(seen_<history_.size())continue;
            const auto at=[&](std::size_t age){return history_[(newest+history_.size()-age)%history_.size()];};
            const auto middle=delay();double value=taps_[middle]*at(middle);
            for(std::size_t i=0;i<middle;++i)
                value+=taps_[i]*(static_cast<double>(at(i))+at(taps_.size()-1-i));
            output[count++]=static_cast<float>(value);++output_;
        }
        return count;
    }
};
}
