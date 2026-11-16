#!/usr/bin/env python3
"""Render explicit Pigen buf transfers from the fork/join example's real trace."""
import argparse,csv,hashlib,json,subprocess
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch,Circle

W,H,FPS=1920,1080,30
PAPER,WHITE,INK='#f5f3ed','#ffffff','#272624'
DARK,BLUE,YELLOW,GREEN='#20252a','#429de8','#e8bd45','#23a766'
WIRE_WIDTH=3.0
MONO='DejaVu Sans Mono'
NODES={'source':(160,535),'a':(530,335),'b':(530,735),'d':(930,335),'i':(930,735),'out':(1370,535),'receiver':(1760,535)}
NAMES={'source':'N','a':'left : buf','b':'right : buf','d':'doubled : buf','i':'incremented : buf','out':'egress : buf','receiver':'received'}
WIDTH,HEIGHT=200,180


def smooth(t):
    t=float(np.clip(t,0,1));return t*t*t*(10+t*(-15+6*t))
def blend(a,b,t):
    from matplotlib.colors import to_rgb,to_hex
    return to_hex(np.array(to_rgb(a))*(1-t)+np.array(to_rgb(b))*t)
def color(v,r):return GREEN if v and r else YELLOW if v else BLUE if r else DARK

def rounded(points):
    points=np.array(points,float);out=[points[0]]
    for j in range(1,len(points)-1):
        p,q,r=points[j-1:j+2];before=q+(p-q)*min(24/np.linalg.norm(p-q),.4);after=q+(r-q)*min(24/np.linalg.norm(r-q),.4)
        out.append(before)
        out.extend([(1-t)**2*before+2*(1-t)*t*q+t*t*after for t in np.linspace(0,1,10)])
    out.append(points[-1]);return np.array(out)
P={
 'input':rounded([(160,535),(310,535)]),
 'fork_a':rounded([(310,535),(310,335),(530,335)]),
 'fork_b':rounded([(310,535),(310,735),(530,735)]),
 'left_in':rounded([(530,335),(730,335)]),
 'left_out':rounded([(730,335),(930,335)]),
 'right_in':rounded([(530,735),(730,735)]),
 'right_out':rounded([(730,735),(930,735)]),
 'join_d':rounded([(930,335),(1150,335),(1150,535)]),
 'join_i':rounded([(930,735),(1150,735),(1150,535)]),
 'join_out':rounded([(1150,535),(1370,535)]),
 'output':rounded([(1370,535),(1760,535)])}

def along(path,t):
    lengths=np.linalg.norm(np.diff(path,axis=0),axis=1);distance=t*sum(lengths)
    for a,b,length in zip(path,path[1:],lengths):
        if distance<=length:return a+(b-a)*distance/max(length,1e-9)
        distance-=length
    return path[-1]

from transfer_trace import signals,validate


def text(ax,x,y,s,size=16,color=INK,ha='center',alpha=1,z=6):
    ax.text(x,y,s,fontsize=size,color=color,ha=ha,va='center',fontfamily=MONO,alpha=alpha,zorder=z)
def rect(ax,x,y,w,h,fc=WHITE,ec='#d2cdc2',z=3,alpha=1):
    ax.add_patch(FancyBboxPatch((x,y),w,h,boxstyle='round,pad=0,rounding_size=4',fc=fc,ec=ec,lw=1.2,zorder=z,alpha=alpha))
def token(ax,point,value,alpha=1):
    if value is None or alpha<=0:return
    x,y=point;rect(ax,x-57,y-32,114,64,'#eee7d8','#b9ad93',10,alpha);text(ax,x,y,str(value),24,alpha=alpha,z=11)

