"""Lay out source data dependencies; never synthesize visual control structures."""
import json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]

def reset_only(event):
 return re.sub(r'[()\s]','',event.get('condition','')) in ('reset','reset==1',"reset==1'b1")

def payload_text(expr):
 """Discard ternary selectors, preserving the actual payload-bearing arms."""
 expr=expr.strip()
 def close_at(text,start):
  pairs={'(':')','[':']','{':'}'};stack=[]
  for i in range(start,len(text)):
   if text[i] in pairs:stack.append(pairs[text[i]])
   elif stack and text[i]==stack[-1]:
    stack.pop()
    if not stack:return i
  return len(text)-1
 while expr.startswith('(') and close_at(expr,0)==len(expr)-1:expr=expr[1:-1].strip()
 depth=0;commas=[];q=None;questions=0;colon=None
 for i,c in enumerate(expr):
  if c in '([{':depth+=1
  elif c in ')]}':depth-=1
  elif depth==0 and c==',':commas.append(i)
  elif depth==0 and c=='?':
   if q is None:q=i
   else:questions+=1
  elif depth==0 and c==':' and q is not None:
   if questions:questions-=1
   elif colon is None:colon=i
 if commas:
  starts=[0]+[i+1 for i in commas];ends=commas+[len(expr)]
  return ', '.join(payload_text(expr[a:b]) for a,b in zip(starts,ends))
 if q is not None and colon is not None:return payload_text(expr[q+1:colon])+', '+payload_text(expr[colon+1:])
 result=[];i=0
 while i<len(expr):
  if expr[i] in '({':
   end=close_at(expr,i);result.append(expr[i]+payload_text(expr[i+1:end])+expr[end]);i=end+1
  else:result.append(expr[i]);i+=1
 return ''.join(result)

def path_label(rhs,source):
 # Only literal projections or simple transforms belong on the drawing.
 text=rhs.strip()
 if '?' in text:return ''
 name=source['name']
 for fn,tag in [('valid','V'),('ready','R'),('accepts','accepts')]:
  if re.search(r'\b'+fn+r'\s*\(\s*'+re.escape(name)+r'\s*\)',text):return ('!' if text.lstrip().startswith('!') else '')+tag
 projection=re.fullmatch(re.escape(name)+r'(\[[^]]+\])',text)
 if projection:return projection[1]
 simple=re.fullmatch(re.escape(name)+r'\s*(<<|>>|[+*\-])\s*(\d+)',text)
 if simple:return simple[1]+' '+simple[2]
 return ''

