import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
import {Renderer,validText,eventKinds} from '../web/renderer.mjs';
import {encodeEvent,encodeViewport,encodeAudio,FrameDecoder,decodeFrame,maxFrame} from '../web/protocol.mjs';
class Node {
    constructor(doc,tag){this.ownerDocument=doc;this.tagName=tag;this.children=[];this.parentNode=null;this.listeners={};this.style={};this.attributes={};this.classList={add:()=>{}};this.value='';this.selectionStart=this.selectionEnd=this.scrollTop=0;this.clientHeight=this.scrollHeight=100;this.textContent='';this.hidden=false;}
    set innerHTML(_){throw new Error('Renderer must never interpret HTML');}
    get isConnected(){return this===this.ownerDocument.root||Boolean(this.parentNode?.isConnected);}
    get lastElementChild(){return this.children.at(-1);}
    setAttribute(k,v){this.attributes[k]=v;}
    append(...nodes){for(const n of nodes)this.insertBefore(n,null);}
    insertBefore(node,before){node.remove();const at=before?this.children.indexOf(before):-1;if(at<0)this.children.push(node);else this.children.splice(at,0,node);node.parentNode=this;}
    remove(){if(this.parentNode){this.parentNode.children=this.parentNode.children.filter(n=>n!==this);this.parentNode=null;}}
    replaceChildren(...nodes){for(const n of this.children)n.parentNode=null;this.children=[];this.append(...nodes);}
    addEventListener(type,fn){(this.listeners[type]??=[]).push(fn);}
    removeEventListener(type,fn){this.listeners[type]=(this.listeners[type]||[]).filter(f=>f!==fn);}
    dispatch(type,extra={}){const event={target:this,preventDefault(){this.prevented=true;},...extra};for(const fn of this.listeners[type]||[])fn(event);return event;}
    focus(){this.ownerDocument.activeElement=this;}
    setSelectionRange(start,end){this.selectionStart=start;this.selectionEnd=end;}
    getContext(){return {createImageData:(w,h)=>({data:new Uint8ClampedArray(w*h*4)}),putImageData:()=>{}};}
}
const doc={createElement(tag){return new Node(this,tag);},defaultView:{navigator:{}}};doc.root=new Node(doc,'root');
const sent=[];const renderer=new Renderer(doc.root,event=>sent.push(event));
const control={id:'1',kind:'text',label:'Generic extension',help:'<img src=x>',enabled:true,stretch:1,multiline:true,readOnly:false,byteLimit:128,history:'0',cursorEnd:'0',text:'<script>literal</script>',options:[],submitEnter:false,submitCtrlEnter:true};
const snapshot={version:1,generation:'1',ack:'0',title:'Fixture',tabs:[],controls:[control],overlay:[],document:null,service:null,layers:{showBackground:true,enableBackground:true,showOverlay:false,enableOverlay:false}};
renderer.apply(snapshot);const input=renderer.views.get('1').input;
assert.equal(input.value,control.text);assert.equal(renderer.views.get('1').label.textContent,'Generic extension');
input.dispatch('compositionstart');input.value='入力';input.dispatch('input',{isComposing:true});assert.equal(sent.length,0);
input.dispatch('keydown',{key:'Enter',isComposing:true,ctrlKey:true});assert.equal(sent.length,0);
input.dispatch('compositionend');assert.equal(sent.length,1);assert.equal(sent[0].kind,eventKinds.edit);assert.equal(sent[0].value,'入力');
input.value='local edit';input.dispatch('input');renderer.apply({...snapshot,ack:'1',controls:[{...control,text:'server old'}]});assert.equal(input.value,'local edit','unacknowledged local input was overwritten');
const key=input.dispatch('keydown',{key:'Enter',ctrlKey:false,shiftKey:false});assert.equal(key.prevented,undefined,'ordinary multiline Enter must insert newline');
const before=sent.length;input.dispatch('keydown',{key:'Enter',ctrlKey:true,shiftKey:false});assert.equal(sent.length,before+2);assert.equal(sent.at(-1).kind,eventKinds.submit);
renderer.apply({...snapshot,generation:'2',ack:String(sent.length),controls:[{...control,history:'1',text:'restricted'}]});
const replacement=renderer.views.get('1').input;assert.notEqual(input,replacement);assert.equal(replacement.value,'restricted');
const after=sent.length;input.value='stale unsafe';input.dispatch('compositionend');assert.equal(sent.length,after,'withdrawn editor callback restored old text');
assert.equal(validText('\0',10),false);assert.equal(validText('é',1),false);assert.equal(validText('a\nb',10,false),false);
assert.throws(()=>renderer.apply({...snapshot,version:2}));renderer.destroy();
const encoded=encodeEvent({version:1,generation:'18446744073709551615',sequence:'2',target:'3',kind:1,value:'literal',amount:-1});
assert.equal(new TextDecoder().decode(encoded.subarray(0,4)),'DPW1');assert.equal(new DataView(encoded.buffer).getUint32(4,true),2);
assert.equal(new DataView(encoded.buffer).getBigUint64(16,true),0xffffffffffffffffn);
assert.equal(encodeViewport(360,640).length,20);
assert.throws(()=>encodeAudio({kind:'capture',generation:'1',stream:'1',position:0,samples:new Float32Array(4097)}));
function output(type,payload){const frame=new Uint8Array(12+payload.length);frame.set([68,80,87,49]);const v=new DataView(frame.buffer);v.setUint32(4,type,true);v.setUint32(8,payload.length,true);frame.set(payload,12);return frame;}
const frame=output(101,new TextEncoder().encode(JSON.stringify(snapshot))),received=[];const decoder=new FrameDecoder(v=>received.push(v));
for(let i=0;i<frame.length;i+=7)decoder.push(frame.subarray(i,i+7));decoder.end();assert.equal(received[0].snapshot.title,'Fixture');
const bad=new Uint8Array(frame);new DataView(bad.buffer).setUint32(8,maxFrame+1,true);assert.throws(()=>decodeFrame(bad));
// Execute the actual AudioWorklet against deterministic render quanta, without
// a microphone. This verifies queue/gap/drain behavior, not device fidelity.
let Processor;const messages=[];const sandbox={AudioWorkletProcessor:class{constructor(){this.port={postMessage:m=>messages.push(m)};}},registerProcessor:(_,C)=>{Processor=C;},Float32Array,Math,Number,String,currentFrame:0,sampleRate:48000};
vm.createContext(sandbox);vm.runInContext(await readFile(new URL('../web/audio_worklet.js',import.meta.url),'utf8'),sandbox);
const proc=new Processor({processorOptions:{generation:'1'}});
proc.message({kind:'capture_start',generation:'1',stream:'7'});proc.process([[new Float32Array([.1,.2,.3])]],[[new Float32Array(3),new Float32Array(3)]]);
assert.equal(messages.at(-1).kind,'capture');assert.equal(messages.at(-1).samples.length,3);
proc.message({kind:'capture_ack',generation:'1',frames:3});proc.message({kind:'capture_stop',generation:'1',stream:'7'});
proc.message({kind:'playback_start',generation:'1',stream:'2',channels:0,gain:1,presentationEpoch:0});
proc.message({kind:'playback_pcm',generation:'1',stream:'2',position:0,startFrame:2,samples:new Float32Array([.25,.5])});proc.message({kind:'playback_end',generation:'1',stream:'2',position:2});
const out=[new Float32Array(5),new Float32Array(5)];proc.process([], [out]);
assert.deepEqual([...out[0]],[0,0,.25,.5,0]);assert.deepEqual([...out[1]],[0,0,0,0,0]);assert.equal(messages.at(-1).kind,'playback_endpoint');assert.equal(messages.at(-1).frame,4);
const gap=new Processor({processorOptions:{generation:'2'}});gap.message({kind:'playback_start',generation:'2',stream:'1',channels:2,gain:1,startFrame:0});gap.message({kind:'playback_pcm',generation:'2',stream:'1',position:1,samples:new Float32Array([1])});assert.equal(messages.at(-1).kind,'interrupted');
const starved=new Processor({processorOptions:{generation:'3'}});starved.message({kind:'playback_start',generation:'3',stream:'1',channels:2,gain:1});starved.process([],[[new Float32Array(128),new Float32Array(128)]]);assert.equal(messages.at(-1).kind,'playback_ready','readiness must not schedule output before first PCM');starved.message({kind:'playback_pcm',generation:'3',stream:'1',position:0,startFrame:0,samples:new Float32Array([.5])});starved.process([],[[new Float32Array(128),new Float32Array(128)]]);assert.equal(messages.at(-1).kind,'interrupted');
console.log('Generic DOM/IME/withdrawal, binary framing and bounded PCM scheduling tests passed');
// Stable record DOM identity exposes every prefix without waiting for a byte.
const rowsDoc={...doc,root:null};rowsDoc.createElement=function(tag){return new Node(this,tag);};rowsDoc.root=new Node(rowsDoc,'root');
const rowsRenderer=new Renderer(rowsDoc.root,()=>{});
const list={...control,id:'2',kind:'list',records:[{id:'pending-1',enabled:true,activatable:false,cells:[{text:'0',tone:0,bold:false}]}],selected:'pending-1'};
rowsRenderer.apply({...snapshot,controls:[list]});const retainedRow=rowsRenderer.views.get('2').rows.get('pending-1');
for(const prefix of ['00','001']){rowsRenderer.apply({...snapshot,controls:[{...list,records:[{...list.records[0],cells:[{text:prefix}]}]}]});assert.equal(rowsRenderer.views.get('2').rows.get('pending-1'),retainedRow);assert.equal(retainedRow.children[0].textContent,prefix);}
rowsRenderer.apply({...snapshot,generation:'2',controls:[{...list,records:[]}]});rowsRenderer.apply({...snapshot,generation:'1',controls:[list]});assert.equal(rowsRenderer.views.get('2').rows.size,0,'old snapshots must not restore withdrawn rows');rowsRenderer.destroy();
// A suspended Wasm export must not be reentered by feed or timer callbacks.
let activeExports=0,tickCallback;const calls=[],workerMessages=[];
const fakeRuntime={HEAPU8:new Uint8Array(4096),_malloc:()=>64,_free:()=>{},_datapump_web_output_size:()=>0,
    ccall:async name=>{assert.equal(activeExports,0,'Asyncify exports overlapped');activeExports++;calls.push(name);await new Promise(resolve=>setTimeout(resolve,2));activeExports--;return 0;}};