def scene(ax,a,b,t,before,after,edge=True,head='',note=''):
    ax.clear();ax.set_xlim(0,W);ax.set_ylim(H,0);ax.axis('off')
    move=smooth((t-.16)/.68) if edge else 0
    settle=smooth((t-.84)/.15) if edge else smooth(t)
    leaving=edge and t>.16;arrived=edge and t>=1
    ev={k:a[k] if edge else 0 for k in ['fork_fire','left_fire','right_fire','join_fire','drain_fire']}
    text(ax,60,65,'Explicit Pigen transfers',30,ha='left');text(ax,60,115,'Fork N → double / increment → join as 3N + 1',18,'#736f67',ha='left')
    text(ax,1860,65,head,20,ha='right');text(ax,1860,115,'transfer' if leaving and not arrived else 'observe' if not arrived else 'settle',15,'#736f67',ha='right')
    ax.plot([60,1860],[160,160],color='#d5d0c5',lw=1);text(ax,60,195,note,15,GREEN if any(ev.values()) else '#736f67',ha='left')
    sa,sb=signals(a),signals(b)
    for name,path in P.items():
        ax.plot(path[:,0],path[:,1],color=color(*(sb[name] if arrived else sa[name])),lw=WIRE_WIDTH,
                solid_capstyle='round',solid_joinstyle='round',zorder=1)
    for key,(x,y) in NODES.items():
        rect(ax,x-WIDTH/2,y-HEIGHT/2,WIDTH,HEIGHT)
        text(ax,x,y-HEIGHT/2+24,NAMES[key],14,'#736f67')
    for x,y,name in [(310,535,'fork'),(730,335,'×2'),(730,735,'+1'),(1150,535,'+')]:
        ax.add_patch(Circle((x,y),22,fc=PAPER,ec='#9c9588',lw=1.2,zorder=3));text(ax,x,y,name,13)
    enabled=a['right_enable'] if settle<.5 else b['right_enable']
    text(ax,730,790,'step_right = '+str(enabled),12,'#736f67')
    # Static values are hidden as their accepted token leaves the slot.
    departures={'a':'left_fire','b':'right_fire','d':'join_fire','i':'join_fire','out':'drain_fire'}
    incoming={'a':('fork_fire',a['in_data']),'b':('fork_fire',a['in_data']),
              'd':('left_fire',2*a['a_data'] if a['a_valid'] else 0),
              'i':('right_fire',a['b_data']+1 if a['b_valid'] else 0),
              'out':('join_fire',a['d_data']+a['i_data'] if a['d_valid'] and a['i_valid'] else 0)}
    snapshot=b if arrived else a
    names={'a':'left','b':'right','d':'doubled','i':'incremented','out':'egress'}
    for key in departures:
        x,y=NODES[key];name=names[key];ready=snapshot['egress_ready'] if key=='out' else snapshot[name+'__pigen_out_ready']
        text(ax,x,y,str(snapshot[key+'_data']),24);text(ax,x,y+58,'V='+str(snapshot[key+'_valid'])+' / R='+str(ready),12)
    x,y=NODES['source'];text(ax,x,y,str(snapshot['ingress']),24);text(ax,x,y+58,'V='+str(snapshot['ingress_valid'])+' / R='+str(snapshot['ingress_ready']),12)
    received=after if arrived else before
    if received[1] is not None:token(ax,NODES['receiver'],received[1])
    text(ax,1760,590,f'{received[0]} accepted',12,'#736f67')

    if leaving and not arrived:
        if ev['fork_fire']:
            if move<.25:token(ax,along(P['input'],move/.25),a['in_data'])
            else:
                token(ax,along(rounded([(310,535),(310,335),(530,335)]),(move-.25)/.75),a['in_data']);token(ax,along(rounded([(310,535),(310,735),(530,735)]),(move-.25)/.75),a['in_data'])
        for event,path,key,value in [('left_fire','left','a',2*a['a_data'] if a['a_valid'] else 0),('right_fire','right','b',a['b_data']+1 if a['b_valid'] else 0)]:
            if ev[event]:token(ax,along(rounded([(530,335),(930,335)]) if path=='left' else rounded([(530,735),(930,735)]) if path=='right' else P[path],move),a[key+'_data'] if move<.5 else value)
        if ev['join_fire']:
            if move<.65:
                token(ax,along(P['join_d'],move/.65*(1-40/np.linalg.norm(np.diff(P['join_d'],axis=0),axis=1).sum())),a['d_data']);token(ax,along(P['join_i'],move/.65*(1-40/np.linalg.norm(np.diff(P['join_i'],axis=0),axis=1).sum())),a['i_data'])
            else:token(ax,along(rounded([(1150,535),(1370,535)]),(move-.65)/.35),a['egress__pigen_packet_in'])
        if ev['drain_fire']:token(ax,along(P['output'],move),a['out_data'])
    ax.plot([60,1860],[920,920],color='#d5d0c5',lw=1)
    for x,c,name in [(60,DARK,'neither'),(400,BLUE,'ready'),(740,YELLOW,'valid'),(1080,GREEN,'both → transfer')]:
        ax.plot([x,x+55],[960,960],color=c,lw=WIRE_WIDTH);text(ax,x+75,960,name,15,c,ha='left')
    text(ax,60,1020,'Every wire is an explicit transfer in the Pigen source · actual simulated values',14,'#736f67',ha='left')

