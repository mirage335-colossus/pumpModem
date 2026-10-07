"""Editable vector-like motion design. No generated media belongs in Git."""
from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
from functools import lru_cache
import math,textwrap
W,H=1600,900
BG='#0b1423';PANEL='#132238';INK='#f3f6fb';MUTED='#a5b4c9';MINT='#64e4c0';BLUE='#77b9ff';GOLD='#ffcd87';LINE='#29405c'
@lru_cache(None)
def font(size,bold=False,mono=False):
    family='DejaVuSansMono' if mono else 'DejaVuSans'
    return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/'+family+('-Bold' if bold else '')+'.ttf',size)
def tx(d,xy,s,size=25,color=INK,bold=False,anchor=None,mono=False):d.text(xy,s,font=font(size,bold,mono),fill=color,anchor=anchor)
def wrap(d,s,box,size=28,color=INK,bold=False,spacing=1.3):
    x,y,width=box;line=''
    for word in s.split():
        trial=(line+' '+word).strip()
        if d.textlength(trial,font=font(size,bold))>width and line:tx(d,(x,y),line,size,color,bold);y+=int(size*spacing);line=word
        else:line=trial
    if line:tx(d,(x,y),line,size,color,bold);y+=int(size*spacing)
    return y
def rounded(d,box,fill=PANEL,outline=LINE,r=20,width=2):d.rounded_rectangle(box,r,fill=fill,outline=outline,width=width)
def arrow(d,p,q,color=MINT,width=3):
    d.line((*p,*q),fill=color,width=width);angle=math.atan2(q[1]-p[1],q[0]-p[0]);d.polygon([q,(q[0]-14*math.cos(angle-.5),q[1]-14*math.sin(angle-.5)),(q[0]-14*math.cos(angle+.5),q[1]-14*math.sin(angle+.5))],fill=color)
def computer(d,x,y,w=330,label='COMPUTER'):
    h=w*.57;rounded(d,(x,y,x+w,y+h),BG,MUTED,12,3);d.line((x+w*.5,y+h,x+w*.5,y+h+24),fill=MUTED,width=3);d.line((x+w*.34,y+h+24,x+w*.66,y+h+24),fill=MUTED,width=3);tx(d,(x+w/2,y+h+49),label,20,MUTED,anchor='mm')
def wave(d,x,y,w,t,color=MINT,noise=False,amp=25):
    pts=[]
    for i in range(int(w)):
        a=(math.sin(i*.13-t*4)+.5*math.sin(i*.329+t*7)+.3*math.sin(i*.733-t*3)) if noise else math.sin(i*.057-t*3)
        pts.append((x+i,y+a*amp))
    d.line(pts,fill=color,width=2)
def pill(d,x,y,label,color=MINT):
    width=d.textlength(label,font=font(19,True))+34;rounded(d,(x,y,x+width,y+40),PANEL,LINE,20);tx(d,(x+17,y+9),label,19,color,True)
def base(scene,t,index,count):
    im=Image.new('RGB',(W,H),BG);d=ImageDraw.Draw(im)
    tx(d,(55,30),'DATA PUMP',19,MINT,True);tx(d,(1545,40),f'{index+1:02d} / {count:02d}',17,MUTED,anchor='rm',mono=True)
    d.line((55,69,1545,69),fill=LINE,width=1)
    return im,d