const workerContext={Uint8Array,Float32Array,Math,Number,String,Object,Promise,Error,URL:{createObjectURL:()=> 'blob:local',revokeObjectURL:()=>{}},Blob:class{},WebAssembly,
    setInterval:callback=>{tickCallback=callback;return 1;},clearInterval:()=>{},postMessage:m=>workerMessages.push(m),close:()=>{},
    importScripts:()=>{workerContext.createDataPump=async()=>fakeRuntime;}};
workerContext.self=workerContext;vm.createContext(workerContext);vm.runInContext(await readFile(new URL('../web/wasm_worker.js',import.meta.url),'utf8'),workerContext);
workerContext.onmessage({data:{type:'init',factorySource:'preloaded fixture',wasmBytes:new Uint8Array([0])}});
await new Promise(resolve=>setTimeout(resolve,10));assert(workerMessages.some(m=>m.type==='ready'));
workerContext.onmessage({data:{type:'feed',id:1,bytes:encodeViewport(320,640)}});
workerContext.onmessage({data:{type:'feed',id:2,bytes:encodeViewport(640,480)}});tickCallback();tickCallback();
await new Promise(resolve=>setTimeout(resolve,20));assert.deepEqual(calls,['datapump_web_create','datapump_web_feed','datapump_web_feed','datapump_web_tick']);
assert.equal(workerMessages.filter(m=>m.type==='accepted').length,2);assert.throws(()=>workerContext.fetch('https://invalid.test'));
// Parent-side checks authenticate the opaque frame by Window identity and nonce,
// and never escalate presentation frames into audio/file service authority.
const {createHostedFrontend}=await import('../web/hosted.mjs');
const channels=[],hostSent=[],hostServices=[],hostStatus=[],windowListeners={};
const hostWindow={navigator:{},setTimeout,clearTimeout,crypto:{getRandomValues:bytes=>{bytes.fill(7);return bytes;}},
    addEventListener:(type,fn)=>{windowListeners[type]=fn;},removeEventListener:(type,fn)=>{if(windowListeners[type]===fn)delete windowListeners[type];},
    MessageChannel:class{constructor(){this.port1={postMessage:m=>this.output.push(m),start:()=>{},close:()=>{}};this.port2={};this.output=[];channels.push(this);}}};
