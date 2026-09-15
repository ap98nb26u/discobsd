/*
 * Smaller C - ARM (ARMv4T / ARM7TDMI) code generator for DiscoBSD/GBA.
 *
 * Emits GNU-as-compatible ARM assembly text (validated against
 * arm-none-eabi-as/ld before the in-tree as/ld are retargeted). Target is
 * the GBA's ARM7TDMI = ARMv4T: NO movw/movt, NO blx <reg>, NO hardware
 * divide - large constants go through ldr= literal pools, indirect calls
 * use the mov lr,pc / bx idiom, and division is a runtime helper call.
 *
 * Frame layout realizes the shared core's convention (AddFxnParamSymbols:
 * first param at fp+2*SizeOfWord):
 *     [fp+0]  = saved caller fp (r11)
 *     [fp+4]  = return address (lr)
 *     [fp+8 .. fp+20] = incoming register params r0-r3, spilled
 *     [fp+24 ..]      = stack params (5th+), AAPCS-compatible
 *     [fp-1 .. ]      = locals (CurFxnMinLocalOfs, negative)
 * fp = entry_sp - 24; callee-saved r4-r10 are pushed just below the frame.
 * This is AAPCS-compatible for the register half (interops with the
 * gcc-built libc for calls of up to 4 args).
 *
 * INCREMENT 1: everything except function-call argument passing (a '(' ... ')'
 * user call currently errors out - see GenExpr0). Division/modulo DO work
 * (fixed __aeabi_* runtime calls). See sys/arch/gba/NOTES.toolchain.
 */

/* --- register model ------------------------------------------------------ */
#define ArmWReg     0   /* r0: working/return register (the core's "V0")     */
#define ArmTmp0     4   /* r4..r10: temp-register stack (T0..T6)             */
#define ArmMaxTemps 7
#define ArmTempA    12  /* ip: momentary scratch A (like MIPS AT)           */
#define ArmTempB    14  /* lr: momentary scratch B (safe: no bl in between) */
#define ArmFP       11
#define ArmSP       13
#define ArmLR       14
#define ArmPC       15
#define ArmIP       12

int GenWreg = ArmWReg;      /* current working register */
int GenLreg, GenRreg;       /* operands after GenPopReg() */
int GenLeaf;
fpos_t GenPrologPos;
int CanUseTempRegs;
int TempsUsed;

/* forward declarations for the temp-register stack (used before defined) */
STATIC void GenPushReg(void);
STATIC void GenPopReg(void);

STATIC
void prn(int r)             /* print an ARM register name */
{
  switch (r)
  {
  case 12: printf2("ip"); break;
  case 13: printf2("sp"); break;
  case 14: printf2("lr"); break;
  case 15: printf2("pc"); break;
  default: printf2("r%d", r); break;
  }
}

/* --- initialization / directives ---------------------------------------- */
STATIC
void GenInit(void)
{
  SizeOfWord = 4;
  OutputFormat = FormatSegmented;
  CodeHeaderFooter[0] = "\t.text";
  DataHeaderFooter[0] = "\t.data";
  RoDataHeaderFooter[0] = "\t.section\t.rodata";
  BssHeaderFooter[0] = "\t.bss";
  UseLeadingUnderscores = 0;
  FileHeader = "\t.syntax unified\n\t.arm";
}

STATIC
int GenInitParams(int argc, char** argv, int* idx)
{
  (void)argc;
  if (!strcmp(argv[*idx], "-v"))
    return 1;
  return 0;
}

STATIC void GenInitFinalize(void) {}
STATIC void GenStartCommentLine(void) { printf2(" @ "); }
STATIC void GenWordAlignment(int bss) { (void)bss; printf2("\t.align 2\n"); }

STATIC
void GenLabel(char* Label, int Static)
{
  if (!Static && GenExterns)
    printf2("\t.globl\t%s\n", Label);
  printf2("%s:\n", Label);
}

STATIC void GenNumLabel(int Label) { printf2(".L%d:\n", Label); }

STATIC
void GenPrintLabel(char* Label)
{
  if (isdigit(*Label))
    printf2(".L%s", Label);
  else
    printf2("%s", Label);
}

STATIC void GenZeroData(unsigned Size, int bss) { (void)bss; printf2("\t.space\t%u\n", truncUint(Size)); }

STATIC
void GenIntData(int Size, int Val)
{
  Val = truncInt(Val);
  if (Size == 1) printf2("\t.byte\t%d\n", Val);
  else if (Size == 2) printf2("\t.short\t%d\n", Val);
  else if (Size == 4) printf2("\t.word\t%d\n", Val);
}

STATIC void GenStartAsciiString(void) { printf2("\t.ascii\t"); }

STATIC
void GenAddrData(int Size, char* Label, int ofs)
{
  ofs = truncInt(ofs);
  if (Size == 1) printf2("\t.byte\t");
  else if (Size == 2) printf2("\t.short\t");
  else if (Size == 4) printf2("\t.word\t");
  GenPrintLabel(Label);
  if (ofs) printf2(" %+d", ofs);
  puts2("");
}

/* --- immediates (ARMv4T: no movt) --------------------------------------- */
STATIC
int ArmImm8r(unsigned v)
{
  /* representable as an 8-bit value rotated right by an even amount? */
  int i;
  for (i = 0; i < 16; i++)
  {
    unsigned r = i ? ((v << (2 * i)) | (v >> (32 - 2 * i))) : v;
    if ((r & 0xFFFFFF00u) == 0)
      return 1;
  }
  return 0;
}

STATIC
void GenMovImm(int rd, int imm)
{
  unsigned u = (unsigned)imm;
  if (ArmImm8r(u))
  {
    printf2("\tmov\t"); prn(rd); printf2(", #%d\n", imm);
  }
  else if (ArmImm8r(~u))
  {
    printf2("\tmvn\t"); prn(rd); printf2(", #%d\n", (int)(~u));
  }
  else
  {
    printf2("\tldr\t"); prn(rd); printf2(", =0x%08X\n", u);
  }
}

