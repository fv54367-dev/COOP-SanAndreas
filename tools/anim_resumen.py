import json,sys,collections,math
def load(f):
    return [json.loads(l) for l in open(f)]
A=load(sys.argv[1]); B=load(sys.argv[2])
# cops / peds with anims, summary per ped index in each game
def summary(frames,name):
    st=collections.defaultdict(lambda:{'m':0,'n':0,'restarts':0,'top_changes':0,'last_top':None,'tops':collections.Counter(),'rotjumps':0,'lastrot':None,'pos':None,'lastt':{}})
    for fr in frames:
        for p in fr['peds']:
            s=st[p['i']]; s['m']=p['m']; s['n']+=1; s['pos']=p['p']
            top=max(p['a'],key=lambda a:a[3]) if p['a'] else None
            key=(top[0],top[1]) if top else None
            if key!=s['last_top']: s['top_changes']+=1; s['last_top']=key
            if key: s['tops'][key]+=1
            for a in p['a']:
                k=(a[0],a[1]); lt=s['lastt'].get(k)
                if lt is not None and a[2]+0.05<lt: s['restarts']+=1
                s['lastt'][k]=a[2]
            for k in list(s['lastt']):
                if k not in [(a[0],a[1]) for a in p['a']]: del s['lastt'][k]
            if s['lastrot'] is not None:
                d=abs((p['r']-s['lastrot']+math.pi)%(2*math.pi)-math.pi)
                if d>0.3: s['rotjumps']+=1
            s['lastrot']=p['r']
    print('==',name)
    for i,s in sorted(st.items(), key=lambda x:-x[1]['top_changes']):
        if s['n']<100: continue
        print(f"ped{i} model={s['m']} frames={s['n']} topChanges={s['top_changes']} restarts={s['restarts']} rotJumps={s['rotjumps']} pos={s['pos']} tops={s['tops'].most_common(4)}")
summary(A,'J1'); summary(B,'J2')