const hostedDoc={defaultView:hostWindow,createElement(tag){const n=new Node(this,tag);if(tag==='iframe')n.contentWindow={postMessage:(...args)=>{n.posted=args;}};return n;}};
hostedDoc.root=new Node(hostedDoc,'root');
const hosted=createHostedFrontend({container:hostedDoc.root,rendererSource:'fixture',protocolSource:'fixture',styleText:'',workletSource:'fixture',send:async bytes=>hostSent.push(bytes),handleService:async request=>{hostServices.push(request);return {cancelled:true};},status:state=>hostStatus.push(state)});
assert.equal(hosted.frame.attributes.sandbox,'allow-scripts');assert(hosted.frame.srcdoc.includes("connect-src 'none'"));assert(!hosted.frame.attributes.sandbox.includes('allow-same-origin'));
const nonce=hosted.frame.srcdoc.match(/const nonce="([0-9a-f]+)"/)[1];
windowListeners.message({source:{},data:{type:'datapump-ready',nonce}});assert.equal(channels.length,0);
windowListeners.message({source:hosted.frame.contentWindow,data:{type:'datapump-ready',nonce}});assert.equal(channels.length,1);
await channels[0].port1.onmessage({data:{type:'bound'}});await hosted.ready;
await channels[0].port1.onmessage({data:{type:'input',bytes:encodeAudio({kind:'configure',generation:'1',rate:48000})}});assert.equal(hostSent.length,0);
await channels[0].port1.onmessage({data:{type:'input',bytes:encodeViewport(320,640)}});assert.equal(hostSent.length,1);
const serviceSnapshot={...snapshot,generation:'3',service:{id:'77',kind:'open_file',title:'Authorized file',value:'safe'}};
hosted.feed(output(101,new TextEncoder().encode(JSON.stringify(serviceSnapshot))));
await channels[0].port1.onmessage({data:{type:'service',id:1,request:{...serviceSnapshot.service,id:'78'},event:{version:1,generation:'3',sequence:'1',target:'78',kind:13}}});assert.equal(hostServices.length,0);
await channels[0].port1.onmessage({data:{type:'service',id:2,request:{...serviceSnapshot.service,value:'untrusted path'},event:{version:1,generation:'3',sequence:'2',target:'77',kind:13,value:'untrusted path'}}});assert.equal(hostServices.length,1);assert.equal(hostServices[0].event.value,'safe');hosted.dispose();
console.log('Record prefixes, serialized Asyncify dispatch and isolated-host authority checks passed');
const {BrowserAudio}=await import('../web/browser_audio.mjs');
const scheduled=[];const clockEnvironment={performance:{timeOrigin:1000000,now:()=>0},clearTimeout:()=>{}};
const browserAudio=new BrowserAudio(()=>{}, {environment:clockEnvironment});browserAudio.generation=1n;browserAudio.clockOffset=1;browserAudio.clockAt=1000;browserAudio.clockUncertainty=.005;
browserAudio.context={sampleRate:48000,currentTime:10,getOutputTimestamp:()=>({contextTime:9.9,performanceTime:0})};browserAudio.node={port:{postMessage:event=>scheduled.push(event)},disconnect:()=>{}};
browserAudio.accept({kind:2,generation:'1',stream:'1',rate:48000,gain:1,channels:0,presentationEpoch:0});
assert.equal(scheduled.length,1);assert.equal(scheduled[0].startFrame,undefined);
browserAudio.accept({kind:3,generation:'1',stream:'1',position:0,rate:48000,gain:1,channels:0,presentationEpoch:1001.5,samples:new Float32Array([.5])});
assert.equal(scheduled.length,2);assert(Math.abs(scheduled[1].startFrame-499200)<=1,'playback schedule ignored physical output/render-ahead offset');
console.log('Physical output timestamp mapping passed');

