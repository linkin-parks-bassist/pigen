"""Independent checks for explicit-buffer trace state and transfer events."""
# Each pair names the actual forward-valid and backward-ready nets at that
# boundary. The adder and multipliers are not independently buffered channels.
WIRES={
 'input':('ingress_valid','ingress_ready'),
 'fork_a':('ingress_valid','ingress_ready'),
 'fork_a_write':('left__pigen_in_valid','left__pigen_in_ready'),
 'fork_b':('ingress_valid','ingress_ready'),
 'fork_b_write':('right__pigen_in_valid','right__pigen_in_ready'),
 'left_in':('left__pigen_valid','left__pigen_out_ready'),
 'left_out':('left__pigen_valid','left__pigen_out_ready'),
 'left_write':('doubled__pigen_in_valid','doubled__pigen_in_ready'),
 'right_in':('right__pigen_valid','right__pigen_out_ready'),
 'right_out':('right__pigen_valid','right__pigen_out_ready'),
 'right_write':('incremented__pigen_in_valid','incremented__pigen_in_ready'),
 'join_d':('doubled__pigen_valid','doubled__pigen_out_ready'),
 'join_i':('incremented__pigen_valid','incremented__pigen_out_ready'),
 'join_out':(('doubled__pigen_valid','incremented__pigen_valid'),'doubled__pigen_out_ready'),
 'join_write':('egress__pigen_in_valid','egress__pigen_in_ready'),
 'output':('egress_valid','egress_ready')}

def signals(r):
 return {wire:tuple(int(all(r[n] for n in net)) if isinstance(net,tuple) else r[net] for net in nets) for wire,nets in WIRES.items()}

def validate(a,b):
    s=signals(a)
    for event,wire in [('fork_fire','input'),('left_fire','left_in'),('right_fire','right_in'),('join_fire','join_write'),('drain_fire','output')]:
        assert a[event]==int(all(s[wire])),(a['cycle'],event)
    assert a['fork_fire']==(a['in_valid'] and a['in_ready'])
    fire=int(not a['reset'] and a['doubled__pigen_valid'] and a['incremented__pigen_valid'] and a['egress__pigen_in_ready'])
    for net in ['doubled__pigen_out_ready','incremented__pigen_out_ready','egress__pigen_in_valid']:
        assert a[net]==fire,(a['cycle'],net,'emitted join equation')
    assert a['egress__pigen_packet_in']==(a['doubled']+a['incremented'] if fire else 0),(a['cycle'],'gated join payload')
    for wire,event in [('fork_a_write','fork_fire'),('fork_b_write','fork_fire'),('left_write','left_fire'),('right_write','right_fire'),('join_d','join_fire'),('join_i','join_fire')]:
        assert int(all(s[wire]))==a[event],(a['cycle'],wire,'actual boundary fire')

    states=[('a','fork_fire','left_fire',a['in_data']),('b','fork_fire','right_fire',a['in_data']),
            ('d','left_fire','join_fire',2*a['a_data'] if a['a_valid'] else 0),
            ('i','right_fire','join_fire',a['b_data']+1 if a['b_valid'] else 0),
            ('out','join_fire','drain_fire',a['d_data']+a['i_data'] if a['d_valid'] and a['i_valid'] else 0)]
    for key,push,pop,data in states:
        assert b[key+'_valid']==int(a[push] or (a[key+'_valid'] and not a[pop])),(a['cycle'],key,'occupancy')
        assert a[('sum_ready' if key=='out' else key+'_ready')]==int(not a[key+'_valid'] or a[pop]),(a['cycle'],key,'capacity')
        if a[push]:assert b[key+'_data']==data,(a['cycle'],key,'data')
        elif a[key+'_valid'] and not a[pop]:assert b[key+'_data']==a[key+'_data'],(a['cycle'],key,'held')