STATIC
void GenMov(int rd, int rs)
{
  if (rd != rs)
  {
    printf2("\tmov\t"); prn(rd); printf2(", "); prn(rs); puts2("");
  }
}

/* sp = sp - size (grow when size>0, shrink when size<0) */
STATIC
void GenGrowStack(int size)
{
  if (!size)
    return;
  if (size > 0)
  {
    if (ArmImm8r((unsigned)size)) printf2("\tsub\tsp, sp, #%d\n", size);
    else { GenMovImm(ArmIP, size); puts2("\tsub\tsp, sp, ip"); }
  }
  else
  {
    int a = -size;
    if (ArmImm8r((unsigned)a)) printf2("\tadd\tsp, sp, #%d\n", a);
    else { GenMovImm(ArmIP, a); puts2("\tadd\tsp, sp, ip"); }
  }
}

/* rd = rn +/- imm (add, sub) with materialization when out of range */
STATIC
void GenAddConst(int rd, int rn, int imm)
{
  if (imm == 0)
  {
    GenMov(rd, rn);
    return;
  }
  if (ArmImm8r((unsigned)imm))
  {
    printf2("\tadd\t"); prn(rd); printf2(", "); prn(rn); printf2(", #%d\n", imm);
  }
  else if (ArmImm8r((unsigned)(-imm)))
  {
    printf2("\tsub\t"); prn(rd); printf2(", "); prn(rn); printf2(", #%d\n", -imm);
  }
  else
  {
    GenMovImm(ArmIP, imm);
    printf2("\tadd\t"); prn(rd); printf2(", "); prn(rn); printf2(", ip\n");
  }
}

/* --- loads / stores ------------------------------------------------------ */
/* opSz: -1 signed byte, 1 unsigned byte, -2 signed half, 2 unsigned half,
   else word. base must not be ip (ip is used for out-of-range fallback). */
STATIC
void GenLoadMem(int rd, int base, int ofs, int opSz)
{
  char* op; int lim;
  if (opSz == -1)      { op = "ldrsb"; lim = 255; }
  else if (opSz == 1)  { op = "ldrb";  lim = 4095; }
  else if (opSz == -2) { op = "ldrsh"; lim = 255; }
  else if (opSz == 2)  { op = "ldrh";  lim = 255; }
  else                 { op = "ldr";   lim = 4095; }

  if (ofs >= -lim && ofs <= lim)
  {
    printf2("\t%s\t", op); prn(rd); printf2(", ["); prn(base);
    if (ofs) printf2(", #%d", ofs);
    puts2("]");
  }
  else
  {
    GenMovImm(ArmIP, ofs);
    printf2("\tadd\tip, ip, "); prn(base); puts2("");
    printf2("\t%s\t", op); prn(rd); puts2(", [ip]");
  }
}

STATIC
void GenStoreMem(int rs, int base, int ofs, int opSz)
{
  char* op; int lim;
  if (opSz == -1 || opSz == 1)      { op = "strb"; lim = 4095; }
  else if (opSz == -2 || opSz == 2) { op = "strh"; lim = 255; }
  else                              { op = "str";  lim = 4095; }

  if (ofs >= -lim && ofs <= lim)
  {
    printf2("\t%s\t", op); prn(rs); printf2(", ["); prn(base);
    if (ofs) printf2(", #%d", ofs);
    puts2("]");
  }
  else
  {
    GenMovImm(ArmIP, ofs);
    printf2("\tadd\tip, ip, "); prn(base); puts2("");
    printf2("\t%s\t", op); prn(rs); puts2(", [ip]");
  }
}

STATIC
void GenReadIdent(int rd, int opSz, int label)
{
  printf2("\tldr\t"); prn(rd); printf2(", =%s\n", IdentTable + label);
  GenLoadMem(rd, rd, 0, opSz);
}

STATIC
void GenReadLocal(int rd, int opSz, int ofs) { GenLoadMem(rd, ArmFP, ofs, opSz); }
STATIC
void GenReadIndirect(int rd, int rsrc, int opSz) { GenLoadMem(rd, rsrc, 0, opSz); }

STATIC
void GenWriteIdent(int rs, int opSz, int label)
{
  printf2("\tldr\tip, =%s\n", IdentTable + label);
  GenStoreMem(rs, ArmIP, 0, opSz);
}

STATIC
void GenWriteLocal(int rs, int opSz, int ofs) { GenStoreMem(rs, ArmFP, ofs, opSz); }
STATIC
void GenWriteIndirect(int rdst, int rsrc, int opSz) { GenStoreMem(rsrc, rdst, 0, opSz); }

/* sign/zero extend a sub-word result in place (ARMv4T has no sxtb/sxth) */
STATIC
void GenExtendReg(int reg, int opSz)
{
  if (opSz == -1)
  {
    printf2("\tlsl\t"); prn(reg); printf2(", "); prn(reg); puts2(", #24");
    printf2("\tasr\t"); prn(reg); printf2(", "); prn(reg); puts2(", #24");
  }
  else if (opSz == 1)
  {
    printf2("\tand\t"); prn(reg); printf2(", "); prn(reg); puts2(", #255");
  }
  else if (opSz == -2)
  {
    printf2("\tlsl\t"); prn(reg); printf2(", "); prn(reg); puts2(", #16");
    printf2("\tasr\t"); prn(reg); printf2(", "); prn(reg); puts2(", #16");
  }
  else if (opSz == 2)
  {
    printf2("\tlsl\t"); prn(reg); printf2(", "); prn(reg); puts2(", #16");
    printf2("\tlsr\t"); prn(reg); printf2(", "); prn(reg); puts2(", #16");
  }
}

