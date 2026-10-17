#include "scheduled_owned_moment_correction.hpp"
#include "scheduled_polynomial_chip_runtime.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace d=datapump::modem::detail;
namespace o=d::scheduled_owned_moment_correction;
namespace p=d::scheduled_polynomial_chip;
namespace c=d::scheduled_acquisition_certificate;
namespace g=d::scheduled_cold_colored;
using Complex=std::complex<double>;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
void contains(c::Interval i,double x,const char* m){check(i.bounded() && i.lo<=x && x<=i.hi,m);}
o::Identity identity(std::uint64_t support=2){return {1,support,3,4,5,6};}
o::FinalErrors errors(o::Identity id,std::uint64_t owner,std::uint64_t count,double e=0) {
    return {id,owner,count,{{{e,e,e,0,true},{e,e,e,0,true}}},{},true};
}
struct Fixture {
    std::array<o::RowSample,69> rows;
    Fixture(){for(std::size_t n=0;n<rows.size();++n){
        auto& r=rows[n];r.pcm=static_cast<float>(.4*std::sin(.27*n)+.15*std::cos(.77*n));
        r.oscillator=std::polar(.8+.001*n,.71*n+(n/17)*.13);
        r.q0={Complex{.1+.003*n,.2-.004*n},Complex{-.15+.002*n,.07+.001*n}};
    }}
    o::NativeCell native(std::uint64_t owner,o::Identity id)const {
        std::array<float,34> pcm;std::array<Complex,34> carrier;std::array<std::array<Complex,2>,34> q;
        for(std::size_t n=0;n<34;++n){const auto& r=rows[owner*34+n];pcm[n]=r.pcm;carrier[n]=r.oscillator;q[n]=r.q0;}
        const auto math=p::contract_math(p::build_source(q),p::build_moments(pcm,carrier));
        check(math.supported,"mathematical native contraction unsupported");return {id,owner,math.intervals};
    }
    o::RowEdit edit(std::uint64_t physical,std::uint64_t native,std::uint64_t original,o::Identity id)const {
        return {id,physical,native,original,rows[physical]};
    }
    // Original independent prefix ledger, restarted every17 observations. Each
    // row uses the rounded difference of independently accumulated prefixes,
    // then the exact production CorrelationFit::add association.
    std::array<d::CorrelationFit,2> original(const std::array<std::uint64_t,69>& owners,std::uint64_t owner,bool whole=false)const {
        std::array<d::CorrelationFit,2> fit;
        for(std::size_t first=0;first<rows.size();first+=17){
            d::CorrelationProjection prefix;
            for(std::size_t n=first;n<std::min(rows.size(),first+17);++n){
                const auto& r=rows[n];const auto x=static_cast<double>(r.pcm),a=r.oscillator.real(),b=r.oscillator.imag();
                const auto previous=prefix;
                prefix.xc+=x*a;prefix.xs+=x*b;prefix.cc+=a*a;prefix.ss+=b*b;prefix.cs+=a*b;prefix.energy+=x*x;
                if(owners[n]==o::no_owner || (!whole && owners[n]!=owner))continue;
                const auto projection=prefix-previous;
                for(unsigned bit=0;bit<2;++bit)fit[bit].add(projection,r.q0[bit],1);
            }
        }
        return fit;
    }
};
std::array<std::uint64_t,69> native_owners(){std::array<std::uint64_t,69> r;for(std::size_t n=0;n<69;++n)r[n]=n<68?n/34:o::no_owner;return r;}
void fields(const g::FitIntervals& f,const d::CorrelationFit& v){
    check(g::finite(f) && f.count==v.count && f.count_lower==v.count && f.count_upper==v.count,"incorrect exact count");
    contains(f.xc,v.xc,"xc escaped correction");contains(f.xs,v.xs,"xs escaped correction");
    contains(f.cc,v.cc,"cc escaped correction");contains(f.ss,v.ss,"ss escaped correction");
    contains(f.cs,v.cs,"cs escaped correction");contains(f.energy,v.energy,"energy escaped correction");
}
void transfers_masks_halos_and_whole(){
    Fixture f;const auto id=identity();auto actual=native_owners();
    const std::array edits{f.edit(0,0,o::no_owner,id),f.edit(33,0,1,id),f.edit(34,1,0,id),
        f.edit(67,1,o::no_owner,id),f.edit(68,o::no_owner,2,id)};
    for(const auto& e:edits)actual[e.physical]=e.original_owner;
    const std::array<o::OwnerCount,3> owners{{{0,34,33},{1,34,33},{2,0,1}}};
    const o::SupportProof proof{id,68,67,true,true};std::array<o::OwnerDelta,3> delta;
    check(o::build_owner_deltas(proof,owners,edits,delta),"valid owner deltas rejected");
    std::array<o::MathCell,3> math;
    g::Summary summary;summary.owner_set_certified=true;
    for(std::size_t owner=0;owner<owners.size();++owner){
        const auto base=owner<2?f.native(owner,id):o::empty_native(id,owner);
        math[owner]=o::corrected_math_cell(base,delta[owner]);check(math[owner].valid,"corrected math cell invalid");
        const auto result=o::originalize(math[owner],errors(id,owner,owners[owner].original_count,1e-10));
        check(result.valid,"corrected Original cell invalid");const auto expected=f.original(actual,owner);
        for(unsigned bit=0;bit<2;++bit){fields(result.fields[bit],expected[bit]);
            const auto projection=g::projection(result.fields[bit]);const auto original=d::CorrelationChipEvidence::projection(expected[bit]);
            check(projection.valid && projection.nested && projection.rank==original.second,"Original rank/nesting mismatch");
            contains(projection.energy,original.first,"projected energy escaped correction");
        }
        summary.retain(result.fields[0]);
    }
    check(summary.valid && summary.nested && summary.rank==5 && summary.count==67,"new singleton omitted from summary");
    auto whole_proof=proof;whole_proof.identity=identity(7);
    const auto whole_math=o::combine_math_whole(whole_proof,math);check(whole_math.valid,"independent whole compose invalid");
    const auto whole=o::originalize(whole_math,errors(whole_proof.identity,o::no_owner,67,1e-9));
    check(whole.valid,"whole Original association invalid");const auto expected=f.original(actual,0,true);
    for(unsigned bit=0;bit<2;++bit)fields(whole.fields[bit],expected[bit]);
    // Independent native whole support. Transfer rows count twice in cells,
    // but contribute no whole edit; the lost/added x² must affect BOTH limits.
    std::array<o::MathCell,2> native_math;
    for(unsigned owner=0;owner<2;++owner){auto base=f.native(owner,whole_proof.identity);native_math[owner]={base.identity,owner,base.fields,true};}
    const o::SupportProof native_proof{whole_proof.identity,68,68,true,true};
    const auto native_whole=o::combine_math_whole(native_proof,native_math);check(native_whole.valid,"native whole compose invalid");
    auto whole_edits=edits;for(auto& e:whole_edits)e.identity=whole_proof.identity;
    o::WholeDelta whole_delta;check(o::build_whole_delta(whole_proof,whole_edits,whole_delta),"whole support edit rejected");
    check(whole_delta.removed==2 && whole_delta.added==1,"owner transfer changed whole mask");
    const o::NativeCell native_whole_cell{native_whole.identity,o::no_owner,native_whole.fields};
    const auto directly_corrected=o::corrected_whole(native_whole_cell,whole_delta,errors(whole_proof.identity,o::no_owner,67,1e-9));
    check(directly_corrected.valid,"whole sparse correction invalid");for(unsigned bit=0;bit<2;++bit)fields(directly_corrected.fields[bit],expected[bit]);
}
void thirty_four_to_thirty_five_and_bad_proofs(){
    Fixture f;const auto id=identity();const std::array edits{f.edit(34,1,0,id)};
    const std::array<o::OwnerCount,2> owners{{{0,34,35},{1,34,33}}};
    const o::SupportProof proof{id,68,68,true,true};std::array<o::OwnerDelta,2> deltas;
    check(o::build_owner_deltas(proof,owners,edits,deltas),"34 to35 transfer rejected");
    auto actual=native_owners();actual[34]=0;
    const auto result=o::corrected_cell(f.native(0,id),deltas[0],errors(id,0,35,1e-10));
    check(result.valid,"35-row Original allowance invalid");fields(result.fields[0],f.original(actual,0)[0]);
    auto bad_native=f.native(0,id);bad_native.identity.ledger++;
    check(!o::corrected_cell(bad_native,deltas[0],errors(id,0,35)).valid,"wrong ledger admitted");
    auto bad_errors=errors(id,0,34);check(!o::corrected_cell(f.native(0,id),deltas[0],bad_errors).valid,"wrong final count admitted");
    auto wrong_rows=edits;wrong_rows[0].identity.source_template++;
    check(!o::build_owner_deltas(proof,owners,wrong_rows,deltas),"foreign template edit admitted");
    const std::array duplicate{edits[0],edits[0]};
    check(!o::build_owner_deltas(proof,owners,duplicate,deltas),"duplicate physical row admitted");
    auto wrong_counts=owners;wrong_counts[0].original_count=36;wrong_counts[1].original_count=32;
    check(!o::build_owner_deltas(proof,wrong_counts,edits,deltas),"incorrect exact partition counts admitted");
    auto incomplete=proof;incomplete.complete_owners=false;
    check(!o::build_owner_deltas(incomplete,owners,edits,deltas),"incomplete owners admitted");
    check(!o::count_changed(o::maximum_count,0,1,o::maximum_count),"count overflow admitted");
    check(!o::count_changed(0,1,0,0),"count underflow admitted");
}
void zero_new_owner_departure_and_overflow(){
    Fixture f;const auto id=identity();auto row=f.edit(68,o::no_owner,2,id);
    row.row.original_q_radius={1e-6,1e-6};const std::array edits{row};
    const std::array<o::OwnerCount,1> owners{{{2,0,1}}};std::array<o::OwnerDelta,1> delta;
    const o::SupportProof proof{id,0,1,true,true};check(o::build_owner_deltas(proof,owners,edits,delta),"new owner rejected");
    auto base=o::empty_native(id,2);for(auto& bit:base.fields)bit.zero_basis=true;
    auto final=errors(id,2,1);final.fields=o::row_template_errors(row.row);
    const auto result=o::corrected_cell(base,delta[0],final);
    check(result.valid && !result.fields[0].zero_basis && result.fields[0].energy.hi>0,"new uncertain owner retained zero basis");
    auto unsafe=edits;unsafe[0].row.q0[0]={std::numeric_limits<double>::max(),0};unsafe[0].row.oscillator={2,0};
    check(!o::build_owner_deltas(proof,owners,unsafe,delta),"overflow row admitted");
    std::array<o::Fields,2> rf;check(o::row_fields(row.row,rf),"row math invalid");
    o::NativeCell singleton{id,2,{}};
    for(unsigned bit=0;bit<2;++bit){const auto& v=rf[bit];singleton.fields[bit]={v.xc,v.xs,v.cc,v.ss,v.cs,v.energy,1,1,1,true,false};}
    auto exit=row;exit.native_owner=2;exit.original_owner=o::no_owner;
    const std::array exits{exit};const std::array<o::OwnerCount,1> exit_owner{{{2,1,0}}};
    const o::SupportProof exit_proof{id,1,0,true,true};
    check(o::build_owner_deltas(exit_proof,exit_owner,exits,delta),"departed owner rejected");
    const auto empty=o::corrected_cell(singleton,delta[0],errors(id,2,0));
    check(empty.valid && o::empty_fields(empty.fields[0]) && g::projection(empty.fields[0]).nested,"departed owner not exact empty");
}
void native_model_identity_and_spans(){
    g::SourceRows source({0,0,1,0,0,1.0000000003L},100,74);
    for(std::uint64_t n=0;n<70;++n){
        const bool selected=n!=10 && n!=35;
        source.append(n<36?100+n:105+n,{{{.1,.2},{-.1,.05}}},static_cast<long double>(n)-1,1,selected);
    }
    source.seal();source.geometry[3]={999,1,.25};
    const auto geometry_before=source.geometry;
    p::SourceModels model(source,68,p::SourceModels::NativeTemplateTag{});
    check(model.matches_native(source) && !model.matches(source),"native model mistaken for Original geometry proof");
    check(source.geometry[3].owner==geometry_before[3].owner && !source.geometry_ready,"native grouping mutated Original geometry");
    std::uint64_t count=0;for(const auto& span:model.native_spans){
        check(span.physical_end>span.physical_first && span.owner<2,"bad native span");count+=span.physical_end-span.physical_first;
        for(std::size_t row=span.row_first;row<span.row_first+span.physical_end-span.physical_first;++row)
            check(source.mask[row] && source.source[row]>=0 && source.source[row]<68 && static_cast<std::uint64_t>(source.source[row]/34)==span.owner,"native span crossed mask/window/owner");
    }
    check(count==66 && count==model.native_selected_count && model.working_bytes()<=p::SourceModels::reservation(68),"native spans count/budget incorrect");
    source.publish_geometry(g::SourceRows::GeometryMode::integral_original);
    check(model.matches_native(source) && !model.matches(source),"mutable Original geometry invalidated native model");
    source.invalidate_geometry();
    check(model.matches_native(source) && !model.matches(source),"Original invalidation invalidated native template");
    source.key.rate=1;check(!model.matches_native(source),"source-rate key ignored");
    g::SourceRows exact({0,0,1,0,0,1},200,34);
    for(std::uint64_t n=0;n<34;++n)exact.append(200+n,{{{.1,.2},{-.1,.05}}},n,1,1);
    exact.seal();p::SourceModels native_full(exact,34,p::SourceModels::NativeTemplateTag{});
    check(native_full.cells.size()==1 && native_full.cells[0].full,"native full C34 cell lost");
    const auto forbidden=p::original_errors(native_full.cells[0],1,0,1,512);
    check(!forbidden[0].valid && !forbidden[1].valid,"native template granted strict Original errors");
    const bool prepared=exact.prepare_integral_geometry(34,34,true);
    if(c::coordinate_environment()) {
        check(prepared,"exact geometry fixture failed");
        p::SourceModels strict(exact,34);
        check(strict.matches(exact) && !strict.matches_native(exact),"strict constructor behavior changed");
        exact.invalidate_geometry();
        check(native_full.matches_native(exact) && !native_full.matches(exact) && !strict.matches(exact),"strict/native geometry invalidation mixed");
    } else {
        check(!prepared&&!exact.geometry_ready&&exact.geometry_mode==g::SourceRows::GeometryMode::unspecified,
            "unsupported coordinate arithmetic published exact geometry");
        bool denied=false;try{p::SourceModels strict(exact,34);}catch(const datapump::Error&){denied=true;}
        check(denied,"strict model accepted unavailable Original geometry");
        exact.invalidate_geometry();
        check(native_full.matches_native(exact)&&!native_full.matches(exact),"geometry refusal invalidated native template");
        std::cout<<"unsupported coordinate arithmetic strict-geometry refusal verified\n";
    }
}
int main(){try{
    transfers_masks_halos_and_whole();thirty_four_to_thirty_five_and_bad_proofs();
    zero_new_owner_departure_and_overflow();native_model_identity_and_spans();
    std::cout<<"owned sparse moment correction: four focused cases passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
