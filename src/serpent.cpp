/*
Portable scalar Serpent-256 adapted from Botan 3.8.1, commit
c06967092edd9baa2b42f89135380912113ae209:
src/lib/block/serpent/{serpent.cpp,serpent_fn.h,serpent_sbox.h}.
Copyright (C) 1999-2007,2013 Jack Lloyd. The Boolean S-box expressions
were discovered by Dag Arne Osvik and described in "Speeding Up Serpent".
Adaptation replaces Botan support types, selects 256-bit keys and scalar paths,
and adds explicit cleansing. The Botan license follows:

Copyright (C) 1999-2025 The Botan Authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions, and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions, and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/
#include "datapump/serpent.hpp"
#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <bit>

namespace datapump {
namespace {
using std::uint8_t;
using std::uint32_t;
using std::size_t;
template <unsigned S> inline uint32_t rotl(uint32_t x) { return std::rotl(x, S); }
template <unsigned S> inline uint32_t rotr(uint32_t x) { return std::rotr(x, S); }
inline uint32_t load_word(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void load_words(const uint8_t* p, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    a=load_word(p); b=load_word(p+4); c=load_word(p+8); d=load_word(p+12);
}
inline void store_words(uint8_t* p, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    const uint32_t x[]{a,b,c,d};
    for (size_t i=0;i<4;++i) for (size_t j=0;j<4;++j) p[4*i+j]=static_cast<uint8_t>(x[i] >> (8*j));
}
template <size_t S>
inline uint32_t shl(uint32_t v) {
   return v << S;
}

/*
* Serpent's Linear Transform
*/
template <typename T>
inline void transform(T& B0, T& B1, T& B2, T& B3) {
   B0 = rotl<13>(B0);
   B2 = rotl<3>(B2);
   B1 ^= B0 ^ B2;
   B3 ^= B2 ^ shl<3>(B0);
   B1 = rotl<1>(B1);
   B3 = rotl<7>(B3);
   B0 ^= B1 ^ B3;
   B2 ^= B3 ^ shl<7>(B1);
   B0 = rotl<5>(B0);
   B2 = rotl<22>(B2);
}

/*
* Serpent's Inverse Linear Transform
*/
template <typename T>
inline void i_transform(T& B0, T& B1, T& B2, T& B3) {
   B2 = rotr<22>(B2);
   B0 = rotr<5>(B0);
   B2 ^= B3 ^ shl<7>(B1);
   B0 ^= B1 ^ B3;
   B3 = rotr<7>(B3);
   B1 = rotr<1>(B1);
   B3 ^= B2 ^ shl<3>(B0);
   B1 ^= B0 ^ B2;
   B2 = rotr<3>(B2);
   B0 = rotr<13>(B0);
}

class Key_Inserter final {
   public:
      Key_Inserter(const uint32_t* RK) : m_RK(RK) {}

      template <typename T>
      inline void operator()(size_t R, T& B0, T& B1, T& B2, T& B3) const {
         B0 ^= m_RK[4 * R];
         B1 ^= m_RK[4 * R + 1];
         B2 ^= m_RK[4 * R + 2];
         B3 ^= m_RK[4 * R + 3];
      }

   private:
      const uint32_t* m_RK;
};

template <typename T>
inline void SBoxE0(T& a, T& b, T& c, T& d) {
   d ^= a;
   T t0 = b;
   b &= d;
   t0 ^= c;
   b ^= a;
   a |= d;
   a ^= t0;
   t0 ^= d;
   d ^= c;
   c |= b;
   c ^= t0;
   t0 = ~t0;
   t0 |= b;
   b ^= d;
   b ^= t0;
   d |= a;
   b ^= d;
   t0 ^= d;
   d = a;
   a = b;
   b = t0;
}

