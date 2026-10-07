#!/usr/bin/env python3
"""Decode the complete deliverable, check captions, inspect encoded audio and export QA frames."""
import argparse,json,subprocess,hashlib,textwrap,math
from pathlib import Path
import numpy as np
from PIL import Image

def run(args):return subprocess.check_output([str(x) for x in args],stderr=subprocess.STDOUT)
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--tools',type=Path,required=True);a=p.parse_args();out=a.output.resolve();repo=Path(__file__).resolve().parents[2]
 if out.is_relative_to(repo):p.error('All QA media must be outside Git')
 movie=out/'data-pump-capabilities.mp4';ff=a.tools/'usr/bin/ffmpeg';fp=a.tools/'usr/bin/ffprobe';timeline=json.loads((out/'timeline.json').read_text())
 probe=json.loads(run([fp,'-v','error','-show_format','-show_streams','-show_chapters','-of','json',movie]));(out/'encoded-probe.json').write_text(json.dumps(probe,indent=2)+'\n')
 run([ff,'-v','error','-xerror','-i',movie,'-map','0:v:0','-map','0:a:0','-f','null','-'])
 loud=run([ff,'-hide_banner','-i',movie,'-af','loudnorm=I=-18:TP=-2:LRA=11:print_format=json','-f','null','-']).decode();loud=json.JSONDecoder().raw_decode(loud[loud.rfind('{'):])[0];(out/'encoded-loudness.json').write_text(json.dumps(loud,indent=2)+'\n');assert float(loud['input_tp'])<-1
 v=next(s for s in probe['streams'] if s['codec_type']=='video');aud=next(s for s in probe['streams'] if s['codec_type']=='audio');assert v['codec_name']=='h264' and aud['codec_name']=='aac';assert v['width']==1600 and v['height']==900;assert len(probe['chapters'])==len(timeline)==10
 frames=[];caption_count=0
 for s in timeline:
  previous=-1
  for c in s['captions']:
   assert 0<=c['start']<c['end']<=s['duration'] and c['start']>previous;previous=c['end'];assert len(textwrap.wrap(c['text'],112))<=2;caption_count+=1
  t=s['start']+min(5,s['duration']/2);path=out/('encoded-'+s['id']+'.png');run([ff,'-v','error','-y','-ss',str(t),'-i',movie,'-frames:v','1',path]);im=Image.open(path).convert('RGB');frames.append(im.resize((640,360)))
 rates=next(s for s in timeline if s['id']=='rates')
 if len(rates['captions'])>=3:
  t=rates['start']+rates['captions'][1]['start']+1
  run([ff,'-v','error','-y','-ss',str(t),'-i',movie,'-frames:v','1',out/'encoded-rates-comparison.png'])
 contact=Image.new('RGB',(1280,1800),'#0b1423')
 for i,im in enumerate(frames):contact.paste(im,((i%2)*640,(i//2)*360))
 contact.save(out/'encoded-contact.jpg')
 s=next(s for s in timeline if s['id']=='private');ex=s['audio_excerpt'];assert ex['at']>=max(c['end'] for c in s['captions'])+.3
 x=np.frombuffer(run([ff,'-v','error','-ss',str(s['start']+ex['at']),'-i',movie,'-t',str(ex['duration']),'-vn','-ar','48000','-ac','1','-f','f32le','-']),dtype='<f4');rms=20*math.log10(float(np.sqrt(np.mean(x*x))));peak=20*math.log10(float(np.max(abs(x))));assert -34<rms<-20 and peak<-10
 report={'full_video_audio_decode':'passed','chapters':10,'captions':caption_count,'resolution':[1600,900],'seconds':float(probe['format']['duration']),'bytes':movie.stat().st_size,'sha256':hashlib.sha256(movie.read_bytes()).hexdigest(),'loudness_lufs':float(loud['input_i']),'true_peak_dbtp':float(loud['input_tp']),'robust_audio_rms_dbfs':rms,'robust_audio_peak_dbfs':peak,'robust_voice_free_seconds':ex['duration'],'encoded_visual_review':'pending'}
 (out/'qa.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