def compose(scene,t,native,media,index,count):
    im,d=base(scene,t,index,count);kind=scene['kind'];ease=min(1,t/1.0);ease=ease*ease*(3-2*ease)
    if kind=='native':
        tx(d,(55,114),scene['eyebrow'],20,MINT,True)
        y=wrap(d,scene['title'],(55,173,490),48,bold=True);y=wrap(d,scene['subtitle'],(55,y+17,490),36,MUTED)
        if scene['id']=='fast':
            for j,(a,b) in enumerate([('30 kB','text file'),('Encrypted','authenticated on receipt'),('Speakers / mic','channel profile')]):
                tx(d,(57,y+55+j*103),a,31,MINT,True);tx(d,(57,y+96+j*103),b,22,MUTED)
        else:
            for j,(a,b) in enumerate([('2 mW','transmit power'),('150 dB','modeled path loss'),('Received: 1','actual simulation result')]):
                tx(d,(57,y+42+j*100),a,33,MINT,True);tx(d,(57,y+85+j*100),b,22,MUTED)
        im.paste(native.resize((884,691),Image.Resampling.LANCZOS),(662,92));d=ImageDraw.Draw(im);d.rectangle((660,90,1548,785),outline=LINE,width=2)
        if scene['id']=='robust':
            crop=native.crop((18,433,440,490)).resize((844,114),Image.Resampling.LANCZOS);im.paste(crop,(682,476));d=ImageDraw.Draw(im);d.rectangle((680,474,1528,592),outline=MINT,width=2);pill(d,950,620,'RECEIVED SNAPSHOT')
        tx(d,(1546,803),scene['note'],17,MUTED,anchor='rm')
    else:
        tx(d,(55,103),scene['eyebrow'],20,MINT,True)
        if kind=='opening':
            tx(d,(55,170),scene['title'],72,INK,True);tx(d,(55,263),scene['subtitle'],46,MUTED)
            computer(d,150,437,315,'SENDING COMPUTER');computer(d,1130,437,315,'RECEIVING COMPUTER')
            for yy,c in [(464,MINT),(523,BLUE),(582,GOLD)]:wave(d,535,yy,520,t,c,noise=yy==582,amp=15)
            for j,label in enumerate(['SAFE DATA TRANSFER','ORDINARY HARDWARE','TESTED PRACTICES']):pill(d,134+j*487,727,label,[MINT,BLUE,GOLD][j])
        elif kind=='isolation':
            tx(d,(55,163),scene['title'],62,INK,True);tx(d,(55,244),scene['subtitle'],43,MUTED)
            computer(d,142,387,310,'COMPUTER A');computer(d,1148,387,310,'COMPUTER B')
            rounded(d,(586,367,1014,627),PANEL,MINT);tx(d,(800,416),'AUDIO BOUNDARY',25,MINT,True,anchor='mm');wave(d,622,489,356,t,amp=23);tx(d,(800,563),'text + files',25,INK,anchor='mm')
            arrow(d,(472,490),(566,490));arrow(d,(1034,490),(1128,490))
            for j,(a,b) in enumerate([('Isolating KVM','keyboard · mouse · display'),('Authenticator','limited credential storage'),('Data Pump','intentional data transfer')]):
                x=55+j*508;rounded(d,(x,692,x+470,787));tx(d,(x+23,705),a,26,MINT,True);tx(d,(x+23,748),b,19,MUTED)
        elif kind=='rates':
            tx(d,(55,165),scene['title'],62,INK,True);tx(d,(55,247),scene['subtitle'],42,MUTED)
            rounded(d,(55,357,647,679),PANEL,BLUE);rounded(d,(953,357,1545,679),PANEL,MINT)
            common=t<scene.get('captions',[{'end':7}])[0]['end']+.2
            if common:
                tx(d,(88,385),'OLIVIA · COMMON TEXT MODES',23,BLUE,True);tx(d,(88,451),'15–29',76,INK,True);tx(d,(90,548),'words/min · selected common modes',23,MUTED);tx(d,(90,619),'Morse practice: 10–35 words/min',22,MUTED)
            else:
                tx(d,(88,385),'OLIVIA · 4 TONES / 2 kHz',23,BLUE,True);tx(d,(88,451),'~109',76,INK,True);tx(d,(90,548),'bit/s · nominal source rate',25,MUTED);tx(d,(90,619),'Faster Olivia variant',23,MUTED)
            tx(d,(985,385),'DATA PUMP · FAST MODEM',23,MINT,True);tx(d,(986,451),'~28.5k',76,INK,True);tx(d,(987,548),'bit/s · recorded transfer goodput',23,MUTED);tx(d,(987,619),'500 kB received exactly in 140.5 s',21,MUTED)
            tx(d,(800,480),'→' if common else '260×',55,GOLD,True,anchor='mm');arrow(d,(691,553),(909,553),GOLD);tx(d,(800,613),'TEXT-MODE ROOTS' if common else 'RATE RATIO',18,GOLD,True,anchor='mm')
            tx(d,(55,728),'A text-modem generation more than twenty years old.' if common else 'More than two orders of magnitude in the recorded rate comparison.',27,INK,True)
            tx(d,(55,777),'Different bandwidths and channel conditions · higher-throughput acoustic profile',22,MUTED)
        elif kind=='private':
            tx(d,(55,165),scene['title'],62,INK,True);tx(d,(55,247),scene['subtitle'],43,MUTED)
            for j,(a,b,c) in enumerate([('Keyed transmitter','encrypted data',MINT),('Noise-like signal','low-power channel',GOLD),('Keyed receiver','known private template',BLUE)]):
                x=55+j*508;rounded(d,(x,376,x+470,687),PANEL,c);tx(d,(x+235,410),a,27,c,True,anchor='mm');wave(d,x+35,511,400,t+j,c,noise=True,amp=31);tx(d,(x+235,634),b,24,MUTED,anchor='mm')
            at=scene.get('audio_excerpt',{}).get('at',1e8);audible=at<=t<at+4
            pill(d,55,724,'MODEM AUDIO · LISTEN' if audible else 'LOW PROBABILITY OF INTERCEPT',GOLD if audible else MINT)
            if audible:tx(d,(1544,754),'Separate waveform sample · same simulation settings',17,MUTED,anchor='rm')
            else:tx(d,(1544,788),'Modeled link: signal 16.5 dB below in-band noise · ~0.1 dB noise rise',21,MUTED,anchor='rm')
        elif kind=='development':
            tx(d,(55,165),scene['title'],57,INK,True);tx(d,(55,243),scene['subtitle'],40,MUTED)
            labels=[('Portable interfaces','Desktop · browser · terminal'),('Regression tests','Normal paths + failure cases'),('Data flow + cancellation','Bounded queues · safe shutdown'),('Reproducible builds','Retained tools + dependencies'),('Verified releases','Test the delivered software'),('Parallel development','Preserve each contribution')]
            for j,(a,b) in enumerate(labels):
                x=55+(j%3)*508;y=351+(j//3)*220;rounded(d,(x,y,x+470,y+189))
                tx(d,(x+25,y+20),f'0{j+1}',20,MINT,True,mono=True);tx(d,(x+25,y+66),a,25,INK,True);tx(d,(x+25,y+116),b,19,MUTED)
                d.line((x+25,y+162,x+25+420*ease,y+162),fill=MINT,width=2)
        elif kind=='team':
            tx(d,(55,165),scene['title'],62,INK,True);tx(d,(55,247),scene['subtitle'],43,MUTED)
            for j,label in enumerate(['Boundaries','Failure cases','Working practices']):
                x=55+j*508;rounded(d,(x,382,x+470,563));tx(d,(x+29,410),f'0{j+1}',22,MINT,True,mono=True);tx(d,(x+29,459),label,34,INK,True)
                for n in range(3):d.line((x+30,528+n*8,x+30+(100+n*70)*ease,528+n*8),fill=LINE,width=3)
                arrow(d,(x+235,580),(800,655))
            rounded(d,(409,675,1191,775),PANEL,MINT);tx(d,(800,724),'REUSABLE CODE + TESTED PRACTICES',24,MINT,True,anchor='mm')
        elif kind=='foundation':
            tx(d,(55,153),scene['title'],56,INK,True);tx(d,(55,227),scene['subtitle'],42,MUTED)
            for j,(name,label) in enumerate([('foundation-fltk.png','Desktop'),('foundation-wasm.png','Browser'),('foundation-terminal.png','Terminal')]):
                x=55+j*508;rounded(d,(x,321,x+470,613));pic=asset(media/name).copy();pic.thumbnail((434,232),Image.Resampling.LANCZOS);im.paste(pic,(x+235-pic.width//2,335+(232-pic.height)//2));d=ImageDraw.Draw(im);tx(d,(x+235,588),label,23,MINT,True,anchor='mm')
            rounded(d,(55,636,1545,786),PANEL,MINT)
            tx(d,(82,655),'THE AMBITION · COMPARABLE DEVELOPMENT BREADTH',20,MINT,True)
            tx(d,(90,697),'~2 weeks',40,INK,True);arrow(d,(379,725),(519,725));tx(d,(551,697),'a few hours',40,MINT,True);tx(d,(977,715),'potentially ~1 hour',32,GOLD,True)
            tx(d,(1544,805),'Team estimate and future target · actual reference-project screenshots',17,MUTED,anchor='rm')
        elif kind=='closing':
            tx(d,(55,165),scene['title'],68,INK,True);tx(d,(55,257),scene['subtitle'],44,MUTED)
            rounded(d,(55,410,729,666),PANEL,MINT);rounded(d,(871,410,1545,666),PANEL,BLUE);arrow(d,(750,536),(850,536))
            tx(d,(90,447),'DATA PUMP',37,MINT,True);wrap(d,'Public capabilities on existing hardware.',(90,514,590),29,MUTED)
            tx(d,(906,447),'SOFTWARE FOUNDATION',32,BLUE,True);wrap(d,'A stronger starting point for future projects.',(906,514,590),29,MUTED)
            tx(d,(55,728),'github.com/mirage335-colossus/pumpModem',24,INK)
            tx(d,(55,772),'github.com/mirage335-colossus/software-foundation',24,INK)
    d=ImageDraw.Draw(im)
    d.rectangle((0,820,W,H),fill=BG);d.line((55,821,1545,821),fill=LINE,width=1)
    caption=next((c['text'] for c in scene.get('captions',[]) if c['start']<=t<c['end']),'')
    lines=textwrap.wrap(caption,112)
    assert len(lines)<=2,(scene['id'],caption)
    for n,line in enumerate(lines):tx(d,(W/2,841+n*28),line,22,INK,anchor='mm')
    d.rectangle((0,H-4,int(W*(index+t/scene['duration'])/count),H),fill=MINT)
    return im
@lru_cache(None)
def asset(p):return Image.open(p).convert('RGB')
