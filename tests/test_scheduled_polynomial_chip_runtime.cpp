#include "scheduled_polynomial_chip_runtime.hpp"
#include <iostream>
#include <stdexcept>

namespace p=datapump::modem::detail::scheduled_polynomial_chip;
namespace c=datapump::modem::detail::scheduled_cold_colored;
namespace cert=datapump::modem::detail::scheduled_acquisition_certificate;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void models_and_ownership() {
    c::SourceRows rows({0,.2L,0,0,0},0,136);
    for(std::size_t i=0;i<136;++i) {
        const bool selected=!(i>=102 && (i<107 || i>=128));
        const auto n=static_cast<double>(i%34);
        const std::array<std::complex<double>,2> q{std::complex<double>{.2+.003*n,.1-.002*n},
            std::complex<double>{-.1+.001*n,.4-.002*n}};
        // The gap cuts owner1 into TWO physical fragments. It must stay one
        // source cell and use Direct, never two separately projected pieces.
        rows.append(1000+i+(i>=51?100:0),q,i+.2L,1,selected?1:0);
    }
    rows.seal();
    for(std::size_t i=0;i<rows.rows.size();++i)
        rows.geometry[i]={rows.mask[i]?i/34:c::no_owner,0,1e-10};
    rows.geometry_ready=true;rows.geometry_delta=1e-5L;
    p::SourceModels models(rows,136);
    check(models.matches(rows),"fresh models do not match source");
    check(models.cells.size()==4,"source owner cell count changed");
    check(models.cells[0].full && !models.cells[1].full && models.cells[2].full && !models.cells[3].full,
        "partial/multifragment/masked owners entered full-cell path");
    check(models.cells[1].first==34 && models.cells[1].end==68,"one owner split before projection");
    check(models.cells[3].first==107 && models.cells[3].end==128,"masked rows invented observations");
    check(models.working_bytes()<=p::SourceModels::reservation(136),"model charge exceeds preflight");
    auto errors=p::original_errors(models.cells[0],100,1e-6,2,512);
    for(const auto& e:errors)check(e.valid && e.dot>0 && e.gram>0 && e.energy>0,"Original error component missing");
    rows.geometry_delta=2e-5L;
    check(!models.matches(rows),"increased source displacement retained smaller error bound");
    std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{p::SourceModels rejected(rows,136,stop.get_token());}catch(const std::exception&){cancelled=true;}
    check(cancelled,"cancelled model preparation published result");
    rows.geometry[34].membership=c::ProxyRow::transfer;bool transfer=false;
    try{p::SourceModels rejected(rows,136);}catch(const std::exception&){transfer=true;}
    check(transfer,"generic transfer geometry entered per-owner split producer");
}
static void field_and_ordered_summary() {
    c::SourceRows rows({0,.2L,0,0,0},0,68);
    std::array<float,68> pcm{};std::array<std::complex<double>,68> oscillator{};
    for(std::size_t i=0;i<68;++i) {
        const auto n=static_cast<double>(i%34);
        const std::array<std::complex<double>,2> q{std::complex<double>{.4+.002*n,.3-.001*n},
            std::complex<double>{-.2+.003*n,.1+.002*n}};
        rows.append(100+i,q,i+.2L,1,1);
        pcm[i]=static_cast<float>(std::sin(.2*i)+.2*std::cos(.4*i));oscillator[i]=std::polar(1.,.731*i);
    }
    rows.seal();for(std::size_t i=0;i<68;++i)rows.geometry[i]={i/34,0,1e-12};
    rows.geometry_ready=true;rows.geometry_delta=1e-5L;p::SourceModels models(rows,68);
    std::array<c::Summary,2> cumulative;for(auto& s:cumulative)s.owner_set_certified=true;
    for(const auto& cell:models.cells) {
        const auto moments=p::build_moments(std::span<const float>(pcm).subspan(cell.first,34),
            std::span<const std::complex<double>>(oscillator).subspan(cell.first,34));
        const auto result=p::contract(cell.model,moments,p::original_errors(cell,1000,1e-6,2,512));
        check(result.supported,"polynomial Original fields unexpectedly unsupported");
        for(unsigned bit=0;bit<2;++bit) {
            datapump::modem::detail::CorrelationFit direct;
            for(auto i=cell.first;i<cell.end;++i) {
                const auto z=rows.rows[i][bit]*oscillator[i];const double x=pcm[i];
                direct.xc+=x*z.real();direct.xs+=x*z.imag();direct.cc+=z.real()*z.real();
                direct.ss+=z.imag()*z.imag();direct.cs+=z.real()*z.imag();direct.energy+=x*x;++direct.count;
            }
            const auto& f=result.intervals[bit];
            for(const auto [interval,value]:std::array<std::pair<cert::Interval,double>,6>{{
                {f.xc,direct.xc},{f.xs,direct.xs},{f.cc,direct.cc},{f.ss,direct.ss},{f.cs,direct.cs},{f.energy,direct.energy}}})
                check(interval.lo<=value && value<=interval.hi,"stored direct field escaped polynomial enclosure");
            c::Summary one;one.owner_set_certified=true;one.retain(f);check(one.valid && one.nested,"full-cell projection rank unknown");
            p::join(cumulative[bit],one);
        }
    }
    for(auto& summary:cumulative) {
        check(summary.valid && summary.nested && summary.owner_set_certified && summary.count==68 && summary.rank==4,
            "ordered cell join changed count/rank/ownership");
        c::Summary bad;bad.owner_set_certified=false;p::join(summary,bad);
        check(!summary.valid,"uncertified owner set entered joined result");
    }
}
int main(){try{models_and_ownership();field_and_ordered_summary();std::cout<<"polynomial chip runtime contracts passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
