"""Editable vector-style tutorial illustrations. No product photos or remote assets."""
from functools import lru_cache
from pathlib import Path
import math,textwrap
from PIL import Image,ImageDraw,ImageFont

W,H=1440,1080
BG='#0b1220';PANEL='#142438';LINE='#34516b';INK='#eef5ff';MUTED='#a5b8cc';CYAN='#71e4c8';BLUE='#68baff';GOLD='#f7cf82'
URL='github.com/mirage335-colossus/pumpModem'
PANGRAM='the quick brown fox jumps over the lazy dog'

@lru_cache(maxsize=40)
def font(size,bold=False,mono=False):
    name='DejaVuSansMono' if mono else 'DejaVuSans'
    return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/'+name+('-Bold' if bold else '')+'.ttf',size)

def text(d,xy,s,size=28,fill=INK,bold=False,anchor=None,mono=False):d.text(xy,s,font=font(size,bold,mono),fill=fill,anchor=anchor)
def paragraph(d,xy,s,width=48,size=26,fill=MUTED,spacing=12):
    for n,line in enumerate(textwrap.wrap(s,width)):text(d,(xy[0],xy[1]+n*(size+spacing)),line,size,fill)
def box(d,xy,fill=PANEL,outline=LINE,r=20,width=2):d.rounded_rectangle(xy,radius=r,fill=fill,outline=outline,width=width)
def arrow(d,start,end,color=CYAN,width=5):
    d.line((*start,*end),fill=color,width=width);ang=math.atan2(end[1]-start[1],end[0]-start[0]);size=16
    d.polygon([end,(end[0]-size*math.cos(ang-.5),end[1]-size*math.sin(ang-.5)),(end[0]-size*math.cos(ang+.5),end[1]-size*math.sin(ang+.5))],fill=color)
def laptop(d,x,y,w=310,label='Computer',content='Data Pump'):
    h=w*.62;box(d,(x,y,x+w,y+h),fill='#08121d',outline=BLUE,r=13,width=3)
    text(d,(x+w/2,y+h*.46),content,max(16,int(w*.07)),CYAN,anchor='mm',mono=True)
    d.polygon([(x-20,y+h+5),(x+w+20,y+h+5),(x+w+38,y+h+30),(x-38,y+h+30)],fill=LINE)
    text(d,(x+w/2,y+h+65),label,23,MUTED,anchor='mm')
def waveform(d,x0,y,w,t,color=CYAN,amp=32):
    points=[(x0+i,y+amp*math.sin(i*.11+t*3)*(.5+.5*math.sin(i*.018))) for i in range(w)]
    d.line(points,fill=color,width=3)
def speaker(d,x,y):
    box(d,(x,y,x+76,y+130),r=10);d.ellipse((x+12,y+47,x+64,y+99),outline=CYAN,width=3);d.ellipse((x+28,y+17,x+48,y+37),fill=BLUE)
def microphone(d,x,y):
    box(d,(x+12,y,x+48,y+77),r=18,outline=CYAN);d.arc((x,y+12,x+60,y+103),0,180,fill=CYAN,width=4);d.line((x+30,y+102,x+30,y+139),fill=LINE,width=6);d.line((x+4,y+140,x+56,y+140),fill=LINE,width=6)
def key(d,x,y,w=180):
    box(d,(x,y,x+w,y+88),fill='#232c37',outline=MUTED,r=24);box(d,(x+w-12,y+16,x+w+50,y+72),fill='#ced6df',outline='#798c9f',r=5)
    for i in range(4):d.rectangle((x+w+3+i*10,y+30,x+w+8+i*10,y+58),fill=GOLD)
    d.ellipse((x+24,y+20,x+72,y+68),outline=GOLD,width=4);d.ellipse((x+40,y+36,x+56,y+52),fill=GOLD)
def kvm(d,x,y,w=270):
    box(d,(x,y,x+w,y+110),fill='#273342',outline=MUTED,r=12)
    text(d,(x+20,y+17),'SECURE KVM',22,INK,bold=True)
    for i in range(4):box(d,(x+22+i*59,y+57,x+62+i*59,y+84),fill='#080f18',r=3);d.ellipse((x+32+i*59,y+93,x+39+i*59,y+100),fill=CYAN)
def scanner(d,x,y):
    d.polygon([(x,y),(x+175,y),(x+200,y+92),(x+85,y+108),(x+120,y+244),(x+66,y+259),(x+3,y+87)],fill='#2f4358',outline=MUTED)
    box(d,(x+17,y+18,x+155,y+70),fill='#07121c',outline=CYAN,r=8,width=3)
    d.line((x+25,y+42,x+145,y+42),fill=GOLD,width=4)
    d.line((x+90,y+253,x+130,y+297,x+286,y+297),fill=MUTED,width=6)
    box(d,(x+284,y+284,x+337,y+310),fill='#bac7d4',r=3)