/* --- ALU ----------------------------------------------------------------- */
STATIC
char* ArmMnem(int tok)
{
  switch (tok)
  {
  case '+': case tokPostAdd: case tokAssignAdd: return "add";
  case '-': case tokPostSub: case tokAssignSub: return "sub";
  case '&': case tokAssignAnd: return "and";
  case '^': case tokAssignXor: return "eor";
  case '|': case tokAssignOr:  return "orr";
  case tokLShift: case tokAssignLSh:  return "lsl";
  case tokRShift: case tokAssignRSh:  return "asr";
  case tokURShift: case tokAssignURSh: return "lsr";
  default: errorInternal(101); return "";
  }
}

STATIC
int ArmIsShift(int tok)
{
  return tok == tokLShift || tok == tokAssignLSh ||
         tok == tokRShift || tok == tokAssignRSh ||
         tok == tokURShift || tok == tokAssignURSh;
}

STATIC
void GenBinReg(int tok, int rd, int rn, int rm)
{
  printf2("\t%s\t", ArmMnem(tok)); prn(rd); printf2(", "); prn(rn);
  printf2(", "); prn(rm); puts2("");
}

STATIC
void GenBinConst(int tok, int rd, int rn, int imm)
{
  char* m = ArmMnem(tok);
  if (ArmIsShift(tok))
  {
    printf2("\t%s\t", m); prn(rd); printf2(", "); prn(rn);
    printf2(", #%d\n", imm & 31);
    return;
  }
  if (ArmImm8r((unsigned)imm))
  {
    printf2("\t%s\t", m); prn(rd); printf2(", "); prn(rn); printf2(", #%d\n", imm);
  }
  else if ((tok == '+' || tok == tokAssignAdd) && ArmImm8r((unsigned)(-imm)))
  {
    printf2("\tsub\t"); prn(rd); printf2(", "); prn(rn); printf2(", #%d\n", -imm);
  }
  else if ((tok == '-' || tok == tokAssignSub) && ArmImm8r((unsigned)(-imm)))
  {
    printf2("\tadd\t"); prn(rd); printf2(", "); prn(rn); printf2(", #%d\n", -imm);
  }
  else if ((tok == '&' || tok == tokAssignAnd) && ArmImm8r((unsigned)(~imm)))
  {
    printf2("\tbic\t"); prn(rd); printf2(", "); prn(rn); printf2(", #%d\n", ~imm);
  }
  else
  {
    GenMovImm(ArmIP, imm);
    GenBinReg(tok, rd, rn, ArmIP);
  }
}

/* rd = a * b, honouring ARMv4T's rd != Rm (first source) restriction */
STATIC
void GenMul(int rd, int a, int b)
{
  if (rd != a)
  {
    printf2("\tmul\t"); prn(rd); printf2(", "); prn(a); printf2(", "); prn(b); puts2("");
  }
  else if (rd != b)
  {
    printf2("\tmul\t"); prn(rd); printf2(", "); prn(b); printf2(", "); prn(a); puts2("");
  }
  else
  {
    printf2("\tmov\tip, "); prn(a); puts2("");
    printf2("\tmul\t"); prn(rd); printf2(", ip, "); prn(rd); puts2("");
  }
}

/* rd = ln (op) rn, where op is /,%,u/,u% via the EABI runtime helpers.
   The helpers clobber r0-r3, ip and lr but preserve r4-r11 (our temps). */
STATIC
void GenDivMod(int tok, int rd, int ln, int rn)
{
  char* fn;
  int res;
  /* stage numerator -> r0, denominator -> r1 (operands are r0 or r4-r10 or ip) */
  if (rn == 0)
  {
    printf2("\tmov\tr1, r0\n");           /* save denominator out of r0 */
    if (ln != 0) { printf2("\tmov\tr0, "); prn(ln); puts2(""); }
  }
  else
  {
    if (ln != 0) { printf2("\tmov\tr0, "); prn(ln); puts2(""); }
    printf2("\tmov\tr1, "); prn(rn); puts2("");
  }
  switch (tok)
  {
  case '/':      fn = "__aeabi_idiv";    res = 0; break;
  case tokUDiv:  fn = "__aeabi_uidiv";   res = 0; break;
  case '%':      fn = "__aeabi_idivmod"; res = 1; break;
  default:       fn = "__aeabi_uidivmod"; res = 1; break; /* tokUMod */
  }
  printf2("\tbl\t%s\n", fn);
  GenMov(rd, res);
  GenLeaf = 0;
}

/* --- comparisons (ARM has flags; far simpler than the MIPS slt dance) ---- */
STATIC
void GenCmpImm(int reg, int imm)
{
  if (ArmImm8r((unsigned)imm))
  {
    printf2("\tcmp\t"); prn(reg); printf2(", #%d\n", imm);
  }
  else if (ArmImm8r((unsigned)(-imm)))
  {
    printf2("\tcmn\t"); prn(reg); printf2(", #%d\n", -imm);
  }
  else
  {
    GenMovImm(ArmIP, imm);
    printf2("\tcmp\t"); prn(reg); printf2(", ip\n");
  }
}

