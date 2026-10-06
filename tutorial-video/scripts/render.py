#!/usr/bin/env python3
"""Compose one small narrated MP4 with reviewed native takes and editable illustrations."""
import argparse,hashlib,json,math,subprocess,sys,textwrap,wave
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from illustrations import W,H,BG,INK,MUTED,CYAN,LINE,font,text,illustration,phone
sys.dont_write_bytecode=True

def probe(ffprobe,path):return json.loads(subprocess.check_output([str(ffprobe),'-v','error','-show_format','-show_streams','-of','json',str(path)]))
def pcm(ffmpeg,path,rate=48000,start=0,duration=None):
    c=[str(ffmpeg),'-v','error','-ss',str(start),'-i',str(path)]
    if duration is not None:c+=['-t',str(duration)]
    c+=['-vn','-af','pan=mono|c0=c0','-ar',str(rate),'-c:a','pcm_f32le','-f','f32le','-']
    return np.frombuffer(subprocess.check_output(c),dtype='<f4').copy()
def write_wav(path,x,rate):
    with wave.open(str(path),'wb') as w:w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes((np.clip(x,-1,1)*32767).astype('<i2').tobytes())
def stamp(s):
    n=round(s*1000);return f'{n//3600000:02}:{n//60000%60:02}:{n//1000%60:02}.{n%1000:03}'
def stop(p):
    if p.poll() is None:
        p.terminate()
        try:p.wait(timeout=10)
        except subprocess.TimeoutExpired:p.kill();p.wait()

def compose(scene,t,native,media):
    if scene['kind'] in ('native','phone'):
        im=Image.new('RGB',(W,H),BG)
        if scene['kind']=='phone':
            im.paste(native.resize((990,774),Image.Resampling.LANCZOS),(12,91));im.paste(phone(max(0,t-8),420,900),(1010,73))
            d=ImageDraw.Draw(im);text(d,(30,900),'BPSK31 · 1500 Hz',28,CYAN,bold=True);text(d,(30,949),'the quick brown fox jumps over the lazy dog',24)
        else:
            im.paste(native.resize((1228,960),Image.Resampling.LANCZOS),(106,49))
            if scene['id']=='robust-result':
                crop=native.crop((18,433,400,490)).resize((955,143),Image.Resampling.LANCZOS)
                im.paste(crop,(242,574));ImageDraw.Draw(im).rectangle((240,572,1199,719),outline=CYAN,width=2)
    else:im=illustration(scene,t,media)
    d=ImageDraw.Draw(im);text(d,(26,15),scene['title'],24,bold=True)
    badge=scene['label'];excerpt=scene.get('audio_excerpt')
    if excerpt and excerpt['at']<=t<excerpt['at']+excerpt['duration']:badge=excerpt['label'].upper()
    if scene['id'].startswith('fast-'):
        switch=scene.get('receiver_at',10000);badge+=' · '+('RECEIVER' if t>=switch else 'SENDER')
    text(d,(W-26,27),badge,14,CYAN,anchor='rm');d.line((26,46,W-26,46),fill=LINE)
    caption=next((c['text'] for c in scene['captions'] if c['start']<=t<c['end']),'')
    if caption:
        lines=textwrap.wrap(caption,112)
        for n,line in enumerate(lines):text(d,(W/2,1021+n*24),line,19,INK,anchor='mm')
    return im

