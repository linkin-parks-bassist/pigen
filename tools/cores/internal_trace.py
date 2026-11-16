"""Observe native source storage and exact emitted write guards without modifying RTL."""
import csv, difflib, hashlib, json, re, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
INSTANCES={'uart-9o2-div7':['sender','receiver'],'i2c-target-addr82-half107':['dut'],'uart-div16':['sender','receiver'],'spi-mode0-half4':['dut'],'spi-target-mode0-half73':['dut'],'i2c-half4':['dut'],'i2s-half4':['dut'],'axis-fifo':['dut'],'axil-seed1':['dut'],'sdram-cas2':['dut'],'axi4-seed1':['dut']}

def clean(s):return re.sub(r'/\*[\s\S]*?\*/|//[^\n]*',lambda m:'\n'*m[0].count('\n'),s)
def matching(s,i):
 depth=0
 for j in range(i,len(s)):
  if s[j]=='(':depth+=1
  elif s[j]==')':
   depth-=1
   if not depth:return j
 raise ValueError('unbalanced expression')
def logical_text(text,nodes):
 for n in nodes:
  if 'rtl_name' not in n:continue
  text=re.sub(r'\b'+re.escape(n['rtl_name'])+r'\b',n['name'],text)
  bus,channel=n['endpoint'],n['channel']
  text=re.sub(r'\b'+re.escape(bus)+r'\.'+channel+r'(?:\.[A-Za-z_]\w*)?',n['name'],text)
  if channel=='beat':text=re.sub(r'\b'+re.escape(bus)+r'(?:\.[A-Za-z_]\w*)?',n['name'],text)
 return re.sub(r"([,{]\s*)[A-Za-z_]\w*\s*:",r'\1',text)

def source_model(path):
 source=path.read_text();s=clean(source);nodes=[]
 pattern=r'(?:(input|output)\s+)?((?:bit|int|uint)(?:\[[^\]]+\])?|wire(?:\s*\[[^\]]+\])?)\s*(reg|buf|wire|fifo\[\d+\]|skid|port)?\s+([A-Za-z_]\w*)\s*(?=[,;)])'
 for m in re.finditer(pattern,s):
  direction,typ,storage,name=m.groups()
  if name in ('clk','reset'):continue
  nodes.append(dict(name=name,kind=storage or ('pin' if direction else 'wire'),direction=direction,datatype=typ,line=s.count('\n',0,m.start())+1))
 for m in re.finditer(r'(input|output)\s+(uint|bit)\[(\d+)\]\s+(axi4_stream|axi4_lite|axi4)\[([^]]*)\]\s*(fifo\[\d+\]|buf|skid)?\s+(\w+)',s):
  direction,typ,width,family,options,storage,bus=m.groups();width=int(width)
  channels=('beat',) if family=='axi4_stream' else ('aw','w','b','ar','r')
  for channel in channels:
   reverse=channel in ('b','r');d=('output' if direction=='input' else 'input') if reverse else direction
   nodes.append(dict(name=bus+'_'+channel,rtl_name=bus+'__pigen_channel_'+channel,endpoint=bus,channel=channel,kind=storage or 'buf',direction=d,datatype='bit',line=s.count('\n',0,m.start())+1))
 s=logical_text(s,nodes)
 names={n['name'] for n in nodes};ops=[]
 for m in re.finditer('<=',s):
  left=re.search(r'(\{[^{}]+\}|[A-Za-z_]\w*(?:\[[^\]]+\])?)\s*$',s[:m.start()])
  if not left:continue
  dest=[n for n in re.findall(r'\b[A-Za-z_]\w*\b',left[1]) if n in names]
  if not dest:continue
  end=s.find(';',m.end());when=s.rfind('when',0,m.start())
  if when>=0:
   op=s.find('(',when)
   if op>=0 and op<m.start():
    close=matching(s,op)
    if close>m.start():end=close
  rhs=s[m.end():end].strip();deps=list(dict.fromkeys(n for n in re.findall(r'\b[A-Za-z_]\w*\b',rhs) if n in names))
  ops.append(dict(dest=dest,rhs=' '.join(rhs.split()),deps=deps,line=s.count('\n',0,m.start())+1,label=left[1].strip()+' <= '+' '.join(rhs.split())))
 fsms=[]
 for part in re.split(r'\bfsm\b',s)[1:]:fsms.append(re.findall(r'\bstate\s+(\w+)\s*:',part))
 return source,nodes,ops,fsms

def sv_names(s):
 return set(re.findall(r'\b(?:input\s+|output\s+)?(?:logic|bit)(?:\s+signed)?(?:\s*\[[^\]]+\])?\s+(\w+)\s*(?:[,;]|\n\s*\))',s))|set(re.findall(r'\blocalparam\s+(?:integer|(?:bit|logic)(?:\s+signed)?(?:\s*\[[^\]]+\])?)\s+(\w+)\s*=',s))
