#!/usr/bin/env python3
"""Uniform Gray-QAM AWGN bit-information boundaries; not finite-code FER."""
import sys, math, json
from pathlib import Path
sys.path.insert(0, str(Path.cwd() / "tools"))
from acoustic_bicm_analysis import qam_bit_information
rows=[]
for q in (16,64):
    for name,r in [("3/4",.75),("8/9",8/9),("9/10",.9)]:
        lo,hi=0.,30.
        for _ in range(36):
            mid=(lo+hi)/2
            if qam_bit_information(q,mid,192).mean()<r: lo=mid
            else: hi=mid
        rows.append(dict(qam=q,rate=name,
            gaussian_bound_esn0_db=10*math.log10(2**(math.log2(q)*r)-1),
            bicm_ideal_esn0_db=(lo+hi)/2))
print(json.dumps(rows,indent=2))
