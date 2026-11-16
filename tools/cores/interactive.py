#!/usr/bin/env python3
"""Package self-contained browser players from verified RTL traces."""
import re
import csv, hashlib, html, json
from pathlib import Path
from transfer_trace import validate, WIRES
from internal_trace import capture
from native_layout import build_views
ROOT=Path(__file__).resolve().parents[2]
# Each lane is an actual public ready/valid interface, never guessed storage internals.
def lane(name,src,dst,v,r,*fields):return dict(name=name,src=src,dst=dst,v=v,r=r,fields=list(fields))
PROFILES={
'uart-9o2-div7':('UART / 9 data bits, odd parity, two stops',['uart/tx.pigen','uart/rx.pigen'],['tx','rx'],[
 lane('TX word','application','UART transmitter','tx_data_valid','tx_data_ready','tx_data'),
 lane('RX word','UART receiver','application','rx_data_valid','rx_data_ready','rx_data')]),
'uart-div16':('UART',['uart/tx.pigen','uart/rx.pigen'],['tx','rx'],[
 lane('TX byte','application','UART transmitter','tx_data_valid','tx_data_ready','tx_data'),
 lane('RX byte','UART receiver','application','rx_data_valid','rx_data_ready','rx_data')]),
'spi-mode0-half4':('SPI controller',['spi/controller.pigen'],['cs_n','sck','mosi','miso'],[
 lane('command','application','SPI controller','command_valid','command_ready','command'),lane('response','SPI controller','application','response_valid','response_ready','response')]),
'spi-target-mode0-half73':('SPI target',['spi/target.pigen'],['cs_n','sck','mosi','miso','miso_enable'],[
 lane('TX byte','application','SPI target','tx_valid','tx_ready','tx'),lane('RX byte','SPI target','application','rx_valid','rx_ready','rx')]),
'i2c-target-addr82-half107':('I²C target',['i2c/target.pigen'],['scl_i','sda_i','scl_low'],[
 lane('TX byte','application','I²C target','tx_valid','tx_ready','tx'),lane('RX byte','I²C target','application','rx_valid','rx_ready','rx')]),
'i2c-half4':('I²C',['i2c/byte_controller.pigen'],['scl_i','sda_i','slave_clock_low'],[
 lane('command','application','I²C controller','command_valid','command_ready','command'),lane('response','I²C controller','application','response_valid','response_ready','response')]),
'i2s-half4':('I²S',['i2s/controller.pigen'],['bclk','word_select','serial_out','serial_in'],[
 lane('stereo TX','application','I²S controller','samples_valid','samples_ready','samples'),lane('stereo RX','I²S controller','application','received_valid','received_ready','received')]),
'axis-fifo':('AXI-Stream',['axis/fifo.pigen'],[],[
 lane('input beat','producer','FIFO intrinsic','s_axis_tvalid','s_axis_tready','s_axis_tdata','s_axis_tkeep','s_axis_tlast'),
 lane('output beat','FIFO intrinsic','consumer','m_axis_tvalid','m_axis_tready','m_axis_tdata','m_axis_tkeep','m_axis_tlast')]),
'axil-seed1':('AXI4-Lite',['axil/registers.pigen'],[],[]),
'axi4-seed1':('AXI4',['axi4/bridge.pigen'],[],[]),
'sdram-cas2':('SDRAM',['sdram/controller.pigen'],['initialized','dq_oe','model_drive'],[
 lane('request','application','SDRAM controller','request_valid','request_ready','request'),lane('response','SDRAM controller','application','response_valid','response_ready','response')])}
