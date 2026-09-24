"""Independent back-substitution encoder for pinned IEEE QC matrices.

Based on the encoder in tavildar/LDPC (MIT, copyright Saurabh Tavildar);
see third_party/ldpc/WIFI-LICENSE and README for provenance. Production uses
Gaussian elimination, not this back-substitution algorithm.
"""
from pathlib import Path
import re, hashlib, json
root=Path(__file__).resolve().parents[4]
text=(root/'third_party/ldpc/wifi_tables.hpp').read_text()
fixtures=[]
for n in (648,1296,1944):
    for numerator,denominator in ((1,2),(2,3),(3,4)):
        body=re.search(r'H_%d_%d_%d\[\d+\]\[24\] = \{(.*?)\};'%(n,numerator,denominator),text,re.S)[1]
        base=[[int(x) for x in re.findall(r'-?\d+',row)] for row in re.findall(r'\{([^}]+)\}',body)]
        z=n//24;k=n*numerator//denominator;m=n-k
        rows=[[col*z+(i+shift)%z for col,shift in enumerate(base[br]) if shift>=0] for br in range(len(base)) for i in range(z)]
        source=bytes((i*73+i//7+0x5a)&255 for i in range(k//8))
        bits=[(b>>(7-j))&1 for b in source for j in range(8)]+[0]*(n-len(source)*8)
        parity=[sum(bits[j] for j in row if j<k)%2 for row in rows]
        for j in range(z):bits[k+j]=sum(parity[j::z])%2
        for i,row in enumerate(rows):parity[i]^=sum(bits[j] for j in row if k<=j<k+z)%2
        for col in range(k+z,n,z):
            for row in range(z):
                bits[col+row]=parity[col+row-k-z]
                parity[col+row-k]^=parity[col+row-k-z]
        assert all(sum(bits[j] for j in row)%2==0 for row in rows)
        fixtures.append(dict(n=n,rate=f'{numerator}/{denominator}',k=k,source_bytes=len(source),sha256=hashlib.sha256(bytes(bits)).hexdigest()))
print(json.dumps(fixtures,indent=2))
