#include "datapump/triad_modem_v3.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using namespace datapump::triad::v3;
using datapump::Crypto;

void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

Crypto fixture_key() {
    std::array<std::uint8_t, 32> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(17 * i + 3);
    return Crypto(bytes);
}

void geometry_checks() {
    Format f;
    const auto g = geometry(f);
    check(std::abs(g.inner_chip_rate_hz - 6) < 1e-12 &&
          std::abs(g.fine_chip_rate_hz - 60) < 1e-12,
          "v3 Rate/Added mapping changed");
    check(g.bit_seconds >= 8 && g.dwells >= 20 && g.inner_coefficients >= 64,
          "v3 format violates minimum duration, dwell diversity, or complexity");
    check(g.fine_coefficients == g.inner_coefficients * f.added_symbols,
          "Added Symbols split an inner-symbol boundary");
    check(std::abs(g.occupied_bandwidth_hz - 225) < 1e-10,
          "Rate36 v3 carrier spacing changed");
    f.bit_seconds = 8;
    f.inner_rate_hz = 360;
    f.sample_rate = 9600;
    const auto hf = geometry(f);
    check(hf.bit_seconds >= 8 && hf.dwells >= 20 && hf.fine_coefficients % 10 == 0,
          "HF v3 format lacks full-duration Added-symbol cells");
    check(std::abs(hf.occupied_bandwidth_hz - 2250) < 1e-9,
          "Rate360 v3 carrier spacing changed");
    f.inner_rate_hz = 36;
    f.sample_rate = 960;
    f.fhss = true;
    f.bit_seconds = 16;
    check(geometry(f).dwells >= 40, "Rate36 FHSS calendar lacks dwell diversity");
    f.quiet_seconds = .15;
    bool rejected = false;
    try { (void)geometry(f); } catch (const std::exception&) { rejected = true; }
    check(rejected, "FHSS geometry accepted ramps with no active pattern cells");
}

void random_access_and_chunk_checks() {
    auto key = fixture_key();
    Format f;
    f.bit_seconds = 16;
    PatternSource whole(f, key, 1), split(f, key, 1);
    std::vector<Complex> expected(whole.geometry().source_samples);
    whole.render(0, expected);
    std::vector<Complex> actual(expected.size());
    std::uint64_t offset = 0;
    std::uint32_t state = 0x12345678;
    while (offset < actual.size()) {
        state = state * 1664525U + 1013904223U;
        const auto count = std::min<std::size_t>(1 + (state % 401), actual.size() - offset);
        split.render(offset, std::span(actual).subspan(offset, count));
        offset += count;
    }
    double error = 0, energy = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        error = std::max(error, std::abs(actual[i] - expected[i]));
        energy += std::norm(actual[i]) / f.sample_rate;
    }
    check(error == 0, "sample sequence changes at arbitrary rendering chunk boundaries");
    if (!(energy > 2 && energy < 40)) std::cerr << "energy=" << energy << '\n';
    check(energy > 2 && energy < 40, "shaped v3 bit has implausible integrated baseband energy");
    check(whole.sample_at(-1) == Complex{} &&
          whole.sample_at(std::numeric_limits<long double>::infinity()) == Complex{} &&
          whole.sample_at(static_cast<long double>(whole.geometry().source_samples)) == Complex{},
          "finite source is not exactly zero outside its stated sample support");
    check(whole.working_bytes() == whole.geometry().source_workspace_bytes &&
          whole.working_bytes() < 1024 * 1024,
          "source state grows with one complete bit instead of bounded caches");

    f.symbol_ordinal = 86400ULL * 1000000;
    PatternSource day_addressed(f, key, 1);
    const auto at_day = day_addressed.sample_at(whole.geometry().source_samples / 2);
    check(std::isfinite(at_day.real()) && std::isfinite(at_day.imag()),
          "large monotonic symbol address failed");
    f.symbol_ordinal++;
    PatternSource next_symbol(f, key, 1);
    check(next_symbol.sample_at(next_symbol.geometry().source_samples / 2) != at_day,
          "different canonical symbol ordinal reused the same pattern");
    f.symbol_ordinal--;
    f.encrypt = false;
    PatternSource clear_zero(f, key, 0), clear_one(f, key, 1);
    check(clear_zero.sample_at(clear_zero.geometry().source_samples / 2) !=
          clear_one.sample_at(clear_one.geometry().source_samples / 2),
          "plaintext bit alternatives are not distinguishable");
}

