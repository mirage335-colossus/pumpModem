#include "datapump/fast/codec.hpp"
#include "fast/ssb_local_fir.hpp"
#include "fast_test_key.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

using namespace datapump;
using namespace datapump::fast;

namespace {
enum class Evidence { exact, correctable, erased };

void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

Bytes transmit(const Profile& profile,const std::optional<Crypto>& key,const Bytes& source) {
    auto encoder=fast::testing::deterministic_encoder(profile,key,byte_source(source),0x7129,SourceEncoding::raw);
    Bytes wire;
    std::array<std::uint8_t,physical_interval_bits> interval{};
    while(encoder.next_interval(interval))wire.insert(wire.end(),interval.begin(),interval.end());
    return wire;
}

std::optional<ValidatedCapacityCycle> deliver_cycle(StreamDecoder& decoder,
                                                    std::span<const std::uint8_t> wire,
                                                    Evidence evidence=Evidence::exact) {
    std::optional<ValidatedCapacityCycle> result;
    for(std::size_t offset=0;offset<wire.size();offset+=physical_interval_bits) {
        std::array<float,physical_interval_bits> soft{};
        if(evidence!=Evidence::erased)
            std::ranges::transform(wire.subspan(offset,soft.size()),soft.begin(),[](auto bit) {
                return bit?12.F:-12.F;
            });
        if(evidence==Evidence::correctable&&offset==0) {
            soft[0]=-soft[0];
            soft[7]=0.F;
        }
        result=decoder.push_interval(soft,true);
        if(offset+soft.size()<wire.size())check(!result,"Partial cycle exposed tracking evidence");
    }
    return result;
}

void check_feedback(const Profile& profile,const std::optional<Crypto>& key) {
    Bytes source(capacity_source_bytes_per_cycle(profile,key.has_value())+13);
    for(std::size_t i=0;i<source.size();++i)source[i]=static_cast<std::uint8_t>(53*i);
    const auto wire=transmit(profile,key,source);
    const auto cycle_bits=cycle_intervals(profile)*physical_interval_bits;
    const auto protected_bits=capacity_coded_bits(profile);
    check(wire.size()==3*cycle_bits && protected_bits<cycle_bits,"Fixture must exercise alignment fill and two source cycles");

    StreamDecoder decoder(profile,key);
    const auto validation=key?CapacityCycleValidation::authenticated:CapacityCycleValidation::checksummed;
    for(std::uint64_t ordinal=0;ordinal<3;++ordinal) {
        const auto cycle=std::span(wire).subspan(ordinal*cycle_bits,cycle_bits);
        const auto event=deliver_cycle(decoder,cycle,ordinal==1?Evidence::correctable:Evidence::exact);
        check(event && event->wire_ordinal==ordinal && event->validation==validation,"Missing or mislabeled validated cycle");
        check(std::ranges::equal(event->protected_bits,cycle.first(protected_bits)),"Tracking bits differ from protected TX prefix");
        check(fast::detail::ssb_local_fir_cycle_feedback(*event,ordinal,protected_bits),
            "Local FIR rejected a typed validated cycle at its actual wire ordinal");
        check(!fast::detail::ssb_local_fir_cycle_feedback(*event,ordinal+1,protected_bits),
            "Local FIR feedback accepted a mismatched wire ordinal");
        check(!fast::detail::ssb_local_fir_cycle_feedback(*event,ordinal,protected_bits+1),
            "Local FIR feedback accepted an unexpected protected codeword length");
    }
    check(!decoder.result()&&!decoder.snapshot().physical_end,"Tracking exposed a file before physical completion");
    decoder.finish(true);
    check(decoder.result()&&std::ranges::equal(decoder.result()->bytes(),source),"Tracking changed complete source decoding");
    check(decoder.snapshot().authenticated==key.has_value(),"Tracking changed source authentication status");

    StreamDecoder interrupted(profile,key);
    check(deliver_cycle(interrupted,std::span(wire).first(cycle_bits)).has_value(),"Bootstrap validation failed");
    check(!deliver_cycle(interrupted,std::span(wire).subspan(cycle_bits,cycle_bits),Evidence::erased),
          "Erased cycle exposed tracking evidence");
    const auto last=std::span(wire).last(cycle_bits);
    const auto after_gap=deliver_cycle(interrupted,last);
    check(after_gap && after_gap->wire_ordinal==2 &&
          std::ranges::equal(after_gap->protected_bits,last.first(protected_bits)),
          "Rejected source cycle changed later tracking ordinals");
    interrupted.finish(true);
    check(!interrupted.result()&&interrupted.snapshot().failed,"Later tracking evidence concealed a source hole");

    const std::optional<Crypto> wrong_key=key?std::optional(fast_test::sequence_key(71)):std::nullopt;
    const auto wrong_source=key?SourceEncoding::raw:SourceEncoding::xz;
    StreamDecoder wrong(profile,wrong_key,256ULL*1024*1024,wrong_source);
    check(!deliver_cycle(wrong,std::span(wire).first(cycle_bits))&&wrong.snapshot().decoding_stopped,
          "Rejected bootstrap exposed tracking evidence or continued decoding");
}
}

int main() {
    try {
        auto profile=capacity_profile(Channel::ssb);
        profile.constellation=4;
        profile.code_rate=CodeRate::half;
        profile.interleave_depth=1;
        check_feedback(profile,std::nullopt);
        check_feedback(profile,fast_test::sequence_key());
        std::cout<<"Fast validated cycle feedback matches protected wire bits for public and encrypted streams\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
