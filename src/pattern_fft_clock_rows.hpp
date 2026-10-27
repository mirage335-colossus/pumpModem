#pragma once
#include "datapump/modem.hpp"
#include <openssl/crypto.h>
#include <array>
#include <bit>
#include <cfenv>
#include <complex>
#include <memory>
#include <span>
#include <vector>

namespace datapump::modem::detail {
// Exact unrotated private samples for ONE serial acquisition cohort/clock.
// The immutable source owner binds all configuration and key material. This
// object cannot cross its owner's synchronous scoring scope. RF rotation,
// energies, transforms and ordered collection remain in the original scorer.
class PatternFftClockRows {
public:
    using Pair=std::array<std::complex<double>,2>;
    struct Identity {
        const void* source_owner=nullptr;
        std::uint64_t symbol=0,phase=0,source_epoch=0,source_ordinal=0;
        std::uint64_t input_sample_origin=0,bin_samples=0,clock_bits=0;
        std::uint32_t source_sample=0;
        bool operator==(const Identity&)const=default;
    };
    static constexpr std::size_t maximum_bytes=8*1024*1024;
    static std::size_t required_bytes(std::size_t bins)noexcept {
        return bins && bins<=(maximum_bytes-sizeof(PatternFftClockRows))/sizeof(Pair)?
            sizeof(PatternFftClockRows)+bins*sizeof(Pair):0;
    }
    static std::unique_ptr<PatternFftClockRows> create(std::size_t bins,std::size_t allowance) {
        const auto needed=required_bytes(bins);
        if(!needed || needed>allowance || std::fegetround()!=FE_TONEAREST)return {};
        try {
            auto rows=std::unique_ptr<PatternFftClockRows>(new PatternFftClockRows);
            rows->values_.resize(bins);
            if(rows->working_bytes()>allowance || rows->working_bytes()>maximum_bytes)return {};
            return rows;
        }catch(const std::bad_alloc&){return {};}
    }
    ~PatternFftClockRows(){cleanse();}
    std::size_t working_bytes()const noexcept {
        return sizeof(*this)+values_.capacity()*sizeof(Pair);
    }
    bool matches(const Identity& identity)const noexcept {
        return valid_ && identity.source_owner && identity==identity_ && std::fegetround()==FE_TONEAREST;
    }
    template<class Original> std::span<const Pair> get(const Identity& identity,Original&& original,
            std::stop_token stop={}) {
        if(stop.stop_requested())throw Error("pattern search cancelled");
        if(!identity.source_owner || std::fegetround()!=FE_TONEAREST)return {};
        if(matches(identity))return values_;
        valid_=false;
        // Keep each original scalar bit's traversal order. A failed/cancelled
        // fill is never a published row, and a retry regenerates the whole pair.
        for(unsigned bit=0;bit<2;++bit)for(std::size_t i=0;i<values_.size();++i) {
            if((i&4095U)==0 && stop.stop_requested())throw Error("pattern search cancelled");
            values_[i][bit]=original(i,bit);
        }
        if(stop.stop_requested())throw Error("pattern search cancelled");
        if(std::fegetround()!=FE_TONEAREST)return {};
        identity_=identity;valid_=true;return values_;
    }
private:
    PatternFftClockRows()=default;
    PatternFftClockRows(const PatternFftClockRows&)=delete;
    PatternFftClockRows& operator=(const PatternFftClockRows&)=delete;
    void cleanse()noexcept {
        OPENSSL_cleanse(values_.data(),values_.size()*sizeof(Pair));
        OPENSSL_cleanse(&identity_,sizeof(identity_));valid_=false;
    }
    std::vector<Pair> values_;
    Identity identity_;
    bool valid_=false;
};
} // namespace datapump::modem::detail
