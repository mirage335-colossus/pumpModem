#pragma once
#include "datapump/types.hpp"
#include "datapump/speculation.h"
#include <algorithm>
#include <array>
#include <span>
#include <vector>

// Shortened systematic RS over GF(2^16), primitive polynomial x^16+x^12+x^3+x+1.
// Two source bytes form one big-endian field element. Roots are alpha^0 onward.
// The fixed local profile selects dimensions; no received size is interpreted.
namespace datapump::fast::outer_rs {
namespace detail {
struct Field {
    std::array<std::uint16_t,131070> exp{};
    std::array<std::uint16_t,65536> log{};
    Field() {
        unsigned value=1;
        for(unsigned i=0;i<65535;++i) {
            exp[i]=static_cast<std::uint16_t>(value);log[value]=static_cast<std::uint16_t>(i);
            value<<=1;if(value&65536)value^=0x1100b;
        }
        for(std::size_t i=65535;i<exp.size();++i)exp[i]=exp[i-65535];
    }
    std::uint16_t mul(std::uint16_t a,std::uint16_t b)const {return a&&b?exp[unsigned(log[a])+log[b]]:0;}
    std::uint16_t div(std::uint16_t a,std::uint16_t b)const {
        if(!b)throw Error("Capacity RS division by zero");
        return a?exp[unsigned(log[a])+65535-log[b]]:0;
    }
};
inline const Field& field(){static const Field value;return value;}
using Polynomial=std::array<std::uint16_t,256>;
inline std::vector<std::uint16_t> unpack(std::span<const std::uint8_t> bytes) {
    if(bytes.size()%2)throw Error("Capacity RS requires even byte geometry");
    std::vector<std::uint16_t> out(bytes.size()/2);
    for(std::size_t i=0;i<out.size();++i)out[i]=static_cast<std::uint16_t>((unsigned(bytes[2*i])<<8)|bytes[2*i+1]);
    return out;
}
inline Bytes pack(std::span<const std::uint16_t> words) {
    Bytes out(words.size()*2);
    for(std::size_t i=0;i<words.size();++i){out[2*i]=static_cast<std::uint8_t>(words[i]>>8);out[2*i+1]=static_cast<std::uint8_t>(words[i]);}
    return out;
}
inline Polynomial syndromes(std::span<const std::uint16_t> word,std::size_t parity) {
    Polynomial out{};const auto& gf=field();
    for(std::size_t i=0;i<parity;++i)for(auto value:word)out[i]=gf.mul(out[i],gf.exp[i])^value;
    return out;
}
inline std::size_t locator(const Polynomial& syndrome,std::size_t count,Polynomial& output) {
    const auto& gf=field();Polynomial previous{};output[0]=previous[0]=1;
    std::size_t degree=0,shift=1;std::uint16_t previous_discrepancy=1;
    for(std::size_t step=0;step<count;++step) {
        auto discrepancy=syndrome[step];
        for(std::size_t i=1;i<=degree;++i)
            discrepancy^=gf.mul(output[datapump_index_nospec(i,output.size())],
                syndrome[datapump_index_nospec(step-i,syndrome.size())]);
        if(!discrepancy){++shift;continue;}
        const auto saved=output;const auto factor=gf.div(discrepancy,previous_discrepancy);
        for(std::size_t i=0;i+shift<output.size();++i)
            output[datapump_index_nospec(i+shift,output.size())]^=gf.mul(factor,previous[i]);
        if(2*degree<=step){degree=step+1-degree;previous=saved;previous_discrepancy=discrepancy;shift=1;}
        else ++shift;
    }
    if(2*degree>count)throw Error("Uncorrectable capacity Reed-Solomon cycle");
    return datapump_index_nospec(degree,output.size());
}
}
inline Bytes encode(std::span<const std::uint8_t> bytes,std::size_t parity) {
    // Validate the caller's dimensions before allocating even the unpacked
    // scratch. Subtraction avoids overflow in a source-plus-parity sum.
    if(bytes.empty()||bytes.size()%2||!parity||parity>=256||bytes.size()/2>65535-parity)
        throw Error("Invalid capacity RS dimensions");
    auto data=detail::unpack(bytes);const auto& gf=detail::field();
    detail::Polynomial generator{};generator[0]=1;std::size_t width=1;
    for(std::size_t root=0;root<parity;++root) {
        detail::Polynomial next{};
        for(std::size_t i=0;i<width;++i){next[i]^=generator[i];next[i+1]^=gf.mul(generator[i],gf.exp[root]);}
        generator=next;++width;
    }
    auto result=data;result.resize(data.size()+parity);
    for(std::size_t i=0;i<data.size();++i) {
        const auto coefficient=result[i];
        for(std::size_t j=1;j<width;++j)result[i+j]^=gf.mul(generator[j],coefficient);
    }
    std::copy(data.begin(),data.end(),result.begin());return detail::pack(result);
}
// Returns the number of changed 16-bit symbols. Erasure positions are symbols,
// not bytes. A successful RS result still requires the cycle SHA-256/HMAC.
inline std::size_t correct(Bytes& bytes,std::size_t parity,std::span<const std::size_t> erasures={}) {
    if(bytes.size()%2||!parity||parity>=256||parity>=bytes.size()/2||bytes.size()/2>65535)
        throw Error("Invalid capacity RS dimensions");
    if(erasures.size()>parity)throw Error("Too many capacity Reed-Solomon erasures");
    datapump_speculation_barrier();
    auto word=detail::unpack(bytes);const auto& gf=detail::field();
    std::vector<bool> erased(word.size());std::array<std::size_t,256> positions{};detail::Polynomial locations{};std::size_t count=0;
    for(auto position:erasures) {
        const auto bounded=datapump_index_nospec(position,word.size());
        if(position>=word.size()||erased[bounded])throw Error("Invalid capacity RS erasure position");
        const auto slot=datapump_index_nospec(count,positions.size());
        erased[bounded]=true;positions[slot]=bounded;locations[slot]=gf.exp[word.size()-1-bounded];++count;
    }
    const auto syndrome=detail::syndromes(word,parity);
    if(std::all_of(syndrome.begin(),syndrome.begin()+static_cast<std::ptrdiff_t>(parity),[](auto x){return x==0;}))return 0;
    auto reduced=syndrome;auto remaining=parity;
    for(std::size_t i=0;i<erasures.size();++i) {
        for(std::size_t j=0;j+1<remaining;++j)reduced[j]=reduced[j+1]^gf.mul(locations[i],reduced[j]);
        --remaining;
    }
    detail::Polynomial locator{};const auto errors=detail::locator(reduced,remaining,locator);
    for(std::size_t position=0;position<word.size();++position) {
        const auto inverse=gf.exp[65535-(word.size()-1-position)];auto value=locator[errors];
        for(std::size_t i=errors;i>0;--i)value=gf.mul(value,inverse)^locator[i-1];
        if(!value) {
            if(erased[position]||count>=parity)throw Error("Uncorrectable capacity Reed-Solomon cycle");
            const auto slot=datapump_index_nospec(count,positions.size());
            positions[slot]=position;locations[slot]=gf.exp[word.size()-1-position];++count;
        }
    }
    if(count!=erasures.size()+errors||!count||count>parity)throw Error("Uncorrectable capacity Reed-Solomon cycle");
    // Decoded root counts size the matrix; validate before allocating it. The
    // full-domain GF tables above need no fences in their arithmetic hot path.
    datapump_speculation_barrier();
    // Scratch is bounded by the locally chosen parity (at most 255 symbols).
    std::vector<std::vector<std::uint16_t>> matrix(count,std::vector<std::uint16_t>(count+1));
    for(std::size_t column=0;column<count;++column) {
        std::uint16_t power=1;
        for(std::size_t row=0;row<count;++row){matrix[row][column]=power;power=gf.mul(power,locations[column]);}
    }
    for(std::size_t row=0;row<count;++row)matrix[row][count]=syndrome[row];
    for(std::size_t column=0;column<count;++column) {
        auto pivot=column;while(pivot<count&&!matrix[datapump_index_nospec(pivot,count)][column])++pivot;
        if(pivot==count)throw Error("Uncorrectable capacity Reed-Solomon cycle");
        std::swap(matrix[column],matrix[datapump_index_nospec(pivot,count)]);const auto scale=matrix[column][column];
        for(std::size_t i=column;i<=count;++i)matrix[column][i]=gf.div(matrix[column][i],scale);
        for(std::size_t row=0;row<count;++row)if(row!=column) {
            const auto factor=matrix[row][column];
            for(std::size_t i=column;i<=count;++i)matrix[row][i]^=gf.mul(factor,matrix[column][i]);
        }
    }
    std::size_t changed=0;
    for(std::size_t i=0;i<count;++i)if(matrix[i][count]) {
        const auto position=datapump_index_nospec(positions[i],word.size());
        word[position]^=matrix[i][count];++changed;
    }
    const auto checked=detail::syndromes(word,parity);
    if(std::any_of(checked.begin(),checked.begin()+static_cast<std::ptrdiff_t>(parity),[](auto x){return x!=0;}))throw Error("Uncorrectable capacity Reed-Solomon cycle");
    bytes=detail::pack(word);return changed;
}
}