for case in ('axil-seed1','axi4-seed1'):
 title,src,pins,lanes=PROFILES[case]
 for ch,fields in [('aw',['addr']),('w',['data','strb']),('b',['resp']),('ar',['addr']),('r',['data','resp'])]:
  if case=='axi4-seed1':fields+= ['id'] if ch in ('aw','ar','b','r') else ['last']
  if case=='axi4-seed1' and ch in ('aw','ar'):fields+=['len','size','burst']
  if case=='axi4-seed1' and ch=='r':fields+=['last']
  lanes.append(lane(ch.upper(),'application' if ch in ('aw','w','ar') else title,title if ch in ('aw','w','ar') else 'application',f's_axi_{ch}valid',f's_axi_{ch}ready',*[f's_axi_{ch}{f}' for f in fields]))
 if case=='axi4-seed1':
  for ch in ('write_request','write_reply','read_request','read_reply'):
   lanes.append(lane(ch.replace('_',' '),title if ch.endswith('request') else 'backend','backend' if ch.endswith('request') else title,ch+'_valid',ch+'_ready',ch))

def vcd_rows(path,needed,limit=5000):
 """Capture values immediately BEFORE every positive clk timestamp, before NBA writes."""
 ids={};widths={};values={};scope=[];rows=[];clock=None
 with path.open() as f:
  for line in f:
   s=line.split()
   if not s:continue
   if s[0]=='$scope':scope.append(s[2])
   elif s[0]=='$upscope':scope.pop()
   elif s[0]=='$var' and len(scope)==1:
    if s[4] in needed or s[4]=='clk':ids[s[4]]=s[3];widths[s[3]]=int(s[2])
   elif s[0]=='$enddefinitions':break
  missing=needed-set(ids);assert not missing,(path,missing)
  clock=ids['clk'];before={};changed={};cycle=0
  def flush():
   nonlocal cycle,before,changed
   if changed.get(clock)==1 and values.get(clock)==0:
    cycle+=1;rows.append(dict(cycle=cycle,**{k:values.get(v) for k,v in ids.items() if k!='clk'}))
   values.update(changed);changed={}
  for line in f:
   line=line.strip()
   if not line:continue
   if line.startswith('#'):
    flush()
    if len(rows)>=limit:break
   elif line[0] in '01xz':
    ident=line[1:]
    if ident in widths:changed[ident]=int(line[0]) if line[0] in '01' else None
   elif line[0] in 'bB':
    bits,ident=line[1:].split()
    if ident in widths:changed[ident]=None if any(c in bits.lower() for c in 'xz') else '0x'+format(int(bits,2),'x')
  else:flush()
 assert rows,path
 return rows