def phone(t,w=420,h=900):
    im=Image.new('RGB',(420,900),BG);d=ImageDraw.Draw(im)
    box(d,(8,8,412,858),fill='#040a12',outline='#53738f',r=44,width=3);box(d,(18,20,402,846),fill='#101d2e',r=35)
    text(d,(37,49),'09:41',17,MUTED,bold=True);text(d,(348,49),'▰',20,MUTED)
    text(d,(210,878),'Illustrated phone · SMS draft only',15,MUTED,anchor='mm')
    if t<22:
        box(d,(24,84,396,153),fill='#1c3e59',outline=None,r=0);text(d,(38,99),'AndFlmsg',29,bold=True);text(d,(39,135),'PSK31 · RX · 1500 Hz',14,BLUE)
        text(d,(36,183),'Terminal       Messages       Modem',16,MUTED);d.line((302,209,383,209),fill=BLUE,width=3)
        state='Listening' if t<13 else 'MODEM OFF';text(d,(40,239),state,25,CYAN if t<13 else MUTED,bold=True)
        box(d,(37,290,204,333),r=8);text(d,(120,311),'MODEM ON/OFF',14,BLUE,anchor='mm');box(d,(218,290,382,333),r=8);text(d,(300,311),'W.FALL ON',15,BLUE,anchor='mm')
        text(d,(39,361),'Received modem text',16,MUTED,bold=True);box(d,(36,389,384,548),fill='#091521',r=10)
        count=round(len(PANGRAM)*min(1,max(0,t)/11));shown=PANGRAM[:count]
        lines=textwrap.wrap(shown,29)
        if 15<=t<19:
            for n,line in enumerate(lines):d.rectangle((47,405+n*30,365,434+n*30),fill='#244a64')
        for n,line in enumerate(lines):text(d,(49,405+n*30),line,18,mono=True)
        if 16<=t<19:box(d,(178,347,283,385),fill='#354d64',r=10);text(d,(230,366),'Copy',18,INK,anchor='mm')
        text(d,(39,577),'Waterfall · illustrated',15,MUTED)
        for row in range(130):
            yy=612+row
            d.line((45,yy,377,yy),fill=(12,25+row%8,40+row%12))
            shift=round(2*math.sin(row*.15+t));d.line((207+shift,yy,215+shift,yy),fill=(71,147+row%80,169+row%65),width=3)
        text(d,(210,783),'Copied to clipboard' if t>=19 else ('Select and copy' if t>=13 else 'Receiving text…'),17,CYAN,anchor='mm')
    else:
        text(d,(45,107),'‹  Messages',30,bold=True);text(d,(80,151),'New SMS draft',17,MUTED)
        box(d,(37,191,384,237),r=9);text(d,(48,204),'To:   No recipient',17,MUTED)
        text(d,(210,320),'SMS draft only',25,BLUE,bold=True,anchor='mm')
        text(d,(210,363),'Review before sending',18,MUTED,anchor='mm')
        box(d,(37,435,384,545),fill='#0a1623',outline=BLUE,r=10)
        if t>=25:paragraph(d,(50,451),PANGRAM,29,18,INK,12)
        elif t>=23:box(d,(48,397,150,436),fill='#354d64',r=9);text(d,(99,416),'Paste',18,INK,anchor='mm')
        for row,letters in enumerate(['qwertyuiop','asdfghjkl','zxcvbnm']):
            for i,c in enumerate(letters):
                x=39+(10-len(letters))*16+i*34;y=611+row*48;box(d,(x,y,x+29,y+40),fill='#2b3e52',r=5);text(d,(x+14,y+20),c,17,anchor='mm')
        box(d,(104,765,305,800),fill='#22364b',r=7);text(d,(205,782),'space',16,MUTED,anchor='mm')
    return im.resize((w,h),Image.Resampling.LANCZOS)