def qualify(s,names,scope):return re.sub(r'\b[A-Za-z_]\w*\b',lambda m:scope+'.'+m[0] if m[0] in names else m[0],s)
def branches(expr,guard='1'):
 expr=expr.strip()
 while expr.startswith('(') and matching(expr,0)==len(expr)-1:expr=expr[1:-1].strip()
 depth=0;q=None;nested=0
 for i,c in enumerate(expr):
  if c in '([{':depth+=1
  elif c in ')]}':depth-=1
  elif not depth and c=='?':
   if q is None:q=i
   else:nested+=1
  elif not depth and c==':' and q is not None:
   if nested:nested-=1
   else:
    condition=expr[:q]
    return branches(expr[q+1:i],f'({guard}) && ({condition})')+branches(expr[i+1:],f'({guard}) && !({condition})')
 return [(guard,expr)]
def canonical(expr,params):
 expr=re.sub(r"(?:signed|unsigned|\d+)'(?=\()",'',expr)
 expr=re.sub(r"(?:\d+)?'[sS]?([bBoOdDhH])([0-9a-fA-F_]+)",lambda m:str(int(m[2].replace('_',''),{'b':2,'o':8,'d':10,'h':16}[m[1].lower()])),expr)
 expr=re.sub(r'\b[A-Za-z_]\w*\b',lambda m:params.get(m[0],m[0]),expr)
 return re.sub(r'[()\s]','',expr)
def pick(ops,dest,rhs,names,params):
 deps=set(re.findall(r'\b[A-Za-z_]\w*\b',rhs))&names
 candidates=[o for o in ops if dest in o['dest']]
 if not candidates:return dict(dest=[dest],deps=list(deps),rhs=rhs,line=0,label=dest+' <= '+rhs)
 def score(o):
  same=set(o['deps'])==deps
  a=canonical(o['rhs'],params);b=canonical(rhs,params)
  def const(x):
   try:return int(eval(x,{'__builtins__':{}},{}))
   except Exception:return None
  ca,cb=const(a),const(b)
  return (a==b,ca is not None and ca==cb,same,len(set(o['deps'])&deps)-len(set(o['deps'])^deps),difflib.SequenceMatcher(None,a,b).ratio())
 return max(candidates,key=score)

def control_signals(identifiers,nodes,states,mi):
 result=[]
 for node in nodes:
  name=node.get('rtl_name',node['name']);key=node['key']
  if name in identifiers:result.append(key)
  for ident in identifiers:
   if not ident.startswith(name+'_'):continue
   suffix='.capacity' if ident.endswith('__pigen_in_ready') else '.valid' if ident.endswith('_valid') else '.ready' if ident.endswith('_ready') else None
   if suffix and key+suffix not in result:result.append(key+suffix)
 result += [st['key'] for st in states if st['key'].split('.',1)[1] in identifiers]
 if 'reset' in identifiers:result.append(f'm{mi}.reset')
 return result

