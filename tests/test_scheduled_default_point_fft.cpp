#include "scheduled_default_point_fft.hpp"
#include <cstring>
#include <iostream>

namespace p=datapump::modem::detail::scheduled_default_point_fft;
using C=p::Wide;using L=std::complex<long double>;
void check(bool value,const char* text){if(!value)throw datapump::Error(text);}
template<class F>void rejects(F run,const char* text){bool caught=false;try{run();}catch(const datapump::Error&){caught=true;}check(caught,text);}
struct Fragment {std::vector<p::InputRow> x;std::vector<p::TemplateRow> q;};
Fragment fragment(std::uint64_t physical,std::size_t global,std::size_t count,std::size_t halo) {
    Fragment f;f.x.resize(count+halo);f.q.resize(count);
    for(std::size_t i=0;i<f.x.size();++i) {
        const auto at=static_cast<double>(physical+i),lambda=.2+.13*std::sin(.17*at);
        const auto o=std::polar(.91+.03*std::cos(.023*at),.271*at),y=(.3*std::sin(.037*at)+.17*std::cos(.077*at))*o;
        const auto k=o*o;const auto t=std::norm(o);const auto a=1-lambda;
        f.x[i]={a*y,lambda*y,a*a*k,a*lambda*k,lambda*lambda*k,C{a*a*t,0},C{a*lambda*t,0},C{lambda*lambda*t,0}};
    }
    for(std::size_t i=0;i<count;++i)for(unsigned bit=0;bit<2;++bit) {
        const auto at=static_cast<double>(global+i);const auto a=std::polar(.4+.01*std::sin(.13*at),.11*at+.43*bit);
        const auto b=std::polar(.3+.07*std::cos(.09*at),-.19*at-.31*bit);const auto z=8*bit;
        f.q[i][z]=a;f.q[i][z+1]=b;f.q[i][z+2]=a*a;f.q[i][z+3]=2.*a*b;f.q[i][z+4]=b*b;
        f.q[i][z+5]={std::norm(a),0};f.q[i][z+6]={2*(a*std::conj(b)).real(),0};f.q[i][z+7]={std::norm(b),0};
    }return f;
}
template<class T>void direct_multi_dwell(std::size_t step) {
    const p::Geometry g{65,9,step,0,true,false};p::StreamingEngine<T> engine(g,p::StreamingEngine<T>::reservation(g));
    const auto halo=(g.outputs-1)*step;
    const std::array<Fragment,3> fragments{fragment(7,0,65,halo),fragment(1000003,65,7,halo),fragment(3000127,72,41,halo)};
    std::array<std::array<L,16>,9> direct{};std::array<std::array<long double,16>,9> mass{};
    std::uint64_t packed_input=0,packed_template=0;
    for(const auto& f:fragments) {
        engine.append(f.x,f.q);check(engine.work().inverse_ffts==0,"append performed an inverse per fragment");
        packed_input+=8*f.x.size();packed_template+=16*f.q.size();
        for(std::size_t out=0;out<g.outputs;++out)for(std::size_t i=0;i<f.q.size();++i)for(std::size_t z=0;z<16;++z) {
            const auto x=f.x[i+out*step][z%8],q=f.q[i][z];const auto v=L{x.real(),x.imag()}*L{q.real(),q.imag()};
            direct[out][z]+=v;mass[out][z]+=std::abs(v);
        }
    }
    const auto result=engine.finish();check(result.size()==g.outputs,"lost selected outputs");
    const auto tolerance=std::is_same_v<T,float>?6e-5L:2e-11L;
    for(std::size_t out=0;out<g.outputs;++out)for(std::size_t z=0;z<16;++z) {
        const auto value=result[out][z];check(std::abs(L{value.real(),value.imag()}-direct[out][z])<=tolerance*std::max(1.L,mass[out][z]),"multi-dwell16-plane direct correlation differs");
    }
    const auto& w=engine.work();check(w.partitions==3 && w.residues==3 && w.input_ffts==24 && w.template_ffts==48 && w.inverse_ffts==16 &&
        w.spectral_products==48*w.transform && w.packed_input==packed_input && w.packed_template==packed_template,"stream work omitted real arithmetic");
    check(w.bytes==engine.working_bytes() && w.bytes<=p::StreamingEngine<T>::reservation(g),"workspace preflight too small");
    rejects([&]{(void)engine.finish();},"finished engine reused");rejects([&]{engine.append(fragments[0].x,fragments[0].q);},"append after finish accepted");
}
template<class T>void fixed_fragment_anchor_and_sign() {
    const p::Geometry g{37,4,3};p::StreamingEngine<T> engine(g,1<<20);
    Fragment a;a.q.resize(5);a.x.resize(14);a.q[4][0]={3,0};a.x[10][0]={2,0};engine.append(a.x,a.q);
    Fragment b;b.q.resize(1);b.x.resize(10);b.q[0][0]={-1,0};b.x[3][0]={4,0};engine.append(b.x,b.q);
    const auto result=engine.finish();const auto eps=std::is_same_v<T,float>?2e-5:1e-12;
    check(std::abs(result[1][0]-C{-4,0})<eps && std::abs(result[2][0]-C{6,0})<eps &&
        std::abs(result[0][0])<eps && std::abs(result[3][0])<eps,"short fragment reversed at wrong global anchor/sign");
}
template<class T>void lean_scalar_vector_recurrence() {
    constexpr std::size_t n=259;std::vector<std::complex<T>> a(n),b(n);
    std::vector<C> scalar(n),sc(n),accelerated(n),ac(n);
    for(unsigned piece=0;piece<13;++piece) {
        for(std::size_t k=0;k<n;++k) {
            const auto sign=piece%3==0?-1.:1.;a[k]={static_cast<T>(sign*(.7+.001*k)),static_cast<T>(.21*std::sin(.13*k))};
            b[k]={static_cast<T>(.31*std::cos(.17*k+piece)),static_cast<T>(-.13+.01*piece)};
        }
        a[0]={-T(0),T(0)};b[0]={T(1),-T(0)};
        p::impl::accumulate<T>(a,b,scalar,sc,false,{});
        p::impl::accumulate<T>(a,b,accelerated,ac,p::impl::avx2_available(),{});
    }
    check(std::memcmp(scalar.data(),accelerated.data(),n*sizeof(C))==0 && std::memcmp(sc.data(),ac.data(),n*sizeof(C))==0,
        "lean vector product/Neumaier differs from scalar recurrence");
}
void failure_cancellation_and_double_fallback() {
    const p::Geometry g{8,3,2};auto f=fragment(3,0,8,4);std::stop_source cancelled;cancelled.request_stop();
    rejects([&]{p::StreamingEngine<float> e(g,1);},"short preflight budget accepted");
    rejects([&]{p::StreamingEngine<double> e(g,1<<20,cancelled.get_token());},"cancelled constructor accepted");
    p::StreamingEngine<float> append_stop(g,1<<20);
    rejects([&]{append_stop.append(f.x,f.q,cancelled.get_token());},"cancelled append accepted");
    rejects([&]{(void)append_stop.finish();},"cancelled append engine was reusable");
    p::StreamingEngine<double> finish_stop(g,1<<20);finish_stop.append(f.x,f.q);
    rejects([&]{(void)finish_stop.finish(cancelled.get_token());},"cancelled finish accepted");
    rejects([&]{finish_stop.append(f.x,f.q);},"cancelled finish engine was reusable");
    p::StreamingEngine<double> failed(g,1<<20);f.q.back()[15]={std::numeric_limits<double>::infinity(),0};
    rejects([&]{failed.append(f.x,f.q);},"nonfinite last plane accepted");
    check(failed.work().input_ffts==8 && failed.work().template_ffts==15,"failure fixture did not exercise partial spectral updates");
    rejects([&]{(void)failed.finish();},"partial update engine reused");
    const p::Geometry small{1,1,1};std::array<p::InputRow,1> x{};std::array<p::TemplateRow,1> q{};x[0][0]={1e20,0};q[0][0]={1e-20,0};
    p::StreamingEngine<float> fp32(small,1<<20);rejects([&]{fp32.append(x,q);},"unsafe FP32 range accepted");
    p::StreamingEngine<double> fp64(small,1<<20);fp64.append(x,q);check(std::abs(fp64.finish()[0][0]-C{1,0})<1e-12,"native FP64 fallback correlation differs");
    x[0][0]={1e-30,0};q[0][0]={1,0};p::StreamingEngine<float> tiny(small,1<<20);rejects([&]{tiny.append(x,q);},"tiny FP32 range accepted");
    p::StreamingEngine<double> tiny64(small,1<<20);tiny64.append(x,q);check(std::abs(tiny64.finish()[0][0]-C{1e-30,0})<1e-40,"tiny FP64 fallback lost point");
    rejects([&]{(void)p::StreamingEngine<float>::reservation({8,3,65});},"unsupported step accepted");
    rejects([&]{(void)p::StreamingEngine<double>::reservation({8,3,1,0,false,false});},"unsupported partial-Gram geometry accepted");
    rejects([&]{(void)p::StreamingEngine<double>::reservation({8,3,1,0,true,true});},"polyphase geometry accepted");
    rejects([&]{(void)p::StreamingEngine<float>::reservation({SIZE_MAX,2,1});},"overflow geometry accepted");
}
int main(){try {
    for(const auto step:{1U,17U,64U}){direct_multi_dwell<float>(step);direct_multi_dwell<double>(step);}
    fixed_fragment_anchor_and_sign<float>();fixed_fragment_anchor_and_sign<double>();
    lean_scalar_vector_recurrence<float>();lean_scalar_vector_recurrence<double>();failure_cancellation_and_double_fallback();
    std::cout<<"Default point FFT: direct16corr/stream/FP32/FP64/range/workspace/cancellation source cases passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
