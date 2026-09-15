import sys
from unicorn import *
from unicorn.arm_const import *
TT=0x10000; VEC=0x03006400; HANDLER=0x00E00000
blob=open("prog.bin","rb").read()
ent=int([l for l in open("ld.err") if l.startswith("ENTRY")][0].split()[1],16)
mu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
mu.mem_map(0,0x100000); mu.mem_write(TT,blob)
mu.mem_map(0x00100000,0x100000)        # stack
mu.mem_map(0x03006000,0x1000)          # syscall vector page
mu.mem_write(VEC, HANDLER.to_bytes(4,'little'))
mu.mem_map(0x00E00000,0x1000)          # trampoline landing (hooked)
out=[]; exit_code=[None]
def hook(uc,addr,size,ud):
    if addr!=HANDLER: return
    lr=uc.reg_read(UC_ARM_REG_LR)
    sysno=int.from_bytes(uc.mem_read(lr,4),'little')
    r0=uc.reg_read(UC_ARM_REG_R0); r1=uc.reg_read(UC_ARM_REG_R1); r2=uc.reg_read(UC_ARM_REG_R2)
    cpsr=uc.reg_read(UC_ARM_REG_CPSR)
    if sysno==1:   exit_code[0]=r0; uc.emu_stop(); return
    elif sysno==4: out.append(bytes(uc.mem_read(r1,r2))); uc.reg_write(UC_ARM_REG_R0,r2); cpsr&=~(1<<29)
    elif sysno==69: uc.reg_write(UC_ARM_REG_R0,0); cpsr&=~(1<<29)
    else: uc.reg_write(UC_ARM_REG_R0,0); cpsr&=~(1<<29)
    uc.reg_write(UC_ARM_REG_CPSR,cpsr)
    uc.reg_write(UC_ARM_REG_PC, lr+4)
mu.hook_add(UC_HOOK_CODE,hook)
mu.reg_write(UC_ARM_REG_SP,0x00200000-16); mu.reg_write(UC_ARM_REG_R0,1); mu.reg_write(UC_ARM_REG_R1,0); mu.reg_write(UC_ARM_REG_R2,0); mu.reg_write(UC_ARM_REG_LR,0)
try: mu.emu_start(ent,0,0,2000000)
except UcError as e: print("EMU ERROR",e,"pc=%08x"%mu.reg_read(UC_ARM_REG_PC))
sys.stdout.write("PROGRAM OUTPUT: "+b"".join(out).decode('latin1'))
print("exit code =", exit_code[0])
