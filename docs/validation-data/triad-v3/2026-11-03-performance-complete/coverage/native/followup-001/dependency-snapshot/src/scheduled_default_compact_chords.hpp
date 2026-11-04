#pragma once
// Frozen chosen-template tags plus a bounded synchronous payload resolver.
// No source borrow, callback, PCM, FFT plan or receiver decision is retained.
#include "linear_grid_moments.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <openssl/crypto.h>

namespace datapump::modem::detail::scheduled_default_compact_chords {
using Pair=scheduled_linear_grid::Pair;
struct Chord {Pair left{},right{};bool supported=false;};
enum class Kind:std::uint8_t {unsupported=0,zero=1,nonzero=2,signed_zero=3};
struct Classification {bool supported=false,nonzero=false;};
inline void require(bool value,const char* message){if(!value)throw Error(message);}
inline void check_stop(std::stop_token stop){require(!stop.stop_requested(),"compact chords cancelled");}
inline Kind classify(const Chord& c) {
    static_assert(sizeof(double)==sizeof(std::uint64_t)&&std::numeric_limits<double>::is_iec559&&
        std::numeric_limits<double>::digits==53,"compact chords require IEEE binary64 endpoints");
    // Original owns unsupported cells: their unused endpoint bits are not a
    // part of the chosen operator and may intentionally be nonfinite.
    if(!c.supported)return Kind::unsupported;
    bool nonzero=false,negative_zero=false;
    for(unsigned side=0;side<2;++side)for(const auto& z:(side?c.right:c.left))for(const auto x:{z.real(),z.imag()}) {
        const auto bits=std::bit_cast<std::uint64_t>(x);const auto magnitude=bits&0x7fffffffffffffffULL;
        require((magnitude&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL,"compact chords supported endpoint nonfinite");
        nonzero=nonzero||magnitude!=0;negative_zero=negative_zero||bits==0x8000000000000000ULL;
    }
    return nonzero?Kind::nonzero:(negative_zero?Kind::signed_zero:Kind::zero);
}
class Directory {
    enum class State {building,sealed,failed};
    std::size_t cells_=0,words_=0,appended_=0,bytes_=0;
    std::unique_ptr<std::uint64_t[]> tags_;
    std::stop_token stop_;
    State state_=State::building;
    void cleanse()noexcept{if(tags_)OPENSSL_cleanse(tags_.get(),words_*sizeof(std::uint64_t));}
    void fail()noexcept{state_=State::failed;cleanse();}
public:
    inline static constexpr std::size_t maximum_cells=0x40000000ULL;
    static std::size_t reservation(std::size_t cells) {
        require(cells&&cells<=maximum_cells,"compact chords directory extent invalid");
        const auto words=cells/32+(cells%32!=0);
        require(words<=(SIZE_MAX-sizeof(Directory)-1024)/sizeof(std::uint64_t),"compact chords directory size overflow");
        return sizeof(Directory)+1024+words*sizeof(std::uint64_t);
    }
    Directory(std::size_t cells,std::size_t budget,std::stop_token stop={})
        :cells_(cells),stop_(stop) {
        check_stop(stop_);bytes_=reservation(cells);require(bytes_<=budget,"compact chords directory workspace preflight");
        words_=cells/32+(cells%32!=0);tags_=std::make_unique<std::uint64_t[]>(words_);check_stop(stop_);
    }
    Directory(const Directory&)=delete;Directory& operator=(const Directory&)=delete;
    Directory(Directory&&)=delete;Directory& operator=(Directory&&)=delete;
    ~Directory(){cleanse();}
    std::size_t size()const noexcept{return cells_;}
    std::size_t working_bytes()const noexcept{return bytes_;}
    bool sealed()const noexcept{return state_==State::sealed;}
    void append(const Chord& c) {
        try {
            require(state_==State::building&&appended_<cells_,"compact chords directory immutable, failed or full");
            if((appended_&255U)==0)check_stop(stop_);
            const auto value=static_cast<std::uint64_t>(classify(c));
            tags_[appended_/32]|=value<<(2*(appended_%32));++appended_;
        }catch(...){fail();throw;}
    }
    void seal(std::stop_token stop={}) {
        try {
            require(state_==State::building&&appended_==cells_,"compact chords directory incomplete, immutable or failed");
            check_stop(stop_);check_stop(stop);state_=State::sealed;
        }catch(...){fail();throw;}
    }
    Kind kind(std::size_t k)const {
        require(sealed()&&k<cells_,"compact chords directory unavailable or index invalid");
        return static_cast<Kind>((tags_[k/32]>>(2*(k%32)))&3U);
    }
    Classification classification(std::size_t k)const {
        const auto value=kind(k);return {value!=Kind::unsupported,value==Kind::nonzero};
    }
};
struct Statistics {std::uint64_t requests=0,hits=0,provider_calls=0,shortcuts=0,errors=0;};
template<class Identity>class Tile {
    // Equality must compare immutable value fields, never dereference an
    // externally mutable source. The key binds the EXACT chosen operator.
    static_assert(std::is_trivially_copyable_v<Identity>,"compact chord identity must be a fixed value without owned heap storage");
    Identity expected_;
    std::array<Chord,64> payload_{};
    std::uint64_t present_=0,signed_zero_=0;
    std::size_t first_=SIZE_MAX;
    static void increment(std::uint64_t& value) {
        require(value!=UINT64_MAX,"compact chord statistics overflow");++value;
    }
    void clear()noexcept {
        OPENSSL_cleanse(payload_.data(),sizeof(payload_));present_=signed_zero_=0;first_=SIZE_MAX;
    }
public:
    inline static constexpr std::size_t tile_cells=64;
    Statistics stats;
    static constexpr std::size_t reservation(){return sizeof(Tile)+1024;}
    Tile(Identity expected,std::size_t budget,std::stop_token stop={}):expected_(expected) {
        require(reservation()<=budget,"compact chord tile workspace preflight");check_stop(stop);
    }
    Tile(const Tile&)=delete;Tile& operator=(const Tile&)=delete;
    Tile(Tile&&)=delete;Tile& operator=(Tile&&)=delete;
    ~Tile(){clear();OPENSSL_cleanse(&expected_,sizeof(expected_));}
    std::size_t working_bytes()const noexcept{return reservation();}
    template<class Provider>
    Chord get(const Directory& directory,std::size_t k,const Identity& actual,Provider&& provider,std::stop_token stop={}) {
        try {
            increment(stats.requests);check_stop(stop);
            require(actual==expected_,"compact chord tile identity changed");
            const auto tag=directory.kind(k);const auto base=(k/tile_cells)*tile_cells;
            if(first_!=base){clear();first_=base;}
            const auto local=k-base;const auto bit=std::uint64_t{1}<<local;
            if(tag==Kind::unsupported||tag==Kind::zero) {
                increment(stats.shortcuts);check_stop(stop);
                Chord canonical;canonical.supported=tag==Kind::zero;return canonical;
            }
            if(present_&bit) {
                const auto frozen=(signed_zero_&bit)?Kind::signed_zero:Kind::nonzero;
                require(frozen==tag,"compact chord tile frozen tag changed");increment(stats.hits);check_stop(stop);
                return payload_[local];
            }
            increment(stats.provider_calls);Chord value=std::forward<Provider>(provider)(k);
            struct Clean {Chord& c;~Clean(){OPENSSL_cleanse(&c,sizeof(c));}} clean{value};
            check_stop(stop);require(classify(value)==tag,"compact chord resolver differs from frozen tag");
            payload_[local]=value;present_|=bit;if(tag==Kind::signed_zero)signed_zero_|=bit;
            check_stop(stop);return payload_[local]; // value is wiped; return is an independent copy
        }catch(...) {clear();if(stats.errors!=UINT64_MAX)++stats.errors;throw;}
    }
};
} // namespace datapump::modem::detail::scheduled_default_compact_chords
