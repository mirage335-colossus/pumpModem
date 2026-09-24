#include "legacy_simulation.hpp"
#include <iostream>
#include <limits>
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main() {
    try {
        using namespace datapump::legacy;using namespace legacy_regression;
        const std::string text="CQ CQ de TEST 123\n";
        for(const auto mode:{Mode::bpsk31,Mode::bpsk125,Mode::olivia4_2000}) {
            const auto result=simulate({mode,1500},text,maximum_snr_db,19);
            check(result.received.find(text)!=std::string::npos,"Legacy high-SNR waveform failed");
            const auto measured=10*std::log10(result.signal_power/(result.measured_noise_power));
            check(std::abs(measured-maximum_snr_db)<.15,"3000 Hz noise normalization is wrong");
        }
        unsigned high=0,low=0;
        for(double snr=minimum_snr_db;snr<=maximum_snr_db;snr+=5) {
            unsigned successes=0;
            for(std::uint64_t seed=1;seed<=3;++seed)successes+=simulate({Mode::bpsk125,1500},text,snr,seed).received.find(text)!=std::string::npos;
            std::cout<<"PSK125 S/N (3000 Hz) "<<snr<<" dB: "<<successes<<"/3 exact text\n";
            if(snr==psk125_design_failure_db-5)check(successes==0,"PSK125 failure bracket shifted below regression reference");
            if(snr==psk125_design_failure_db+5)check(successes==3,"PSK125 success bracket shifted above regression reference");
            if(snr==minimum_snr_db)low=successes;
            if(snr==maximum_snr_db)high=successes;
        }
        check(low==0&&high==3,"Legacy SNR sweep failed to straddle the PSK125 failure region");
        check(simulate({Mode::bpsk125,1500},text,0,57).received==simulate({Mode::bpsk125,1500},text,0,57).received,"Seeded channel is not repeatable");
        for(double invalid:{minimum_snr_db-.1,maximum_snr_db+.1,std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected=false;try{(void)noise_sigma(1,invalid);}catch(const std::exception&){rejected=true;}
            check(rejected,"Out-of-range/nonfinite Legacy SNR accepted");
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
