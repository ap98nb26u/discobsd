import subprocess
from unicorn import *
from unicorn.arm_const import *
STOP=0x00F00000
def asm(cfile):
    stem=cfile[:-2]
    subprocess.check_call(["./smlrc-arm",cfile,stem+".s"])
    r=subprocess.run(["./asarm","-b",stem+".s",stem+".bin"],capture_output=True,text=True)
    syms={}; und=[]
    for ln in r.stderr.splitlines():
        p=ln.split()
        if p and p[0]=="SYM": syms[p[1]]=int(p[2])
        if p and p[0]=="UND": und.append(p[1])
    if r.returncode!=0: raise RuntimeError("asarm failed: "+r.stderr)
    return open(stem+".bin","rb").read(), syms, und
def run(cfile,func,args=()):
    blob,syms,und=asm(cfile)
    ext=[u for u in und if u not in syms]
    if ext: return ("EXT",ext)
    mu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    sz=(len(blob)+0xffff)&~0xffff
    mu.mem_map(0, max(sz,0x10000)); mu.mem_write(0, blob)
    mu.mem_map(0x00F00000,0x1000)          # stop page
    mu.mem_map(0x00100000,0x100000)        # stack
    for i,a in enumerate(args[:4]): mu.reg_write(UC_ARM_REG_R0+i,a&0xffffffff)
    mu.reg_write(UC_ARM_REG_SP,0x00200000-16)
    mu.reg_write(UC_ARM_REG_LR,STOP); mu.reg_write(UC_ARM_REG_R11,0)
    try: mu.emu_start(syms[func],STOP,0,2000000)
    except UcError as e: return ("ERR",str(e),hex(mu.reg_read(UC_ARM_REG_PC)))
    return mu.reg_read(UC_ARM_REG_R0)
