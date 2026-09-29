// Real C++ live audio regression, without a server, sockets or browser launch.
// Only the physical audio device is simulated. BrowserAudio, its AudioWorklet,
// the protocol and the Wasm Worker script are the production implementations.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {spawn} from 'node:child_process';
import {Worker, isMainThread, parentPort, workerData} from 'node:worker_threads';
import {webcrypto} from 'node:crypto';
import {performance} from 'node:perf_hooks';
import {BrowserAudio} from '../web/browser_audio.mjs';
import {FrameDecoder, encodeAudio, encodeEvent} from '../web/protocol.mjs';

const sourceRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const readAsset=name=>fs.readFileSync(path.join(sourceRoot,'web',name),'utf8');
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
const denied=()=>{throw new Error('Live regression forbids network and remote asset loading');};

async function wasmThread() {
    const blobs=new Map();let nextBlob=0;
    // Coarsen only the runtime clock, never the physical audio clock or the
    // fixture's deadlines. Browsers need not expose Node's clock precision.
    const resolution=workerData.clockResolutionMs;
    const runtimePerformance=resolution?{timeOrigin:performance.timeOrigin,
        now:()=>Math.floor(performance.now()/resolution)*resolution}:performance;
    const sandbox={console,WebAssembly,ArrayBuffer,Uint8Array,Int8Array,Uint16Array,Int16Array,
        Uint32Array,Int32Array,Float32Array,Float64Array,BigInt64Array,BigUint64Array,TextEncoder,TextDecoder,
        performance:runtimePerformance,crypto:webcrypto,setTimeout,clearTimeout,setInterval,clearInterval,
        WorkerGlobalScope:function(){},location:{href:'blob:preloaded-live-test'},SharedArrayBuffer:undefined,
        Blob:class {constructor(parts){this.source=parts.join('');}},
        URL:{createObjectURL(blob){const id=`blob:fixture-${++nextBlob}`;blobs.set(id,blob.source);return id;},revokeObjectURL(id){blobs.delete(id);}},
        fetch:denied,XMLHttpRequest:denied,WebSocket:denied,WebTransport:denied,EventSource:denied,RTCPeerConnection:denied,
        postMessage:(message,transfer=[])=>parentPort.postMessage(message,transfer),close:()=>parentPort.close()};
    sandbox.self=sandbox;
    const realm=vm.createContext(sandbox);
    sandbox.importScripts=url=>{
        assert.equal(blobs.get(url),workerData.factorySource,'only the preloaded factory may be imported');
        vm.runInContext(blobs.get(url),realm,{filename:workerData.factoryPath});
        const create=sandbox.createDataPump;
        sandbox.createDataPump=async options=>{
            const module=await create(options),call=module.ccall;
            module.ccall=function(name,...args) {
                const start=performance.now();
                return Promise.resolve(call.call(module,name,...args)).finally(()=>
                    parentPort.postMessage({type:'fixture_timing',name,milliseconds:performance.now()-start}));
            };
            return module;
        };
    };
    vm.runInContext(workerData.workerSource,realm,{filename:'web/wasm_worker.js'});
    parentPort.on('message',message=>sandbox.onmessage({data:message}));
    parentPort.postMessage({type:'fixture_loaded'});
}

// Node's default child stdio can be socketpairs. A small Python launcher creates
// actual anonymous pipes and passes their descriptors directly to this harness
// and the native executable. The launcher itself inherits ordinary stdio.
const nativeLauncher=String.raw`
import os, subprocess, sys, tempfile
binary,node,script,rate,temp_root,idle_seconds=sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='datapump-web-live-',dir=temp_root or None) as workspace:
    ir,iw=os.pipe();orr,ow=os.pipe();er,ew=os.pipe()
    environment=dict(os.environ,TMPDIR=workspace)
    app=subprocess.Popen([binary],stdin=ir,stdout=ow,stderr=ew,env=environment)
    os.close(ir);os.close(ow);os.close(ew)
    fixture=None
    try:
        fixture=subprocess.Popen([node,script,'--native-fds',str(iw),str(orr),str(er),'--rate',rate,'--idle-seconds',idle_seconds],pass_fds=(iw,orr,er),env=environment)
        os.close(iw);os.close(orr);os.close(er)
        result=fixture.wait()
    finally:
        if fixture is not None and fixture.poll() is None: fixture.kill();fixture.wait()
        try: app.wait(timeout=5)
        except subprocess.TimeoutExpired: app.kill();app.wait()
    sys.exit(result)
`;