def build_views(data):
 views=[]
 for mi,module in enumerate(data['modules']):
  native=[n for n in data['nodes'] if n['module']==mi];byname={n['name']:n for n in native};bykey={n['key']:n for n in native}
  ops=[]
  for i,e in enumerate(data['events']):
   if e['module']==mi and not reset_only(e):ops.append(dict(dest=e['dest'],deps=e['deps'],rhs=e['label'].split('<=',1)[1].strip(),value_rhs=e['emitted_value'],event=i,line=e['line'],label=e['label']))
  # Continuous assignments are real native expressions too (not clocked writes).
  source=(ROOT/module['source']).read_text();source=re.sub(r'/\*[\s\S]*?\*/|//[^\n]*','',source)
  for match in re.finditer(r'\bassign\s+(\w+)\s*=\s*([^;]+);',source):
   name,rhs=match.groups()
   if name in byname:ops.append(dict(dest=byname[name]['key'],deps=list(dict.fromkeys(byname[n]['key'] for n in re.findall(r'\b\w+\b',rhs) if n in byname)),rhs=rhs.strip(),event=None,line=0,label=name+' = '+rhs.strip()))
  for op in ops:
   identifiers=set(re.findall(r'\b[A-Za-z_]\w*\b',payload_text(op.get('value_rhs',op['rhs']))))
   op['deps']=[n['key'] for n in native if n.get('rtl_name',n['name']) in identifiers or n.get('rtl_name',n['name'])+'_valid' in identifiers or n.get('rtl_name',n['name'])+'_ready' in identifiers or any(ident.startswith(n.get('rtl_name',n['name'])+'__pigen_') and ident.endswith(('_valid','_ready')) for ident in identifiers)]
  name=module['name']
  groups=[]
  if name=='pigen_uart_rx':
   groups=[('Receive sampling','rx sync_first sync_second shift timer index'),('Frame validation / delivery','shift data parity_error parity_bad stop_index index framing_error overrun timer')]
  elif 'spi_target' in name:
   groups=[('Transmit','tx pending tx_shift tx_bit miso index selected fresh'),('Receive','mosi rx_shift rx index selected'),('Pin sampling / status','cs_n sck mosi sync_first sync_second previous_sck selected fresh miso_enable underrun overrun')]
  elif 'i2c_target' in name:
   groups=[('Byte data','tx pending shift rx remaining reading nack'),('Pin sampling','scl_i sda_i meta pins previous'),('Bus control','scl_low sda_low active reading completed nack setup_left')]
  elif 'axi4_bridge' in name:
   groups=[('AW descriptor','s_axi_aw wa write_id write_size write_burst'),('Write address','write_address write_step write_wrap_mask write_end'),('Write progress','write_left write_bad write_lanes'),('Write data','s_axi_w w_queue beat write_request'),('Write reply','write_reply ack write_status s_axi_b'),('AR descriptor','s_axi_ar ra read_id read_size read_burst'),('Read address','read_address read_step read_wrap_mask read_end'),('Read progress','read_left read_bad'),('Read data / reply','read_request read_reply read_value s_axi_r')]
  elif 'axil_registers' in name:
   groups=[('Write / register bank','s_axi_aw s_axi_w s_axi_b aw wd address data status word0 word1 word2 word3'),('Read / register bank','s_axi_ar s_axi_r read_value')]
  elif 'sdram_controller' in name:
   groups=[('Requests / data','request active pending dq_in result dq_out dq_oe dqm response'),('Command pins','initialized command address bank dqm dq_oe'),('Timing','timer refresh_timer')]
  elif 'i2c_byte_controller' in name:
   groups=[('Byte data','command request tx_shift rx_shift response nack remaining'),('Pin sampling','scl_i sda_i scl_meta scl_sync sda_meta sda_sync'),('Bus timing','scl_low sda_low timer request')]
  elif 'i2s_controller' in name:
   groups=[('Serial data','samples tx_shift serial_out serial_in rx_shift received slot'),('Timing / status','bclk word_select timer slot underrun overrun samples received')]
  else:groups=[(name.removeprefix('pigen_').replace('_',' '),' '.join(byname))]
  used=set()
  for title,names in groups:
   keys={byname[n]['key'] for n in names.split() if n in byname}
   # Include actual RHS operands of every displayed destination, once per view.
   primary=set(keys)
   selected_ops=[op for op in ops if op['dest'] in primary]
   keys.update(src for op in selected_ops for src in op['deps'])
   used.update(keys)
   visible=[n for n in native if n['key'] in keys]
   view=dict(name=title,module=mi,keys=[n['key'] for n in visible],operations=selected_ops,primary=list(primary))
   view.update(layout(visible,view['operations']));views.append(view)
  assert used==set(bykey),(name,'unreachable native declarations',set(bykey)-used)
 return views