def illustration(scene,t,media):
    im=Image.new('RGB',(W,H),BG);d=ImageDraw.Draw(im);kind=scene['kind']
    if kind in ('purpose','setup','isolation','optical'):
        text(d,(80,130),{'purpose':'Text and files, carried by sound','setup':'Match both ends. Try a short message.','isolation':'Audio is a signal, not a device connection.','optical':'One-way fiber, with compatible hardware'}[kind],43,bold=True)
        laptop(d,105,360,350,'Sending computer','Hello');laptop(d,980,360,350,'Receiving computer','Hello' if t>5 else 'Listening…')
        if kind=='optical':
            arrow(d,(495,469),(932,469),BLUE,8);text(d,(714,419),'OPTICAL AUDIO',24,BLUE,anchor='mm');text(d,(714,524),'One-way hardware path',22,MUTED,anchor='mm')
        else:
            speaker(d,477,408);microphone(d,883,401);waveform(d,578,470,270,t)
            text(d,(716,550),'AUDIO',22,CYAN,anchor='mm')
        if kind=='purpose':
            box(d,(120,747,1320,901));text(d,(720,790),'Choose text or a file → Transmit → Check the result',29,INK,anchor='mm');text(d,(720,846),'Speakers + microphone  •  or a suitable audio cable',25,MUTED,anchor='mm')
        elif kind=='setup':
            for n,(a,b) in enumerate([('1','Fast Modem on both computers'),('2','Choose sound input and output'),('3','Match the Speakers / mic · short profile')]):text(d,(150,755+n*56),a+'    '+b,28,CYAN if n==int(t/6)%3 else INK)
        elif kind=='isolation':
            for x,title,body in [(115,'No peer bus connection','Audio does not expose the sender as a keyboard or network adapter.'),(740,'You choose the next step','Copy text or save a file. Nothing is opened or executed automatically.')]:
                box(d,(x,742,x+585,927));text(d,(x+26,770),title,28,CYAN,bold=True);paragraph(d,(x+26,818),body,43,22)
        else:
            paragraph(d,(250,755),'Compatible S/PDIF audio interfaces or custom optical data diode hardware. The physical device provides the one-way isolation.',68,28)
    elif kind=='firewalls':
        text(d,(80,128),'Allowed traffic still reaches connected software.',40,bold=True)
        box(d,(95,290,470,654));text(d,(282,346),'Network traffic',29,CYAN,bold=True,anchor='mm')
        for n in range(4):
            y=410+n*43;box(d,(150,y,415,y+29),fill='#29445a',r=7);text(d,(282,y+14),'allowed data' if n%2 else 'packet',16,MUTED,anchor='mm',mono=True)
        box(d,(589,316,826,617),fill='#21374a',outline=BLUE);text(d,(707,446),'FIREWALL',27,BLUE,bold=True,anchor='mm');text(d,(707,497),'Filters traffic',21,MUTED,anchor='mm')
        arrow(d,(485,466),(573,466));arrow(d,(842,466),(940,466))
        box(d,(955,290,1344,654));text(d,(1150,347),'Receiving software',28,CYAN,bold=True,anchor='mm');paragraph(d,(987,421),'Allowed input is still parsed. Implementation flaws can be exploited.',24,27)
        box(d,(95,731,1345,931),outline=GOLD);text(d,(720,779),'Critical build systems may require stronger isolation.',28,GOLD,anchor='mm');text(d,(720,842),'Neither a firewall nor Data Pump makes a file safe to open.',27,INK,anchor='mm')
    elif kind=='layers':
        text(d,(80,125),'Shared peripherals. Authentication. Data transfer.',39,bold=True)
        for n,(title,sub) in enumerate([('Secure KVM','Optical diodes + isolated peripherals'),('Hardware authenticator','FIPS YubiKey: small memory, dedicated security functions'),('Data Pump','Controlled text and file transfer')]):
            x=70+n*445;box(d,(x,268,x+410,887),outline=CYAN if int(t/9)%3==n else LINE);text(d,(x+28,300),f'0{n+1}',29,CYAN,bold=True)
            if n==0:kvm(d,x+66,440)
            elif n==1:key(d,x+80,446)
            else:waveform(d,x+47,490,312,t);text(d,(x+205,569),'TEXT → AUDIO → FILE',18,BLUE,anchor='mm',mono=True)
            text(d,(x+205,644),title,28,INK,bold=True,anchor='mm');paragraph(d,(x+28,707),sub,25,23)
        text(d,(720,952),'These complementary tools make an isolating workstation.',24,MUTED,anchor='mm')
    elif kind in ('windows','linux'):
        text(d,(75,130),'Get Data Pump from its GitHub project',40,bold=True);box(d,(75,213,1365,290),fill='#172f45');text(d,(720,251),URL+'/releases/latest',27,CYAN,anchor='mm',mono=True)
        if kind=='windows':
            steps=[('01','Download','Windows x86_64 · Rev · .zip'),('02','Extract All','Keep bin / lib / share together'),('03','Open bin','Run datapump-gui.exe')]
            for n,(a,b,c) in enumerate(steps):
                y=357+n*167;box(d,(110,y,1330,y+135),outline=CYAN if min(2,int(t/7))==n else LINE);text(d,(150,y+43),a,38,CYAN,bold=True);text(d,(265,y+25),b,30,bold=True);text(d,(265,y+76),c,27,MUTED,mono=n==2)
            text(d,(720,940),'Windows 10 (1903+) or Windows 11 · x64 portable application',23,MUTED,anchor='mm')
        else:
            box(d,(100,335,1338,551));text(d,(130,363),'1   Complete the repository setup guide',29,bold=True)
            text(d,(130,420),URL+'/blob/main/docs/releases.md',20,CYAN,mono=True);text(d,(130,453),'#debian-installation-from-github-releases',20,CYAN,mono=True)
            text(d,(130,505),'Verify the signing key → add the package source',22,MUTED)
            box(d,(100,581,1338,833),fill='#07131d');text(d,(140,610),'sudo apt update',29,CYAN,mono=True);text(d,(140,673),'sudo apt install datapump-fltk',29,CYAN,mono=True);text(d,(140,736),'datapump-fltk',29,CYAN,mono=True)
            text(d,(720,891),'FLTK suits embedded systems and computers without graphics acceleration.',23,MUTED,anchor='mm');text(d,(720,936),'Current x64 package: Debian 12+ · Ubuntu 24.04+ · see guide for other architectures',21,MUTED,anchor='mm')
    elif kind=='terminal':
        text(d,(80,125),'A terminal is enough for the interface.',42,bold=True);text(d,(80,199),'Recovery · bootstrap environments · systems without a graphical desktop',25,MUTED)
        box(d,(75,267,1365,824),fill='#06111b',outline=BLUE,r=17)
        screen=media/'tui-screen.txt'
        if screen.exists():
            for n,line in enumerate(screen.read_text().splitlines()[:27]):text(d,(94,288+n*19),line[:110],17,CYAN,mono=True)
        else:
            for n,line in enumerate(['$ datapump-tui-ncurses','','Data Pump','Modem: Fast Modem','Message: Hello','[Transmit text]    [Attach file]','','Listening for signals…']):text(d,(110,310+n*47),line,25,CYAN,mono=True)
        text(d,(100,867),'datapump-tui-ncurses',30,CYAN,mono=True);text(d,(100,918),'Tab: move    Arrows: select    Enter: activate    F1: help',25,MUTED)
    elif kind=='qr':
        text(d,(80,125),'Display → scan → type and submit',43,bold=True)
        box(d,(74,246,607,795));text(d,(341,279),'QR brightness: Normal',24,INK,anchor='mm')
        qr=Image.open(media/'command-qr.pbm').convert('RGB').resize((370,370),Image.Resampling.NEAREST);im.paste(qr,(155,336));text(d,(340,747),'uname -a',26,CYAN,anchor='mm',mono=True)
        scanner(d,702,354);arrow(d,(610,505),(681,432),GOLD,4);text(d,(805,706),'USB QR scanner',25,MUTED,anchor='mm');text(d,(803,757),'One-way optical input',20,CYAN,anchor='mm')
        box(d,(1035,307,1364,768));text(d,(1200,342),'Receiving computer',20,MUTED,anchor='mm');box(d,(1056,398,1342,572),fill='#07131d',r=8)
        text(d,(1074,447),'$ '+('uname -a' if t>8 else '_'),24,CYAN,mono=True)
        if t>14:
            text(d,(1074,484),'Linux …',21,MUTED,mono=True);text(d,(1074,526),'$ _',24,CYAN,mono=True)
        if t>8:text(d,(1200,652),'Automatic Enter enabled',18,INK,anchor='mm')
        paragraph(d,(90,850),'Focus the intended window. Keyboard mode types the command; automatic Enter submits it. The command content still comes from the sender.',86,25)
        text(d,(720,964),'No return data path through the optical scan · illustrated command entry',22,CYAN,anchor='mm')
    elif kind=='radio':
        text(d,(80,130),'Explore low-power links between fixed locations',40,bold=True)
        for x in [200,1210]:
            d.line((x,319,x,677),fill=BLUE,width=8);d.line((x-80,675,x+80,675),fill=LINE,width=8);d.line((x-90,372,x+90,372),fill=BLUE,width=6);d.line((x-70,332,x+70,332),fill=BLUE,width=6)
        waveform(d,360,462,690,t,amp=22);text(d,(720,598),'Low power · long receive times · suitable antennas',25,CYAN,anchor='mm')
        for y,s in [(756,'Sub-9 kHz is one area for experiments with suitable hardware.'),(820,'Choose frequencies and modes permitted for your radio service.'),(884,'Safe data transfer remains the development priority.')]:text(d,(720,y),s,25,MUTED,anchor='mm')
    elif kind=='end':
        text(d,(95,180),'Start with one short message.',55,bold=True)
        for n,s in enumerate(['Install Data Pump','Match both ends','Transmit and check','Then try a file']):
            y=348+n*119;box(d,(94,y,1340,y+89));text(d,(130,y+27),f'{n+1:02}    '+s,29,CYAN if int(t/3)%4==n else INK,bold=True)
        text(d,(720,939),URL,27,CYAN,anchor='mm',mono=True)
    return im