def capture(case,profile,out,windows=None):
 """Run an observational extra SV root against the same checked testbench/core files."""
 manifest=json.loads((out/'results.json').read_text());entry=next(c for c in manifest['cases'] if c['name']==case);top=entry['top']
 directory=out/case;work=directory/'internal';work.mkdir(exist_ok=True)
 modules=[];all_nodes=[];events=[];registers=[];generated=sorted(directory.glob('core*.sv'))
 for index,(src,instance,sv) in enumerate(zip(profile[1],INSTANCES[case],generated)):
  path=ROOT/'cores'/src;source,nodes,ops,fsms=source_model(path);code=sv.read_text();names=sv_names(code);scope=top+'.'+instance;native={n['name'] for n in nodes};params=dict(re.findall(r'localparam\b[^;=]*?\b(\w+)\s*=\s*(\d+);',code))
  state_vars=re.findall(r'\b(?:logic|bit)(?:\s*\[[^\]]+\])?\s+(__pigen_fsm_\d+)\s*;',code)
  assert len(fsms)==len(state_vars),(case,fsms,state_vars)
  module=dict(transitions=[],name=re.search(r'\bmodule\s+(\w+)',clean(source))[1],source='cores/'+src,instance=instance,parameters=params,states=[])
  for var,labels in zip(state_vars,fsms):
   key=f'm{index}.{var}';module['states'].append(dict(key=key,labels=labels));registers.append((key,scope+'.'+var));
  for n in nodes:
   n=dict(n,key=f'm{index}.'+n['name'],module=index);base=n.get('rtl_name',n['name']);n['width']=int(re.search(r'\[(\d+)\]',n['datatype'])[1]) if re.search(r'\[(\d+)\]',n['datatype']) else 1;n['width']=abs(int(re.search(r'\[(\d+):(\d+)\]',n['datatype'])[1])-int(re.search(r'\[(\d+):(\d+)\]',n['datatype'])[2]))+1 if re.search(r'\[(\d+):(\d+)\]',n['datatype']) else n['width'];n['signed']=n['datatype'].startswith('int');decl=re.search(r'\b(?:bit|logic)(?:\s+signed)?(?:\s*\[(\d+):(\d+)\])?\s+'+re.escape(base)+r'\b',code);assert decl,(case,base,'missing emitted declaration');n['width']=abs(int(decl[1])-int(decl[2]))+1 if decl[1] else 1;n['width_expr']='$bits('+scope+'.'+base+')';all_nodes.append(n);registers.append((n['key'],scope+'.'+base))
   if n['kind'] in ('buf','skid','port') or n['kind'].startswith('fifo'):
    valid=base+'_valid' if n['direction'] else base+'__pigen_out_valid';ready=base+'_ready' if n['direction']=='input' else base+'__pigen_out_ready' if not n['direction'] else base+'_ready'
    # Internal buf output signals use the public signal name plus generated net names.
    if not n['direction']:valid=base+'__pigen_valid';ready=base+'__pigen_ready'
    # Consult primitive connections, not guessed naming policy.
    primitive=re.search(r'\b'+re.escape(base)+r'__pigen_(?:buf|fifo|skid|port)\s*\(([^;]+)\);',code)
    if primitive:
     conns=dict(re.findall(r'\.(\w+)\(([^)]+)\)',primitive[1]));valid=conns['out_valid'];ready=conns['out_ready']
    if valid in names:registers.append((n['key']+'.valid',qualify(valid,names,scope)))
    if ready in names:registers.append((n['key']+'.ready',qualify(ready,names,scope)))
    if base+'__pigen_in_ready' in names:registers.append((n['key']+'.capacity',scope+'.'+base+'__pigen_in_ready'))
  for line in code.splitlines():
   m=re.match(r'\s*if \((.*)\) (\w+(?:\[[^\]]+\])?) <= (.*);\s*$',line)
   if not m:continue
   guard,lhs,rhs=m.groups();dest=lhs.split('[')[0]
   if dest.startswith('__pigen_fsm_'):
    module['transitions'].append(dict(dest=f'm{index}.'+dest,condition=guard,value=rhs))
    continue
   if dest not in native:raise ValueError((case,'unrepresented register',dest))
   op=pick(ops,dest,logical_text(rhs,nodes),native,params);projection=re.search(r'\[(\d+)(?::(\d+))?\]',lhs);lo=min(int(projection[1]),int(projection[2] or projection[1])) if projection else 0;hi=max(int(projection[1]),int(projection[2] or projection[1])) if projection else next(n['width'] for n in all_nodes if n['module']==index and n['name']==dest)-1;events.append(dict(lo=lo,hi=hi,module=index,dest=f'm{index}.'+dest,deps=[f'm{index}.'+n for n in op['deps']],label=op['label'],line=op['line'],guard=qualify(guard,names,scope),rhs=qualify(rhs,names,scope),lhs=scope+'.'+lhs))
  for n in nodes:
   logical=n['name'];base=n.get('rtl_name',logical);prefix=base+'__pigen_'
   if n['direction']=='input' or n['kind'] not in ('buf','fifo[4]','skid','port'):continue
   assignments=dict(re.findall(r'^\s*assign\s+(\w+)\s*=\s*(.*);\s*$',code,re.M))
   packet=assignments.get(prefix+'packet_in')
   if packet is None:raise ValueError((case,'no packet input',base))
   for guard,rhs in branches(packet):
    if rhs.strip()=='0':continue
    op=pick(ops,logical,logical_text(rhs,nodes),native,params)
    g=f'({guard}) && {prefix}in_valid && {prefix}in_ready'
    events.append(dict(lo=0,hi=next(n['width'] for n in all_nodes if n['module']==index and n['name']==logical)-1,module=index,dest=f'm{index}.'+logical,deps=[f'm{index}.'+n for n in op['deps']],label=op['label'],line=op['line'],guard=qualify(g,names,scope),rhs=qualify(rhs,names,scope),lhs=scope+'.'+base))
  modules.append(module)
 # Actual write guards/results are evaluated by Icarus, not a JavaScript SV approximation.
 observer=['module pigen_internal_observer;','integer f,c=0;','initial begin f=$fopen("internals.csv","w");']
 columns=['cycle','reset']+[k for k,_ in registers]+sum(([f'e{i}',f'v{i}',f'o{i}'] for i in range(len(events))),[])
 observer.append('$fwrite(f,"'+','.join(columns)+'\\n"); end')
 for i,e in enumerate(events):
  e['offer_guard']=re.sub(r'\b[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*__pigen_in_ready\b',"1'b1",e['guard'])
  observer.append(f"wire [$bits({e['lhs']})-1:0] value{i} = {e['rhs']};\nwire enable{i} = {e['guard']};\nwire offer{i} = {e['offer_guard']};")
 window_guard=' || '.join(f'(c>={lo} && c<={hi})' for lo,hi in (windows or [(1,5000)]))
 observer.append(f'always @(posedge {top}.clk) begin c=c+1; if({window_guard}) $fwrite(f,"%0d,%0b'+',%h'*len(registers)+',%b,%h,%b'*len(events)+'\\n",c,'+top+'.reset,'+','.join(v for _,v in registers)+(',' if events else '')+','.join(f'enable{i},value{i},offer{i}' for i in range(len(events)))+'); end\nendmodule')
 probe=work/'observer.sv';probe.write_text('\n'.join(observer))
 adapters=[ROOT/p for p in entry['sha256'] if p.startswith('cores/') and p.endswith('.sv')]+[ROOT/p for p in entry['sha256'] if p.startswith('tests/cores/') and p.endswith('_model.sv')]
 tb=ROOT/'tests/cores'/f'{top}.sv';command=['iverilog','-g2012','-s',top,'-s','pigen_internal_observer']
 for key,val in entry['parameters'].items():command+=['-P',f'{top}.{key}={val}']
 command+=['-o',str(work/'sim'),*map(str,generated),*map(str,adapters),str(ROOT/'rtl/pigen_primitives.sv'),str(tb),str(probe)]
 log=[]
 for cmd in (command,['vvp',str(work/'sim')]):
  result=subprocess.run(cmd,cwd=work,text=True,capture_output=True,timeout=120);log.append('$ '+' '.join(cmd)+'\n'+result.stdout+result.stderr)
  if result.returncode:raise RuntimeError('\n'.join(log))
 (work/'run.log').write_text('\n'.join(log));assert 'PASS ' in log[-1]
 result=[]
 for row in csv.DictReader((work/'internals.csv').open()):
  converted={'cycle':int(row['cycle']),'reset':int(row['reset']) if row['reset'] in ('0','1') else None}
  for k,_ in registers:converted[k]=None if any(v in row[k].lower() for v in ('x','z')) else '0x'+row[k]
  converted['writes']=[[i,None if any(v in row[f'v{i}'].lower() for v in ('x','z')) else '0x'+row[f'v{i}']] for i in range(len(events)) if row[f'e{i}']=='1']
  converted['offers']=[i for i in range(len(events)) if row[f'o{i}']=='1']
  result.append(converted)
 checked=0;node_map={n['key']:n for n in all_nodes}
 for a,b in zip(result,result[1:]):
  if b['cycle']!=a['cycle']+1:continue
  expected={}
  for i,value in a['writes']:
   e=events[i];key=e['dest']
   if node_map[key]['kind']!='reg' or value is None:continue
   old=expected.get(key,a[key])
   if old is None:continue
   mask=((1<<(e['hi']-e['lo']+1))-1)<<e['lo']
   expected[key]=hex((int(old,16)&~mask)|((int(value,16)<<e['lo'])&mask))
  for key,value in expected.items():
   assert b[key] is not None and int(value,16)==int(b[key],16),(case,a['cycle'],key,value,b[key])
   checked+=1
 (work/'commit-check.log').write_text(f'PASS {checked} register commits, including projected writes and source ordering\n')
 for e in events:
  scope=top+'.'+INSTANCES[case][e['module']]+'.'
  e['condition']=e['guard'].replace(scope,'')
  e['emitted_value']=e['rhs'].replace(scope,'')
  module_nodes=[n for n in all_nodes if n['module']==e['module']]
  identifiers=set(re.findall(r'\b[A-Za-z_]\w*\b',e['condition']))
  e['controls']=control_signals(identifiers,module_nodes,modules[e['module']]['states'],e['module'])
  for k in ('guard','rhs','lhs','offer_guard'):e.pop(k)
 data=dict(kind='internal',configured_sources=[str((directory/f'source{i}.pigen').relative_to(out)) for i in range(len(modules))],pins=[n['key'] for n in all_nodes if n['width']==1 and n['direction']],modules=modules,nodes=all_nodes,events=events,rows=result,trace=case+'/internal/internals.csv',trace_sha256=hashlib.sha256((work/'internals.csv').read_bytes()).hexdigest())
 (work/'model.json').write_text(json.dumps(data,separators=(',',':')))
 return data
