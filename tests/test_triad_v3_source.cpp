#include "triad_crypto_fixture.hpp"
#include "datapump/triad_modem_v3.hpp"
#include "datapump/triad_pulse.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
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
    return triad_test_key(bytes);
}

// Reconstruct the documented public coordinates rather than asking the
// transmitter which child it selected. This checks the current independently keyed layout.
datapump::Bytes payload_context(const Format& f) {
    constexpr std::string_view prefix="datapump/experimental-triad/v3/independent-apsk-2/";
    datapump::Bytes id(prefix.begin(),prefix.end());
    const auto append64=[&](std::uint64_t value){
        for(int shift=56;shift>=0;shift-=8)id.push_back(static_cast<std::uint8_t>(value>>shift));
    };
    constexpr std::string_view purpose="payload";
    append64(purpose.size());id.insert(id.end(),purpose.begin(),purpose.end());
    append64(f.epoch);append64(f.symbol_ordinal);append64(0);
    append64(2);append64(f.fhss_center_aligned?1:0);
    return id;
}

void confidentiality_payload_checks() {
    const auto key=fixture_key();
    {
        for(bool aligned:{false,true}){
            Format f;f.epoch=1720000011;
            if(aligned){f.inner_rate_hz=360;f.added_symbols=1;f.sample_rate=960;
                f.bit_seconds=8;f.fhss=true;f.fhss_center_aligned=true;
                f.quiet_seconds=.055;f.ramp_seconds=.006;}
            bool seen[2]{};
            // Search public ordinals for both mask values, then compare the
            // actual encrypted source with the independently selected clear
            // bit alternative at several non-guard coordinates.
            for(std::uint64_t ordinal=0;ordinal<64 && !(seen[0]&&seen[1]);++ordinal){
                f.symbol_ordinal=ordinal;
                const auto context=payload_context(f);
                const auto mask=key.confidentiality_child(context).confidentiality_stream(f.epoch,0,1)[0]&1U;
                if(seen[mask])continue;
                seen[mask]=true;
                for(unsigned bit=0;bit<2;++bit){
                    f.encrypt=true;PatternSource encrypted(f,key,bit);
                    f.encrypt=false;PatternSource clear(f,key,bit^mask);
                    PatternSource opposite(f,key,bit^mask^1U);
                    unsigned differs=0,shared=0;
                    // Sparse evenly spaced points can coincide with the FHSS
                    // guards on every dwell. Walk actual cells instead, so the
                    // comparison must cover both payload and shared cover.
                    for(std::uint64_t index=0;index<encrypted.geometry().fine_cells &&
                            (differs<8 || shared<8);++index){
                        const auto expected=clear.pattern_coefficient_at(index);
                        check(encrypted.pattern_coefficient_at(index)==expected,
                              "triad payload did not use the confidentiality cascade at its canonical context");
                        if(opposite.pattern_coefficient_at(index)!=expected)++differs;
                        else ++shared;
                    }
                    check(differs>=8&&shared>=8,
                          "triad payload comparison did not cover both payload and shared cover");
                }
            }
            check(seen[0]&&seen[1],"triad confidentiality fixture did not exercise both mask values");
        }
    }
}

