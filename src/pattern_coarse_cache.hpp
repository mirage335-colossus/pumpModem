#pragma once
// Optional exact INNER Gaussian coefficient copies. Final outer rotation,
// legacy bit mask, physical gate and every receiver decision remain original.
#include "datapump/pattern_code.hpp"
#include <openssl/crypto.h>
#include <array>
#include <atomic>
#include <complex>
#include <cfenv>
#include <cfloat>
#include <cmath>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <stop_token>

namespace datapump::modem::detail {
struct InnerCoefficientKey {
    std::uint64_t key_namespace=0,pattern_epoch=0,layer_epoch=0,position=0;
    std::uint32_t factor=0;
    std::uint8_t bit=0,algorithm_revision=1,outer_version=2,layer_present=0;
    StreamPurpose pattern_purpose=StreamPurpose::Scrambler,layer_purpose=StreamPurpose::Dsss;
    StreamDomain pattern_domain=StreamDomain::PatternZeroV2,layer_domain=StreamDomain::PatternZeroV2;
    std::array<std::uint8_t,32> pattern_root{},dsss_root{};
    InnerCoefficientKey()=default;
    InnerCoefficientKey(const InnerCoefficientKey&)=default;
    InnerCoefficientKey& operator=(const InnerCoefficientKey&)=default;
    ~InnerCoefficientKey(){clear();}
    void clear()noexcept {
        OPENSSL_cleanse(pattern_root.data(),pattern_root.size());
        OPENSSL_cleanse(dsss_root.data(),dsss_root.size());
    }
    bool eligible()const noexcept {
        const auto domain=bit?StreamDomain::PatternOneV2:StreamDomain::PatternZeroV2;
        return key_namespace && bit<=1 && algorithm_revision==1 && outer_version==2 &&
            (factor==10 || factor==100 || factor==1000) && layer_present<=1 &&
            pattern_purpose==StreamPurpose::Scrambler && pattern_domain==domain &&
            (!layer_present || (layer_purpose==StreamPurpose::Dsss && layer_domain==domain)) &&
            position<=(std::numeric_limits<std::uint64_t>::max()-7)/8;
    }
    bool operator==(const InnerCoefficientKey& b)const noexcept {
        return key_namespace==b.key_namespace && pattern_epoch==b.pattern_epoch &&
            layer_epoch==b.layer_epoch && position==b.position && factor==b.factor &&
            bit==b.bit && algorithm_revision==b.algorithm_revision && outer_version==b.outer_version &&
            layer_present==b.layer_present && pattern_purpose==b.pattern_purpose && layer_purpose==b.layer_purpose &&
            pattern_domain==b.pattern_domain && layer_domain==b.layer_domain &&
            CRYPTO_memcmp(pattern_root.data(),b.pattern_root.data(),pattern_root.size())==0 &&
            CRYPTO_memcmp(dsss_root.data(),b.dsss_root.data(),dsss_root.size())==0;
    }
};
inline bool inner_coefficient_cache_requested()noexcept {
    const auto* value=std::getenv("DATAPUMP_RX_INNER_COEFFICIENT_CACHE");
    return value && value[0]=='1' && value[1]=='\0';
}

inline bool inner_coefficient_arithmetic_supported()noexcept {
#if !defined(DATAPUMP_ORDERED_PULSE_CONTRACT_OFF) || defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__>0)
    return false;
#endif
    if(!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::digits!=53 ||
        FLT_EVAL_METHOD!=0 || std::fegetround()!=FE_TONEAREST)return false;
#if defined(__SSE2__)
    return (_mm_getcsr()&((1U<<15)|(1U<<6)|(3U<<13)))==0;
#else
    return false;
#endif
}

class InnerCoefficientCache {
public:
    static constexpr std::size_t ways=4,sets=1024,capacity=ways*sets;
    struct Statistics {
        std::uint64_t requests=0,hits=0,misses=0,builder_attempts=0,builder_completed=0,
            publications=0,busy=0,evictions=0,bypassed=0;
    };
    // Hash is only a bounded set selector. Full scalar/domain/root identity
    // decides every hit; no hash fingerprint substitutes for private equality.
    static std::size_t bucket(const InnerCoefficientKey& key)noexcept {
        const auto mix=[](std::uint64_t n) {
            n^=n>>30;n*=0xbf58476d1ce4e5b9ULL;n^=n>>27;n*=0x94d049bb133111ebULL;return n^(n>>31);
        };
        return mix(key.position^mix(key.key_namespace)^mix(key.pattern_epoch)^mix(key.layer_epoch)^ 
            (std::uint64_t{key.bit}<<56)^(std::uint64_t{key.factor}<<16)^std::uint64_t(key.pattern_domain))&(sets-1);
    }
private:
    struct Row {
        InnerCoefficientKey key;
        std::complex<double> value{};
        std::uint64_t stamp=0;
        bool valid=false;
        void clear()noexcept {
            valid=false;key.clear();OPENSSL_cleanse(&value,sizeof(value));stamp=0;
        }
        ~Row(){clear();}
    };
    struct Counters {
        std::atomic<std::uint64_t> requests{},hits{},misses{},builder_attempts{},builder_completed{},
            publications{},busy{},evictions{},bypassed{};
    } counters_;
    std::array<std::array<Row,ways>,sets> rows_{};
    mutable std::mutex mutex_;
    std::uint64_t stamp_=0;
    bool accepting_=true;
    static void add(std::atomic<std::uint64_t>& value)noexcept {
        auto n=value.load(std::memory_order_relaxed);
        while(n!=std::numeric_limits<std::uint64_t>::max() &&
            !value.compare_exchange_weak(n,n+1,std::memory_order_relaxed)){}
    }
    static void cancelled(std::stop_token stop) {
        if(stop.stop_requested())throw Error("inner coefficient construction cancelled");
    }
    std::uint64_t stamp()noexcept {
        if(stamp_==std::numeric_limits<std::uint64_t>::max()) {
            for(auto& set:rows_)for(auto& row:set)row.stamp=0;stamp_=0;
        }
        return ++stamp_;
    }
public:
    InnerCoefficientCache()=default;
    InnerCoefficientCache(const InnerCoefficientCache&)=delete;
    InnerCoefficientCache& operator=(const InnerCoefficientCache&)=delete;
    // Caller must join all users before object destruction. Retirement alone
    // does not destroy this object while an original private builder is active.
    ~InnerCoefficientCache(){std::lock_guard lock(mutex_);for(auto& set:rows_)for(auto& row:set)row.clear();}
    template<class Builder>
    std::complex<double> copy_or_build(const InnerCoefficientKey& key,Builder&& original,std::stop_token stop={}) {
        add(counters_.requests);cancelled(stop);bool may_publish=false;
        if(key.eligible() && inner_coefficient_arithmetic_supported()) {
            std::unique_lock lock(mutex_,std::try_to_lock);
            if(!lock)add(counters_.busy);
            else if(accepting_) {
                may_publish=true;
                for(auto& row:rows_[bucket(key)])if(row.valid && row.key==key) {
                    const auto value=row.value;row.stamp=stamp();cancelled(stop);add(counters_.hits);return value;
                }
            } else add(counters_.bypassed);
        } else add(counters_.bypassed);
        add(counters_.misses);add(counters_.builder_attempts);
        auto value=original();add(counters_.builder_completed);
        // Clean private temporary even when late cancellation or publication
        // lock failure exits. The returned value is an independent exact copy.
        struct CleanValue {std::complex<double>& value;~CleanValue(){OPENSSL_cleanse(&value,sizeof(value));}} clean{value};
        cancelled(stop);if(!may_publish || !inner_coefficient_arithmetic_supported() ||
            !std::isfinite(value.real()) || !std::isfinite(value.imag()))return {value.real(),value.imag()};
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock){add(counters_.busy);return {value.real(),value.imag()};}
        if(!accepting_)return {value.real(),value.imag()};
        cancelled(stop);
        auto& set=rows_[bucket(key)];Row* selected=nullptr;
        for(auto& row:set) {
            if(row.valid && row.key==key)return {value.real(),value.imag()}; // another original build already published
            if(!row.valid && !selected)selected=&row;
        }
        if(!selected) {
            selected=&set[0];for(auto& row:set)if(row.stamp<selected->stamp)selected=&row;
            add(counters_.evictions);
        }
        selected->clear();selected->key=key;selected->value=value;
        if(stop.stop_requested()){selected->clear();cancelled(stop);}
        selected->stamp=stamp();selected->valid=true;add(counters_.publications);return {value.real(),value.imag()};
    }
    // No row pointer escapes. A builder already outside the lock finishes its
    // original work, sees retired state and cannot refill this cache.
    bool try_reclaim() {
        std::unique_lock lock(mutex_,std::try_to_lock);if(!lock)return false;
        accepting_=false;for(auto& set:rows_)for(auto& row:set)row.clear();return true;
    }
    Statistics statistics()const noexcept {
        const auto get=[](const auto& value){return value.load(std::memory_order_relaxed);};
        return {get(counters_.requests),get(counters_.hits),get(counters_.misses),get(counters_.builder_attempts),
            get(counters_.builder_completed),get(counters_.publications),get(counters_.busy),get(counters_.evictions),get(counters_.bypassed)};
    }
};
} // namespace datapump::modem::detail
