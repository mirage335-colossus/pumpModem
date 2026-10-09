// Generic presentation adapter. Every application declaration and action comes
// from the C++ facade. This module has no transport or network capability.
export const protocolVersion = 1;
export const eventKinds = Object.freeze({edit:1, select:2, toggle:3, activate:4,
    preset:5, submit:6, record:7, click:8, double_click:9, wheel:10,
    navigate:11, key:12, service:13, close:14, measure:15});
const decimal = value => typeof value === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(value);
const utf8 = new TextEncoder();
const documentExtent=16*1024*1024;
export function validText(value, limit, multiline = true) {
    return typeof value === 'string' && !value.includes('\0') &&
        utf8.encode(value).byteLength <= limit && (multiline || !/[\r\n]/.test(value));
}
function element(doc, tag, text) {
    const node = doc.createElement(tag);
    if (text !== undefined) node.textContent = text;
    return node;
}
function text(node,value) {if(node.textContent!==value)node.textContent=value;}
function place(node,rect,origin={x:0,y:0},limit=16384) {
    if(!rect)return;
    for(const key of ['x','y','w','h'])if(!Number.isFinite(rect[key])||Math.abs(rect[key])>limit)throw new RangeError('Invalid presentation rectangle');
    Object.assign(node.style,{position:'absolute',left:`${rect.x-origin.x}px`,top:`${rect.y-origin.y}px`,width:`${Math.max(0,rect.w)}px`,height:`${Math.max(0,rect.h)}px`});
}
function children(parent,nodes) {
    for(let i=0;i<nodes.length;i++)if(parent.children[i]!==nodes[i])parent.insertBefore(nodes[i],parent.children[i]??null);
    while(parent.children.length>nodes.length)parent.lastElementChild.remove();
}
export class Renderer {
    constructor(root, send, {platformService = null, resize = null} = {}) {
        if (!root?.ownerDocument || typeof send !== 'function') throw new TypeError('A root and local event callback are required');
        this.root=root; this.doc=root.ownerDocument; this.send=send;
        this.platformService=platformService; this.generation='0'; this.sequence=0n;
        this.views=new Map();this.tabViews=new Map();this.documentViews=new Map();this.painted=new WeakMap(); this.serviceState=null; this.lastSnapshot=null;
        this.measured=new Map();this.measurements=new Map();
        this.measurePass=0;
        this.onFonts=()=>{this.measured.clear();if(this.lastSnapshot)this.apply(this.lastSnapshot);};
        this.doc.fonts?.addEventListener?.('loadingdone',this.onFonts);
        this.tabs=element(this.doc,'nav'); this.tabs.setAttribute('aria-label','Application pages');
        this.background=element(this.doc,'main'); this.controls=element(this.doc,'section');
        this.document=element(this.doc,'section'); this.overlay=element(this.doc,'section');
        this.background.className='dp-stage';this.controls.className='dp-controls';this.document.className='dp-document';
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
        if(Resize&&resize) {this.observer=new Resize(entries=>{
            const box=entries[0]?.contentRect;if(!box)return;
            const width=Math.max(240,Math.min(4096,Math.round(box.width))),height=Math.max(240,Math.min(4096,Math.round(box.height)));
            const key=`${width}:${height}`;if(key===this.viewport)return;this.viewport=key;resize(width,height);
        });this.observer.observe(root);}
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
        if(this.views.get(target)!==view||!view.node.isConnected||!view.enabled)return '0';
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
        if(changed) {this.cancelService();this.measured.clear();for(const view of this.views.values())view.invalidate=true;}
        this.generation=snapshot.generation;this.sequence=this.sequence>BigInt(snapshot.ack)?this.sequence:BigInt(snapshot.ack);
        this.lastSnapshot=snapshot;this.doc.title=String(snapshot.title);
        if(snapshot.layout) {
            this.background.style.width=`${snapshot.layout.width}px`;this.background.style.height=`${snapshot.layout.height}px`;
            this.overlay.style.width=`${snapshot.layout.width}px`;this.overlay.style.height=`${snapshot.layout.height}px`;
            place(this.document,snapshot.layout.page);
            const padding=snapshot.layout.documentPadding;
            if(padding)Object.assign(this.document.style,{padding:`${padding.top}px ${padding.side}px ${padding.bottom}px`});
        }
        const tabs=new Set();
        children(this.tabs,snapshot.tabs.map(tab=>{
            tabs.add(tab.id);let button=this.tabViews.get(tab.id);
            if(!button){button=element(this.doc,'button');button.type='button';this.tabViews.set(tab.id,button);
                button.addEventListener('click',()=>{if(button.isConnected&&this.tabViews.get(tab.id)===button)this.event('navigate',tab.id);});}
            text(button,tab.label);place(button,tab.frame);
            button.setAttribute('aria-current',tab.selected?'page':'false');button.disabled=!snapshot.layers.enableBackground;
            return button;
        }));
        for(const [id] of this.tabViews)if(!tabs.has(id))this.tabViews.delete(id);
        const used=new Set();
        this.renderControls(this.controls,snapshot.controls,used, snapshot.layers.enableBackground);
        const documents=new Set();
        this.measurements.clear();
        children(this.document,snapshot.document?[this.renderDocument(snapshot.document,used,snapshot.layers.enableBackground,0,'root',documents)]:[]);
        this.document.hidden=!snapshot.document;
        for(const [id] of this.documentViews)if(!documents.has(id))this.documentViews.delete(id);
        this.renderControls(this.overlay,snapshot.overlay,used,snapshot.layers.enableOverlay);
        for(const [id,view] of this.views)if(!used.has(id)){view.node.remove();this.views.delete(id);}
        this.background.hidden=!snapshot.layers.showBackground;
        this.background.inert=!snapshot.layers.enableBackground;
        this.overlay.hidden=!snapshot.layers.showOverlay;
        this.overlay.inert=!snapshot.layers.enableOverlay;
        this.renderService(snapshot.service);
        this.layoutLists();
        this.measureQueue=[...this.measurements];this.measureCursor=0;++this.measurePass;
        this.measureDocument();
    }
    layoutLists() {
        for(const view of this.views.values()) {
            const pending=view.listLayout;if(!pending||!view.input.isConnected)continue;
            const width=view.input.scrollWidth;if(width<=0)continue;
            // Reconciliation and layer visibility must precede DOM measurement.
            // Hidden lists retain pending work until a visible snapshot arrives.
            for(const row of view.rows.values())row.style.width=`${width}px`;
            view.recordsKey=pending.recordsKey;view.listLayout=null;
            view.input.scrollTop=pending.follow?view.input.scrollHeight:pending.before;
        }
    }
    renderControls(parent,controls,used,enabled) {
        const nodes=controls.map(control=>this.renderControl(control,used,enabled));
        // Moving existing nodes preserves native composition, caret and scroll.
        children(parent,nodes);
    }
    renderControl(c,used,layerEnabled,inDocument=false) {
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
        view.node.className=`dp-control dp-${c.kind}${inDocument?' dp-document-control':''}`;
        view.node.style.fontSize=`${c.fontSize||13}px`;text(view.label,c.label);view.node.title=c.help||'';
        if(c.geometry) {
            const g=c.geometry,limit=inDocument?documentExtent:16384;place(view.node,g.frame,undefined,limit);
            if(view.input)place(view.input,g.widget,g.frame,limit);
            if(view.value)place(view.value,g.widget,g.frame,limit);
            place(view.label,g.label,g.frame,limit);view.label.hidden=!g.hasLabel||c.kind==='label'||c.kind==='menu';
            if(c.kind==='toggle'){view.label.hidden=false;place(view.label,{...g.widget,x:g.widget.x+24,w:g.widget.w-24},g.frame,limit);}
            if(view.presets)place(view.presets,g.suggestions,g.frame,limit);
            if(view.caption){place(view.caption,g.caption,g.frame,limit);view.caption.hidden=!g.hasCaption;view.caption.className=g.captionOverlay?'dp-caption-overlay':'dp-caption';}
        } else if(inDocument) {
            view.node.style.position='relative';view.node.style.width='100%';view.node.style.height='100%';
        }
        const enabled=Boolean(c.enabled&&layerEnabled);
        view.enabled=enabled;
        if(view.input)view.input.disabled=!enabled;
        if(!enabled&&this.doc.activeElement===view.input)view.input.blur?.();
        if(c.kind==='text') {
            view.input.readOnly=Boolean(c.readOnly);
            const pending=BigInt(view.pending||'0')>BigInt(this.lastSnapshot.ack);
            const overridden=!enabled&&Boolean(c.disabledText);
            const shownText=overridden?c.disabledText:c.text;
            // A disabled presentation is authoritative even while an earlier
            // edit is awaiting acknowledgment. Restore the base on withdrawal.
            if((overridden||view.presentationOverride||(!view.composing&&!pending))&&view.input.value!==shownText) {
                const start=view.input.selectionStart,end=view.input.selectionEnd,scroll=view.input.scrollTop;
                view.input.value=shownText;
                if(this.doc.activeElement===view.input)view.input.setSelectionRange(Math.min(start,shownText.length),Math.min(end,shownText.length));
                view.input.scrollTop=scroll;
            }
            view.presentationOverride=overridden;
            if(c.cursorEnd!=='0'&&c.cursorEnd!==view.cursorEnd&&!view.composing) {
                view.input.setSelectionRange(view.input.value.length,view.input.value.length);view.cursorEnd=c.cursorEnd;
            }
            this.options(view.presets,c.options||[],'▾');view.presets.hidden=!c.options?.length;view.presets.disabled=!enabled;
        } else if(c.kind==='choice'||c.kind==='menu') {
            this.options(view.input,c.options||[],c.kind==='menu'?c.label:null);view.input.value=c.kind==='menu'?'':c.selected;
        } else if(c.kind==='toggle')view.input.checked=Boolean(c.checked);
        else if(c.kind==='action')text(view.input,c.label);
        else if(c.kind==='label')text(view.value,c.displayText||c.text||c.label);
        else if(c.kind==='bitmap') {this.paint(view.input,c.bitmap);view.caption.textContent=c.caption||'';}
        else if(c.kind==='list') {
            if(view.input.isConnected&&view.input.scrollWidth>0) {
                const before=view.input.scrollTop;
                view.listScroll={before,follow:Boolean(c.followTail&&before+view.input.clientHeight>=view.input.scrollHeight-3)};
            }
            const saved=view.listScroll??{before:0,follow:Boolean(c.followTail)};
            const before=saved.before,follow=Boolean(c.followTail&&saved.follow);
            const recordsKey=JSON.stringify([c.records,c.rowHeight,c.geometry?.widget?.w,c.geometry?.widget?.h]);
            const measure=recordsKey!==view.recordsKey,keep=new Set();
            for(const record of c.records||[]) {
                keep.add(record.id);let row=view.rows.get(record.id);
                if(!row){row=element(this.doc,'button');row.type='button';row.setAttribute('role','option');view.rows.set(record.id,row);
                    row.addEventListener('click',()=>{const live=view.data.records.find(item=>item.id===record.id);if(live)this.controlEvent(view,view.data.activateOnSelect&&live.activatable?'record':'select',c.id,{value:live.id});});}
                row.disabled=!enabled||!record.enabled;row.setAttribute('aria-selected',record.id===c.selected?'true':'false');
                row.style.height=`${c.rowHeight||28}px`;if(measure)row.style.width='100%';const cells=[];
                for(let index=0;index<record.cells.length;index++) {
                    const cell=record.cells[index];let node=row.children[index];if(!node)node=element(this.doc,'span');
                    text(node,cell.text);node.className=`dp-tone-${cell.tone}`;
                    node.style.fontWeight=cell.bold?'bold':'normal';node.style.fontSize=`${cell.fontSize||13}px`;
                    if(Number.isFinite(cell.x))Object.assign(node.style,{position:'absolute',left:`${cell.x}px`,top:`${cell.y}px`,height:`${cell.h}px`,width:cell.w>0?`${cell.w}px`:`calc(100% - ${cell.x-cell.w}px)`,minWidth:cell.w>0?'0':'max-content',marginRight:cell.w<0?`${-cell.w}px`:'0'});
                    cells.push(node);
                }
                children(row,cells);
            }
            for(const [id,row] of view.rows)if(!keep.has(id)){row.remove();view.rows.delete(id);}
            children(view.input,(c.records||[]).map(record=>view.rows.get(record.id)));
            // Intrinsic text widths include the shared trailing margin. The
            // final attached/visible pass gives every row one scrollable extent.
            if(measure)view.listLayout={recordsKey,follow,before};
        }
        if(view.restore) {const state=view.restore;queueMicrotask(()=>{
            if(!view.node.isConnected)return;if(state.focused){view.input.focus({preventScroll:true});if(state.selection)view.input.setSelectionRange(Math.min(state.selection[0],view.input.value.length),Math.min(state.selection[1],view.input.value.length));}view.input.scrollTop=state.scroll;
        });delete view.restore;}
        return view.node;
    }
    options(select,options,prompt=null) {
        const key=JSON.stringify([prompt,options]);if(select.optionKey===key)return;select.optionKey=key;
        const nodes=[];
        if(prompt!==null){const item=element(this.doc,'option',prompt);item.value='';nodes.push(item);}
        for(const option of options){const item=element(this.doc,'option',option.label);item.value=option.id;item.disabled=!option.enabled;nodes.push(item);}
        children(select,nodes);
    }
    makeControl(c) {
        const view={kind:c.kind,data:c,node:element(this.doc,'div'),label:element(this.doc,'label'),pending:'0',composing:false};
        view.node.append(view.label);view.label.htmlFor=`dp-${c.id}`;
        const input=(tag,type)=>{const item=element(this.doc,tag);if(type)item.type=type;item.id=`dp-${c.id}`;view.input=item;view.node.append(item);return item;};
        if(c.kind==='text') {
            const edit=input(c.multiline?'textarea':'input',c.multiline?null:'text');edit.spellcheck=false;edit.autocomplete='off';
            const commit=()=>{const d=view.data;if(view.composing||!view.enabled||this.views.get(d.id)!==view||!view.node.isConnected)return;if(!validText(edit.value,d.byteLimit,d.multiline)){this.error('Text exceeds this field’s input policy');edit.value=d.text;return;}view.pending=this.event('edit',d.id,{value:edit.value});};
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
        else if(c.kind==='menu')input('select').addEventListener('change',()=>{
            const d=view.data,option=d.options.find(item=>item.id===view.input.value);
            if(this.views.get(d.id)===view&&view.node.isConnected&&d.enabled&&option?.enabled)this.event('activate',option.id);
            view.input.value='';
        });
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
        const {width,height,rgb,rgbRuns}=picture,runs=rgbRuns!==undefined,data=runs?rgbRuns:rgb;
        if(!Number.isInteger(width)||!Number.isInteger(height)||width<1||height<1||width>640||height>320||typeof data!=='string'||data.length>640*320*4||(runs&&rgb!==undefined))
            throw new RangeError('Invalid bounded bitmap');
        const previous=this.painted.get(canvas),revision=picture.revision??data;
        if(previous&&previous.revision===revision&&previous.width===width&&previous.height===height&&previous.sampling===picture.sampling)return;
        const raw=atob(data);
        if(runs?raw.length%4!==0:raw.length!==width*height*3)throw new RangeError('Bitmap byte count mismatch');
        canvas.width=width;canvas.height=height;canvas.style.imageRendering=picture.sampling==='discrete'?'pixelated':'auto';
        const context=canvas.getContext('2d');if(!context)return;
        const image=context.createImageData(width,height);
        let dest=0;
        for(let source=0;source<raw.length;) {
            const count=runs?raw.charCodeAt(source++)+1:1;
            if(dest+count*4>image.data.length)throw new RangeError('Bitmap run exceeds bounds');
            const red=raw.charCodeAt(source++),green=raw.charCodeAt(source++),blue=raw.charCodeAt(source++);
            for(let i=0;i<count;++i){image.data[dest++]=red;image.data[dest++]=green;image.data[dest++]=blue;image.data[dest++]=255;}
        }
        if(dest!==image.data.length)throw new RangeError('Bitmap byte count mismatch');
        context.putImageData(image,0,0);
        this.painted.set(canvas,{revision,width,height,sampling:picture.sampling});
    }
    renderDocument(node,used,enabled,depth=0,path='root',retained=new Set()) {
        if(depth>64)throw new RangeError('Document nesting limit exceeded');
        enabled=enabled&&node.enabled!==false;
        const tag=node.kind==='action'?'button':'div';
        const key=node.id?`action:${node.id}`:path;retained.add(key);
        let view=this.documentViews.get(key);
        if(!view||view.kind!==node.kind){view={kind:node.kind,node:element(this.doc,tag)};this.documentViews.set(key,view);
            if(['text','action','bitmap'].includes(node.kind)){view.content=element(this.doc,node.kind==='bitmap'?'canvas':'span');view.node.append(view.content);}
            if(node.kind==='action'){view.node.type='button';view.node.addEventListener('click',()=>{if(view.node.isConnected&&this.documentViews.get(key)===view&&!view.node.disabled)this.event('activate',view.data.id);});}}
        view.data=node;const result=view.node;result.className=`dp-document-${node.kind} dp-tone-${node.tone} dp-fill-${node.fill||0}`;
        Object.assign(result.style,{minWidth:'0',fontSize:`${Math.max(1,Math.round(node.fontSize??12))}px`,fontWeight:node.bold?'bold':'normal',
            padding:'0',margin:'0',border:'0',overflow:'hidden',outline:node.border?'1px solid GrayText':'none',outlineOffset:'-1px'});
        if(node.geometry) {
            place(result,node.geometry.frame,undefined,documentExtent);result.hidden=!node.geometry.allocated;
            // A relatively positioned root contributes its measured height to
            // scrolling. Descendants use only shared parent-relative rectangles.
            if(depth===0)result.style.position='relative';
            if(view.content)place(view.content,node.geometry.content,undefined,documentExtent);
        }
        if(node.kind==='action')result.disabled=!enabled||node.available===false;
        if(node.kind==='bitmap')this.paint(view.content,node.bitmap);
        else if(node.kind==='text'||node.kind==='action') {
            text(view.content,node.text||'');
            if(node.measure){if(!decimal(node.measure.id)||!Number.isInteger(node.measure.width)||node.measure.width<1||node.measure.width>4096)throw new RangeError('Invalid text measurement request');this.measurements.set(node.measure.id,node);}
        } else if(node.kind==='control')children(result,node.control?[this.renderControl(node.control,used,enabled,true)]:[]);
        else children(result,(node.children||[]).map((child,index)=>this.renderDocument(child,used,enabled,depth+1,`${path}:${index}`,retained)));
        return result;
    }
    measureDocument() {
        for(const id of this.measured.keys())if(!this.measurements.has(id))this.measured.delete(id);
        const values=[];let attempted=0;
        const clock=this.doc.defaultView?.performance;
        const now=clock?.now?.bind(clock),deadline=now?now()+4:Infinity;
        // Yield even on a slow glyph engine so audio forwarding and input can
        // run between batches. Always attempt one item to guarantee progress.
        while(this.measureCursor<this.measureQueue.length&&attempted<512&&(!attempted||!now||now()<deadline)) {
            const [id,node]=this.measureQueue[this.measureCursor++];
            if(this.measured.has(id))continue;
            ++attempted;
            if(!this.measureProbe){this.measureProbe=element(this.doc,'div');this.measureProbe.className='dp-document-measure';this.measureProbe.setAttribute('aria-hidden','true');this.root.append(this.measureProbe);}
            const probe=this.measureProbe;
            Object.assign(probe.style,{width:`${node.measure.width}px`,fontSize:`${Math.max(1,Math.round(node.fontSize??12))}px`,fontWeight:node.bold?'bold':'normal'});
            text(probe,node.text||'');
            const height=Math.ceil(probe.getBoundingClientRect?.().height||0);
            if(!Number.isFinite(height)||height<=0)continue; // Hidden host; retry when visible.
            if(height>16384)throw new RangeError('Wrapped text exceeds its measurement bound');
            this.measured.set(id,height);
            if(height!==node.measure.height)values.push(`${id} ${height}\n`);
        }
        if(values.length)this.event('measure','0',{value:values.join('')});
        if(this.measureCursor<this.measureQueue.length) {
            const pass=this.measurePass,environment=this.doc.defaultView;
            const schedule=environment?.requestAnimationFrame?.bind(environment)||environment?.setTimeout?.bind(environment);
            schedule?.(()=>{if(this.measurePass===pass)this.measureDocument();});
        }
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
    destroy() {++this.measurePass;this.cancelService();this.observer?.disconnect();this.doc.fonts?.removeEventListener?.('loadingdone',this.onFonts);this.root.removeEventListener('keydown',this.onKey);this.root.replaceChildren();this.views.clear();this.tabViews.clear();this.documentViews.clear();this.measured.clear();}
}
