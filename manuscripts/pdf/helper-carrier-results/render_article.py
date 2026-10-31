"""Render the evidence-led manuscript without running or modifying the receiver."""
from pathlib import Path
import json, re, math, html, zipfile, base64
from reportlab.lib import colors
from reportlab.lib.enums import TA_JUSTIFY, TA_LEFT, TA_CENTER
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, KeepTogether, Preformatted
from reportlab.graphics.shapes import Drawing, Rect, Line, String, Polygon
from reportlab.graphics.charts.lineplots import LinePlot
from reportlab.graphics.charts.barcharts import VerticalBarChart
from reportlab.graphics import renderPDF, renderSVG
import manuscript as m

HERE = Path(__file__).resolve().parent
DATA = json.loads((HERE/'paper-data.json').read_text())
BLUE = colors.HexColor('#1c5d7a')
ORANGE = colors.HexColor('#b96927')
INK = colors.HexColor('#202a30')
GRAY = colors.HexColor('#5c6971')
LIGHT = colors.HexColor('#e8edf0')
FONTDIR = Path('/usr/share/fonts/truetype/liberation')
for family, prefix in [('Serif','LiberationSerif'), ('Sans','LiberationSans')]:
    for suffix, f in [('', 'Regular'), ('-Bold', 'Bold'), ('-Italic', 'Italic'), ('-BoldItalic', 'BoldItalic')]:
        pdfmetrics.registerFont(TTFont(family+suffix, str(FONTDIR/(prefix+'-'+f+'.ttf'))))
    pdfmetrics.registerFontFamily(family, normal=family, bold=family+'-Bold', italic=family+'-Italic', boldItalic=family+'-BoldItalic')

def txt(d, x, y, text, size=8, color=INK, anchor='start', font='Sans'):
    d.add(String(x, y, text, fontName=font, fontSize=size, fillColor=color, textAnchor=anchor))

def arrow(d,x1,y1,x2,y2,color=GRAY,dash=None):
    d.add(Line(x1,y1,x2,y2,strokeColor=color,strokeWidth=.85,strokeDashArray=dash))
    angle=math.atan2(y2-y1,x2-x1); length=5; width=2.5
    bx=x2-length*math.cos(angle); by=y2-length*math.sin(angle)
    d.add(Polygon([x2,y2,bx+width*math.sin(angle),by-width*math.cos(angle),bx-width*math.sin(angle),by+width*math.cos(angle)],fillColor=color,strokeColor=None))

def box(d,x,y,w,h,lines,fill='#f3f6f8',border=BLUE):
    d.add(Rect(x,y,w,h,rx=3,ry=3,fillColor=colors.HexColor(fill),strokeColor=border,strokeWidth=.8))
    top=y+h/2+(len(lines)-1)*5
    for i,line in enumerate(lines): txt(d,x+w/2,top-10*i-2,line,8.2,anchor='middle')