/* op: low nibble 0:< 1:<= 2:> 3:>= 4:== 5:!=, bit4 = unsigned */
STATIC
void GenCmp(int* idx, int op)
{
  static char* sc[6] = { "lt", "le", "gt", "ge", "eq", "ne" };
  static char* uc[6] = { "lo", "ls", "hi", "hs", "eq", "ne" };
  static int inv[6] = { 3, 2, 1, 0, 5, 4 };
  int constness = (stack[*idx - 1][0] == tokNumInt) ? (stack[*idx - 1][1] != 0) : 2;
  int constval = truncInt(stack[*idx - 1][1]);
  int condbranch = (*idx + 1 < sp) ? (stack[*idx + 1][0] == tokIf) + (stack[*idx + 1][0] == tokIfNot) * 2 : 0;
  int unsign = (op >> 4) & 1;
  int kind = op & 0xF;
  int label = condbranch ? stack[*idx + 1][1] : 0;
  char* cc;

  if (constness == 2)
  {
    GenPopReg();
    printf2("\tcmp\t"); prn(GenLreg); printf2(", "); prn(GenRreg); puts2("");
  }
  else
  {
    GenCmpImm(GenWreg, constval);
  }

  if (condbranch == 2)
    kind = inv[kind];
  cc = unsign ? uc[kind] : sc[kind];

  if (condbranch)
  {
    printf2("\tb%s\t.L%d\n", cc, label);
  }
  else
  {
    printf2("\tmov\t"); prn(GenWreg); puts2(", #0");
    printf2("\tmov%s\t", cc); prn(GenWreg); puts2(", #1");
  }

  *idx += condbranch != 0;
}

STATIC
int GenIsCmp(int t)
{
  return t == '<' || t == '>' || t == tokGEQ || t == tokLEQ ||
         t == tokULess || t == tokUGreater || t == tokUGEQ || t == tokULEQ ||
         t == tokEQ || t == tokNEQ;
}

/* --- unconditional / boolean jumps -------------------------------------- */
STATIC void GenJumpUncond(int label) { printf2("\tb\t.L%d\n", label); }

STATIC
void GenJumpIfEqual(int val, int label)
{
  GenCmpImm(GenWreg, val);
  printf2("\tbeq\t.L%d\n", label);
}

STATIC
void GenJumpIfZero(int label)
{
  GenCmpImm(GenWreg, 0);
  printf2("\tbeq\t.L%d\n", label);
}

STATIC
void GenJumpIfNotZero(int label)
{
  GenCmpImm(GenWreg, 0);
  printf2("\tbne\t.L%d\n", label);
}

/* --- function frame ------------------------------------------------------ */
STATIC
void GenWriteFrameSize(void)
{
  /* fixed-width so the single-pass prolog can be back-patched in place */
  printf2("\tldr\tip, =0x%08X\n", (unsigned)(-CurFxnMinLocalOfs));
}

STATIC
void GenUpdateFrameSize(void)
{
  fpos_t pos;
  fgetpos(OutFile, &pos);
  fsetpos(OutFile, &GenPrologPos);
  GenWriteFrameSize();
  fsetpos(OutFile, &pos);
}

STATIC
void GenFxnProlog(void)
{
  /* build the [fp+0..20] block just below the caller's sp; fp = entry_sp-24 */
  puts2("\tsub\tsp, sp, #24");
  puts2("\tstr\tr11, [sp, #0]");
  puts2("\tstr\tlr, [sp, #4]");
  puts2("\tstr\tr0, [sp, #8]");
  puts2("\tstr\tr1, [sp, #12]");
  puts2("\tstr\tr2, [sp, #16]");
  puts2("\tstr\tr3, [sp, #20]");
  puts2("\tmov\tr11, sp");
  puts2("\tpush\t{r4, r5, r6, r7, r8, r9, r10}");
  GenLeaf = 1;
  fgetpos(OutFile, &GenPrologPos);
  GenWriteFrameSize();
  puts2("\tsub\tsp, sp, ip");   /* allocate locals (ip = size, 0 when none) */
}

STATIC
void GenFxnEpilog(void)
{
  GenUpdateFrameSize();
  puts2("\tsub\tsp, r11, #28");
  puts2("\tpop\t{r4, r5, r6, r7, r8, r9, r10}");
  puts2("\tldr\tlr, [r11, #4]");
  puts2("\tldr\tip, [r11, #0]");
  puts2("\tadd\tsp, r11, #24");
  puts2("\tmov\tr11, ip");
  puts2("\tbx\tlr");
  puts2("\t.ltorg");            /* flush this function's literal pool */
}

STATIC int GenMaxLocalsSize(void) { return 0x7FFFFFFF; }

/* --- inc / dec ----------------------------------------------------------- */
STATIC
void GenIncDecIdent(int rd, int opSz, int label, int tok)
{
  int t = (tok == tokInc) ? tokAssignAdd : tokAssignSub;
  GenReadIdent(rd, opSz, label);
  GenBinConst(t, rd, rd, 1);
  GenWriteIdent(rd, opSz, label);
  GenExtendReg(rd, opSz);
}

STATIC
void GenIncDecLocal(int rd, int opSz, int ofs, int tok)
{
  int t = (tok == tokInc) ? tokAssignAdd : tokAssignSub;
  GenReadLocal(rd, opSz, ofs);
  GenBinConst(t, rd, rd, 1);
  GenWriteLocal(rd, opSz, ofs);
  GenExtendReg(rd, opSz);
}

STATIC
void GenIncDecIndirect(int rd, int rsrc, int opSz, int tok)
{
  int t = (tok == tokInc) ? tokAssignAdd : tokAssignSub;
  GenReadIndirect(rd, rsrc, opSz);
  GenBinConst(t, rd, rd, 1);
  GenWriteIndirect(rsrc, rd, opSz);
  GenExtendReg(rd, opSz);
}

STATIC
void GenPostIncDecIdent(int rd, int opSz, int label, int tok)
{
  int t = (tok == tokPostInc) ? tokAssignAdd : tokAssignSub;
  int b = (tok == tokPostInc) ? tokAssignSub : tokAssignAdd;
  GenReadIdent(rd, opSz, label);
  GenBinConst(t, rd, rd, 1);
  GenWriteIdent(rd, opSz, label);
  GenBinConst(b, rd, rd, 1);
  GenExtendReg(rd, opSz);
}

STATIC
void GenPostIncDecLocal(int rd, int opSz, int ofs, int tok)
{
  int t = (tok == tokPostInc) ? tokAssignAdd : tokAssignSub;
  int b = (tok == tokPostInc) ? tokAssignSub : tokAssignAdd;
  GenReadLocal(rd, opSz, ofs);
  GenBinConst(t, rd, rd, 1);
  GenWriteLocal(rd, opSz, ofs);
  GenBinConst(b, rd, rd, 1);
  GenExtendReg(rd, opSz);
}

