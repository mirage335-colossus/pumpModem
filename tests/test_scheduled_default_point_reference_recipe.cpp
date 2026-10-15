#include "scheduled_default_point_reference_recipe.hpp"
#include <bit>
#include <memory>
#include <string>
#define main maintained_default_linear_fhss_main
#include "test_scheduled_default_linear_fhss.cpp"
#undef main
namespace recipe=d::scheduled_default_point_reference_recipe;
namespace compact=d::scheduled_default_compact_chords;
namespace family=d::scheduled_default_point_family;
template<class F>void rejects(F&& fn,const char* message){bool caught=false;try{fn();}catch(const datapump::Error&){caught=true;}check(caught,message);}
static bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
static void equal_cell(const e::Cell& a,const e::Cell& b) {
    check(a.chord.supported==b.chord.supported&&a.chord.observed==b.chord.observed&&a.source_node==b.source_node&&a.outer_zero==b.outer_zero,"frozen recipe changed control/source-node metadata");
    check(bits(a.outer_gain_left,b.outer_gain_left)&&bits(a.outer_gain_right,b.outer_gain_right),"frozen recipe changed public pulse gains");
    for(unsigned bit=0;bit<2;++bit){check(bits(a.chord.left[bit].real(),b.chord.left[bit].real())&&bits(a.chord.left[bit].imag(),b.chord.left[bit].imag())&&
        bits(a.chord.right[bit].real(),b.chord.right[bit].real())&&bits(a.chord.right[bit].imag(),b.chord.right[bit].imag()),"frozen recipe changed endpoint bits");}
}
static e::Cell normalized(e::Cell c){if(c.chord.supported&&!c.chord.observed){c.chord.left={};c.chord.right={};c.chord.observed=true;}return c;}
static void frozen(std::uint32_t fs,std::uint64_t chip,std::uint64_t count,long double rate,long double origin,bool with_fhss) {
    auto v=event(fs,chip,count,rate,origin);if(!with_fhss)v.fhss={};
    constexpr unsigned Q=4;constexpr double amplitude=.5;
    family::Geometry g{origin,chip/(static_cast<long double>(Q)*rate),0,v.capture_end+512,
        static_cast<std::size_t>(count*Q),7,0,Q/2,37};
    // Keep the canonical source grid but make its first cell cross the exact
    // ACTUAL observation crop. Both Context.classify and endpoint decline that
    // cell, even when FHSS makes all integer rows in it quiet. Later canonical
    // interior/quiet cells remain supported. No support flag is injected.
    v.begin=v.native_begin=static_cast<std::uint64_t>(std::ceil(origin+g.node_spacing/2));
    check(v.begin<origin+g.node_spacing,"recipe crop lost its boundary-crossing cell");
    const recipe::SourceIdentity source{v.identity.source,v.identity.source_revision,v.epoch_sample,v.phase,v.index,v.symbol_samples,v.chip_samples,Q,1,amplitude};
    std::vector<Fence> ledger;fences(v.begin,v.end,[&](const auto f){ledger.push_back(f);});
    const auto actual_fences=[&](auto first,auto end,auto emit){for(const auto& f:ledger)
        if(f.original_first<end&&f.original_first+f.original_count>first)emit(f);};
    const auto coefficients=[](std::uint64_t k){return e::Pair{std::polar(.7,.13*k),std::polar(.6,-.21*k)};};
    std::uint64_t shape_calls=0;
    const auto shape=[&](double s){++shape_calls;return e::Pair{Complex{.19+.11*std::sin(s/chip*.71),.23*std::cos(s/chip*.17)},
        Complex{.25*std::cos(s/chip*.31),-.13-.08*std::sin(s/chip*.23)}};};
    std::vector<e::Cell> expected;expected.reserve(g.template_cells);
    std::vector<e::Row> expected_rows(v.capture_end+2);
    std::unique_ptr<recipe::FrozenReference> retained;
    std::size_t original_capacity_bytes=0,charged=0;
    {
        Map map;r::Context context(1,0);context.add(0,v,map,actual_fences);
        for(std::size_t n=0;n<expected_rows.size();++n)expected_rows[n]=context.row(0,n,false,[](long double,double){return e::Pair{};});
        for(std::size_t k=0;k<g.template_cells;++k)expected.push_back(normalized(context.cell(0,g.physical_base+k*g.node_spacing,
            g.physical_base+(k+1)*g.node_spacing,Q,amplitude,coefficients,shape)));
        original_capacity_bytes=context.working_bytes()-sizeof(r::Context);charged=recipe::FrozenReference::reservation(context);
        rejects([&]{recipe::FrozenReference too_small(std::move(context),g,source,1);},"recipe moved uncharged vectors");
        check(context.lane_count()==1&&context.working_bytes()-sizeof(r::Context)==original_capacity_bytes,"failed recipe precharge consumed context");
        auto wrong=source;++wrong.epoch_sample;
        rejects([&]{recipe::FrozenReference wrong_epoch(std::move(context),g,wrong,charged);},"stream/private epoch substituted for physical epoch_sample");
        retained=std::make_unique<recipe::FrozenReference>(std::move(context),g,source,charged);
    }
    // Future ledger retirement must not alter the already selected operator.
    // Both original mapping and Context objects are gone; no locator is used.
    ledger.clear();ledger.shrink_to_fit();v.origin+=.25L;v.capture_end+=8192;
    check(retained->working_bytes()==charged&&charged>=sizeof(recipe::FrozenReference)+original_capacity_bytes,"recipe lost actual-capacity charge after move");
    for(std::size_t n=0;n<expected_rows.size();++n){const auto actual=retained->context().row(0,n,false,[](long double,double){return e::Pair{};});
        const auto& prior=expected_rows[n];check(actual.observed==prior.observed&&actual.owner==prior.owner&&actual.source==prior.source&&bits(actual.gain,prior.gain),
            "frozen recipe changed exact actual mask/owner/source or raw-gain coordinates");}
    compact::Directory directory(g.template_cells,compact::Directory::reservation(g.template_cells));
    std::uint64_t supported=0,unsupported=0;
    for(const auto& c:expected){directory.append({c.chord.left,c.chord.right,c.chord.supported});c.chord.supported?++supported:++unsupported;}
    directory.seal();
    if(!supported||!unsupported)throw std::runtime_error("recipe fixture lacks supported and exact fallback cells: Fs="+std::to_string(fs)+
        " C="+std::to_string(chip)+" FHSS="+std::to_string(with_fhss)+" supported="+std::to_string(supported)+" unsupported="+std::to_string(unsupported));
    check(!expected.front().chord.supported,"recipe first cell failed to exercise exact crop fallback");
    const auto key=retained->lookup_identity();compact::Tile<recipe::LookupIdentity> tile(key,compact::Tile<recipe::LookupIdentity>::reservation());
    const auto resolve=[&](std::size_t k){return retained->chord(k,source,coefficients,shape);};
    for(std::size_t k=0;k<g.template_cells;++k){equal_cell(expected[k],retained->cell(k,source,coefficients,shape));
        const auto c=tile.get(directory,k,key,resolve);
        if(c.supported)for(unsigned bit=0;bit<2;++bit)check(bits(c.left[bit].real(),expected[k].chord.left[bit].real())&&
            bits(c.left[bit].imag(),expected[k].chord.left[bit].imag())&&bits(c.right[bit].real(),expected[k].chord.right[bit].real())&&
            bits(c.right[bit].imag(),expected[k].chord.right[bit].imag()),"bounded tile regeneration changed chosen chord");}
    auto wrong=source;++wrong.revision;const auto before=shape_calls;
    rejects([&]{retained->cell(0,wrong,coefficients,shape);},"wrong private source revision used frozen recipe");
    check(shape_calls==before,"source callback executed before frozen identity check");
    auto wrongkey=key;++wrongkey.instance;
    rejects([&]{tile.get(directory,0,wrongkey,resolve);},"different frozen Context/grid reused payload tile");
    std::stop_source stop;stop.request_stop();
    rejects([&]{retained->cell(0,source,coefficients,shape,stop.get_token());},"frozen recipe cancellation ignored");
    rejects([&]{retained->cell(g.template_cells,source,coefficients,shape);},"frozen recipe index escaped geometry");
    check(retained->identity()==source&&retained->geometry().physical_base==g.physical_base,"failed lookup mutated frozen provenance");
}
int main(){try {if(!d::scheduled_sparse_owned_spans::environment())return 77;
    frozen(6120,34,256,.9999999997L,.375L,false);frozen(6120,34,256,1.0000000003L,.125L,true);
    frozen(14400,8,640,.9999997L,.25L,true);
    std::cout<<"owned frozen reference recipe preserves exact chosen endpoints, current Pieces, public gains, source identity and vector capacity\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