template <typename T>
inline void SBoxE1(T& a, T& b, T& c, T& d) {
   a = ~a;
   c = ~c;
   T t0 = a;
   a &= b;
   c ^= a;
   a |= d;
   d ^= c;
   b ^= a;
   a ^= t0;
   t0 |= b;
   b ^= d;
   c |= a;
   c &= t0;
   a ^= b;
   b &= c;
   b ^= a;
   a &= c;
   t0 ^= a;
   a = c;
   c = d;
   d = b;
   b = t0;
}

template <typename T>
inline void SBoxE2(T& a, T& b, T& c, T& d) {
   T t0 = a;
   a &= c;
   a ^= d;
   c ^= b;
   c ^= a;
   d |= t0;
   d ^= b;
   t0 ^= c;
   b = d;
   d |= t0;
   d ^= a;
   a &= b;
   t0 ^= a;
   b ^= d;
   b ^= t0;
   a = c;
   c = b;
   b = d;
   d = ~t0;
}

template <typename T>
inline void SBoxE3(T& a, T& b, T& c, T& d) {
   T t0 = a;
   a |= d;
   d ^= b;
   b &= t0;
   t0 ^= c;
   c ^= d;
   d &= a;
   t0 |= b;
   d ^= t0;
   a ^= b;
   t0 &= a;
   b ^= d;
   t0 ^= c;
   b |= a;
   b ^= c;
   a ^= d;
   c = b;
   b |= d;
   a ^= b;
   b = c;
   c = d;
   d = t0;
}

template <typename T>
inline void SBoxE4(T& a, T& b, T& c, T& d) {
   b ^= d;
   d = ~d;
   c ^= d;
   d ^= a;
   T t0 = b;
   b &= d;
   b ^= c;
   t0 ^= d;
   a ^= t0;
   c &= t0;
   c ^= a;
   a &= b;
   d ^= a;
   t0 |= b;
   t0 ^= a;
   a |= d;
   a ^= c;
   c &= d;
   a = ~a;
   t0 ^= c;
   c = a;
   a = b;
   b = t0;
}

template <typename T>
inline void SBoxE5(T& a, T& b, T& c, T& d) {
   a ^= b;
   b ^= d;
   d = ~d;
   T t0 = b;
   b &= a;
   c ^= d;
   b ^= c;
   c |= t0;
   t0 ^= d;
   d &= b;
   d ^= a;
   t0 ^= b;
   t0 ^= c;
   c ^= a;
   a &= d;
   c = ~c;
   a ^= t0;
   t0 |= d;
   t0 ^= c;
   c = a;
   a = b;
   b = d;
   d = t0;
}

template <typename T>
inline void SBoxE6(T& a, T& b, T& c, T& d) {
   c = ~c;
   T t0 = d;
   d &= a;
   a ^= t0;
   d ^= c;
   c |= t0;
   b ^= d;
   c ^= a;
   a |= b;
   c ^= b;
   t0 ^= a;
   a |= d;
   a ^= c;
   t0 ^= d;
   t0 ^= a;
   d = ~d;
   c &= t0;
   d ^= c;
   c = t0;
}

template <typename T>
inline void SBoxE7(T& a, T& b, T& c, T& d) {
   T t0 = b;
   b |= c;
   b ^= d;
   t0 ^= c;
   c ^= b;
   d |= t0;
   d &= a;
   t0 ^= c;
   d ^= b;
   b |= t0;
   b ^= a;
   a |= t0;
   a ^= c;
   b ^= t0;
   c ^= b;
   b &= a;
   b ^= t0;
   c = ~c;
   c |= a;
   t0 ^= c;
   c = b;
   b = d;
   d = a;
   a = t0;
}

template <typename T>
inline void SBoxD0(T& a, T& b, T& c, T& d) {
   c = ~c;
   T t0 = b;
   b |= a;
   t0 = ~t0;
   b ^= c;
   c |= t0;
   b ^= d;
   a ^= t0;
   c ^= a;
   a &= d;
   t0 ^= a;
   a |= b;
   a ^= c;
   d ^= t0;
   c ^= b;
   d ^= a;
   d ^= b;
   c &= d;
   t0 ^= c;
   c = b;
   b = t0;
}

