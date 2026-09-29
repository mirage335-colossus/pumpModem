// Development host transport only; the isolated renderer and C++ own the UI.
import {createHostedFrontend} from '../web/hosted.mjs';

const maximumFrame=8*1024*1024+12;

export class InputBatcher {
    constructor(post,{delay=4,limit=2*maximumFrame}={}) {
        this.post=post;this.delay=delay;this.limit=limit;
        this.queue=[];this.bytes=0;this.running=false;this.timer=null;this.failure=null;this.pending=new Set();
    }
    send(bytes) {
        if(this.failure)return Promise.reject(this.failure);
        if(!(bytes instanceof Uint8Array)||bytes.length>maximumFrame||bytes.length<12)
            return Promise.reject(new Error('Invalid native frame'));
        if(this.bytes+bytes.length>this.limit||this.queue.length>=2048)
            return Promise.reject(new Error('Browser input queue exceeded its bound'));
        this.bytes+=bytes.length;
        const job=new Promise((resolve,reject)=>this.queue.push({bytes,resolve,reject}));
        const tracked=job.finally(()=>this.pending.delete(tracked));this.pending.add(tracked);
        this.schedule();return tracked;
    }
    schedule() {
        if(!this.running&&this.timer===null&&this.queue.length)
            this.timer=setTimeout(()=>{this.timer=null;this.flush();},this.delay);
    }
    async flush() {
        if(this.running||!this.queue.length)return;
        this.running=true;
        const batch=[];let size=0;
        // A legal large frame stands alone; 64 KiB is a batching target.
        while(this.queue.length&&(!batch.length||size+this.queue[0].bytes.length<=65536)) {
            const item=this.queue.shift();batch.push(item);size+=item.bytes.length;
        }
        const bytes=new Uint8Array(size);let at=0;
        for(const item of batch){bytes.set(item.bytes,at);at+=item.bytes.length;}
        try {
            await this.post(bytes);
            if(this.failure)throw this.failure;
            for(const item of batch)item.resolve();
        } catch(error) {
            for(const item of batch)item.reject(error);
            this.close(error);
        } finally {this.bytes-=size;this.running=false;this.schedule();}
    }
    drain() {
        // Fence the already accepted events without waiting for future PCM.
        return Promise.all([...this.pending]);
    }
    close(error=new Error('Worker stopped')) {
        this.failure=error;clearTimeout(this.timer);this.timer=null;
        for(const item of this.queue){this.bytes-=item.bytes.length;item.reject(error);}
        this.queue.length=0;
    }
}

export function feedFrames(bytes,deliver) {
    let at=0;
    while(at<bytes.length) {
        if(bytes.length-at<12)throw new Error('Truncated worker frame');
        const header=new DataView(bytes.buffer,bytes.byteOffset+at,12);
        const size=header.getUint32(8,true)+12;
        if(header.getUint32(0,true)!==0x31575044||size>maximumFrame||size>bytes.length-at)
            throw new Error('Invalid worker frame');
        deliver(bytes.subarray(at,at+size));at+=size;
    }
}

