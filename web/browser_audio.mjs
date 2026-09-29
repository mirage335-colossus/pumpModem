// The embedding host provides a bounded local callback. It must resolve only
// after taking ownership of the PCM block, and must not retain an unbounded queue.
// This module never creates network connections or loads a remote worklet.
export class BrowserAudio {
    constructor(send, {status=()=>{},environment=globalThis}={}) {
        if(typeof send!=='function')throw new TypeError('A local audio callback is required');
        this.send=send;this.status=status;this.env=environment;this.generation=0n;
        this.context=null;this.node=null;this.stream=null;this.failed=false;this.pendingDrain=null;this.clockOffset=null;this.clockPending=new Map();this.nextNonce=0n;this.sessions=new Map();
    }
    async enable(workletSource,{microphone=true}={}) {
        if(typeof workletSource!=='string'||workletSource.length>256*1024)throw new TypeError('Preloaded bounded worklet source is required');
        const stopped=this.stop();this.failed=false;const generation=String(++this.generation);
        // Observe old-close failures immediately even if permission stays open.
        stopped.catch(()=>{});
        let context,stream,url;
        try {
            const Context=this.env.AudioContext||this.env.webkitAudioContext;
            if(!Context)throw new Error('Web Audio is unavailable in this browser');
            // Invoke both APIs in the activation call, before awaiting either.
            context=new Context({latencyHint:'interactive'});this.context=context;
            this.sessions.set(generation,{context,configured:false,closed:null,idle:false,resolve:null});
            // Retain only a small history for already flushed late messages.
            while(this.sessions.size>8)this.sessions.delete(this.sessions.keys().next().value);
            const resumed=Promise.resolve(context.resume());resumed.catch(()=>{});
            const acquired=microphone?this.env.navigator.mediaDevices?.getUserMedia({audio:{channelCount:1,echoCancellation:false,noiseSuppression:false,autoGainControl:false},video:false}):Promise.resolve(null);
            if(!acquired)throw new Error('Microphone access requires a secure browser context');
            const permission=Promise.resolve(acquired).then(value=>{
                stream=value;
                // Permission may resolve after this attempt failed or was replaced.
                if(this.context!==context){stream?.getTracks().forEach(track=>track.stop());stream=null;}
            });
            await Promise.all([permission,resumed,stopped]);
            if(this.context!==context)return;
            this.stream=stream;
            url=this.env.URL.createObjectURL(new this.env.Blob([workletSource],{type:'text/javascript'}));
            await context.audioWorklet.addModule(url);
            if(this.context!==context)return;
            this.node=new this.env.AudioWorkletNode(context,'datapump-audio',{numberOfInputs:microphone?1:0,numberOfOutputs:1,outputChannelCount:[2],processorOptions:{generation}});
            this.node.port.onmessage=event=>this.message(event.data);
            this.node.onprocessorerror=()=>{if(this.context===context)this.interrupt('Audio processor failed');};
            if(stream) {
                this.input=context.createMediaStreamSource(stream);this.input.connect(this.node);
                for(const track of stream.getAudioTracks()) {
                    track.addEventListener('ended',()=>{if(this.context===context)this.interrupt('Microphone permission or device was withdrawn');});
                    track.addEventListener('mute',()=>{if(this.context===context)this.interrupt('Microphone input was muted');});
                }
            }
            this.node.connect(context.destination);
            context.onstatechange=()=>{if(this.context===context&&context.state!=='running')this.interrupt('Browser audio was suspended or interrupted');};
            const session=this.sessions.get(generation);session.configured=true;
            try {await this.send({kind:'configure',generation,rate:context.sampleRate});}
            catch(error){session.configured=false;throw error;}
            if(this.context!==context)return;
            this.clockOffset=null;this.ping();
            this.status({state:'ready',rate:context.sampleRate,settings:stream?.getAudioTracks()[0]?.getSettings()||{},
                drainCriterion:typeof context.getOutputTimestamp==='function'?'output timestamp':'estimated output latency',foregroundRequired:true});
        } catch(error) {if(context&&this.context===context)await this.stop().catch(()=>{});throw error;}
        finally {
            if(stream&&this.stream!==stream)stream.getTracks().forEach(track=>track.stop());
            if(url)this.env.URL.revokeObjectURL(url);
        }
    }
    now() {return (this.env.performance.timeOrigin+this.env.performance.now())/1000;}
    ping() {
        if(!this.context||this.failed)return;
        const context=this.context,nonce=String(++this.nextNonce),clientEpoch=this.now();
        this.clockPending.clear();this.clockPending.set(nonce,clientEpoch);
        Promise.resolve().then(()=>this.send({kind:'clock_ping',nonce,clientEpoch})).catch(error=>{if(this.context===context)this.interrupt(error.message);});
        this.clockTimer=this.env.setTimeout(()=>this.ping(),10000);
    }
    clock(reply) {
        const t4=this.now(),t1=this.clockPending.get(reply.nonce);
        if(t1===undefined||t1!==reply.clientEpoch)return;
        this.clockPending.delete(reply.nonce);
        const elapsed=t4-t1,serverElapsed=reply.serverSendEpoch-reply.serverReceiveEpoch;
        const roundTrip=elapsed-serverElapsed;
        if(elapsed<0||roundTrip<-.001||roundTrip>.1||serverElapsed<0) {this.interrupt('Audio clock synchronization exceeded its timing bound');return;}
        const offset=((reply.serverReceiveEpoch-t1)+(reply.serverSendEpoch-t4))/2;
        if(this.clockOffset!==null&&Math.abs(offset-this.clockOffset)>.05){this.interrupt('Audio clock changed during the session');return;}
        this.clockOffset=offset;this.clockAt=t4;this.clockUncertainty=Math.max(0,roundTrip)/2;
        this.status({state:'clock-ready',uncertainty:this.clockUncertainty});
    }
    async message(event) {
        const context=this.context;
        if(!this.context||String(event.generation)!==String(this.generation)||this.failed)return;
        try {
            if(event.kind==='capture') {
                const node=this.node;await this.send(event);
                if(node===this.node)node.port.postMessage({kind:'capture_ack',generation:String(this.generation),frames:event.samples.length});
            } else if(event.kind==='playback_endpoint') {
                if(this.pendingDrain)throw new Error('Previous output drain is still pending');
                this.pendingDrain=event;this.waitDrain(event);
            } else if(event.kind==='playback_started')this.status({state:'playing',frame:event.frame,rate:this.context.sampleRate});
            else if(event.kind==='interrupted')this.interrupt(event.reason);
            else await this.send(event);
        } catch(error){if(this.context===context)this.interrupt(error?.message||'Audio consumer rejected a block');}
    }
    waitDrain(event) {
        if(!this.context||this.pendingDrain!==event||this.failed)return;
        const context=this.context,deadline=event.frame/context.sampleRate;
        const timestamp=context.getOutputTimestamp?.();
        // Neither queue acceptance nor AudioWorklet consumption is completion.
        // The fallback explicitly includes the exposed device/render latencies.
        const cursor=timestamp&&timestamp.contextTime>0?timestamp.contextTime:
            context.currentTime-(context.baseLatency||0)-(context.outputLatency||0)-256/context.sampleRate;
        if(context.state!=='running'){this.interrupt('Audio stopped before output drain');return;}
        if(event.empty||cursor>=deadline) {
            this.pendingDrain=null;
            Promise.resolve().then(()=>this.send({kind:'playback_progress',generation:event.generation,stream:event.stream,position:event.position,drained:true})).catch(error=>{if(this.context===context)this.interrupt(error.message);});
        } else this.drainTimer=this.env.setTimeout(()=>this.waitDrain(event),10);
    }
    accept(event) {
        const names=['capture_start','capture_stop','playback_start','playback_pcm','playback_end','playback_cancel','stopped'];
        const message={...event,kind:typeof event.kind==='number'?names[event.kind]:event.kind};
        const generation=String(event.generation),session=this.sessions.get(generation);
        if(message.kind==='stopped') {
            if(!session)return;
            session.idle=true;session.resolve?.();return;
        }
        if(session&&(session.closed||this.failed||generation!==String(this.generation))) {
            if(message.kind==='playback_cancel') {
                // Closing the old device is stronger than clearing its worklet
                // queue. Acknowledge only after close actually completes.
                if(!session.closed&&this.context===session.context)this.stop().catch(error=>this.status({state:'interrupted',reason:error.message}));
                Promise.resolve(session.closed).then(()=>this.send({kind:'playback_cancelled',generation,stream:String(event.stream)}))
                    .catch(error=>this.status({state:'interrupted',reason:error.message}));
            }
            return;
        }
        if(!this.node||!this.context||this.failed)throw new Error('Enable browser audio before starting the stream');
        if(generation!==String(this.generation))throw new Error('Stale browser audio generation');
        if(message.kind==='playback_cancel') {this.pendingDrain=null;this.env.clearTimeout(this.drainTimer);}
        if(message.kind==='capture_start'&&!this.stream){this.interrupt('This audio session has no microphone grant');return;}
        if(message.kind==='playback_start'&&event.rate!==this.context.sampleRate){this.interrupt('Audio sample rate changed');return;}
        // Readiness precedes C++ preparation. The first PCM packet carries the
        // scheduled epoch; never attempt to schedule the readiness handshake.
        if(message.kind==='playback_pcm'&&event.position===0) {
            if(event.rate!==this.context.sampleRate){this.interrupt('Audio sample rate changed');return;}
            if(this.clockOffset===null||this.now()-this.clockAt>30){this.interrupt('Audio clock has not been qualified');return;}
            const delay=event.presentationEpoch-this.clockOffset-this.now();
            if(!Number.isFinite(delay)||delay<this.clockUncertainty+512/event.rate||delay>10){this.interrupt('Scheduled audio start is late or outside the bounded horizon');return;}
            const output=this.context.getOutputTimestamp?.();
            // Map the requested physical output epoch through the device clock,
            // rather than treating the render-ahead currentTime as audible time.
            const clientEpoch=event.presentationEpoch-this.clockOffset;
            const contextTime=output&&output.contextTime>0&&Number.isFinite(output.performanceTime)?
                output.contextTime+clientEpoch-(this.env.performance.timeOrigin+output.performanceTime)/1000:
                this.context.currentTime+delay-(this.context.baseLatency||0)-(this.context.outputLatency||0);
            message.startFrame=Math.round(contextTime*event.rate);
            if(message.startFrame<this.context.currentTime*event.rate+256){this.interrupt('Output latency leaves insufficient scheduled start lead');return;}
        }
        const transfers=message.samples instanceof Float32Array?[message.samples.buffer]:[];
        this.node.port.postMessage(message,transfers);
    }
    interrupt(reason) {
        const generation=String(this.generation);
        if(this.failed)return;this.failed=true;this.pendingDrain=null;this.env.clearTimeout(this.drainTimer);this.env.clearTimeout(this.clockTimer);
        this.node?.disconnect();this.input?.disconnect();this.stream?.getTracks().forEach(track=>track.stop());
        this.status({state:'interrupted',reason:String(reason)});
        Promise.resolve().then(()=>this.send({kind:'interrupted',generation,reason:String(reason).slice(0,512)})).catch(()=>{});
    }
    async stop() {
        const context=this.context,generation=String(this.generation),session=this.sessions.get(generation);
        this.context=null;this.pendingDrain=null;this.env.clearTimeout(this.drainTimer);this.env.clearTimeout(this.clockTimer);this.clockPending.clear();
        if(this.node){this.node.port.onmessage=null;this.node.disconnect();this.node=null;}
        this.input?.disconnect();this.input=null;this.stream?.getTracks().forEach(track=>track.stop());this.stream=null;
        if(!context)return this.stopping;
        context.onstatechange=null;
        const closed=context.state==='closed'?Promise.resolve():Promise.resolve().then(()=>context.close());
        if(session)session.closed=closed;
        let timer;
        const idle=session?.configured&&!session.idle?new Promise((resolve,reject)=>{
            session.resolve=resolve;
            timer=this.env.setTimeout(()=>reject(new Error('Host audio stop acknowledgment timed out')),4000);
        }):Promise.resolve();
        // Notifications cannot prevent local microphone/output cleanup.
        Promise.resolve().then(()=>this.send({kind:'interrupted',generation,reason:'Browser audio stopped'})).catch(()=>{});
        const stopping=Promise.all([this.stopping,closed,idle]);this.stopping=stopping;
        try {await stopping;}
        finally {if(timer!==undefined)this.env.clearTimeout(timer);if(session)session.resolve=null;if(this.stopping===stopping)this.stopping=null;}
    }
}