template <typename T>
inline void SBoxD1(T& a, T& b, T& c, T& d) {
   T t0 = b;
   b ^= d;
   d &= b;
   t0 ^= c;
   d ^= a;
   a |= b;
   c ^= d;
   a ^= t0;
   a |= c;
   b ^= d;
   a ^= b;
   b |= d;
   b ^= a;
   t0 = ~t0;
   t0 ^= b;
   b |= a;
   b ^= a;
   b |= t0;
   d ^= b;
   b = a;
   a = t0;
   t0 = c;
   c = d;
   d = t0;
}

template <typename T>
inline void SBoxD2(T& a, T& b, T& c, T& d) {
   c ^= d;
   d ^= a;
   T t0 = d;
   d &= c;
   d ^= b;
   b |= c;
   b ^= t0;
   t0 &= d;
   c ^= d;
   t0 &= a;
   t0 ^= c;
   c &= b;
   c |= a;
   d = ~d;
   c ^= d;
   a ^= d;
   a &= b;
   d ^= t0;
   d ^= a;
   a = b;
   b = t0;
}

template <typename T>
inline void SBoxD3(T& a, T& b, T& c, T& d) {
   T t0 = c;
   c ^= b;
   a ^= c;
   t0 &= c;
   t0 ^= a;
   a &= b;
   b ^= d;
   d |= t0;
   c ^= d;
   a ^= d;
   b ^= t0;
   d &= c;
   d ^= b;
   b ^= a;
   b |= c;
   a ^= d;
   b ^= t0;
   a ^= b;
   t0 = a;
   a = c;
   c = d;
   d = t0;
}

template <typename T>
inline void SBoxD4(T& a, T& b, T& c, T& d) {
   T t0 = c;
   c &= d;
   c ^= b;
   b |= d;
   b &= a;
   t0 ^= c;
   t0 ^= b;
   b &= c;
   a = ~a;
   d ^= t0;
   b ^= d;
   d &= a;
   d ^= c;
   a ^= b;
   c &= a;
   d ^= a;
   c ^= t0;
   c |= d;
   d ^= a;
   c ^= b;
   b = d;
   d = t0;
}

template <typename T>
inline void SBoxD5(T& a, T& b, T& c, T& d) {
   b = ~b;
   T t0 = d;
   c ^= b;
   d |= a;
   d ^= c;
   c |= b;
   c &= a;
   t0 ^= d;
   c ^= t0;
   t0 |= a;
   t0 ^= b;
   b &= c;
   b ^= d;
   t0 ^= c;
   d &= t0;
   t0 ^= b;
   d ^= t0;
   t0 = ~t0;
   d ^= a;
   a = b;
   b = t0;
   t0 = d;
   d = c;
   c = t0;
}

template <typename T>
inline void SBoxD6(T& a, T& b, T& c, T& d) {
   a ^= c;
   T t0 = c;
   c &= a;
   t0 ^= d;
   c = ~c;
   d ^= b;
   c ^= d;
   t0 |= a;
   a ^= c;
   d ^= t0;
   t0 ^= b;
   b &= d;
   b ^= a;
   a ^= d;
   a |= c;
   d ^= b;
   t0 ^= a;
   a = b;
   b = c;
   c = t0;
}

