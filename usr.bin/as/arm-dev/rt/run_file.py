# Run a native DiscoBSD/GBA OMAGIC a.out in Unicorn with file I/O.
#
#   python3 run_file.py [--root DIR] [--max N] prog.out [args...]
#
# Loads the a.out like the GBA kernel does (text+data at 0x02001800, bss
# zeroed) into the GBAED user window (250 KB, up to the end of EWRAM), and
# serves the syscalls the native libc uses through the simulated-SWI
# trampoline vector at 0x03007E00: exit, read, write, open, close, unlink, chmod,
# stat, lstat, fstat, lseek, sbrk.  Guest paths are resolved inside --root (default: current dir), so
# a program can read and write real host files there.  fd 1/2 go to the
# host's stdout/stderr.  Exit status is the guest's exit code.
import os, sys, struct
from unicorn import *
from unicorn.arm_const import *

BASE = 0x02001800
USEREND = 0x02040000           # GBAED: 250 KB user window = end of EWRAM
VECADDR = 0x03007E00           # trampoline vector (SYSCALL_VECTOR_ADDR)
HANDLER = 0x00E00000

args = sys.argv[1:]
root = "."
maxinsn = 0
while args and args[0].startswith("--"):
    if args[0] == "--root": root = args[1]; args = args[2:]
    elif args[0] == "--max": maxinsn = int(args[1]); args = args[2:]
    else: sys.exit("unknown option " + args[0])
prog = args[0]

d = open(prog, "rb").read()
mg, a_text, a_data, a_bss, _, _, _, a_entry = struct.unpack("<8I", d[:32])
assert (mg & 0xffff) == 0o407, "not an OMAGIC a.out"
img = d[32:32 + a_text + a_data]
end = BASE + a_text + a_data + a_bss
if end > USEREND:
    sys.exit("%s: image %d bytes does not fit the 250 KB window"
             % (prog, end - BASE))

mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
mu.mem_map(0x02000000, 0x40000)
mu.mem_write(BASE, img)
mu.mem_map(0x03000000, 0x8000)
mu.mem_write(VECADDR, struct.pack("<I", HANDLER))
mu.mem_map(HANDLER, 0x1000)

# argv/envp at the top of the window, like the kernel's exec.
strs = [a.encode() + b"\0" for a in [os.path.basename(prog)] + args[1:]]
sp = USEREND - sum(len(s) for s in strs) - 16
sp &= ~7
ptrs = []
p = sp
for s in strs:
    mu.mem_write(p, s); ptrs.append(p); p += len(s)
vec = struct.pack("<%dI" % (len(ptrs) + 2), *(ptrs + [0, 0]))
sp = (sp - len(vec) - 8) & ~7
argv = sp
mu.mem_write(argv, vec)
envp = argv + 4 * (len(ptrs) + 1)

fds = {0: 0, 1: 1, 2: 2}
EBADF, ENOENT, EINVAL = 9, 2, 22
status = [None]
brk = [end]

def hpath(gp):
    gp = gp.lstrip("/") if gp.startswith("/") else gp
    return os.path.join(root, gp)

def cstr(addr):
    b = bytearray()
    while True:
        c = mu.mem_read(addr, 1)[0]
        if c == 0: return b.decode("latin1")
        b.append(c); addr += 1

