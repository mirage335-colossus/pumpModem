// Offline, known-transmitted-bit analysis of the archived 4M-QAM 8/9 capture.
// This deliberately bypasses source/FEC decoding and does not use audio devices.
#include "datapump/fast/modem.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
using namespace datapump;
using namespace datapump::fast;

int main(int argc,char** argv) {try {
    if(argc!=3)throw std::runtime_error("usage: qam-known-symbol-diagnostic capture.f32 transmitted.bits");
    auto p=capacity_profile();
    p.constellation=4194304;p.code_rate=CodeRate::eight_ninths;
    p.symbol_rate=17647.0588235294;p.rolloff=.02;p.marker_spacing_intervals=16;
    p.pilot_spacing_symbols=256;p.sample_rate=48000;p.carrier_hz=9300;p.amplitude=.30;
    std::ifstream bit_input(argv[2],std::ios::binary);
    if(!bit_input)throw std::runtime_error("cannot open transmitted bits");
    Bytes bits((std::istreambuf_iterator<char>(bit_input)),{});
    if(bits.empty()||bits.size()%physical_interval_bits||
       std::any_of(bits.begin(),bits.end(),[](auto bit){return bit>1;}))
        throw std::runtime_error("invalid transmitted bits");
    std::vector<float> llrs;
    std::vector<double> clock_trace;Receiver* active_receiver=nullptr;
    std::vector<std::complex<float>> observations;
    Receiver rx(p,[&](std::span<const float> soft) {
        llrs.insert(llrs.end(),soft.begin(),soft.end());
        clock_trace.push_back(active_receiver->progress().clock_error_ppm);
    },[&](std::complex<float> value) {observations.push_back(value);});
    active_receiver=&rx;
    std::ifstream pcm_input(argv[1],std::ios::binary);
    if(!pcm_input)throw std::runtime_error("cannot open capture");
    std::array<float,2400> buffer{};
    while(pcm_input.read(reinterpret_cast<char*>(buffer.data()),sizeof(buffer))||pcm_input.gcount()) {
        if(pcm_input.gcount()%sizeof(float))throw std::runtime_error("partial float sample");
        rx.push(std::span(buffer).first(static_cast<std::size_t>(pcm_input.gcount())/sizeof(float)));
    }
    if(!pcm_input.eof())throw std::runtime_error("capture read failed");
    rx.finish();
    if(llrs.empty()||llrs.size()>bits.size()||!rx.progress().physical_complete)
        throw std::runtime_error("diagnostic requires physically completed, correctly positioned whole intervals");
    std::cerr<<"RX bits "<<llrs.size()<<", TX bits "<<bits.size()
        <<", observed points "<<observations.size()<<", decision EVM "<<rx.progress().evm<<'\n';
    const auto points=square_qam_constellation(p.constellation);
    std::size_t observed=0;
    std::cout<<"interval,raw_errors,symbols,evm,phase,gain,mean_abs_llr,error_mean_abs_llr,peak_evm,variance,gmi,clock_ppm,signal_energy,error_energy,bit_log_loss_nats\n";
    for(std::size_t interval=0;interval*physical_interval_bits<llrs.size();++interval) {
        const auto payload=std::span(bits).subspan(interval*physical_interval_bits,physical_interval_bits);
        std::size_t errors=0;
        double llr_magnitude=0,error_llr=0,loss=0;
        for(std::size_t i=0;i<payload.size();++i) {
            const auto llr=llrs[interval*payload.size()+i];
            const bool wrong=(llr>0)!=bool(payload[i]);
            errors+=wrong;llr_magnitude+=std::abs(llr);
            if(wrong)error_llr+=std::abs(llr);
            const double argument=payload[i]?-llr:llr;
            loss+=std::max(0.,argument)+std::log1p(std::exp(-std::abs(argument)));
        }
        double error_power=0,signal_power=0,peak_error=0,variance=0;
        std::size_t variance_count=0,symbols=0;
        std::complex<double> correlation{};
        for(std::size_t offset=0;offset<payload.size();offset+=22) {
            unsigned label=0;
            for(unsigned bit=0;bit<22;++bit)
                label=(label<<1)|(offset+bit<payload.size()?payload[offset+bit]:0);
            const auto expected=points[label];
            if(observed>=observations.size())throw std::runtime_error("missing observed payload symbol");
            const auto actual=std::complex<double>(observations[observed++]);
            std::array<double,22> metrics{};
            square_qam_soft_demodulate(p.constellation,actual,metrics);
            for(unsigned bit=0;bit<22&&offset+bit<payload.size();++bit) {
                const auto llr=llrs[interval*payload.size()+offset+bit];
                if(std::abs(llr)>.1&&std::abs(llr)<20) {
                    variance+=metrics[bit]/llr;++variance_count;
                }
            }
            const auto error=std::norm(expected-actual);
            error_power+=error;peak_error=std::max(peak_error,error);
            signal_power+=std::norm(expected);correlation+=actual*std::conj(expected);++symbols;
        }
        std::cout<<interval<<','<<errors<<','<<symbols<<','<<std::sqrt(error_power/signal_power)
            <<','<<std::arg(correlation)<<','<<std::abs(correlation)/signal_power
            <<','<<llr_magnitude/payload.size()<<','<<(errors?error_llr/errors:0)
            <<','<<std::sqrt(peak_error)<<','<<(variance_count?variance/variance_count:0)
            <<','<<(payload.size()-loss/std::log(2.))/symbols<<','<<clock_trace[interval]
            <<','<<signal_power<<','<<error_power<<','<<loss<<'\n';
    }
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
