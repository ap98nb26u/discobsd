import sys, subprocess, struct
from unicorn import *
from unicorn.arm_const import *

LIBGCC = subprocess.check_output(
    ["arm-none-eabi-gcc","-march=armv4t","-print-libgcc-file-name"]).decode().strip()
AS="arm-none-eabi-as"; LD="arm-none-eabi-ld"; OC="arm-none-eabi-objcopy"; NM="arm-none-eabi-nm"
BASE=0x10000; STACK=0x200000; STOP=0xfffffff0

def build(cfile, base):
    stem=cfile[:-2]
    subprocess.check_call(["./smlrc-arm", cfile, stem+".s"])
    subprocess.check_call([AS,"-march=armv4t","-o",stem+".o",stem+".s"])
    subprocess.check_call([LD,"-Ttext=0x%x"%base,"-e","0","-o",stem+".elf",stem+".o",LIBGCC])
    subprocess.check_call([OC,"-O","binary",stem+".elf",stem+".bin"])
    syms={}
    for line in subprocess.check_output([NM,stem+".elf"]).decode().splitlines():
        p=line.split()
        if len(p)==3: syms[p[2]]=int(p[0],16)
    return open(stem+".bin","rb").read(), syms

def run(cfile, func, args=(), base=BASE):
    blob,syms=build(cfile, base)
    mu=Uc(UC_ARCH_ARM, UC_MODE_ARM)
    size=(len(blob)+0xffff)&~0xffff
    mu.mem_map(base, max(size,0x10000))
    mu.mem_write(base, blob)
    mu.mem_map(STACK-0x100000, 0x100000)   # stack region
    sp=STACK-16
    for i,a in enumerate(args[:4]): mu.reg_write(UC_ARM_REG_R0+i, a & 0xffffffff)
    mu.reg_write(UC_ARM_REG_SP, sp)
    mu.reg_write(UC_ARM_REG_LR, STOP)
    mu.reg_write(UC_ARM_REG_R11, 0)
    start=syms[func]
    try:
        mu.emu_start(start, STOP)
    except UcError as e:
        print("EMU ERROR:", e, "pc=0x%x"%mu.reg_read(UC_ARM_REG_PC)); return None
    return mu.reg_read(UC_ARM_REG_R0)

if __name__=="__main__":
    cfile=sys.argv[1]; func=sys.argv[2]
    args=[int(x,0) for x in sys.argv[3:]]
    r=run(cfile,func,args)
    print("r0 =", r if r is None else "%d (0x%x)"%(r, r))
