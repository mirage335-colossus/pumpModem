#pragma once

// Private deterministic boundary observer plus one exact original push receipt.
// Observer callbacks are synchronous/noexcept; no detector state is exposed.
#include "datapump/types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <openssl/crypto.h>
#include <openssl/sha.h>

namespace datapump::modem::detail::scheduled_resume {
enum class Boundary { captured, numeric_prepared, replay_fence, before_event, event_committed, block_committed, diagnostic_materialized, detached_prepared };
struct Binding {
    void (*observe)(Boundary,std::uint64_t,void*)noexcept=nullptr;
    void* context=nullptr;
};
inline thread_local Binding binding;
inline void checkpoint(Boundary boundary,std::uint64_t coordinate)noexcept {
    if(binding.observe)binding.observe(boundary,coordinate,binding.context);
}
class Scope {
    Binding previous_;
public:
    explicit Scope(Binding next)noexcept:previous_(binding){binding=next;}
    ~Scope(){binding=previous_;}
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
class Error:public ::datapump::Error {public:using ::datapump::Error::Error;};
class PushReceipt {
    bool active_=false,fingerprint_=false;
    std::uint64_t first_=0,end_=0;
    std::array<unsigned char,SHA256_DIGEST_LENGTH> expected_{};
    SHA256_CTX prefix_{};
    static std::size_t bytes(std::span<const float> input) {
        if(input.size()>std::numeric_limits<std::size_t>::max()/sizeof(float))throw Error("original PCM extent overflow");
        return input.size()*sizeof(float);
    }
    static auto digest(std::span<const float> input) {
        std::array<unsigned char,SHA256_DIGEST_LENGTH> result{};SHA256_CTX context{};
        struct Wipe {SHA256_CTX& context;~Wipe(){OPENSSL_cleanse(&context,sizeof(context));}} wipe{context};
        if(SHA256_Init(&context)!=1 || SHA256_Update(&context,input.data(),bytes(input))!=1 ||
            SHA256_Final(result.data(),&context)!=1)throw Error("original PCM fingerprint failed");
        return result;
    }
public:
    ~PushReceipt(){OPENSSL_cleanse(&prefix_,sizeof(prefix_));OPENSSL_cleanse(expected_.data(),expected_.size());}
    bool active()const{return active_;}
    bool fingerprinted()const{return fingerprint_;}
    std::uint64_t first()const{return first_;}
    std::uint64_t end()const{return end_;}
    void begin(std::uint64_t first,std::size_t count) {
        if(active_ || !count || count>std::numeric_limits<std::uint64_t>::max()-first)
            throw Error("invalid original push receipt");
        first_=first;end_=first+count;active_=true;fingerprint_=false;
    }
    // Called only after cancellation while the original caller span still lives.
    // Ordinary successful pushes add no hash scan or digest allocation.
    void cancelled_original(std::span<const float> original,std::uint64_t captured_end) {
        if(!active_ || fingerprint_ || original.size()!=end_-first_ || captured_end<first_ || captured_end>end_)
            throw Error("cancelled original PCM receipt is inconsistent");
        expected_=digest(original);
        if(SHA256_Init(&prefix_)!=1 || SHA256_Update(&prefix_,original.data(),bytes(original.first(static_cast<std::size_t>(captured_end-first_))))!=1)
            throw Error("cancelled PCM prefix fingerprint failed");
        fingerprint_=true;
    }
    // Retry may supply the SAME complete original span or its EXACT remaining
    // suffix. The receipt retains the original caller endpoint in both cases.
    std::size_t retry_offset(std::span<const float> input,std::uint64_t captured_end)const {
        if(!active_ || !fingerprint_ || captured_end<first_ || captured_end>end_)
            throw Error("original PCM retry has no cancelled receipt");
        if(input.size()==end_-first_) {
            if(digest(input)!=expected_)throw Error("replacement PCM differs from the cancelled original push");
            return static_cast<std::size_t>(captured_end-first_);
        }
        if(input.size()!=end_-captured_end)throw Error("original PCM retry must preserve the remaining push endpoint");
        auto copy=prefix_;std::array<unsigned char,SHA256_DIGEST_LENGTH> result{};
        if(SHA256_Update(&copy,input.data(),bytes(input))!=1 || SHA256_Final(result.data(),&copy)!=1)
            throw Error("original PCM retry fingerprint failed");
        OPENSSL_cleanse(&copy,sizeof(copy));
        if(result!=expected_)throw Error("replacement PCM suffix differs from the cancelled original push");
        return 0;
    }
    void appended(std::span<const float> input) {
        if(fingerprint_ && SHA256_Update(&prefix_,input.data(),bytes(input))!=1)
            throw Error("resumed PCM prefix fingerprint failed");
    }
    void clear()noexcept {
        active_=fingerprint_=false;first_=end_=0;
        OPENSSL_cleanse(&prefix_,sizeof(prefix_));OPENSSL_cleanse(expected_.data(),expected_.size());
    }
};
} // namespace datapump::modem::detail::scheduled_resume
