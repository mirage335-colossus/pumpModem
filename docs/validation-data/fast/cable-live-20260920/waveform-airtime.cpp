// Projection only: call the production estimator; generate no PCM or live audio.
// From the repository root:
// c++ -O3 -std=c++20 -Iinclude docs/validation-data/fast/cable-live-20260920/waveform-airtime.cpp -o /tmp/fast-waveform-airtime build/libdatapump_fast.a build/libdatapump.a -lcrypto -ldl build/third_party/xz/liblzma.a
// /tmp/fast-waveform-airtime > /tmp/fast-waveform-airtime.csv
#include "datapump/fast/codec.hpp"
#include <iomanip>
#include <iostream>

using namespace datapump::fast;
struct Waveform { double symbols_per_second, carrier_hz, rolloff; };

int main() {
    std::cout << "symbol_rate,carrier_hz,rolloff,sample_rate,apsk,code_rate,rs,depth,encrypted,source_bytes,intervals,samples,estimated_seconds,estimated_source_bps\n";
    std::cout << std::setprecision(15);
    for(const auto waveform : {Waveform{15000,9300,.2}, Waveform{16500,9400,.1},
                              Waveform{18000,10200,.1}, Waveform{19200,10800,.1}}) {
        auto p=profile(Channel::wire);
        p.constellation=256;p.code_rate=CodeRate::seven_eighths;
        p.robust=false;p.interleave_depth=62;p.sample_rate=48000;
        p.symbol_rate=waveform.symbols_per_second;
        p.carrier_hz=waveform.carrier_hz;p.rolloff=waveform.rolloff;
        for(const auto bytes : {100000ULL,5000000ULL,50000000ULL}) {
            const auto e=estimate_transmission(p,false,bytes);
            std::cout << p.symbol_rate << ',' << p.carrier_hz << ',' << p.rolloff
                << ",48000,256,0.875,high-rate,62,false," << bytes << ','
                << e.intervals << ',' << e.samples << ',' << e.seconds << ','
                << e.source_bps << '\n';
        }
    }
}
