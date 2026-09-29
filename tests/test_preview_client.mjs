import assert from 'node:assert/strict';
import {InputBatcher,feedFrames,boot} from '../tools/preview.mjs';
import {encodeViewport} from '../web/protocol.mjs';

const packet=encodeViewport(640,480);
let accept;const sent=[];
const batcher=new InputBatcher(bytes=>{sent.push(bytes);return new Promise(resolve=>accept=resolve);},{delay:0});
let resolved=false;
const first=batcher.send(packet).then(()=>resolved=true);
const second=batcher.send(packet);
await new Promise(resolve=>setTimeout(resolve,10));
assert.equal(resolved,false,'capture credits wait for host acceptance');
assert.equal(sent.length,1);assert.equal(sent[0].length,packet.length*2);
accept();await Promise.all([first,second]);assert.equal(batcher.bytes,0);

const delivered=[];feedFrames(sent[0],bytes=>delivered.push(bytes));
assert.equal(delivered.length,2);assert.deepEqual(delivered[0],packet);
assert.throws(()=>feedFrames(sent[0].slice(0,-1),()=>{}),/Invalid|Truncated/);
const invalid=packet.slice();invalid[0]=0;assert.throws(()=>feedFrames(invalid,()=>{}),/Invalid/);

// A single legal frame larger than the batching target is still accepted.
const large=new Uint8Array(100000);large.set(packet.subarray(0,12));
new DataView(large.buffer).setUint32(8,large.length-12,true);
const sizes=[];const largeBatcher=new InputBatcher(async b=>sizes.push(b.length),{delay:0});
await Promise.all([largeBatcher.send(large),largeBatcher.send(packet)]);
assert.deepEqual(sizes,[large.length,packet.length]);

const bounded=new InputBatcher(()=>new Promise(()=>{}),{delay:1000,limit:packet.length});
const queued=bounded.send(packet);const queuedRejected=assert.rejects(queued,/closed/);
await assert.rejects(bounded.send(packet),/bound/);bounded.close(new Error('closed'));await queuedRejected;
assert.equal(bounded.bytes,0);

let gate;const fence=new InputBatcher(()=>new Promise(resolve=>gate=resolve),{delay:0});
const earlier=fence.send(packet);await new Promise(resolve=>setTimeout(resolve,10));
let drained=false;const drain=fence.drain().then(()=>drained=true);
const later=fence.send(packet);gate();await earlier;await drain;
assert.equal(drained,true,'service fence must not wait for future audio');
await new Promise(resolve=>setTimeout(resolve,10));gate();await later;
console.log('Preview batching, admission credits, frame boundaries, bounds and service fence passed');

// Drive the parent host with an asynchronous frontend/HTTP double. This covers
// pagehide during startup and the adapter's abort on native acknowledgment.
const deferred=()=>{let resolve;const promise=new Promise(r=>resolve=r);return {promise,resolve};};
const response=value=>({ok:true,text:async()=>'',json:async()=>value,arrayBuffer:async()=>value});
function harness() {
    const elements=new Map(),events={},calls=[],beacons=[];
    const document={querySelector:id=>{if(!elements.has(id))elements.set(id,{});return elements.get(id);}};
    const start=deferred(),output=deferred(),service=deferred(),ready=deferred(),seenStart=deferred(),seenService=deferred();
    const environment={location:{hash:'#token'},navigator:{sendBeacon:(path,body)=>beacons.push(JSON.parse(body)),clipboard:{writeText:async()=>{}}},
        addEventListener:(event,handler)=>events[event]=handler,
        fetch:async(path,options)=>{
            calls.push(path);
            if(path==='/start'){seenStart.resolve();return start.promise;}
            if(path==='/output')return output.promise;
            if(path==='/service'){seenService.resolve();return service.promise;}
            return response(null);
        }};
    let options,controller;
    const frontend=value=>{options=value;return {ready:ready.promise,dispose(){},feed(){controller?.abort();}};};
    return {document,environment,frontend,start,output,service,ready,seenStart,seenService,events,calls,beacons,
        get options(){return options;},set controller(value){controller=value;}};
}
{
    const h=harness();const running=boot(h.document,h.environment,h.frontend);
    h.ready.resolve();await h.seenStart.promise;h.events.pagehide();
    h.start.resolve(response({session:'late-session'}));await running;
    assert.deepEqual(h.beacons,[{token:'token',session:'late-session'}]);
    assert.equal(h.calls.includes('/output'),false,'closed startup must not poll or heartbeat');
}
{
    const h=harness();const running=boot(h.document,h.environment,h.frontend);
    h.ready.resolve();h.start.resolve(response({session:'active-session'}));
    await h.seenStart.promise;
    // The deferred request lets the worker acknowledge before HTTP completion.
    const controller=new AbortController();h.controller=controller;
    const event={generation:'1',sequence:'2',target:'3'};
    const completion=h.options.handleService({request:{kind:'clipboard',value:'test'},event},controller.signal);
    await h.seenService.promise;
    const payload=new TextEncoder().encode(JSON.stringify({generation:'1',ack:'2'}));
    const snapshot=new Uint8Array(12+payload.length);snapshot.set(packet.subarray(0,12));snapshot.set(payload,12);
    const header=new DataView(snapshot.buffer);header.setUint32(4,101,true);header.setUint32(8,payload.length,true);
    h.output.resolve(response(snapshot.buffer));
    await new Promise(resolve=>controller.signal.addEventListener('abort',resolve,{once:true}));
    h.events.pagehide();h.service.resolve(response(null));
    assert.deepEqual(await completion,{handled:true});await running;
    assert.equal(h.calls.includes('/cancel'),false,'successful native acknowledgment must not cancel export');
}
console.log('Preview startup close and acknowledged service withdrawal passed');