def put_stat(uc, buf, st):
    # DiscoBSD struct stat: 14 32-bit fields (sys/sys/stat.h)
    f = [st.st_dev, st.st_ino, st.st_mode, st.st_nlink, st.st_uid, st.st_gid,
         st.st_rdev, st.st_size, int(st.st_atime), int(st.st_mtime),
         int(st.st_ctime), 1024, (st.st_size + 1023) // 1024, 0]
    uc.mem_write(buf, struct.pack("<14I", *[x & 0xffffffff for x in f]))

def hook(uc, addr, size, ud):
    lr = uc.reg_read(UC_ARM_REG_LR)
    sysno = struct.unpack("<I", uc.mem_read(lr, 4))[0]
    r0, r1, r2 = [uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2)]
    err = 0; ret = 0
    try:
        if sysno == 1:
            status[0] = r0; uc.emu_stop(); return
        elif sysno == 3:                                   # read
            if r0 not in fds: err = EBADF
            else:
                data = os.read(fds[r0], r2); uc.mem_write(r1, data); ret = len(data)
        elif sysno == 4:                                   # write
            if r0 not in fds: err = EBADF
            else:
                data = bytes(uc.mem_read(r1, r2))
                if r0 in (1, 2):
                    (sys.stdout if r0 == 1 else sys.stderr).buffer.write(data)
                    (sys.stdout if r0 == 1 else sys.stderr).flush()
                else:
                    os.write(fds[r0], data)
                ret = r2
        elif sysno == 5:                                   # open
            fl = r1; hf = 0
            acc = fl & 3
            hf |= (os.O_RDONLY, os.O_WRONLY, os.O_RDWR, os.O_RDWR)[acc]
            if fl & 0x200: hf |= os.O_CREAT
            if fl & 0x400: hf |= os.O_TRUNC
            if fl & 0x8: hf |= os.O_APPEND
            h = os.open(hpath(cstr(r0)), hf, r2 & 0o777)
            g = 3
            while g in fds: g += 1
            fds[g] = h; ret = g
        elif sysno == 6:                                   # close
            if r0 not in fds: err = EBADF
            else:
                if r0 > 2: os.close(fds[r0])
                del fds[r0]
        elif sysno == 15:                                  # chmod
            os.chmod(hpath(cstr(r0)), r1 & 0o7777)
        elif sysno == 10:                                  # unlink
            os.unlink(hpath(cstr(r0)))
        elif sysno in (38, 40):                            # stat, lstat
            st = (os.stat if sysno == 38 else os.lstat)(hpath(cstr(r0)))
            put_stat(uc, r1, st)
        elif sysno == 62:                                  # fstat
            if r0 not in fds: err = EBADF
            else: put_stat(uc, r1, os.fstat(fds[r0]))
        elif sysno == 19:                                  # lseek
            if r0 not in fds: err = EBADF
            else:
                off = r1 - (1 << 32) if r1 & 0x80000000 else r1
                ret = os.lseek(fds[r0], off, r2)
        elif sysno == 69:                                  # sbrk(newbrk)
            if r0 >= USEREND - 0x2000: err = 12            # ENOMEM
            else: brk[0] = r0
        else:
            sys.stderr.write("[run_file] unhandled syscall %d\n" % sysno)
            err = EINVAL
    except FileNotFoundError:
        err = ENOENT
    except OSError as e:
        err = e.errno or EINVAL
    cpsr = uc.reg_read(UC_ARM_REG_CPSR)
    if err:
        uc.reg_write(UC_ARM_REG_R0, err); cpsr |= (1 << 29)
    else:
        uc.reg_write(UC_ARM_REG_R0, ret & 0xffffffff); cpsr &= ~(1 << 29)
    uc.reg_write(UC_ARM_REG_CPSR, cpsr)
    uc.reg_write(UC_ARM_REG_PC, lr + 4)

mu.hook_add(UC_HOOK_CODE, hook, begin=HANDLER, end=HANDLER)
mu.reg_write(UC_ARM_REG_SP, sp)
mu.reg_write(UC_ARM_REG_R0, len(ptrs))
mu.reg_write(UC_ARM_REG_R1, argv)
mu.reg_write(UC_ARM_REG_R2, envp)
mu.reg_write(UC_ARM_REG_LR, 0xfffffff0)
try:
    mu.emu_start(a_entry, 0xfffffff0, 0, maxinsn)
except UcError as e:
    sys.stderr.write("[run_file] EMU ERROR %s pc=%08x lr=%08x sp=%08x fp=%08x\n" % (
        e, mu.reg_read(UC_ARM_REG_PC), mu.reg_read(UC_ARM_REG_LR),
        mu.reg_read(UC_ARM_REG_SP), mu.reg_read(UC_ARM_REG_R11)))
    sys.exit(125)
if status[0] is None:
    sys.stderr.write("[run_file] stopped without exit (pc=%08x)\n"
                     % mu.reg_read(UC_ARM_REG_PC))
    sys.exit(124)
sys.exit(status[0] & 0xff)
