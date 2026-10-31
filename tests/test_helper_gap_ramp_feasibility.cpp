// Exploratory two-hypothesis ramp witness for the known one-cycle gap jump.
// This is evaluator-only: it is given the true pre-gap timing reference,
// oscillator rates, RF retune dates and the two alternatives. It never feeds
// the Helper receiver. Independent complex pilot/auxiliary gains are fitted
// on each side of every hop. The independent probe is unmodeled interference:
// no probe waveform, symbols or timing is used to select a branch.
#define main helper_continuation_included_main
#include "test_helper_continuation_study.cpp"
#undef main

namespace {
using Matrix3=std::array<std::array<C,3>,3>;
using Vector3=std::array<C,3>;
double ramp_gate(double dt,double width){
    const double x=std::clamp(std::abs(dt)/width,0.,1.);
    return x*x*x*(x*(6*x-15)+10);
}
double ramp_projection(Matrix3 g,Vector3 b){
    const auto original=b;
    for(unsigned j=0;j<3;++j){
        unsigned pivot=j;
        for(unsigned k=j+1;k<3;++k)if(std::abs(g[k][j])>std::abs(g[pivot][j]))pivot=k;
        if(std::abs(g[pivot][j])<1e-10)return 0;
        std::swap(g[j],g[pivot]);std::swap(b[j],b[pivot]);
        const auto divisor=g[j][j];
        for(unsigned k=j;k<3;++k)g[j][k]/=divisor;
        b[j]/=divisor;
        for(unsigned i=0;i<3;++i)if(i!=j){
            const auto m=g[i][j];
            for(unsigned k=j;k<3;++k)g[i][k]-=m*g[j][k];
            b[i]-=m*b[j];
        }
    }
    C score{};for(unsigned i=0;i<3;++i)score+=std::conj(original[i])*b[i];
    return std::max(0.,score.real());
}
double ideal_original_boundary(const ht::Source& source,double boundary){
    double lo=boundary-.01,hi=boundary+.01;
    for(unsigned j=0;j<50;++j){const double mid=(lo+hi)/2;
        if(source.tx_time(mid)<boundary)lo=mid;else hi=mid;}
    return (lo+hi)/2;
}
struct RampResult {double difference=0;unsigned boundaries=0;};
RampResult ramp_run(unsigned seed,bool jump,hp::Probe probe,double loss,double reference_bias_us,double retune_bias_us){
    hp::Config config;config.seed=seed;config.probe=probe;config.path_loss_db=loss;
    ContinuationSource source(config,{},false,false,36.8,0,jump?1./1850:0);
    const double noise_power=fs*source.base().rx_scale()/std::pow(10.,source.base().high_cn0_db_hz()/10);
    std::array<double,2> score{};hp::Dwell previous;
    unsigned count=0;
    for(unsigned d=0;d<100;++d){
        auto current=source.generate(d);
        if(d>=95){
            const double nominal=.4*d;
            const double original=ideal_original_boundary(static_cast<const ht::Source&>(source),nominal);
            const double clock=source.base().audio_scale_at(original);
            const double retune=original+retune_bias_us*1e-6;
            const auto start=static_cast<std::uint64_t>(std::llround(nominal*fs))-288;
            for(unsigned candidate=0;candidate<2;++candidate){
                const double boundary=original+reference_bias_us*1e-6-(candidate?1./1850/clock:0);
                std::array<Matrix3,2> gram{};std::array<Vector3,2> rhs{};
                for(unsigned n=0;n<672;++n){
                    const auto sample=start+n;const double time=static_cast<double>(sample)/fs;
                    const bool before=time<retune;const unsigned side=before?0:1;
                    const auto& dwell=sample<d*hp::dwell_samples?previous:current;
                    const auto offset=sample-static_cast<std::uint64_t>(std::llround(dwell.first_seconds*fs));
                    const C input(dwell.samples.at(static_cast<std::size_t>(offset)));
                    const double rf=before?previous.shift_hz:current.shift_hz;
                    const double shift=source.base().shift_error_hz(rf,nominal);
                    constexpr std::array<double,3> frequencies{2400,550,950};
                    Vector3 x{};
                    for(unsigned p=0;p<3;++p){
                        const double gate=ramp_gate((time-boundary)*clock,p==2?.008:.004);
                        x[p]=gate*std::polar(1.,std::remainder(tau*(frequencies[p]*clock+shift)*(time-nominal),tau));
                        rhs[side][p]+=std::conj(x[p])*input;
                    }
                    for(unsigned p=0;p<3;++p)for(unsigned q=0;q<3;++q)gram[side][p][q]+=std::conj(x[p])*x[q];
                }
                score[candidate]+=(ramp_projection(gram[0],rhs[0])+ramp_projection(gram[1],rhs[1]))/noise_power;
            }
            ++count;
        }
        previous=std::move(current);
    }
    return {score[1]-score[0],count};
}
}

int main(int argc,char**argv){
    try{
        if(argc!=6)throw std::invalid_argument("SEED COUNT tone|apsk LOSS REFERENCE_BIAS_US");
        const unsigned first=static_cast<unsigned>(std::stoul(argv[1]));
        const unsigned n=static_cast<unsigned>(std::stoul(argv[2]));
        if(!n||n>64||first>UINT32_MAX-n)throw std::invalid_argument("invalid bounded seed range");
        const std::string kind=argv[3];if(kind!="tone"&&kind!="apsk")throw std::invalid_argument("expected tone or apsk");
        const auto probe=kind=="tone"?hp::Probe::tone:hp::Probe::apsk64;
        const double loss=std::stod(argv[4]),bias=std::stod(argv[5]);
        unsigned fp=0,fn=0;double normal_mean=0,jump_mean=0;
        for(unsigned i=0;i<n;++i){
            const auto normal=ramp_run(first+i,false,probe,loss,bias,0);
            const auto jump=ramp_run(first+i,true,probe,loss,bias,0);
            if(normal.difference>0)++fp;if(jump.difference<=0)++fn;
            normal_mean+=normal.difference;jump_mean+=jump.difference;
            std::cout<<std::setprecision(9)<<"helper_gap_ramp seed="<<first+i<<" probe="<<kind
                <<" loss_db="<<loss<<" helper_w=40 probe_w=40 combined_w=80"
                <<" temperature_K=290 noise_figure_db=10 schedule=keyed_hf50 original_reference_bias_us="<<bias
                <<" post_gap_boundaries="<<normal.boundaries<<" nojump_score_difference="<<normal.difference
                <<" jump_score_difference="<<jump.difference<<" false_positive="<<(normal.difference>0)
                <<" missed_jump="<<(jump.difference<=0)<<'\n';
        }
        std::cout<<"helper_gap_ramp_summary cases="<<n<<" false_positives="<<fp<<" missed_jumps="<<fn
            <<" nojump_mean_difference="<<normal_mean/n<<" jump_mean_difference="<<jump_mean/n
            <<" scoring=optimistic_two_hypotheses_exact_original_reference_clock_retune_rf frequency_model=truth"
            <<" nuisance=independent_pilot_aux_gain_each_side probe_model=none oracle_feeds_receiver=0"<<'\n';
        return 0;
    }catch(const std::exception&e){std::cerr<<"gap ramp feasibility: "<<e.what()<<'\n';return 1;}
}
