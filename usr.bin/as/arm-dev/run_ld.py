import subprocess
from unicorn import *
from unicorn.arm_const import *
TT=0x10000
def build(cfiles):
    objs=[]
    for c in cfiles:
        s=c[:-2]+".s"; o=c[:-2]+".o"
        subprocess.check_call(["./smlrc-arm",c,s]); subprocess.check_call(["./asarm","-o",o,s])
        objs.append(o)
    return objs
def link(objs,entry):
    r=subprocess.run(["./ldarm","-Ttext=0x%x"%TT,"-e",entry,"-o","out.bin"]+objs,capture_output=True,text=True)
    if r.returncode!=0: raise RuntimeError(r.stderr)
    ent=None
    for ln in r.stderr.splitlines():
        if ln.startswith("ENTRY"): ent=int(ln.split()[1],16)
    return open("out.bin","rb").read(), ent
def run(cfiles,entry,args=()):
    objs=build(cfiles); blob,ent=link(objs,entry)
    mu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    mu.mem_map(0,0x100000); mu.mem_write(TT,blob)
    mu.mem_map(0x00100000,0x100000); mu.mem_map(0x00F00000,0x1000)
    for i,a in enumerate(args[:4]): mu.reg_write(UC_ARM_REG_R0+i,a&0xffffffff)
    mu.reg_write(UC_ARM_REG_SP,0x00200000-16); mu.reg_write(UC_ARM_REG_LR,0x00F00000); mu.reg_write(UC_ARM_REG_R11,0)
    try: mu.emu_start(ent,0x00F00000,0,3000000)
    except UcError as e: return ("ERR",str(e),hex(mu.reg_read(UC_ARM_REG_PC)))
    return mu.reg_read(UC_ARM_REG_R0)