STATIC
void GenPostIncDecIndirect(int rd, int rsrc, int opSz, int tok)
{
  int t = (tok == tokPostInc) ? tokAssignAdd : tokAssignSub;
  int b = (tok == tokPostInc) ? tokAssignSub : tokAssignAdd;
  GenReadIndirect(rd, rsrc, opSz);
  GenBinConst(t, rd, rd, 1);
  GenWriteIndirect(rsrc, rd, opSz);
  GenBinConst(b, rd, rd, 1);
  GenExtendReg(rd, opSz);
}

/* --- temp-register stack ------------------------------------------------- */
STATIC
void GenWregInc(int inc)
{
  if (inc > 0)
  {
    if (GenWreg == ArmWReg) GenWreg = ArmTmp0;
    else GenWreg++;
  }
  else
  {
    if (GenWreg == ArmTmp0) GenWreg = ArmWReg;
    else GenWreg--;
  }
}

STATIC
void GenPushReg(void)
{
  if (CanUseTempRegs && TempsUsed < ArmMaxTemps)
  {
    GenWregInc(1);
    TempsUsed++;
    return;
  }
  printf2("\tstr\t"); prn(GenWreg); puts2(", [sp, #-4]!");
  TempsUsed++;
}

STATIC
void GenPopReg(void)
{
  TempsUsed--;
  if (CanUseTempRegs && TempsUsed < ArmMaxTemps)
  {
    GenRreg = GenWreg;
    GenWregInc(-1);
    GenLreg = GenWreg;
    return;
  }
  printf2("\tldr\t"); prn(ArmTempA); puts2(", [sp], #4");
  GenLreg = ArmTempA;
  GenRreg = GenWreg;
}

#define tokRevIdent    0x100
#define tokRevLocalOfs 0x101
#define tokAssign0     0x102
#define tokNum0        0x103

/* GenPrep: arch-neutral stack reshaping, copied verbatim from cgmips.c. */
STATIC
void GenPrep(int* idx)
{
  int tok;
  int oldIdxRight, oldIdxLeft, t0, t1;

  if (*idx < 0)
    errorInternal(100);

  tok = stack[*idx][0];
  oldIdxRight = --*idx;

  switch (tok)
  {
  case tokUDiv:
  case tokUMod:
  case tokAssignUDiv:
  case tokAssignUMod:
    if (stack[oldIdxRight][0] == tokNumInt || stack[oldIdxRight][0] == tokNumUint)
    {
      unsigned m = truncUint(stack[oldIdxRight][1]);
      if (m && !(m & (m - 1)))
      {
        if (tok == tokUMod || tok == tokAssignUMod)
        {
          stack[oldIdxRight][1] = (int)(m - 1);
          tok = (tok == tokUMod) ? '&' : tokAssignAnd;
        }
        else
        {
          t1 = 0;
          while (m >>= 1) t1++;
          stack[oldIdxRight][1] = t1;
          tok = (tok == tokUDiv) ? tokURShift : tokAssignURSh;
        }
        stack[oldIdxRight + 1][0] = tok;
      }
    }
  }

  switch (tok)
  {
  case tokNumUint:
    stack[oldIdxRight + 1][0] = tokNumInt;
    /* fallthrough */
  case tokNumInt:
  case tokNum0:
  case tokIdent:
  case tokLocalOfs:
    break;

  case tokPostAdd:
  case tokPostSub:
  case '-':
  case '/':
  case '%':
  case tokUDiv:
  case tokUMod:
  case tokLShift:
  case tokRShift:
  case tokURShift:
  case tokLogAnd:
  case tokLogOr:
  case tokComma:
    GenPrep(idx);
    /* fallthrough */
  case tokShortCirc:
  case tokGoto:
  case tokUnaryStar:
  case tokInc:
  case tokDec:
  case tokPostInc:
  case tokPostDec:
  case '~':
  case tokUnaryPlus:
  case tokUnaryMinus:
  case tok_Bool:
  case tokVoid:
  case tokUChar:
  case tokSChar:
  case tokShort:
  case tokUShort:
    GenPrep(idx);
    break;

  case '=':
    if (oldIdxRight + 1 == sp - 1 &&
        (stack[oldIdxRight][0] == tokNumInt || stack[oldIdxRight][0] == tokNumUint) &&
        truncUint(stack[oldIdxRight][1]) == 0)
    {
      stack[oldIdxRight][0] = tokNum0;
      stack[oldIdxRight + 1][0] = tokAssign0;
    }
    /* fallthrough */
  case tokAssignAdd:
  case tokAssignSub:
  case tokAssignMul:
  case tokAssignDiv:
  case tokAssignUDiv:
  case tokAssignMod:
  case tokAssignUMod:
  case tokAssignLSh:
  case tokAssignRSh:
  case tokAssignURSh:
  case tokAssignAnd:
  case tokAssignXor:
  case tokAssignOr:
    GenPrep(idx);
    oldIdxLeft = *idx;
    GenPrep(idx);
    if ((t0 = stack[oldIdxLeft][0]) == tokIdent || t0 == tokLocalOfs)
    {
      t1 = stack[oldIdxLeft][1];
      memmove(stack[oldIdxLeft], stack[oldIdxLeft + 1], (oldIdxRight - oldIdxLeft) * sizeof(stack[0]));
      stack[oldIdxRight][0] = (t0 == tokIdent) ? tokRevIdent : tokRevLocalOfs;
      stack[oldIdxRight][1] = t1;
    }
    break;

  case '+':
  case '*':
  case '&':
  case '^':
  case '|':
  case tokEQ:
  case tokNEQ:
  case '<':
  case '>':
  case tokLEQ:
  case tokGEQ:
  case tokULess:
  case tokUGreater:
  case tokULEQ:
  case tokUGEQ:
    GenPrep(idx);
    oldIdxLeft = *idx;
    GenPrep(idx);
    t1 = stack[oldIdxRight][0];
    t0 = stack[oldIdxLeft][0];
    if (t1 != tokNumInt && t0 == tokNumInt)
    {
      int xor;
      t1 = stack[oldIdxLeft][1];
      memmove(stack[oldIdxLeft], stack[oldIdxLeft + 1], (oldIdxRight - oldIdxLeft) * sizeof(stack[0]));
      stack[oldIdxRight][0] = t0;
      stack[oldIdxRight][1] = t1;
      switch (tok)
      {
      case '<': case '>': xor = '<' ^ '>'; break;
      case tokLEQ: case tokGEQ: xor = tokLEQ ^ tokGEQ; break;
      case tokULess: case tokUGreater: xor = tokULess ^ tokUGreater; break;
      case tokULEQ: case tokUGEQ: xor = tokULEQ ^ tokUGEQ; break;
      default: xor = 0; break;
      }
      tok ^= xor;
    }
    if (stack[oldIdxRight][0] == tokNumInt)
    {
      unsigned m = truncUint(stack[oldIdxRight][1]);
      switch (tok)
      {
      case '*':
        if (m && !(m & (m - 1)))
        {
          t1 = 0;
          while (m >>= 1) t1++;
          stack[oldIdxRight][1] = t1;
          tok = tokLShift;
        }
        break;
      case tokLEQ:
        if (m == 0x7FFFFFFF) { stack[oldIdxRight][1] = 0; tok = tokUGEQ; }
        break;
      case tokULEQ:
        if (m == 0xFFFFFFFF) { stack[oldIdxRight][1] = 0; tok = tokUGEQ; }
        break;
      case '>':
        if (m == 0x7FFFFFFF) { stack[oldIdxRight][1] = 0; tok = '&'; }
        break;
      case tokUGreater:
        if (m == 0xFFFFFFFF) { stack[oldIdxRight][1] = 0; tok = '&'; }
        break;
      }
    }
    stack[oldIdxRight + 1][0] = tok;
    break;

  case ')':
    while (stack[*idx][0] != '(')
    {
      GenPrep(idx);
      if (stack[*idx][0] == ',')
        --*idx;
    }
    --*idx;
    break;

  default:
    errorInternal(101);
  }
}