def main():
    p=argparse.ArgumentParser();p.add_argument('--trace',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--edge-seconds',type=float,default=1.25/3);a=p.parse_args()
    def number(v):
        try:return int(v)
        except ValueError:return None
    rows=[{k:number(v) for k,v in r.items()} for r in csv.DictReader(a.trace.open())]
    usable=[i for i in range(len(rows)-1) if rows[i]['reset']==0 and rows[i+1]['reset']==0]
    for i in usable:validate(rows[i],rows[i+1])
    results=[rows[i]['out_data'] for i in usable if rows[i]['drain_fire']]
    last=max(i for i in usable if rows[i]['drain_fire']);end=last+1
    def sig(r):return tuple(signals(r).values())+tuple((r[k+'_valid'],r[k+'_data'] if r[k+'_valid'] else None) for k in ['a','b','d','i','out'])+(r['in_valid'],r['in_data'] if r['in_valid'] else None)
    plan=[('edge',i,i,round(FPS*a.edge_seconds)) for i in range(usable[0],last+1)]
    plan.append(('end',end,end,round(FPS*a.edge_seconds)))
    rx={};taken=0;last_value=None
    for i in usable:
        old=(taken,last_value)
        if rows[i]['drain_fire']:taken+=1;last_value=rows[i]['out_data']
        rx[i]=(old,(taken,last_value))
    fig=plt.figure(figsize=(19.2,10.8),dpi=100,facecolor=PAPER);ax=fig.add_axes([0,0,1,1])
    def render(kind,i,j,t):
        if kind in ('edge','control'):
            names=[label for key,label in [('fork_fire','fork'),('left_fire','double'),('right_fire','increment'),('join_fire','join'),('drain_fire','receive')] if rows[i][key]]
            scene(ax,rows[i],rows[i+1],t,*rx[i],head=f'edge {rows[i]["cycle"]}',note=' + '.join(names) if names else 'Signal update · no accepted transfer')
        else:
            note='Receiver paused: valid data waits on yellow wires' if rows[i]['out_valid'] and not rows[i]['out_ready'] else 'No accepted transfers' if kind=='hold' else f'{len(results)} results checked: '+', '.join(map(str,results))
            before=rx[i][0] if i in rx else rx[last][1]
            scene(ax,rows[i],rows[i],0,before,before,edge=False,head=f'cycles {rows[i]["cycle"]}–{rows[j]["cycle"]}' if kind=='hold' else 'End of trace',note=note)
    a.out.mkdir(parents=True,exist_ok=True);video=a.out/'transfer_slots.mp4';temporary=a.out/'transfer_slots.rendering.mp4'
    proc=subprocess.Popen(['ffmpeg','-y','-loglevel','error','-f','rawvideo','-pix_fmt','rgb24','-s','1920x1080','-r',str(FPS),'-i','-', '-c:v','libx264','-preset','fast','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(temporary)],stdin=subprocess.PIPE)
    try:
        for kind,i,j,frames in plan:
            cached=None;previous=None
            for f in range(frames):
                t=f/max(1,frames-1);pause='held' if kind in ('hold','end') else 'before' if t<=.16 else 'after' if t>=.99 else None
                shown=0 if pause in ('held','before') else 1 if pause=='after' else t
                if pause is None or pause!=previous or cached is None:
                    render(kind,i,j,shown);fig.canvas.draw();cached=np.ascontiguousarray(np.asarray(fig.canvas.buffer_rgba())[:,:,:3]).tobytes()
                proc.stdin.write(cached);previous=pause
    finally:proc.stdin.close()
    if proc.wait():raise RuntimeError('ffmpeg failed')
    temporary.replace(video)
    poster=next(x for x in plan if x[0]=='edge' and rows[x[1]]['join_fire'])
    render(*poster[:3],.50);fig.savefig(a.out/'transfer_slots_poster.png',facecolor=PAPER);plt.close(fig)
    meta={'source':str(a.trace.resolve()),'trace_sha256':hashlib.sha256(a.trace.read_bytes()).hexdigest(),'fps':FPS,'frames':sum(x[3] for x in plan),'seconds':sum(x[3] for x in plan)/FPS,'size':[W,H],'edge_seconds':a.edge_seconds,
          'example':'examples/transfer_flow.pigen','validated_edges':len(usable),'connections':'explicit source transfers; no FIFO internals or selected paths','wire_width':WIRE_WIDTH,'colors':dict(neither=DARK,ready=BLUE,valid=YELLOW,both=GREEN),
          'segments':[dict(kind=k,first_cycle=rows[i]['cycle'],last_cycle=rows[j]['cycle'],frames=n) for k,i,j,n in plan]}
    (a.out/'transfer_slots.json').write_text(json.dumps(meta,indent=2)+'\n');print(f'{video}: {meta["seconds"]:.1f}s',flush=True)

if __name__=='__main__':main()