template <typename T>
inline void SBoxD7(T& a, T& b, T& c, T& d) {
   T t0 = c;
   c ^= a;
   a &= d;
   t0 |= d;
   c = ~c;
   d ^= b;
   b |= a;
   a ^= c;
   c &= t0;
   d &= t0;
   b ^= c;
   c ^= a;
   a |= c;
   t0 ^= b;
   a ^= d;
   d ^= t0;
   t0 |= a;
   d ^= c;
   t0 ^= c;
   c = b;
   b = a;
   a = d;
   d = t0;
}

} // namespace
Serpent256::Serpent256(std::span<const uint8_t,32> key) {
   constexpr uint32_t PHI = 0x9E3779B9;
   std::array<uint32_t,140> W{};
   for (size_t i=0; i<8; ++i) W[i]=load_word(key.data()+4*i);
   for (size_t i=8; i<140; ++i) {
      const uint32_t wi=W[i-8]^W[i-5]^W[i-3]^W[i-1]^PHI^uint32_t(i-8);
      W[i]=rotl<11>(wi);
   }
   SBoxE0(W[20], W[21], W[22], W[23]);
   SBoxE0(W[52], W[53], W[54], W[55]);
   SBoxE0(W[84], W[85], W[86], W[87]);
   SBoxE0(W[116], W[117], W[118], W[119]);

   SBoxE1(W[16], W[17], W[18], W[19]);
   SBoxE1(W[48], W[49], W[50], W[51]);
   SBoxE1(W[80], W[81], W[82], W[83]);
   SBoxE1(W[112], W[113], W[114], W[115]);

   SBoxE2(W[12], W[13], W[14], W[15]);
   SBoxE2(W[44], W[45], W[46], W[47]);
   SBoxE2(W[76], W[77], W[78], W[79]);
   SBoxE2(W[108], W[109], W[110], W[111]);

   SBoxE3(W[8], W[9], W[10], W[11]);
   SBoxE3(W[40], W[41], W[42], W[43]);
   SBoxE3(W[72], W[73], W[74], W[75]);
   SBoxE3(W[104], W[105], W[106], W[107]);
   SBoxE3(W[136], W[137], W[138], W[139]);

   SBoxE4(W[36], W[37], W[38], W[39]);
   SBoxE4(W[68], W[69], W[70], W[71]);
   SBoxE4(W[100], W[101], W[102], W[103]);
   SBoxE4(W[132], W[133], W[134], W[135]);

   SBoxE5(W[32], W[33], W[34], W[35]);
   SBoxE5(W[64], W[65], W[66], W[67]);
   SBoxE5(W[96], W[97], W[98], W[99]);
   SBoxE5(W[128], W[129], W[130], W[131]);

   SBoxE6(W[28], W[29], W[30], W[31]);
   SBoxE6(W[60], W[61], W[62], W[63]);
   SBoxE6(W[92], W[93], W[94], W[95]);
   SBoxE6(W[124], W[125], W[126], W[127]);

   SBoxE7(W[24], W[25], W[26], W[27]);
   SBoxE7(W[56], W[57], W[58], W[59]);
   SBoxE7(W[88], W[89], W[90], W[91]);
   SBoxE7(W[120], W[121], W[122], W[123]);

   std::copy(W.begin()+8,W.end(),round_key_.begin());
   OPENSSL_cleanse(W.data(), sizeof(W));
}
Serpent256::~Serpent256() { OPENSSL_cleanse(round_key_.data(), sizeof(round_key_)); }
void Serpent256::encrypt_blocks(const uint8_t* in, uint8_t* out, size_t blocks) const {
   const Key_Inserter key_xor(round_key_.data());

   for(size_t i = 0; i < blocks; ++i) {
      uint32_t B0, B1, B2, B3;
      load_words(in + 16 * i, B0, B1, B2, B3);

      key_xor(0, B0, B1, B2, B3);
      SBoxE0(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(1, B0, B1, B2, B3);
      SBoxE1(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(2, B0, B1, B2, B3);
      SBoxE2(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(3, B0, B1, B2, B3);
      SBoxE3(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(4, B0, B1, B2, B3);
      SBoxE4(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(5, B0, B1, B2, B3);
      SBoxE5(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(6, B0, B1, B2, B3);
      SBoxE6(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(7, B0, B1, B2, B3);
      SBoxE7(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(8, B0, B1, B2, B3);
      SBoxE0(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(9, B0, B1, B2, B3);
      SBoxE1(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(10, B0, B1, B2, B3);
      SBoxE2(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(11, B0, B1, B2, B3);
      SBoxE3(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(12, B0, B1, B2, B3);
      SBoxE4(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(13, B0, B1, B2, B3);
      SBoxE5(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(14, B0, B1, B2, B3);
      SBoxE6(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(15, B0, B1, B2, B3);
      SBoxE7(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(16, B0, B1, B2, B3);
      SBoxE0(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(17, B0, B1, B2, B3);
      SBoxE1(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(18, B0, B1, B2, B3);
      SBoxE2(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(19, B0, B1, B2, B3);
      SBoxE3(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(20, B0, B1, B2, B3);
      SBoxE4(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(21, B0, B1, B2, B3);
      SBoxE5(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(22, B0, B1, B2, B3);
      SBoxE6(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(23, B0, B1, B2, B3);
      SBoxE7(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(24, B0, B1, B2, B3);
      SBoxE0(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(25, B0, B1, B2, B3);
      SBoxE1(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(26, B0, B1, B2, B3);
      SBoxE2(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(27, B0, B1, B2, B3);
      SBoxE3(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(28, B0, B1, B2, B3);
      SBoxE4(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(29, B0, B1, B2, B3);
      SBoxE5(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(30, B0, B1, B2, B3);
      SBoxE6(B0, B1, B2, B3);
      transform(B0, B1, B2, B3);
      key_xor(31, B0, B1, B2, B3);
      SBoxE7(B0, B1, B2, B3);
      key_xor(32, B0, B1, B2, B3);

      store_words(out + 16 * i, B0, B1, B2, B3);
   }
}

void Serpent256::decrypt_blocks(const uint8_t* in, uint8_t* out, size_t blocks) const {
   const Key_Inserter key_xor(round_key_.data());

   for(size_t i = 0; i < blocks; ++i) {
      uint32_t B0, B1, B2, B3;
      load_words(in + 16 * i, B0, B1, B2, B3);

      key_xor(32, B0, B1, B2, B3);
      SBoxD7(B0, B1, B2, B3);
      key_xor(31, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD6(B0, B1, B2, B3);
      key_xor(30, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD5(B0, B1, B2, B3);
      key_xor(29, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD4(B0, B1, B2, B3);
      key_xor(28, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD3(B0, B1, B2, B3);
      key_xor(27, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD2(B0, B1, B2, B3);
      key_xor(26, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD1(B0, B1, B2, B3);
      key_xor(25, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD0(B0, B1, B2, B3);
      key_xor(24, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD7(B0, B1, B2, B3);
      key_xor(23, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD6(B0, B1, B2, B3);
      key_xor(22, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD5(B0, B1, B2, B3);
      key_xor(21, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD4(B0, B1, B2, B3);
      key_xor(20, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD3(B0, B1, B2, B3);
      key_xor(19, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD2(B0, B1, B2, B3);
      key_xor(18, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD1(B0, B1, B2, B3);
      key_xor(17, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD0(B0, B1, B2, B3);
      key_xor(16, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD7(B0, B1, B2, B3);
      key_xor(15, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD6(B0, B1, B2, B3);
      key_xor(14, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD5(B0, B1, B2, B3);
      key_xor(13, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD4(B0, B1, B2, B3);
      key_xor(12, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD3(B0, B1, B2, B3);
      key_xor(11, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD2(B0, B1, B2, B3);
      key_xor(10, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD1(B0, B1, B2, B3);
      key_xor(9, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD0(B0, B1, B2, B3);
      key_xor(8, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD7(B0, B1, B2, B3);
      key_xor(7, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD6(B0, B1, B2, B3);
      key_xor(6, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD5(B0, B1, B2, B3);
      key_xor(5, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD4(B0, B1, B2, B3);
      key_xor(4, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD3(B0, B1, B2, B3);
      key_xor(3, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD2(B0, B1, B2, B3);
      key_xor(2, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD1(B0, B1, B2, B3);
      key_xor(1, B0, B1, B2, B3);
      i_transform(B0, B1, B2, B3);
      SBoxD0(B0, B1, B2, B3);
      key_xor(0, B0, B1, B2, B3);

      store_words(out + 16 * i, B0, B1, B2, B3);
   }
}

} // namespace datapump
