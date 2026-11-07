#include "datapump/serpent.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
template <std::size_t N> std::array<std::uint8_t,N> hex(std::string_view text) {
    if (text.size()!=2*N) throw std::runtime_error("invalid test vector length");
    std::array<std::uint8_t,N> result{};
    auto digit=[](char c) -> unsigned {
        if (c>='0' && c<='9') return static_cast<unsigned>(c-'0');
        if (c>='A' && c<='F') return static_cast<unsigned>(c-'A'+10);
        throw std::runtime_error("invalid test vector digit");
    };
    for (std::size_t i=0;i<N;++i) result[i]=static_cast<std::uint8_t>((digit(text[2*i])<<4)|digit(text[2*i+1]));
    return result;
}
void check(bool result,const char* text) { if (!result) throw std::runtime_error(text); }
void known_answer(std::string_view key_text,std::string_view in_text,std::string_view out_text) {
    const auto key=hex<32>(key_text); const auto in=hex<16>(in_text); const auto expected=hex<16>(out_text);
    datapump::Serpent256 cipher(key);
    std::array<std::uint8_t,16> out{};
    cipher.encrypt_blocks(in.data(),out.data(),1);
    check(out==expected,"Serpent encryption differs from published vector");
    cipher.decrypt_blocks(out.data(),out.data(),1);
    check(out==in,"Serpent in-place decryption differs from published vector");
    cipher.encrypt_blocks(out.data(),out.data(),1);
    check(out==expected,"Serpent in-place encryption differs from published vector");
    cipher.decrypt_blocks(expected.data(),out.data(),1);
    check(out==in,"Serpent decryption differs from published vector");
    std::array<std::uint8_t,32> pair{};
    std::copy(in.begin(),in.end(),pair.begin());std::copy(in.begin(),in.end(),pair.begin()+16);
    cipher.encrypt_blocks(pair.data(),pair.data(),2);
    check(std::equal(pair.begin(),pair.begin()+16,expected.begin()) && std::equal(pair.begin()+16,pair.end(),expected.begin()),"Serpent multi-block encryption failed");
    cipher.decrypt_blocks(pair.data(),pair.data(),2);
    check(std::equal(pair.begin(),pair.begin()+16,in.begin()) && std::equal(pair.begin()+16,pair.end(),in.begin()),"Serpent multi-block decryption failed");
}
}
int main() {
    try {
        // Botan 3.8.1 src/tests/data/block/serpent.vec, independent expected outputs.
        known_answer("0000000000000000000000000000000000000000000000000000000000000000","D095576FCEA3E3A7ED98D9F29073D78E","B90EE5862DE69168F2BDD5125B45472B");
        known_answer("0000000000000000000000000000000000000000000000000000000000000080","00000000000000000000000000000000","1908EF821AD2EBC0CB28BF66E796EDAB");
        known_answer("0000000000000000000000000800000000000000000000000000000000000000","00000000000000000000000000000000","E42C18F2B56A0E536E4908F84DE06419");
        known_answer("00112233445566778899AABBCCDDEEFFFFEEDDCCBBAA99887766554433221100","1032547698BADCFEEFCDAB8967452301","93DF9A3CAFE387BD999EEBE393A17FCA");
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
