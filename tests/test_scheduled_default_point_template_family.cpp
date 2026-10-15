// Reuse the maintained masks/zero-q/Original-addback fixture without changing
// any of its assertions or source. Integration puts both files in tests/.
#define main maintained_default_point_family_main
#include "test_scheduled_default_point_family.cpp"
#undef main
#include <bit>
#include <cstring>
namespace cache=d::scheduled_default_point_template_cache;
static void exact_fit(const d::CorrelationFit&a,const d::CorrelationFit&b) {
    check(a.count==b.count,"cached family changed observation count");
    for(const auto fields:{std::pair{a.xc,b.xc},std::pair{a.xs,b.xs},std::pair{a.cc,b.cc},std::pair{a.ss,b.ss},
        std::pair{a.cs,b.cs},std::pair{a.energy,b.energy}})
        check(std::bit_cast<std::uint64_t>(fields.first)==std::bit_cast<std::uint64_t>(fields.second),
            "exact cached template changed family I/Q/Gram/E bits");
    check(std::bit_cast<std::uint64_t>(a.score())==std::bit_cast<std::uint64_t>(b.score()),"exact cached family changed score bits");
}
template<class T>void retained_family() {
    Fixture f(3,33);cache::Pool<T> pool(1<<20);const auto budget=family::reservation<T>(f.g);
    const auto execute=[&](cache::Pool<T>* owner){return family::run<T>(f.g,budget,f.cells(),f.visit(),f.selections,f.fallback(),{},std::stop_token{},nullptr,owner);};
    const auto first=execute(&pool),first_reference=execute(nullptr);
    for(std::size_t j=0;j<f.g.outputs;++j)for(unsigned bit=0;bit<2;++bit)exact_fit(first.whole[j][bit],first_reference.whole[j][bit]);
    // Capture-time frequency differs; source/q, observation masks, actual
    // selected rows, zero-q noise rows and Original addbacks remain unchanged.
    for(std::uint64_t n=0;n<f.pcm.size();++n) {
        const auto x=.37*std::sin(.13*n)+.29*std::cos(.041*n)+.07*std::sin(.007*n*n);
        const auto o=std::polar(1.,.919*n);
        f.pcm[n]={x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x};
    }
    const auto second=execute(&pool),reference=execute(nullptr);
    check(second.valid==reference.valid,"cached family changed numerical fallback decisions");
    for(std::size_t j=0;j<f.g.outputs;++j)for(unsigned bit=0;bit<2;++bit)exact_fit(second.whole[j][bit],reference.whole[j][bit]);
    check(second.work.transforms.template_cache_hits>0&&second.work.transforms.template_ffts==0,
        "family did not execute exact frequency-independent prepared spectra");
    check(second.work.mask_rows==reference.work.mask_rows&&second.work.original_rows==reference.work.original_rows&&
        second.work.selected_rows==reference.work.selected_rows&&second.work.shared_input_rows==reference.work.shared_input_rows,
        "template reuse altered masks/noise/Original physical work");
    check(second.work.transforms.input_ffts==reference.work.transforms.input_ffts&&
        second.work.transforms.inverse_ffts==reference.work.transforms.inverse_ffts&&
        second.work.transforms.spectral_products==reference.work.transforms.spectral_products,
        "template reuse omitted unrelated point arithmetic");
    check(second.work.peak_bytes<=budget&&pool.statistics().peak_bytes<=pool.working_bytes(),"family+archive exceeded separate charged loans");
    check(!pool.pinned(),"returned family retained a spectrum or run pin");
}
int main(){try {
    if(!cache::environment().supported){
        Fixture f(3,33);cache::Pool<float> pool(1<<20);const auto budget=family::reservation<float>(f.g);
        const auto a=family::run<float>(f.g,budget,f.cells(),f.visit(),f.selections,f.fallback(),{},std::stop_token{},nullptr,&pool);
        const auto b=family::run<float>(f.g,budget,f.cells(),f.visit(),f.selections,f.fallback());
        check(a.valid==b.valid,"unqualified family fallback changed validity");
        for(std::size_t j=0;j<f.g.outputs;++j)for(unsigned bit=0;bit<2;++bit)exact_fit(a.whole[j][bit],b.whole[j][bit]);
        std::cout<<"Exact template cache provider unqualified; unchanged family checked, cache-hit scope omitted\n";return 0;}
    retained_family<float>();retained_family<double>();
    std::cout<<"Exact frequency-independent template archive preserves full point family fields/masks/addback/energy/fallback\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
