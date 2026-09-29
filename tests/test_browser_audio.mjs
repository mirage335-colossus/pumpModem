import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {BrowserAudio} from '../web/browser_audio.mjs';

const source=fs.readFileSync(new URL('../web/audio_worklet.js',import.meta.url),'utf8');
function worklet(rate=48000) {
    let Processor;const messages=[];
    const realm={AudioWorkletProcessor:class {constructor(){this.port={postMessage:(message,transfer=[])=>messages.push(structuredClone(message,{transfer}))};}},
        registerProcessor:(_,value)=>Processor=value,Float32Array,Math,Number,String,currentFrame:0,sampleRate:rate};
    vm.createContext(realm);vm.runInContext(source,realm);
    const processor=new Processor({processorOptions:{generation:'1'}});
    const send=event=>processor.message({generation:'1',...event});
    const render=(input=new Float32Array(128))=>{const output=[new Float32Array(128),new Float32Array(128)];processor.process(input?[[input]]:[],[output]);realm.currentFrame+=128;return output;};
    return {processor,messages,realm,send,render};
}
for(const rate of [44100,48000,192000]) {
    const w=worklet(rate);w.send({kind:'capture_start',stream:'7'});
    let received=0,packets=0;const count=Math.ceil(rate/128),credits=[];
    for(let quantum=0;quantum<count;++quantum) {
        // Delayed acknowledgments exercise real transfer detachment and credits.
        for(const message of credits.splice(0))w.send({kind:'capture_ack',frames:message.samples.length});
        const input=Float32Array.from({length:128},(_,i)=>(quantum*128+i)%1000/1000);
        w.render(input);
        for(const message of w.messages.splice(0)) {
            assert.equal(message.kind,'capture');assert.equal(message.position,received);
            assert(message.samples.length<=Math.ceil(rate/100));
            for(let i=0;i<message.samples.length;++i)assert.equal(message.samples[i],Math.fround((received+i)%1000/1000));
            received+=message.samples.length;packets++;credits.push(message);
        }
    }
    w.send({kind:'capture_stop',stream:'7'});
    for(const message of w.messages){assert.equal(message.kind,'capture');assert.equal(message.position,received);received+=message.samples.length;packets++;}
    assert.equal(received,count*128);assert(packets<=102,'capture did not batch render quanta');assert.equal(w.processor.failed,false);
}
// Startup may have no active input. An established input cannot lose a quantum.
const startup=worklet();startup.send({kind:'capture_start',stream:'1'});
for(let i=0;i<4;++i)startup.render(null);
assert.equal(startup.processor.failed,false);assert.equal(startup.messages.length,0);
startup.render();startup.render(null);
assert.equal(startup.processor.failed,true);assert.equal(startup.messages.at(-1).kind,'interrupted');
const absent=worklet();absent.send({kind:'capture_start',stream:'1'});
for(let i=0;i<752;++i)absent.render(null);
assert.equal(absent.messages.at(-1).kind,'interrupted');assert(!absent.messages.some(e=>e.kind==='capture'));
const gap=worklet();gap.send({kind:'capture_start',stream:'1'});gap.render();gap.realm.currentFrame+=128;gap.render();
assert.equal(gap.messages.at(-1).kind,'interrupted');

// Batching retains the original bounded capture-credit ceiling.
const blocked=worklet();blocked.send({kind:'capture_start',stream:'1'});
for(let i=0;i<1501;++i)blocked.render();
assert.equal(blocked.messages.at(-1).kind,'interrupted');
assert.equal(blocked.messages.filter(e=>e.kind==='capture').reduce((n,e)=>n+e.samples.length,0),192000);

// A failed output stream cannot stop a valid microphone or claim completion.
const duplex=worklet();duplex.send({kind:'capture_start',stream:'1'});
duplex.send({kind:'playback_start',stream:'2',channels:0,gain:1});
duplex.send({kind:'playback_pcm',stream:'2',position:0,startFrame:0,samples:new Float32Array([.5])});duplex.render();
assert.equal(duplex.messages.at(-1).kind,'playback_failed');assert.equal(duplex.processor.failed,false);
for(let i=0;i<4;++i)duplex.render();
assert(duplex.messages.some(e=>e.kind==='capture'));assert(!duplex.messages.some(e=>e.kind==='playback_endpoint'));
duplex.send({kind:'playback_pcm',stream:'2',position:1,samples:new Float32Array(128)});
duplex.send({kind:'playback_cancel',stream:'2'});assert.equal(duplex.messages.at(-1).kind,'playback_cancelled');
duplex.send({kind:'playback_start',stream:'3',channels:2,gain:1});
duplex.send({kind:'playback_pcm',stream:'3',position:0,startFrame:duplex.realm.currentFrame,samples:new Float32Array(128).fill(.25)});
duplex.send({kind:'playback_end',stream:'3',position:128});
assert.equal(duplex.render()[0][0],.25);assert.equal(duplex.messages.at(-1).kind,'playback_endpoint');

