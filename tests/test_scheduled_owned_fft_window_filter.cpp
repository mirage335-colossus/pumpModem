#include "scheduled_owned_fft_window_filter.hpp"
#include <array>
#include <iostream>
#include <memory>
#include <set>

namespace d=datapump::modem::detail;
namespace w=d::scheduled_owned_fft_window_filter;
namespace p=d::scheduled_polynomial_chip;
namespace c=d::scheduled_cold_colored;
void check(bool value,const char* message){if(!value)throw datapump::Error(message);}
template<class F>void rejects(F run,const char* message){bool caught=false;try{run();}catch(const datapump::Error&){caught=true;}check(caught,message);}
struct Spec {std::uint64_t physical;std::size_t rows=34;bool hole=false;};
struct Fixture {
    std::unique_ptr<c::SourceRows> source;std::unique_ptr<p::SourceModels> models;
    explicit Fixture(const std::vector<Spec>& specs) {
        std::size_t rows=0;for(const auto& s:specs)rows+=s.rows;
        c::SourceRows::Key key;key.origin=17;key.anchor_fraction=.000715L;key.rate=1.0000000003L;
        source=std::make_unique<c::SourceRows>(key,901,rows);
        for(std::size_t i=0;i<specs.size();++i)for(std::size_t n=0;n<specs[i].rows;++n)
            source->append(specs[i].physical+n,{{{.2,.1},{-.1,.3}}},
                (34*i+n)*key.rate+.000715L,1,specs[i].hole && n==11?0:1);
        source->seal();models=std::make_unique<p::SourceModels>(*source,34*(specs.size()+1),p::SourceModels::NativeTemplateTag{});
    }
};
std::vector<std::size_t> selection(const w::View* view,std::uint64_t request,std::uint64_t first,std::size_t count,w::Selection* work=nullptr) {
    std::array<std::size_t,256> output{};const auto result=w::select(view,request,first,count,output);
    if(work)*work=result;return {output.begin(),output.begin()+result.count};
}
std::vector<std::size_t> oracle(const w::View& view,std::uint64_t request,std::uint64_t first,std::size_t count) {
    std::set<std::size_t> rows{static_cast<std::size_t>(request-first)};
    for(const auto p:view.starts())for(unsigned shift:{0U,17U}) {
        if(shift && !view.next17())continue;
        if(p>std::numeric_limits<std::uint64_t>::max()-shift)continue;const auto q=p+shift;
        if(q>=first && q-first<=count-34 && q>=view.crop().available_first &&
           q<=view.crop().available_end && view.crop().available_end-q>=34 && q%17==request%17)
            rows.insert(static_cast<std::size_t>(q-first));
    }
    return {rows.begin(),rows.end()};
}
void exact_translation_crop_and_partial_cells() {
    Fixture f({{1000},{1068,33},{1120,34,true},{1204},{1255}});
    const w::Crop crop{90,500,100,450};w::View view(*f.source,*f.models,-901,crop);
    //1000-901=99 is excluded by the available crop; remaining full cells303,354.
    check(view.starts().size()==2 && view.starts()[0]==303 && view.starts()[1]==354,"actual translated full-cell coordinates changed");
    check(view.matches() && view.translation()==-901 && view.working_bytes()<=w::View::reservation(f.models->cells.size()),"view binding or reservation failed");
    for(const auto request:{100ULL,303ULL,320ULL,416ULL}) {
        const auto actual=selection(&view,request,100,350);check(actual==oracle(view,request,100,350),"sparse full-cell selector differs from oracle");
        check(std::binary_search(actual.begin(),actual.end(),request-100),"mandatory request omitted");
    }
}
void edge_next17_and_dense_default() {
    Fixture f({{983},{1017},{1051},{1085},{1160}});w::View view(*f.source,*f.models,0,{0,2000,0,2000});
    for(const auto request:{1000ULL,1017ULL,1169ULL}) {
        const auto actual=selection(&view,request,1000,203);check(actual==oracle(view,request,1000,203),"tile-edge next-origin merge differs");
    }
    const auto selected=selection(&view,1000,1000,203);
    check(std::binary_search(selected.begin(),selected.end(),0) && std::binary_search(selected.begin(),selected.end(),17),"+17 stream missed cell before tile or current source cell");
    check(!std::binary_search(selected.begin(),selected.end(),160),"a different full-cell residue was silently added");
    const auto last=selection(&view,1169,1000,203);
    check(std::binary_search(last.begin(),last.end(),169),"last legal request omitted");
    for(std::size_t residue=0;residue<17;++residue) {
        std::vector<std::size_t> dense;for(auto at=residue;at+34<=4096;at+=17)dense.push_back(at);
        check(selection(nullptr,1000+residue,1000,4096)==dense,"default dense17 policy changed");
    }
    w::View current(*f.source,*f.models,0,{0,2000,0,2000},false);
    check(selection(&current,1000,1000,203)==oracle(current,1000,1000,203),"next17 opt-out changed current cell selection");
    w::View exact_end(*f.source,*f.models,0,{0,2000,0,1034});
    w::View one_beyond(*f.source,*f.models,0,{0,2000,0,1033});
    const auto edge=selection(&exact_end,983,980,100),clipped=selection(&one_beyond,983,980,100);
    check(std::binary_search(edge.begin(),edge.end(),20) && !std::binary_search(clipped.begin(),clipped.end(),20),
        "+17 prefetch did not respect exact capture edge");
    std::array<std::size_t,1> tiny{};
    rejects([&]{(void)w::select(&exact_end,983,980,100,tiny);},"undersized selected-window buffer accepted");
}
void identity_and_unsupported_mapping() {
    Fixture f({{1000},{1068}});const w::Crop crop{0,2000,0,2000};
    for(const auto bad:{.25L,std::numeric_limits<long double>::infinity(),std::numeric_limits<long double>::quiet_NaN(),0x1p63L})
        rejects([&]{w::View view(*f.source,*f.models,bad,crop);},"unsupported translation accepted");
    w::View view(*f.source,*f.models,0,crop);std::array<std::size_t,256> output{};
    const auto rate=f.source->key.rate;f.source->key.rate=1;
    rejects([&]{(void)w::select(&view,1000,1000,128,output);},"different actual source rate reused old inventory");f.source->key.rate=rate;
    ++f.source->geometry_revision;f.source->geometry_delta=.01L;
    check(view.matches(),"mutable Original geometry incorrectly identifies native cache windows");
    f.source->sealed=false;check(!view.matches(),"unsealed source reused native inventory");f.source->sealed=true;
    Fixture other({{1000},{1068}});rejects([&]{w::View bad(*other.source,*f.models,0,crop);},"different source pointer reused model");
    const auto saved=f.models->native_template_only;f.models->native_template_only=false;check(!view.matches(),"strict owner model reused native inventory");f.models->native_template_only=saved;
    rejects([&]{(void)w::select(&view,999,1000,128,output);},"request before tile accepted");
    rejects([&]{(void)w::select(&view,1095,1000,128,output);},"request after final full window accepted");
    rejects([&]{(void)w::select(&view,1000,std::numeric_limits<std::uint64_t>::max()-10,128,output);},"tile coordinate overflow accepted");
    std::stop_source stop;stop.request_stop();rejects([&]{(void)w::select(&view,1000,1000,128,output,stop.get_token());},"cancelled selector completed");
    rejects([&]{w::View cancelled(*f.source,*f.models,0,crop,true,stop.get_token());},"cancelled inventory completed");
    check(!w::translate(0,-1) && !w::translate(std::numeric_limits<std::uint64_t>::max(),1) &&
        w::translate(0,0)==0 && w::translate(0x8000000000000000ULL,std::numeric_limits<std::int64_t>::min())==0,"checked integer mapping failed");
}
void bounded_index_and_logarithmic_tile_lookup() {
    std::vector<Spec> specs;for(std::uint64_t i=0;i<2560;++i)specs.push_back({1000+i*1000});Fixture f(specs);
    w::View view(*f.source,*f.models,0,{0,3000000,0,3000000});check(view.starts().size()==2560,"actualK inventory changed");
    check(2*view.starts().size()==5120,"current/next inventory bound changed");
    w::Selection work;const auto actual=selection(&view,1281000,1280000,4096,&work);
    check(actual==oracle(view,1281000,1280000,4096),"large sparse inventory differs from oracle");
    check(work.filtered && work.search_probes<=24 && work.visited<=12,"tile lookup rescanned allK cells");
    check(actual.size()<20 && actual.size()<selection(nullptr,1281000,1280000,4096).size(),"sparse tile still fills null-grid windows");
}
int main(){try {
    exact_translation_crop_and_partial_cells();edge_next17_and_dense_default();identity_and_unsupported_mapping();bounded_index_and_logarithmic_tile_lookup();
    std::cout<<"owned FFT window filter: four focused source cases passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
