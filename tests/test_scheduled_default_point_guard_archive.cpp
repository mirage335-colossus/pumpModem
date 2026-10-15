#include "scheduled_default_point_guard_archive.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <vector>
namespace d=datapump::modem::detail;
namespace archive=d::scheduled_default_point_guard_archive;
namespace events=d::scheduled_default_linear_events;
namespace fft=d::scheduled_linear_grid_fft;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void fails(F&& action,const char* message) {
    bool failed=false;try{action();}catch(const datapump::Error&){failed=true;}check(failed,message);
}
archive::Layout layout() {return {-.375L,8.5L,3,2000,7,6,15,3,5,2};}
std::vector<events::Cell> cells() {
    std::vector<events::Cell> out(7);
    for(std::size_t i=0;i<out.size();++i) {
        auto& c=out[i];c.chord.supported=i!=4;c.chord.observed=i!=5;c.source_node=17*i;
        c.outer_gain_left=.5;c.outer_gain_right=.75;c.outer_zero=i==5;
        for(unsigned b=0;b<2;++b) {
            c.chord.left[b]={.125*static_cast<double>(i+b),-.25};
            c.chord.right[b]={.125*static_cast<double>(i+b+1),.375};
        }
    }
    return out;
}
fft::InputRow row(std::size_t k) {
    fft::InputRow out{};
    for(std::size_t p=0;p<out.size();++p)out[p]={.25*static_cast<double>(k+p),p<5?-.125*static_cast<double>(p):0};
    return out;
}
struct Input {
    std::vector<fft::InputRow> rows;
    std::vector<long double> energies;
    std::vector<std::uint64_t> counts;
    Input(std::size_t first,std::size_t n):rows(n),energies(n),counts(n) {
        for(std::size_t i=0;i<n;++i){rows[i]=row(first+i);energies[i]=.5L*(first+i);counts[i]=2;}
    }
    void install(archive::Archive& a,std::size_t first,std::size_t offset=0,std::size_t n=SIZE_MAX)const {
        n=std::min(n,rows.size()-offset);
        a.install_moments(first,std::span<const fft::InputRow>(rows).subspan(offset,n),
            std::span<const long double>(energies).subspan(offset,n),std::span<const std::uint64_t>(counts).subspan(offset,n));
    }
};
events::Geometry guard() {
    const auto l=layout();events::Geometry g;
    g.physical_base=l.physical_base;g.node_spacing=l.node_spacing;g.capture_first=l.capture_first;g.capture_end=l.capture_end;
    g.template_cells=l.template_cells;g.origin_first=l.origin_first;g.outputs=2;g.step=l.step;return g;
}
void overlap_holes_and_owned_copy() {
    const auto l=layout();const auto budget=archive::Archive::reservation(l);archive::Archive a(l,budget);
    check(a.working_bytes()==budget&&budget<=archive::Archive::maximum_bytes,"archive charge changed");
    check(!a.sealed()&&!a.cell(0)&&!a.moment(l.input_first).row&&!a.accepts(guard()),"unsealed archive escaped");
    auto c=cells();a.install_cells(0,std::span<const events::Cell>(c).first(3));a.install_cells(3,std::span<const events::Cell>(c).subspan(3));
    a.install_cells(1,std::span<const events::Cell>(c).subspan(1,2));
    Input input(l.input_first,11);input.install(a,l.input_first,0,7);input.install(a,l.input_first+4,4,7);
    const std::array<fft::InputRow,1> zero{};const std::array<long double,1> e{};const std::array<std::uint64_t,1> n{};
    a.install_moments(l.input_first+13,zero,e,n);
    a.seal();check(a.sealed(),"complete archive did not seal");
    for(std::size_t k=0;k<l.template_cells;++k)check(a.cell(k)&&a.cell(k)->source_node==c[k].source_node,"fragment cell indexing changed");
    for(std::size_t i=0;i<11;++i) {
        const auto v=a.moment(l.input_first+i);check(v.row&&*v.row==input.rows[i]&&v.energy==input.energies[i]&&v.count==2,
            "overlapping physical halo moment changed");
    }
    check(!a.cell(l.template_cells)&&!a.moment(l.input_first-1).row&&!a.moment(l.input_first+l.input_cells).row,
        "out-of-range lookup returned a row");
    check(!a.moment(l.input_first+12).row,"missing moment became a fake zero");
    const auto z=a.moment(l.input_first+13);check(z.row&&*z.row==fft::InputRow{}&&z.energy==0&&z.count==0,"present zero moment became a hole");
    const auto saved=a.cell(0)->chord.left;const auto saved_row=*a.moment(l.input_first).row;
    c[0].chord.left={};input.rows[0]={};check(a.cell(0)->chord.left==saved&&*a.moment(l.input_first).row==saved_row,"archive retained caller borrows");
    // Compatibility does not authorize a partial physical cell contraction:
    // cached N must equal the exact requested interval's row count first.
    check(a.accepts(guard())&&a.moment(l.input_first).count!=1,"consumer count mismatch witness lost");
}
void compatible_geometry() {
    const auto l=layout();archive::Archive a(l,archive::Archive::reservation(l));const auto c=cells();a.install_cells(0,c);a.seal();
    auto g=guard();check(a.accepts(g),"same cohort rejected");
    g.origin_first=5;g.outputs=3;check(a.accepts(g),"global origin subset rejected");
    g.capture_first=9;g.capture_end=1500;check(a.accepts(g),"covering capture was not accepted");
    auto wrong=g;wrong.capture_first=2;check(!a.accepts(wrong),"uncovered capture prefix accepted");
    wrong=g;wrong.capture_end=2001;check(!a.accepts(wrong),"uncovered capture tail accepted");
    wrong=g;wrong.capture_first=1501;check(!a.accepts(wrong),"reversed guard capture accepted");
    wrong=g;wrong.origin_first=2;check(!a.accepts(wrong),"origin before producer accepted");
    wrong=g;wrong.outputs=4;check(!a.accepts(wrong),"origin subset overrun accepted");
    wrong=g;wrong.origin_first=8;check(!a.accepts(wrong),"origin beyond producer accepted");
    wrong=g;wrong.outputs=0;check(!a.accepts(wrong),"empty guard cohort accepted");
    wrong=g;wrong.physical_base+=.01L;check(!a.accepts(wrong),"different global phase accepted");
    wrong=g;wrong.node_spacing+=.01L;check(!a.accepts(wrong),"different grid spacing accepted");
    wrong=g;++wrong.template_cells;check(!a.accepts(wrong),"different private template extent accepted");
    wrong=g;++wrong.step;check(!a.accepts(wrong),"different global origin step accepted");
}
void poison_and_duplicates() {
    const auto l=layout();const auto budget=archive::Archive::reservation(l);const auto c=cells();
    {
        archive::Archive a(l,budget);a.install_cells(0,std::span<const events::Cell>(c).first(6));
        fails([&]{a.seal();},"cell holes allowed seal");check(!a.sealed()&&!a.cell(0),"failed seal exposed partial cells");
        fails([&]{a.install_cells(0,c);},"failed archive resumed");
    }
    {
        archive::Archive a(l,budget);Input input(l.input_first,2);input.install(a,l.input_first);input.install(a,l.input_first);
        input.energies[1]+=1;
        fails([&]{input.install(a,l.input_first);},"different overlapping energy accepted");
        check(!a.sealed()&&!a.moment(l.input_first).row,"bad duplicate exposed prior moments");
        fails([&]{a.install_cells(0,c);},"bad duplicate did not poison archive");
    }
    {
        archive::Archive a(l,budget);Input input(l.input_first,1);input.install(a,l.input_first);input.counts[0]=3;
        fails([&]{input.install(a,l.input_first);},"different overlapping count accepted");
    }
    {
        archive::Archive a(l,budget);Input input(l.input_first,1);input.install(a,l.input_first);input.rows[0][0]+=fft::Wide{1,0};
        fails([&]{input.install(a,l.input_first);},"different overlapping moment plane accepted");
    }
    {
        archive::Archive a(l,budget);a.install_cells(0,c);auto changed=c;changed[2].source_node++;
        fails([&]{a.install_cells(0,changed);},"different cell duplicate accepted");
    }
    {
        archive::Archive a(l,budget);a.install_cells(0,c);a.seal();
        fails([&]{a.install_cells(0,c);},"sealed archive accepted mutation");check(!a.sealed(),"invalid mutation left archive usable");
    }
}
void bad_inputs_and_budget() {
    const auto l=layout();const auto budget=archive::Archive::reservation(l);
    fails([&]{archive::Archive a(l,budget-1);},"archive workspace limit ignored");
    auto huge=l;huge.template_cells=huge.input_cells=1048576;huge.origin_first=huge.input_first=0;huge.outputs=1;
    fails([&]{(void)archive::Archive::reservation(huge);},"archive32MiB cap ignored");
    auto bad=l;++bad.input_first;fails([&]{(void)archive::Archive::reservation(bad);},"wrong absolute input origin accepted");
    bad=l;--bad.input_cells;fails([&]{(void)archive::Archive::reservation(bad);},"incomplete input family halo accepted");
    bad=l;bad.node_spacing=std::numeric_limits<long double>::quiet_NaN();
    fails([&]{(void)archive::Archive::reservation(bad);},"nonfinite layout accepted");
    const auto invalid=[&](auto edit) {
        archive::Archive a(l,budget);Input input(l.input_first,1);edit(input);
        fails([&]{input.install(a,l.input_first);},"invalid moment accepted");check(!a.sealed(),"invalid moment sealed archive");
    };
    invalid([](auto& x){x.rows[0][1]={std::numeric_limits<double>::infinity(),0};});
    invalid([](auto& x){x.energies[0]=std::numeric_limits<long double>::infinity();});
    invalid([](auto& x){x.counts[0]=UINT64_MAX;});
    invalid([](auto& x){x.counts[0]=0;});
    invalid([](auto& x){x.rows[0][6]={1,1};});
    {
        archive::Archive a(l,budget);auto c=cells();c[1].chord.left[0]={std::numeric_limits<double>::quiet_NaN(),0};
        fails([&]{a.install_cells(0,c);},"nonfinite private cell accepted");
    }
    std::stop_source stop;stop.request_stop();fails([&]{archive::Archive a(l,budget,stop.get_token());},"constructor cancellation ignored");
    {
        archive::Archive a(l,budget);const auto c=cells();a.install_cells(0,c);
        fails([&]{a.seal(stop.get_token());},"seal cancellation ignored");check(!a.sealed()&&!a.cell(0),"cancelled seal exposed cells");
    }
    {
        archive::Archive a(l,budget);const auto c=cells();a.install_cells(0,c);Input input(l.input_first,1);input.install(a,l.input_first);
        fails([&]{a.install_cells(0,c,stop.get_token());},"install cancellation ignored");
        check(!a.sealed()&&!a.moment(l.input_first).row,"cancelled install exposed moments");
    }
}
}
int main(){try {
    static_assert(!std::is_copy_constructible_v<archive::Archive>&&!std::is_move_constructible_v<archive::Archive>);
    overlap_holes_and_owned_copy();compatible_geometry();poison_and_duplicates();bad_inputs_and_budget();
    std::cout<<"Default point guard archive source cases passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
