"""Conditional statistic confidence, not full-bank or universal PCM qualification."""
import argparse, bisect, csv, json, math, random, statistics

def beta_fraction(a,b,x):
    qab=a+b;qap=a+1;qam=a-1;c=1.;d=1-qab*x/qap
    if abs(d)<1e-300:d=1e-300
    d=1/d;h=d
    for m in range(1,501):
        aa=m*(b-m)*x/((qam+2*m)*(a+2*m));d=1+aa*d;c=1+aa/c
        if abs(d)<1e-300:d=1e-300
        if abs(c)<1e-300:c=1e-300
        d=1/d;h*=d*c
        aa=-(a+m)*(qab+m)*x/((a+2*m)*(qap+2*m));d=1+aa*d;c=1+aa/c
        if abs(d)<1e-300:d=1e-300
        if abs(c)<1e-300:c=1e-300
        d=1/d;change=d*c;h*=change
        if abs(change-1)<3e-14:return h
    raise ValueError('incomplete beta failed convergence')

def beta_cdf(x,a,b):
    if x<=0:return 0.
    if x>=1:return 1.
    log=math.lgamma(a+b)-math.lgamma(a)-math.lgamma(b)+a*math.log(x)+b*math.log1p(-x)
    front=math.exp(log)
    if x<(a+1)/(a+b+2):return front*beta_fraction(a,b,x)/a
    return 1-front*beta_fraction(b,a,1-x)/b

def beta_inverse(p,a,b):
    low=0.;high=1.
    for _ in range(65):
        mid=(low+high)/2
        if beta_cdf(mid,a,b)<p:low=mid
        else:high=mid
    return (low+high)/2

def cp(k,n,alpha):
    return (0. if not k else beta_inverse(alpha,k,n-k+1),
            1. if k==n else beta_inverse(1-alpha,k+1,n-k))

