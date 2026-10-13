#include "family_simd.hpp"
#include <limits>
#include <stdexcept>
#if !defined(DP_FAMILY_PORTABLE_ONLY) && (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define DP_FAMILY_X86 1
#else
#define DP_FAMILY_X86 0
#endif
namespace datapump::modem::detail::family_simd {
namespace {
std::size_t extent(std::size_t samples,std::size_t candidates,std::size_t stride) {
    if(!samples || !candidates)return 0;
    if(stride && stride<candidates)throw std::invalid_argument("family row stride too small");
    const auto step=stride?stride:1;
    const auto tail=stride?candidates:1;
    if(samples-1>(std::numeric_limits<std::size_t>::max()-tail)/step)
        throw std::invalid_argument("family extent overflow");
    return (samples-1)*step+tail;
}
void validate(const RawFamily& in,const std::array<FitPlanes,2>& out) {
    const auto pn=extent(in.samples,in.candidates,in.projection_row_stride);
    const auto tn=extent(in.samples,in.candidates,in.template_row_stride);
    for(const auto p:in.projection)if(p.size()<pn)throw std::invalid_argument("family projection extent");
    for(const auto& bit:in.coefficient)for(const auto p:bit)
        if(p.size()<tn)throw std::invalid_argument("family template extent");
    for(const auto& bit:out) {
        for(const auto p:bit.value)if(p.size()<in.candidates)throw std::invalid_argument("family output extent");
        if(bit.count.size()<in.candidates)throw std::invalid_argument("family count extent");
        for(std::size_t k=0;k<in.candidates;++k)
            if(in.samples>std::numeric_limits<std::uint64_t>::max()-bit.count[k])
                throw std::invalid_argument("family count overflow");
    }
}
// Contributions start from positive zero exactly as the production scheduled
// per-sample temporary Fit does. Keep separate operations (-ffp-contract=off).
void raw_scalar(const RawFamily& in,const std::array<FitPlanes,2>& out,std::size_t first) {
    for(std::size_t sample=0;sample<in.samples;++sample) {
        const auto pr=sample*(in.projection_row_stride?in.projection_row_stride:1);
        const auto tr=sample*(in.template_row_stride?in.template_row_stride:1);
        for(std::size_t lane=first;lane<in.candidates;++lane) {
            const auto p=pr+(in.projection_row_stride?lane:0);
            const auto t=tr+(in.template_row_stride?lane:0);
            const auto xc=in.projection[0][p],xs=in.projection[1][p];
            const auto cc=in.projection[2][p],ss=in.projection[3][p],cs=in.projection[4][p];
            for(unsigned bit=0;bit<2;++bit) {
                const auto a=in.coefficient[bit][0][t],b=in.coefficient[bit][1][t];
                const std::array<double,6> c{0.0+(a*xc-b*xs),0.0+(b*xc+a*xs),
                    0.0+((a*a*cc+b*b*ss)-((2*a)*b)*cs),
                    0.0+((b*b*cc+a*a*ss)+((2*a)*b)*cs),
                    0.0+(a*b*(cc-ss)+(a*a-b*b)*cs),0.0+in.projection[5][p]};
                for(unsigned f=0;f<6;++f)out[bit].value[f][lane]+=c[f];
            }
        }
    }
}
template<class T> void affine_scalar(std::span<const T> ar,std::span<const T> ai,
    std::span<const T> br,std::span<const T> bi,std::complex<T> z0,std::complex<T> z1,
    std::span<T> real,std::span<T> imag,std::size_t first) {
    for(std::size_t k=first;k<ar.size();++k) {
        real[k]=(ar[k]*z0.real()-ai[k]*z0.imag())+(br[k]*z1.real()-bi[k]*z1.imag());
        imag[k]=(ar[k]*z0.imag()+ai[k]*z0.real())+(br[k]*z1.imag()+bi[k]*z1.real());
    }
}
template<class T> void validate_affine(std::span<const T> ar,std::span<const T> ai,
    std::span<const T> br,std::span<const T> bi,std::span<T> real,std::span<T> imag) {
    if(ai.size()!=ar.size() || br.size()!=ar.size() || bi.size()!=ar.size() ||
       real.size()!=ar.size() || imag.size()!=ar.size())throw std::invalid_argument("affine plane extent");
}
#if DP_FAMILY_X86
__attribute__((target("sse2"))) void raw_sse2(const RawFamily& in,const std::array<FitPlanes,2>& out) {
    const auto lanes=in.candidates-in.candidates%2;
    const auto zero=_mm_setzero_pd(),two=_mm_set1_pd(2.0);
    for(std::size_t sample=0;sample<in.samples;++sample) {
        const auto pr=sample*(in.projection_row_stride?in.projection_row_stride:1);
        const auto tr=sample*(in.template_row_stride?in.template_row_stride:1);
        for(std::size_t lane=0;lane<lanes;lane+=2) {
            __m128d p[6];
            for(unsigned f=0;f<6;++f)p[f]=in.projection_row_stride?_mm_loadu_pd(in.projection[f].data()+pr+lane):_mm_set1_pd(in.projection[f][pr]);
            for(unsigned bit=0;bit<2;++bit) {
                const auto a=in.template_row_stride?_mm_loadu_pd(in.coefficient[bit][0].data()+tr+lane):_mm_set1_pd(in.coefficient[bit][0][tr]);
                const auto b=in.template_row_stride?_mm_loadu_pd(in.coefficient[bit][1].data()+tr+lane):_mm_set1_pd(in.coefficient[bit][1][tr]);
                const auto aa=_mm_mul_pd(a,a),bb=_mm_mul_pd(b,b),ab=_mm_mul_pd(a,b);
                const auto twice_ab=_mm_mul_pd(_mm_mul_pd(two,a),b);
                const __m128d c[6]{
                    _mm_add_pd(zero,_mm_sub_pd(_mm_mul_pd(a,p[0]),_mm_mul_pd(b,p[1]))),
                    _mm_add_pd(zero,_mm_add_pd(_mm_mul_pd(b,p[0]),_mm_mul_pd(a,p[1]))),
                    _mm_add_pd(zero,_mm_sub_pd(_mm_add_pd(_mm_mul_pd(aa,p[2]),_mm_mul_pd(bb,p[3])),_mm_mul_pd(twice_ab,p[4]))),
                    _mm_add_pd(zero,_mm_add_pd(_mm_add_pd(_mm_mul_pd(bb,p[2]),_mm_mul_pd(aa,p[3])),_mm_mul_pd(twice_ab,p[4]))),
                    _mm_add_pd(zero,_mm_add_pd(_mm_mul_pd(ab,_mm_sub_pd(p[2],p[3])),_mm_mul_pd(_mm_sub_pd(aa,bb),p[4]))),
                    _mm_add_pd(zero,p[5])};
                for(unsigned f=0;f<6;++f)_mm_storeu_pd(out[bit].value[f].data()+lane,
                    _mm_add_pd(_mm_loadu_pd(out[bit].value[f].data()+lane),c[f]));
            }
        }
    }
    raw_scalar(in,out,lanes);
}
__attribute__((target("avx2"))) void raw_avx2(const RawFamily& in,const std::array<FitPlanes,2>& out) {
    const auto lanes=in.candidates-in.candidates%4;
    const auto zero=_mm256_setzero_pd(),two=_mm256_set1_pd(2.0);
    for(std::size_t sample=0;sample<in.samples;++sample) {
        const auto pr=sample*(in.projection_row_stride?in.projection_row_stride:1);
        const auto tr=sample*(in.template_row_stride?in.template_row_stride:1);
        for(std::size_t lane=0;lane<lanes;lane+=4) {
            __m256d p[6];
            for(unsigned f=0;f<6;++f)p[f]=in.projection_row_stride?_mm256_loadu_pd(in.projection[f].data()+pr+lane):_mm256_set1_pd(in.projection[f][pr]);
            for(unsigned bit=0;bit<2;++bit) {
                const auto a=in.template_row_stride?_mm256_loadu_pd(in.coefficient[bit][0].data()+tr+lane):_mm256_set1_pd(in.coefficient[bit][0][tr]);
                const auto b=in.template_row_stride?_mm256_loadu_pd(in.coefficient[bit][1].data()+tr+lane):_mm256_set1_pd(in.coefficient[bit][1][tr]);
                const auto aa=_mm256_mul_pd(a,a),bb=_mm256_mul_pd(b,b),ab=_mm256_mul_pd(a,b);
                const auto twice_ab=_mm256_mul_pd(_mm256_mul_pd(two,a),b);
                const __m256d c[6]{
                    _mm256_add_pd(zero,_mm256_sub_pd(_mm256_mul_pd(a,p[0]),_mm256_mul_pd(b,p[1]))),
                    _mm256_add_pd(zero,_mm256_add_pd(_mm256_mul_pd(b,p[0]),_mm256_mul_pd(a,p[1]))),
                    _mm256_add_pd(zero,_mm256_sub_pd(_mm256_add_pd(_mm256_mul_pd(aa,p[2]),_mm256_mul_pd(bb,p[3])),_mm256_mul_pd(twice_ab,p[4]))),
                    _mm256_add_pd(zero,_mm256_add_pd(_mm256_add_pd(_mm256_mul_pd(bb,p[2]),_mm256_mul_pd(aa,p[3])),_mm256_mul_pd(twice_ab,p[4]))),
                    _mm256_add_pd(zero,_mm256_add_pd(_mm256_mul_pd(ab,_mm256_sub_pd(p[2],p[3])),_mm256_mul_pd(_mm256_sub_pd(aa,bb),p[4]))),
                    _mm256_add_pd(zero,p[5])};
                for(unsigned f=0;f<6;++f)_mm256_storeu_pd(out[bit].value[f].data()+lane,
                    _mm256_add_pd(_mm256_loadu_pd(out[bit].value[f].data()+lane),c[f]));
            }
        }
    }
    raw_scalar(in,out,lanes);
}
__attribute__((target("sse2"))) void affine_sse2_double(std::span<const double> ar,std::span<const double> ai,
    std::span<const double> br,std::span<const double> bi,std::complex<double> z0,std::complex<double> z1,
    std::span<double> real,std::span<double> imag) {
    std::size_t k=0;
    const auto z0r=_mm_set1_pd(z0.real()),z0i=_mm_set1_pd(z0.imag());
    const auto z1r=_mm_set1_pd(z1.real()),z1i=_mm_set1_pd(z1.imag());
    for(;ar.size()-k>=2;k+=2) {
        const auto a=_mm_loadu_pd(ar.data()+k),b=_mm_loadu_pd(ai.data()+k),c=_mm_loadu_pd(br.data()+k),d=_mm_loadu_pd(bi.data()+k);
        _mm_storeu_pd(real.data()+k,_mm_add_pd(_mm_sub_pd(_mm_mul_pd(a,z0r),_mm_mul_pd(b,z0i)),_mm_sub_pd(_mm_mul_pd(c,z1r),_mm_mul_pd(d,z1i))));
        _mm_storeu_pd(imag.data()+k,_mm_add_pd(_mm_add_pd(_mm_mul_pd(a,z0i),_mm_mul_pd(b,z0r)),_mm_add_pd(_mm_mul_pd(c,z1i),_mm_mul_pd(d,z1r))));
    }
    affine_scalar(ar,ai,br,bi,z0,z1,real,imag,k);
}
__attribute__((target("sse2"))) void affine_sse2_float(std::span<const float> ar,std::span<const float> ai,
    std::span<const float> br,std::span<const float> bi,std::complex<float> z0,std::complex<float> z1,
    std::span<float> real,std::span<float> imag) {
    std::size_t k=0;
    const auto z0r=_mm_set1_ps(z0.real()),z0i=_mm_set1_ps(z0.imag());
    const auto z1r=_mm_set1_ps(z1.real()),z1i=_mm_set1_ps(z1.imag());
    for(;ar.size()-k>=4;k+=4) {
        const auto a=_mm_loadu_ps(ar.data()+k),b=_mm_loadu_ps(ai.data()+k),c=_mm_loadu_ps(br.data()+k),d=_mm_loadu_ps(bi.data()+k);
        _mm_storeu_ps(real.data()+k,_mm_add_ps(_mm_sub_ps(_mm_mul_ps(a,z0r),_mm_mul_ps(b,z0i)),_mm_sub_ps(_mm_mul_ps(c,z1r),_mm_mul_ps(d,z1i))));
        _mm_storeu_ps(imag.data()+k,_mm_add_ps(_mm_add_ps(_mm_mul_ps(a,z0i),_mm_mul_ps(b,z0r)),_mm_add_ps(_mm_mul_ps(c,z1i),_mm_mul_ps(d,z1r))));
    }
    affine_scalar(ar,ai,br,bi,z0,z1,real,imag,k);
}
__attribute__((target("avx2"))) void affine_avx2_double(std::span<const double> ar,std::span<const double> ai,
    std::span<const double> br,std::span<const double> bi,std::complex<double> z0,std::complex<double> z1,
    std::span<double> real,std::span<double> imag) {
    std::size_t k=0;
    const auto z0r=_mm256_set1_pd(z0.real()),z0i=_mm256_set1_pd(z0.imag());
    const auto z1r=_mm256_set1_pd(z1.real()),z1i=_mm256_set1_pd(z1.imag());
    for(;ar.size()-k>=4;k+=4) {
        const auto a=_mm256_loadu_pd(ar.data()+k),b=_mm256_loadu_pd(ai.data()+k),c=_mm256_loadu_pd(br.data()+k),d=_mm256_loadu_pd(bi.data()+k);
        _mm256_storeu_pd(real.data()+k,_mm256_add_pd(_mm256_sub_pd(_mm256_mul_pd(a,z0r),_mm256_mul_pd(b,z0i)),_mm256_sub_pd(_mm256_mul_pd(c,z1r),_mm256_mul_pd(d,z1i))));
        _mm256_storeu_pd(imag.data()+k,_mm256_add_pd(_mm256_add_pd(_mm256_mul_pd(a,z0i),_mm256_mul_pd(b,z0r)),_mm256_add_pd(_mm256_mul_pd(c,z1i),_mm256_mul_pd(d,z1r))));
    }
    affine_scalar(ar,ai,br,bi,z0,z1,real,imag,k);
}
__attribute__((target("avx2"))) void affine_avx2_float(std::span<const float> ar,std::span<const float> ai,
    std::span<const float> br,std::span<const float> bi,std::complex<float> z0,std::complex<float> z1,
    std::span<float> real,std::span<float> imag) {
    std::size_t k=0;
    const auto z0r=_mm256_set1_ps(z0.real()),z0i=_mm256_set1_ps(z0.imag());
    const auto z1r=_mm256_set1_ps(z1.real()),z1i=_mm256_set1_ps(z1.imag());
    for(;ar.size()-k>=8;k+=8) {
        const auto a=_mm256_loadu_ps(ar.data()+k),b=_mm256_loadu_ps(ai.data()+k),c=_mm256_loadu_ps(br.data()+k),d=_mm256_loadu_ps(bi.data()+k);
        _mm256_storeu_ps(real.data()+k,_mm256_add_ps(_mm256_sub_ps(_mm256_mul_ps(a,z0r),_mm256_mul_ps(b,z0i)),_mm256_sub_ps(_mm256_mul_ps(c,z1r),_mm256_mul_ps(d,z1i))));
        _mm256_storeu_ps(imag.data()+k,_mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(a,z0i),_mm256_mul_ps(b,z0r)),_mm256_add_ps(_mm256_mul_ps(c,z1i),_mm256_mul_ps(d,z1r))));
    }
    affine_scalar(ar,ai,br,bi,z0,z1,real,imag,k);
}
#endif
} // namespace
bool available(Backend backend) noexcept {
    if(backend==Backend::automatic || backend==Backend::scalar)return true;
#if DP_FAMILY_X86
    // Compiler builtin includes OS-enabled AVX state in the runtime predicate.
    static const bool init=[] {__builtin_cpu_init();return true;}();(void)init;
    if(backend==Backend::avx2)return __builtin_cpu_supports("avx2");
    if(backend==Backend::sse2)return __builtin_cpu_supports("sse2");
#endif
    return false;
}
Backend selected(Backend backend) {
    if(backend!=Backend::automatic) {
        if(!available(backend))throw std::invalid_argument("unavailable family SIMD backend");
        return backend;
    }
    if(available(Backend::avx2))return Backend::avx2;
    if(available(Backend::sse2))return Backend::sse2;
    return Backend::scalar;
}
const char* name(Backend backend) {
    switch(selected(backend)) {
        case Backend::avx2:return "avx2";
        case Backend::sse2:return "sse2";
        default:return "scalar";
    }
}
void add_raw_f64(const RawFamily& in,const std::array<FitPlanes,2>& out,Backend backend) {
    validate(in,out);backend=selected(backend);
#if DP_FAMILY_X86
    if(backend==Backend::avx2)raw_avx2(in,out);
    else if(backend==Backend::sse2)raw_sse2(in,out);
    else
#endif
    raw_scalar(in,out,0);
    for(const auto& bit:out)for(std::size_t lane=0;lane<in.candidates;++lane)bit.count[lane]+=in.samples;
}
void affine_f64(std::span<const double> ar,std::span<const double> ai,
    std::span<const double> br,std::span<const double> bi,std::complex<double> z0,std::complex<double> z1,
    std::span<double> real,std::span<double> imag,Backend backend) {
    validate_affine(ar,ai,br,bi,real,imag);backend=selected(backend);
#if DP_FAMILY_X86
    if(backend==Backend::avx2)return affine_avx2_double(ar,ai,br,bi,z0,z1,real,imag);
    if(backend==Backend::sse2)return affine_sse2_double(ar,ai,br,bi,z0,z1,real,imag);
#endif
    affine_scalar(ar,ai,br,bi,z0,z1,real,imag,0);
}
void affine_f32(std::span<const float> ar,std::span<const float> ai,
    std::span<const float> br,std::span<const float> bi,std::complex<float> z0,std::complex<float> z1,
    std::span<float> real,std::span<float> imag,Backend backend) {
    validate_affine(ar,ai,br,bi,real,imag);backend=selected(backend);
#if DP_FAMILY_X86
    if(backend==Backend::avx2)return affine_avx2_float(ar,ai,br,bi,z0,z1,real,imag);
    if(backend==Backend::sse2)return affine_sse2_float(ar,ai,br,bi,z0,z1,real,imag);
#endif
    affine_scalar(ar,ai,br,bi,z0,z1,real,imag,0);
}
} // namespace datapump::modem::detail::family_simd