void independent_patterns_and_apsk() {
    auto key=fixture_key();Format f;f.encrypt=false;f.added_symbols=1;
    f.inner_rate_hz=240;f.sample_rate=2048;f.bit_seconds=24;
    PatternSource zero(f,key,0),one(f,key,1);
    constexpr unsigned permutations[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    std::array<double,6> amplitude_error{};std::array<Complex,6> phase_relation{},conjugate_relation{};
    Complex phase_mean{};double energy=0;unsigned count=0;
    for(std::uint64_t k=0;k<zero.geometry().fine_cells;++k){
        const auto a=zero.pattern_coefficient_at(k),b=one.pattern_coefficient_at(k);
        double norm_a=0,norm_b=0;for(unsigned c=0;c<3;++c){norm_a+=std::norm(a[c]);norm_b+=std::norm(b[c]);}
        if(norm_a==0&&norm_b==0){
            check((static_cast<long double>(k)+.5L)/zero.geometry().fine_chip_rate_hz>=zero.geometry().bit_seconds,
                  "zero coefficient inside the active continuous calendar");
            continue; // The conservative ceil count can contain one end cell.
        }
        check(std::abs(norm_a-3)<3e-15&&std::abs(norm_b-3)<3e-15,
              "active coefficient power is not constant");
        if(a==b)continue; // Common non-payload cover is deliberately identical.
        ++count;energy+=norm_a;
        for(unsigned c=0;c<3;++c)phase_mean+=a[c]/std::abs(a[c]);
        for(unsigned p=0;p<6;++p)for(unsigned c=0;c<3;++c){
            amplitude_error[p]+=std::pow(std::abs(a[c])-std::abs(b[permutations[p][c]]),2);
            phase_relation[p]+=std::conj(a[c])*b[permutations[p][c]];
            conjugate_relation[p]+=a[c]*b[permutations[p][c]];
        }
    }
    check(count>700,"too little payload sampled for independent-pattern regression");
    check(std::abs(phase_mean)/(3*count)<.1,"new keyed phase distribution has a large mean");
    for(unsigned p=0;p<6;++p){
        check(amplitude_error[p]/count>.08,"bit alternatives retain a fixed amplitude permutation");
        check(std::abs(phase_relation[p])/energy<.15&&std::abs(conjugate_relation[p])/energy<.15,
              "bit alternatives retain a fixed phase/conjugation/permutation relation");
    }
    check(zero.pattern_coefficient_at(zero.geometry().fine_cells)==std::array<Complex,3>{},
          "coefficient diagnostics escape the finite pattern calendar");

    f=Format{};f.encrypt=false;f.added_interleave=false;f.bit_seconds=32;
    PatternSource apsk(f,key,0),other(f,key,1);
    auto changed=f;changed.added_amplitude_depth=.6;PatternSource deeper(changed,key,0);
    std::uint64_t first=0;
    while(first<apsk.geometry().fine_cells&&apsk.pattern_coefficient_at(first)==other.pattern_coefficient_at(first))++first;
    check(first+400<apsk.geometry().fine_cells,"missing APSK test payload");
    const auto radius_ratio=std::sqrt((1+f.added_amplitude_depth)/(1-f.added_amplitude_depth));
    unsigned amplitude_changes=0,positive=0,negative=0;
    for(std::uint64_t group=0;group<40;++group){
        const auto reference=apsk.pattern_coefficient_at(first+group*f.added_symbols);
        for(unsigned j=0;j<f.added_symbols;++j){
            const auto k=first+group*f.added_symbols+j;
            const auto a=apsk.pattern_coefficient_at(k),d=deeper.pattern_coefficient_at(k);
            double power=0;
            for(unsigned c=0;c<3;++c){
                check(std::abs(a[c])>0,"APSK introduced a zero amplitude");power+=std::norm(a[c]);
                const auto relative=a[c]/reference[c];
                check(std::abs(relative.imag())<2e-14,"Added Symbols used a phase outside the two antipodal phases");
                if(relative.real()>0)++positive;else ++negative;
                check(std::abs(a[c]/std::abs(a[c])-d[c]/std::abs(d[c]))<2e-14,
                      "changing radius depth changes the independently derived phase");
            }
            check(std::abs(power-3)<3e-15,"APSK coefficient balancing changed total power");
            // The common gain cancels from carrier ratios. Two binary radii
            // allow these five ratios relative to another fine symbol.
            const auto r=std::abs((a[0]/a[1])/(reference[0]/reference[1]));
            double distance=1e9;
            for(int exponent=-2;exponent<=2;++exponent)distance=std::min(distance,std::abs(r-std::pow(radius_ratio,exponent)));
            check(distance<2e-14,"Added Symbols have more than two radii");
            if(std::abs(r-1)>1e-12)++amplitude_changes;
        }
    }
    check(amplitude_changes>100&&positive>100&&negative>100,
          "APSK did not exercise both radii and both phases");
    for(double depth:{0.,1.,-1.,std::numeric_limits<double>::quiet_NaN()}){
        f.added_amplitude_depth=depth;bool rejected=false;
        try{(void)geometry(f);}catch(const std::exception&){rejected=true;}
        check(rejected,"invalid independent APSK depth was accepted");
    }
}

void revision_random_access_checks() {
    auto key=fixture_key();
    {
        Format f;f.bit_seconds=38*86400.;
        PatternSource sequential(f,key,1),random(f,key,1);
        const auto first=sequential.geometry().source_samples/2;
        std::array<Complex,59> a{},b{};sequential.render(first,a);
        random.render(first+23,std::span(b).subspan(23));random.render(first,std::span(b).first(23));
        check(a==b,"pattern revision loses long-coordinate chunk invariance");
        check(sequential.sample_at(first+.375L)==random.sample_at(first+.375L),
              "pattern revision loses fractional random-access invariance");
        auto short_f=f;short_f.bit_seconds=16;PatternSource short_source(short_f,key,1);
        check(short_source.working_bytes()==sequential.working_bytes(),
              "pattern revision allocates in proportion to bit duration");
    }
}

void shared_geometry_bank_checks() {
    auto key=fixture_key();
    std::vector<Format> formats(6);
    formats[0].sample_rate=6000;formats[0].bit_seconds=11.6;
    formats[0].pulse_shape=PulseShape::TaperedRrc;
    formats[1].inner_rate_hz=360;formats[1].added_symbols=1;formats[1].sample_rate=6000;
    formats[1].bit_seconds=8;formats[1].fhss=true;formats[1].fhss_center_aligned=true;
    formats[1].quiet_seconds=.055;formats[1].ramp_seconds=.006;
    formats[2].inner_rate_hz=359.3;formats[2].added_symbols=1;formats[2].sample_rate=6000;
    formats[2].bit_seconds=17;
    formats[3].bit_seconds=40;formats[3].burst_wait_tiles=1;
    formats[4].inner_rate_hz=.01;formats[4].added_symbols=1;
    formats[4].sample_rate=1;formats[4].sample_rate_denominator=100;formats[4].bit_seconds=100000;
    formats[5].bit_seconds=38*86400.;
    for(const auto& format:formats){
        std::vector<PatternSource> bank;bank.reserve(4);
        std::vector<PatternSource> references;references.reserve(4);
        std::vector<PatternSource*> pointers;
        for(unsigned row=0;row<4;++row){
            auto f=format;f.epoch+=row;f.symbol_ordinal+=13*row;
            f.added_interleave=(row&1)==0;f.encrypt=(row&2)==0;
            bank.emplace_back(f,key,static_cast<std::uint8_t>(row%2));
            references.emplace_back(f,key,static_cast<std::uint8_t>(row%2));
            pointers.push_back(&bank.back());
        }
        const auto& g=bank.front().geometry();
        const auto fs=effective_sample_rate(format);
        for(const auto first:{-3.L,static_cast<long double>(g.calendar_origin_sample)+.375L,
                static_cast<long double>(g.source_samples)/2+.25L,
                static_cast<long double>(g.source_samples)-5.L,
                static_cast<long double>(g.calendar_origin_sample)+1.5L*g.tile_seconds*fs}){
            constexpr std::size_t rows=513;
            std::vector<Complex> actual(pointers.size()*rows);
            PatternSource::render_bank(pointers,first,1.0002L,rows,actual);
            for(std::size_t row=0;row<pointers.size();++row)for(std::size_t i=0;i<rows;++i){
                const auto expected=references[row].sample_at(first+i*1.0002L);
                check(actual[row*rows+i]==expected,"shared pulse bank differs from scalar source");
            }
            // Integer-lattice split rendering is bit-identical at internal and
            // caller chunk boundaries. Sparse steps exercise bounded fallback.
            std::vector<Complex> whole(pointers.size()*19),left(pointers.size()*7),right(pointers.size()*12);
            PatternSource::render_bank(pointers,first,1,19,whole);
            PatternSource::render_bank(pointers,first,1,7,left);
            PatternSource::render_bank(pointers,first+7,1,12,right);
            for(std::size_t row=0;row<pointers.size();++row){
                check(std::equal(left.begin()+row*7,left.begin()+(row+1)*7,whole.begin()+row*19)&&
                      std::equal(right.begin()+row*12,right.begin()+(row+1)*12,whole.begin()+row*19+7),
                      "shared pulse bank depends on rendering chunk boundaries");
            }
        }
        std::array<Complex,8> sparse{};
        PatternSource* one=&bank.front();
        const auto step=static_cast<long double>(g.source_samples)/7;
        PatternSource::render_bank(std::span(&one,1),0,step,sparse.size(),sparse);
        for(std::size_t i=0;i<sparse.size();++i)
            check(sparse[i]==references.front().sample_at(i*step),"sparse bank fallback differs");
        const std::array<double,5> offsets{0.,.125,1.25,17.75,53.75};
        std::vector<Complex> mapped(pointers.size()*offsets.size());
        const auto first=g.calendar_origin_sample;
        PatternSource::render_bank(pointers,first,offsets,mapped);
        for(std::size_t row=0;row<pointers.size();++row)
            for(std::size_t i=0;i<offsets.size();++i)
                check(mapped[row*offsets.size()+i]==references[row].sample_at(
                    static_cast<long double>(first)+offsets[i]),"mapped bank differs from scalar source");
    }
    Format f;PatternSource a(f,key,0);f.ramp_seconds=.03;PatternSource b(f,key,1);
    std::array<PatternSource*,2> mismatch{&a,&b};std::array<Complex,2> output{};
    bool rejected=false;try{PatternSource::render_bank(mismatch,0,1,1,output);}catch(const std::exception&){rejected=true;}
    check(rejected,"shared pulse bank accepted different geometry");
    for(long double step:{0.L,-1.L,std::numeric_limits<long double>::infinity()}){
        rejected=false;try{PatternSource::render_bank(mismatch,0,step,1,output);}catch(const std::exception&){rejected=true;}
        check(rejected,"shared pulse bank accepted invalid step");
    }
    check(PatternSource::render_bank_scratch_bytes<512*1024,"shared pulse bank scratch is not bounded");
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
    // Exercise source boundaries with fixed short windows. Never render a
    // complete bit merely to establish local random-access/chunk equivalence.
    const auto& shape=whole.geometry();
    const auto tile_boundary=shape.calendar_origin_sample+
        static_cast<std::uint64_t>(shape.tile_seconds*f.sample_rate);
    for(const auto first:{std::uint64_t{0},shape.calendar_origin_sample+100,
                          tile_boundary-100,shape.source_samples-128}){
        std::array<Complex,257> expected{},actual{};
        whole.render(first,expected);
        split.render(first,std::span(actual).first(101));
        split.render(first+101,std::span(actual).subspan(101));
        check(actual==expected,"source sequence changes across local rendering chunks");
    }
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
    check(std::abs(burst.sample_at(bh+.5*bg.tile_seconds*f.sample_rate))>0,
          "active Symbol Burst tile did not apply its within-tile window");
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

void center_aligned_fhss_checks() {
    auto key=fixture_key();Format f;f.inner_rate_hz=360;f.added_symbols=1;
    f.sample_rate=960;f.bit_seconds=8;f.fhss=true;f.quiet_seconds=.055;
    f.ramp_seconds=2./3600.;f.margin_seconds=.005;
    const auto rejects=[](const Format& format){
        try{(void)geometry(format);return false;}catch(const std::exception&){return true;}};
    check(rejects(f),"legacy half-cell FHSS unexpectedly accepted the narrow aperture");
    f.fhss_center_aligned=true;
    const auto g=geometry(f);
    check(g.dwells==66&&g.fine_cells==66*24&&g.inner_coefficients==64&&
          g.payload_fine_cells_per_tile==64&&std::abs(g.bit_seconds-26.4)<1e-12,
          "center-aligned FHSS did not retain 64 inner cells in 66 dwells");
    auto invalid=f;invalid.fhss=false;
    check(rejects(invalid),"center alignment was accepted without FHSS");
    invalid=f;invalid.inner_rate_hz=359.3;
    check(rejects(invalid),"center alignment was accepted on a noncommensurate dwell grid");
    invalid=f;invalid.quiet_seconds=.062;
    check(rejects(invalid),"center alignment admitted a center outside protected support");

    // One protected center per interior dwell. Its bit-dependent difference
    // must be exactly a full, uncut eight-chip pulse on every carrier.
    for(bool interleave:{false,true})for(unsigned added:{1U,3U}){
        f.added_symbols=added;f.inner_rate_hz=360./added;f.added_interleave=interleave;
        PatternSource zero(f,key,0),one(f,key,1);const auto& shape=zero.geometry();
        check(shape.inner_coefficients==64&&shape.fine_coefficients==64*added&&
              shape.dwells==64*added+2,"interleave split the aligned inner-cell allocation");
        // Fs/fine=16: specify support endpoints as exact integer samples.
        const auto at=[&](PatternSource& source,long double chips){
            return source.carrier_samples_at(shape.calendar_origin_sample+576+chips*16);};
        const auto a=at(zero,0),b=at(one,0);
        double difference=0;for(unsigned c=0;c<3;++c)difference+=std::norm(a[c]-b[c]);
        check(difference>1e-8,"aligned protected center carries no bit information");
        for(long double chips:{-8.00001L,-8.L,-7.5L,-4.25L,0.L,4.25L,7.5L,8.L,8.00001L}){
            const auto x=at(zero,chips),y=at(one,chips);
            const auto ratio=datapump::triad::v3::rrc_pulse(static_cast<double>(chips))/datapump::triad::v3::rrc_pulse(0);
            for(unsigned c=0;c<3;++c)
                check(std::abs((x[c]-y[c])-(a[c]-b[c])*ratio)<1e-11,
                      "aligned fine cell lost or shifted its complete pulse support");
        }
        for(long double within:{.01L,.054L,.0549L,.399L})
            check(zero.sample_at(shape.calendar_origin_sample+(.4L+within)*f.sample_rate)==Complex{},
                  "center-aligned source leaked into the FHSS quiet interval");

        // Probe both sides of each tile transition, with and without a wait,
        // through sequential chunks and deliberately reordered random access.
        for(unsigned waits:{0U,1U}){
            f.bit_seconds=2*shape.tile_seconds;f.burst_wait_tiles=waits;
            PatternSource whole(f,key,1),split(f,key,1),random(f,key,1);
            for(unsigned transition=1;transition<=waits+1;++transition){
                const auto first=whole.geometry().calendar_origin_sample+
                    static_cast<std::uint64_t>(std::llround(transition*shape.tile_seconds*f.sample_rate))-211;
                std::vector<Complex> expected(503),actual(503);whole.render(first,expected);
                for(std::size_t offset=0;offset<actual.size();){
                    const auto count=std::min<std::size_t>(37,actual.size()-offset);
                    split.render(first+offset,std::span(actual).subspan(offset,count));offset+=count;}
                check(actual==expected,"aligned samples changed across rendering chunks or tile boundaries");
                for(std::size_t i=0;i<expected.size();++i){const auto index=(i*97)%expected.size();
                    check(random.sample_at(first+index)==expected[index],"aligned random-access sample differs");}
            }
            check(whole.sample_at(-1)==Complex{}&&whole.sample_at(whole.geometry().source_samples)==Complex{},
                  "aligned source escaped its finite sample support");
        }
        f.bit_seconds=8;f.burst_wait_tiles=0;
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

// Independent copy of the old linear calendar calculation, restricted to
// modest fixtures. It fixes compatibility of the first feasible dwell and
// protected coefficient count, rather than copying the new search strategy.
std::uint64_t legacy_protected_cells(const Format& f,std::uint64_t dwells) {
    const double fine=f.inner_rate_hz/6*f.added_symbols;
    const long double edge=8/fine+std::max(f.ramp_seconds,8/fine)+f.margin_seconds;
    if(!f.fhss){
        const auto width=dwells*static_cast<long double>(f.dwell_seconds)-2*edge;
        return width>0?static_cast<std::uint64_t>(std::floor(width*fine)):0;
    }
    const long double guard=f.quiet_seconds+f.ramp_seconds+f.margin_seconds+8/fine;
    const auto cells=std::floor((static_cast<long double>(f.dwell_seconds)-2*guard)*fine);
    check(cells>=1,"invalid legacy FHSS fixture");
    return (dwells-2)*static_cast<std::uint64_t>(cells);
}

std::uint64_t legacy_tile_dwells(const Format& f) {
    const double fine=f.inner_rate_hz/6*f.added_symbols;
    const long double edge=8/fine+std::max(f.ramp_seconds,8/fine)+f.margin_seconds;
    const long double guard=f.quiet_seconds+f.ramp_seconds+f.margin_seconds+8/fine;
    const auto per_dwell=std::floor((f.fhss?f.dwell_seconds-2*guard:f.dwell_seconds)*fine);
    std::uint64_t dwells=20;
    if(per_dwell>=1){
        dwells+=f.fhss?2:2*static_cast<std::uint64_t>(std::ceil(edge/f.dwell_seconds));
        dwells+=static_cast<std::uint64_t>(std::ceil((f.added_symbols-1)/per_dwell));
    }
    while(legacy_protected_cells(f,dwells)<f.minimum_inner_coefficients*f.added_symbols){
        check(dwells<100000,"legacy comparison fixture exceeds its bounded test budget");
        ++dwells;
    }
    return dwells;
}

void tile_search_checks() {
    const auto compare_legacy=[](const Format& f){
        const auto dwells=legacy_tile_dwells(f);const auto g=geometry(f);
        check(g.tile_seconds==static_cast<double>(dwells)*f.dwell_seconds,
              "binary tile search changed a legacy calendar");
        check(g.payload_fine_cells_per_tile==legacy_protected_cells(f,dwells)/f.added_symbols*f.added_symbols,
              "binary tile search changed legacy payload coefficients");
        const auto cells=static_cast<std::uint64_t>(std::ceil(g.tile_seconds*g.fine_chip_rate_hz+3));
        check(g.source_workspace_bytes==cells*176+128*1024,
              "binary tile search changed legacy workspace geometry");
    };
    for(double rate:{.036,.36,3.6,35.9,36.,359.3,360.})for(unsigned added:{1U,3U,10U}) {
        Format f;f.inner_rate_hz=rate;f.added_symbols=added;f.sample_rate=9600;
        compare_legacy(f);
        f.minimum_inner_coefficients=97;f.ramp_seconds=.013;f.margin_seconds=.006;
        compare_legacy(f);
    }
    for(double rate:{359.3,360.,3600.})for(unsigned added:{1U,3U,10U}) {
        Format f;f.inner_rate_hz=rate;f.added_symbols=added;f.sample_rate=96000;
        f.fhss=true;f.quiet_seconds=.01;f.ramp_seconds=.002;
        compare_legacy(f);
    }
    Format f;f.inner_rate_hz=.000036;f.added_symbols=1;f.sample_rate=1;f.bit_seconds=38*86400.;
    const auto g=geometry(f);
    const auto dwells=static_cast<std::uint64_t>(std::llround(g.tile_seconds/f.dwell_seconds));
    check(dwells>10000000&&g.tile_seconds>185*86400.&&g.tile_seconds<186*86400.,
          "sub-millihertz pattern still has the old dwell-duration ceiling");
    check(g.tile_count==1&&g.inner_coefficients>=64&&g.source_workspace_bytes<1024*1024,
          "multi-month minimum pattern lost complexity or bounded workspace");
    check(legacy_protected_cells(f,dwells)>=64&&legacy_protected_cells(f,dwells-1)<64,
          "multi-month search did not select the first feasible dwell");
    f.bit_seconds=2*g.tile_seconds;
    check(geometry(f).tile_count==2,"multi-month exact tile boundary rounded upward");
    f.bit_seconds=(2.+1e-6)*g.tile_seconds;
    check(geometry(f).tile_count==3,"multi-month fractional tile boundary rounded downward");
    f.inner_rate_hz=.000000036;f.bit_seconds=8;
    const auto centuries=geometry(f);
    check(centuries.dwells>std::numeric_limits<std::uint32_t>::max()&&
          centuries.source_samples>std::numeric_limits<std::uint32_t>::max()&&
          centuries.source_workspace_bytes==g.source_workspace_bytes,
          "very-low-rate calendar lost 64-bit coordinates or bounded workspace");
    f.inner_rate_hz=.036;f.minimum_inner_coefficients=1000000;
    bool rejected=false;try{(void)geometry(f);}catch(const std::exception&){rejected=true;}
    check(rejected,"search bypassed the bounded-cell workspace cap");
    f.minimum_inner_coefficients=64;
    for(double invalid:{std::numeric_limits<double>::min(),std::numeric_limits<double>::denorm_min()}) {
        f.inner_rate_hz=invalid;rejected=false;
        try{(void)geometry(f);}catch(const std::exception&){rejected=true;}
        check(rejected,"unrepresentable tile duration bypassed checked coordinates");
    }
}

void month_and_low_sample_rate_checks() {
    auto key=fixture_key();
    for(double rate:{3.6,.36,.036,.0036}) {
        Format f;f.inner_rate_hz=rate;f.added_symbols=1;
        f.sample_rate=rate>1?4:1;f.bit_seconds=38*86400.;
        PatternSource low(f,key,1);const auto& g=low.geometry();
        check(g.bit_seconds>=38*86400.&&g.dwells>=20&&g.inner_coefficients>=64,
              "month-long low-rate source lost duration or complexity");
        check(low.working_bytes()<1024*1024,"low-rate source exceeds bounded tile workspace");
        f.sample_rate=64;PatternSource reference(f,key,1);
        check(reference.geometry().tile_count==g.tile_count&&
              reference.geometry().fine_coefficients==g.fine_coefficients,
              "sample rate changed private pattern geometry");
        for(long double fraction:{.1L,.5L,.9L}) {
            const auto seconds=fraction*g.bit_seconds;
            const auto a=low.sample_at(g.calendar_origin_sample+seconds*(rate>1?4:1));
            const auto b=reference.sample_at(reference.geometry().calendar_origin_sample+seconds*64);
            check(std::isfinite(a.real())&&std::isfinite(a.imag())&&std::abs(a-b)<1e-8,
                  "low-rate source differs at matching physical timestamps");
        }
        f.bit_seconds=8;PatternSource short_source(f,key,1);
        check(short_source.working_bytes()==low.working_bytes(),"month duration grew coefficient workspace");
    }
    Format f;f.inner_rate_hz=36;f.added_symbols=1;f.sample_rate=9600;f.bit_seconds=38*86400.;
    PatternSource month(f,key,0);
    check(month.geometry().source_samples>std::numeric_limits<std::uint32_t>::max(),
          "month source failed to exercise 64-bit sample coordinates");
    const auto at=month.geometry().source_samples-month.geometry().calendar_origin_sample-1000;
    std::array<Complex,37> whole{},split{};
    month.render(at,whole);month.render(at,std::span(split).first(13));
    month.render(at+13,std::span(split).subspan(13));
    check(whole==split,"month-end random access changes across chunk boundaries");
    check(std::all_of(whole.begin(),whole.end(),[](Complex v){return std::isfinite(v.real())&&std::isfinite(v.imag());}),
          "month-end samples are not finite");
    for(unsigned invalid:{0U,1U}) {
        f.sample_rate=invalid;bool rejected=false;
        try{(void)geometry(f);}catch(const std::exception&){rejected=true;}
        check(rejected,"zero or undersampled shaped triplet was accepted");
    }
    f.inner_rate_hz=.036;f.sample_rate=1;f.fhss=true;bool rejected=false;
    try{(void)geometry(f);}catch(const std::exception&){rejected=true;}
    check(rejected,"low-sample source silently enabled unresolved FHSS ramps");
}
} // namespace

int main() {
    try {
        confidentiality_payload_checks();
        independent_patterns_and_apsk();
        revision_random_access_checks();
        shared_geometry_bank_checks();
        geometry_checks();
        random_access_and_chunk_checks();
        protected_waveform_checks();
        center_aligned_fhss_checks();
        complete_tile_duration_checks();
        tile_search_checks();
        month_and_low_sample_rate_checks();
        std::cout << "triad v3 source: geometry, addressing, pulse, and chunk checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