const settle=async()=>{for(let i=0;i<5;++i)await Promise.resolve();};
function browser() {
    let now=0,next=0,trackStops=0;const timers=new Map(),sent=[],posted=[],states=[];
    const environment={performance:{timeOrigin:1000000,now:()=>now},setTimeout:(fn,delay)=>{const id=++next;timers.set(id,{fn,delay});return id;},clearTimeout:id=>timers.delete(id)};
    const audio=new BrowserAudio(async event=>sent.push(event),{environment,status:value=>states.push(value)});
    audio.generation=1n;audio.context={state:'running',sampleRate:48000,currentTime:10,getOutputTimestamp:()=>({contextTime:10,performanceTime:now}),close:async()=>{}};
    audio.node={port:{postMessage:message=>posted.push(message)},disconnect(){}};
    audio.stream={getTracks:()=>[{stop(){trackStops++;}}]};
    audio.sessions.set('1',{context:audio.context,configured:true,closed:null,idle:false,resolve:null});
    return {audio,sent,posted,states,timers,setNow:value=>now=value,trackStops:()=>trackStops};
}
const b=browser();b.audio.accept({kind:2,generation:'1',stream:'2',rate:48000,channels:0,gain:1});await settle();
assert.equal(b.posted.length,0,'unqualified output opened before clock handshake');
let ping=b.sent.find(e=>e.kind==='clock_ping');b.setNow(200);
b.audio.clock({nonce:ping.nonce,clientEpoch:ping.clientEpoch,serverReceiveEpoch:1000.001,serverSendEpoch:1000.001});
assert.equal(b.audio.failed,false);assert.equal(b.trackStops(),0);assert(b.audio.pendingStart);
b.setNow(250);b.audio.ping();await settle();ping=b.sent.at(-1);b.setNow(270);
b.audio.clock({nonce:ping.nonce,clientEpoch:ping.clientEpoch,serverReceiveEpoch:1000.260,serverSendEpoch:1000.260});
assert.equal(b.posted.at(-1).kind,'playback_start');assert.equal(b.audio.pendingStart,null);
const qualified=b.audio.clockOffset;b.audio.ping();await settle();ping=b.sent.at(-1);b.setNow(470);
b.audio.clock({nonce:ping.nonce,clientEpoch:ping.clientEpoch,serverReceiveEpoch:1000.280,serverSendEpoch:1000.280});
assert.equal(b.audio.clockOffset,qualified);assert.equal(b.audio.failed,false);assert.equal(b.trackStops(),0);

await b.audio.message({kind:'playback_failed',generation:'1',stream:'2',reason:'Playback underrun'});await settle();
assert.equal(b.sent.at(-1).kind,'playback_failed');assert.equal(b.audio.failed,false);assert.equal(b.trackStops(),0);
await b.audio.message({kind:'capture',generation:'1',stream:'1',position:0,samples:new Float32Array(480)});
assert.equal(b.sent.at(-1).kind,'capture');assert.equal(b.posted.at(-1).kind,'capture_ack');assert.equal(b.posted.at(-1).frames,480);
b.audio.accept({kind:5,generation:'1',stream:'2'});
const cancelledMessages=b.sent.length;
await b.audio.message({kind:'playback_endpoint',generation:'1',stream:'2',position:128,frame:128,empty:true});await settle();
assert.equal(b.audio.pendingDrain,null);assert.equal(b.sent.length,cancelledMessages,'retired output reported a late drain');
b.audio.accept({kind:2,generation:'1',stream:'3',rate:48000,channels:0,gain:1});
assert.equal(b.posted.at(-1).kind,'playback_start');
b.audio.accept({kind:3,generation:'1',stream:'3',position:0,rate:48000,presentationEpoch:999,samples:new Float32Array(128)});await settle();
assert.equal(b.sent.at(-1).kind,'playback_failed');assert.equal(b.audio.failed,false);assert.equal(b.trackStops(),0);
await b.audio.message({kind:'playback_endpoint',generation:'1',stream:'3',position:128,frame:128,empty:true});await settle();
assert.equal(b.audio.pendingDrain,null,'failed output restored a pending drain');
assert(!b.sent.some(e=>e.kind==='interrupted'||(e.kind==='playback_progress'&&e.drained)));
const timeout=browser();timeout.audio.accept({kind:2,generation:'1',stream:'9',rate:48000,channels:0,gain:1});
[...timeout.timers.values()].find(t=>t.delay===2000).fn();await settle();
assert.equal(timeout.sent.at(-1).kind,'playback_failed');assert.equal(timeout.trackStops(),0);assert.equal(timeout.audio.failed,false);
const unicode=browser();unicode.audio.playbackFailure('10','😀'.repeat(300));await settle();
assert(new TextEncoder().encode(unicode.sent.at(-1).reason).length<=512);
console.log('Browser audio: bounded continuous PCM, startup/established gaps, qualified start, noisy clock and playback failure recovery passed');
