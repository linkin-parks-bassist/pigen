/* Native data dependencies and observed values. No inferred control hardware. */
let internalView=0,internalInitialized=false,inspectKey=null;
function number(value){try{return value===null||value===undefined?null:BigInt(value);}catch{return null;}}
function isBuffered(node){return node&&(['buf','skid','port'].includes(node.kind)||node.kind.startsWith('fifo'));}
function resetOnly(event){return ['reset','reset==1',"reset==1'b1"].includes((event.condition||'').replace(/[()\s]/g,''));}
function nodeFormat(node,value){let n=number(value);if(n===null)return 'x';if(node.signed){const w=BigInt(node.width);if(n>=(1n<<(w-1n)))n-=1n<<w;return String(n);}return node.width===1?String(n):'0x'+n.toString(16).padStart(Math.ceil(node.width/4),'0');}
function internalActions(row){if(row.reset===1)return [];const view=D.views[internalView],byKey=new Map(D.nodes.map(n=>[n.key,n]));return (row.writes||[]).filter(([id])=>!resetOnly(D.events[id])).flatMap(([id,value])=>{const op=view.operations.find(op=>op.event===id);if(!op)return [];const source=op.deps.length?op.deps.map(key=>byKey.get(key).name).join(', '):nodeFormat(byKey.get(op.dest),value);return [source+' → '+byKey.get(op.dest).name];});}
function internalSignature(row){return JSON.stringify(Object.fromEntries(Object.entries(row).filter(([key])=>!['writes','offers','cycle'].includes(key))));}
function internalNodes(view){return D.nodes.filter(n=>view.keys.includes(n.key));}
function setupInternal(){
 if(internalInitialized)return;internalInitialized=true;window.internalViews=D.views;$('next').textContent='Next operation';
 const label=document.createElement('label');label.textContent='Datapath ';const select=document.createElement('select');select.id='datapath';
 D.views.forEach((view,i)=>{const option=document.createElement('option');option.value=i;option.textContent=view.name;select.append(option);});
 select.onchange=()=>{internalView=Number(select.value);inspectKey=null;const view=D.views[internalView];const next=timeline.findIndex(s=>rows[s.i].reset===0&&(rows[s.i].writes||[]).some(([id])=>view.operations.some(op=>op.event===id)));if(next>=0)seek(next);else render();};label.append(select);document.querySelector('.controls').append(label);
 const states=document.createElement('div');states.id='fsm';$('scene').before(states);
 const log=document.createElement('details');log.id='operations';$('scene').after(log);
 const inspect=document.createElement('details');inspect.id='native-inspection';inspect.innerHTML='<summary>Source operations</summary><div id="native-operation-list"></div>';$('caption').after(inspect);
}
function bitStrip(node,value,x,y,parent){
 const n=number(value);if(n===null||node.width<2||node.width>16||node.signed)return;
 const width=138,gap=width/node.width;
 for(let j=0;j<node.width;j++){const bit=Number((n>>BigInt(node.width-j-1))&1n);el('rect',{x:x-width/2+j*gap+.6,y:y-7,width:gap-1.2,height:14,rx:1,fill:bit?'#d9eadf':'#eeeae1'},parent);text(x-width/2+(j+.5)*gap,y,bit,10,parent);}
}
function stateDisplay(module,row,view){
 const host=$('fsm');host.replaceChildren();
 let states=module.states;if(states.length===2&&/^(AW|Write|AR|Read)/.test(view.name))states=[states[/^(AR|Read)/.test(view.name)?1:0]];
 for(const state of states){const strip=document.createElement('div');strip.className='fsm-strip';state.labels.forEach((label,i)=>{const chip=document.createElement('span');chip.textContent=label;chip.className=Number(number(row[state.key]))===i?'state-current':'';strip.append(chip);});host.append(strip);}
}
function renderInternal(a,b,t,edge){
 setupInternal();const view=D.views[internalView],module=D.modules[view.module],nodes=internalNodes(view),byKey=new Map(D.nodes.map(n=>[n.key,n]));
 const graph=new Map(view.graph_nodes.map(n=>[n.key,n])),current=edge&&t>=1?b:a,writes=new Map((a.writes||[]).filter(([id])=>!resetOnly(D.events[id])));
 const visibleIds=new Set(view.operations.filter(op=>op.event!==null).map(op=>op.event));
 $('description').textContent=module.name+' · '+view.name;stateDisplay(module,current,view);
 $('scene').setAttribute('viewBox',`0 0 ${view.width} ${view.height}`);$('scene').style.width=`min(100%, ${view.width}px)`;
 const defs=el('defs');const marker=el('marker',{id:'data-arrow',viewBox:'0 0 8 8',refX:7,refY:4,markerWidth:5,markerHeight:5,orient:'auto-start-reverse',markerUnits:'strokeWidth'},defs);el('path',{d:'M0 0L8 4L0 8Z',fill:'context-stroke'},marker);
 const paths=[];let edgeIndex=0;
 for(const edge of view.graph_edges){
  const source=graph.get(edge.src),target=graph.get(edge.dest),src=byKey.get(edge.src),sink=byKey.get(target?.kind==='expression'?target.dest:edge.dest);
  const sourceNodes=source.kind==='expression'?source.deps.map(k=>byKey.get(k)):[src];
  const valid=edge.observational?1:Number(sourceNodes.every(n=>!isBuffered(n)||number(current[n.key+'.valid'])===1n));
  const ready=isBuffered(sink)?Number(number(current[sink.key+'.capacity']??current[sink.key+'.ready'])):1;
  const points=edge.points;let d=`M${points[0].join(' ')}`;for(let i=1;i+2<points.length;i+=3)d+=`C${points[i].join(' ')} ${points[i+1].join(' ')} ${points[i+2].join(' ')}`;
  const path=el('path',{d,fill:'none',stroke:color(valid,ready),'stroke-width':3,'stroke-linecap':'round','vector-effect':'non-scaling-stroke','marker-end':'url(#data-arrow)','data-edge-index':edgeIndex++,'data-source':edge.src,'data-destination':edge.dest,'data-valid':valid,'data-ready':ready});
  const title=el('title',{},path);title.textContent=edge.expressions.join('\n');paths.push([edge,path]);
  for(const label of edge.labels){const bg=el('rect',{x:label.x-label.width/2-4,y:label.y-label.size+1,width:label.width+8,height:label.size+5,fill:'#f5f3ed'});bg.setAttribute('class','expression-label-background');const value=text(label.x,label.y,label.text,label.size);value.setAttribute('dominant-baseline','alphabetic');value.setAttribute('class','expression-label');}
 }
 for(const vertex of view.graph_nodes){
  const {x,y,w,h}=vertex;
  if(vertex.kind==='expression'){
   const lines=vertex.name.split('\n'),group=el('g',{class:'source-expression','data-expression':vertex.rhs});
   lines.forEach((line,i)=>{const label=text(x,y+(i-(lines.length-1)/2)*16,line,13,group);label.setAttribute('class','expression-label');});
   const title=el('title',{},group);title.textContent=vertex.rhs;continue;
  }
  const node=byKey.get(vertex.key),group=el('g',{class:'register','data-key':node.key,style:'cursor:pointer'});
  group.onclick=()=>{inspectKey=node.key;const panel=$('native-inspection');panel.open=true;render();};
  el('rect',{x:x-w/2,y:y-h/2,width:w,height:h,rx:3,fill:'#fff',stroke:inspectKey===node.key?'#928979':'#c8c1b5','stroke-width':1.2},group);
  text(x,y-h/2+17,node.name,16,group);text(x,y-h/2+34,(node.kind==='reg'?'reg':node.kind)+' · '+node.width+' bits',11,group);
  const formatted=nodeFormat(node,current[node.key]);if(formatted.length>20){const lines=formatted.match(/.{1,18}/g);lines.forEach((line,i)=>text(x,y+3+(i-(lines.length-1)/2)*15,line,13,group));}else text(x,y+3,formatted,Math.min(22,(w-18)/(formatted.length*.62)),group);bitStrip(node,current[node.key],x,y+25,group);
  if(isBuffered(node))text(x,y+h/2-9,'V '+(number(current[node.key+'.valid'])??'x')+' / R '+(number(current[node.key+'.ready'])??'x'),12,group);
  else if(vertex.constants?.length){const constants=vertex.constants.join(', '),line='← '+constants;const foot=text(x,y+h/2-9,line.length>24?line.slice(0,23)+'…':line,11,group);const title=el('title',{},foot);title.textContent=vertex.constants.map(rhs=>node.name+' <= '+rhs).join('\n');}
 }
 if(edge&&t>.16&&t<.84){const progress=smooth((t-.16)/.68);for(const [link,path]of paths){const fired=link.events.filter(id=>writes.has(id));if(!fired.length)continue;const id=fired.at(-1),event=D.events[id],source=byKey.get(link.src),target=graph.get(link.dest),origin=graph.get(link.src);let travel=progress;if(target?.kind==='expression'){if(progress>=.5)continue;travel=progress*2;}else if(origin?.kind==='expression'){if(progress<.5)continue;travel=(progress-.5)*2;}const destination=byKey.get(event.dest),field={...destination,width:event.hi-event.lo+1,signed:event.hi-event.lo+1===destination.width&&destination.signed};const value=target?.kind==='expression'&&source?nodeFormat(source,a[source.key]):nodeFormat(field,writes.get(id));moving(path,travel,value,Math.min(230,Math.max(88,String(value).length*8+16)));}}
 const entries=[...writes].filter(([id])=>visibleIds.has(id)),log=$('operations');log.replaceChildren();const heading=document.createElement('summary');heading.textContent='Executed source operations · '+entries.length;log.append(heading);
 if(!entries.length){const hold=document.createElement('p');hold.textContent='No clocked assignment in this view.';log.append(hold);}
 for(const [id,value]of entries){const event=D.events[id],line=document.createElement('div');line.className='operation';line.textContent=event.label+' → '+nodeFormat({...byKey.get(event.dest),width:event.hi-event.lo+1},value);log.append(line);}
 const inspector=$('native-operation-list');inspector.replaceChildren();const selected=view.operations.filter(op=>!inspectKey||op.dest===inspectKey);
 for(const op of selected){const line=document.createElement('div');line.className='operation';line.textContent=op.label;inspector.append(line);}
 document.querySelectorAll('.source-line.active').forEach(n=>n.classList.remove('active'));
 for(const [id]of entries){const e=D.events[id];document.querySelector(`.source-line[data-module="${e.module}"][data-line="${e.line}"]`)?.classList.add('active');}
 pins(edge&&t>=1?Math.min(rows.length-1,timeline[at].i+1):timeline[at].i);
 $('caption').textContent='Source data paths and captured register values. Reset assignments are omitted.';
}
