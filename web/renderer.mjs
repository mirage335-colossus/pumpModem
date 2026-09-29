// Generic presentation adapter. Every application declaration and action comes
// from the C++ facade. This module has no transport or network capability.
export const protocolVersion = 1;
export const eventKinds = Object.freeze({edit:1, select:2, toggle:3, activate:4,
    preset:5, submit:6, record:7, click:8, double_click:9, wheel:10,
    navigate:11, key:12, service:13, close:14});
const decimal = value => typeof value === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(value);
const utf8 = new TextEncoder();
export function validText(value, limit, multiline = true) {
    return typeof value === 'string' && !value.includes('\0') &&
        utf8.encode(value).byteLength <= limit && (multiline || !/[\r\n]/.test(value));
}
function element(doc, tag, text) {
    const node = doc.createElement(tag);
    if (text !== undefined) node.textContent = text;
    return node;
}
export class Renderer {
    constructor(root, send, {platformService = null, resize = null} = {}) {
        if (!root?.ownerDocument || typeof send !== 'function') throw new TypeError('A root and local event callback are required');
        this.root=root; this.doc=root.ownerDocument; this.send=send;
        this.platformService=platformService; this.generation='0'; this.sequence=0n;
        this.views=new Map(); this.serviceState=null; this.lastSnapshot=null;
        this.tabs=element(this.doc,'nav'); this.tabs.setAttribute('aria-label','Application pages');
        this.background=element(this.doc,'main'); this.controls=element(this.doc,'section');
        this.document=element(this.doc,'section'); this.overlay=element(this.doc,'section');
        this.overlay.className='dp-overlay'; this.overlay.setAttribute('role','dialog');
        this.services=element(this.doc,'section'); this.services.setAttribute('role','dialog');
        this.services.className='dp-service'; this.status=element(this.doc,'p');
        this.status.setAttribute('role','status'); this.status.className='dp-status';
        this.background.append(this.tabs,this.controls,this.document);
        root.classList.add('datapump'); root.replaceChildren(this.background,this.overlay,this.services,this.status);
        this.onKey=e=>{
            if(e.isComposing||e.keyCode===229||!this.lastSnapshot?.layers.showOverlay)return;
            const names=['','Escape','Enter',' ','Tab','ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Backspace','Delete'];
            const code=names.indexOf(e.key);
            if(code>0 && e.target===this.root) {e.preventDefault();this.event('key','0',{amount:code,ctrl:e.ctrlKey,shift:e.shiftKey,alt:e.altKey});}
            else if(e.key==='Escape') {e.preventDefault();this.event('key','0',{amount:1,ctrl:e.ctrlKey,shift:e.shiftKey,alt:e.altKey});}
        };
        root.addEventListener('keydown',this.onKey);
        const Resize=root.ownerDocument.defaultView?.ResizeObserver;
        if(Resize&&resize) {this.observer=new Resize(entries=>{const box=entries[0]?.contentRect;if(box)resize(Math.round(box.width),Math.round(box.height));});this.observer.observe(root);}
    }
    envelope(kind,target,extra={}) {
        if(!decimal(target)||this.generation==='0')throw new TypeError('A current presentation target is required');
        const sequence=String(++this.sequence);
        return {version:protocolVersion,generation:this.generation,sequence,target,kind:eventKinds[kind],...extra};
    }
    event(kind,target,extra={}) {
        const envelope=this.envelope(kind,target,extra);this.send(envelope);return envelope.sequence;
    }
    controlEvent(view,kind,target,extra={}) {
        if(this.views.get(target)!==view||!view.node.isConnected||!view.data.enabled)return '0';
        return this.event(kind,target,extra);
    }
    error(message) {this.status.textContent=String(message);}
    apply(snapshot) {
        if(typeof snapshot==='string') {
            if(snapshot.length>8*1024*1024)throw new RangeError('Presentation exceeds its bound');
            snapshot=JSON.parse(snapshot);
        }
        if(snapshot?.version!==protocolVersion||!decimal(snapshot.generation)||!decimal(snapshot.ack)||
           !Array.isArray(snapshot.controls)||!Array.isArray(snapshot.overlay)||!Array.isArray(snapshot.tabs)||!snapshot.layers)
            throw new TypeError('Unsupported presentation envelope');
        if(BigInt(snapshot.generation)<BigInt(this.generation))return false;
        if(snapshot.generation===this.generation&&decimal(snapshot.revision)&&decimal(this.lastSnapshot?.revision)&&
           BigInt(snapshot.revision)<BigInt(this.lastSnapshot.revision))return false;
        const changed=this.generation!==snapshot.generation;
        // A generation withdraws all outstanding edits, dialogs and callbacks.
        if(changed) {this.cancelService();for(const view of this.views.values())view.invalidate=true;}
        this.generation=snapshot.generation;this.sequence=this.sequence>BigInt(snapshot.ack)?this.sequence:BigInt(snapshot.ack);
        this.lastSnapshot=snapshot;this.doc.title=String(snapshot.title);
        this.tabs.replaceChildren(...snapshot.tabs.map(tab=>{
            const button=element(this.doc,'button',tab.label);button.type='button';
            button.setAttribute('aria-current',tab.selected?'page':'false');button.disabled=!snapshot.layers.enableBackground;
            button.addEventListener('click',()=>this.event('navigate',tab.id));return button;
        }));
        const used=new Set();
        this.renderControls(this.controls,snapshot.controls,used, snapshot.layers.enableBackground);
        this.document.replaceChildren();
        if(snapshot.document)this.document.append(this.renderDocument(snapshot.document,used,snapshot.layers.enableBackground));
        this.renderControls(this.overlay,snapshot.overlay,used,snapshot.layers.enableOverlay);
        for(const [id,view] of this.views)if(!used.has(id)){view.node.remove();this.views.delete(id);}
        this.background.hidden=!snapshot.layers.showBackground;
        this.background.inert=!snapshot.layers.enableBackground;
        this.overlay.hidden=!snapshot.layers.showOverlay;
        this.overlay.inert=!snapshot.layers.enableOverlay;
        this.renderService(snapshot.service);
    }
    renderControls(parent,controls,used,enabled) {
        const nodes=controls.map(control=>this.renderControl(control,used,enabled));
        // Moving existing nodes preserves native composition, caret and scroll.
        for(let i=0;i<nodes.length;i++)if(parent.children[i]!==nodes[i])parent.insertBefore(nodes[i],parent.children[i]??null);
        while(parent.children.length>nodes.length)parent.lastElementChild.remove();
    }
    renderControl(c,used,layerEnabled) {
        if(!decimal(c.id)||typeof c.kind!=='string')throw new TypeError('Invalid control declaration');
        used.add(c.id);
        let view=this.views.get(c.id);
        if(view&&(view.kind!==c.kind||view.history!==c.history||view.invalidate)) {
            const focused=this.doc.activeElement===view.input;
            const selection=focused?[view.input.selectionStart,view.input.selectionEnd]:null;
            const scroll=view.input?.scrollTop??0;
            view.node.remove();this.views.delete(c.id);view=null;
            view=this.makeControl(c);this.views.set(c.id,view);
            view.restore={focused,selection,scroll};
        }
        if(!view){view=this.makeControl(c);this.views.set(c.id,view);}
        view.data=c;view.history=c.history;view.invalidate=false;
        view.node.className=`dp-control dp-${c.kind}`;view.node.style.flexGrow=String(Math.max(1,Math.min(20,c.stretch||1)));
        view.label.textContent=c.label;view.node.title=c.help||'';
        const enabled=Boolean(c.enabled&&layerEnabled);
        if(view.input)view.input.disabled=!enabled;
        if(c.kind==='text') {
            view.input.readOnly=Boolean(c.readOnly);
            const pending=BigInt(view.pending||'0')>BigInt(this.lastSnapshot.ack);
            if(!view.composing&&!pending&&view.input.value!==c.text) {
                const start=view.input.selectionStart,end=view.input.selectionEnd,scroll=view.input.scrollTop;
                view.input.value=c.text;
                if(this.doc.activeElement===view.input)view.input.setSelectionRange(Math.min(start,c.text.length),Math.min(end,c.text.length));
                view.input.scrollTop=scroll;
            }
            if(c.cursorEnd!=='0'&&c.cursorEnd!==view.cursorEnd&&!view.composing) {
                view.input.setSelectionRange(view.input.value.length,view.input.value.length);view.cursorEnd=c.cursorEnd;
            }
            view.presets.replaceChildren();view.presets.hidden=!c.options?.length;
            if(c.options?.length) {
                view.presets.append(element(this.doc,'option','Presets'));
                for(const option of c.options){const item=element(this.doc,'option',option.label);item.value=option.id;item.disabled=!option.enabled;view.presets.append(item);}
            }
        } else if(c.kind==='choice') {
            view.input.replaceChildren(...(c.options||[]).map(option=>{const item=element(this.doc,'option',option.label);item.value=option.id;item.disabled=!option.enabled;return item;}));view.input.value=c.selected;
        } else if(c.kind==='toggle')view.input.checked=Boolean(c.checked);
        else if(c.kind==='action')view.input.textContent=c.label;
        else if(c.kind==='label')view.value.textContent=c.displayText||c.text;
        else if(c.kind==='bitmap') {this.paint(view.input,c.bitmap);view.caption.textContent=c.caption||'';}
        else if(c.kind==='list') {
            const before=view.input.scrollTop,follow=c.followTail&&before+view.input.clientHeight>=view.input.scrollHeight-3;
            const keep=new Set();
            for(const record of c.records||[]) {
                keep.add(record.id);let row=view.rows.get(record.id);
                if(!row){row=element(this.doc,'button');row.type='button';row.setAttribute('role','option');view.rows.set(record.id,row);
                    row.addEventListener('click',()=>{const live=view.data.records.find(item=>item.id===record.id);if(live)this.controlEvent(view,view.data.activateOnSelect&&live.activatable?'record':'select',c.id,{value:live.id});});}
                row.disabled=!enabled||!record.enabled;row.setAttribute('aria-selected',record.id===c.selected?'true':'false');
                row.replaceChildren(...record.cells.map(cell=>{const node=element(this.doc,cell.bold?'strong':'span',cell.text);node.className=`dp-tone-${cell.tone}`;return node;}));view.input.append(row);
            }
            for(const [id,row] of view.rows)if(!keep.has(id)){row.remove();view.rows.delete(id);}
            view.input.scrollTop=follow?view.input.scrollHeight:before;
        }
        if(view.restore) {const state=view.restore;queueMicrotask(()=>{
            if(!view.node.isConnected)return;if(state.focused){view.input.focus({preventScroll:true});if(state.selection)view.input.setSelectionRange(Math.min(state.selection[0],view.input.value.length),Math.min(state.selection[1],view.input.value.length));}view.input.scrollTop=state.scroll;
        });delete view.restore;}
        return view.node;
    }
    makeControl(c) {
        const view={kind:c.kind,data:c,node:element(this.doc,'div'),label:element(this.doc,'label'),pending:'0',composing:false};
        view.node.append(view.label);view.label.htmlFor=`dp-${c.id}`;
        const input=(tag,type)=>{const item=element(this.doc,tag);if(type)item.type=type;item.id=`dp-${c.id}`;view.input=item;view.node.append(item);return item;};
        if(c.kind==='text') {
            const edit=input(c.multiline?'textarea':'input',c.multiline?null:'text');edit.spellcheck=false;edit.autocomplete='off';
            const commit=()=>{const d=view.data;if(view.composing||this.views.get(d.id)!==view||!view.node.isConnected)return;if(!validText(edit.value,d.byteLimit,d.multiline)){this.error('Text exceeds this field’s input policy');edit.value=d.text;return;}view.pending=this.event('edit',d.id,{value:edit.value});};
            edit.addEventListener('compositionstart',()=>{view.composing=true;});
            edit.addEventListener('compositionend',()=>{view.composing=false;commit();});
            edit.addEventListener('input',event=>{if(!event.isComposing)commit();});
            edit.addEventListener('keydown',event=>{
                if(event.isComposing||view.composing||event.keyCode===229)return;
                const d=view.data;
                if(event.key==='Enter'&&!event.shiftKey&&(event.ctrlKey?d.submitCtrlEnter:d.submitEnter)) {
                    event.preventDefault();commit();this.controlEvent(view,'submit',d.id,{ctrl:event.ctrlKey,shift:event.shiftKey});
                }
            });
            view.presets=element(this.doc,'select');view.presets.setAttribute('aria-label','Text presets');view.node.append(view.presets);
            view.presets.addEventListener('change',()=>{if(view.presets.selectedIndex>0)this.controlEvent(view,'preset',view.data.id,{value:view.presets.value});});
        } else if(c.kind==='choice')input('select').addEventListener('change',()=>this.controlEvent(view,'select',view.data.id,{value:view.input.value}));
        else if(c.kind==='toggle')input('input','checkbox').addEventListener('change',()=>this.controlEvent(view,'toggle',view.data.id,{checked:view.input.checked}));
        else if(c.kind==='action')input('button','button').addEventListener('click',()=>this.controlEvent(view,'activate',view.data.id));
        else if(c.kind==='label') {view.value=element(this.doc,'pre');view.node.append(view.value);}
        else if(c.kind==='list') {input('div').setAttribute('role','listbox');view.rows=new Map();}
        else if(c.kind==='bitmap') {
            const canvas=input('canvas');canvas.setAttribute('role','img');canvas.setAttribute('aria-label',c.label||'Application plot');
            view.caption=element(this.doc,'p');view.node.append(view.caption);
            canvas.addEventListener('click',event=>{if(view.data.click&&event.detail===1)this.controlEvent(view,'click',view.data.id);});
            canvas.addEventListener('dblclick',()=>{if(view.data.doubleClick)this.controlEvent(view,'double_click',view.data.id);});
            canvas.addEventListener('wheel',event=>{if(view.data.wheel){event.preventDefault();this.controlEvent(view,'wheel',view.data.id,{amount:event.deltaY<0?1:-1});}},{passive:false});
        } else throw new TypeError('Unknown presentation primitive');
        return view;
    }
    paint(canvas,picture) {
        if(!picture)return;
        const {width,height,rgb}=picture;
        if(!Number.isInteger(width)||!Number.isInteger(height)||width<1||height<1||width>640||height>320||typeof rgb!=='string'||rgb.length>640*320*4)
            throw new RangeError('Invalid bounded bitmap');
        const raw=atob(rgb);if(raw.length!==width*height*3)throw new RangeError('Bitmap byte count mismatch');
        canvas.width=width;canvas.height=height;canvas.style.imageRendering=picture.sampling==='discrete'?'pixelated':'auto';
        const context=canvas.getContext('2d');if(!context)return;
        const image=context.createImageData(width,height);
        for(let source=0,dest=0;source<raw.length;source+=3,dest+=4){image.data[dest]=raw.charCodeAt(source);image.data[dest+1]=raw.charCodeAt(source+1);image.data[dest+2]=raw.charCodeAt(source+2);image.data[dest+3]=255;}
        context.putImageData(image,0,0);
    }
    renderDocument(node,used,enabled,depth=0) {
        if(depth>64)throw new RangeError('Document nesting limit exceeded');
        enabled=enabled&&node.enabled!==false;
        if(node.kind==='control')return node.control?this.renderControl(node.control,used,enabled):element(this.doc,'span');
        const tag=node.kind==='action'?'button':node.kind==='bitmap'?'canvas':node.kind==='text'?'p':'div';
        const result=element(this.doc,tag);result.className=`dp-document-${node.kind}`;
        if(node.kind==='action'){result.type='button';result.disabled=!enabled||node.available===false;result.addEventListener('click',()=>this.event('activate',node.id));}
        if(node.kind==='bitmap')this.paint(result,node.bitmap);
        else {result.textContent=node.text||'';for(const child of node.children||[])result.append(this.renderDocument(child,used,enabled,depth+1));}
        return result;
    }
    cancelService() {
        this.serviceState?.abort.abort();this.serviceState=null;this.services.replaceChildren();this.services.hidden=true;
    }
    renderService(request) {
        if(!request){this.cancelService();return;}
        if(this.serviceState?.id===request.id)return;
        this.cancelService();const state={id:request.id,generation:this.generation,abort:new AbortController()};this.serviceState=state;
        this.services.hidden=false;this.services.append(element(this.doc,'h2',request.title));
        const finish=result=>{if(this.serviceState!==state||state.abort.signal.aborted||this.generation!==state.generation)return;
            if(!result?.handled)this.event('service',request.id,result);this.cancelService();};
        const cancel=element(this.doc,'button','Cancel');cancel.type='button';cancel.addEventListener('click',()=>finish({cancelled:true}));
        if(request.kind==='prompt') {
            const input=element(this.doc,'input');input.type='text';input.value=request.value;input.setAttribute('aria-label',request.title);
            const accept=element(this.doc,'button','Continue');accept.type='button';accept.addEventListener('click',()=>{if(validText(input.value,request.byteLimit,false))finish({value:input.value});else this.error('Input exceeds this request’s policy');});
            this.services.append(input,accept);
        } else {
            if(request.kind==='clipboard'){const text=element(this.doc,'textarea');text.value=request.value;text.readOnly=true;text.setAttribute('aria-label',request.title);this.services.append(text);}
            const action=element(this.doc,'button',request.kind==='clipboard'?'Copy':request.kind==='save_file'?'Choose destination':'Choose file');action.type='button';
            action.addEventListener('click',async()=>{
                action.disabled=true;
                try {
                    if(this.platformService)finish(await this.platformService(request,state.abort.signal,value=>this.envelope('service',request.id,{value})));
                    else if(request.kind==='clipboard'&&this.doc.defaultView.navigator.clipboard){await this.doc.defaultView.navigator.clipboard.writeText(request.value);finish({value:''});}
                    else {action.disabled=false;this.error('This host has not supplied the requested file service');}
                } catch(error){if(!state.abort.signal.aborted){action.disabled=false;this.error(error?.message||'The platform request was denied');}}
            });this.services.append(action);
        }
        this.services.append(cancel);
    }
    destroy() {this.cancelService();this.observer?.disconnect();this.root.removeEventListener('keydown',this.onKey);this.root.replaceChildren();this.views.clear();}
}
