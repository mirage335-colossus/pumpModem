// Local PCM only. The constructor receives no URL, port or host authority.
class DataPumpAudio extends AudioWorkletProcessor {
    constructor(options) {
        super();this.generation=String(options.processorOptions.generation);
        this.capture=false;this.outstanding=0;this.play=null;this.failed=false;this.retiredPlayback=null;
        this.captureBlock=new Float32Array(Math.min(4096,Math.max(1,Math.ceil(sampleRate/100))));this.captureCount=0;
        this.port.onmessage=event=>this.message(event.data);
    }
    notify(kind,detail={},transfer=[]) {this.port.postMessage({kind,generation:this.generation,...detail},transfer);}
    fail(reason) {if(this.failed)return;this.failed=true;this.capture=false;this.play=null;this.notify('interrupted',{reason});}
    flushCapture() {
        if(!this.captureCount)return;
        const samples=this.captureBlock.slice(0,this.captureCount);
        this.outstanding+=samples.length;
        this.notify('capture',{stream:this.captureStream,position:this.capturePosition,samples},[samples.buffer]);
        this.captureCount=0;
    }
    failPlayback(reason,stream=this.play?.stream) {
        if(!stream)return;
        this.retiredPlayback=String(stream);this.play=null;
        this.notify('playback_failed',{stream:String(stream),reason});
    }
    message(event) {
        if(!event||String(event.generation)!==this.generation)return;
        if(event.kind==='capture_ack') {if(!Number.isInteger(event.frames)||event.frames<0||event.frames>this.outstanding){this.fail('Invalid capture credit');return;}this.outstanding-=event.frames;return;}
        if(event.kind==='capture_start'){if(!this.failed){this.capture=true;this.captureStream=String(event.stream);this.captureCount=0;this.captureNext=null;this.captureWait=currentFrame;}return;}
        if(event.kind==='capture_stop'){if(String(event.stream)===this.captureStream){this.flushCapture();this.capture=false;}return;}
        if(event.kind==='playback_abort'){if(this.play&&String(event.stream)!==this.play.stream)return;this.retiredPlayback=String(event.stream);this.play=null;return;}
        if(event.kind==='playback_cancel') {if(this.play&&String(event.stream)!==this.play.stream)return;this.retiredPlayback=String(event.stream);this.play=null;this.notify('playback_cancelled',{stream:String(event.stream)});return;}
        if(this.failed)return;
        if(event.kind==='playback_start') {
            if(this.play){this.failPlayback('Concurrent playback is unavailable',event.stream);return;}
            if(!Number.isFinite(event.gain)||event.gain<0||event.gain>16||![0,1,2].includes(event.channels)){this.failPlayback('Invalid audio output format',event.stream);return;}
            this.play={stream:String(event.stream),startFrame:null,gain:event.gain,channels:event.channels,queue:[],offset:0,queued:0,admitted:0,consumed:0,reported:0,started:false,end:false};
            this.notify('playback_ready',{stream:this.play.stream});return;
        }
        if(String(event.stream)===this.retiredPlayback)return;
        const play=this.play;
        if(!play||String(event.stream)!==play.stream){this.failPlayback('Unknown playback stream',event.stream);return;}
        if(event.kind==='playback_pcm') {
            const pcm=event.samples;
            if(!(pcm instanceof Float32Array)||!pcm.length||pcm.length>4096||event.position!==play.admitted||play.end||play.queued+pcm.length>192000||pcm.some(x=>!Number.isFinite(x)||Math.abs(x)>8)) {
                this.failPlayback('Invalid, reordered or excessive playback PCM');return;
            }
            if(play.admitted===0) {
                if(!Number.isSafeInteger(event.startFrame)||event.startFrame<currentFrame){this.failPlayback('Invalid scheduled output frame');return;}
                play.startFrame=event.startFrame;
            }
            play.queue.push(pcm);play.queued+=pcm.length;play.admitted+=pcm.length;
        } else if(event.kind==='playback_end') {
            if(play.end||event.position!==play.admitted){this.failPlayback('Invalid playback endpoint');return;}play.end=true;
            if(play.admitted===0){this.notify('playback_endpoint',{stream:play.stream,position:0,frame:currentFrame,empty:true});this.play=null;}
        } else this.failPlayback('Unknown audio primitive');
    }
    process(inputs,outputs) {
        const output=outputs[0]||[];
        for(const channel of output)channel.fill(0);
        const frames=output[0]?.length||inputs[0]?.[0]?.length||0;
        if(this.capture&&!this.failed) {
            const input=inputs[0]?.[0];
            if(!input?.length) {
                // A connected MediaStream may not have produced its first
                // quantum yet. Never replace missing device time with silence.
                if(this.captureNext!==null||currentFrame-this.captureWait>=2*sampleRate) {
                    this.fail('Audio input stopped providing samples');return true;
                }
            } else {
                if(this.captureNext!==null&&currentFrame!==this.captureNext){this.fail('Audio input sample timeline skipped');return true;}
                if(this.outstanding+this.captureCount+input.length>192000){this.fail('Capture consumer exceeded its bounded queue');return true;}
                this.captureNext=currentFrame+input.length;
                for(let start=0;start<input.length;) {
                    if(!this.captureCount)this.capturePosition=currentFrame+start;
                    const count=Math.min(input.length-start,this.captureBlock.length-this.captureCount);
                    this.captureBlock.set(input.subarray(start,start+count),this.captureCount);
                    this.captureCount+=count;start+=count;
                    if(this.captureCount===this.captureBlock.length)this.flushCapture();
                }
            }
        }
        const play=this.play;
        if(!play||this.failed||!frames||play.startFrame===null)return true;
        let written=0;
        if(!play.started) {
            if(currentFrame+frames<=play.startFrame)return true;
            if(currentFrame>play.startFrame){this.failPlayback('Scheduled playback start was missed');return true;}
            written=Math.max(0,play.startFrame-currentFrame);
            if(play.queued<frames-written&&!play.end){this.failPlayback('Scheduled playback has insufficient buffered PCM');return true;}
            play.started=true;this.notify('playback_started',{stream:play.stream,frame:play.startFrame});
        }
        while(written<frames&&play.queue.length) {
            const block=play.queue[0];const count=Math.min(frames-written,block.length-play.offset);
            for(let i=0;i<count;i++) {
                const value=Math.max(-1,Math.min(1,block[play.offset+i]*play.gain));
                if(play.channels!==1&&output[0])output[0][written+i]=value;
                if(play.channels!==0&&output[1])output[1][written+i]=value;
            }
            written+=count;play.offset+=count;play.queued-=count;play.consumed+=count;
            if(play.offset===block.length){play.queue.shift();play.offset=0;}
        }
        if(play.queued===0&&play.end) {
            this.notify('playback_endpoint',{stream:play.stream,position:play.consumed,frame:currentFrame+written});this.play=null;
        } else if(written<frames) {
            this.failPlayback('Playback underrun; transmission did not complete');
        } else if(play.consumed-play.reported>=1024) {
            play.reported=play.consumed;this.notify('playback_progress',{stream:play.stream,position:play.consumed});
        }
        return true;
    }
}
registerProcessor('datapump-audio',DataPumpAudio);
