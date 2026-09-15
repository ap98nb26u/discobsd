# Load a DiscoBSD OMAGIC a.out the way the GBA kernel does and run it.
import sys, struct
from unicorn import *
from unicorn.arm_const import *
BASE=0x02001800; USEREND=0x02011800; VEC=0x03006400; HANDLER=0x00E00000
fn=sys.argv[1] if len(sys.argv)>1 else "hello.out"
d=open(fn,"rb").read()
mg,a_text,a_data,a_bss,rt,rd,sy,a_entry=struct.unpack("<8I",d[:32])
assert (mg&0xffff)==0o407 and ((mg>>16)&0x3ff)==0, "not OMAGIC/MID0"
img=d[32:32+a_text+a_data]
mu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
mu.mem_map(0x02000000,0x40000)          # 256KB EWRAM (user region + stack)
mu.mem_write(BASE, img)                  # text+data at 0x02001800
mu.mem_write(BASE+a_text+a_data, b"\x00"*a_bss)   # zero bss
mu.mem_map(0x03006000,0x1000); mu.mem_write(VEC, HANDLER.to_bytes(4,'little'))
mu.mem_map(HANDLER,0x1000)
# set up a tiny stack near the top of the 64KB user region: argc=1, argv, envp
sp=USEREND-0x100
argv0=sp+0x80; mu.mem_write(argv0, b"prog\x00")
argv=sp+0x40; mu.mem_write(argv, struct.pack("<II",argv0,0))     # argv[0], NULL
envp=sp+0x50; mu.mem_write(envp, struct.pack("<I",0))            # envp[0]=NULL
out=[]; ec=[None]
def hook(uc,addr,size,ud):
    if addr!=HANDLER: return
    lr=uc.reg_read(UC_ARM_REG_LR); sysno=int.from_bytes(uc.mem_read(lr,4),'little')
    r0=uc.reg_read(UC_ARM_REG_R0); r1=uc.reg_read(UC_ARM_REG_R1); r2=uc.reg_read(UC_ARM_REG_R2)
    cpsr=uc.reg_read(UC_ARM_REG_CPSR)
    if sysno==1: ec[0]=r0; uc.emu_stop(); return
    elif sysno==4: out.append(bytes(uc.mem_read(r1,r2))); uc.reg_write(UC_ARM_REG_R0,r2); cpsr&=~(1<<29)
    else: uc.reg_write(UC_ARM_REG_R0,0); cpsr&=~(1<<29)
    uc.reg_write(UC_ARM_REG_CPSR,cpsr); uc.reg_write(UC_ARM_REG_PC,lr+4)
mu.hook_add(UC_HOOK_CODE,hook)
mu.reg_write(UC_ARM_REG_SP,sp); mu.reg_write(UC_ARM_REG_R0,1)
mu.reg_write(UC_ARM_REG_R1,argv); mu.reg_write(UC_ARM_REG_R2,envp); mu.reg_write(UC_ARM_REG_LR,0xffffffff)
try: mu.emu_start(a_entry,0,0,3000000)
except UcError as e: print("EMU ERR",e,"pc=%08x"%mu.reg_read(UC_ARM_REG_PC))
sys.stdout.write("[%s @ 0x%08x] "%(fn,a_entry)+b"".join(out).decode('latin1'))
print("exit=",ec[0])
