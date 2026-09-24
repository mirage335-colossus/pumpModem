#pragma once
#include "application.hpp"
#include <chrono>
#include <stdexcept>

namespace datapump::gui::test {
// The same field transitions must reach native label colors in both adapters.
template<class Present,class Verify>
void estimate_warning_fields(Application& application,Present present,Verify verify) {
    using F=ui::Field;
    const auto await=[&](auto ready) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        do {present();if(ready())return;}while(std::chrono::steady_clock::now()<deadline);
        throw std::runtime_error("Native estimate warning fixture did not receive its expected labels");
    };
    const auto numeric=[&] {
        return application.field(F::simulation_confidence).text.find('%')!=std::string::npos&&
            application.field(F::lpi_estimate).text.find("×")!=std::string::npos;
    };
    application.edit(F::message,"e");application.edit(F::snr,"23");
    application.select(F::simulation_oscillator,"gpsdo-ocxo");
    application.select(F::simulation,"3dBm -170dB");
    await(numeric);
    verify(F::simulation_confidence,ui::TextTone::negative);
    verify(F::lpi_estimate,ui::TextTone::negative);
    application.edit(F::short_bits,"001x");
    await([&] {return application.field(F::simulation_confidence).text.ends_with("Unavailable");});
    verify(F::simulation_confidence,ui::TextTone::normal);
    verify(F::lpi_estimate,ui::TextTone::negative);
    application.edit(F::message,"e");application.edit(F::bandwidth,"invalid");
    await([&] {return application.field(F::lpi_estimate).text.ends_with("Invalid settings");});
    verify(F::lpi_estimate,ui::TextTone::negative);
    application.edit(F::bandwidth,"3.6 kHz");application.edit(F::snr,"55");
    await([&] {return application.field(F::lpi_estimate).text.find("outside model range")!=std::string::npos;});
    verify(F::lpi_estimate,ui::TextTone::negative);
    application.edit(F::message,"e");application.edit(F::snr,"0");
    application.select(F::simulation,"3dBm -60dB");
    await(numeric);
    verify(F::simulation_confidence,ui::TextTone::normal);
    verify(F::lpi_estimate,ui::TextTone::normal);
}
}
