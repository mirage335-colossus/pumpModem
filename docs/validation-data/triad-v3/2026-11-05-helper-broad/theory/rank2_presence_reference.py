#!/usr/bin/env python3
"""Favorable rank-two known-noise matched-subspace presence references.

This is a numeric hypothesis test, not a modem decoder or receiver simulation.
The noise-only energy has Gamma(shape=2, scale=1). Under signal energy rho
(projected complex energy / noise variance), twice that statistic is
noncentral chi-square(df=4, lambda=2*rho). Multiplicity uses Bonferroni, so no
independence assumption is made. Extra model/duration alternatives increase M.
"""
from __future__ import annotations
import json
import math
from scipy.optimize import brentq
from scipy.special import ndtr, ndtri
from scipy.stats import gamma, ncx2

PD = (0.90, 0.99)
ALPHAS = (0.05, 1.0e-4)
SEARCHES = (1, 21_320)

def rank2_energy(pd: float, alpha: float, multiplicity: int) -> dict:
    per_test_alpha = alpha / multiplicity
    energy_threshold = float(gamma.ppf(1.0 - per_test_alpha, a=2.0, scale=1.0))
    def detection(rho: float) -> float:
        return float(ncx2.sf(2.0 * energy_threshold, df=4, nc=2.0 * rho))
    rho = float(brentq(lambda value: detection(value) - pd, 0.0, 1000.0))
    return dict(pd=pd, overall_alpha=alpha, multiplicity=multiplicity,
                per_test_alpha=per_test_alpha, threshold_gamma2=energy_threshold,
                rho=rho, detection_probability=detection(rho))


def main() -> None:
    c_no_60 = 10.0 ** (-60.0 / 10.0)
    cases = (
        dict(name='EME360', source_energy_seconds=18.97,
             symmetric_difference_fraction=0.0264439, cn0_db_hz=1.5,
             duration_seconds=26.4, correct_admit=0, wrong_admit=0, missed=0),
        dict(name='EME1200', source_energy_seconds=6.37393,
             symmetric_difference_fraction=0.3462517, cn0_db_hz=1.5,
             duration_seconds=8.8, correct_admit=4, wrong_admit=2, missed=10),
        dict(name='HF_minus6', source_energy_seconds=23.0743,
             symmetric_difference_fraction=0.4686364, cn0_db_hz=-6.0,
             duration_seconds=23.2, correct_admit=None, wrong_admit=None, missed=None),
    )
    results=[]
    for x in cases:
        c=10.0 ** (x['cn0_db_hz']/10.0)
        rho_bit=c*x['source_energy_seconds']*x['symmetric_difference_fraction']
        p_error=.5*math.erfc(math.sqrt(2.0*rho_bit)/math.sqrt(2.0))
        rho_source=c*x['source_energy_seconds']
        results.append(dict(**x, cn0_linear_hz=c, rho_source=rho_source,
                            rho_symmetric_difference=rho_bit,
                            favorable_known_complex_bit_error=p_error))
    # A doubled duration at the same active C/N0 and same source efficiency doubles rho.
    eme1200=results[1]
    rho2=2.0*eme1200['rho_symmetric_difference']
    results[1]['double_duration_seconds']=2.0*eme1200['duration_seconds']
    results[1]['double_duration_known_complex_bit_error']=.5*math.erfc(math.sqrt(2.0*rho2)/math.sqrt(2.0))
    helper=[]
    for name, ratio_db in (('HF_fixed_ratio', 10.0), ('EME_fixed_ratio', 10*math.log10(15000/40))):
        helper_c_db=-60.0+ratio_db
        helper_c=10.0**(helper_c_db/10)
        row=dict(name=name, helper_data_power_ratio_db=ratio_db,
                 helper_cn0_db_hz=helper_c_db,
                 rho_36s=helper_c*36.0)
        for pd,alpha in ((.90,.01),(.99,1e-4)):
            row[f'days_for_PD{int(pd*100)}_alpha{alpha:g}']=6.5084691830804715/helper_c/86400 if pd==.90 else 18.27321511953556/helper_c/86400
        helper.append(row)
    rank2=[rank2_energy(pd, alpha, m)
                 for m in SEARCHES for alpha in ALPHAS for pd in PD]
    oracle_p_error=.000874
    oracle_rho=(ndtri(1-oracle_p_error)**2)/2.0
    output=dict(schema='triad-helper-broad-rank2-energy-reference-v1',
      model=dict(rank_complex=2, h0='Gamma(shape=2, scale=1)',
                 twice_statistic_h1='noncentral-chi-square(df=4, lambda=2*rho)',
                 rho_definition='projected complex signal energy / proper-complex noise variance',
                 multiplicity_rule='Bonferroni alpha/M; valid without independence',
                 scope='known channel-subspace, known noise, no nuisance fitting; presence only'),
      rank2_presence=rank2, source_efficiency_cases=results,
      failed_seed=dict(known_received_means_oracle_pe=.000874,
                       implied_pairwise_distance_over_4sigma2=oracle_rho,
                       qualification='One specific known-means oracle, not the unknown-sync receiver distribution.'),
      fixed_power_helper=helper,
      cno_minus60=dict(linear_hz=c_no_60, rho_8s=c_no_60*8, rho_16s=c_no_60*16,
                       rho_38days=c_no_60*38*86400))
    print(json.dumps(output, indent=2, allow_nan=False))

if __name__=='__main__':
    main()