void protected_waveform_checks() {
    auto key=fixture_key(); Format f; f.inner_rate_hz=360; f.sample_rate=9600;
    f.bit_seconds=8; f.fhss=true; f.quiet_seconds=.055; f.ramp_seconds=.002;
    PatternSource zero(f,key,0),one(f,key,1);
    const auto& g=zero.geometry();
    const auto halo=std::ceil(8.*f.sample_rate/g.fine_chip_rate_hz)+2;
    for(unsigned dwell=1;dwell<19;++dwell){
        for(double within: {.01,.04,.399}) {
            const auto sample=halo+(dwell*.4+within)*f.sample_rate;
            check(zero.sample_at(sample)==Complex{}&&one.sample_at(sample)==Complex{},
                  "FHSS source pulses leaked through the transmitter quiet gate");
        }
        for(double within:{.054,.056,.057}) {
            const auto sample=halo+(dwell*.4+within)*f.sample_rate;
            check(std::abs(zero.sample_at(sample)-one.sample_at(sample))<1e-12,
                  "payload pulse support intruded into FHSS quiet/ramp/margin");
        }
    }
    f.inner_rate_hz=359.3; f.sample_rate=9600; f.fhss=false; f.bit_seconds=17;
    PatternSource fractional(f,key,0);
    std::vector<Complex> a(37),b(37);
    fractional.render(5001,a);fractional.render(5001,b);
    check(a==b,"noncommensurate fine/dwell grid is not deterministic");
    f.bit_seconds=2*86400;
    PatternSource days(f,key,0);
    check(days.working_bytes()==fractional.working_bytes(),"source workspace scales with days");
    const auto v=days.sample_at(days.geometry().source_samples-1000);
    check(std::isfinite(v.real())&&std::isfinite(v.imag()),"multi-day random access failed");
    f.inner_rate_hz=36;f.sample_rate=960;f.bit_seconds=24;f.burst_wait_tiles=1;
    PatternSource burst(f,key,1);
    const auto& bg=burst.geometry();
    const auto bh=std::ceil(8.*f.sample_rate/bg.fine_chip_rate_hz)+2;
    check(burst.sample_at(bh+1.5*bg.tile_seconds*f.sample_rate)==Complex{},
          "Symbol Burst wait emitted source payload");
    for(bool hopping:{false,true}) {
        Format d;d.inner_rate_hz=hopping?360:240;d.added_symbols=1;
        d.sample_rate=960;d.bit_seconds=8;d.fhss=hopping;
        PatternSource bit_zero(d,key,0),bit_one(d,key,1);const auto& dg=bit_zero.geometry();
        const auto h=std::ceil(8.*d.sample_rate/dg.fine_chip_rate_hz)+2;
        unsigned occupied=0;
        for(std::uint64_t dwell=0;dwell<dg.dwells;++dwell){double evidence=0;
            for(unsigned j=0;j<32;++j){const auto sample=h+
                (static_cast<double>(dwell)*.4+(j+.5)*.4/32)*d.sample_rate;
                evidence+=std::norm(bit_zero.sample_at(sample)-bit_one.sample_at(sample));}
            if(evidence>1e-12)++occupied;
        }
        check(occupied>=20,"guarded one-bit payload occupies fewer than twenty dwells");
    }
}

void complete_tile_duration_checks() {
    for(double rate:{3.6,36.,359.3})for(unsigned waits:{0U,1U}) {
        Format f;f.inner_rate_hz=rate;f.sample_rate=9600;f.burst_wait_tiles=waits;
        const auto tile=geometry(f).tile_seconds;
        for(std::uint64_t count=1;count<=1000;++count) {
            f.bit_seconds=static_cast<double>(count)*tile;
            const auto exact=geometry(f);
            check(exact.tile_count==count,"represented complete-tile request added an extra tile");
            f.bit_seconds=(static_cast<double>(count)+1e-6)*tile;
            check(geometry(f).tile_count==count+1,"genuine fractional tile request was rounded down");
        }
    }
}
} // namespace

int main() {
    try {
        geometry_checks();
        random_access_and_chunk_checks();
        protected_waveform_checks();
        complete_tile_duration_checks();
        std::cout << "triad v3 source: geometry, addressing, pulse, and chunk checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
