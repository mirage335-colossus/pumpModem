#include "../src/live_helper.hpp"
#include <iostream>
using namespace datapump;
void check(bool v,const char* text){if(!v)throw Error(text);}
int main(){try {
    live::detail::HelperFftProbeTokens tokens;
    tokens.earn(8);check(!tokens.ready()&&tokens.may_learn_bound(),"one earned FFT token bought an unbounded first audit");
    tokens.require_packet(16600);check(!tokens.ready(),"one token bought the original16600-job FFT packet");
    tokens.earn(132800);check(tokens.ready()&&tokens.credit==16600,"actual FFT credit did not prepay one packet");
    tokens.charge(10375);check(!tokens.ready()&&!tokens.debt&&tokens.credit==6225,"paid packet minted a second cold audit or artificial debt");
    tokens.earn(215800);check(tokens.ready(),"fresh actual conditional FFT work did not rearm periodic audit");
    tokens.require_packet(20000);check(!tokens.ready(),"larger current geometry reused an insufficient earlier bound");
    tokens.earn(243000);check(tokens.ready(),"higher geometry bound could not earn its exact missing prepayment");
    tokens.charge(21000);check(!tokens.ready()&&tokens.debt>0,"actual onebatch overrun was not exposed as bounded debt");
    tokens.earn(243000);check(!tokens.ready(),"same cumulative FFT count minted fresh work");
    std::cout<<"native_fft_prepay first_packet16600 actual10375 no_initial_one_token_probe overrun_debt="<<tokens.debt<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