def main():
    p=argparse.ArgumentParser();p.add_argument('--media',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--tools',type=Path,required=True);p.add_argument('--voice',type=Path,required=True);p.add_argument('--narration-only',action='store_true');a=p.parse_args()
    repo=Path(__file__).resolve().parents[2];out=a.output.resolve()
    if out.is_relative_to(repo) or (out/'data-pump-tutorial.mp4').exists() or (out/'picture.mp4').exists():p.error('Choose a fresh output directory outside Git')
    out.mkdir(parents=True,exist_ok=True);cache=out/'speech';cache.mkdir(exist_ok=True)
    ff=a.tools/'usr/bin/ffmpeg';fp=a.tools/'usr/bin/ffprobe';story=json.loads((Path(__file__).resolve().parents[1]/'storyboard.json').read_text());fps=story['fps']
    from piper import PiperVoice,SynthesisConfig
    import onnxruntime
    onnxruntime.disable_telemetry_events();voice=PiperVoice.load(str(a.voice));rate=voice.config.sample_rate;config=SynthesisConfig(length_scale=.94);voicehash=hashlib.sha256(a.voice.read_bytes()).hexdigest()
    timeline=[];captionlist=[];sound=bytearray();cursor=0
    for original in story['scenes']:
        scene=dict(original);spoken=bytearray(round(rate*.3)*2);caps=[]
        for n,sentence in enumerate(scene['sentences']):
            cues=scene.get('cue_times',[])
            if n<len(cues):spoken.extend(bytes(max(0,round(cues[n]*rate)*2-len(spoken))))
            path=cache/(hashlib.sha256((voicehash+'|.94|'+sentence).encode()).hexdigest()[:18]+'.wav')
            if not path.exists():
                with wave.open(str(path),'wb') as w:voice.synthesize_wav(sentence,w,syn_config=config)
            with wave.open(str(path),'rb') as w:assert w.getframerate()==rate and w.getnchannels()==1;data=w.readframes(w.getnframes())
            start=len(spoken)/(rate*2);spoken.extend(data);end=len(spoken)/(rate*2);caps.append({'start':start,'end':end,'text':sentence});spoken.extend(bytes(round(rate*.45)*2))
        native=0
        if scene.get('file'):
            m=probe(fp,a.media/scene['file']);v=next(s for s in m['streams'] if s['codec_type']=='video');native=min(float(v.get('duration',m['format']['duration'])),scene.get('trim_end',1e12))-scene.get('trim_start',0)
            if native<=0:raise ValueError('Empty native cut')
        duration=math.ceil(max(native,scene.get('minimum',0),len(spoken)/(rate*2)+.5)*fps)/fps
        scene.update(start=cursor,duration=duration,native_seconds=native,captions=caps);timeline.append(scene)
        for c in caps:captionlist.append(dict(c,start=c['start']+cursor,end=c['end']+cursor))
        n=round((cursor+duration)*rate)-len(sound)//2;spoken.extend(bytes(max(0,n*2-len(spoken))));sound.extend(spoken);cursor+=duration
        print('Prepared',scene['id'],round(duration,2),'s',flush=True)
    with wave.open(str(out/'narration.wav'),'wb') as w:w.setparams((1,2,rate,0,'NONE','not compressed'));w.writeframes(sound)
    (out/'timeline.json').write_text(json.dumps(timeline,indent=2)+'\n');(out/'data-pump-tutorial.vtt').write_text('WEBVTT\n\n'+''.join(f"{stamp(c['start'])} --> {stamp(c['end'])}\n{c['text']}\n\n" for c in captionlist))
    if a.narration_only:return
    normalized=out/'narration-normalized.wav'
    subprocess.run([str(ff),'-v','error','-y','-i',str(out/'narration.wav'),'-af','loudnorm=I=-18:TP=-4:LRA=11','-ar','48000','-ac','1',str(normalized)],check=True)
    narration=pcm(ff,normalized);samples=round(cursor*48000);mixed=np.zeros(samples,dtype=np.float32);mixed[:min(samples,len(narration))]=narration[:samples]
    speech=np.zeros(samples,dtype=np.float32)
    for c in captionlist:speech[max(0,round((c['start']-.08)*48000)):min(samples,round((c['end']+.1)*48000))]=1
    # Smooth the narration duck over 80 ms to avoid abrupt gain changes.
    width=3840;acc=np.cumsum(np.pad(speech,(width//2,width//2)),dtype=np.float64);speech=((acc[width:]-acc[:-width])/width)[:samples];speech=np.pad(speech,(0,max(0,samples-len(speech))))
    report=[]
    for scene in timeline:
        excerpt=scene.get('audio_excerpt');path=a.media/(excerpt['file'] if excerpt else scene.get('file',''))
        if not path.is_file():continue
        streams=probe(fp,path)['streams']
        if not any(s['codec_type']=='audio' for s in streams):continue
        at=excerpt.get('at',0) if excerpt else 0;offset=excerpt.get('source_start',0) if excerpt else scene.get('trim_start',0);duration=excerpt['duration'] if excerpt else scene['native_seconds']
        x=pcm(ff,path,start=offset,duration=duration);peak=float(np.max(abs(x))) if len(x) else 0
        if not peak:continue
        active=x[abs(x)>max(.002,peak*.06)];rms=float(np.sqrt(np.mean(active*active))) if len(active) else peak
        level=-26 if excerpt else -30;gain=min(1,10**(level/20)/rms,10**(-20/20)/peak);x*=gain
        fade=min(4800,len(x)//2);x[:fade]*=np.linspace(0,1,fade);x[-fade:]*=np.linspace(1,0,fade)
        pos=round((scene['start']+at)*48000);end=min(samples,pos+len(x));x=x[:end-pos];x*=1-(1-10**(-5/20))*speech[pos:end];mixed[pos:end]+=x
        report.append({'scene':scene['id'],'source':path.name,'gain_db':20*math.log10(gain),'source_start':offset,'at':at,'duration':len(x)/48000,'separately_generated':bool(excerpt)})
    peak=float(np.max(abs(mixed)));gain=min(1,10**(-2/20)/peak);mixed*=gain;write_wav(out/'mixed.wav',mixed,48000);(out/'audio-mix.json').write_text(json.dumps({'sources':report,'final_peak_dbfs':20*math.log10(float(np.max(abs(mixed))))},indent=2)+'\n')
    encoder=subprocess.Popen([str(ff),'-v','error','-n','-f','rawvideo','-pixel_format','rgb24','-video_size',f'{W}x{H}','-framerate',str(fps),'-i','-','-an','-c:v','libx264','-preset','medium','-crf','27','-maxrate','1500k','-bufsize','3000k','-threads','4','-pix_fmt','yuv420p',str(out/'picture.mp4')],stdin=subprocess.PIPE)
    try:
        for scene in timeline:
            decoder=None;native=Image.new('RGB',(1280,1000),BG);decoded=0
            if scene.get('file'):
                cmd=[str(ff),'-v','error','-ss',str(scene.get('trim_start',0)),'-i',str(a.media/scene['file']),'-t',str(scene['native_seconds']),'-vf',f'fps={fps},scale=1280:1000','-an','-f','rawvideo','-pix_fmt','rgb24','-'];decoder=subprocess.Popen(cmd,stdout=subprocess.PIPE)
            try:
                for n in range(round(scene['duration']*fps)):
                    if decoder:
                        data=decoder.stdout.read(1280*1000*3)
                        if data:
                            if len(data)!=1280*1000*3:raise RuntimeError('Truncated decoded frame')
                            native=Image.frombytes('RGB',(1280,1000),data);decoded+=1
                        else:
                            code=decoder.wait(timeout=10);decoder.stdout.close();decoder=None
                            if code or decoded<max(1,int(scene['native_seconds']*fps)-2):raise RuntimeError('Native decode ended early')
                    frame=compose(scene,n/fps,native,a.media)
                    if n in (round(scene['duration']*fps)//2,round(scene['duration']*fps)-1):frame.save(out/(scene['id']+f'-{n:04}.png'))
                    encoder.stdin.write(frame.tobytes())
                print('Rendered',scene['id'],flush=True)
            finally:
                if decoder:decoder.stdout.close();stop(decoder)
        encoder.stdin.close()
        if encoder.wait(timeout=60):raise RuntimeError('Encoding failed')
    finally:stop(encoder)
    metadata=';FFMETADATA1\ntitle=Data Pump - a practical introduction\n'
    for s in timeline:metadata+=f"[CHAPTER]\nTIMEBASE=1/1000\nSTART={round(s['start']*1000)}\nEND={round((s['start']+s['duration'])*1000)}\ntitle={s['title']}\n"
    (out/'chapters.txt').write_text(metadata);final=out/'data-pump-tutorial.mp4'
    subprocess.run([str(ff),'-v','error','-n','-i',str(out/'picture.mp4'),'-i',str(out/'mixed.wav'),'-i',str(out/'chapters.txt'),'-map','0:v:0','-map','1:a:0','-map_metadata','2','-map_chapters','2','-c:v','copy','-c:a','aac','-b:a','64k','-ar','48000','-ac','1','-movflags','+faststart','-t',str(cursor),str(final)],check=True)
    digest=hashlib.sha256(final.read_bytes()).hexdigest();(out/'SHA256SUMS').write_text(digest+'  '+final.name+'\n')
    delivery={'duration_seconds':cursor,'bytes':final.stat().st_size,'sha256':digest,'voice_model_sha256':voicehash,'captions':len(captionlist),'chapters':len(timeline)};(out/'delivery.json').write_text(json.dumps(delivery,indent=2)+'\n')
    if final.stat().st_size>30000000:raise RuntimeError('Final video exceeds 30 MB budget')
    print('Finished',json.dumps(delivery),flush=True)

if __name__=='__main__':main()
