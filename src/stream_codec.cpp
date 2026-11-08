#include "datapump/stream_codec.hpp"
#include "datapump/speculation.h"
#include <algorithm>

namespace datapump {
namespace {
struct Field {
    std::array<std::uint8_t,512> exp{};
    std::array<std::uint8_t,256> log{};
    Field() {
        unsigned value=1;
        for(unsigned i=0;i<255;++i) {
            exp[i]=static_cast<std::uint8_t>(value);log[value]=static_cast<std::uint8_t>(i);
            value<<=1;if(value&256)value^=0x11d;
        }
        for(std::size_t i=255;i<exp.size();++i)exp[i]=exp[i-255];
    }
    std::uint8_t mul(std::uint8_t a,std::uint8_t b) const { return a&&b?exp[static_cast<unsigned>(log[a])+log[b]]:0; }
    std::uint8_t div(std::uint8_t a,std::uint8_t b) const {
        if(!b)throw Error("Reed-Solomon division by zero");
        return a?exp[static_cast<unsigned>(log[a])+255-log[b]]:0;
    }
};
const Field gf;
using Polynomial=std::array<std::uint8_t,256>;
Polynomial syndromes(const Bytes& word,std::size_t parity) {
    Polynomial result{};
    for(std::size_t i=0;i<parity;++i)
        for(auto byte:word)result[i]=gf.mul(result[i],gf.exp[i])^byte;
    return result;
}
std::size_t error_locator(const Polynomial& syndrome,std::size_t count,Polynomial& locator) {
    Polynomial previous{};locator[0]=previous[0]=1;
    std::size_t degree=0,shift=1;std::uint8_t previous_discrepancy=1;
    for(std::size_t step=0;step<count;++step) {
        auto discrepancy=syndrome[step];
        for(std::size_t i=1;i<=degree;++i)
            discrepancy^=gf.mul(locator[datapump_index_nospec(i,locator.size())],
                syndrome[datapump_index_nospec(step-i,syndrome.size())]);
        if(!discrepancy){++shift;continue;}
        const auto saved=locator;const auto factor=gf.div(discrepancy,previous_discrepancy);
        for(std::size_t i=0;i+shift<locator.size();++i)
            locator[datapump_index_nospec(i+shift,locator.size())]^=gf.mul(factor,previous[i]);
        if(2*degree<=step) { degree=step+1-degree;previous=saved;previous_discrepancy=discrepancy;shift=1; }
        else ++shift;
    }
    if(2*degree>count)throw Error("Uncorrectable Reed-Solomon interval");
    return datapump_index_nospec(degree,locator.size());
}
} // namespace
namespace fec {
Bytes rs_encode(const Bytes& data,std::size_t parity) {
    if(data.empty() || !parity || parity>=255 || data.size()>255-parity)
        throw Error("Invalid Reed-Solomon block dimensions");
    Polynomial generator{};generator[0]=1;std::size_t width=1;
    for(std::size_t root=0;root<parity;++root) {
        Polynomial next{};
        for(std::size_t i=0;i<width;++i) { next[i]^=generator[i];next[i+1]^=gf.mul(generator[i],gf.exp[root]); }
        generator=next;++width;
    }
    Bytes result(data.size()+parity);std::copy(data.begin(),data.end(),result.begin());
    for(std::size_t i=0;i<data.size();++i) {
        const auto coefficient=result[i];
        for(std::size_t j=1;j<width;++j)result[i+j]^=gf.mul(generator[j],coefficient);
    }
    std::copy(data.begin(),data.end(),result.begin());return result;
}
std::size_t rs_correct(Bytes& word,std::size_t parity,std::span<const std::size_t> erasures) {
    if(!parity || parity>=word.size() || word.size()>255)throw Error("Invalid Reed-Solomon block dimensions");
    if(erasures.size()>parity)throw Error("Too many Reed-Solomon erasures");
    datapump_speculation_barrier();
    std::array<bool,255> erased{};
    std::array<std::size_t,255> positions{};
    Polynomial locations{};
    std::size_t count=0;
    for(auto position:erasures) {
        if(position>=word.size())throw Error("Invalid Reed-Solomon erasure position");
        position=datapump_index_nospec(position,word.size());
        if(erased[position])throw Error("Invalid Reed-Solomon erasure position");
        const auto slot=datapump_index_nospec(count,positions.size());
        erased[position]=true;positions[slot]=position;locations[slot]=gf.exp[word.size()-1-position];++count;
    }
    const auto syndrome=syndromes(word,parity);
    if(std::all_of(syndrome.begin(),syndrome.begin()+static_cast<std::ptrdiff_t>(parity),[](auto x){return x==0;}))return 0;
    // Eliminate each known erasure from the syndrome recurrence. The remaining
    // sequence contains only unknown-location errors, with p-v equations.
    auto reduced=syndrome;auto remaining=parity;
    for(std::size_t i=0;i<erasures.size();++i) {
        for(std::size_t j=0;j+1<remaining;++j)reduced[j]=reduced[j+1]^gf.mul(locations[i],reduced[j]);
        --remaining;
    }
    Polynomial locator{};const auto errors=error_locator(reduced,remaining,locator);
    for(std::size_t position=0;position<word.size();++position) {
        const auto inverse=gf.exp[255-(word.size()-1-position)];
        auto value=locator[errors];
        for(std::size_t i=errors;i>0;--i)value=gf.mul(value,inverse)^locator[i-1];
        if(!value) {
            if(erased[position] || count>=parity)throw Error("Uncorrectable Reed-Solomon interval");
            const auto slot=datapump_index_nospec(count,positions.size());
            positions[slot]=position;locations[slot]=gf.exp[word.size()-1-position];++count;
        }
    }
    if(count!=erasures.size()+errors || !count)throw Error("Uncorrectable Reed-Solomon interval");
    datapump_speculation_barrier();
    // Fixed upper-bound scratch; solve magnitudes against the original
    // syndrome. No allocation dimension comes from received bytes.
    std::array<std::array<std::uint8_t,256>,255> matrix{};
    for(std::size_t column=0;column<count;++column) {
        std::uint8_t power=1;
        for(std::size_t row=0;row<count;++row) { matrix[row][column]=power;power=gf.mul(power,locations[column]); }
    }
    for(std::size_t row=0;row<count;++row)matrix[row][count]=syndrome[row];
    for(std::size_t column=0;column<count;++column) {
        auto pivot=column;
        while(pivot<count&&!matrix[datapump_index_nospec(pivot,matrix.size())][column])++pivot;
        if(pivot==count)throw Error("Uncorrectable Reed-Solomon interval");
        pivot=datapump_index_nospec(pivot,count);
        std::swap(matrix[column],matrix[pivot]);const auto scale=matrix[column][column];
        for(std::size_t i=column;i<=count;++i)matrix[column][i]=gf.div(matrix[column][i],scale);
        for(std::size_t row=0;row<count;++row)if(row!=column) {
            const auto factor=matrix[row][column];
            for(std::size_t i=column;i<=count;++i)matrix[row][i]^=gf.mul(factor,matrix[column][i]);
        }
    }
    auto repaired=word;std::size_t changed=0;
    for(std::size_t i=0;i<count;++i)if(matrix[i][count]) {
        repaired[datapump_index_nospec(positions[i],repaired.size())]^=matrix[i][count];++changed;
    }
    const auto checked=syndromes(repaired,parity);
    if(std::any_of(checked.begin(),checked.begin()+static_cast<std::ptrdiff_t>(parity),[](auto x){return x!=0;}))
        throw Error("Uncorrectable Reed-Solomon interval");
    datapump_speculation_barrier();
    word=std::move(repaired);return changed;
}
}

} // namespace datapump
