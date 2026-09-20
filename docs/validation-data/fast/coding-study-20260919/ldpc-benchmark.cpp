#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>
#include "encoder.hh"
#include "generic.hh"
#include "layered_decoder.hh"
#include "dvb_s2_tables.hh"
#include "dvb_s2x_tables.hh"
using C=std::complex<double>;
std::vector<C> constellation(int m) {
 std::vector<unsigned> pop; std::vector<double> rad;
 if(m==4){pop={4};rad={1};}
 if(m==16){pop={4,12};rad={1,2.85};}
 if(m==64){pop={4,12,20,28};rad={1,2.8,4.6,6.4};}
 if(m==256){pop={4,12,20,28,36,44,52,60};rad={1,2.8,4.6,6.4,8.2,10,11.8,13.6};}
 double e=0;for(size_t i=0;i<pop.size();++i)e+=pop[i]*rad[i]*rad[i];
 std::vector<C> p(m);int l=0;for(size_t r=0;r<pop.size();++r)for(unsigned k=0;k<pop[r];++k,++l)p[l^(l>>1)]=std::polar(rad[r]/std::sqrt(e/m),2*std::acos(-1)*(k+.5)/pop[r]);
 return p;
}
int main(int argc,char**argv){
 if(argc!=6 && argc!=7){std::cerr<<"usage: rate (3/4,5/6,8/9,9/10), order, EsN0db, frames, seed\n";return 2;}
 std::string rate=argv[1];int m=std::stoi(argv[2]);double db=std::stod(argv[3]);int frames=std::stoi(argv[4]);unsigned seed=std::stoul(argv[5]);
 std::unique_ptr<LDPCInterface> code;
 if(rate=="3/4")code.reset(new LDPC<DVB_S2_TABLE_B7>);
 else if(rate=="7/9-B10")code.reset(new LDPC<DVB_S2X_TABLE_B10>);
 else if(rate=="7/9-B20")code.reset(new LDPC<DVB_S2X_TABLE_B20>);
 else if(rate=="4/5")code.reset(new LDPC<DVB_S2_TABLE_B8>);
 else if(rate=="5/6")code.reset(new LDPC<DVB_S2_TABLE_B9>);
 else if(rate=="8/9")code.reset(new LDPC<DVB_S2_TABLE_B10>);
 else if(rate=="9/10")code.reset(new LDPC<DVB_S2_TABLE_B11>);
 else return 3;
 int n=code->code_len(),k=code->data_len(),bits=std::lround(std::log2(m)),iters=100;
 LDPCEncoder<double> encode;encode.init(code.get());
 LDPCDecoder<double,LogDomainSPA<double,NormalUpdate<double>>> decode;decode.init(code.get());
 std::mt19937_64 gen(seed);std::normal_distribution<double> norm;std::vector<int> perm(n);std::iota(perm.begin(),perm.end(),0);std::shuffle(perm.begin(),perm.end(),gen);
 auto points=constellation(m);
 if(argc==7 && std::string(argv[6]).find("qam")==0) {
  int q=std::lround(std::sqrt(m)),qb=bits/2; double scale=std::sqrt(2.*(m-1)/3.);
  for(int y=0;y<q;++y)for(int x=0;x<q;++x)points[(x^(x>>1))|((y^(y>>1))<<qb)]=C((2*x-q+1)/scale,(2*y-q+1)/scale);
 }
 double n0=std::pow(10.,-db/10),sigma=std::sqrt(n0/2);
 std::vector<double> data(n),llr(n),prob(m),metrics(m);double decode_seconds=0,channel_seconds=0,gmi=0;long rawerr=0,biterr=0,totaliter=0;int badframes=0,syndromefail=0,undetected=0;
 for(int f=0;f<frames;++f){
  for(int i=0;i<k;++i)data[i]=(gen()&1)?-1:1;encode(data.data(),data.data()+k);
  auto t=std::chrono::steady_clock::now();
  for(int i=0;i<n;i+=bits){
   int label=0;for(int b=0;b<bits;++b)if(data[perm[i+b]]<0)label|=1<<b;
   C y=points[label]+C(sigma*norm(gen),sigma*norm(gen));double maxmet=-1e300;
   if(argc==7 && std::string(argv[6])=="qam_sep") {
    int q=std::lround(std::sqrt(m)),qb=bits/2; double scale=std::sqrt(2.*(m-1)/3.);
    for(int axis=0;axis<2;++axis){
     double z=axis?y.imag():y.real(),maximum=-1e300;
     for(int v=0;v<q;++v){double delta=z-(2*v-q+1)/scale;metrics[v]=-delta*delta/n0;maximum=std::max(maximum,metrics[v]);}
     for(int v=0;v<q;++v)prob[v]=std::exp(metrics[v]-maximum);
     for(int b=0;b<qb;++b){double s0=0,s1=0;for(int v=0;v<q;++v)if((v^(v>>1))&(1<<b))s1+=prob[v];else s0+=prob[v];double l=std::clamp(std::log(std::max(s0,1e-300)/std::max(s1,1e-300)),-50.,50.);int p=perm[i+axis*qb+b];llr[p]=l;rawerr+=(l*data[p]<=0);gmi+=1-std::log1p(std::exp(-l*data[p]))/std::log(2.);}
    }
    continue;
   }
   for(int j=0;j<m;++j){metrics[j]=-std::norm(y-points[j])/n0;maxmet=std::max(maxmet,metrics[j]);}
   for(int j=0;j<m;++j)prob[j]=std::exp(metrics[j]-maxmet);
   for(int b=0;b<bits;++b){double s0=0,s1=0;for(int j=0;j<m;++j)if(j&(1<<b))s1+=prob[j];else s0+=prob[j];double l=std::clamp(std::log(std::max(s0,1e-300)/std::max(s1,1e-300)),-50.,50.);int p=perm[i+b];llr[p]=l;rawerr+=(l*data[p]<=0);gmi+=1-std::log1p(std::exp(-l*data[p]))/std::log(2.);}
  }
  auto t2=std::chrono::steady_clock::now();channel_seconds+=std::chrono::duration<double>(t2-t).count();
  int left=decode(llr.data(),llr.data()+k,iters);auto t3=std::chrono::steady_clock::now();decode_seconds+=std::chrono::duration<double>(t3-t2).count();totaliter+=left<0?iters:iters-left;
  long errors=0;for(int i=0;i<n;++i)errors+=(llr[i]*data[i]<=0);biterr+=errors;badframes+=(errors>0);syndromefail+=(left<0);undetected+=(errors>0&&left>=0);
 }
 std::cout<<std::setprecision(8)<<rate<<','<<m<<','<<db<<','<<n<<','<<k<<','<<frames<<','<<seed<<','<<badframes<<','<<syndromefail<<','<<undetected<<','<<biterr<<','<<double(rawerr)/(n*frames)<<','<<gmi*bits/(n*frames)<<','<<double(totaliter)/frames<<','<<decode_seconds/frames<<','<<channel_seconds/frames<<','<<(argc==7?argv[6]:"apsk")<<'\n';
}
