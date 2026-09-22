// Compile only against xdsopl/LDPC at the pinned commit documented alongside
// this file. Do not link production LDPC or include its vendored matrices.
#include "encoder.hh"
#include "dvb_s2_tables.hh"
#include <openssl/evp.h>
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

template<class Table> bool fixture(const char* name) {
    LDPC<Table> code;
    const auto k=code.data_len(),n=code.code_len();
    std::vector<int> signs(n);
    std::vector<std::uint8_t> bits(n);
    for(int bit=0;bit<k;++bit) {
        const auto i=bit/8,byte=(i*73+i/7+0x5a)&255;
        signs[bit]=((byte>>(7-bit%8))&1)?-1:1;
    }
    LDPCEncoder<int> encoder;encoder.init(&code);
    encoder(signs.data(),signs.data()+k);
    for(int bit=0;bit<n;++bit)bits[bit]=signs[bit]<0;
    std::array<unsigned char,32> digest{};unsigned size=0;
    if(EVP_Digest(bits.data(),bits.size(),digest.data(),&size,EVP_sha256(),nullptr)!=1||size!=32)return false;
    std::cout<<"DVB-S2 "<<name<<" N="<<std::dec<<n<<" K="<<k<<" SHA256=";
    for(auto byte:digest)std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);
    std::cout<<'\n';return true;
}
int main() {
    return fixture<DVB_S2_TABLE_C4>("C4")&&fixture<DVB_S2_TABLE_C6>("C6")&&
        fixture<DVB_S2_TABLE_C7>("C7")?0:1;
}