function nativeTransport(fds,onBytes,onFailure,metrics) {
    const output=fs.createReadStream(null,{fd:fds[1],autoClose:true});
    const input=fs.createWriteStream(null,{fd:fds[0],autoClose:true});
    const errors=fs.createReadStream(null,{fd:fds[2],autoClose:true});
    let closing=false,diagnostic='';
    errors.on('data',data=>{diagnostic=(diagnostic+data.toString()).slice(-8192);});
    for(const stream of [input,output,errors])stream.on('error',error=>{if(!closing)onFailure(error);});
    // Model a modest peripheral relay with bounded pipe backpressure. This
    // adds no networking: serialized bytes traverse the same anonymous pipe.
    metrics.relayMbps=10;
    output.on('data',async data=>{
        output.pause();
        try {
            for(let at=0;at<data.length&&!closing;at+=4096) {
                const chunk=data.subarray(at,at+4096);
                await pause(Math.ceil(chunk.length*8/(metrics.relayMbps*1000)));
                if(!closing)onBytes(chunk);
            }
        }catch(error){onFailure(error);}
        finally {if(!closing)output.resume();}
    });
    output.on('end',()=>{if(!closing)onFailure(new Error(`Native worker ended: ${diagnostic}`));});
    return {
        ready:Promise.resolve(),
        send(bytes){const start=performance.now();return new Promise((resolve,reject)=>input.write(bytes,error=>{
            metrics.maxFeedMs=Math.max(metrics.maxFeedMs,performance.now()-start);error?reject(error):resolve();
        }));},
        async close(){closing=true;input.end();output.destroy();errors.destroy();},
        diagnostic:()=>diagnostic
    };
}

function wasmTransport(factoryPath,wasmPath,onBytes,onFailure,metrics,clockResolutionMs) {
    const factorySource=fs.readFileSync(factoryPath,'utf8'),wasmBytes=new Uint8Array(fs.readFileSync(wasmPath));
    const thread=new Worker(new URL(import.meta.url),{workerData:{factoryPath,factorySource,workerSource:readAsset('wasm_worker.js'),clockResolutionMs}});
    let readyResolve,readyReject,next=0,pendingBytes=0,closing=false,diagnostic='';const pending=new Map();
    const ready=new Promise((resolve,reject)=>{readyResolve=resolve;readyReject=reject;});
    const fail=error=>{readyReject(error);for(const p of pending.values())p.reject(error);pending.clear();pendingBytes=0;onFailure(error);};
    thread.on('error',fail);thread.on('exit',code=>{if(!closing)fail(new Error(`Wasm worker ended (${code}): ${diagnostic}`));});
    thread.on('message',message=>{
        try {
            if(message.type==='fixture_loaded')thread.postMessage({type:'init',factorySource,wasmBytes},[wasmBytes.buffer]);
            else if(message.type==='ready')readyResolve();
            else if(message.type==='bytes'){onBytes(message.bytes);thread.postMessage({type:'output_consumed',size:message.bytes.length});}
            else if(message.type==='accepted') {
                const item=pending.get(message.id);assert(item,'unknown Worker input acknowledgment');pending.delete(message.id);pendingBytes-=item.size;
                metrics.maxFeedMs=Math.max(metrics.maxFeedMs,performance.now()-item.start);item.resolve();
            } else if(message.type==='fixture_timing') {
                metrics.maxExportMs=Math.max(metrics.maxExportMs,message.milliseconds);
                if(message.name==='datapump_web_tick'){metrics.tickCount++;metrics.tickMilliseconds+=message.milliseconds;}
            } else if(message.type==='diagnostic')diagnostic=(diagnostic+'\n'+message.message).slice(-8192);
            else if(message.type==='error')throw new Error(message.message);
        }catch(error){fail(error);}
    });
    return {
        ready,
        send(bytes){
            // Match the actual page's admission bounds: an unbounded harness
            // would conceal a browser failure before C++ ever receives PCM.
            if(pending.size>=128||pendingBytes+bytes.byteLength>16*1024*1024)return Promise.reject(new Error('Local input queue exceeded its bound'));
            const id=++next,start=performance.now();pendingBytes+=bytes.byteLength;
            return new Promise((resolve,reject)=>{
            pending.set(id,{resolve,reject,start,size:bytes.byteLength});thread.postMessage({type:'feed',id,bytes},[bytes.buffer]);
        });},
        async close(){closing=true;for(const p of pending.values())p.reject(new Error('Fixture closed'));pending.clear();pendingBytes=0;await thread.terminate();},
        diagnostic:()=>diagnostic
    };
}

