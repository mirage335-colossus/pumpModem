// Exact version-1 pipe/local-Worker framing. No transport is created here.
const utf8=new TextEncoder(),text=new TextDecoder('utf-8',{fatal:true});
export const maxFrame=8*1024*1024;
class Writer {
    constructor(){this.parts=[];this.size=0;}
    bytes(value){this.parts.push(value);this.size+=value.byteLength;if(this.size>maxFrame)throw new RangeError('Frame exceeds limit');}
    u32(value){if(!Number.isInteger(value)||value<0||value>0xffffffff)throw new RangeError('Invalid unsigned integer');const a=new Uint8Array(4);new DataView(a.buffer).setUint32(0,value,true);this.bytes(a);}
    u64(value){value=BigInt(value);if(value<0n||value>0xffffffffffffffffn)throw new RangeError('Invalid identifier');const a=new Uint8Array(8);new DataView(a.buffer).setBigUint64(0,value,true);this.bytes(a);}
    f64(value){if(!Number.isFinite(value))throw new RangeError('Non-finite number');const a=new Uint8Array(8);new DataView(a.buffer).setFloat64(0,value,true);this.bytes(a);}
    string(value){const a=utf8.encode(value);this.u32(a.length);this.bytes(a);}
    pcm(value){if(!(value instanceof Float32Array)||!value.length||value.length>4096)throw new RangeError('PCM packet size');this.u32(value.length);const a=new Uint8Array(value.length*4),v=new DataView(a.buffer);for(let i=0;i<value.length;i++){if(!Number.isFinite(value[i])||Math.abs(value[i])>8)throw new RangeError('Invalid PCM');v.setFloat32(i*4,value[i],true);}this.bytes(a);}
    frame(type){const result=new Uint8Array(12+this.size);result.set([68,80,87,49]);const v=new DataView(result.buffer);v.setUint32(4,type,true);v.setUint32(8,this.size,true);let at=12;for(const part of this.parts){result.set(part,at);at+=part.length;}return result;}
}
class Reader {
    constructor(bytes){this.bytes=bytes;this.view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);this.at=0;}
    need(n){if(n<0||n>this.bytes.length-this.at)throw new RangeError('Truncated frame');}
    u32(){this.need(4);const n=this.view.getUint32(this.at,true);this.at+=4;return n;}
    u64(){this.need(8);const n=this.view.getBigUint64(this.at,true);this.at+=8;return String(n);}
    position(){const n=BigInt(this.u64());if(n>BigInt(Number.MAX_SAFE_INTEGER))throw new RangeError('Audio position is outside browser precision');return Number(n);}
    f64(){this.need(8);const n=this.view.getFloat64(this.at,true);this.at+=8;if(!Number.isFinite(n))throw new RangeError('Non-finite value');return n;}
    string(){const n=this.u32();this.need(n);const s=text.decode(this.bytes.subarray(this.at,this.at+n));this.at+=n;return s;}
    pcm(){const n=this.u32();if(n>4096)throw new RangeError('PCM count exceeds limit');this.need(n*4);const pcm=new Float32Array(n);for(let i=0;i<n;i++){const value=this.view.getFloat32(this.at,true);this.at+=4;if(!Number.isFinite(value)||Math.abs(value)>8)throw new RangeError('Invalid PCM');pcm[i]=value;}return pcm;}
    end(){if(this.at!==this.bytes.length)throw new RangeError('Trailing frame data');}
}
function writeEvent(w,event) {
    w.u32(event.version);w.u64(event.generation);w.u64(event.sequence);w.u64(event.target);w.u32(event.kind);
    w.u32((event.checked?1:0)|(event.cancelled?2:0)|(event.ctrl?4:0)|(event.shift?8:0)|(event.alt?16:0));w.u32((event.amount||0)>>>0);w.string(event.value||'');w.string(event.error||'');
}
export function encodeEvent(event) {const w=new Writer();writeEvent(w,event);return w.frame(2);}
export function encodeUploadBegin(event,total) {const w=new Writer();writeEvent(w,event);w.u64(total);return w.frame(10);}
export function encodeUploadChunk(target,position,bytes) {if(!(bytes instanceof Uint8Array)||bytes.length>65536)throw new RangeError('File chunk limit');const w=new Writer();w.u64(target);w.u64(position);w.u32(bytes.length);w.bytes(bytes);return w.frame(11);}
export function encodeUploadCommit(target) {const w=new Writer();w.u64(target);return w.frame(12);}
export function encodeDownload(event) {const w=new Writer();writeEvent(w,event);return w.frame(13);}
export function encodeViewport(width,height){const w=new Writer();w.u32(width);w.u32(height);return w.frame(1);}
export function encodeReconnect(){return new Writer().frame(9);}
export function encodeAudio(event) {
    const w=new Writer();
    if(event.kind==='clock_ping'){w.u64(event.nonce);w.f64(event.clientEpoch);return w.frame(15);}
    w.u64(event.generation);
    if(event.kind==='configure'){w.u32(event.rate);return w.frame(3);}
    if(event.kind==='capture'){w.u64(event.stream);w.u64(event.position);w.pcm(event.samples);return w.frame(4);}
    if(event.kind==='playback_ready'||event.kind==='playback_cancelled'){w.u64(event.stream);return w.frame(event.kind==='playback_ready'?5:8);}
    if(event.kind==='playback_progress'){w.u64(event.stream);w.u64(event.position);w.u32(event.drained?1:0);return w.frame(6);}
    if(event.kind==='interrupted'){w.string(event.reason);return w.frame(7);}
    if(event.kind==='playback_failed'){w.u64(event.stream);w.string(event.reason);return w.frame(16);}
    throw new TypeError('Unknown audio input primitive');
}
export function decodeFrame(bytes) {
    if(!(bytes instanceof Uint8Array)||bytes.length<12||bytes[0]!==68||bytes[1]!==80||bytes[2]!==87||bytes[3]!==49)throw new TypeError('Invalid protocol magic');
    const header=new DataView(bytes.buffer,bytes.byteOffset,12),type=header.getUint32(4,true),size=header.getUint32(8,true);
    if(size>maxFrame||size!==bytes.length-12)throw new RangeError('Invalid protocol length');
    const payload=bytes.subarray(12),r=new Reader(payload);let value;
    if(type===101)return {type:'snapshot',snapshot:JSON.parse(text.decode(payload))};
    if(type===102)value={type:'audio',event:{kind:r.u32(),generation:r.u64(),stream:r.u64(),position:r.position(),rate:r.u32(),channels:r.u32(),gain:r.f64(),presentationEpoch:r.f64(),samples:r.pcm()}};
    else if(type===103)value={type:'error',error:r.string()};
    else if(type===104)value={type:'closed',result:r.u32()};
    else if(type===105)value={type:'file_begin',target:r.u64(),filename:r.string(),size:r.position()};
    else if(type===106){const target=r.u64(),position=r.position(),count=r.u32();if(count>65536)throw new RangeError('File chunk limit');r.need(count);const data=r.bytes.slice(r.at,r.at+count);r.at+=count;value={type:'file_chunk',target,position,data};}
    else if(type===107)value={type:'file_end',target:r.u64(),error:r.string()};
    else if(type===108)value={type:'clock',nonce:r.u64(),clientEpoch:r.f64(),serverReceiveEpoch:r.f64(),serverSendEpoch:r.f64()};
    else throw new TypeError('Unknown output frame type');
    r.end();return value;
}
// Pipe relays may split/coalesce frames. The host must bound ingress separately;
// this incremental decoder never retains more than one maximum-sized frame.
export class FrameDecoder {
    constructor(deliver){this.deliver=deliver;this.buffer=new Uint8Array(12);this.filled=0;this.total=12;}
    push(bytes) {
        if(!(bytes instanceof Uint8Array))throw new TypeError('Byte input required');
        let at=0;
        while(at<bytes.length) {
            const count=Math.min(this.total-this.filled,bytes.length-at);
            this.buffer.set(bytes.subarray(at,at+count),this.filled);this.filled+=count;at+=count;
            if(this.filled===12&&this.total===12) {
                const view=new DataView(this.buffer.buffer),length=view.getUint32(8,true);
                if(length>maxFrame||this.buffer[0]!==68||this.buffer[1]!==80||this.buffer[2]!==87||this.buffer[3]!==49)
                    throw new RangeError('Invalid protocol envelope');
                this.total=12+length;
                if(length){const next=new Uint8Array(this.total);next.set(this.buffer);this.buffer=next;}
            }
            if(this.filled===this.total) {
                const frame=this.buffer;this.buffer=new Uint8Array(12);this.filled=0;this.total=12;this.deliver(decodeFrame(frame));
            }
        }
    }
    end(){if(this.filled)throw new RangeError('Incomplete final frame');}
}