// A rejected older permission request must not close a newer audio context.
let rejectOldPermission;const contexts=[];let permissionCalls=0;
const tracks=[];
const audioEnvironment={...clockEnvironment,navigator:{mediaDevices:{getUserMedia:()=>++permissionCalls===1?new Promise((_,reject)=>{rejectOldPermission=reject;}):Promise.resolve({getTracks:()=>tracks,getAudioTracks:()=>tracks})}},
    AudioContext:class {constructor(){this.state='running';this.sampleRate=48000;this.audioWorklet={addModule:async()=>{}};contexts.push(this);}resume(){return Promise.resolve();}async close(){this.state='closed';}createMediaStreamSource(){return {connect(){},disconnect(){}};}},
    AudioWorkletNode:class {constructor(){this.port={postMessage(){}};}connect(){}disconnect(){}},
    URL:{createObjectURL:()=> 'blob:preloaded-worklet',revokeObjectURL(){}},Blob,
    setTimeout:()=>0};
const overlappingAudio=new BrowserAudio(async event=>{if(event.kind==='interrupted')overlappingAudio.accept({kind:6,generation:event.generation});}, {environment:audioEnvironment});
const oldEnable=overlappingAudio.enable('preloaded');const oldFailure=assert.rejects(oldEnable,/old permission denied/);
await overlappingAudio.enable('preloaded');rejectOldPermission(new Error('old permission denied'));await oldFailure;
assert.equal(overlappingAudio.context,contexts[1]);assert.equal(contexts[1].state,'running');
await overlappingAudio.stop();
console.log('Overlapping microphone permission lifecycle passed');

