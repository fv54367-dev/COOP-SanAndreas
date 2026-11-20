# records peds near the local player: pos, heading, move state, anims (id, group, time, blend) every 25 ms
import ctypes, struct, sys, time, json
pid=int(sys.argv[1]); secs=float(sys.argv[2]); out=open(sys.argv[3],'w'); R=float(sys.argv[4]) if len(sys.argv)>4 else 60
k=ctypes.windll.kernel32; h=k.OpenProcess(0x10|0x400,False,pid)
def rd(a,n):
    b=ctypes.create_string_buffer(n); r=ctypes.c_size_t()
    if not k.ReadProcessMemory(h,ctypes.c_void_p(a),b,n,ctypes.byref(r)): return None
    return b.raw
def u32(a):
    b=rd(a,4); return struct.unpack('<I',b)[0] if b else 0
clumpOff=u32(0xB5F878)
pool=u32(0xB74490); objs=u32(pool); flags=u32(pool+4); size=u32(pool+8)
t0=time.perf_counter()
while time.perf_counter()-t0<secs:
    t=time.perf_counter()-t0
    me=u32(0xB7CD98); mpos=None
    if me:
        m=u32(me+0x14); mpos=struct.unpack('<3f',rd(m+0x30,12)) if m else None
    fl=rd(flags,size) or b''
    peds=[]
    for i in range(size):
        if i>=len(fl) or fl[i]&0x80: continue
        p=objs+i*0x7C4
        m=u32(p+0x14)
        if not m: continue
        pos=struct.unpack('<3f',rd(m+0x30,12))
        if mpos and ((pos[0]-mpos[0])**2+(pos[1]-mpos[1])**2)>R*R: continue
        rot=struct.unpack('<f',rd(p+0x558,4))[0]
        model=struct.unpack('<h',rd(p+0x22,2))[0]
        clump=u32(p+0x18); anims=[]
        if clump and clumpOff:
            cd=u32(clump+clumpOff)
            if cd:
                head=cd; link=u32(head); n=0
                while link and link!=head and n<16:
                    a=link-4; b=rd(a,0x30)
                    if not b: break
                    grp=struct.unpack_from('<H',b,0xE)[0]; bl,dl,tm,sp=struct.unpack_from('<4f',b,0x18); aid=struct.unpack_from('<h',b,0x2C)[0]
                    if bl>0.02: anims.append([aid,grp,round(tm,3),round(bl,2),round(dl,2)])
                    link=u32(link); n+=1
        peds.append({'i':i,'m':model,'p':[round(x,2) for x in pos],'r':round(rot,3),'a':anims})
    out.write(json.dumps({'t':round(t,3),'peds':peds})+'\n')
    time.sleep(max(0,0.025-(time.perf_counter()-t0-t)))
