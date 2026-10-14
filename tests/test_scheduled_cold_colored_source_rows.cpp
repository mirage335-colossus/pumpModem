#include "scheduled_cold_colored_source_rows.hpp"
#include <iostream>
#include <stdexcept>
namespace d=datapump::modem::detail;
namespace c=d::scheduled_acquisition_certificate;
namespace g=d::scheduled_cold_colored;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(){try{
    constexpr std::size_t N=102,C=34;
    const g::SourceRows::Key key{-65999.000716L,0,17,1,900000};
    g::SourceRows source(key,67000,N);
    for(std::size_t i=0;i<N;++i){const auto physical=i<47?100+i:1000+i-47;
        const auto gain=std::min(1.,std::pow(double(i+1)/60,3));
        const std::array<std::complex<double>,2> q{gain*std::polar(.7,.23*i),gain*std::polar(.8,-.17*i)};
        source.append(physical,q,i+.000716L,gain,1);}
    source.seal();check(source.runs.size()==2,"source rows retained null span");
    check(source.runs[0].count==47 && source.runs[1].first==47 && source.runs[1].count==N-47,"source-run row binding changed");
    check(source.working_bytes()<=g::SourceRows::reservation(N),"source cache escaped reservation");
    check(source.key==key,"source cache changed exact reference key");
    auto other=key;other.anchor_fraction=.000001L;check(!(source.key==other),"source cache reused another fractional anchor");
    other=key;other.origin+=1;check(!(source.key==other),"source cache reused another origin");
    other=key;other.origin+=17;const auto translation=source.translation_to(other);
    if(c::coordinate_environment()) {
        check(translation && *translation==17,"exact integer source translation was not reused");
        for(const auto& run:source.runs)for(std::size_t i=0;i<run.count;++i) {
            const auto p=static_cast<long double>(run.physical_offset+i)-source.shift;
            check((p-key.origin)==((p+*translation)-other.origin),"integer rebase changed executed source subtraction");
        }
    } else check(!translation,"unqualified coordinate arithmetic admitted a source translation");
#if (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
    unsigned short saved=0;asm volatile("fnstcw %0":"=m"(saved));
    for(const unsigned short width:{0x0000U,0x0200U}) {
        auto changed=static_cast<unsigned short>((saved&~0x0300U)|width);
        asm volatile("fldcw %0"::"m"(changed));
        const bool refused=!source.translation_to(other);
        unsigned short after=0;asm volatile("fnstcw %0":"=m"(after));
        asm volatile("fldcw %0"::"m"(saved));
        check(refused && after==changed,"source cache failed x87 precision qualification or changed control word");
    }
#endif
    other=key;other.origin+=.125L;check(!source.translation_to(other),"fractional source translation admitted");
    for(unsigned role=0;role<4;++role) {other=key;other.origin+=17;
        if(role==0)++other.phase;else if(role==1)++other.index;else if(role==2)++other.anchor_sample;else other.anchor_fraction=.25;
        check(!source.translation_to(other),"source translation reused a different cryptographic/anchor identity");}
    auto adversarial=key;adversarial.origin=std::nextafter(-.25L,-std::numeric_limits<long double>::infinity());
    g::SourceRows edge(adversarial,67000,1);edge.append(100,{},.5L,0,0);edge.seal();
    auto false_integer=adversarial;false_integer.origin=.75L;
    check(!edge.translation_to(false_integer),"rounded integer difference hid a nonzero TwoSum residual");
    bool postseal=false;try{source.append(2000,{},0,0,0);}catch(const datapump::Error&){postseal=true;}check(postseal,"sealed source rows changed");
    double energy=0;
    for(std::size_t i=0;i<N;++i){source.geometry[i]={i/C,0,c::mul(2e-6,std::max(source.norms[i][0],source.norms[i][1]))};
        const auto x=static_cast<double>(static_cast<float>(std::sin(.371*i)));energy+=x*x;}
    const auto eupper=c::up(energy/c::down(1-c::gamma(N,std::numeric_limits<double>::epsilon()/2)));
    g::RuntimeProxy cached(1,C,eupper,1e-6,2,512,0),fresh(1,C,eupper,1e-6,2,512,0);
    const std::array<std::size_t,1> starts{0};
    std::array<d::CorrelationChipEvidence,2> original{};
    for(const auto& run:source.runs){std::vector<float> pcm(run.count);std::vector<std::complex<double>> carrier(run.count);
        for(std::size_t i=0;i<run.count;++i){const auto row=run.first+i;pcm[i]=static_cast<float>(std::sin(.371*row));carrier[i]=std::polar(1.,.731*(run.physical_offset+i));
            d::CorrelationProjection p;const auto x=static_cast<double>(pcm[i]),a=carrier[i].real(),b=carrier[i].imag();
            p.xc=x*a;p.xs=x*b;p.cc=a*a;p.ss=b*b;p.cs=a*b;p.energy=x*x;
            for(unsigned bit=0;bit<2;++bit){d::CorrelationFit f;f.add(p,source.rows[row][bit]*std::complex<double>(1,1e-6),1);original[bit].add_fit(f,row/C);}}
        const auto rows=std::span<const std::array<std::complex<double>,2>>(source.rows).subspan(run.first,run.count);
        const auto masks=std::span<const std::uint8_t>(source.mask).subspan(run.first,run.count);
        const auto geometry=std::span<const g::ProxyRow>(source.geometry).subspan(run.first,run.count);
        const auto norms=std::span<const std::array<double,2>>(source.norms).subspan(run.first,run.count);
        cached.add({pcm,carrier,{},run.physical_offset},{rows,masks,run.physical_offset},starts,geometry,{},norms);
        fresh.add({pcm,carrier,{},run.physical_offset},{rows,masks,run.physical_offset},starts,geometry);
    }
    const auto a=cached.finish(),b=fresh.finish();check(cached.sample_pairs()==N,"on-demand source pairs include null span");
    for(unsigned bit=0;bit<2;++bit){const auto& v=a[0][bit];const auto tail=d::CorrelationChipEvidence::projection(original[bit].cell);
        const auto exact=original[bit].projected_energy+tail.first;
        check(v.valid && v.nested && v.owner_set_certified && v.count==N,"cached source evidence unknown");
        check(v.rank==original[bit].projected_rank+tail.second,"cache changed original projected rank");
        check(v.projected.lo<=exact && exact<=v.projected.hi,"cached source original energy escaped");
        check(v.projected.lo==b[0][bit].projected.lo && v.projected.hi==b[0][bit].projected.hi,"cached norm changed certificate");}
    g::SourceRows reversed(key,67000,2);reversed.append(100,{},0,0,0);
    bool overlap=false;try{reversed.append(100,{},1,0,0);}catch(const datapump::Error&){overlap=true;}check(overlap,"overlapping source cache rows admitted");
    std::cout<<"scheduled cold colored source reuse checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