def architecture():
    d=Drawing(504,278)
    txt(d,0,264,'A. Receiver evidence and complete output',9.4,font='Sans-Bold')
    box(d,0,192,106,47,['Captured mixture','sample index + UTC'])
    box(d,132,213,162,35,['Guarded pilot observations','joint clock / fresh hop phase'])
    box(d,132,166,162,35,['Auxiliary reversal profiles','pooled source calendar'])
    box(d,332,188,172,48,['Complete-dwell buffer','own-hop mixing + interpolation','capture-time output metadata'])
    arrow(d,106,222,132,230);arrow(d,106,202,132,183)
    arrow(d,294,230,332,223);arrow(d,294,183,332,198)
    # The sample path is separate from the estimation arrows.
    d.add(Line(53,192,53,151,strokeColor=GRAY,strokeWidth=.8))
    d.add(Line(53,151,418,151,strokeColor=GRAY,strokeWidth=.8))
    arrow(d,418,151,418,188)
    txt(d,175,155,'Captured samples retained; no confidence gating',7.8)
    box(d,332,94,172,34,['All released samples','phase / clock / confidence / dates'])
    d.add(Line(494,188,494,142,strokeColor=GRAY,strokeWidth=.8))
    d.add(Line(494,142,471,142,strokeColor=GRAY,strokeWidth=.8))
    arrow(d,471,142,471,128)
    box(d,0,94,258,34,['Evaluator only: probe, clean twin and truth','scores output; no feedback to receiver'],fill='#faf5ee',border=ORANGE)
    arrow(d,332,111,258,111,color=ORANGE,dash=[3,2])
    txt(d,0,73,'B. Capture time remains distinct from release time',9.4,font='Sans-Bold')
    y=43
    d.add(Line(18,y,475,y,strokeColor=GRAY,strokeWidth=1))
    for x,label in [(18,'0 s'),(291,'36 s'),(361,'38 s'),(429,'40 s')]:
        d.add(Line(x,y-4,x,y+5,strokeColor=GRAY));txt(d,x,y-16,label,8,anchor='middle')
    txt(d,130,51,'Helper observations',8,anchor='middle')
    txt(d,291,62,'Freeze result',7.8,anchor='middle')
    txt(d,362,51,'Lead ends',7.8,anchor='middle')
    txt(d,430,62,'Capture ends',7.8,anchor='middle')
    arrow(d,429,y,492,y)
    txt(d,503,28,'Drain',7.8,anchor='end')
    txt(d,0,7,'Timeline is schematic; later evidence may correct only output still buffered.',7.4,color=GRAY)
    return d

def line_plot(d,x,y,w,h,series,xrange,yrange,xtick,ytick):
    lp=LinePlot();lp.x=x;lp.y=y;lp.width=w;lp.height=h
    lp.data=series;lp.joinedLines=1
    for i,col in enumerate([BLUE,ORANGE]):
        lp.lines[i].strokeColor=col;lp.lines[i].strokeWidth=1.7
    for axis in [lp.xValueAxis,lp.yValueAxis]:
        axis.strokeColor=GRAY;axis.strokeWidth=.6;axis.labels.fontName='Sans';axis.labels.fontSize=7.5
    lp.xValueAxis.tickDown=3
    lp.xValueAxis.valueMin=xrange[0];lp.xValueAxis.valueMax=xrange[1];lp.xValueAxis.valueStep=xtick
    lp.yValueAxis.valueMin=yrange[0];lp.yValueAxis.valueMax=yrange[1];lp.yValueAxis.valueStep=ytick
    lp.yValueAxis.visibleGrid=1;lp.yValueAxis.gridStrokeColor=LIGHT;lp.yValueAxis.gridStrokeWidth=.5
    d.add(lp)
    return lp

def timing():
    d=Drawing(504,252)
    txt(d,0,238,'A. Reserved 180 dB holdouts (n = 32)',9.1,font='Sans-Bold')
    txt(d,277,238,'B. Loss screen (n = 8 per loss)',9.1,font='Sans-Bold')
    def cdf(vals):
        result=[(0,0)]
        for i,v in enumerate(sorted(vals)):
            result.extend([(v,i/len(vals)),(v,(i+1)/len(vals))])
        result.append((3.5,1))
        return result
    a=[abs(r['error_us'])/1000 for r in DATA['heldout']]
    b=[r['radius_us']/1000 for r in DATA['heldout']]
    line_plot(d,35,62,196,154,[cdf(a),cdf(b)],(0,3.5),(0,1),.5,.25)
    d.add(Line(35+196/3.5,62,35+196/3.5,216,strokeColor=GRAY,strokeWidth=.7,strokeDashArray=[2,2]))
    txt(d,35+196/3.5+3,203,'1 ms',7,color=GRAY)
    txt(d,135,36,'Timing magnitude (ms)',8,anchor='middle')
    txt(d,35,222,'Empirical fraction',7.8,color=GRAY)
    rows=sorted(DATA['screen'],key=lambda r:r['loss_db'])
    rms=[(r['loss_db'],r['profile36_qualified_error_rms_us']/1000) for r in rows]
    rad=[(r['loss_db'],r['profile36_qualified_radius_median_us']/1000) for r in rows]
    line_plot(d,302,62,190,154,[rms,rad],(160,180),(0,2.5),5,.5)
    txt(d,398,36,'Path loss (dB)',8,anchor='middle');txt(d,302,222,'Timing (ms)',7.8,color=GRAY)
    d.add(Line(3,15,20,15,strokeColor=BLUE,strokeWidth=2));txt(d,25,12,'Absolute error (left); RMS error (right)',7.7)
    d.add(Line(284,15,301,15,strokeColor=ORANGE,strokeWidth=2));txt(d,306,12,'Conditional interval half-width',7.7)
    return d