function physicalAudio(rate,metrics,fail) {
    const contexts=[],recorded=[];let Processor,noise=0x51b4ca97,replay=null,noiseGain=.002;
    const realm=vm.createContext({AudioWorkletProcessor:class {constructor(){this.port={postMessage:()=>{}};}},
        registerProcessor:(_,value)=>{Processor=value;},Float32Array,Math,Number,String,BigInt,currentFrame:0,sampleRate:rate});
    vm.runInContext(readAsset('audio_worklet.js'),realm,{filename:'web/audio_worklet.js'});
    class Context {
        constructor(){this.sampleRate=rate;this.state='running';this.currentTime=0;this.baseLatency=this.outputLatency=0;this.start=performance.now();this.frame=0;this.destination={};this.nodes=[];contexts.push(this);this.audioWorklet={addModule:async()=>{}};this.timer=setInterval(()=>this.advance(),1);}
        resume(){return Promise.resolve();}
        createMediaStreamSource(){return {connect(){},disconnect(){}};}
        getOutputTimestamp(){return {contextTime:this.currentTime,performanceTime:this.start+this.currentTime*1000};}
        async close(){clearInterval(this.timer);this.state='closed';}
        advance(){
            try {
                const end=Math.floor((performance.now()-this.start)*rate/128000)*128;
                assert(end-this.frame<rate*3,'fixture audio clock stalled for three seconds');
                while(this.frame+128<=end){
                    realm.currentFrame=this.frame;
                    const input=new Float32Array(128);
                    for(let i=0;i<input.length;i++){
                        noise^=noise<<13;noise^=noise>>>17;noise^=noise<<5;
                        const position=replay?this.frame+i-replay.start:-1;
                        // Continue microphone noise through data and absence;
                        // a perfect codeword followed by digital zero misses
                        // noisy correction and live end-of-transmission work.
                        input[i]=position>=0?.65*(replay.samples[position]||0)+.04*(noise/2147483648):noiseGain*(noise/2147483648);
                    }
                    for(const node of this.nodes)if(node.connected){
                        const left=new Float32Array(128),right=new Float32Array(128);
                        node.processor.process([[input]],[[left,right]]);
                        node.verify(this.frame,left,right);
                    }
                    this.frame+=128;this.currentTime=this.frame/rate;metrics.quanta++;
                }
            }catch(error){fail(error);clearInterval(this.timer);}
        }
    }
    class AudioNode {
        constructor(context,_name,options){
            this.context=context;this.processor=new Processor({processorOptions:options.processorOptions});this.connected=false;this.expected=null;this.capturePositions=new Map();this.burst=[];this.burstStart=null;this.burstsFinished=0;context.nodes.push(this);
            this.port={onmessage:null,postMessage:message=>{
                const copy=structuredClone(message);
                if(copy.kind==='playback_start')this.expected={stream:String(copy.stream),gain:copy.gain,channels:copy.channels,start:null,blocks:[],index:0,end:null};
                if(copy.kind==='playback_pcm'){
                    assert(this.expected&&String(copy.stream)===this.expected.stream,'unexpected output stream');
                    if(copy.position===0)this.expected.start=copy.startFrame;
                    this.expected.blocks.push({position:copy.position,samples:copy.samples});
                }
                if(copy.kind==='playback_end'&&this.expected)this.expected.end=copy.position;
                if(copy.kind==='playback_cancel'||copy.kind==='playback_abort')this.expected=null;
                this.processor.message(copy);
            }};
            this.processor.port.postMessage=message=>{
                const copy=structuredClone(message);
                if(copy.kind==='capture'){
                    assert(copy.samples.length>0&&copy.samples.length<=Math.ceil(rate/100),'invalid capture packet size');
                    const stream=String(copy.stream),position=this.capturePositions.get(stream);
                    if(position!==undefined)assert.equal(copy.position,position,'capture packet timeline has a gap');
                    this.capturePositions.set(stream,copy.position+copy.samples.length);
                    metrics.capturePackets++;metrics.captureFrames+=copy.samples.length;
                }
                if(copy.kind==='playback_progress')metrics.progressMessages++;
                if(copy.kind==='playback_endpoint')metrics.endpoints++;
                if(copy.kind==='interrupted'||copy.kind==='playback_failed')fail(new Error(`AudioWorklet: ${copy.reason}`));
                // A busy browser can delay main-thread delivery while the
                // AudioWorklet continues sampling. Retain bounded 1.3s
                // bursts during training and absence, then deliver every original
                // packet in order; neither PCM nor sample time changes.
                if(copy.kind==='capture'&&replay&&this.burstsFinished<2&&copy.position>=replay.start+rate*(.5+4*this.burstsFinished)){
                    if(this.burstStart===null)this.burstStart=copy.position;
                    this.burst.push(copy);assert(this.burst.length<=132,'capture burst exceeded its fixture bound');
                    if(copy.position-this.burstStart<rate*1.3)return;
                    this.burstsFinished++;metrics.captureBursts=this.burstsFinished;metrics.captureBurstPackets=(metrics.captureBurstPackets||0)+this.burst.length;this.burstStart=null;
                    for(const held of this.burst)setImmediate(()=>this.port.onmessage?.({data:held}));
                    this.burst=[];return;
                }
                setImmediate(()=>this.port.onmessage?.({data:copy}));
            };
        }
        connect(){this.connected=true;}
        disconnect(){this.connected=false;}
        verify(frame,left,right){
            const e=this.expected,played=[];
            for(let i=0;i<128;i++){
                const position=e?.start===null?-1:frame+i-(e?.start??Infinity);let expected=0;
                if(e&&position>=0&&(e.end===null||position<e.end)){
                    while(e.index<e.blocks.length&&position>=e.blocks[e.index].position+e.blocks[e.index].samples.length)e.index++;
                    const block=e.blocks[e.index];
                    assert(block&&position>=block.position,`physical playback lacks sample ${position}`);
                    expected=Math.fround(Math.max(-1,Math.min(1,block.samples[position-block.position]*e.gain)));
                    metrics.playedFrames++;if(Math.abs(expected)>1e-6)metrics.nonzeroFrames++;
                    played.push(e.channels===1?right[i]:left[i]);
                }
                assert.equal(left[i],e&&e.channels!==1?expected:0,'left output differs from emitted PCM/routing');
                assert.equal(right[i],e&&e.channels!==0?expected:0,'right output differs from emitted PCM/routing');
            }
            if(played.length)recorded.push(Float32Array.from(played));
        }
    }
    const track={stop(){},addEventListener(){},getSettings:()=>({sampleRate:rate})};
    const stream={getTracks:()=>[track],getAudioTracks:()=>[track]};
    return {environment:{AudioContext:Context,AudioWorkletNode:AudioNode,performance,setTimeout,clearTimeout,
        navigator:{mediaDevices:{getUserMedia:async()=>stream}},URL:{createObjectURL:()=> 'blob:preloaded-worklet',revokeObjectURL(){}},Blob:class{}},
        replay(){
            assert(!replay,'only one incoming recording is expected');
            const samples=new Float32Array(metrics.playedFrames);let at=0;
            for(const block of recorded){samples.set(block,at);at+=block.length;}
            assert.equal(at,samples.length);assert(samples.length>6*rate,'recorded transmission lacks physical absence tail');
            const context=contexts.at(-1);replay={samples,start:context.frame+Math.ceil(rate/2)};
            let lastSignal=samples.length-1;while(lastSignal>=0&&Math.abs(samples[lastSignal])<1e-6)--lastSignal;
            return {start:replay.start,end:replay.start+samples.length,absenceEnd:replay.start+lastSignal+6*rate};
        },
        noiseLevel(gain){noiseGain=gain;},
        frame:()=>contexts.at(-1)?.frame||0,
        close:()=>Promise.all(contexts.map(context=>context.close()))};
}

