import {Renderer} from './renderer.mjs';
import {BrowserAudio} from './browser_audio.mjs';
import {FrameDecoder,encodeEvent,encodeViewport,encodeAudio,encodeUploadBegin,
    encodeUploadChunk,encodeUploadCommit,encodeDownload} from './protocol.mjs';
const maximumFile=256*1024*1024;
function node(document,tag,text){const element=document.createElement(tag);if(text!==undefined)element.textContent=text;return element;}
function basename(value){const name=String(value).split(/[\\/]/).at(-1).replace(/[\x00-\x1f\x7f]/g,'_');return name&&name!=='.'&&name!=='..'?name:'download.bin';}
export async function boot({root,factorySource,wasmBytes,workletSource,workerSource,standalone=false}) {
    if(!root?.ownerDocument||typeof factorySource!=='string'||!(wasmBytes instanceof Uint8Array)||typeof workerSource!=='string')throw new TypeError('Preloaded application assets and root are required');
    const document=root.ownerDocument,environment=document.defaultView;
    const toolbar=node(document,'section'),status=node(document,'p'),surface=node(document,'section');
    root.classList.add('dp-client');toolbar.className='dp-toolbar';
    if(standalone)document.body.classList.add('dp-page');
    const enable=node(document,'button','Enable microphone and audio'),stop=node(document,'button','Stop audio');
    for(const button of [enable,stop])button.type='button';
    status.setAttribute('role','status');toolbar.append(enable,stop,status);root.replaceChildren(toolbar,surface);
    const url=environment.URL.createObjectURL(new environment.Blob([workerSource],{type:'text/javascript'}));
    const worker=new environment.Worker(url);environment.URL.revokeObjectURL(url);
    let alive=true,nextId=0,pendingBytes=0,readyResolve,readyReject;
    const ready=new Promise((resolve,reject)=>{readyResolve=resolve;readyReject=reject;});
    const pending=new Map(),downloads=new Map();
    const fail=error=>{
        const reason=error?.message||String(error);status.textContent=reason;alive=false;worker.terminate();
        readyReject(new Error(reason));for(const request of pending.values())request.reject(new Error(reason));pending.clear();pendingBytes=0;
        for(const download of downloads.values())download.reject(new Error(reason));downloads.clear();
    };
    const send=bytes=>{
        if(!alive)return Promise.reject(new Error('Application was closed'));
        if(pending.size>=128||pendingBytes+bytes.byteLength>16*1024*1024)return Promise.reject(new Error('Local input queue exceeded its bound'));
        const id=++nextId;pendingBytes+=bytes.byteLength;
        return new Promise((resolve,reject)=>{pending.set(id,{resolve,reject,size:bytes.byteLength});worker.postMessage({type:'feed',id,bytes},[bytes.buffer]);});
    };
    const audio=new BrowserAudio(event=>send(encodeAudio(event)),{environment,status:value=>{
        if(value.state==='ready')status.textContent=`Audio enabled at ${value.rate} Hz. Keep this page in the foreground. Microphone processing depends on the browser and device.`;
        else if(value.state==='interrupted'||value.state==='playback-failed')status.textContent=value.reason;
    }});
    const chooseFile=signal=>new Promise((resolve,reject)=>{
        const input=node(document,'input');input.type='file';input.hidden=true;root.append(input);
        const finish=(error,file)=>{input.remove();signal.removeEventListener('abort',abort);error?reject(error):resolve(file);};
        const abort=()=>finish(new Error('File selection cancelled'));
        signal.addEventListener('abort',abort,{once:true});
        input.addEventListener('change',()=>finish(null,input.files?.[0]||null),{once:true});
        input.addEventListener('cancel',()=>finish(null,null),{once:true});input.click();
    });
    const platformService=async(request,signal,authorize)=>{
        if(request.kind==='clipboard') {
            if(!environment.navigator.clipboard)throw new Error('Select and copy the displayed text with your browser');
            await environment.navigator.clipboard.writeText(request.value);return {value:''};
        }
        if(request.kind==='open_file') {
            const file=await chooseFile(signal);if(!file)return {cancelled:true};
            if(file.size>maximumFile)throw new Error('This browser profile limits selected files to 256 MiB');
            await send(encodeUploadBegin(authorize(basename(file.name)),file.size));
            for(let position=0;position<file.size;position+=65536) {
                if(signal.aborted)throw new Error('File import cancelled');
                const bytes=new Uint8Array(await file.slice(position,position+65536).arrayBuffer());
                await send(encodeUploadChunk(request.id,position,bytes));
            }
            if(signal.aborted)throw new Error('File import cancelled');
            await send(encodeUploadCommit(request.id));return {handled:true};
        }
        if(request.kind==='save_file') {
            const received=new Promise((resolve,reject)=>downloads.set(request.id,{resolve,reject,size:null,position:0,chunks:[],filename:basename(request.value)}));
            if(signal.aborted)throw new Error('File export cancelled');
            try {
                await send(encodeDownload(authorize(basename(request.value))));
                const download=await received;
                const blob=new environment.Blob(download.chunks,{type:'application/octet-stream'});
                const url=environment.URL.createObjectURL(blob),link=node(document,'a','Download received file');
                link.href=url;link.download=download.filename;link.rel='noopener';root.append(link);link.click();
                // Keep a visible explicit retry link; offering a download is not
                // proof that the user saved bytes to persistent device storage.
                status.textContent='Download offered. Your browser controls where the file is saved.';
                environment.setTimeout(()=>{environment.URL.revokeObjectURL(url);link.remove();},60000);
                return {handled:true};
            } finally {downloads.delete(request.id);}
        }
        throw new Error('This browser profile has no folder-opening capability');
    };
    const renderer=new Renderer(surface,event=>{send(encodeEvent(event)).catch(fail);},{platformService,resize:(width,height)=>{if(alive)send(encodeViewport(Math.max(240,Math.min(4096,width)),Math.max(240,Math.min(4096,height)))).catch(fail);}});
    let presentation=null,paintPending=false;
    const present=snapshot=>{
        presentation=snapshot;
        if(paintPending)return;
        paintPending=true;
        // A complete snapshot supersedes an older pending snapshot. Yield the
        // main thread so clock and audio messages are handled before painting.
        (environment.requestAnimationFrame?.bind(environment)||environment.setTimeout.bind(environment))(()=>{
            paintPending=false;
            if(!alive)return;
            const next=presentation;presentation=null;
            try{renderer.apply(next);}catch(error){fail(error);audio.interrupt(error.message);}
        });
    };
    const decoder=new FrameDecoder(message=>{
        if(message.type==='snapshot')present(message.snapshot);
        else if(message.type==='audio')audio.accept(message.event);
        else if(message.type==='clock')audio.clock(message);
        else if(message.type==='error'){renderer.error(message.error);for(const download of downloads.values())download.reject(new Error(message.error));downloads.clear();}
        else if(message.type==='closed')status.textContent=`Application closed (${message.result})`;
        else if(message.type==='file_begin') {
            const download=downloads.get(message.target);
            if(!download||download.size!==null||message.size>maximumFile)throw new Error('Unexpected or excessive file export');
            download.size=message.size;download.filename=basename(message.filename);
        } else if(message.type==='file_chunk') {
            const download=downloads.get(message.target);
            if(!download||download.size===null||message.position!==download.position||message.data.length>download.size-download.position)throw new Error('Invalid file export sequence');
            download.chunks.push(message.data);download.position+=message.data.length;
        } else if(message.type==='file_end') {
            const download=downloads.get(message.target);if(!download)throw new Error('Unknown file export completion');
            if(message.error)download.reject(new Error(message.error));
            else if(download.size===null||download.position!==download.size)download.reject(new Error('Incomplete file export'));
            else download.resolve(download);
        }
    });
    worker.onmessage=event=>{
        const message=event.data;
        try {
            if(message.type==='ready')readyResolve();
            else if(message.type==='accepted') {const request=pending.get(message.id);if(!request)throw new Error('Unknown input acknowledgment');pending.delete(message.id);pendingBytes-=request.size;request.resolve();}
            else if(message.type==='bytes') {decoder.push(message.bytes);worker.postMessage({type:'output_consumed',size:message.bytes.length});}
            else if(message.type==='error')throw new Error(message.message);
            else if(message.type==='diagnostic')status.textContent=message.message;
        } catch(error){fail(error);audio.interrupt(error.message);}
    };
    worker.onerror=event=>fail(new Error(event.message||'Application Worker failed'));
    worker.postMessage({type:'init',factorySource,wasmBytes},[wasmBytes.buffer]);
    const enableAudio=()=>audio.enable(workletSource,{microphone:true}).catch(error=>{status.textContent=error?.message||String(error);});
    enable.addEventListener('click',enableAudio);
    stop.addEventListener('click',()=>audio.stop().catch(fail));
    const close=()=>{if(!alive)return;alive=false;renderer.destroy();audio.stop().catch(()=>{});worker.postMessage({type:'close'});worker.terminate();fail(new Error('Application closed'));};
    environment.addEventListener('pagehide',close,{once:true});
    await ready;status.textContent='Ready. Enable browser audio when you want to use the microphone or speaker.';
    return {close,renderer,audio};
}
