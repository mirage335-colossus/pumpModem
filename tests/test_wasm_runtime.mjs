// Actual compiled C++ integration in a local VM realm. This test starts no
// browser or server and supplies every compiler asset before initialization.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {performance} from 'node:perf_hooks';

const [factoryPath,wasmPath]=process.argv.slice(2);
assert(factoryPath&&wasmPath,'usage: node test_wasm_runtime.mjs factory.js module.wasm');
const deny=()=>{throw new Error('Test forbids runtime network or secondary asset loading');};
const errors=[];
let phase='factory initialization';
const sandbox={console,WebAssembly,ArrayBuffer,SharedArrayBuffer:undefined,Uint8Array,Int8Array,
    Uint16Array,Int16Array,Uint32Array,Int32Array,Float32Array,Float64Array,BigInt64Array,BigUint64Array,
    TextEncoder,TextDecoder,performance,crypto:webcrypto,setTimeout,clearTimeout,setInterval,clearInterval,
    WorkerGlobalScope:function(){},location:{href:'blob:preloaded-datapump-test'},
    fetch:deny,XMLHttpRequest:deny,WebSocket:deny,WebTransport:deny,EventSource:deny,
    RTCPeerConnection:deny,importScripts:deny};
sandbox.self=sandbox;
vm.createContext(sandbox);vm.runInContext(fs.readFileSync(factoryPath,'utf8'),sandbox,{filename:factoryPath});
assert.equal(typeof sandbox.createDataPump,'function');
const compiled=new WebAssembly.Module(fs.readFileSync(wasmPath));
const imports=WebAssembly.Module.imports(compiled);
assert(!imports.some(item=>/socket|sockaddr|connect|listen|accept4?|websocket|pthread_create|emscripten_proxy/.test(item.name)),'Wasm imports a socket/proxy/thread capability');
const module=await sandbox.createDataPump({noInitialRun:true,wasmBinary:new Uint8Array(fs.readFileSync(wasmPath)),
    instantiateWasm(importObject,receive){const instance=new WebAssembly.Instance(compiled,importObject);receive(instance,compiled);return instance.exports;},
    locateFile:deny,print:()=>{},printErr:line=>errors.push(String(line)),onAbort:reason=>{throw new Error(`${phase}: ${reason}\n${errors.join('\n')}`);}});
