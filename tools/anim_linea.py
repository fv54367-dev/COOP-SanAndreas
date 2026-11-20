import json,sys
A=[json.loads(l) for l in open(sys.argv[1])]; B=[json.loads(l) for l in open(sys.argv[2])]
ia,ib=int(sys.argv[3]),int(sys.argv[4]); t0,t1=float(sys.argv[5]),float(sys.argv[6])
def fmt(fr,i):
    for p in fr['peds']:
        if p['i']==i:
            a=sorted(p['a'],key=lambda x:-x[3])[:3]
            return ' '.join(f"{x[0]}/{x[1]}@{x[2]:.2f}b{x[3]:.1f}{'+' if x[4]>0 else '-' if x[4]<0 else ''}" for x in a)
    return '-'
for fa,fb in zip(A,B):
    if t0<=fa['t']<=t1: print(f"{fa['t']:6.2f} J1[{fmt(fa,ia):48s}] J2[{fmt(fb,ib)}]")