async function scenario(options,rate) {
    const metrics={rate,idleSeconds:options.idleSeconds,clockResolutionMs:options.clockResolutionMs,quanta:0,capturePackets:0,captureFrames:0,playedFrames:0,nonzeroFrames:0,progressMessages:0,endpoints:0,drains:0,captureStarts:0,captureStops:0,pcmFrames:0,maxFeedMs:0,maxExportMs:0,maxEditMs:0,maxInputLevelMs:0,tickCount:0,tickMilliseconds:0,receivedBytes:0,snapshotCount:0};
    let failure=null,snapshot=null,sequence=0n,audio,transport,closing=false,incoming=null;
    const histories=[];const fail=error=>{if(!closing&&!failure)failure=error instanceof Error?error:new Error(String(error));};
    const physical=physicalAudio(rate,metrics,fail);
    const decoder=new FrameDecoder(message=>{
        if(message.type==='snapshot'){
            snapshot=message.snapshot;sequence=sequence>BigInt(snapshot.ack)?sequence:BigInt(snapshot.ack);metrics.snapshotCount++;
            const statuses=snapshot.controls.filter(c=>/status|progress|receiv|transmi|audio|Fast/i.test((c.label||'')+' '+(c.text||''))).map(c=>c.text||'').filter(Boolean).join(' | ');
            if(statuses!==histories.at(-1)){histories.push(statuses);if(histories.length>12)histories.shift();}
            // Fast cancels its receive operation when a transmission is
            // admitted; that exact status is expected during RX -> TX.
            const error=snapshot.controls.find(c=>/overrun|underrun|timed out|incomplete|audio error|discontinuity|random generation failed/i.test(c.text||'')&&
                c.text!=='Cancelled; transfer incomplete'&&!/No incomplete/i.test(c.text||''));
            if(error)fail(new Error(`Application audio failure: ${error.text}`));
        }else if(message.type==='audio'){
            assert(audio,'audio event before configuration');const e=message.event;
            if(e.kind===0){
                metrics.captureStarts++;
                if(incoming)assert(physical.frame()>=incoming.absenceEnd,'receiver restarted before incoming physical absence');
            }
            if(e.kind===1)metrics.captureStops++;
            if(e.kind===3){metrics.pcmFrames+=e.samples.length;assert(metrics.pcmFrames<=rate*120,'unbounded fixture transmission');}
            audio.accept(e);
        }else if(message.type==='clock')audio.clock(message);
        else if(message.type==='error')fail(new Error(`C++ host error: ${message.error}`));
        else if(message.type==='closed'&&!closing)fail(new Error(`C++ application closed (${message.result})`));
    });
    const bytes=data=>{metrics.receivedBytes+=data.length;decoder.push(data);};
    const started=performance.now();
    try {
        transport=options.fds?nativeTransport(options.fds,bytes,fail,metrics):wasmTransport(options.factory,options.wasm,bytes,fail,metrics,options.clockResolutionMs);
        audio=new BrowserAudio(event=>{
            if(event.kind==='playback_progress'&&event.drained)metrics.drains++;
            return transport.send(encodeAudio(event));
        },{environment:physical.environment,status:value=>{if(value.state==='interrupted'||value.state==='playback-failed')fail(new Error(`BrowserAudio: ${value.reason}`));}});
        const wait=async(predicate,label,timeout=15000)=>{
            const deadline=performance.now()+timeout;
            while(!predicate()){
                if(failure)throw failure;
                assert(performance.now()<deadline,`${label} exceeded ${timeout}ms; ${histories.join('\n')}; ${transport.diagnostic()}`);
                await pause(5);
            }
            if(failure)throw failure;
        };
        let ready=false;transport.ready.then(()=>{ready=true;},fail);
        await wait(()=>ready,'runtime initialization',30000);await wait(()=>snapshot!==null,'initial presentation',30000);
        const control=label=>snapshot.controls.find(c=>c.label===label);
        const modem=snapshot.controls.find(c=>c.options?.some(o=>o.label==='Fast Modem'));
        assert(modem&&modem.options.find(o=>o.id===modem.selected)?.label==='Fast Modem','fixture must start in default Fast live mode');
        assert.equal(control('Expected SNR')?.selected,'-6','fixture must use the default short acoustic Fast preset');
        assert(!snapshot.controls.some(c=>c.label==='Simulation'&&c.options?.find(o=>o.id===c.selected)?.label==='Yes'),'simulation must remain disabled');
        await audio.enable(readAsset('audio_worklet.js'),{microphone:true});
        await wait(()=>metrics.captureStarts===1&&metrics.captureFrames>=rate/5&&audio.clockOffset!==null,'live microphone subscription and clock');
        const edit=async value=>{
            const c=control('Message');assert(c?.enabled&&!c.readOnly,'default Fast composer unavailable');
            const next=++sequence,start=performance.now();
            await transport.send(encodeEvent({version:1,generation:snapshot.generation,sequence:String(next),target:c.id,kind:1,value}));
            await wait(()=>BigInt(snapshot.ack)>=next&&control('Message')?.text===value,'simultaneous UI edit acknowledgment');
            const elapsed=performance.now()-start;metrics.maxEditMs=Math.max(metrics.maxEditMs,elapsed);
            assert(elapsed<=1500,`live UI edit acknowledgment took ${elapsed.toFixed(1)}ms (limit1500ms)`);
        };
        for(const value of ['l','li','live']){await edit(value);await pause(150);}
        const initialSamples=metrics.captureFrames;
        await wait(()=>metrics.captureFrames>=initialSamples+rate*20,'twenty seconds of uninterrupted broadband-noise RX',30000);
        assert.equal(metrics.captureStarts,1,'receiver silently restarted after capture failure');
        await edit('hi');
        await wait(()=>control('Transmit text')?.enabled,'live transmit enabled');
        const action=control('Transmit text'),next=++sequence;
        await transport.send(encodeEvent({version:1,generation:snapshot.generation,sequence:String(next),target:action.id,kind:4}));
        await wait(()=>metrics.pcmFrames>0,'actual C++ transmission PCM',30000);
        // The shared application locks the admitted message during TX, while
        // local presentation choices remain editable. Exercise an actual
        // allowed UI update without changing the physical transmission.
        assert.equal(control('Message')?.enabled,false,'transmitting message must be locked');
        const brightness=snapshot.controls.find(c=>c.options?.some(o=>o.label==='Dark')&&c.options?.some(o=>o.label==='Dim'));
        assert(brightness?.enabled,'local QR presentation must remain available during TX');
        const selected=brightness.options.find(o=>o.label==='Dim'),choiceSequence=++sequence,choiceStart=performance.now();
        await transport.send(encodeEvent({version:1,generation:snapshot.generation,sequence:String(choiceSequence),target:brightness.id,kind:2,value:selected.id}));
        await wait(()=>BigInt(snapshot.ack)>=choiceSequence&&snapshot.controls.find(c=>c.id===brightness.id)?.selected===selected.id,'UI update during physical output');
        const choiceElapsed=performance.now()-choiceStart;metrics.maxEditMs=Math.max(metrics.maxEditMs,choiceElapsed);
        assert(choiceElapsed<=1500,`UI choice during playback took ${choiceElapsed.toFixed(1)}ms (limit1500ms)`);
        assert.equal(control('Message')?.text,'hi','presentation edit changed the admitted message');
        await wait(()=>metrics.drains===1&&metrics.captureStarts>=2&&control('Message')?.enabled,'physical drain and automatic return to RX',60000);
        assert.equal(metrics.endpoints,1,'expected exactly one physical playback endpoint');
        assert.equal(metrics.playedFrames,metrics.pcmFrames,'device did not consume every emitted sample');
        assert(metrics.nonzeroFrames>128,'transmission emitted no meaningful nonzero waveform');
        assert(metrics.progressMessages>0,'output progress acknowledgments were not exercised');
        const resumed=metrics.captureFrames,streams=metrics.captureStarts;
        await edit('ready');await wait(()=>metrics.captureFrames>=resumed+rate*2,'RX after transmission',10000);
        assert.equal(metrics.captureStarts,streams,'post-transmission receiver restarted after audio failure');
        const inputDb=()=>{
            const caption=snapshot.controls.find(c=>/RMS -?\d+ dBFS/.test(c.caption||''))?.caption;
            return Number(caption?.match(/RMS (-?\d+) dBFS/)?.[1]??NaN);
        };
        const levelStep=async()=>{
            await wait(()=>inputDb()< -50,'initial consumed microphone noise level');
            const start=performance.now();physical.noiseLevel(.02);
            await wait(()=>inputDb()> -45&&inputDb()< -30,'consumed microphone level step',2000);
            const elapsed=performance.now()-start;
            (metrics.inputLevelMs??=[]).push(elapsed);
            metrics.maxInputLevelMs=Math.max(metrics.maxInputLevelMs,elapsed);
            physical.noiseLevel(.002);
            await wait(()=>inputDb()< -50,'consumed microphone level recovery',2000);
        };
        await levelStep();
        const idleStart=metrics.captureFrames,idleClock=performance.now(),idleTicks=metrics.tickMilliseconds;
        await wait(()=>metrics.captureFrames>=idleStart+rate*options.idleSeconds,
            `${options.idleSeconds} seconds of sustained RX`,options.idleSeconds*1000+10000);
        metrics.idleElapsedMs=performance.now()-idleClock;
        metrics.idleTickMs=metrics.tickMilliseconds-idleTicks;
        assert.equal(metrics.captureStarts,streams,'sustained idle receiver restarted');
        await edit('listening');await levelStep();
        assert.equal(metrics.captureStarts,streams,'receiver restarted during the late input/UI checks');
        // Replay the actual physical output into the microphone. A tone-only
        // capture probe misses acquisition, FEC and physical-end processing.
        // Use this same capture stream: a new local TX would cancel RX and
        // discard accumulated backlog, concealing sustained throughput failure.
        const replay=incoming=physical.replay();let pendingId=null,received=null;
        await wait(()=>{
            const rows=control('Signals')?.records||[];
            for(const row of rows){
                const text=row.cells.map(cell=>cell.text).join(' ');
                if(text.startsWith('RECEIVING / PENDING')){
                    if(pendingId===null)pendingId=row.id;
                    assert.equal(row.id,pendingId,'reception row identity changed while pending');
                }
                if(text.startsWith('RECEIVED')&&text.endsWith(' · 2 bytes · hi'))received=row;
            }
            return received;
        },'complete incoming live message after physical absence',30000);
        assert(pendingId!==null,'incoming message was never exposed as pending');
        assert.equal(received.id,pendingId,'completed reception replaced the pending row');
        assert(physical.frame()>=replay.absenceEnd,'received content escaped the six-second physical absence boundary');
        assert(metrics.captureStarts<=streams+1,'receiver restarted more than once while completing incoming reception');
        assert(metrics.captureBursts===2&&metrics.captureBurstPackets>=260,'incoming reception did not exercise repeated delayed capture delivery');
        metrics.receivedMessages=1;metrics.replayedFrames=replay.end-replay.start;
        metrics.elapsedMs=performance.now()-started;
        console.log(JSON.stringify({passed:true,mode:options.fds?'native':'wasm',...metrics}));
    }catch(error){throw new Error(`${error.message}\nLive metrics: ${JSON.stringify(metrics)}\nRecent status: ${histories.join('\n')}\n${transport?.diagnostic()||''}`,{cause:error});}
    finally {closing=true;await audio?.stop().catch(()=>{});await physical.close();await transport?.close();}
}