/* --- the expression walker ---------------------------------------------- */
/* INCREMENT 1: function calls are not yet supported (the '(' / ')' cases
   below error out). Everything else mirrors cgmips.c's GenExpr0. */
STATIC
void GenExpr0(void)
{
  int i;
  int gotUnary = 0;
  int maxCallDepth = 0;
  int callDepth = 0;
  int hasHiddenCall = 0;
  int t = sp - 1;

  if (stack[t][0] == tokIf || stack[t][0] == tokIfNot || stack[t][0] == tokReturn)
    t--;
  GenPrep(&t);

  for (i = 0; i < sp; i++)
  {
    int st = stack[i][0];
    if (st == '(')
    {
      if (++callDepth > maxCallDepth)
        maxCallDepth = callDepth;
    }
    else if (st == ')')
      callDepth--;
    /*
     * Division/modulo compile to __aeabi_* runtime calls (ARMv4T has no
     * divide instruction), which clobber r0-r3. The register-temp path
     * keeps live subexpression values in r0/r4-r10, so a hidden call would
     * destroy the working register - treat it like a real call and spill
     * to the stack instead. (MIPS is immune: its div/mflo aren't a call.)
     */
    else if (st == '/' || st == '%' || st == tokUDiv || st == tokUMod ||
             st == tokAssignDiv || st == tokAssignMod ||
             st == tokAssignUDiv || st == tokAssignUMod)
      hasHiddenCall = 1;
  }

  CanUseTempRegs = maxCallDepth == 0 && !hasHiddenCall;
  TempsUsed = 0;
  if (GenWreg != ArmWReg)
    errorInternal(102);

  for (i = 0; i < sp; i++)
  {
    int tok = stack[i][0];
    int v = stack[i][1];

    switch (tok)
    {
    case tokNumInt:
      if (!(i + 1 < sp && ((t = stack[i + 1][0]) == '+' || t == '-' ||
                           t == '&' || t == '^' || t == '|' ||
                           t == tokLShift || t == tokRShift || t == tokURShift ||
                           GenIsCmp(t))))
      {
        if (gotUnary)
          GenPushReg();
        GenMovImm(GenWreg, v);
      }
      gotUnary = 1;
      break;

    case tokIdent:
      if (gotUnary)
        GenPushReg();
      if (!(i + 1 < sp && ((t = stack[i + 1][0]) == ')' || t == tokUnaryStar ||
                           t == tokInc || t == tokDec ||
                           t == tokPostInc || t == tokPostDec)))
      {
        printf2("\tldr\t"); prn(GenWreg); printf2(", =%s\n", IdentTable + v);
      }
      gotUnary = 1;
      break;

    case tokLocalOfs:
      if (gotUnary)
        GenPushReg();
      if (!(i + 1 < sp && ((t = stack[i + 1][0]) == tokUnaryStar ||
                           t == tokInc || t == tokDec ||
                           t == tokPostInc || t == tokPostDec)))
      {
        GenAddConst(GenWreg, ArmFP, v);
      }
      gotUnary = 1;
      break;

    case '(':
      if (gotUnary)
        GenPushReg();
      gotUnary = 0;
      /* keep at least a 16-byte outgoing arg area on the stack */
      if (v < 16)
        GenGrowStack(16 - v);
      break;

    case ',':
      break;

    case ')':
      GenLeaf = 0;
      if (v > 16)
        errorInternal(200);   /* >4 args: next increment */
      if (stack[i - 1][0] == tokIdent)
      {
        if (v >= 4)  GenLoadMem(0, ArmSP, 0, 4);
        if (v >= 8)  GenLoadMem(1, ArmSP, 4, 4);
        if (v >= 12) GenLoadMem(2, ArmSP, 8, 4);
        if (v >= 16) GenLoadMem(3, ArmSP, 12, 4);
        printf2("\tbl\t%s\n", IdentTable + stack[i - 1][1]);
      }
      else
      {
        /* indirect call: target is in the working reg (r0); stash it in ip
           before the arg loads clobber r0. ARMv4T: mov lr,pc / bx. */
        GenMov(ArmIP, GenWreg);
        if (v >= 4)  GenLoadMem(0, ArmSP, 0, 4);
        if (v >= 8)  GenLoadMem(1, ArmSP, 4, 4);
        if (v >= 12) GenLoadMem(2, ArmSP, 8, 4);
        if (v >= 16) GenLoadMem(3, ArmSP, 12, 4);
        puts2("\tmov\tlr, pc");
        puts2("\tbx\tip");
      }
      if (v < 16)
        v = 16;
      GenGrowStack(-v);
      break;

    case tokUnaryStar:
      if (stack[i - 1][0] == tokIdent)
        GenReadIdent(GenWreg, v, stack[i - 1][1]);
      else if (stack[i - 1][0] == tokLocalOfs)
        GenReadLocal(GenWreg, v, stack[i - 1][1]);
      else
        GenReadIndirect(GenWreg, GenWreg, v);
      break;

    case tokUnaryPlus:
      break;
    case '~':
      printf2("\tmvn\t"); prn(GenWreg); printf2(", "); prn(GenWreg); puts2("");
      break;
    case tokUnaryMinus:
      printf2("\trsb\t"); prn(GenWreg); printf2(", "); prn(GenWreg); puts2(", #0");
      break;

    case '+':
    case '-':
    case '*':
    case '&':
    case '^':
    case '|':
    case tokLShift:
    case tokRShift:
    case tokURShift:
      if (stack[i - 1][0] == tokNumInt && tok != '*')
      {
        GenBinConst(tok, GenWreg, GenWreg, stack[i - 1][1]);
      }
      else
      {
        GenPopReg();
        if (tok == '*')
          GenMul(GenWreg, GenLreg, GenRreg);
        else
          GenBinReg(tok, GenWreg, GenLreg, GenRreg);
      }
      break;

    case '/':
    case tokUDiv:
    case '%':
    case tokUMod:
      GenPopReg();
      GenDivMod(tok, GenWreg, GenLreg, GenRreg);
      break;

    case tokInc:
    case tokDec:
      if (stack[i - 1][0] == tokIdent)
        GenIncDecIdent(GenWreg, v, stack[i - 1][1], tok);
      else if (stack[i - 1][0] == tokLocalOfs)
        GenIncDecLocal(GenWreg, v, stack[i - 1][1], tok);
      else
      {
        GenMov(ArmTempA, GenWreg);
        GenIncDecIndirect(GenWreg, ArmTempA, v, tok);
      }
      break;

    case tokPostInc:
    case tokPostDec:
      if (stack[i - 1][0] == tokIdent)
        GenPostIncDecIdent(GenWreg, v, stack[i - 1][1], tok);
      else if (stack[i - 1][0] == tokLocalOfs)
        GenPostIncDecLocal(GenWreg, v, stack[i - 1][1], tok);
      else
      {
        GenMov(ArmTempA, GenWreg);
        GenPostIncDecIndirect(GenWreg, ArmTempA, v, tok);
      }
      break;

    case tokPostAdd:
    case tokPostSub:
      {
        GenPopReg();
        if (GenWreg == GenLreg)
        {
          GenMov(ArmTempB, GenLreg);
          GenReadIndirect(GenWreg, ArmTempB, v);
          GenBinReg(tok, ArmTempA, GenWreg, GenRreg);
          GenWriteIndirect(ArmTempB, ArmTempA, v);
        }
        else
        {
          GenMov(ArmTempB, GenRreg);
          GenReadIndirect(GenWreg, GenLreg, v);
          GenBinReg(tok, ArmTempB, GenWreg, ArmTempB);
          GenWriteIndirect(GenLreg, ArmTempB, v);
        }
      }
      break;

    case tokAssignAdd:
    case tokAssignSub:
    case tokAssignMul:
    case tokAssignAnd:
    case tokAssignXor:
    case tokAssignOr:
    case tokAssignLSh:
    case tokAssignRSh:
    case tokAssignURSh:
      if (stack[i - 1][0] == tokRevLocalOfs || stack[i - 1][0] == tokRevIdent)
      {
        if (stack[i - 1][0] == tokRevLocalOfs)
          GenReadLocal(ArmTempB, v, stack[i - 1][1]);
        else
          GenReadIdent(ArmTempB, v, stack[i - 1][1]);
        if (tok == tokAssignMul)
          GenMul(GenWreg, ArmTempB, GenWreg);
        else
          GenBinReg(tok, GenWreg, ArmTempB, GenWreg);
        if (stack[i - 1][0] == tokRevLocalOfs)
          GenWriteLocal(GenWreg, v, stack[i - 1][1]);
        else
          GenWriteIdent(GenWreg, v, stack[i - 1][1]);
      }
      else
      {
        int lsaved, rsaved;
        GenPopReg();
        if (GenWreg == GenLreg)
        {
          GenMov(ArmTempB, GenLreg);
          lsaved = ArmTempB;
          rsaved = GenRreg;
        }
        else
        {
          GenMov(ArmTempB, GenRreg);
          rsaved = ArmTempB;
          lsaved = GenLreg;
        }
        GenReadIndirect(GenWreg, lsaved, v);
        if (tok == tokAssignMul)
          GenMul(GenWreg, GenWreg, rsaved);
        else
          GenBinReg(tok, GenWreg, GenWreg, rsaved);
        GenWriteIndirect(lsaved, GenWreg, v);
      }
      GenExtendReg(GenWreg, v);
      break;

    case tokAssignDiv:
    case tokAssignUDiv:
    case tokAssignMod:
    case tokAssignUMod:
      {
        int divtok = (tok == tokAssignDiv) ? '/' :
                     (tok == tokAssignUDiv) ? tokUDiv :
                     (tok == tokAssignMod) ? '%' : tokUMod;
        if (stack[i - 1][0] == tokRevLocalOfs || stack[i - 1][0] == tokRevIdent)
        {
          if (stack[i - 1][0] == tokRevLocalOfs)
            GenReadLocal(ArmTempB, v, stack[i - 1][1]);
          else
            GenReadIdent(ArmTempB, v, stack[i - 1][1]);
          GenDivMod(divtok, GenWreg, ArmTempB, GenWreg);
          if (stack[i - 1][0] == tokRevLocalOfs)
            GenWriteLocal(GenWreg, v, stack[i - 1][1]);
          else
            GenWriteIdent(GenWreg, v, stack[i - 1][1]);
        }
        else
        {
          int lsaved, rsaved;
          GenPopReg();
          if (GenWreg == GenLreg)
          {
            GenMov(ArmTempB, GenLreg);
            lsaved = ArmTempB;
            rsaved = GenRreg;
          }
          else
          {
            GenMov(ArmTempB, GenRreg);
            rsaved = ArmTempB;
            lsaved = GenLreg;
          }
          GenReadIndirect(GenWreg, lsaved, v);
          GenDivMod(divtok, GenWreg, GenWreg, rsaved);
          GenWriteIndirect(lsaved, GenWreg, v);
        }
        GenExtendReg(GenWreg, v);
      }
      break;

    case '=':
      if (stack[i - 1][0] == tokRevLocalOfs)
        GenWriteLocal(GenWreg, v, stack[i - 1][1]);
      else if (stack[i - 1][0] == tokRevIdent)
        GenWriteIdent(GenWreg, v, stack[i - 1][1]);
      else
      {
        GenPopReg();
        GenWriteIndirect(GenLreg, GenRreg, v);
        GenMov(GenWreg, GenRreg);
      }
      GenExtendReg(GenWreg, v);
      break;

    case tokAssign0:
      GenMovImm(ArmTempB, 0);
      if (stack[i - 1][0] == tokRevLocalOfs)
        GenWriteLocal(ArmTempB, v, stack[i - 1][1]);
      else if (stack[i - 1][0] == tokRevIdent)
        GenWriteIdent(ArmTempB, v, stack[i - 1][1]);
      else
        GenWriteIndirect(GenWreg, ArmTempB, v);
      break;

    case '<':         GenCmp(&i, 0x00); break;
    case tokLEQ:      GenCmp(&i, 0x01); break;
    case '>':         GenCmp(&i, 0x02); break;
    case tokGEQ:      GenCmp(&i, 0x03); break;
    case tokULess:    GenCmp(&i, 0x10); break;
    case tokULEQ:     GenCmp(&i, 0x11); break;
    case tokUGreater: GenCmp(&i, 0x12); break;
    case tokUGEQ:     GenCmp(&i, 0x13); break;
    case tokEQ:       GenCmp(&i, 0x04); break;
    case tokNEQ:      GenCmp(&i, 0x05); break;

    case tok_Bool:
      GenCmpImm(GenWreg, 0);
      printf2("\tmov\t"); prn(GenWreg); puts2(", #0");
      printf2("\tmovne\t"); prn(GenWreg); puts2(", #1");
      break;

    case tokSChar: GenExtendReg(GenWreg, -1); break;
    case tokUChar: GenExtendReg(GenWreg, 1); break;
    case tokShort: GenExtendReg(GenWreg, -2); break;
    case tokUShort: GenExtendReg(GenWreg, 2); break;

    case tokShortCirc:
      if (v >= 0)
        GenJumpIfZero(v);
      else
        GenJumpIfNotZero(-v);
      gotUnary = 0;
      break;
    case tokGoto:
      GenJumpUncond(v);
      gotUnary = 0;
      break;
    case tokLogAnd:
    case tokLogOr:
      GenNumLabel(v);
      break;

    case tokVoid:
      gotUnary = 0;
      break;

    case tokRevIdent:
    case tokRevLocalOfs:
    case tokComma:
    case tokReturn:
    case tokNum0:
      break;

    case tokIf:
      GenJumpIfNotZero(stack[i][1]);
      break;
    case tokIfNot:
      GenJumpIfZero(stack[i][1]);
      break;

    default:
      errorInternal(103);
      break;
    }
  }

  if (GenWreg != ArmWReg)
    errorInternal(104);
}

STATIC
void GenDumpChar(int ch)
{
  if (ch < 0)
  {
    if (TokenStringLen)
      printf2("\"\n");
    return;
  }
  if (TokenStringLen == 0)
  {
    GenStartAsciiString();
    printf2("\"");
  }
  if (ch >= 0x20 && ch <= 0x7E)
  {
    if (ch == '"' || ch == '\\')
      printf2("\\");
    printf2("%c", ch);
  }
  else
  {
    printf2("\\%03o", ch);
  }
}

STATIC void GenExpr(void) { GenExpr0(); }

/* Struct-by-value is disabled for ARM (NO_STRUCT_BY_VAL), so no runtime
   struct copy/push helpers are needed here. */
STATIC void GenFin(void) {}

/* Non-static: forward-declared without STATIC in smlrc.c (like cgx86.c).
   Interrupt functions are not supported for ARM yet. */
void GenIsrProlog(void) { errorInternal(201); }
void GenIsrEpilog(void) {}
