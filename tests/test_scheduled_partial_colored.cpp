#include "../src/scheduled_partial_colored.hpp"
#include <iostream>
#include <stdexcept>

namespace d=datapump::modem::detail;namespace p=d::scheduled_partial_colored;
namespace o=d::scheduled_cold_colored;namespace c=d::scheduled_acquisition_certificate;
#define CHECK(x) do{if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
constexpr std::size_t K=256,n=34;constexpr std::uint64_t identity=0x51c341;
constexpr p::FiniteProjectionDomain domain{32,2,512,true};
p::Gram gram(double diagonal){return {c::Interval::point(diagonal),c::Interval::point(diagonal),{}};}
void merge(d::CorrelationFit& a,const d::CorrelationFit& b){a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;
    a.cs+=b.cs;a.energy+=b.energy;a.count+=b.count;}
struct Fixture {std::vector<d::CorrelationFit> cells;d::CorrelationFit whole;d::CorrelationChipEvidence original;
    explicit Fixture(double diagonal=17){const auto cosine=std::sqrt(120./(20*K)),sine=std::sqrt(1-cosine*cosine),amplitude=std::sqrt(diagonal*20);
        for(std::size_t k=0;k<K;++k){d::CorrelationFit f;f.cc=f.ss=diagonal;f.xc=amplitude*cosine;f.xs=(k&1?-1:1)*amplitude*sine;
            f.energy=n;f.count=n;cells.push_back(f);merge(whole,f);original.add_fit(f,k);}}
};
p::AllCells proof(const Fixture& f,bool witnesses=false){p::NestingBuilder builder(K*n,K,128*1024,identity,domain);
    for(std::size_t k=0;k<K;++k){if(witnesses){const p::Witness a{k*n,{{1,1},{0,0}}},b{k*n+1,{{0,0},{1,1}}};
        builder.witness(k,n,a,b,34,1e-12);}else builder.gram(k,n,gram(f.cells[k].cc));}return builder.finish();}
