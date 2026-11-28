// Coherent power kept when the carrier frequency random-walks during the window.
// Scratch calculation of a delegated literature search, 27 November 2026, kept unchanged below
// this header. Monte Carlo of a model; quoted in
// _ref/algorithms-matrixModem/Fable-5.2/reviews/review-long-coherence-search.md (section 1.5).
// Build: g++ -O3 -march=native -std=c++20 -fopenmp frequency_wander.cpp -o /tmp/frequency_wander
// Run:   /tmp/frequency_wander > results/frequency_wander.txt

// Monte Carlo (own calculation): coherent power retained when the carrier frequency performs a
// Wiener random walk (random-walk FM) with diffusion sigma_f^2 [Hz^2/s] over a window T,
// q = sigma_f^2 T^3 (dimensionless). Receiver maximises over (a) frequency only, (b) frequency + linear drift.
#include <cstdio>
#include <cmath>
#include <random>
#include <vector>
#include <complex>
#include <algorithm>
#include <omp.h>
int main(){
  const int N=256; const double PI=M_PI;
  const double qs[]={0.01,0.03,0.1,0.25,0.5,1.0,2.0,4.0,8.0};
  const int trials=600;
  printf("q=sigma_f^2*T^3 | rms end freq wander [bins] | mean power retained: f-only (dB) | f+drift (dB) | 10th pct f+drift (dB)\n");
  for(double q: qs){
    std::vector<double> pf(trials), pd(trials);
    #pragma omp parallel for schedule(dynamic)
    for(int tr=0; tr<trials; ++tr){
      std::mt19937_64 g(1234567ULL*(tr+1)+ (unsigned long long)(q*1e6));
      std::normal_distribution<double> nd(0,1);
      std::vector<double> ph(N); double f=0, phi=0; double dt=1.0/N; double sf=std::sqrt(q*dt);
      const int sub=8; // sub-steps for integration accuracy
      for(int k=0;k<N;++k){
        ph[k]=phi; // phase at sample start (cycles)
        for(int s=0;s<sub;++s){ f+= sf/std::sqrt((double)sub)*nd(g); phi+= f*dt/sub; }
      }
      double frange=3.0+4.0*std::sqrt(q), drange=4.0+10.0*std::sqrt(q);
      double bestf=0, bestd=0;
      for(double d=-drange; d<=drange+1e-9; d+=0.25){
        for(double f0=-frange; f0<=frange+1e-9; f0+=0.0625){
          double re=0, im=0;
          for(int k=0;k<N;++k){ double t=(k+0.5)*dt-0.5; double a=2*PI*(ph[k]-f0*t-0.5*d*t*t); re+=std::cos(a); im+=std::sin(a);}          
          double p=(re*re+im*im)/((double)N*N);
          if(std::fabs(d)<1e-12 && p>bestf) bestf=p;
          if(p>bestd) bestd=p;
        }
      }
      pf[tr]=bestf; pd[tr]=bestd;
    }
    double mf=0, md=0; for(int i=0;i<trials;++i){mf+=pf[i]; md+=pd[i];} mf/=trials; md/=trials;
    std::sort(pd.begin(), pd.end());
    printf("%5.2f | %5.2f | %6.3f (%6.2f dB) | %6.3f (%6.2f dB) | %6.2f dB\n", q, std::sqrt(q), mf, 10*std::log10(mf), md, 10*std::log10(md), 10*std::log10(pd[trials/10]));
    fflush(stdout);
  }
}