def boundary():
    d=Drawing(504,245)
    txt(d,0,232,'Complete output: phase error by physical distance from a hop',9.4,font='Sans-Bold')
    bc=VerticalBarChart();bc.x=40;bc.y=62;bc.width=452;bc.height=145
    bc.data=[[r['nominal_deg'] for r in DATA['distance']],[r['pooled_deg'] for r in DATA['distance']]]
    bc.categoryAxis.categoryNames=['<0.5 ms','0.5-2 ms','2-8 ms','8-30 ms','30-60 ms','Interior']
    bc.categoryAxis.labels.fontName='Sans';bc.categoryAxis.labels.fontSize=7.7
    bc.categoryAxis.strokeColor=GRAY;bc.categoryAxis.labels.boxAnchor='n'
    bc.valueAxis.valueMin=0;bc.valueAxis.valueMax=80;bc.valueAxis.valueStep=20
    bc.valueAxis.labels.fontName='Sans';bc.valueAxis.labels.fontSize=8;bc.valueAxis.strokeColor=GRAY
    bc.valueAxis.visibleGrid=1;bc.valueAxis.gridStrokeColor=LIGHT;bc.valueAxis.gridStrokeWidth=.5
    bc.bars[0].fillColor=BLUE;bc.bars[1].fillColor=ORANGE;bc.bars.strokeColor=None
    bc.barSpacing=2;bc.groupSpacing=12
    d.add(bc)
    txt(d,40,213,'RMS phase (degrees)',8,color=GRAY)
    txt(d,266,34,'Distance from physical transition',8,anchor='middle')
    d.add(Rect(83,12,10,7,fillColor=BLUE,strokeColor=None));txt(d,98,12,'Nominal calendar',8)
    d.add(Rect(285,12,10,7,fillColor=ORANGE,strokeColor=None));txt(d,300,12,'Pooled inversion',8)
    return d

FIGURES={'architecture':architecture(), 'timing':timing(), 'boundary':boundary()}
from visual_figures import figures as extra_figures
FIGURES.update(extra_figures(HERE,txt,arrow,box))
for name,d in FIGURES.items():
    renderPDF.drawToFile(d,str(HERE/'figures'/f'{name}.pdf'))
    renderSVG.drawToFile(d,str(HERE/'figures'/f'{name}.svg'))
    svgpath=HERE/'figures'/f'{name}.svg'
    svg=svgpath.read_text()
    for raster in (HERE/'figures').glob('waterfall-*.png'):
        svg=svg.replace(str(raster),'data:image/png;base64,'+base64.b64encode(raster.read_bytes()).decode())
    svgpath.write_text(svg)

