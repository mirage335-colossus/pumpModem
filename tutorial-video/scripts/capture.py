#!/usr/bin/env python3
"""Record a reviewed action sequence on an explicitly selected virtual display."""
import argparse,json,os,signal,subprocess,time
from pathlib import Path

def main():
    p=argparse.ArgumentParser()
    p.add_argument('scenario',type=Path);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--tools',type=Path,required=True);p.add_argument('--display',required=True)
    p.add_argument('--peers',type=Path);p.add_argument('--pulse-source')
    a=p.parse_args();repo=Path(__file__).resolve().parents[2]
    if a.output.resolve().is_relative_to(repo) or a.output.exists():p.error('Use new external media output')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,DISPLAY=a.display)
    peers=json.loads(a.peers.read_text()) if a.peers else {}
    def x(*v):subprocess.run([str(a.tools/'usr/bin/xdotool'),*map(str,v)],env=env,check=True,stdout=subprocess.DEVNULL)
    cmd=[str(a.tools/'usr/bin/ffmpeg'),'-hide_banner','-loglevel','warning','-n','-f','x11grab','-draw_mouse','1','-framerate','20','-video_size','1280x1000','-i',a.display]
    if a.pulse_source:cmd+=['-thread_queue_size','1024','-f','pulse','-i',a.pulse_source,'-map','0:v:0','-map','1:a:0','-c:a','aac','-b:a','128k']
    else:cmd+=['-an']
    cmd+=['-c:v','libx264','-preset','veryfast','-crf','20','-pix_fmt','yuv420p','-movflags','+faststart',str(a.output)]
    rec=subprocess.Popen(cmd,stdin=subprocess.PIPE,env=env);start=time.monotonic();events=[];xy=(40,40)
    def interrupt(*unused):raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM,interrupt)
    try:
        time.sleep(1)
        spec=json.loads(a.scenario.read_text())
        for action in spec['actions']:
            if rec.poll() is not None:raise RuntimeError('Recorder stopped early')
            time.sleep(action.get('wait',0))
            if 'window' in action:
                w=peers[action['window']]['window'];x('windowraise',w,'windowfocus',w)
            if 'move' in action:
                dest=action['move']
                for n in range(1,19):
                    t=n/18;t=t*t*(3-2*t);x('mousemove',round(xy[0]+(dest[0]-xy[0])*t),round(xy[1]+(dest[1]-xy[1])*t));time.sleep(.025)
                xy=tuple(dest)
            events.append(dict(seconds=round(time.monotonic()-start,3),**action))
            if action.get('click'):x('click',action['click'])
            if 'text' in action:x('type','--clearmodifiers','--delay','50','--',action['text'])
            if 'key' in action:x('key','--clearmodifiers',action['key'])
        time.sleep(spec.get('hold',3))
    finally:
        if rec.poll() is None:
            try:rec.communicate(b'q\n',timeout=30)
            except subprocess.TimeoutExpired:
                rec.terminate()
                try:rec.wait(timeout=10)
                except subprocess.TimeoutExpired:rec.kill();rec.wait()
        a.output.with_suffix('.actions.json').write_text(json.dumps({'capture_seconds':time.monotonic()-start,'actions':events},indent=2)+'\n')
    if rec.returncode:raise RuntimeError('Capture failed')

if __name__=='__main__':main()