o::FitIntervals enclose(const d::CorrelationFit& f){return o::enclose(f,0,0,0);}
void rejects_with_partial_scan(){Fixture f;auto all=proof(f);CHECK(all.valid());CHECK(all.rank()==2*K);CHECK(all.count()==K*n);
    CHECK(all.working_bytes()<128*1024);p::Partial part(all,identity);p::Result result;
    const double threshold=50;std::size_t scanned=0;
    for(;scanned<K;++scanned){part.complete_cell(scanned,enclose(f.cells[scanned]));result=p::bound(enclose(f.whole),part,threshold,{1e-12,true});
        if(result.colored&&result.score_upper<threshold){++scanned;break;}}
    CHECK(part.valid());CHECK(scanned<K/2);CHECK(part.count()==scanned*n);CHECK(part.count()<all.count());
    CHECK(f.original.colored_excess(f.whole,threshold));CHECK(f.original.score(f.whole)<threshold);
    CHECK(result.score_upper>=f.original.score(f.whole));
    // Actual counts never pretend this subset is the complete observation.
    CHECK(part.count()!=f.whole.count);
    auto witness_all=proof(f,true);CHECK(witness_all.valid());CHECK(witness_all.rank()==all.rank());
}
void all_owner_and_nesting_failures(){Fixture f;
    p::NestingBuilder missing(K*n,K,128*1024,identity,domain);for(std::size_t k=0;k<K-1;++k)missing.gram(k,n,gram(17));CHECK(!missing.finish().valid());
    p::NestingBuilder singular(K*n,K,128*1024,identity,domain);for(std::size_t k=0;k<K;++k)
        singular.gram(k,n,k==K-1?p::Gram{{34,34},{},{}}:gram(17));auto unsupported=singular.finish();CHECK(!unsupported.valid());
    p::Partial unavailable(unsupported,identity);unavailable.complete_cell(0,enclose(f.cells[0]));CHECK(!p::bound(enclose(f.whole),unavailable,50,{1e-12,true}).colored);
    p::NestingBuilder bad_count(K*n+1,K,128*1024,identity,domain);for(std::size_t k=0;k<K;++k)bad_count.gram(k,n,gram(17));CHECK(!bad_count.finish().valid());
    p::NestingBuilder bad_domain(K*n,K,128*1024,identity,{32,2,512,false});for(std::size_t k=0;k<K;++k)bad_domain.gram(k,n,gram(17));CHECK(!bad_domain.finish().valid());
    CHECK(!p::rank(1,{{1e-40,1e-40},{},{}}));CHECK(p::rank(1,{{1,1},{},{}})==1);
    CHECK(p::rank(1,{},{true})==0);CHECK(!p::rank(34,{{34,34},{},{}}));
    const p::Witness a{1,{{1,1},{0,0}}},b{2,{{0,0},{1,1}}};
    CHECK(p::rank2_witness(34,a,b,34,1e-12));CHECK(!p::rank2_witness(34,a,a,34,1e-12));
    CHECK(!p::rank2_witness(34,a,b,34,1));
}
void selection_failures(){Fixture f;auto all=proof(f);p::Partial repeated(all,identity);repeated.complete_cell(0,enclose(f.cells[0]));
    repeated.complete_cell(0,enclose(f.cells[0]));CHECK(!repeated.valid());
    p::Partial reversed(all,identity);reversed.complete_cell(1,enclose(f.cells[1]));reversed.complete_cell(0,enclose(f.cells[0]));CHECK(!reversed.valid());
    p::Partial wrong_context(all,identity+1);CHECK(!wrong_context.valid());
    p::Partial wrong_count(all,identity);auto cell=enclose(f.cells[0]);++cell.count;cell.count_lower=cell.count_upper=cell.count;
    wrong_count.complete_cell(0,cell);CHECK(!wrong_count.valid());
    p::Partial full(all,identity);for(std::size_t k=0;k<K;++k)full.complete_cell(k,enclose(f.cells[k]));
    CHECK(!p::bound(enclose(f.whole),full,50,{c::infinity,true}).colored);
    CHECK(!p::bound(enclose(f.whole),full,50,{1e-12,false}).colored);
    auto moving=proof(f);p::Partial borrowed(moving,identity);borrowed.complete_cell(0,enclose(f.cells[0]));
    auto moved=std::move(moving);CHECK(moved.valid());CHECK(!borrowed.valid());
    auto whole=enclose(f.whole);++whole.count_upper;CHECK(!p::bound(whole,full,50,{1e-12,true}).colored);
}
void explained_branch_and_rank_limit(){Fixture tiny(1e-10);auto all=proof(tiny);CHECK(all.valid());p::Partial part(all,identity);
    for(std::size_t k=0;k<K;++k)part.complete_cell(k,enclose(tiny.cells[k]));
    const auto value=p::bound(enclose(tiny.whole),part,50,{1e-12,true});CHECK(value.colored);CHECK(tiny.whole.explained()==0);
    CHECK(value.score_upper<1e-8); // projection is valid; explained has different max(1,cc*ss) gate.
    constexpr std::size_t count=128;p::NestingBuilder singles(count,count,8192,identity,domain);d::CorrelationFit whole;
    for(std::size_t k=0;k<count;++k){const p::Gram g=k&1?p::Gram{{},{1,1},{}}:p::Gram{{1,1},{},{}};singles.gram(k,1,g);
        d::CorrelationFit f;f.count=1;f.energy=1;f.cc=k&1?0:1;f.ss=k&1?1:0;merge(whole,f);}
    auto proof=singles.finish();CHECK(proof.valid());CHECK(proof.rank()==count);p::Partial empty(proof,identity);
    CHECK(!p::bound(enclose(whole),empty,50,{1e-12,true}).colored); // R=N intentionally disables original colored guard.
}
void prefix_identity_and_association(){Fixture f;auto all=proof(f);p::Partial prefix(all,identity);
    o::Summary running;running.owner_set_certified=true;bool stopped=false;
    for(std::size_t k=0;k<K;++k){running.retain(enclose(f.cells[k]));prefix.certified_prefix(k,running);CHECK(prefix.valid());
        CHECK(prefix.count()==(k+1)*n);CHECK(prefix.selected_rank()==2*(k+1));
        auto result=p::bound(enclose(f.whole),prefix,50,{1e-12,true});
        if(result.colored&&result.score_upper<50){CHECK(k<K/2);CHECK(result.score_upper>=f.original.score(f.whole));stopped=true;break;}}
    CHECK(stopped);p::Partial repeated(all,identity);repeated.certified_prefix(0,o::Summary{});CHECK(!repeated.valid());
    p::Partial wrong_count(all,identity);auto bad=running;++bad.count;wrong_count.certified_prefix(prefix.owners()-1,bad);CHECK(!wrong_count.valid());
    p::Partial wrong_rank(all,identity);bad=running;++bad.rank;wrong_rank.certified_prefix(prefix.owners()-1,bad);CHECK(!wrong_rank.valid());
    p::Partial mixed(all,identity);mixed.complete_cell(0,enclose(f.cells[0]));mixed.certified_prefix(prefix.owners()-1,running);CHECK(!mixed.valid());
    p::Partial reverse_mixed(all,identity);reverse_mixed.certified_prefix(prefix.owners()-1,running);reverse_mixed.complete_cell(prefix.owners(),enclose(f.cells[0]));CHECK(!reverse_mixed.valid());
    auto unsafe=enclose(f.cells[0]);unsafe.count=unsafe.count_lower=unsafe.count_upper=1;unsafe.xc={1e308,1e308};
    CHECK(!o::projection(unsafe).valid);
    CHECK(p::rank2_eigen_lower(34,1,34,1e-12));CHECK(!p::rank2_eigen_lower(34,1,34,1));
}
int main(){try{prefix_identity_and_association();rejects_with_partial_scan();all_owner_and_nesting_failures();selection_failures();explained_branch_and_rank_limit();
    std::cout<<"partial colored rejection certificate tests PASS\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