export async function boot(document,environment=window,frontend=createHostedFrontend) {
    const token=environment.location.hash.slice(1),status=document.querySelector('#status');
    const audio=document.querySelector('#audio'),stop=document.querySelector('#stop');
    let session='',view,stopped=false,heartbeat,files=null,snapshot=null;
    let startResolve;const started=new Promise(resolve=>startResolve=resolve);
    let serviceBarrier=Promise.resolve(),gatedBytes=0,gatedCount=0;
    const report=error=>{status.textContent=error?.message||String(error);};
    const request=async(path,body,signal)=>{
        const response=await environment.fetch(path,{method:body===undefined?'GET':'POST',
            headers:{'X-Preview-Token':token,'X-Preview-Session':session},body,signal,cache:'no-store'});
        if(!response.ok)throw new Error(await response.text());return response;
    };
    const inputs=new InputBatcher(async bytes=>{await started;await request('/input',bytes);});
    const send=bytes=>{
        const kind=new DataView(bytes.buffer,bytes.byteOffset,12).getUint32(4,true);
        // File service UI sequences cannot overtake an unfinished import. Audio
        // and clocks must continue while the trusted host streams large files.
        if(kind!==2)return inputs.send(bytes);
        if(gatedBytes+bytes.length>maximumFrame||gatedCount>=128)
            return Promise.reject(new Error('Pending UI events exceeded their bound'));
        gatedBytes+=bytes.length;gatedCount++;
        return serviceBarrier.then(()=>inputs.send(bytes)).finally(()=>{gatedBytes-=bytes.length;gatedCount--;});
    };
    const choose=(service,signal)=>{
        if(signal.aborted)return Promise.resolve(null);
        const dialog=document.querySelector('#chooser'),input=document.querySelector('#path');
        document.querySelector('#choice-title').textContent=service.title;
        document.querySelector('#file-root').textContent='Host files beneath '+files+'; choose a relative path. Existing files are never overwritten.';
        input.value=service.kind==='save_file'?service.value||'':'';
        document.querySelector('#choose-path').onclick=()=>dialog.close('ok');
        document.querySelector('#cancel-path').onclick=()=>dialog.close('cancel');
        dialog.returnValue='';dialog.showModal();input.focus();
        return new Promise(resolve=>{
            const abort=()=>dialog.close('cancel');signal.addEventListener('abort',abort,{once:true});
            dialog.addEventListener('close',()=>{signal.removeEventListener('abort',abort);
                resolve(dialog.returnValue==='ok'?input.value:null);},{once:true});
        });
    };
    const handleService=async({request:service,event},signal)=>{
        let release;const previous=serviceBarrier;
        serviceBarrier=new Promise(resolve=>release=resolve);
        // The hosted adapter also aborts a chooser on its successful native
        // acknowledgment. That withdrawal must not cancel an authorized save.
        const cancel=()=>{
            if(String(snapshot?.generation)===String(event.generation)&&BigInt(snapshot.ack)>=BigInt(event.sequence))return;
            request('/cancel',JSON.stringify({target:event.target})).catch(()=>{});
        };
        try {
            await previous;await inputs.drain();
            if(signal.aborted)return {cancelled:true};
            let path='';
            if(service.kind==='clipboard')await environment.navigator.clipboard.writeText(service.value);
            else {
                if(!files)throw new Error('Restart with --files DIR to enable host file choices.');
                if(!['open_file','save_file'].includes(service.kind))throw new Error('This preview supports individual files, not folders.');
                path=await choose(service,signal);
                if(path===null||signal.aborted)return {cancelled:true};
            }
            signal.addEventListener('abort',cancel,{once:true});
            await request('/service',JSON.stringify({event,path,kind:service.kind}));
            return {handled:true};
        } finally {signal.removeEventListener('abort',cancel);release();}
    };
    const close=()=>{
        if(stopped)return;stopped=true;clearInterval(heartbeat);inputs.close();
        if(session)environment.navigator.sendBeacon('/close',JSON.stringify({token,session}));
        view?.dispose();audio.disabled=true;stop.disabled=true;
    };
    environment.addEventListener('pagehide',close,{once:true});
    try {
        if(!token)throw new Error('Open the complete URL printed by preview.py, including its # token.');
        const sources=await Promise.all(['renderer.mjs','protocol.mjs','style.css','audio_worklet.js'].map(async name=>{
            const response=await environment.fetch('/web/'+name);if(!response.ok)throw new Error('Missing browser asset '+name);return response.text();
        }));
        if(stopped)return;
        view=frontend({container:document.querySelector('#root'),rendererSource:sources[0],protocolSource:sources[1],
            styleText:sources[2],workletSource:sources[3],send,handleService,
            status:value=>report(value.reason||('Audio: '+value.state))});
        await view.ready;
        if(stopped)return;
        const result=await (await request('/start',new Uint8Array())).json();
        session=result.session;files=result.files;startResolve();
        if(stopped){environment.navigator.sendBeacon('/close',JSON.stringify({token,session}));return;}
        heartbeat=setInterval(()=>request('/heartbeat',new Uint8Array()).catch(error=>{report(error);close();}),4000);
        audio.disabled=false;audio.onclick=()=>view.enableAudio().catch(report);
        stop.disabled=false;stop.onclick=async()=>{
            try{await request('/stop',new Uint8Array());}catch(error){report(error);}
            finally{close();report('Worker stopped. Reload to start a fresh worker.');}
        };
        report('Native worker connected'+(result.simulation?' (simulation).':'.')+(files?' Host files: '+files: ' Host file choices are disabled.'));
        while(!stopped) {
            const bytes=new Uint8Array(await (await request('/output')).arrayBuffer());
            if(!stopped)feedFrames(bytes,frame=>{
                if(new DataView(frame.buffer,frame.byteOffset,12).getUint32(4,true)===101)
                    snapshot=JSON.parse(new TextDecoder().decode(frame.subarray(12)));
                view.feed(frame);
            });
        }
    } catch(error) {if(!stopped){report(error);close();}}
    finally {startResolve();}
}

if(typeof document!=='undefined')boot(document);
