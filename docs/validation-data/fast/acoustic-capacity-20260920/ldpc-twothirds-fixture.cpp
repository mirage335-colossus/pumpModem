// Compile against pinned, independent xdsopl/LDPC headers, never production
// src/fast/ldpc.cpp or third_party/ldpc/tables.hpp. See ldpc-twothirds-method.md.
#include "encoder.hh"
#include "dvb_s2_tables.hh"
#include <openssl/evp.h>
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

int main() {
    LDPC<DVB_S2_TABLE_B6> code;
    const auto k=code.data_len(), n=code.code_len();
    std::vector<int> signs(n);
    std::vector<std::uint8_t> bits(n);
    for(int bit=0;bit<k;++bit) {
        const auto i=bit/8, byte=(i*73+i/7+0x5a)&255;
        signs[bit]=((byte>>(7-bit%8))&1)?-1:1;
    }
    LDPCEncoder<int> encoder;encoder.init(&code);
    encoder(signs.data(),signs.data()+k);
    for(int bit=0;bit<n;++bit)bits[bit]=signs[bit]<0;
    std::array<unsigned char,32> digest{};unsigned size=0;
    if(EVP_Digest(bits.data(),bits.size(),digest.data(),&size,EVP_sha256(),nullptr)!=1||size!=32)return 1;
    std::cout<<"DVB-S2 B6 N="<<n<<" K="<<k<<" SHA256=";
    for(auto byte:digest)std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);
    std::cout<<'\n';
}