styles={
 'body':ParagraphStyle('body',fontName='Serif',fontSize=10.5,leading=13.6,alignment=TA_JUSTIFY,spaceAfter=6,allowWidows=0,allowOrphans=0,textColor=INK),
 'title':ParagraphStyle('title',fontName='Serif-Bold',fontSize=23,leading=26,spaceAfter=9,textColor=INK),
 'subtitle':ParagraphStyle('subtitle',fontName='Serif',fontSize=15,leading=19,spaceAfter=11,textColor=BLUE),
 'byline':ParagraphStyle('byline',fontName='Sans',fontSize=8.5,leading=12,spaceAfter=16,textColor=GRAY),
 'abstract':ParagraphStyle('abstract',fontName='Serif',fontSize=10.2,leading=13.5,alignment=TA_JUSTIFY,spaceAfter=11),
 'keywords':ParagraphStyle('keywords',fontName='Serif-Italic',fontSize=9.3,leading=12,spaceAfter=14,textColor=GRAY),
 'h':ParagraphStyle('h',fontName='Serif-Bold',fontSize=13.2,leading=16.4,spaceBefore=13,spaceAfter=7,keepWithNext=True,textColor=INK),
 'sub':ParagraphStyle('sub',fontName='Serif-Bold',fontSize=11.1,leading=14,spaceBefore=8,spaceAfter=5,keepWithNext=True,textColor=INK),
 'eq':ParagraphStyle('eq',fontName='Serif',fontSize=11,leading=16,alignment=TA_CENTER,spaceBefore=3,spaceAfter=9),
 'caption':ParagraphStyle('caption',fontName='Serif',fontSize=9.3,leading=12,spaceBefore=6,spaceAfter=11,textColor=INK),
 'tablecap':ParagraphStyle('tablecap',fontName='Serif',fontSize=9.3,leading=12,spaceBefore=8,spaceAfter=6,keepWithNext=True),
 'cell':ParagraphStyle('cell',fontName='Serif',fontSize=9,leading=11.4,spaceAfter=0),
 'headcell':ParagraphStyle('headcell',fontName='Sans-Bold',fontSize=8.3,leading=10.6,spaceAfter=0,textColor=INK),
 'ref':ParagraphStyle('ref',fontName='Serif',fontSize=9.2,leading=12.2,spaceAfter=6,leftIndent=17,firstLineIndent=-17),
 'code':ParagraphStyle('code',fontName='Courier',fontSize=7.4,leading=10,spaceBefore=5,spaceAfter=10,backColor=colors.HexColor('#f3f5f6'),borderPadding=7),
}

def page(canvas,doc):
    canvas.saveState()
    if doc.page>1:
        canvas.setFont('Sans',7.4);canvas.setFillColor(GRAY)
        canvas.drawString(54,758,'HELPER-CARRIER RECEPTION: METHODS AND RESULTS')
        canvas.drawRightString(558,758,'Research manuscript · 31 October 2026')
        canvas.setStrokeColor(LIGHT);canvas.setLineWidth(.6);canvas.line(54,750,558,750)
    canvas.setFillColor(GRAY);canvas.setFont('Sans',8)
    canvas.drawString(54,31,'Helper Carrier | Comparative development results')
    canvas.drawRightString(558,31,str(doc.page))
    canvas.restoreState()

story=[Paragraph(m.TITLE,styles['title']),Paragraph(m.SUBTITLE,styles['subtitle']),
       Paragraph('Research manuscript | Communications and statistical signal processing<br/>Evidence through 31 October 2026',styles['byline']),
       Paragraph('<b>Abstract.</b> '+m.ABSTRACT,styles['abstract']),
       Paragraph('<b>Keywords:</b> '+m.KEYWORDS,styles['keywords'])]

def bold_caption(text):
    return re.sub(r'^(Table \d+\.|Figure \d+\.)',r'<b>\1</b>',text)

