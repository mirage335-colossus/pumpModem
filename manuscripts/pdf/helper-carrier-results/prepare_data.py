"""Extract existing evidence for the manuscript; never run a receiver or modify it."""
from pathlib import Path
import hashlib, json, csv, subprocess

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
EVIDENCE = REPO / 'local-evidence/helper-continuation/inversion-profile'

def read(name):
    return json.loads((EVIDENCE / name).read_text())

held = read('heldout-results.json')
summary = read('heldout-summary.json')
screen = read('screen-summary.json')
candidate = next(g for g in summary['groups'] if g['method'] == 'pooled-inversion')
baseline = next(g for g in summary['groups'] if g['method'] == 'nominal')
cases = [r['rows']['helper_complete_study'][0] for r in held['records'] if r['method'] == 'pooled-inversion']
assert len(cases) == 32 and len({r['seed'] for r in cases}) == 32
assert all(r['profile36_qualified'] and abs(r['profile36_error_us']) <= r['profile36_radius_us'] for r in cases)
screen_rows = [{k:g[k] for k in ('loss_db','cases','profile36_qualified_error_rms_us',
    'profile36_qualified_error_worst_us','profile36_qualified_radius_median_us')}
    for g in screen['groups'] if g['method'] == 'pooled-inversion']

bins = ['lt_0p5ms','0p5_to_2ms','2_to_8ms','8_to_30ms','30_to_60ms','interior']
distance = []
for name in bins:
    key = 'helper_complete_physical_boundary/continuous_synthetic_clean_twin/' + name + '/all'
    distance.append({'bin':name,'nominal_deg':baseline['metrics'][key]['clean_phase_rms_deg'],
        'pooled_deg':candidate['metrics'][key]['clean_phase_rms_deg']})

out = {
    'cutoff':'2026-10-31',
    'screen':screen_rows,
    'heldout':[{'seed':r['seed'],'error_us':r['profile36_error_us'],'radius_us':r['profile36_radius_us'],
        'first_qualification_s':r['profile_first_qualified_capture_end_seconds']} for r in cases],
    'distance':distance,
    'historical_matched_ablation':[
        {'method':'Four-window baseline','frequency_rms_hz':.01658,'half_width_hz':.21393,'phase_median_deg':3.08},
        {'method':'Covariance + joint + dual','frequency_rms_hz':.01762,'half_width_hz':.12211,'phase_median_deg':2.30},
        {'method':'Partitioned Hann + joint + dual','frequency_rms_hz':.01783,'half_width_hz':.12520,'phase_median_deg':2.29}],
    'cpu':read('cpu-isolated/results.json'),
    'provenance_note':'Historical ablation transcribed from docs/helper-continuation.md, Matched 40 W results. Other plot data extracted from existing campaign JSON; no new receiver experiments.'
}
(HERE/'paper-data.json').write_text(json.dumps(out,indent=2)+'\n')
with (HERE/'timing-holdouts.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(out['heldout'][0]));writer.writeheader();writer.writerows(out['heldout'])
paths=['docs/helper-continuation.md','docs/helper-frequency-refinement.md','docs/helper-tracking.md',
 'docs/helper-measurement.md','docs/helper-carrier.md','docs/helper-eme-measurement.md','docs/helper-live-probe.md',
 'src/helper_inversion_profile.cpp','include/datapump/helper_inversion_profile.hpp',
 'src/helper_complete_conditioner.cpp','tests/test_helper_complete_study.cpp']
paths += ['local-evidence/helper-continuation/inversion-profile/'+n for n in
    ('heldout-results.json','heldout-summary.json','screen-summary.json','cpu-isolated/results.json','reserved-inputs.json')]
manifest={'article_date':'2026-10-31','checkout_head_at_authoring':subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip(),
 'campaign_binary_sha256':'df88e4cfa9c93c7338ad15feed3fca6a4f561daf796c68990425d77c6c8ece98',
 'accounting_binary_sha256':'049d8b01b1c41df9260c8d58c5b15accd46af73d4710c1839770ae8e90b16e67',
 'files':{p:hashlib.sha256((REPO/p).read_bytes()).hexdigest() for p in paths}}
(HERE/'evidence-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Extracted 32 held-out timing records and five matched loss-screen summaries.')