def quantile(values,p):
    # Inverse empirical CDF; no interpolation across unsampled tail draws.
    return values[max(0,math.ceil(p*len(values))-1)]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('csv',nargs='+');parser.add_argument('--expected-trials',type=int,default=20000)
    parser.add_argument('--grid-low',type=float,default=-5);parser.add_argument('--grid-high',type=float,default=10)
    parser.add_argument('--cell-db',type=float,default=.025);parser.add_argument('--family-alpha',type=float,default=.05)
    parser.add_argument('--bootstrap',type=int,default=1000);parser.add_argument('--output',required=True)
    a=parser.parse_args()
    cells=round((a.grid_high-a.grid_low)/a.cell_db)
    if cells<1 or abs(a.grid_low+cells*a.cell_db-a.grid_high)>1e-9:raise ValueError('grid must contain whole cells')
    grid=[a.grid_low+i*a.cell_db for i in range(cells+1)]
    # Both tails of every endpoint interval and every cell lower bound, across
    # all files. The grid is specified before opening qualification data.
    family_tests=len(a.csv)*(2*len(grid)+cells)
    exception_alpha=a.family_alpha*.2/len(a.csv)
    per_test=a.family_alpha*.8/family_tests
    reports=[]
    for path in a.csv:
        rows=list(csv.DictReader(open(path)))
        if len(rows)!=a.expected_trials:raise ValueError(f'{path}: incomplete trial count')
        if len({r['seed'] for r in rows})!=len(rows):raise ValueError('duplicate noise seed')
        invalid=sum(any(r[k]!='1' for k in ('off_bracketed','on_bracketed','off_monotone','on_monotone','nested')) for r in rows)
        if any(a.grid_low<float(r['qualified_cn0_low']) or a.grid_high>float(r['qualified_cn0_high']) for r in rows):raise ValueError('analysis grid outside qualified energy bracket')
        off=[float(r['guard_off_cn0']) for r in rows];on=[float(r['guard_on_cn0']) for r in rows]
        if not all(math.isfinite(x) for x in off+on):raise ValueError('unbracketed roots retained; quantile qualification unavailable')
        ordered=sorted(off);guarded=sorted(on);n=len(rows)
        eps=max(float(r['activation_probability_bound']) for r in rows)
        exception_upper=cp(invalid,len(rows),exception_alpha)[1]
        analytical=math.isfinite(eps)
        replay=[float(r['actual_maximum_root_error_db']) for r in rows if math.isfinite(float(r['actual_raw_off_cn0']))]
        replay_file=path+'.replay.csv'
        try:
            for r in csv.DictReader(open(replay_file)):replay.append(float(r['maximum_root_error_db']))
        except FileNotFoundError:pass
        endpoint=[]
        for g in grid:
            k=bisect.bisect_right(ordered,g);endpoint.append(cp(k,n,per_test))
        cell=[]
        for left,right in zip(grid,grid[1:]):
            k=bisect.bisect_right(ordered,right)-bisect.bisect_right(ordered,left)
            cell.append((k,cp(k,n,per_test)[0]))
        report={'input':path,'scope':'conditional fixed-waveform real-AWGN sufficient statistic; one coherent correct-bit hypothesis',
            'trials':n,'invalid_or_nonmonotone_draws':invalid,'changed_root_draws':sum(x!=y for x,y in zip(off,on)),
            'potential_lack_of_fit_activation_draws':sum(int(r['active_intervals'])>0 for r in rows),
            'exception_population_probability_upper':exception_upper,
            'exception_scope':'unbracketed, nonmonotone or nonnested draw; observed-none does not mean population-none',
            'activation_probability_bound_per_fixed_correct_bit':eps,
            'maximum_waveform_mismatch_energy':max(float(r['mismatch_energy_at_upper']) for r in rows),
            'activation_allowance':float(rows[0]['activation_allowance']),
            'actual_pcm_replayed_noise_seeds_initial':len([r for r in rows if math.isfinite(float(r['actual_raw_off_cn0']))]),
            'maximum_observed_production_root_error_db':max(replay,default=None),
            'production_numerical_scope':'selected raw/automatic float-PCM replay only; no universal floating-point error proof',
            'statistic_grid_family_confidence':1-a.family_alpha,'per_test_alpha':per_test,'quantiles':[]}
        for p in (.9,.99):
            lo=[g for g,(_,upper) in zip(grid,endpoint) if upper+exception_upper<p]
            hi=[g for g,(lower,_) in zip(grid,endpoint) if lower-exception_upper>=p]
            lower=max(lo) if lo else None;upper=min(hi) if hi else None
            q={'p':p,'guard_off_cn0':quantile(ordered,p),'guard_on_cn0':quantile(guarded,p),
                'estimated_additional_cn0_db':quantile(guarded,p)-quantile(ordered,p),
                'baseline_population_quantile_grid_ci':[lower,upper]}
            if off==on:q['paired_bootstrap_delta_ci']=[0.,0.];q['bootstrap_note']='All empirical paired roots tie; this interval alone does not bound population loss.'
            else:
                rng=random.Random(8841+int(p*100));differences=[]
                for _ in range(a.bootstrap):
                    indices=[rng.randrange(n) for _ in range(n)]
                    differences.append(quantile(sorted(on[i] for i in indices),p)-quantile(sorted(off[i] for i in indices),p))
                differences.sort();q['paired_bootstrap_delta_ci']=[quantile(differences,a.family_alpha/2),quantile(differences,1-a.family_alpha/2)]
            eligible=[(k,mass) for i,(k,mass) in enumerate(cell) if lower is not None and upper is not None and grid[i+1]>=lower and grid[i]<=upper+2*a.cell_db]
            minimum=min((mass for _,mass in eligible),default=0.)
            qualified=analytical and lower is not None and upper is not None and upper+2*a.cell_db<=a.grid_high and minimum>eps+2*exception_upper
            q['minimum_simultaneous_cell_mass_lower']=minimum
            q['minimum_cell_count']=min((k for k,_ in eligible),default=0)
            q['conditional_statistic_additional_cn0_upper_db']=2*a.cell_db if qualified else None
            q['bound_basis']='Every [q,q+2*cell_width] contains a whole grid cell whose true mass exceeds pointwise activation bound plus twice the simultaneous exceptional-draw allowance; exact-binomial intervals and union bound.'
            q['production_sensitivity_qualification']='Conditional statistic bound plus selected actual PCM replay; not full-bank or universal production qualification.'
            report['quantiles'].append(q)
        reports.append(report)
    with open(a.output,'w') as output:json.dump({'family_tests':family_tests,'reports':reports},output,indent=2,allow_nan=False)
    print(json.dumps({'files':len(reports),'draws':sum(r['trials'] for r in reports),'output':a.output}))

if __name__=='__main__':main()
