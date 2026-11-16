#!/usr/bin/env python3
"""Browser acceptance checks for internal visibility, controls, views and clock phases."""
from pathlib import Path
import re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
SCRIPT=r'''<script>
try {
 const p=window.pigenPlayer;
 if(p.data.kind!=='internal')throw Error('missing internal view');
 const seen=new Set();
 if(p.timeline.some(s=>s.ms!==417||!['edge','end'].includes(s.kind)))throw Error('nonuniform clocks');
 for(const w of p.windows)for(let n=w.start;n<w.end;n++)if(p.data.rows[p.timeline[n+1].i].cycle!==p.data.rows[p.timeline[n].i].cycle+1)throw Error('automatic capture gap');

 for(let view=0;view<window.internalViews.length;view++){
  const select=document.getElementById('datapath');select.value=view;select.dispatchEvent(new Event('change'));
  const expected=internalNodes(window.internalViews[view]);
  if(document.querySelectorAll('.register').length!==expected.length)throw Error('hidden declared storage');
  expected.forEach(n=>seen.add(n.key));
  const sample=new Set([0,p.timeline.length-1]);
  for(let n=0;n<p.timeline.length;n+=Math.max(1,Math.floor(p.timeline.length/35)))sample.add(n);
  const keySet=new Set(expected.map(n=>n.key));
  for(let n=0;n<p.timeline.length;n++)if(p.data.rows[p.timeline[n].i].writes.some(([id])=>keySet.has(p.data.events[id].dest)&&/<<|>>|\{/.test(p.data.events[id].label))){sample.add(n);break;}
  let geometry=null;
  for(const n of sample)for(const t of [0,.5,.82,1]){
   p.seek(n);phase=t;render();
   if(document.querySelector('.circuit-viewport'))throw Error('scrollable circuit');
   if(new Set([...document.querySelectorAll('.register')].map(n=>n.dataset.key)).size!==expected.length)throw Error('duplicate registers');
   const rects=['.controls','.clock-status','#scrub','#scene','#operations','#pins'].map(k=>{const r=document.querySelector(k).getBoundingClientRect();return [r.top,r.height];});
   if(geometry&&JSON.stringify(rects)!==geometry)throw Error('playback moves layout');geometry=JSON.stringify(rects);
   const byKey=new Map(p.data.nodes.map(n=>[n.key,n])),viewData=window.internalViews[view],graph=new Map(viewData.graph_nodes.map(n=>[n.key,n])),row=p.data.rows[Math.min(p.data.rows.length-1,p.timeline[p.index].i+(p.timeline[p.index].kind==='edge'&&t>=1?1:0))];
   for(const wire of document.querySelectorAll('path[data-source]')){
    const edge=viewData.graph_edges[Number(wire.dataset.edgeIndex)],src=graph.get(edge.src),dst=graph.get(edge.dest);
    const sources=src.kind==='expression'?src.deps.map(k=>byKey.get(k)):[byKey.get(src.key)],sink=byKey.get(dst.kind==='expression'?dst.dest:dst.key);
    const buffered=n=>n&&(['buf','skid','port'].includes(n.kind)||n.kind.startsWith('fifo'));
    const v=edge.observational?1:Number(sources.every(n=>!buffered(n)||BigInt(row[n.key+'.valid']??0)===1n)),r=buffered(sink)?Number(BigInt(row[sink.key+'.capacity']??row[sink.key+'.ready']??0)):1;
    const expected=v===1&&r===1?'#23a766':v===1?'#e8bd45':r===1?'#429de8':'#30373d';
    if(wire.getAttribute('stroke')!==expected)throw Error('endpoint colours '+edge.src+' → '+edge.dest);
    if(wire.getAttribute('stroke-width')!=='3')throw Error('wire width');
   }
   if(document.querySelector('.driver,.control-decoder'))throw Error('fabricated selector/decoder');
   for(const op of viewData.operations)if(op.event!==null&&resetOnly(p.data.events[op.event]))throw Error('reset in data view');
   if(document.querySelector('#scene').innerHTML.includes('NaN'))throw Error('invalid geometry');
   if(document.querySelectorAll('.register').length!==expected.length)throw Error('storage vanished');
  }
  p.seek(0);const card=document.querySelector('.register');card.dispatchEvent(new Event('click'));if(!document.querySelector('#native-inspection').open)throw Error('source inspection');
 }
 for(const n of p.data.nodes)if(!seen.has(n.key))throw Error('unreachable storage '+n.name);
 if(p.data.modules.some(m=>m.name==='pigen_spi_controller'))for(const name of ['tx_shift','rx_shift','timer','remaining','mosi','miso','sck','cs_n','command','response'])if(!p.data.nodes.some(n=>n.name===name))throw Error('missing SPI internals '+name);
 for(const n of [0,Math.floor(p.windows[0].end/2)]){
  p.seek(n);document.getElementById('play').click();frame(0);frame(417);if(p.index!==n+1||p.phase!==0)throw Error('tick duration changes');pause();
 }
 p.seek(p.windows[0].end-1);document.getElementById('play').click();frame(0);frame(1000);if(p.index!==p.windows[0].end||p.playing)throw Error('autoplay crossed capture window');
 p.seek(0);document.getElementById('step').click();if(p.index!==1)throw Error('step');document.getElementById('back').click();if(p.index!==0)throw Error('back');
 document.getElementById('next').click();if(!fires(p.data.rows[p.timeline[p.index].i]).length)throw Error('next action');
 document.getElementById('scrub').value=2;document.getElementById('scrub').dispatchEvent(new Event('input'));if(p.index!==2)throw Error('scrub');
 document.getElementById('play').click();frame(1000);frame(1550);document.getElementById('play').click();
 const result=document.createElement('p');result.id='qa';result.textContent='PASS '+seen.size+' actual storage nodes; all datapath views, sampled phases, uniform clocks, capture boundaries, endpoint colours, drivers, stable layout and controls';document.body.append(result);
} catch(e){const result=document.createElement('p');result.id='qa';result.textContent='FAIL '+e.stack;document.body.append(result);}
</script>'''
FLOW_SCRIPT=r'''<script>
try{
 if(document.querySelector('#scene').textContent.includes('joined'))throw Error('removed counter visible');
 const p=window.pigenPlayer,colors={both:'#23a766',valid:'#e8bd45',ready:'#429de8',neither:'#30373d'};
 if(p.data.kind!=='flow')throw Error('missing flow');
 for(let n=0;n<p.timeline.length;n++){
  p.seek(n);const a=p.data.rows[p.timeline[n].i];
  if(p.timeline[n].ms!==417)throw Error('variable clock');
  for(const path of document.querySelectorAll('[data-boundary]')){
   const names=p.data.wires[path.dataset.boundary],v=Array.isArray(names[0])?Number(names[0].every(net=>a[net]===1)):a[names[0]],r=a[names[1]],expected=v===1?(r===1?colors.both:colors.valid):(r===1?colors.ready:colors.neither);
   if(path.getAttribute('stroke')!==expected)throw Error('wrong emitted net colour '+names.join(' / '));
  }
 }
 const n=p.timeline.findIndex(s=>p.data.rows[s.i].cycle===8);p.seek(n);
 const expected={join_d:colors.valid,join_i:colors.neither,join_out:colors.neither};
 for(const [name,c]of Object.entries(expected))if(document.querySelector('[data-boundary="'+name+'"]').getAttribute('stroke')!==c)throw Error('cycle 8 join '+name);
 for(const row of p.data.rows){if(JSON.stringify(flowSignals(row).left_in)!==JSON.stringify(flowSignals(row).left_out)||JSON.stringify(flowSignals(row).right_in)!==JSON.stringify(flowSignals(row).right_out))throw Error('transform changed handshake');}
 for(const e of p.data.equations)if(!p.data.emitted_sv.includes('assign '+e.net+' = '+e.expression+';'))throw Error('non-emitted equation');
 if(document.querySelector('#scene').textContent.includes('?:0')||document.querySelector('#scene').textContent.includes('write enable'))throw Error('invented stage or enable indicator');
 if(document.querySelector('#flow-logic').open)throw Error('expanded SV dump');
 if(document.querySelectorAll('#flow-equations .flow-equation').length!==p.data.equations.length)throw Error('hidden equations');
 const result=document.createElement('p');result.id='qa';result.textContent='PASS every flow boundary uses captured SV nets; asymmetric-valid join, exact equations and uniform clocks';document.body.append(result);
}catch(e){const result=document.createElement('p');result.id='qa';result.textContent='FAIL '+e.stack;document.body.append(result);}
</script>'''
def main():
 folder=ROOT/'build/cores/interactive';results=[]
 with tempfile.TemporaryDirectory(prefix='pigen-internal-browser-',dir=ROOT/'build') as temporary:
  for source in sorted(folder.glob('*.html')):
   target=Path(temporary)/source.name;target.write_text(source.read_text()+(FLOW_SCRIPT if source.stem=='transfer-flow' else SCRIPT))
   run=subprocess.run(['/snap/bin/chromium','--headless','--no-sandbox','--disable-gpu','--dump-dom','--virtual-time-budget=1000',target.as_uri()],text=True,capture_output=True,timeout=90)
   hit=re.search(r'<p id="qa">(.*?)</p>',run.stdout,re.S)
   assert hit and hit[1].startswith('PASS'),(source,hit[1] if hit else run.stderr[-1000:])
   results.append(source.stem+': '+hit[1]);print(results[-1],flush=True)
  # Reproduce the reported oversized embedded frame, with both open and closed source.
  import json
  child=(folder/'spi-mode0-half4.html').read_text()
  parent=Path(temporary)/'embedded-height.html'
  harness=r'''<script>
const f=document.querySelector('iframe');
addEventListener('message',e=>{if(e.source===f.contentWindow&&Number.isFinite(e.data?.pigenHeight))f.style.height=e.data.pigenHeight+8+'px';});
const delay=()=>new Promise(r=>setTimeout(r,150));
f.onload=async()=>{try{
 await delay();const main=()=>f.contentDocument.querySelector('main').getBoundingClientRect().height;
 const check=()=>{if(Math.abs(f.getBoundingClientRect().height-main()-8)>2)throw Error('frame retains viewport height: '+f.getBoundingClientRect().height+' vs '+main()+' open='+[...f.contentDocument.querySelectorAll('details')].map(d=>d.open));};
 check();const open=f.getBoundingClientRect().height;
 f.contentDocument.querySelectorAll('details').forEach(d=>d.open=false);await delay();check();
 if(f.getBoundingClientRect().height>=open-100)throw Error('closing source leaves whitespace');
 f.contentDocument.querySelectorAll('details').forEach(d=>d.open=true);await delay();check();
 const p=document.createElement('p');p.id='qa';p.textContent='PASS embedded height shrinks and grows with actual source content';document.body.append(p);
}catch(e){const p=document.createElement('p');p.id='qa';p.textContent='FAIL '+e.stack;document.body.append(p);}};
f.srcdoc=CHILD;
</script>'''
  parent.write_text('<iframe style="width:940px;height:6000px;border:0"></iframe>'+harness.replace('CHILD',json.dumps(child).replace('</','<\\/')))
  run=subprocess.run(['/snap/bin/chromium','--headless','--no-sandbox','--disable-gpu','--dump-dom','--virtual-time-budget=2000',parent.as_uri()],text=True,capture_output=True,timeout=90)
  hit=re.search(r'<p id="qa">(.*?)</p>',run.stdout,re.S)
  assert hit and hit[1].startswith('PASS'),hit[1] if hit else run.stderr[-1000:]
  results.append(hit[1]);print(results[-1],flush=True)
 (folder/'browser-qa.log').write_text('\n'.join(results)+'\n')
if __name__=='__main__':main()
