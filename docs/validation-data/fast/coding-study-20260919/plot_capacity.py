"""Run from the repository root with ReportLab installed.

Writes /tmp/fast-capacity.pdf. Rasterize separately with:
pdftoppm -png -scale-to 1200 -singlefile /tmp/fast-capacity.pdf /tmp/fast-capacity
The archived PNG was inspected after this rendering step.
"""
import csv, math
from pathlib import Path
from reportlab.graphics.shapes import Drawing, String, Line
from reportlab.graphics.charts.lineplots import LinePlot
from reportlab.graphics import renderPDF
from reportlab.lib.colors import HexColor
folder=Path('docs/validation-data/fast/coding-study-20260919')
rows=list(csv.DictReader((folder/'capacity.csv').open()))
select=lambda kind,order: [(float(r['snr_in_band_db']),float(r['bit_gmi'])*15) for r in rows if r['mapping']==kind and int(r['order'])==order and 14<=float(r['snr_in_band_db'])<=30]
xs=[x for x,y in select('qam',256)]
series=[[(x,18*math.log2(1+10**(x/10))) for x in xs],[(x,15*math.log2(1+1.2*10**(x/10))) for x in xs],select('qam',1024),select('qam',256),select('apsk',256)]
colors=['#243645','#6e8294','#267861','#3b78b2','#c26140']
names=['Occupied-band Shannon bound','Gaussian symbol-stream bound','Gray 1024-QAM, bitwise rate','Gray 256-QAM, bitwise rate','Current 256-APSK, bitwise rate']
d=Drawing(800,530)
d.add(String(45,494,'Coding is only one part of the gap to capacity',fontName='Helvetica-Bold',fontSize=19,fillColor=HexColor('#172938')))
d.add(String(45,471,'Wire reference: 18 kHz bandwidth, 15,000 symbols/s, ideal AWGN',fontSize=12,fillColor=HexColor('#455469')))
p=LinePlot();p.x=74;p.y=170;p.width=676;p.height=270;p.data=series;p.joinedLines=1
p.xValueAxis.valueMin=14;p.xValueAxis.valueMax=30;p.xValueAxis.valueSteps=list(range(14,31,2))
p.yValueAxis.valueMin=0;p.yValueAxis.valueMax=190;p.yValueAxis.valueSteps=list(range(0,181,30))
for i,c in enumerate(colors):p.lines[i].strokeColor=HexColor(c);p.lines[i].strokeWidth=2.3
p.lines[0].strokeDashArray=[5,3];p.lines[1].strokeDashArray=[2,3]
d.add(p)
d.add(String(410,135,'SNR in the occupied band (dB)',textAnchor='middle',fontSize=11))
d.add(String(15,330,'kbit/s',fontSize=10))
for i,(c,name) in enumerate(zip(colors,names)):
 y=114-i*17
 d.add(Line(80,y,105,y,strokeColor=HexColor(c),strokeWidth=2.5))
 d.add(String(115,y-4,name,fontSize=10))
d.add(String(80,13,'Before finite-code losses, markers, pilots, integrity fields and source formatting.',fontSize=9,fillColor=HexColor('#455469')))
renderPDF.drawToFile(d,'/tmp/fast-capacity.pdf')
