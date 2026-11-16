'use strict';
const D=JSON.parse(document.getElementById('data').textContent), rows=D.rows;
const $=id=>document.getElementById(id), NS='http://www.w3.org/2000/svg';
const colors={dark:'#30373d',blue:'#429de8',yellow:'#e8bd45',green:'#23a766'};
const color=(v,r)=>v===1&&r===1?colors.green:v===1?colors.yellow:r===1?colors.blue:colors.dark;
const smooth=t=>{t=Math.max(0,Math.min(1,t));return t*t*t*(10+t*(-15+6*t));};
function el(tag,attrs={},parent=$('scene')){const e=document.createElementNS(NS,tag);for(const [k,v]of Object.entries(attrs))e.setAttribute(k,v);parent.append(e);return e;}
function text(x,y,value,size=18,parent=$('scene'),anchor='middle'){const e=el('text',{x,y,'text-anchor':anchor,'dominant-baseline':'middle','font-size':size},parent);e.textContent=value;return e;}
function card(x,y,label,w=200,h=150){el('rect',{x:x-w/2,y:y-h/2,width:w,height:h,rx:4,fill:'#fff',stroke:'#d2cdc2','stroke-width':1.5});text(x,y-h/2+23,label,16);}
function token(x,y,value,opacity=1,w=114){if(value===null||value===undefined||opacity<=0)return;const g=el('g',{opacity});el('rect',{x:x-w/2,y:y-28,width:w,height:56,rx:4,fill:'#eee7d8',stroke:'#b9ad93','stroke-width':1.5},g);text(x,y,value,Math.min(w>114?16:27,(w-14)/(String(value).length*.62)),g);}
function flowSignals(a){return Object.fromEntries(Object.entries(D.wires).map(([name,nets])=>[name,nets.map(net=>Array.isArray(net)?Number(net.every(n=>a[n]===1)):a[net])]));}
const fires=a=>a.reset===1?[]:D.kind==='internal'?internalActions(a):['fork_fire','left_fire','right_fire','join_fire','drain_fire'].filter(k=>a[k]===1);
function signature(a){return D.kind==='internal'?internalSignature(a):D.kind==='flow'?JSON.stringify([flowSignals(a),...['a','b','d','i','out'].map(k=>[a[k+'_valid'],a[k+'_valid']?a[k+'_data']:null]),a.in_valid,a.in_valid?a.in_data:null,a.right_enable]):internalSignature(a);}
const clockMs=417,timeline=[],windows=[];
let first=Math.max(0,rows.findIndex(r=>r.reset===0)),last=rows.length-1;
if(D.kind==='flow')last=rows.reduce((n,a,i)=>a.drain_fire?i:n,first)+1;
for(let i=first;i<=last;i++){
 if(i===first||rows[i].cycle!==rows[i-1].cycle+1)windows.push({start:timeline.length,end:timeline.length});
 const terminal=i===last||rows[i+1].cycle!==rows[i].cycle+1;
 timeline.push({i,j:i,kind:terminal?'end':'edge',ms:clockMs});windows.at(-1).end=timeline.length-1;
}
let at=0,phase=0,playing=false,previous=null,progress=0,windowIndex=0;
const windowLabel=document.createElement('label');windowLabel.textContent='Trace window ';
const windowSelect=document.createElement('select');windowSelect.id='trace-window';
windows.forEach((w,n)=>{const o=document.createElement('option');o.value=n;o.textContent=rows[timeline[w.start].i].cycle+'–'+rows[timeline[w.end].i].cycle;windowSelect.append(o);});
windowSelect.onchange=()=>seek(windows[Number(windowSelect.value)].start);windowLabel.append(windowSelect);if(windows.length>1)document.querySelector('.controls').append(windowLabel);
function setWindow(){windowIndex=windows.findIndex(w=>at>=w.start&&at<=w.end);const w=windows[windowIndex];windowSelect.value=windowIndex;$('scrub').min=w.start;$('scrub').max=w.end;}
setWindow();
$('description').textContent=D.kind==='flow'?'Explicit native buf operations, driven by the checked simulation trace. Forks split and joins merge real values.':'Actual native storage, FSM states and data operations from the checked RTL trace.';
function wire(path,v,r){return el('path',{d:path,fill:'none',stroke:color(Number(v),Number(r)),'stroke-width':3,'stroke-linejoin':'round','stroke-linecap':'round','vector-effect':'non-scaling-stroke'});}
function moving(path,t,value,w=114){const p=path.getPointAtLength(Math.max(0,Math.min(1,t))*path.getTotalLength());token(p.x,p.y,value,1,w);}
function flow(a,b,t,edge){
 $('scene').setAttribute('viewBox','0 190 1920 710');
 const p={input:'M160 535H310',fork_a:'M310 535V347Q310 335 322 335H530',fork_b:'M310 535V723Q310 735 322 735H530',left_in:'M530 335H730',left_out:'M730 335H930',right_in:'M530 735H730',right_out:'M730 735H930',join_d:'M930 335H1138Q1150 335 1150 347V535',join_i:'M930 735H1138Q1150 735 1150 723V535',join_out:'M1150 535H1370',output:'M1370 535H1760'};
 const live=t>=1?b:a,paths={},sa=flowSignals(live);
 for(const k of Object.keys(p)){paths[k]=wire(p[k],...sa[k]);paths[k].dataset.boundary=k;const title=el('title',{},paths[k]);title.textContent=D.wires[k].map(net=>(Array.isArray(net)?net.join(' && '):net)+' = '+(Array.isArray(net)?Number(net.every(n=>a[n]===1)):a[net])).join('\n');}
 paths.left=el('path',{d:'M530 335H930',fill:'none',stroke:'none'});paths.right=el('path',{d:'M530 735H930',fill:'none',stroke:'none'});
 paths.fork_a_travel=el('path',{d:'M310 535V347Q310 335 322 335H530',fill:'none',stroke:'none'});paths.fork_b_travel=el('path',{d:'M310 535V723Q310 735 322 735H530',fill:'none',stroke:'none'});paths.join_travel=el('path',{d:'M1150 535H1370',fill:'none',stroke:'none'});


 const nodes={source:[160,535],a:[530,335],b:[530,735],d:[930,335],i:[930,735],out:[1370,535],receiver:[1760,535]};
 const names={source:'N',a:'left : buf',b:'right : buf',d:'doubled : buf',i:'incremented : buf',out:'egress : buf',receiver:'received'};
 for(const k of Object.keys(nodes))card(...nodes[k],names[k],200,180);
 for(const [x,y,label]of [[310,535,'fork'],[730,335,'×2'],[730,735,'+1'],[1150,535,'+']]){el('circle',{cx:x,cy:y,r:22,fill:'#f5f3ed',stroke:'#9c9588'});text(x,y,label,16);}
 text(730,790,'step_right = '+a.right_enable,15);text(730,270,a.left+' × 2 = '+(a.left*2),14);text(730,670,a.right+' + 1 = '+(a.right+1),14);text(1150,465,a.doubled+' + '+a.incremented+' = '+(a.doubled+a.incremented),14);
 const m=edge?smooth((t-.16)/.68):0,leaving=edge&&t>.16,arrived=edge&&t>=1;
 const events=Object.fromEntries(['fork_fire','left_fire','right_fire','join_fire','drain_fire'].map(k=>[k,edge&&a[k]===1]));
 const departures={a:'left_fire',b:'right_fire',d:'join_fire',i:'join_fire',out:'drain_fire'};
 const incoming={a:['fork_fire',a.in_data],b:['fork_fire',a.in_data],d:['left_fire',a.doubled__pigen_packet_in],i:['right_fire',a.incremented__pigen_packet_in],out:['join_fire',a.egress__pigen_packet_in]};
 const snapshot=arrived?b:a;
 const netNames={a:'left',b:'right',d:'doubled',i:'incremented',out:'egress'};
 for(const k of Object.keys(departures)){
  const name=netNames[k],v=snapshot[k+'_valid'],r=k==='out'?snapshot.egress_ready:snapshot[name+'__pigen_out_ready'];
  text(...nodes[k],snapshot[k+'_data'],25);text(nodes[k][0],nodes[k][1]+58,'V='+v+' / R='+r,14);
 }
 text(...nodes.source,snapshot.ingress,25);text(nodes.source[0],nodes.source[1]+58,'V='+snapshot.ingress_valid+' / R='+snapshot.ingress_ready,14);
 let count=0,received=null;for(let n=0;n<timeline[at].i;n++)if(rows[n].drain_fire){count++;received=rows[n].out_data;}
 if(events.drain_fire&&arrived){received=a.out_data;count++;}
 if(received!==null)token(...nodes.receiver,received,events.drain_fire&&!arrived?1-smooth((m-.2)/.35):.8);
 text(1760,590,count+' accepted',15);
 if(leaving&&!arrived){
  if(events.fork_fire){if(m<.25)moving(paths.input,m/.25,a.in_data);else{moving(paths.fork_a_travel,(m-.25)/.75,a.in_data);moving(paths.fork_b_travel,(m-.25)/.75,a.in_data);}}
  if(events.left_fire)moving(paths.left,m,m<.5?a.left:a.doubled__pigen_packet_in);
  if(events.right_fire)moving(paths.right,m,m<.5?a.right:a.incremented__pigen_packet_in);
  if(events.join_fire){if(m<.65){moving(paths.join_d,m/.65*(1-40/paths.join_d.getTotalLength()),a.d_data);moving(paths.join_i,m/.65*(1-40/paths.join_i.getTotalLength()),a.i_data);}else moving(paths.join_travel,(m-.65)/.35,a.egress__pigen_packet_in);}
  if(events.drain_fire)moving(paths.output,m,a.out_data);
 }
 renderFlowLogic(live);
 $('caption').textContent='Transforms preserve ready/valid; combining operands ANDs valids. Green input means transfer. Values hold until the clock edge.';
}
function pins(i){
 const host=$('pins');host.replaceChildren();if(!D.pins.length)return;
 const svg=document.createElementNS(NS,'svg');svg.setAttribute('viewBox',`0 0 1280 ${D.pins.length*45+35}`);svg.setAttribute('role','img');svg.setAttribute('aria-label','Actual pin waveforms around the current edge');host.append(svg);
 const w=windows[windowIndex],lo=Math.max(timeline[w.start].i,i-30),hi=Math.min(timeline[w.end].i,i+30),x=n=>170+(n-lo)*1060/Math.max(1,hi-lo);
 D.pins.forEach((k,n)=>{const y=22+n*45;text(10,y,k.replace(/^m\d+\./,''),14,svg,'start');let d='',prior=null;for(let j=lo;j<=hi;j++){if(j>lo&&rows[j].cycle!==rows[j-1].cycle+1)prior=null;const raw=rows[j][k],v=typeof raw==='string'&&raw.startsWith('0x')?Number.parseInt(raw,16):raw;if(v!==0&&v!==1){prior=null;continue;}const yy=y+(v?-10:10);d+=prior===null?`M${x(j)} ${yy}`:`H${x(j)}V${yy}`;prior=yy;}el('path',{d,fill:'none',stroke:'#706b63','stroke-width':2},svg);});
 el('path',{d:`M${x(i)} 0V${D.pins.length*45}`,stroke:colors.blue,'stroke-width':2},svg);text(640,D.pins.length*45+15,'Pin window · cycles '+rows[lo].cycle+'–'+rows[hi].cycle,13,svg);
}
function render(){const s=timeline[at],a=rows[s.i],b=s.kind==='end'?a:rows[s.i+1];$('scene').replaceChildren();const t=s.kind==='end'?0:phase;D.kind==='flow'?flow(a,b,t,s.kind==='edge'):renderInternal(a,b,t,s.kind==='edge');$('position').textContent='Edge '+a.cycle+(s.kind==='end'?' · end of recorded window':'');const activity=fires(a).map(k=>({fork_fire:'fork',left_fire:'double',right_fire:'increment',join_fire:'join',drain_fire:'receive'}[k]||k)).join(' · ')||'No data transfer';$('activity').textContent=activity;$('activity').title=activity;$('scrub').value=at;}
function pause(){playing=false;previous=null;$('play').textContent='Play';}
function seek(n){pause();at=Math.max(0,Math.min(timeline.length-1,n));setWindow();phase=0;progress=0;render();}
$('play').onclick=()=>{if(playing){pause();return;}if(at===windows[windowIndex].end){at=windows[windowIndex].start;phase=0;progress=0;}playing=true;previous=null;$('play').textContent='Pause';};
$('step').onclick=()=>seek(Math.min(windows[windowIndex].end,at+1));$('back').onclick=()=>seek(Math.max(windows[windowIndex].start,at-1));$('restart').onclick=()=>seek(windows[windowIndex].start);$('scrub').oninput=e=>seek(Number(e.target.value));$('next').onclick=()=>{let n=at+1;while(n<windows[windowIndex].end&&!fires(rows[timeline[n].i]).length)n++;seek(Math.min(n,windows[windowIndex].end));};
function frame(now){if(playing){if(previous!==null)progress+=(now-previous)*Number($('speed').value);previous=now;while(progress>=clockMs){progress-=clockMs;if(at>=windows[windowIndex].end){pause();progress=0;break;}at++;if(at===windows[windowIndex].end){pause();progress=0;break;}}phase=progress/clockMs;render();}requestAnimationFrame(frame);}
window.pigenPlayer={data:D,timeline,windows,clockMs,get index(){return at;},get phase(){return phase;},get playing(){return playing;},get windowIndex(){return windowIndex;},seek};
render();requestAnimationFrame(frame);

function renderFlowLogic(a){
 let panel=$('flow-logic');if(!panel){panel=document.createElement('details');panel.id='flow-logic';$('scene').after(panel);const h=document.createElement('summary');h.textContent='Emitted SV · inspection';panel.append(h);const note=document.createElement('p');note.textContent='Emitted assignments and captured net values. These implementation nets are shown here without adding stages to the Pigen datapath.';panel.append(note);const list=document.createElement('div');list.id='flow-equations';panel.append(list);const details=document.createElement('details');details.innerHTML='<summary>Complete emitted SystemVerilog</summary>';const pre=document.createElement('pre');pre.textContent=D.emitted_sv;details.append(pre);panel.append(details);}
 const list=$('flow-equations');list.replaceChildren();
 for(const e of D.equations){const row=document.createElement('div');row.className='flow-equation';const value=document.createElement('strong');value.textContent=e.net+' = '+a[e.net];const code=document.createElement('code');code.textContent='assign '+e.net+' = '+e.expression+';';row.append(value,code);list.append(row);}
}