async function main() {
    const args=process.argv.slice(2),options={idleSeconds:20,clockResolutionMs:0};let rates=[48000,44100];
    for(let i=0;i<args.length;i++){
        if(args[i]==='--native')options.native=path.resolve(args[++i]);
        else if(args[i]==='--wasm'){options.factory=path.resolve(args[++i]);options.wasm=path.resolve(args[++i]);}
        else if(args[i]==='--native-fds')options.fds=[Number(args[++i]),Number(args[++i]),Number(args[++i])];
        else if(args[i]==='--rate'){const rate=Number(args[++i]);assert([48000,44100].includes(rate));rates=[rate];}
        else if(args[i]==='--temp-root')options.tempRoot=path.resolve(args[++i]);
        else if(args[i]==='--idle-seconds'){options.idleSeconds=Number(args[++i]);assert(Number.isInteger(options.idleSeconds)&&options.idleSeconds>=5&&options.idleSeconds<=300);}
        else if(args[i]==='--clock-resolution-ms'){options.clockResolutionMs=Number(args[++i]);assert([0,1,2].includes(options.clockResolutionMs));}
        else throw new Error(`Unknown option: ${args[i]}`);
    }
    assert.equal(Number(Boolean(options.native))+Number(Boolean(options.factory))+Number(Boolean(options.fds)),1,
        'usage: node tests/test_web_live.mjs --native WORKER | --wasm FACTORY WASM [--rate 48000|44100] [--temp-root DIR] [--idle-seconds 5..300] [--clock-resolution-ms 0|1|2]');
    assert(options.factory||options.clockResolutionMs===0,'clock coarsening requires Wasm');
    for(const rate of rates){
        if(options.native){
            const child=spawn(process.env.PYTHON||'python3',['-B','-c',nativeLauncher,options.native,process.execPath,fileURLToPath(import.meta.url),String(rate),options.tempRoot||process.env.TMPDIR||'',String(options.idleSeconds)],{stdio:'inherit'});
            const code=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',resolve);});
            assert.equal(code,0,`native live ${rate}Hz fixture failed`);
        }else await scenario(options,rate);
    }
}

if(!isMainThread)await wasmThread();
else await main();
