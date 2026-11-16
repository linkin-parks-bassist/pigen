#!/usr/bin/env python3
"""Render every native view and reject overlapping or clipped source content."""
from pathlib import Path
import json,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
SCRIPT=r'''<script>
const select=document.getElementById('datapath');select.value=VIEW;select.dispatchEvent(new Event('change'));
const view=internalViews[VIEW],sourceNodes=new Map(pigenPlayer.data.nodes.map(n=>[n.key,n]));
const n=pigenPlayer.timeline.findIndex(s=>(pigenPlayer.data.rows[s.i].writes||[]).some(([id])=>view.operations.some(op=>op.event===id)&&pigenPlayer.data.events[id].deps.length>0&&!resetOnly(pigenPlayer.data.events[id])));if(n>=0)pigenPlayer.seek(n);phase=.5;render();
try{
 const scene=document.getElementById('scene'),box=scene.getBoundingClientRect(),cards=[...scene.querySelectorAll('.register')];
 if([...scene.querySelectorAll('text')].some(label=>/[?]|valid\(|peek\(/.test(label.textContent)))throw Error('source-code predicate in data drawing');
 if(scene.textContent.includes('state / predicate decode')||scene.querySelector('.driver'))throw Error('invented structures');
 if(cards.length!==view.keys.length)throw Error('missing native declaration');
 const rect=el=>el.getBoundingClientRect(),intersects=(a,b)=>a.left<b.right-1&&a.right>b.left+1&&a.top<b.bottom-1&&a.bottom>b.top+1;
 for(let i=0;i<cards.length;i++){
  const a=rect(cards[i]);if(a.left<box.left-1||a.top<box.top-1||a.right>box.right+1||a.bottom>box.bottom+1)throw Error('card clipped');
  for(let j=i+1;j<cards.length;j++)if(intersects(a,rect(cards[j])))throw Error('cards overlap');
 }
 for(const label of scene.querySelectorAll('.expression-label')){
  const a=rect(label);if(a.left<box.left-2||a.top<box.top-2||a.right>box.right+2||a.bottom>box.bottom+2)throw Error('expression clipped '+label.textContent);
  for(const card of cards)if(intersects(a,rect(card)))throw Error('expression crosses register '+label.textContent);
 }
 for(const op of view.operations){
  const name=sourceNodes.get(op.dest)?.name,module=pigenPlayer.data.modules[view.module].name;
  if(module==='pigen_axil_registers'&&['address','data'].includes(name)&&JSON.stringify(op.deps)!==JSON.stringify(['m0.'+(name==='address'?'aw':'wd')]))throw Error('co-transfer donor mixed');
  if(module==='pigen_spi_target'&&name==='miso'&&(op.deps.includes('m0.fresh')||!op.deps.includes('m0.pending')||!op.deps.includes('m0.tx_bit')))throw Error('selector shown as payload donor');
 }
 for(const op of view.operations)if(op.event!==null&&resetOnly(pigenPlayer.data.events[op.event]))throw Error('reset assignment included');
 for(const card of cards){const r=rect(card.querySelector('text'));if(parseFloat(getComputedStyle(card.querySelector('text')).fontSize)*scene.getScreenCTM().a<10)throw Error('unreadable card');}
 const p=document.createElement('p');p.id='layout-qa';p.textContent='PASS '+view.name;document.body.append(p);
}catch(e){const p=document.createElement('p');p.id='layout-qa';p.textContent='FAIL '+e.stack;document.body.append(p);}
</script>'''
def main():
 folder=ROOT/'build/cores/interactive';screens=ROOT/'build/cores/layout-checks';screens.mkdir(exist_ok=True);results=[]
 with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='native-layout-check-') as tmp:
  for source in sorted(folder.glob('*.html')):
   if source.stem=='transfer-flow':continue
   html=source.read_text();data=json.loads(html.split('<script id="data" type="application/json">',1)[1].split('</script>',1)[0])
   for i,view in enumerate(data['views']):
    target=Path(tmp)/source.name;target.write_text(html+SCRIPT.replace('VIEW',str(i)))
    shot=screens/f'{source.stem}-{i}.png'
    run=subprocess.run(['/snap/bin/chromium','--headless','--no-sandbox','--disable-gpu','--dump-dom','--screenshot='+str(shot),'--window-size=1000,1120','--virtual-time-budget=500',target.as_uri()],capture_output=True,text=True,timeout=90)
    hit=re.search(r'<p id="layout-qa">(.*?)</p>',run.stdout,re.S)
    result=source.stem+' / '+view['name']+': '+(hit[1] if hit else run.stderr[-300:]);results.append(result);print(result,flush=True)
    # Keep rendering the complete suite so visual inspection never stops at one view.
 (screens/'checks.log').write_text('\n'.join(results)+'\n')
 assert all(': PASS ' in line for line in results),'layout failures; see '+str(screens/'checks.log')
if __name__=='__main__':main()
