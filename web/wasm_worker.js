// Dedicated local Worker: assets arrive once; mutation exports are serialized
// across Asyncify suspension so timer/input callbacks cannot reenter C++ state.
let runtime=null,timer=null,initializing=false,unacknowledged=0,busy=false,stopped=false,tickPending=false;
let queuedBytes=0;const queue=[];
const maximumFrame=8*1024*1024+12,maximumOutput=16*1024*1024;
function fatal(error) {
    stopped=true;if(timer)clearInterval(timer);timer=null;queue.length=0;queuedBytes=0;
    postMessage({type:'error',message:String(error?.message||error).slice(0,4096)});
}
function flush() {
    if(!runtime)return;
    for(let count=0;count<64;count++) {
        const size=runtime._datapump_web_output_size();
        if(!size||size+unacknowledged>maximumOutput)return;
        if(size>maximumFrame)throw new Error('C++ output exceeded frame bound');
        const pointer=runtime._datapump_web_output();if(!pointer)throw new Error('C++ output pointer is unavailable');
        const bytes=runtime.HEAPU8.slice(pointer,pointer+size);
        if(bytes.length!==size||runtime._datapump_web_consume(size)!==0)throw new Error('C++ output consumption failed');
        unacknowledged+=size;postMessage({type:'bytes',bytes},[bytes.buffer]);
    }
}
async function call(name,args=[],types=[]) {
    const result=await runtime.ccall(name,'number',types,args,{async:true});
    if(result!==0)throw new Error(`C++ application ${name} failed`);
}
async function process(message) {
    if(message.type==='init') {
        if(runtime||initializing)throw new Error('Duplicate initialization');initializing=true;
        if(typeof message.factorySource!=='string'||message.factorySource.length>32*1024*1024||!(message.wasmBytes instanceof Uint8Array)||message.wasmBytes.length>128*1024*1024)
            throw new Error('Invalid preloaded application assets');
        const url=URL.createObjectURL(new Blob([message.factorySource],{type:'text/javascript'}));
        try {importScripts(url);} finally {URL.revokeObjectURL(url);}
        if(typeof createDataPump!=='function')throw new Error('Compiled C++ factory is unavailable');
        const deny=()=>{throw new Error('Runtime network and secondary script loading are disabled');};
        for(const key of ['fetch','XMLHttpRequest','WebSocket','WebTransport','EventSource','RTCPeerConnection','importScripts']) {
            try {Object.defineProperty(self,key,{value:deny,writable:false,configurable:false});} catch {}
        }
        runtime=await createDataPump({wasmBinary:message.wasmBytes,noInitialRun:true,
            locateFile:deny,
            instantiateWasm:(imports,receive)=>{WebAssembly.instantiate(message.wasmBytes,imports)
                .then(result=>receive(result.instance,result.module)).catch(fatal);return {};},
            print:()=>{},printErr:line=>postMessage({type:'diagnostic',message:String(line).slice(0,4096)}),
            onAbort:reason=>fatal(new Error(String(reason)))});
        await call('datapump_web_create');flush();
        // The cooperative pump has a 4ms budget. A 10ms service interval can
        // fall behind continuous capture when a DSP turn exhausts that budget.
        // Keep one pending tick and leave input callbacks between bounded turns.
        timer=setInterval(()=>{if(!tickPending){tickPending=true;enqueue({type:'tick'});}},4);
        postMessage({type:'ready'});return;
    }
    if(!runtime)throw new Error('Application is not initialized');
    if(message.type==='feed') {
        const bytes=message.bytes,pointer=runtime._malloc(bytes.length);
        if(!pointer)throw new Error('Application input allocation failed');
        try {runtime.HEAPU8.set(bytes,pointer);await call('datapump_web_feed',[pointer,bytes.length],['number','number']);}
        finally {runtime._free(pointer);}
        flush();postMessage({type:'accepted',id:message.id});
    } else if(message.type==='tick') {tickPending=false;await call('datapump_web_tick');flush();}
    else if(message.type==='flush')flush();
    else if(message.type==='close') {
        if(timer)clearInterval(timer);timer=null;
        await runtime.ccall('datapump_web_destroy',null,[],[],{async:true});runtime=null;stopped=true;close();
    } else throw new Error('Unknown local Worker command');
}
function enqueue(message) {
    if(stopped)return;
    const bytes=message.type==='feed'?message.bytes.byteLength:0;
    if(queue.length>=128||queuedBytes+bytes>maximumOutput){fatal(new Error('Bounded local input queue overflow'));return;}
    queue.push(message);queuedBytes+=bytes;
    if(busy)return;busy=true;
    (async()=>{
        try {while(queue.length&&!stopped){const next=queue.shift();queuedBytes-=next.type==='feed'?next.bytes.byteLength:0;await process(next);}}
        catch(error){fatal(error);}finally{busy=false;}
    })();
}
self.onmessage=event=>{
    try {
        const message=event.data;
        if(message?.type==='output_consumed') {
            if(!Number.isInteger(message.size)||message.size<0||message.size>unacknowledged)throw new Error('Invalid output acknowledgment');
            unacknowledged-=message.size;
            if(!busy)enqueue({type:'flush'});return;
        }
        if(message?.type==='feed'&&(!(message.bytes instanceof Uint8Array)||!message.bytes.length||message.bytes.length>maximumFrame))
            throw new Error('Invalid application input frame');
        if(!message||!['init','feed','close'].includes(message.type))throw new Error('Unknown Worker input');
        enqueue(message);
    } catch(error){fatal(error);}
};
