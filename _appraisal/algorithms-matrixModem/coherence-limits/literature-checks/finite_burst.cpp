// Written by the theory literature reviewer of the Sol-6.2 coherence-limit pass (25 Nov 2026).
// Loss of capacity per unit energy from a finite constant-envelope burst of length T, exact Gaussian
// log-determinant, Gaussian Doppler spectrum, -10 dB-Hz. DSTEP is the sample spacing in units of 1/(2 sigma).
#ifndef DSTEP
#define DSTEP 0.02
#endif
// Finite-burst capacity per unit energy for Gaussian-spectrum Rayleigh fading:
// Cp(T) = 1 - ln det(I + P K_T)/(P T), K_T sampled autocorrelation r(tau)=exp(-2 pi^2 sigma^2 tau^2)
#include <cstdio>
#include <cmath>
#include <vector>
double logdet(int n, double a, double s, double d){ // matrix I + a*r((i-j)d)
  std::vector<double> L((size_t)n*n,0.0);
  for(int i=0;i<n;i++) for(int j=0;j<=i;j++){ double t=(i-j)*d; L[(size_t)i*n+j]=a*exp(-2*M_PI*M_PI*s*s*t*t)+(i==j?1.0:0.0);} 
  double ld=0;
  for(int j=0;j<n;j++){
    double sum=L[(size_t)j*n+j];
    for(int k=0;k<j;k++) sum-=L[(size_t)j*n+k]*L[(size_t)j*n+k];
    double dj=sqrt(sum); L[(size_t)j*n+j]=dj; ld+=2*log(dj);
    for(int i=j+1;i<n;i++){ double v=L[(size_t)i*n+j]; for(int k=0;k<j;k++) v-=L[(size_t)i*n+k]*L[(size_t)j*n+k]; L[(size_t)i*n+j]=v/dj; }
  }
  return ld;
}
double asym(double P,double s){ // integral ln(1+P S(f)) df
  int n=200001; double Lm=12*s, h=2*Lm/(n-1), acc=0;
  for(int i=0;i<n;i++){ double f=-Lm+i*h; double S=exp(-0.5*f*f/(s*s))/(s*sqrt(2*M_PI)); acc+=(i==0||i==n-1?0.5:1)*log1p(P*S);} return acc*h;
}
int main(){
  double P=0.1; double spreads[4]={0.1,1,10,30};
  for(double B: spreads){ double s=B/2; double Cinf=1-asym(P,s)/P; double ebinf=10*log10(log(2)/Cinf);
    printf("spread %g Hz (P/B=%g): asymptotic Eb/N0min %.2f dB\n",B,P/B,ebinf);
    double TB[]={1,2,5,10,20,50,100,200};
    for(double tb: TB){ double T=tb/B; double d=DSTEP/(B); int n=(int)ceil(T/d); if(n<4) n=4; if(n>2500){n=2500;} d=T/n; 
      double ld=logdet(n,P*d,s,d); double Cp=1-ld/(P*T); double eb=10*log10(log(2)/Cp);
      printf("   T*B=%6g (T=%8.3g s, n=%d): Eb/N0 %.2f dB  (penalty %.2f dB)\n",tb,T,n,eb,eb-ebinf);} }
}
