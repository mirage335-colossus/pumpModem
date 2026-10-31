"""Scientific format diagrams and archived-data spectrograms for the paper."""
import hashlib, math, json
import numpy as np
from PIL import Image as PILImage
from reportlab.lib import colors
from reportlab.graphics.shapes import Drawing, Rect, Line, String, PolyLine, Circle, Image

BLUE=colors.HexColor('#1c5d7a');ORANGE=colors.HexColor('#b96927')
TEAL=colors.HexColor('#218b83');INK=colors.HexColor('#202a30')
GRAY=colors.HexColor('#5c6971');LIGHT=colors.HexColor('#e8edf0')

def figures(here,txt,arrow,box):
    def line(d,xx,yy,col=BLUE,width=1.4):
        points=[v for pair in zip(xx,yy) for v in pair]
        d.add(PolyLine(points,strokeColor=col,strokeWidth=width,fillColor=None))
    def axis(d,x,y,w,ticks,lo,hi,label):
        d.add(Line(x,y,x+w,y,strokeColor=GRAY,strokeWidth=.6))
        for val in ticks:
            u=x+w*(val-lo)/(hi-lo);d.add(Line(u,y,u,y-3,strokeColor=GRAY,strokeWidth=.6))
            txt(d,u,y-13,f'{val:g}',7.5,anchor='middle')
        txt(d,x+w/2,y-27,label,8,anchor='middle')

    def format_plot():
        d=Drawing(504,408)
        txt(d,0,393,'A. Frequency positions and roles',9.5,font='Sans-Bold')
        x=55;w=425; y=310
        axis(d,x,y,w,[0,550,950,1600,2400],0,2600,'Nominal baseband frequency (Hz)')
        for f,db,col,label in [(550,-3,TEAL,'Low pilot'),(950,-9,ORANGE,'Auxiliary'),(2400,0,BLUE,'High pilot')]:
            u=x+w*f/2600;ht=(db+14)*4
            d.add(Line(u,y,u,y+ht,strokeColor=col,strokeWidth=2.8));txt(d,u,y+ht+16,label,8.1,anchor='middle')
            txt(d,u,y+ht+5,f'{db:g} dB',7.5,anchor='middle',color=col)
        a=x+w*1420/2600;b=x+w*1780/2600
        d.add(Rect(a,y,b-a,26,fillColor=colors.HexColor('#edf0f2'),strokeColor=GRAY,strokeDashArray=[2,2]))
        txt(d,(a+b)/2,y+39,'Separate probe',7.9,anchor='middle');txt(d,(a+b)/2,y+29,'360 Hz band',7.2,anchor='middle')
        txt(d,0,261,'B. One 400 ms FHSS dwell',9.5,font='Sans-Bold')
        xx=lambda t:70+420*t/400
        d.add(Rect(xx(20),218,xx(380)-xx(20),22,fillColor=colors.HexColor('#edf4f6'),strokeColor=None))
        txt(d,280,245,'Pilot estimation: 20-380 ms; output uses the complete dwell',7.8,anchor='middle')
        t=np.linspace(0,400,801);u=np.clip(np.minimum(t,400-t)/4,0,1);env=u**3*(10+u*(-15+6*u))
        line(d,[xx(v) for v in t],219+19*env,BLUE)
        txt(d,0,228,'Pilots',8.2);txt(d,0,218,'4 ms edges',7.2,color=GRAY)
        d.add(Line(70,183,490,183,strokeColor=LIGHT,strokeWidth=.6))
        t=np.linspace(0,212,425);u=np.clip(t/8,0,1);gate=u**3*(10+u*(-15+6*u))
        z=np.clip((t-152)/8,0,1);s=lambda q:q**4*(35+q*(-84+q*(70-20*q)))
        v=np.where(t<160,1-s(z),-s(np.clip((t-160)/8,0,1)))*gate
        line(d,[xx(vv) for vv in t],183+16*v,ORANGE)
        d.add(Rect(xx(212),165,xx(392)-xx(212),36,fillColor=colors.HexColor('#f7efe6'),strokeColor=ORANGE,strokeWidth=.5))
        for z in range(232,392,20):d.add(Line(xx(z),165,xx(z),201,strokeColor=colors.HexColor('#d9b48f'),strokeWidth=.5))
        txt(d,(xx(212)+xx(392))/2,186,'Optional metadata',7.8,anchor='middle')
        txt(d,(xx(212)+xx(392))/2,174,'9 × 20 ms APSK symbols',7.5,anchor='middle')
        d.add(Rect(xx(392),165,xx(400)-xx(392),36,fillColor=LIGHT,strokeColor=None))
        txt(d,0,186,'Auxiliary',8.2);txt(d,0,175,'signed envelope',7.1,color=GRAY)
        for v,label in [(0,'0'),(160,'160'),(212,'212'),(392,'392')]:
            d.add(Line(xx(v),160,xx(v),155,strokeColor=GRAY));txt(d,xx(v),145,label,7.5,anchor='middle')
        txt(d,280,131,'Time within dwell (ms); final auxiliary ramp: 392-400 ms',7.8,anchor='middle')
        txt(d,0,112,'C. Why phase inversion needs a phase-sensitive measurement',9.3,font='Sans-Bold')
        x=40;y=62;w=225
        t=np.linspace(148,172,241);u=np.where(t<160,1-s(np.clip((t-152)/8,0,1)),-s(np.clip((t-160)/8,0,1)))
        line(d,x+(t-148)*w/24,y+24*u,ORANGE);line(d,x+(t-148)*w/24,y+24*u*u,BLUE)
        d.add(Line(x,y,x+w,y,strokeColor=LIGHT));d.add(Circle(x+w/2,y,2.5,fillColor=colors.white,strokeColor=INK))
        axis(d,x,32,w,[152,160,168],148,172,'Marker time (ms)')
        for yy,label in [(86,'+1'),(62,'0'),(38,'-1')]:txt(d,30,yy-2,label,7.5,anchor='end')
        d.add(Line(290,83,309,83,strokeColor=ORANGE,strokeWidth=1.7));txt(d,314,80,'Signed auxiliary envelope',8)
        d.add(Line(290,66,309,66,strokeColor=BLUE,strokeWidth=1.7));txt(d,314,63,'Power (envelope squared)',8)
        txt(d,290,43,'Before: phase 0. After: phase π.',8)
        txt(d,290,29,'At zero amplitude, phase is undefined.',8)
        txt(d,290,15,'Both straight flanks have equal power.',8)
        return d

    def hop_plot():
        d=Drawing(504,248);x=55;w=432;n=8;dx=w/n
        raw=hashlib.shake_256(b'Helper-paper-illustrative-hops-v1').digest(128)
        ch=[b%50 for b in raw if b<250][:n]
        txt(d,0,232,'A. Illustrative RF tuning commands',9.5,font='Sans-Bold')
        y0=158;scale=39
        for f in [-1,0,1]:
            yy=y0+f*scale;d.add(Line(x,yy,x+w,yy,strokeColor=LIGHT));txt(d,x-8,yy-2,f'{f:+g}',8,anchor='end')
        txt(d,55,216,'RF offset from 30 MHz (MHz)',8,color=GRAY)
        for i,c in enumerate(ch):
            off=(c-24.5)*.05;yy=y0+off*scale
            d.add(Line(x+i*dx+3,yy,x+(i+1)*dx-3,yy,strokeColor=BLUE,strokeWidth=3))
            txt(d,x+(i+.5)*dx,yy+7,f'ch {c}',7.1,anchor='middle')
        txt(d,0,95,'B. Nominal audio coordinates after tuning to each hop',9.5,font='Sans-Bold')
        ys=[(550,32,TEAL),(950,46,ORANGE),(2400,76,BLUE)]
        for f,y,col in ys:
            txt(d,46,y-2,str(f),8,anchor='end')
            for i in range(n):d.add(Line(x+i*dx+2,y,x+(i+1)*dx-2,y,strokeColor=col,strokeWidth=2))
        for i in range(n+1):d.add(Line(x+i*dx,22,x+i*dx,82,strokeColor=LIGHT,strokeDashArray=[2,2]))
        for i in range(n):txt(d,x+(i+.5)*dx,10,'φ'+str(i+1),8.5,anchor='middle')
        txt(d,0,10,'Fresh phase:',8)
        return d

    def palette(values):
        stops=np.array([[0,14,29,55],[.25,32,61,112],[.5,30,112,132],[.75,102,175,113],[1,248,229,105]],float)
        v=np.clip((values+75)/75,0,1)
        return np.stack([np.interp(v,stops[:,0],stops[:,i]) for i in range(1,4)],axis=-1).astype('uint8')

    def waterfall():
        a=np.load(here/'spectrogram-data.npz');d=Drawing(504,347)
        def panel(prefix,x,y,w,h,title,yticks):
            time=a[prefix+'time'];freq=a[prefix+'frequency'];power=a[prefix+'power_db']
            name='waterfall-'+('overview' if not prefix else 'zoom')+'.png'
            PILImage.fromarray(palette(power)).save(here/'figures'/name)
            # Original row order is chronological; image row zero is the top.
            d.add(Image(x,y,w,h,str(here/'figures'/name)))
            txt(d,x,y+h+29,title,9.1,font='Sans-Bold')
            txt(d,x,y+h+14,'Frequency (Hz)',7.8,color=GRAY)
            f0=float(freq[0]);f1=float(freq[-1]);dt=float(time[1]-time[0])
            t0=float(time[0]-dt/2);t1=float(time[-1]+dt/2)
            for f in [0,1000,2000,3000]:
                u=x+w*(f-f0)/(f1-f0);d.add(Line(u,y+h,u,y+h+3,strokeColor=GRAY));txt(d,u,y+h+5,f'{f:g}',7,anchor='middle')
            for t in yticks:
                if not t0<=t<=t1:continue
                v=y+h*(1-(t-t0)/(t1-t0));d.add(Line(x-3,v,x,v,strokeColor=GRAY));txt(d,x-6,v-2,f'{t:g}',7.5,anchor='end')
            return t0,t1
        t0,t1=panel('',38,64,220,238,'A. Original cable capture',[1,10,20,30,40])
        panel('zoom_',305,64,185,238,'B. Same capture, shorter window',[11,11.4,11.8,12.2])
        # Approximate onset annotation, explicitly not a timing measurement.
        py=64+238*(1-(38.8-t0)/(t1-t0));px=38+220*1604.16/3000
        d.add(Circle(px,py,6,strokeColor=colors.white,fillColor=None,strokeWidth=.9))
        arrow(d,px+45,py+22,px+5,py+4,color=colors.white)
        txt(d,px+43,py+24,'Probe appears',7.5,color=colors.white,anchor='middle')
        txt(d,38,49,'Capture-file seconds increase downward',8,color=GRAY)
        txt(d,305,49,'Auxiliary structure broadens in frequency',7.4,color=GRAY)
        for i in range(240):
            col=palette(np.array([-75+75*i/239]))[0]/255
            d.add(Rect(130+i,25,1.1,9,fillColor=colors.Color(*col),strokeColor=None))
        for v in [-75,-50,-25,0]:txt(d,130+240*(v+75)/75,13,str(v),7.5,anchor='middle')
        txt(d,374,26,'dB relative PSD',7.8)
        return d

    def probe():
        d=Drawing(504,252);txt(d,0,237,'A. Diagnostic 64-APSK constellation',9.4,font='Sans-Bold')
        cx=119;cy=125;scale=61
        d.add(Line(20,cy,218,cy,strokeColor=LIGHT));d.add(Line(cx,30,cx,218,strokeColor=LIGHT))
        counts=[4,12,20,28];radii=np.array([1,2.8,4.6,6.4]);norm=math.sqrt(sum(c*r*r for c,r in zip(counts,radii))/64)
        points=[]
        for n,r in zip(counts,radii/norm):
            d.add(Circle(cx,cy,scale*r,strokeColor=LIGHT,fillColor=None,strokeWidth=.5))
            for i in range(n):
                a=2*math.pi*(i+.5)/n;xx=cx+scale*r*math.cos(a);yy=cy+scale*r*math.sin(a)
                points.append((xx,yy));d.add(Circle(xx,yy,1.6,fillColor=BLUE,strokeColor=None))
        sx,sy=points[-7];ex,ey=sx-17,sy-14
        arrow(d,sx,sy,ex,ey,color=ORANGE);d.add(Circle(ex,ey,2,fillColor=ORANGE,strokeColor=None));txt(d,ex-3,ey-12,'e',9,color=ORANGE)
        txt(d,222,cy-2,'I',8);txt(d,cx+5,218,'Q',8)
        txt(d,120,10,'Four rings: 4 + 12 + 20 + 28 points',8,anchor='middle')
        txt(d,267,237,'B. Ideal shaped probe spectrum',9.4,font='Sans-Bold')
        f=np.linspace(1380,1820,441);u=abs(f-1600)
        power=np.where(u<=120,1,np.where(u<180,.5*(1+np.cos(np.pi*(u-120)/60)),0))
        xx=285+(f-1380)*205/440;yy=60+125*power
        line(d,xx,yy,BLUE,1.8)
        axis(d,285,60,205,[1420,1600,1780],1380,1820,'Frequency (Hz)')
        for val in [0,.5,1]:txt(d,278,60+125*val-2,f'{val:g}',7.5,anchor='end')
        txt(d,285,207,'Relative power density',7.9,color=GRAY)
        txt(d,387,10,'300 symbols/s; rolloff 0.2; span 360 Hz',7.8,anchor='middle')
        return d

    def cost():
        d=Drawing(504,236)
        txt(d,0,220,'A. Historical matched tracking comparison',9.1,font='Sans-Bold')
        txt(d,0,206,'Eight cases; 36 s capture; range shown',8,color=GRAY)
        txt(d,273,220,'B. Complete-stream timing feature',9.1,font='Sans-Bold')
        txt(d,273,206,'Three repetitions per arm; 40 s plus drain',8,color=GRAY)
        for x,w,limit in [(105,135,.5),(378,116,.8)]:
            axis(d,x,54,w,[0,limit/2,limit],0,limit,'CPU seconds')
        for label,y,low,high,col in [('Broad only',161,.392,.437,GRAY),('Aided + clock',103,.060,.069,BLUE)]:
            txt(d,98,y-2,label,8.1,anchor='end');a=105+135*low/.5;b=105+135*high/.5
            d.add(Line(a,y,b,y,strokeColor=col,strokeWidth=5));d.add(Line(a,y-5,a,y+5,strokeColor=col));d.add(Line(b,y-5,b,y+5,strokeColor=col))
            txt(d,(a+b)/2,y+14,f'{low:.3f}-{high:.3f}',8,anchor='middle')
        for label,y,value,col in [('Nominal',161,.507,GRAY),('Pooled inversion',103,.703,ORANGE)]:
            txt(d,371,y-2,label,8,anchor='end');d.add(Rect(378,y-7,116*value/.8,14,fillColor=col,strokeColor=None))
            txt(d,378+116*value/.8,y+15,f'{value:.3f}',8,anchor='end')
        txt(d,252,3,'Different experiments and work scopes; compare within each panel only.',8,color=GRAY,anchor='middle')
        return d

    return {'format':format_plot(),'hop_geometry':hop_plot(),'waterfall':waterfall(),
            'diagnostic_probe':probe(),'cost_comparison':cost()}
