import {BrowserAudio} from './browser_audio.mjs';
import {decodeFrame,encodeAudio} from './protocol.mjs';
// The embedding application supplies transport and its authorized server-file
// service. Neither this adapter nor the isolated renderer opens a connection.
const sourceLimit=1024*1024;
function encoded(text){const bytes=new TextEncoder().encode(text);let raw='';for(let i=0;i<bytes.length;i+=32768)raw+=String.fromCharCode(...bytes.subarray(i,i+32768));return btoa(raw);}
export function createHostedFrontend({container,rendererSource,protocolSource,styleText='',workletSource,send,handleService=null,status=()=>{}}) {
    if(!container?.ownerDocument||typeof send!=='function')throw new TypeError('A container and bounded host transport callback are required');
    for(const asset of [rendererSource,protocolSource,styleText,workletSource])if(typeof asset!=='string'||asset.length>sourceLimit)throw new RangeError('Preloaded bounded renderer assets are required');
    const document=container.ownerDocument,window=document.defaultView;
    const nonce=Array.from(window.crypto.getRandomValues(new Uint8Array(32)),byte=>byte.toString(16).padStart(2,'0')).join('');
    const frame=document.createElement('iframe');frame.setAttribute('sandbox','allow-scripts');frame.setAttribute('title','Data Pump');
    frame.style.width='100%';frame.style.minHeight='36rem';frame.style.border='0';
    const assets={renderer:encoded(rendererSource),protocol:encoded(protocolSource),style:encoded(styleText)};
    // Only reviewed static code assets and generated hex nonce are interpolated.
    // Acoustic text, filenames, fields and host file paths never enter srcdoc.
    frame.srcdoc=`<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline' blob:; style-src 'unsafe-inline'; connect-src 'none'; img-src data: blob:; form-action 'none'; base-uri 'none'"><main id="root"></main><script type="module">
const nonce=${JSON.stringify(nonce)},assets=${JSON.stringify(assets)};
window.addEventListener('error',event=>parent.postMessage({type:'datapump-init-error',nonce,message:String(event.message).slice(0,512)},'*'));
const decode=s=>new TextDecoder().decode(Uint8Array.from(atob(s),c=>c.charCodeAt(0)));
const style=document.createElement('style');style.textContent=decode(assets.style);document.head.append(style);
const blob=source=>URL.createObjectURL(new Blob([decode(source)],{type:'text/javascript'}));
const rendererURL=blob(assets.renderer),protocolURL=blob(assets.protocol);
const [{Renderer},{encodeEvent,encodeViewport}]=await Promise.all([import(rendererURL),import(protocolURL)]);
URL.revokeObjectURL(rendererURL);URL.revokeObjectURL(protocolURL);
let port,renderer,pendingService=null,nextService=0;
window.addEventListener('message',event=>{
 if(port||event.source!==parent||event.data?.type!=='datapump-port'||event.data.nonce!==nonce||event.ports.length!==1)return;
 port=event.ports[0];
 const input=bytes=>port.postMessage({type:'input',bytes},[bytes.buffer]);
 renderer=new Renderer(document.getElementById('root'),event=>input(encodeEvent(event)),{
  resize:(width,height)=>{const bytes=encodeViewport(Math.max(240,Math.min(4096,width)),Math.max(240,Math.min(4096,height)));input(bytes);},
  platformService:(request,signal,authorize)=>new Promise((resolve,reject)=>{
   const id=++nextService;pendingService={id,resolve,reject};
   const abort=()=>{if(pendingService?.id===id){pendingService=null;port.postMessage({type:'service-cancel',id});reject(new Error('Service withdrawn'));}};
   signal.addEventListener('abort',abort,{once:true});
   port.postMessage({type:'service',id,request,event:authorize(request.value)});
  })});
 let presentation=null,paintPending=false,closed=false;
 port.onmessage=message=>{const data=message.data;
  if(data?.type==='snapshot'){
   presentation=data.snapshot;
   if(!paintPending){paintPending=true;requestAnimationFrame(()=>{paintPending=false;if(!closed){const next=presentation;presentation=null;renderer.apply(next);}});}
  }
  else if(data?.type==='error')renderer.error(String(data.error));
  else if(data?.type==='service-result'&&pendingService?.id===data.id){const job=pendingService;pendingService=null;job.resolve(data.result);}
  else if(data?.type==='close'){closed=true;renderer.destroy();port.close();}
 };
 port.start();port.postMessage({type:'bound'});
});
parent.postMessage({type:'datapump-ready',nonce},'*');
</script>`;
    let port=null,alive=true,bound=false,currentService=null,currentGeneration='0',removeHandshake=()=>{};const pending=[];let pendingBytes=0;const serviceJobs=new Map();
    const audio=new BrowserAudio(event=>send(encodeAudio(event)),{environment:window,status});
    const ready=new Promise((resolve,reject)=>{
        const handshake=event=>{
            if(!alive||event.source!==frame.contentWindow||event.data?.nonce!==nonce)return;
            if(event.data.type==='datapump-init-error'){removeHandshake();reject(new Error(event.data.message));return;}
            if(event.data.type!=='datapump-ready')return;
            removeHandshake();
            const channel=new window.MessageChannel();port=channel.port1;
            port.onmessage=async event=>{
                const message=event.data;
                try {
                    if(message?.type==='bound'){bound=true;for(const item of pending)port.postMessage(item);pending.length=0;pendingBytes=0;resolve();}
                    else if(message?.type==='input') {
                        if(!(message.bytes instanceof Uint8Array)||message.bytes.length<12||message.bytes.length>8*1024*1024+12)throw new Error('Invalid renderer input');
                        const header=new DataView(message.bytes.buffer,message.bytes.byteOffset,12);
                        if(message.bytes[0]!==68||message.bytes[1]!==80||message.bytes[2]!==87||message.bytes[3]!==49||
                           ![1,2].includes(header.getUint32(4,true))||header.getUint32(8,true)!==message.bytes.length-12)
                            throw new Error('Renderer input exceeds presentation authority');
                        await send(message.bytes);
                    } else if(message?.type==='service') {
                        if(!handleService)throw new Error('The host has no authorized file service');
                        if(!currentService||message.request?.id!==currentService.id||message.request?.kind!==currentService.kind||
                           message.event?.version!==1||message.event?.kind!==13||message.event?.target!==currentService.id||
                           message.event?.generation!==currentGeneration||!/^[1-9][0-9]{0,19}$/.test(message.event?.sequence||''))
                            throw new Error('Unknown or withdrawn host service');
                        if(serviceJobs.size>=1)throw new Error('Concurrent host file services are unavailable');
                        const controller=new AbortController();serviceJobs.set(message.id,controller);
                        try {const result=await handleService({request:{...currentService},event:{version:1,generation:currentGeneration,
                            sequence:message.event.sequence,target:currentService.id,kind:13,value:currentService.value}},controller.signal);
                            if(serviceJobs.has(message.id))port.postMessage({type:'service-result',id:message.id,result});}
                        finally {serviceJobs.delete(message.id);}
                    } else if(message?.type==='service-cancel'){serviceJobs.get(message.id)?.abort();serviceJobs.delete(message.id);}
                    else throw new Error('Unknown isolated renderer message');
                } catch(error){if(message?.type==='service')port.postMessage({type:'service-result',id:message.id,result:{error:error?.message||String(error)}});port.postMessage({type:'error',error:error?.message||String(error)});status({state:'error',reason:error?.message||String(error)});}
            };
            port.start();frame.contentWindow.postMessage({type:'datapump-port',nonce},'*',[channel.port2]);
        };
        const timeout=window.setTimeout(()=>{removeHandshake();reject(new Error('Isolated renderer initialization timed out'));},30000);
        removeHandshake=()=>{window.removeEventListener('message',handshake);window.clearTimeout(timeout);};
        window.addEventListener('message',handshake);
        frame.addEventListener('error',()=>{window.removeEventListener('message',handshake);reject(new Error('Isolated renderer failed to initialize'));},{once:true});
    });
    container.append(frame);
    const present=message=>{
        if(!alive)throw new Error('Renderer closed');
        if(bound)port.postMessage(message);
        else {const size=JSON.stringify(message).length;if(pendingBytes+size>8*1024*1024)throw new Error('Renderer initialization queue exceeded bound');pendingBytes+=size;pending.push(message);}
    };
    return {frame,ready,audio,
        enableAudio:()=>audio.enable(workletSource,{microphone:true}),
        // Call only with one complete C++ frame, from the authenticated host
        // application session. File frames go exclusively to its file service.
        feed(bytes){const message=decodeFrame(bytes);
            if(message.type==='snapshot') {
                const next=message.snapshot.service;
                if(currentService&&(next?.id!==currentService.id||message.snapshot.generation!==currentGeneration))
                    for(const job of serviceJobs.values())job.abort();
                currentService=next;currentGeneration=message.snapshot.generation;
                present({type:'snapshot',snapshot:message.snapshot});
            }
            else if(message.type==='error')present({type:'error',error:message.error});
            else if(message.type==='audio')audio.accept(message.event);
            else if(message.type==='clock')audio.clock(message);
            else if(message.type==='closed')present({type:'error',error:'Application closed'});
            else throw new Error('Raw file exports must remain in the authorized host file service');},
        dispose(){if(!alive)return;alive=false;removeHandshake();for(const job of serviceJobs.values())job.abort();serviceJobs.clear();port?.postMessage({type:'close'});port?.close();audio.stop().catch(()=>{});frame.remove();}
    };
}
