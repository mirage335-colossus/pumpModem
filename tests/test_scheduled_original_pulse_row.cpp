#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "../src/scheduled_original_pulse_row.hpp"
#include "../src/pattern_correlator_batch.hpp"
#include <bit>
#include <iostream>
#include <vector>

using namespace datapump;
namespace {
using Complex=std::complex<double>;
void check(bool value,const char* message) { if(!value)throw Error(message); }
bool same(double a,double b) { return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b); }
void same(Complex a,Complex b) {
    check(same(a.real(),b.real())&&same(a.imag(),b.imag()),"Original pulse export changed source pair");
}
void same(const modem::detail::CorrelationOuterEvidence& a,const modem::detail::CorrelationOuterEvidence& b) {
    check(a.first==b.first&&a.last==b.last&&a.initialized==b.initialized,"pulse export changed ring cursor");
    for(std::size_t i=0;i<a.pulse.size();++i)same(a.pulse[i],b.pulse[i]);
    for(unsigned bit=0;bit<2;++bit) {
        check(a.dot[bit]==b.dot[bit]&&a.variance[bit]==b.variance[bit]&&
              a.contributing_terms[bit]==b.contributing_terms[bit]&&same(a.score(bit),b.score(bit)),
              "pulse export changed full outer evidence");
    }
}
modem::Config configuration(unsigned fs,double bandwidth,std::uint64_t symbol) {
    modem::Config c;c.sample_rate=fs;c.carrier_hz=1500;c.bandwidth_hz=bandwidth;
    c.integration_seconds=std::nextafter(static_cast<double>(symbol)/fs,0.);
    c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;c.dsss=true;c.dsss_factor=10;
    c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.data_key.emplace(Bytes(32,0x61));
    const auto p=c.data_key->stream(StreamPurpose::Scrambler,0,0,32);
    const auto d=c.data_key->stream(StreamPurpose::Dsss,0,0,32);
    std::copy(p.begin(),p.end(),c.spreading_seed.begin());std::copy(d.begin(),d.end(),c.dsss_seed.begin());
    return c;
}
void geometry_and_source(modem::Config c) {
    modem::PatternCode reference(c,1800000000),candidate(c,1800000000);
    const auto symbol=reference.symbol_samples(),chip=reference.chip_samples(),chips=reference.chips_per_symbol();
    check(modem::pattern_pulse_enabled(c),"pulse-row fixture lost shaped geometry");
    check(symbol==modem::symbol_sample_count(c),"pulse-row fixture symbol changed");
    const auto support=8.*chip;
    std::vector<double> positions{-support,std::nextafter(-support,0.),-1.,-0.,0.,.125,
        .5*chip,8.5*chip,static_cast<double>(symbol)-.5,static_cast<double>(symbol),
        static_cast<double>(symbol)+support,std::nextafter(static_cast<double>(symbol)+support,INFINITY)};
    for(double base:{0.,static_cast<double>(symbol)-2.*chip})
        for(unsigned i=0;i<32;++i)positions.push_back(base+i*chip/16.+.03125);
    for(const auto first:{std::uint64_t{0},chips})for(const auto position:positions) {
        reference.prepare_symbol(first);candidate.prepare_symbol(first);
        const auto expected=reference.shaped_values(first,position);
        const modem::detail::OriginalPulseRow* exported=nullptr;
        const auto actual=candidate.shaped_values(first,position,exported);
        for(unsigned bit=0;bit<2;++bit)same(expected[bit],actual[bit]);
        check(exported!=nullptr,"valid pulse traversal was not exported");
        std::vector<std::pair<std::uint64_t,double>> atoms;
        modem::pattern_pulse_each(position,symbol,chip,[&](auto local,auto weight){atoms.emplace_back(local,weight);});
        std::size_t index=0;
        check(exported->consume(position,symbol,chip,[&](auto local,auto weight){
            check(index<atoms.size()&&atoms[index].first==local&&same(atoms[index].second,weight),
                  "export lost an ordered endpoint/partial pulse");++index;
        }),"matching pulse capability refused");
        check(index==atoms.size(),"export omitted pulse atoms");
        bool touched=false;
        check(!exported->consume(std::nextafter(position,INFINITY),symbol,chip,[&](auto,auto){touched=true;})&&!touched,
              "stale coordinate changed ring before rejection");
        (void)candidate.shaped_values(first,position);
        check(!exported->consume(position,symbol,chip,[&](auto,auto){touched=true;})&&!touched,
              "next shaping call failed to invalidate borrowed row");
    }
    // A full clipped partial-symbol pulse is accumulated in the original row
    // order across two disjoint dwells, with nonzero seed state and both bits.
    modem::detail::CorrelationOuterEvidence old_outer,new_outer,bulk_outer;
    std::size_t bulk_rows=0;
    const auto coefficients=[&](std::uint64_t index){return reference.values(index);};
    const Complex seed{.25,-.125};old_outer.add(0,seed,coefficients);new_outer.add(0,seed,coefficients);bulk_outer.add(0,seed,coefficients);
    for(std::uint64_t n=0;n<symbol;++n) {
        if(n>symbol/3&&n<symbol/2)continue;
        const auto at=static_cast<double>(n)+.125;
        const auto gain=n%13==0?0.:.875;
        const Complex projection{std::sin(.113*n),std::cos(.071*n)};
        const modem::detail::OriginalPulseRow* row=nullptr;
        const auto q=candidate.shaped_values(0,at,row);
        for(unsigned bit=0;bit<2;++bit)same(modem::pattern_limit_pcm(8.*q[bit]),
            modem::pattern_limit_pcm(8.*reference.shaped_values(0,at)[bit]));
        const auto consume_old=[&](auto local,auto pulse){old_outer.add(local,gain*pulse*projection,coefficients);};
        const auto consume_new=[&](auto local,auto pulse){new_outer.add(local,gain*pulse*projection,coefficients);};
        modem::pattern_pulse_each(at,symbol,chip,consume_old);
        check(row&&row->consume(at,symbol,chip,consume_new),"full outer capability unavailable");
        if(row->accumulate_outer(at,symbol,chip,0,bulk_outer,gain,projection,coefficients))++bulk_rows;
        else check(row->consume(at,symbol,chip,[&](auto i,auto w){bulk_outer.add(i,(gain*w)*projection,coefficients);}),
                   "wide valid pulse row lost original scalar fallback");
        if(n%257==0){same(old_outer,new_outer);same(old_outer,bulk_outer);}
    }
    check(bulk_rows>0,"bulk outer fixture executed no row");
    old_outer.finish(coefficients);new_outer.finish(coefficients);bulk_outer.finish(coefficients);
    same(old_outer,new_outer);same(old_outer,bulk_outer);
    // Deliberately sparse row: advance by index difference, including a wrap.
    modem::detail::OriginalPulseRow sparse;sparse.begin(.25,symbol,chip);
    sparse.append(10,.5);sparse.append(14,-.25);sparse.append(26,.125);sparse.finish();
    modem::detail::CorrelationOuterEvidence scalar,bulk;
    scalar.add(0,seed,coefficients);bulk.add(0,seed,coefficients);
    sparse.consume(.25,symbol,chip,[&](auto i,auto w){scalar.add(i,(.875*w)*seed,coefficients);});
    check(sparse.accumulate_outer(.25,symbol,chip,0,bulk,.875,seed,coefficients),"sparse bulk row refused");
    same(scalar,bulk);scalar.finish(coefficients);bulk.finish(coefficients);same(scalar,bulk);
    const auto saved=bulk;
    const auto reject=[&](const modem::detail::OriginalPulseRow& bad) {
        check(!bad.accumulate_outer(.25,symbol,chip,0,bulk,.875,seed,coefficients),"invalid bulk row accepted");
        same(saved,bulk);
    };
    auto bad=sparse;bad.atoms[1].local=10;reject(bad);
    bad=sparse;bad.atoms[2].local=27;reject(bad);
    bad=sparse;bad.atoms[1].weight=INFINITY;reject(bad);
    bad=sparse;bad.argument_bits=0;reject(bad);
    reject(sparse); // A finished ring has already flushed these local indices.

}
void incomplete_and_unshaped() {
    modem::detail::OriginalPulseRow row;row.begin(0,5120,8);
    for(std::size_t i=0;i<=row.capacity;++i)row.append(i,1);
    row.finish();bool touched=false;
    check(!row.consume(0,5120,8,[&](auto,auto){touched=true;})&&!touched,
          "overflow exported a partial outer traversal");
    auto c=configuration(14400,360,5120);c.pulse_shaping=false;c.carrier_hz=3600;
    modem::PatternCode code(c,1800000000);
    const modem::detail::OriginalPulseRow* exported=&row;
    const auto old=code.shaped_values(0,.25),actual=code.shaped_values(0,.25,exported);
    check(!exported,"unshaped path exported pulse geometry");
    for(unsigned bit=0;bit<2;++bit)same(old[bit],actual[bit]);
}
}
int main() {
    try {
        geometry_and_source(configuration(14400,360,5120));
        geometry_and_source(configuration(14400,360,5123));
        geometry_and_source(configuration(6120,36,87040));
        geometry_and_source(configuration(6120,36,87057));
        incomplete_and_unshaped();
        std::cout<<"PASS Original pulse/source/outer exact reuse and safe fallback\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