def layout(nodes,ops):
 vertices=[dict(key=n['key'],kind='storage',name=n['name'],constants=[]) for n in nodes];known={n['key'] for n in nodes};edges={}
 for op in ops:
  deps=[src for src in op['deps'] if src in known]
  if not deps:
   vertex=next(v for v in vertices if v['key']==op['dest'])
   constant=payload_text(op['rhs'])
   if '?' in constant or len(constant)>24:continue
   if constant not in vertex['constants']:vertex['constants'].append(constant)
   continue
  for src in deps:
   label=path_label(op['rhs'],next(n for n in nodes if n['key']==src))
   key=(src,op['dest'],label)
   if key not in edges:edges[key]=dict(src=src,dest=op['dest'],label=label,events=[],expressions=[],observational=bool(re.search(r'\b(?:valid|ready|accepts|peek)\s*\(',op['rhs'])))
   if op['event'] is not None:edges[key]['events'].append(op['event'])
   if op['label'] not in edges[key]['expressions']:edges[key]['expressions'].append(op['label'])
 connected={k for e in edges.values() for k in (e['src'],e['dest'])}
 isolated=[v for v in vertices if v['key'] not in connected]
 vertices=[v for v in vertices if v['key'] in connected]
 index={v['key']:'n'+str(i) for i,v in enumerate(vertices)}
 quote=lambda s:json.dumps(s,ensure_ascii=False)
 dot=['digraph data { graph [rankdir=LR,nodesep=0.3,ranksep=0.55,pad=0.25,splines=spline]; node [shape=box,fixedsize=true,label="",width=2.25,height=1.4]; edge [fontname="DejaVu Sans Mono",fontsize=13,arrowsize=0.6];']
 for v in vertices:dot.append(index[v['key']]+';')
 ordered=list(edges.values())
 for ei,e in enumerate(ordered):dot.append(index[e['src']]+' -> '+index[e['dest']]+' [id='+quote(str(ei))+',label='+quote(e['label'])+'];')
 dot.append('}')
 result=subprocess.run(['dot','-Tjson'],input='\n'.join(dot),text=True,capture_output=True,check=True)
 graph=json.loads(result.stdout)
 # Spread broad register banks over columns instead of producing a tall fanout.
 ranks={}
 for v,obj in zip(vertices,graph.get('objects',[])):
  if v['kind']=='storage':ranks.setdefault(round(float(obj['pos'].split(',')[0])),[]).append((float(obj['pos'].split(',')[1]),v))
 extra=[]
 for members in ranks.values():
  members.sort(reverse=True,key=lambda item:item[0])
  if len(members)>4:
   for j in range(4,len(members)):extra.append(index[members[j-4][1]['key']]+' -> '+index[members[j][1]['key']]+' [style=invis,weight=100];')
 if extra:
  rerun=subprocess.run(['dot','-Tjson'],input='\n'.join(dot[:-1]+extra+['}']),text=True,capture_output=True,check=True)
  graph=json.loads(rerun.stdout)
 bb=[float(v) for v in graph['bb'].split(',')];height=bb[3]
 for v,obj in zip(vertices,graph.get('objects',[])):
  x,y=map(float,obj['pos'].split(','));v.update(x=x+24,y=height-y+24,w=float(obj['width'])*72,h=float(obj['height'])*72)
 for obj in graph.get('edges',[]):
  if obj.get('style')=='invis':continue
  # Graphviz can reorder edges; identify endpoints using object indices.
  src=vertices[obj['tail']]['key'];dst=vertices[obj['head']]['key'];label=obj.get('label','')
  edge=ordered[int(obj['id'])]
  points=next(d['points'] for d in obj['_draw_'] if d['op']=='b');edge['points']=[[x+24,height-y+24] for x,y in points]
  labels=[];size=13
  for draw in obj.get('_ldraw_',[]):
   if draw['op']=='F':size=draw['size']
   if draw['op']=='T':labels.append(dict(x=draw['pt'][0]+24,y=height-draw['pt'][1]+24,text=draw['text'],size=size,width=draw['width']))
  edge['labels']=labels
 width=max(bb[2]+48,min(5,max(1,len(isolated)))*215+32)
 for i,v in enumerate(isolated):v.update(x=115+(i%5)*215,y=height+110+(i//5)*140,w=180,h=101)
 return dict(graph_nodes=vertices+isolated,graph_edges=ordered,width=width,height=height+48+(140*((len(isolated)+4)//5) if isolated else 0))
