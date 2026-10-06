#!/usr/bin/env python3
"""Keep a dedicated display and two GUI peers alive; stop and reap all children."""
import argparse,json,os,signal,subprocess,time
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--display',default=':91');p.add_argument('--robust',action='store_true');a=p.parse_args()
    b=a.root.resolve();tools=b/'tools';env=dict(os.environ,DISPLAY=a.display,LD_LIBRARY_PATH=str(tools/'usr/lib/x86_64-linux-gnu'))
    children=[];logs=[];stopped=False
    def stop(*args):
        nonlocal stopped
        stopped=True
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    def launch(cmd,name,extra=None):
        log=(b/(('robust-' if a.robust else '')+name+'.log')).open('xb');logs.append(log)
        child=subprocess.Popen(cmd,cwd=b,env=dict(env,**(extra or {})),stdout=log,stderr=subprocess.STDOUT);children.append(child);return child
    peers={}
    try:
        display=launch([str(tools/'Xvfb'),a.display,'-screen','0','1440x1000x24','-nolisten','tcp','-noreset','-extension','GLX'],'display');time.sleep(1)
        for role in (['sender'] if a.robust else ['receiver','sender']):
            args=['--simulation','--pattern','auto-keystream','--tx-dbm','3','--path-loss-db','150','--noise-dbm-hz','-164','--oscillator','gpsdo-xo','--target-snr','6.326999095983183','--rate','3600','--carrier','1500','--dsp-workspace','50%'] if a.robust else []
            child=launch([str(b/'datapump-gui'),*args],role,{'XDG_CONFIG_HOME':str(b/(('robust-' if a.robust else '')+role+'-config'))});time.sleep(2)
            out=subprocess.check_output([str(tools/'usr/bin/xdotool'),'search','--onlyvisible','--pid',str(child.pid)],env=env).decode().split();window=out[0]
            subprocess.run([str(tools/'usr/bin/xdotool'),'windowsize',window,'1280','1000','windowmove',window,'0','0','windowfocus',window],env=env,check=True)
            peers[role]={'pid':child.pid,'window':window}
        (b/('robust-peers.json' if a.robust else 'peers.json')).write_text(json.dumps(peers,indent=2)+'\n');print('Peers ready',json.dumps(peers),flush=True)
        while not stopped and not (b/('stop-robust' if a.robust else 'stop')).exists():
            if any(c.poll() is not None for c in children):raise RuntimeError('Display or GUI exited')
            time.sleep(.5)
    finally:
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
                try:child.wait(timeout=30)
                except subprocess.TimeoutExpired:child.kill();child.wait()
        for log in logs:log.close()
        print('All owned display and GUI children stopped and reaped',flush=True)

if __name__=='__main__':main()