def main(out=ROOT/'build/cores'):
 dest=out/'interactive';dest.mkdir(exist_ok=True)
 here=Path(__file__).parent
 css=(here/'interactive.css').read_text();js=(here/'internal.js').read_text()+'\n'+(here/'interactive.js').read_text()+"\nif(parent!==window){const publishHeight=()=>parent.postMessage({pigenHeight:Math.ceil(document.querySelector('main').getBoundingClientRect().height)},'*');new ResizeObserver(publishHeight).observe(document.querySelector('main'));document.addEventListener('toggle',publishHeight,true);addEventListener('resize',publishHeight);publishHeight();}"
 def write(case,title,source,data):
  panels=''.join('<details open><summary>'+html.escape(str(p))+(' · tested configuration' if data.get('configured_sources') else '')+'</summary><pre><code>'+''.join('<span class="source-line" data-module="'+str(module)+'" data-line="'+str(i+1)+'">'+html.escape(line)+'</span>\n' for i,line in enumerate(((out/data['configured_sources'][module]).read_text() if data.get('configured_sources') else (ROOT/p).read_text()).splitlines()))+'</code></pre></details>' for module,p in enumerate(source))
  payload=json.dumps(data,separators=(',',':')).replace('</','<\\/')
  page='<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>'+html.escape(title)+' / Pigen</title><style>'+css+'</style><main><header><h1>'+html.escape(title)+'</h1><p id="description"></p></header><div class="controls"><button id="play">Play</button><button id="back" aria-label="Previous edge">←</button><button id="step">Step</button><button id="next">Next transfer</button><button id="restart">Restart</button><label>Speed <select id="speed"><option value="0.5">½×</option><option value="1" selected>1×</option><option value="2">2×</option></select></label></div><div class="clock-status"><span id="position" aria-live="off"></span><span id="activity"></span></div><input id="scrub" type="range" min="0" value="0" aria-label="Trace position"><div class="legend"><span class="dark">Neither</span><span class="blue">Ready</span><span class="yellow">Valid</span><span class="green">Valid + ready</span></div><svg id="scene" role="img" aria-label="Data transfers between fixed endpoints"></svg><div id="pins"></div><p class="caption" id="caption"></p>'+panels+'</main><script id="data" type="application/json">'+payload+'</script><script>'+js+'</script></html>'
  (dest/(case+'.html')).write_text(page)
  (dest/(case+'.json')).write_text(json.dumps({k:v for k,v in data.items() if k not in ('rows',)},indent=2))
 for case,(title,src,pins,lanes) in PROFILES.items():
  needed={'reset',*pins}
  for l in lanes:needed.update([l['v'],l['r'],*l['fields']])
  trace=out/case/'wave.vcd';rows=vcd_rows(trace,needed,500000 if case.startswith('i2c-target') else 120000 if case.startswith('uart-') else 5000)
  if case.startswith('uart-'):
   first_rx=next(i for i,r in enumerate(rows) if r['reset']==0 and r['rx_data_valid']==1 and r['rx_data_ready']==1)
   selected=set(range(min(1500,len(rows))))|set(range(max(0,first_rx-200),min(len(rows),first_rx+2000)))|set(range(max(0,len(rows)-1200),len(rows)))
   rows=[r for i,r in enumerate(rows) if i in selected]
  if case.startswith('i2c-target'):
   first_rx=next(i for i,r in enumerate(rows) if r['reset']==0 and r['rx_valid']==1 and r['rx_ready']==1)
   first_tx=next(i for i,r in enumerate(rows) if r['reset']==0 and r['tx_valid']==1 and r['tx_ready']==1)
   selected=set(range(min(800,len(rows))))|set(range(max(0,first_rx-500),min(len(rows),first_rx+3000)))|set(range(max(0,first_tx-500),min(len(rows),first_tx+3000)))|set(range(max(0,len(rows)-6000),len(rows)))
   rows=[r for i,r in enumerate(rows) if i in selected]
  windows=[]
  for row in rows:
   if not windows or row['cycle']!=windows[-1][1]+1:windows.append([row['cycle'],row['cycle']])
   else:windows[-1][1]=row['cycle']
  internal=capture(case,PROFILES[case],out,windows)
  internal['views']=build_views(internal)
  write(case,title,[Path('cores')/p for p in src],internal)
 trace=out/'transfer-flow/slots.csv';rows=[{k:int(v) if v.lower() not in ('x','z') else None for k,v in r.items()} for r in csv.DictReader(trace.open())]
 for a,b in zip(rows,rows[1:]):
  if a['reset']==0 and b['reset']==0:validate(a,b)
 emitted=(out/'transfer-flow/core.sv').read_text()
 equations=[dict(net=m[1],expression=m[2]) for m in re.finditer(r'^\s*assign\s+(\w+)\s*=\s*(.*);\s*$',emitted,re.M) if m[1] in {n for pair in WIRES.values() for net in pair for n in (net if isinstance(net,tuple) else (net,))} or m[1].endswith('__pigen_packet_in')]
 write('transfer-flow','Fork, compute, join',[Path('examples/transfer_flow.pigen')],dict(kind='flow',rows=rows,wires=WIRES,equations=equations,emitted_sv=emitted,trace='transfer-flow/slots.csv',trace_sha256=hashlib.sha256(trace.read_bytes()).hexdigest()))
 print(f'Interactive players: {dest} (12 examples)')
if __name__=='__main__':main()