for block in m.BLOCKS:
    kind=block[0]
    if kind in ['p','h','sub','eq','ref']:
        story.append(Paragraph(block[1],styles['body' if kind=='p' else kind]))
    elif kind=='table':
        _,cap,headers,rows,widths=block
        capstyle=styles['tablecap']
        if cap.startswith(('Table 5.','Table 7.')):
            capstyle=ParagraphStyle('longtablecap',parent=capstyle,keepWithNext=False)
        story.append(Paragraph(bold_caption(cap),capstyle))
        cells=[[Paragraph(c,styles['headcell']) for c in headers]]+[[Paragraph(c,styles['cell']) for c in row] for row in rows]
        t=Table(cells,colWidths=widths,repeatRows=1,hAlign='LEFT',rowSplitRange=(2,-2))
        t.setStyle(TableStyle([
            ('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),6),('RIGHTPADDING',(0,0),(-1,-1),6),
            ('TOPPADDING',(0,0),(-1,-1),6),('BOTTOMPADDING',(0,0),(-1,-1),6),
            ('LINEABOVE',(0,0),(-1,0),.8,INK),('LINEBELOW',(0,0),(-1,0),.6,INK),
            ('LINEBELOW',(0,-1),(-1,-1),.6,INK),('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,colors.HexColor('#f4f6f7')])]))
        story.extend([t,Spacer(1,9)])
    elif kind=='figure':
        story.append(KeepTogether([Spacer(1,7),FIGURES[block[1]],Paragraph(bold_caption(block[2]),styles['caption'])]))
    elif kind=='code':
        story.append(KeepTogether([Preformatted(block[1],styles['code'],maxLineLength=105)]))

class PaperDoc(SimpleDocTemplate):
    def afterFlowable(self, flowable):
        if isinstance(flowable,Paragraph) and flowable.style.name=='h':
            title=flowable.getPlainText()
            key='section-'+str(getattr(self,'section_count',0))
            self.section_count=getattr(self,'section_count',0)+1
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(title,key,level=0,closed=False)

doc=PaperDoc(str(HERE/'helper-carrier-results.pdf'),pagesize=(612,792),
    leftMargin=48,rightMargin=48,topMargin=54,bottomMargin=51,
    title=m.TITLE+': '+m.SUBTITLE,author='',subject='Evidence-led synthesis of standalone Helper Carrier experiments',
    creator='ReportLab; editable manuscript and evidence manifest included')
doc.build(story,onFirstPage=page,onLaterPages=page)

def html_text(text):
    text=text.replace('<super>','<sup>').replace('</super>','</sup>')
    text=re.sub(r'<link href="([^"]+)"(?: color="[^"]+")?>',r'<a href="\1">',text).replace('</link>','</a>')
    text=re.sub(r'<font[^>]*>','',text).replace('</font>','')
    return text
out=['<!doctype html><html lang="en"><meta charset="utf-8"><title>'+m.TITLE+'</title>',
'''<style>body{max-width:850px;margin:50px auto;padding:0 25px;color:#202a30;font:18px/1.55 Georgia,serif}h1{font-size:36px;line-height:1.15;margin-bottom:10px}h2{margin-top:36px}h3{font-size:20px}.subtitle{font-size:24px;color:#1c5d7a}.meta,figcaption{font-size:15px}figure{margin:30px 0}img{width:100%}table{width:100%;border-collapse:collapse;font-size:15px;margin:12px 0 25px}th,td{padding:9px;text-align:left;vertical-align:top;border-bottom:1px solid #d9e0e4}th{border-top:2px solid #202a30}tbody tr:nth-child(even){background:#f4f6f7}caption{text-align:left;font-size:16px;margin:10px 0}pre{font-size:12px;background:#f3f5f6;padding:15px;overflow:auto}.eq{text-align:center}.ref{font-size:15px}a{color:#16496a}@media print{body{max-width:none;margin:0;font-size:11pt}h2,h3{break-after:avoid}figure,tr{break-inside:avoid}}</style>''',
f'<h1>{m.TITLE}</h1><div class="subtitle">{m.SUBTITLE}</div><p class="meta">Research manuscript | Evidence through 31 October 2026</p>',
'<p><b>Abstract.</b> '+html_text(m.ABSTRACT)+'</p><p><i><b>Keywords:</b> '+m.KEYWORDS+'</i></p>']
for b in m.BLOCKS:
    if b[0] in ['p','h','sub','eq','ref']:
        tag={'p':'p','h':'h2','sub':'h3','eq':'p','ref':'p'}[b[0]]
        out.append(f'<{tag} class="{b[0]}">{html_text(b[1])}</{tag}>')
    elif b[0]=='figure':out.append(f'<figure><img src="figures/{b[1]}.svg" alt="{html.escape(b[2])}"><figcaption>{html_text(b[2])}</figcaption></figure>')
    elif b[0]=='table':
        out.append('<table><caption>'+html_text(b[1])+'</caption><thead><tr>'+''.join('<th>'+html_text(c)+'</th>' for c in b[2])+'</tr></thead><tbody>')
        out.extend('<tr>'+''.join('<td>'+html_text(c)+'</td>' for c in row)+'</tr>' for row in b[3]);out.append('</tbody></table>')
    elif b[0]=='code':out.append('<pre>'+html.escape(b[1])+'</pre>')
out.append('</html>');(HERE/'article.html').write_text('\n'.join(out))
print(f'Rendered PDF, HTML and {len(FIGURES)} scientific figures.')
