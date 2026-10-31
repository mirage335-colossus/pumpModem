#include "datapump/helper_measurement.hpp"

#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace h=datapump::helper;
namespace {
void require(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
h::HelperMeasurementConfig config() {return {24000,550,2400,2300,2500,300,0};}
h::HelperDwellMeasurement measurement(unsigned index) {
    h::HelperDwellMeasurement m;
    m.valid=true;m.first_sample_time_seconds=.4*index+.02;
    m.end_sample_time_seconds=m.first_sample_time_seconds+.36;
    m.time_seconds=m.first_sample_time_seconds+.18;
    m.shift_hz=28.8e6+static_cast<double>((index*17)%50)*50000;
    const double audio=(50+.02*m.time_seconds)*1e-6;
    const double shift=(.8+.00003*m.time_seconds)*1e-6;
    m.high_frequency_hz=2400*(1+audio)+m.shift_hz*shift;
    m.low_frequency_hz=550*(1+audio)+m.shift_hz*shift;
    m.high_phase_radians=.3;m.low_phase_radians=-.7;
    m.high_sigma_hz=.02;m.low_sigma_hz=.03;
    m.common_phase_frequency_sigma_hz=.001;
    m.high_cn0_db_hz=40;m.low_cn0_db_hz=37;
    return m;
}
void validation() {
    h::HelperMeasurement receiver(config());
    for (unsigned i=0;i<12;++i) require(receiver.ingest(measurement(i)),"valid external measurement rejected");
    const auto before=receiver.snapshot();
    require(before.valid && before.dwell_count==12,"external measurements did not produce a valid clock fit");
    const auto reject=[&](auto change) {
        auto m=measurement(12);change(m);
        require(!receiver.ingest(m),"malformed external measurement admitted");
        const auto after=receiver.snapshot();
        require(after.dwell_count==before.dwell_count && after.audio_ppm==before.audio_ppm &&
                after.shift_ppm==before.shift_ppm && after.valid==before.valid,
                "rejected measurement changed retained clock fit");
    };
    const double infinity=std::numeric_limits<double>::infinity();
    reject([](auto& m){m.valid=false;});
    reject([&](auto& m){m.first_sample_time_seconds=infinity;});
    reject([&](auto& m){m.end_sample_time_seconds=infinity;});
    reject([](auto& m){m.end_sample_time_seconds=m.first_sample_time_seconds;});
    reject([](auto& m){m.end_sample_time_seconds=m.first_sample_time_seconds+.05;});
    reject([](auto& m){m.time_seconds+=.01;});
    reject([&](auto& m){m.time_seconds=infinity;});
    reject([](auto& m){m.shift_hz=0;});
    reject([&](auto& m){m.shift_hz=infinity;});
    reject([](auto& m){m.shift_hz=1e308;});
    reject([](auto& m){m.shift_hz=-1e308;});
    reject([](auto& m){m.high_sigma_hz=m.low_sigma_hz=1e-154;});
    reject([](auto& m){
        m.first_sample_time_seconds=1e308;m.end_sample_time_seconds=1e308;
        m.time_seconds=1e308;
    });
    reject([](auto& m){m.high_frequency_hz=2501;});
    reject([](auto& m){m.low_frequency_hz=-12000;});
    reject([](auto& m){m.low_frequency_hz=m.high_frequency_hz;});
    reject([&](auto& m){m.high_phase_radians=infinity;});
    reject([&](auto& m){m.low_phase_radians=infinity;});
    reject([](auto& m){m.high_sigma_hz=0;});
    reject([](auto& m){m.low_sigma_hz=-1;});
    reject([](auto& m){m.high_sigma_hz=1e-300;});
    reject([](auto& m){m.low_sigma_hz=1e300;});
    reject([](auto& m){m.common_phase_frequency_sigma_hz=-1;});
    reject([&](auto& m){m.common_phase_frequency_sigma_hz=infinity;});
    reject([&](auto& m){m.high_cn0_db_hz=infinity;});
    reject([&](auto& m){m.low_cn0_db_hz=infinity;});
    require(!receiver.ingest(measurement(11)),"overlapping measurement admitted");
    require(receiver.ingest(measurement(12)),"invalid observation advanced history watermark");
    receiver.reset();
    require(receiver.snapshot().dwell_count==0,"reset retained external observations");
    for (unsigned i=0;i<256;++i) require(receiver.ingest(measurement(i)),"bounded history filled early");
    require(!receiver.ingest(measurement(256)),"external history exceeded fixed bound");
    require(receiver.snapshot().dwell_count==256,"history bound changed accepted count");
}
void acquisition_equivalence() {
    h::HelperMeasurement acquired(config()),ingested(config());
    constexpr double fs=24000,tau=2*std::numbers::pi;
    for (unsigned dwell=0;dwell<6;++dwell) {
        const auto expected=measurement(dwell);
        std::vector<std::complex<float>> samples(8640);
        for (std::size_t i=0;i<samples.size();++i) {
            const double t=static_cast<double>(i)/fs;
            // A component fixture: every dwell has a fresh unrelated common
            // phase, and the two simultaneously observed pilots carry it.
            const double common=.51*static_cast<double>(dwell*dwell);
            const auto z=std::polar(1.,tau*expected.high_frequency_hz*t+common)+
                std::polar(.7,tau*expected.low_frequency_hz*t+common+.2);
            samples[i]={static_cast<float>(z.real()),static_cast<float>(z.imag())};
        }
        const auto m=acquired.observe(samples,expected.shift_hz,expected.first_sample_time_seconds);
        require(m.valid && ingested.ingest(m),"acquired measurement failed external intake");
        const auto a=acquired.snapshot(),b=ingested.snapshot();
        require(a.valid==b.valid && a.dwell_count==b.dwell_count && a.time_seconds==b.time_seconds &&
                a.audio_ppm==b.audio_ppm && a.shift_ppm==b.shift_ppm &&
                a.audio_drift_ppm_per_second==b.audio_drift_ppm_per_second &&
                a.shift_drift_ppm_per_second==b.shift_drift_ppm_per_second && a.covariance==b.covariance,
                "external intake changed acquired clock fit");
    }
}
}
int main() {
    try {validation();acquisition_equivalence();std::cout<<"Helper external measurement intake passed\n";return 0;}
    catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
