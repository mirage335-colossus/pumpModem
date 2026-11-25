// Written by the field-practice literature reviewer of the Sol-6.2 coherence-limit pass (25 Nov 2026).
// Usage: orthogonal_mi M bits_per_symbol trials. Q65 data symbols: M=64, R=78/63=1.238.
// The L-branch model (energy split over L i.i.d. Rayleigh branches, L ~ 1 + B*T) approximates a
// spread tone with an equal-weight square-law receiver; it is not the adapted weighted periodogram.
// Symbol-level mutual information of noncoherent M-ary orthogonal signalling:
//  mode 0: AWGN, unknown phase, known amplitude (ideal noncoherent MFSK)
//  mode L>=1: Rayleigh, energy split over L i.i.d. branches (square-law sum is sufficient statistic)
// Finds Es/N0 (dB, per channel symbol) needed for target bits/symbol.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
static double logI0(double x){ if(x<600.0) return std::log(std::cyl_bessel_i(0.0,x)); return x-0.5*std::log(2*M_PI*x); }
double mi(int M,int L,double esn0,int trials,unsigned seed){
  std::mt19937_64 rng(seed); std::normal_distribution<double> nd(0.0,std::sqrt(0.5));
  double acc=0; std::vector<double> lam(M);
  if(L==0){
    double A=std::sqrt(esn0);
    for(int t=0;t<trials;t++){
      for(int j=0;j<M;j++){ double re=nd(rng),im=nd(rng); if(j==0) re+=A; lam[j]=logI0(2*A*std::sqrt(re*re+im*im)); }
      double s=0; for(int j=0;j<M;j++) s+=std::exp(lam[j]-lam[0]); acc+=std::log2(s);
    }
  } else {
    double eb=esn0/L; double a=eb/(1+eb);
    std::gamma_distribution<double> gn(L,1.0), gs(L,1.0+eb);
    for(int t=0;t<trials;t++){
      for(int j=0;j<M;j++){ double e=(j==0)?gs(rng):gn(rng); lam[j]=a*e; }
      double s=0; for(int j=0;j<M;j++) s+=std::exp(lam[j]-lam[0]); acc+=std::log2(s);
    }
  }
  return std::log2((double)M)-acc/trials;
}
int main(int argc,char**argv){
  int M=atoi(argv[1]); double R=atof(argv[2]); int trials=atoi(argv[3]);
  int Ls[]={0,1,2,4,8,16,32,64};
  for(int L: Ls){
    double lo=-10,hi=30;
    for(int it=0;it<30;it++){ double mid=0.5*(lo+hi); double I=mi(M,L,std::pow(10,mid/10),trials,12345+it); if(I<R) lo=mid; else hi=mid; }
    double es=0.5*(lo+hi);
    printf("M=%d R=%.4f bits/sym L=%s%d  Es/N0=%.2f dB  Eb/N0(code)=%.2f dB\n",M,R,L==0?"AWGN":"Rayleigh x",L,es,es-10*std::log10(R));
  }
}