const invoke=async(name,args=[])=>{
    phase=name;
    const result=await module.ccall(`datapump_web_${name}`,'number',args.map(()=> 'number'),args,{async:true});
    assert.equal(result,0,`C++ ${name} failed: ${errors.join('\n')}`);
};
const text=value=>{const bytes=new TextEncoder().encode(value),result=Buffer.alloc(4+bytes.length);result.writeUInt32LE(bytes.length);result.set(bytes,4);return result;};
const u32=value=>{const result=Buffer.alloc(4);result.writeUInt32LE(value>>>0);return result;};
const u64=value=>{const result=Buffer.alloc(8);result.writeBigUInt64LE(BigInt(value));return result;};
const frame=(type,payload=Buffer.alloc(0))=>Buffer.concat([Buffer.from('DPW1'),u32(type),u32(payload.length),payload]);
const send=async bytes=>{
    const pointer=module._malloc(bytes.length);assert(pointer);
    try{module.HEAPU8.set(bytes,pointer);await invoke('feed',[pointer,bytes.length]);}finally{module._free(pointer);}
};
let snapshot=null;const received=[];
const flush=()=>{
    for(let i=0;i<128;i++){
        const size=module._datapump_web_output_size();if(!size)return;
        assert(size<=8*1024*1024+12);const pointer=module._datapump_web_output();assert(pointer);
        const bytes=Buffer.from(module.HEAPU8.slice(pointer,pointer+size));assert.equal(bytes.subarray(0,4).toString(),'DPW1');
        const type=bytes.readUInt32LE(4),length=bytes.readUInt32LE(8);assert.equal(length+12,size);
        assert.equal(module._datapump_web_consume(size),0);received.push({type,bytes:bytes.subarray(12)});
        if(type===101)snapshot=JSON.parse(bytes.subarray(12).toString());
    }
    throw new Error('Output queue did not stay bounded');
};
const tick=async()=>{await invoke('tick');flush();};
const wait=async (predicate,timeout=10000)=>{
    const deadline=performance.now()+timeout;
    while(!predicate()) {assert(performance.now()<deadline,`C++ application progress deadline: ${errors.join('\n')} ${JSON.stringify(received.filter(m=>m.type===103).map(m=>m.bytes.toString()))} ${JSON.stringify(snapshot?.controls.filter(c=>c.text&&c.kind!=='bitmap').map(c=>({label:c.label,text:c.text,enabled:c.enabled})))}`);await tick();await new Promise(resolve=>setTimeout(resolve,5));}
};
const event=(kind,target,value='',flags=0)=>Buffer.concat([u32(1),u64(snapshot.generation),u64(BigInt(snapshot.ack)+1n),u64(target),u32(kind),u32(flags),u32(0),text(value),text('')]);
await invoke('create');await wait(()=>snapshot!==null);
assert.equal(snapshot.version,1);assert(snapshot.controls.length>0&&snapshot.tabs.length>0);
// A real shared declaration carries editing behavior; the JS test never maps
// a feature enum or reimplements a modem/application operation.
const editor=snapshot.controls.find(control=>control.kind==='text'&&control.enabled&&!control.readOnly&&control.multiline);
assert(editor,'shared application has no editable composer');
const literal='<script>literal data & Unicode α</script>';
await send(frame(2,event(1,editor.id,literal)));await tick();
assert(snapshot.controls.some(control=>control.kind==='text'&&control.text===literal),'Wasm editor failed to preserve literal source text');
assert.equal(snapshot.ack,'1');
// Real frame fragmentation and clock response work inside the compiled module.
const ping=Buffer.alloc(16);ping.writeBigUInt64LE(77n);ping.writeDoubleLE(1234.5,8);const packet=frame(15,ping);
await send(packet.subarray(0,7));await send(packet.subarray(7));flush();
const reply=received.find(message=>message.type===108);assert(reply);assert.equal(reply.bytes.readBigUInt64LE(),77n);assert.equal(reply.bytes.readDoubleLE(8),1234.5);
// Import an opaque binary attachment, pass it through the real modem/receiver,
// then export only the explicit completed-file capability. UI test selection
// follows published labels and IDs; production renderer has no feature mapping.
const control=label=>snapshot.controls.find(item=>item.label===label);
const act=async(kind,id,value='')=>{const sequence=BigInt(snapshot.ack)+1n,start=received.length;await send(frame(2,event(kind,id,value)));await tick();assert.equal(BigInt(snapshot.ack),sequence,'application rejected UI fixture event');assert(!received.slice(start).some(m=>m.type===103),'application reported UI fixture error');};
const modem=snapshot.controls.find(item=>item.options?.some(option=>option.label==='Robust Modem'));
assert(modem,'modem selector missing');
await act(2,modem.id,modem.options.find(option=>option.label==='Robust Modem').id);
await wait(()=>control('Simulation')?.enabled);
// Exercise browser glyph measurement through the compiled C++/libc++ parser,
// then verify the common document engine refines its published geometry.
const planner=snapshot.tabs.find(tab=>tab.label==='Link planner');
assert(planner,'document page missing');await act(11,planner.id);
assert(snapshot.document,'document navigation did not publish a document');
const paragraph=snapshot.document.children.find(node=>node.kind==='text'&&node.height===0&&node.geometry?.allocated&&node.measure);
assert(paragraph,'document has no measurable auto-height paragraph');
const priorHeight=paragraph.geometry.frame.h,measurementId=paragraph.measure.id,glyphHeight=priorHeight+37;
assert(glyphHeight<=16384);await act(15,'0',`${measurementId} ${glyphHeight}\n`);
const refined=snapshot.document.children.find(node=>node.measure?.id===measurementId);
assert(refined,'measurement lost its published identity');
assert.equal(refined.measure.height,glyphHeight,'Wasm did not retain the browser glyph measurement');
assert(refined.geometry.frame.h>priorHeight,'Browser measurement did not refine shared C++ geometry');
const consoleTab=snapshot.tabs.find(tab=>tab.label==='Console');assert(consoleTab);
await act(11,consoleTab.id);assert.equal(snapshot.document,null);
await wait(()=>control('Simulation')?.enabled);
const simulation=control('Simulation');assert(simulation,'simulation selector missing');
await act(2,simulation.id,simulation.options.find(option=>option.label==='Yes').id);
const loss=control('Path loss');assert(loss,'link loss control missing');await act(1,loss.id,'6 dB');
await act(1,control('Short ≤16 B target SNR (dB-Hz)').id,'80');
await act(1,control('Long / file target SNR (dB-Hz)').id,'80');
await act(4,control('Attach file').id);
await wait(()=>snapshot.service?.kind==='open_file');
const incoming=snapshot.service.id,fixture=Buffer.from([0,1,2,3,255,192,128,68,97,116,97,80,117,109,112,10]);
await send(frame(10,Buffer.concat([event(13,incoming,'a.bin'),u64(fixture.length)])));
for(const [start,end] of [[0,5],[5,fixture.length]])await send(frame(11,Buffer.concat([u64(incoming),u64(start),u32(end-start),fixture.subarray(start,end)])));
await send(frame(12,u64(incoming)));await tick();
assert(control('Transmit'),JSON.stringify(snapshot.controls.map(c=>({label:c.label,text:c.text,kind:c.kind,enabled:c.enabled}))));
await wait(()=>control('Transmit')?.enabled);
await act(4,control('Transmit').id);
await wait(()=>control('Files in memory')?.records?.length>0,180000);
const files=control('Files in memory');await act(7,files.id,files.records[0].id);
await wait(()=>snapshot.service?.kind==='save_file');const outgoing=snapshot.service.id;
await send(frame(13,event(13,outgoing,'copy.bin')));await tick();
await wait(()=>received.some(message=>message.type===107&&message.bytes.readBigUInt64LE()===BigInt(outgoing)));
const exported=received.filter(message=>message.type>=105&&message.type<=107&&message.bytes.readBigUInt64LE()===BigInt(outgoing));
assert.equal(exported[0].type,105);const nameLength=exported[0].bytes.readUInt32LE(8);
assert.equal(exported[0].bytes.subarray(12,12+nameLength).toString(),'copy.bin');
assert.equal(exported[0].bytes.readBigUInt64LE(12+nameLength),BigInt(fixture.length));
let exportedCount=0;const pieces=[];
for(const message of exported) {
    if(message.type===106){assert.equal(message.bytes.readBigUInt64LE(8),BigInt(exportedCount));const n=message.bytes.readUInt32LE(16);assert.equal(message.bytes.length,20+n);pieces.push(message.bytes.subarray(20));exportedCount+=n;}
    if(message.type===107)assert.equal(message.bytes.readUInt32LE(8),0,'C++ save/export failed');
}
assert.deepEqual(Buffer.concat(pieces),fixture,'binary file changed through C++ import/modem/export');
// Reject an unavailable file capability and complete its error, so a browser
// waiting for an explicit save cannot hang or receive unrelated output bytes.
await send(frame(13,event(13,'999999','received.bin')));flush();
assert(received.some(message=>message.type===107&&message.bytes.readBigUInt64LE()===999999n),'rejected file export omitted completion');
// Pump repeatedly across separate JS calls to exercise actual fiber root
// switching, not just a main() that never returns to the embedding host.
for(let i=0;i<50;i++){await tick();await new Promise(resolve=>setTimeout(resolve,2));}
await module.ccall('datapump_web_destroy',null,[],[],{async:true});
assert(!errors.some(error=>/Aborted|out of bounds|unreachable/i.test(error)),errors.join('\n'));
console.log('Actual Wasm application initialization, task polling, literal edits, framed clock, binary file round trip and revoked file capability passed; no network APIs available');
