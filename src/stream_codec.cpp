#include "datapump/stream_codec.hpp"
#include "datapump/compression.hpp"
#include "datapump/speculation.h"
#include <algorithm>
#include <bit>
#include <limits>

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
std::size_t checked_product(std::size_t a,std::size_t b) {
    if(a && b>std::numeric_limits<std::size_t>::max()/a)throw Error("Source storage size overflow");
    return a*b;
}
void check_area(std::size_t area) {
    if(!area || area>stream_interval_bytes)throw Error("Invalid fixed source data area");
}
unsigned bit(std::span<const std::uint8_t> bytes,std::size_t position) {
    return (bytes[position/8]>>(7-position%8))&1U;
}
void set_bit(std::span<std::uint8_t> bytes,std::size_t position,unsigned value) {
    bytes[position/8]|=static_cast<std::uint8_t>(value<<(7-position%8));
}
}
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

std::size_t interval_parity_bytes(FecMode fec) {
    switch(fec) { case FecMode::off:return 0;case FecMode::rs20:return 22;case FecMode::rs60:return 48; }
    throw Error("Unknown fixed Reed-Solomon profile");
}
std::size_t interval_data_bytes(FecMode fec,bool keyed) {
    return stream_interval_bytes-interval_parity_bytes(fec)-(keyed?stream_mac_bytes:0);
}
Bytes encode_interval(std::span<const std::uint8_t> data,const IntervalOptions& options) {
    const bool keyed=static_cast<bool>(options.authenticator)||static_cast<bool>(options.verifier);
    if(keyed&&!options.authenticator)throw Error("Keyed interval requires an authenticator");
    if(data.size()!=interval_data_bytes(options.fec,keyed))throw Error("Incorrect fixed interval data width");
    Bytes body(data.begin(),data.end());
    if(keyed) {
        const auto tag=options.authenticator(body);
        if(tag.size()!=stream_mac_bytes)throw Error("Interval HMAC must contain 32 bytes");
        body.insert(body.end(),tag.begin(),tag.end());
    }
    const auto parity=interval_parity_bytes(options.fec);
    return parity?fec::rs_encode(body,parity):body;
}
DecodedInterval decode_interval(std::span<const std::uint8_t> coded,const IntervalOptions& options,
                                std::span<const std::size_t> erasures,
                                std::span<const std::uint8_t> erasure_bits) {
    if(coded.size()!=stream_interval_bytes)throw Error("Incomplete fixed coding interval");
    if(!erasure_bits.empty() && erasure_bits.size()!=coded.size())throw Error("Incorrect interval erasure mask width");
    datapump_speculation_barrier();
    std::array<std::uint8_t,stream_interval_bytes> missing{};
    for(auto position:erasures) {
        if(position>=coded.size())throw Error("Invalid interval erasure position");
        position=datapump_index_nospec(position,missing.size());
        if(missing[position])throw Error("Invalid interval erasure position");
        missing[position]=0xff;
    }
    if(!erasure_bits.empty())for(std::size_t i=0;i<missing.size();++i) {
        if(static_cast<bool>(missing[i])!=static_cast<bool>(erasure_bits[i]))
            throw Error("Interval erasure masks disagree with byte positions");
        missing[i]=erasure_bits[i];
    }
    const bool keyed=static_cast<bool>(options.authenticator)||static_cast<bool>(options.verifier);
    if(keyed&&!options.verifier)throw Error("Keyed interval requires a verifier");
    const auto parity=interval_parity_bytes(options.fec),data_size=interval_data_bytes(options.fec,keyed);
    Bytes body(coded.begin(),coded.end());DecodedInterval result;result.erased_bytes=erasures.size();
    if(parity)result.corrected_bytes=fec::rs_correct(body,parity,erasures);
    else if(!erasures.empty())throw Error("Unresolved erasure in interval without FEC");
    result.data.assign(body.begin(),body.begin()+static_cast<std::ptrdiff_t>(data_size));
    if(keyed) {
        const Bytes tag(body.begin()+static_cast<std::ptrdiff_t>(data_size),body.begin()+static_cast<std::ptrdiff_t>(data_size+stream_mac_bytes));
        if(!options.verifier(result.data,tag))throw Error("Interval authentication failed");
        datapump_speculation_barrier();
        result.authenticated=true;
    }
    for(std::size_t i=0;i<coded.size();++i) {
        auto& stats=i<data_size?result.fec_stats.data:
            i<stream_interval_bytes-parity?result.fec_stats.integrity:result.fec_stats.parity;
        const auto lost=static_cast<std::uint64_t>(std::popcount(missing[i]));
        const auto changed=static_cast<std::uint8_t>(coded[i]^body[i]);
        stats.received_bits+=8-lost;stats.missing_bits+=lost;
        stats.corrected_bits+=static_cast<std::uint64_t>(std::popcount(
            static_cast<std::uint8_t>(changed&static_cast<std::uint8_t>(~missing[i]))));
        stats.corrected_bytes+=changed!=0;
        stats.erased_bytes+=missing[i]!=0;
        stats.repaired_bytes+=changed!=0 || missing[i]!=0;
    }
    const auto& data=result.fec_stats.data;
    result.pre_fec_accuracy=StreamBitAccuracy{data.received_bits,data.corrected_bits,data.missing_bits};
    return result;
}
std::size_t source_bytes_per_interval(std::size_t area,bool compressed) {
    check_area(area);return compressed?area:area*8/9;
}
Bytes encode_source(std::span<const std::uint8_t> input,std::size_t area,bool compressed,std::size_t limit) {
    check_area(area);limit=std::min(limit,Bytes{}.max_size());
    if(compressed) {
        auto output=compression::encode_lzma2(input,limit);
        const auto padding=(area-output.size()%area)%area;
        if(padding>limit-output.size())throw Error("Compressed source padding exceeds output limit");
        output.resize(output.size()+padding);return output;
    }
    const auto capacity=source_bytes_per_interval(area,false);
    if(!capacity)throw Error("Fixed data area cannot contain a source byte");
    const auto intervals=std::max<std::size_t>(1,input.size()/capacity+(input.size()%capacity!=0));
    const auto count=checked_product(intervals,area);
    if(count>limit)throw Error("Source cells exceed output limit");
    Bytes output(count);
    for(std::size_t i=0;i<input.size();++i) {
        auto block=std::span(output).subspan((i/capacity)*area,area);const auto position=(i%capacity)*9;
        set_bit(block,position,1);
        for(unsigned j=0;j<8;++j)set_bit(block,position+1+j,(input[i]>>(7-j))&1U);
    }
    return output;
}
Bytes decode_source(std::span<const std::uint8_t> areas,std::size_t area,bool compressed,std::size_t limit) {
    check_area(area);
    if(areas.empty() || areas.size()%area)throw Error("Incomplete source data areas");
    datapump_speculation_barrier();
    limit=std::min(limit,Bytes{}.max_size());
    if(compressed) {
        auto decoded=compression::decode_lzma2(areas,limit);
        const auto padding=areas.size()-decoded.consumed_bytes;
        if(padding>=area || std::any_of(areas.begin()+static_cast<std::ptrdiff_t>(decoded.consumed_bytes),areas.end(),[](auto byte){return byte!=0;}))
            throw Error("Invalid final source padding");
        return std::move(decoded.data);
    }
    Bytes output;const auto capacity=source_bytes_per_interval(area,false);
    for(std::size_t offset=0;offset<areas.size();offset+=area) {
        const auto block=areas.subspan(offset,area);bool padding=false;
        for(std::size_t cell=0;cell<capacity;++cell) {
            const auto position=cell*9;const auto present=bit(block,position);unsigned value=0;
            for(unsigned j=0;j<8;++j)value=(value<<1)|bit(block,position+1+j);
            if(!present) { padding=true;if(value)throw Error("Nonzero absent source cell"); }
            else {
                if(padding)throw Error("Noncanonical source cell occupancy");
                if(output.size()==limit)throw Error("Decoded source exceeds output limit");
                output.push_back(static_cast<std::uint8_t>(value));
            }
        }
        for(std::size_t position=capacity*9;position<area*8;++position)
            if(bit(block,position))throw Error("Nonzero fixed source padding bit");
    }
    return output;
}
}