// Permission arriving after resume failure must release its microphone tracks.
let grantLate,trackStops=0;
const failedResumeEnvironment={...audioEnvironment,
    navigator:{mediaDevices:{getUserMedia:()=>new Promise(resolve=>{grantLate=resolve;})}},
    AudioContext:class extends audioEnvironment.AudioContext {resume(){return Promise.reject(new Error('resume failed'));}}};
const failedAudio=new BrowserAudio(async()=>{}, {environment:failedResumeEnvironment});
await assert.rejects(failedAudio.enable('preloaded'),/resume failed/);
grantLate({getTracks:()=>[{stop(){trackStops++;}}]});await Promise.resolve();await Promise.resolve();
assert.equal(trackStops,1);assert.equal(failedAudio.context,null);
console.log('Late microphone grant after failed resume is released');

const throwsOnNotify=new BrowserAudio(()=>{throw new Error('disconnected host');},{environment:audioEnvironment});
throwsOnNotify.context={state:'running',close:async()=>{}};
let cleaned=0;throwsOnNotify.stream={getTracks:()=>[{stop(){cleaned++;}}]};
await throwsOnNotify.stop();assert.equal(cleaned,1);assert.equal(throwsOnNotify.context,null);
console.log('Synchronous host notification failure does not prevent audio cleanup');

// Old cancellation is acknowledged only after close; a new configure waits for
// C++ to confirm that both capture and flushed playback have retired.
const finishCloses=[],lifecycleSent=[];
const restartEnvironment={...audioEnvironment,
    AudioContext:class extends audioEnvironment.AudioContext {close(){return new Promise(resolve=>{finishCloses.push(()=>{this.state='closed';resolve();});});}}};
const restarting=new BrowserAudio(async event=>{
    lifecycleSent.push(event);
    if(event.kind==='playback_cancelled')restarting.accept({kind:6,generation:event.generation});
},{environment:restartEnvironment});
await restarting.enable('preloaded');
const stoppedAudio=restarting.stop();const intermediateAudio=restarting.enable('preloaded');const reenabledAudio=restarting.enable('preloaded');
restarting.accept({kind:1,generation:'1',stream:'1'});
restarting.accept({kind:5,generation:'1',stream:'2'});
await Promise.resolve();await Promise.resolve();
assert.equal(lifecycleSent.filter(e=>e.kind==='playback_cancelled').length,0);
assert.equal(lifecycleSent.filter(e=>e.kind==='configure').length,1);
finishCloses[1]();await Promise.resolve();await Promise.resolve();
assert.equal(lifecycleSent.filter(e=>e.kind==='configure').length,1,'unconfigured replacement bypassed the old stop barrier');
finishCloses[0]();await stoppedAudio;await intermediateAudio;await reenabledAudio;
assert.equal(lifecycleSent.filter(e=>e.kind==='playback_cancelled').length,1);
assert.equal(lifecycleSent.filter(e=>e.kind==='configure').length,2);
const finalStop=restarting.stop();await Promise.resolve();finishCloses[2]();restarting.accept({kind:6,generation:'3'});await finalStop;
// Empty playback has no PCM epoch, and therefore needs no scheduled sample.
const empty=new Processor({processorOptions:{generation:'9'}});
empty.message({kind:'playback_start',generation:'9',stream:'1',channels:0,gain:1});
empty.message({kind:'playback_end',generation:'9',stream:'1',position:0});
assert.equal(messages.at(-1).kind,'playback_endpoint');assert.equal(messages.at(-1).position,0);assert.equal(messages.at(-1).empty,true);assert.equal(empty.play,null);
console.log('Stop/flush/idle/re-enable ordering and empty output completion passed');
