/*
 * asarm - minimal ARM (ARMv4T) assembler for the DiscoBSD/GBA toolchain.
 *
 * Assembles the subset of ARM assembly that usr.bin/smlrc's cgarm.c emits
 * (plus the hand-written runtime in arm-dev/rt) into a DiscoBSD a.out
 * relocatable object, or with -b into a flat image for the test harnesses.
 *
 * Modes:
 *   asarm -e "<one instruction>"   -> print the 8-hex-digit encoding (oracle)
 *   asarm [-v] -o out.o file.s     -> assemble to an a.out object
 *   asarm [-v] -b file.s out.bin   -> flat text+rodata+data(+bss) image
 * -v lists the symbols (SYM/UND lines) on stderr.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/*
 * Memory use.  as_arm runs in the target user window (GBAED = 250 KB), so it
 * streams: the input is read twice (pass 1 sizes and labels, pass 2 emits),
 * section bytes go straight to their place in the output file, and the text
 * relocation records go to a temporary file in order.  What stays in memory
 * is the symbol table (compiler labels ".L<n>" in a compact numeric table,
 * other names in a hash table), one literal-pool group and the data-section
 * relocations.  This lets it assemble the compiler itself (smlrc.c: about
 * 38,000 lines and 146 KB of text).
 */
#define NHASH    1024                 /* named-symbol hash buckets */
#define MAXPEND  1024                 /* literals waiting for one pool */
#define WBUF     256                  /* per-section output buffer */

static const char *infile = "<expr>";
static int lineno;
static char *tmpname;                 /* text-relocation temp file, if open */

static void fatal(const char *msg, const char *arg)
{
    fprintf(stderr, "asarm: %s:%d: %s%s%s\n", infile, lineno, msg,
            arg ? " " : "", arg ? arg : "");
    if (tmpname) remove(tmpname);
    exit(1);
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (!p) fatal("out of memory", "");
    return p;
}

/*
 * Symbols.  A symbol is named by a "ref".  A ref >= NUMREF is the compiler
 * label ".L<ref-NUMREF>", kept in a numeric table (address and section, no
 * name string).  A smaller ref indexes the named-symbol table, which keeps
 * creation order (the a.out symbol-table order) and a hash chain.  Only
 * named symbols can be global or undefined.
 */
#define NUMREF  0x40000000
struct sym { char *name; int addr; int next; unsigned char defined, global, sec; };
static struct sym *syms;
static int nsym, maxsym;
static int hashtab[NHASH];

static int *lab_addr;                 /* .L<n> address, by n */
static unsigned char *lab_sec;        /* section + 1; 0 = not defined */
static int maxlab;

static unsigned hashname(const char *n)
{
    unsigned h = 5381;
    while (*n) h = h * 33 + (unsigned char)*n++;
    return h % NHASH;
}

/* ".L<digits>" (no leading zero) -> the number; anything else -> -1 */
static int numlabel(const char *n)
{
    int v = 0;
    if (n[0] != '.' || n[1] != 'L' || !isdigit((unsigned char)n[2])) return -1;
    if (n[2] == '0' && n[3]) return -1;
    for (n += 2; *n; n++) {
        if (!isdigit((unsigned char)*n) || v > 100000000) return -1;
        v = v * 10 + (*n - '0');
    }
    return v;
}

static int named_find(const char *n)
{
    int i;
    for (i = hashtab[hashname(n)]; i >= 0; i = syms[i].next)
        if (!strcmp(syms[i].name, n))
            return i;
    return -1;
}

static int named_get(const char *n)
{
    int i = named_find(n);
    unsigned h;
    if (i >= 0) return i;
    if (nsym == maxsym) {
        maxsym = maxsym ? maxsym * 2 : 256;
        syms = xrealloc(syms, maxsym * sizeof(struct sym));
    }
    i = nsym++;
    syms[i].name = xrealloc(NULL, strlen(n) + 1);
    strcpy(syms[i].name, n);
    syms[i].addr = 0; syms[i].defined = 0; syms[i].global = 0; syms[i].sec = 0;
    h = hashname(n);
    syms[i].next = hashtab[h];
    hashtab[h] = i;
    return i;
}

/* Look up a symbol.  With create, an unknown name becomes a new undefined
   named symbol; without, it gives -1. */
static int sym_ref(const char *n, int create)
{
    int v = numlabel(n), i;
    if (v >= 0 && v < maxlab && lab_sec[v]) return NUMREF + v;
    i = named_find(n);
    if (i >= 0 || !create) return i;
    return named_get(n);
}

static int ref_defined(int r)
{
    if (r < 0) return 0;
    return r >= NUMREF ? 1 : syms[r].defined;
}
static int ref_addr(int r) { return r >= NUMREF ? lab_addr[r - NUMREF] : syms[r].addr; }
static int ref_sec(int r)  { return r >= NUMREF ? lab_sec[r - NUMREF] - 1 : syms[r].sec; }

/* define a label at (sec, off); pass 1 only */
static void sym_define(const char *n, int sec, int off)
{
    int v = numlabel(n);
    if (v >= 0 && named_find(n) < 0) {
        if (v >= maxlab) {
            int m = maxlab ? maxlab : 1024, k;
            while (m <= v) m *= 2;
            lab_addr = xrealloc(lab_addr, m * sizeof(int));
            lab_sec = xrealloc(lab_sec, m);
            for (k = maxlab; k < m; k++) lab_sec[k] = 0;
            maxlab = m;
        }
        lab_addr[v] = off;
        lab_sec[v] = sec + 1;
        return;
    }
    v = named_get(n);
    syms[v].defined = 1; syms[v].addr = off; syms[v].sec = sec;
}

/* ---- tokenizing helpers over a single instruction/operand string ---- */

/* condition-code suffix -> 4-bit field; returns -1 if not a known cc */
static int cond_code(const char *s)
{
    static struct { const char *n; int v; } t[] = {
        {"eq",0},{"ne",1},{"cs",2},{"hs",2},{"cc",3},{"lo",3},{"mi",4},
        {"pl",5},{"vs",6},{"vc",7},{"hi",8},{"ls",9},{"ge",10},{"lt",11},
        {"gt",12},{"le",13},{"al",14},{NULL,0}
    };
    int i;
    for (i = 0; t[i].n; i++)
        if (!strcmp(s, t[i].n)) return t[i].v;
    return -1;
}

/* register name -> number 0..15, or -1 */
static int reg_num(const char *s)
{
    if (!s) return -1;
    if ((s[0]=='r'||s[0]=='R') && isdigit((unsigned char)s[1])) {
        int n = atoi(s+1);
        if (n >= 0 && n <= 15 && (s[2]==0 || (isdigit((unsigned char)s[2])&&s[3]==0)))
            return n;
    }
    if (!strcmp(s,"sp")) return 13;
    if (!strcmp(s,"lr")) return 14;
    if (!strcmp(s,"pc")) return 15;
    if (!strcmp(s,"ip")) return 12;
    if (!strcmp(s,"fp")) return 11;
    if (!strcmp(s,"sl")) return 10;
    return -1;
}

/* parse an immediate '#value' (decimal, 0x hex, negatives). */
static int parse_imm(const char *s, long *out)
{
    if (*s != '#') return 0;
    s++;
    *out = strtol(s, NULL, 0);
    return 1;
}

/* encode a 32-bit constant into ARM data-processing rotate:imm8; -1 if it
   does not fit.  The CPU computes imm8 ROR (2*rot), so imm8 is v rotated
   LEFT by 2*rot: rotate v left by 2*i until it fits in 8 bits, then rot = i. */
static int enc_imm8r(unsigned v)
{
    int i;
    for (i = 0; i < 16; i++) {
        unsigned r = i ? ((v << (2*i)) | (v >> (32-2*i))) : v;
        if ((r & 0xFFFFFF00u) == 0)
            return i << 8 | (r & 0xFF);
    }
    return -1;
}

/* ---- operand splitting ---- */
/* split "a, b, c" -> up to 4 trimmed fields; returns count. Handles nested
   [ ] and { } by not splitting commas inside them. */
static int split_ops(char *s, char *out[], int max)
{
    int n = 0, depth = 0;
    char *p = s, *start = s;
    while (*p) {
        if (*p == '[' || *p == '{') depth++;
        else if (*p == ']' || *p == '}') depth--;
        else if (*p == ',' && depth == 0) {
            *p = 0;
            if (n < max) out[n++] = start;
            start = p+1;
        }
        p++;
    }
    if (n < max) out[n++] = start;
    /* trim */
    {
        int i;
        for (i = 0; i < n; i++) {
            char *a = out[i];
            while (*a==' '||*a=='\t') a++;
            char *e = a + strlen(a);
            while (e>a && (e[-1]==' '||e[-1]=='\t')) *--e = 0;
            out[i] = a;
        }
    }
    return n;
}

/* condition + 's' stripping from a mnemonic base. Fills *cc (default AL=14).
   e.g. "movne"->base "mov", cc ne. "bl"->"bl". Only strips a cc that leaves a
   known base handled by the caller. We strip greedily then let caller match. */

/* ---- the encoder: mnemonic + operand strings -> 32-bit word ---- */
/* reloc-out: if the instruction references an external/undefined symbol, the
   caller records it. Here (flat mode) branches to externals encode -2 like
   GNU as. addr = current text address of this instruction (for PC-relative).
   For -e mode addr is 0. Returns encoded word; sets *ext to symbol name if a
   relocation is needed (branch to undefined). */

static int dp_opcode(const char *m)   /* data-processing opcode field, or -1 */
{
    if (!strcmp(m,"and")) return 0;
    if (!strcmp(m,"eor")) return 1;
    if (!strcmp(m,"sub")) return 2;
    if (!strcmp(m,"rsb")) return 3;
    if (!strcmp(m,"add")) return 4;
    if (!strcmp(m,"orr")) return 12;
    if (!strcmp(m,"mov")) return 13;
    if (!strcmp(m,"bic")) return 14;
    if (!strcmp(m,"mvn")) return 15;
    return -1;
}

/* forward */
static int find_label_addr(const char *n, int *known);

/* encode one instruction. addr = address of this word. *extname/​*extkind
   out-params describe an unresolved reference (kind: 1=bl/b to symbol). */
static unsigned encode(char *mnem, char *ops, int addr, char *extname, int *extkind)
{
    char *o[4];
    int no;
    int cc = 14;
    char base[16];
    long imm;
    extname[0] = 0; *extkind = 0;

    /* separate a trailing condition code from data-proc/branch mnemonics */
    strncpy(base, mnem, sizeof(base)-1); base[sizeof(base)-1]=0;

    /* branch: b, bl, bCC */
    if (base[0]=='b' && (base[1]==0 || base[1]=='l' ||
        (cond_code(base+1)>=0) || (base[1]=='x'))) {
        if (!strcmp(base,"bx")) {
            int rm;
            no = split_ops(ops,o,4);
            rm = reg_num(o[0]);
            if (rm<0) fatal("bx: bad reg", o[0]);
            return 0xE12FFF10u | rm;
        }
        {
            int l = 0, c = 14;
            const char *rest = base+1;
            if (!strcmp(base,"bl")) l=1;
            else if (base[1]==0) l=0;
            else { c = cond_code(rest); if (c<0) fatal("bad branch", base); }
            no = split_ops(ops,o,4);
            {
                int known, target = find_label_addr(o[0], &known);
                int off;
                if (!known) { strcpy(extname,o[0]); *extkind=1; off = -2; }
                else off = (target - (addr + 8)) >> 2;
                return ((unsigned)c<<28) | (0x5u<<25) | ((unsigned)l<<24) |
                       (off & 0x00FFFFFF);
            }
        }
    }

    /* strip condition suffix for the rest (dp, ldr/str, etc.) */
    {
        int L = strlen(base);
        /* try 2-char cc suffix if it leaves a >=3 char base */
        if (L > 3) {
            int c = cond_code(base + L - 2);
            /* only treat as cc if the remaining base is a real mnemonic */
            if (c >= 0) {
                char stem[16];
                strncpy(stem, base, L-2); stem[L-2]=0;
                if (dp_opcode(stem) >= 0) { cc = c; strcpy(base, stem); }
            }
        }
    }

    /* data processing (incl. cmp/cmn, which have no dp_opcode row) */
    if (dp_opcode(base) >= 0 || !strcmp(base,"cmp") || !strcmp(base,"cmn")) {
        int op = dp_opcode(base);
        int rd, rn, rm;
        no = split_ops(ops,o,4);
        if (!strcmp(base,"mov") || !strcmp(base,"mvn")) {
            rd = reg_num(o[0]);
            if (parse_imm(o[1], &imm)) {
                int e = enc_imm8r((unsigned)imm);
                if (e<0) fatal("mov imm not encodable", o[1]);
                return ((unsigned)cc<<28)|(1u<<25)|((unsigned)op<<21)|(rd<<12)|e;
            } else {
                rm = reg_num(o[1]);
                if (rm<0) fatal("mov: bad operand", o[1]);
                return ((unsigned)cc<<28)|((unsigned)op<<21)|(rd<<12)|rm;
            }
        }
        if (!strcmp(base,"cmp")||!strcmp(base,"cmn")) {
            int opc = !strcmp(base,"cmp")?10:11;
            rn = reg_num(o[0]);
            if (parse_imm(o[1], &imm)) {
                int e = enc_imm8r((unsigned)imm);
                if (e<0) fatal("cmp imm not encodable", o[1]);
                return ((unsigned)cc<<28)|(1u<<25)|((unsigned)opc<<21)|(1u<<20)|(rn<<16)|e;
            } else {
                rm = reg_num(o[1]);
                return ((unsigned)cc<<28)|((unsigned)opc<<21)|(1u<<20)|(rn<<16)|rm;
            }
        }
        /* 3-operand: op rd, rn, (#imm | rm) */
        rd = reg_num(o[0]); rn = reg_num(o[1]);
        if (rd<0||rn<0) fatal("dp: bad reg", ops);
        if (no>=3 && parse_imm(o[2], &imm)) {
            int e = enc_imm8r((unsigned)imm);
            if (e<0) fatal("dp imm not encodable", o[2]);
            return ((unsigned)cc<<28)|(1u<<25)|((unsigned)op<<21)|(rn<<16)|(rd<<12)|e;
        } else {
            rm = reg_num(o[2]);
            if (rm<0) fatal("dp: bad rm", o[2]);
            return ((unsigned)cc<<28)|((unsigned)op<<21)|(rn<<16)|(rd<<12)|rm;
        }
    }

    /* shifts: lsl/lsr/asr rd, rn, (#imm | rs)  == MOV with shifter */
    if (!strcmp(base,"lsl")||!strcmp(base,"lsr")||!strcmp(base,"asr")) {
        int type = !strcmp(base,"lsl")?0:!strcmp(base,"lsr")?1:2;
        int rd, rn;
        no = split_ops(ops,o,4);
        rd = reg_num(o[0]); rn = reg_num(o[1]);
        if (parse_imm(o[2], &imm)) {
            int sh = imm & 31;
            return ((unsigned)cc<<28)|(13u<<21)|(rd<<12)|(sh<<7)|(type<<5)|rn;
        } else {
            int rs = reg_num(o[2]);
            return ((unsigned)cc<<28)|(13u<<21)|(rd<<12)|(rs<<8)|(type<<5)|(1<<4)|rn;
        }
    }

    /* mul rd, rm, rs */
    if (!strcmp(base,"mul")) {
        int rd, rm, rs;
        no = split_ops(ops,o,4);
        rd = reg_num(o[0]); rm = reg_num(o[1]); rs = reg_num(o[2]);
        return ((unsigned)cc<<28)|(rd<<16)|(rs<<8)|(9<<4)|rm;
    }

    /* push/pop {reglist} */
    if (!strcmp(base,"push")||!strcmp(base,"pop")) {
        int list = 0;
        char *b = strchr(ops,'{'); char *e = strchr(ops,'}');
        char tmp[128];
        if (!b||!e) fatal("push/pop: bad list", ops);
        strncpy(tmp,b+1,e-b-1); tmp[e-b-1]=0;
        {
            char *parts[20]; int np = split_ops(tmp,parts,20), i;
            for (i=0;i<np;i++) {
                char *dash = strchr(parts[i],'-');
                if (dash) {
                    char a[8],z[8]; int ra,rz,k;
                    *dash=0; strcpy(a,parts[i]); strcpy(z,dash+1);
                    ra=reg_num(a); rz=reg_num(z);
                    for (k=ra;k<=rz;k++) list|=1<<k;
                } else list |= 1<<reg_num(parts[i]);
            }
        }
        /* GNU as encodes a single-register push/pop as the str/ldr form */
        {
            int cnt=0,only=-1,k;
            for(k=0;k<16;k++) if(list&(1<<k)){cnt++;only=k;}
            if(cnt==1){
                if(!strcmp(base,"push")) return 0xE52D0004u|(only<<12); /* str rX,[sp,#-4]! */
                else return 0xE49D0004u|(only<<12);                    /* ldr rX,[sp],#4 */
            }
        }
        if (!strcmp(base,"push")) return 0xE92D0000u | list;
        else return 0xE8BD0000u | list;
    }

    /* load/store */
    {
        int isload=-1, byte=0, half=0, sign=0;
        if (!strcmp(base,"ldr")) {isload=1;}
        else if (!strcmp(base,"str")) {isload=0;}
        else if (!strcmp(base,"ldrb")){isload=1;byte=1;}
        else if (!strcmp(base,"strb")){isload=0;byte=1;}
        else if (!strcmp(base,"ldrh")){isload=1;half=1;}
        else if (!strcmp(base,"strh")){isload=0;half=1;}
        else if (!strcmp(base,"ldrsb")){isload=1;half=1;sign=1;byte=1;}
        else if (!strcmp(base,"ldrsh")){isload=1;half=1;sign=1;}
        if (isload>=0) {
            /* operands: rd, [rn] | [rn, #imm] | [rn, #imm]! | [rn], #imm | =lit */
            no = split_ops(ops,o,4);
            int rd = reg_num(o[0]);
            /* ldr rd, =literal */
            if (o[1][0]=='=') return 0xFFFFFFFFu; /* handled specially by caller */
            /* addressing: o[1] like "[rn" ... may have been split; rejoin */
            {
                char addrbuf[128]; addrbuf[0]=0;
                int i; for(i=1;i<no;i++){ if(i>1)strcat(addrbuf,", "); strcat(addrbuf,o[i]); }
                /* parse [rn], [rn, #imm], [rn, #imm]!, [rn], #imm */
                char *lb=strchr(addrbuf,'['); char *rb=strchr(addrbuf,']');
                int rn, off=0, P=1, U=1, W=0;
                char inside[128], after[64];
                if(!lb||!rb) fatal("ldr/str: bad addr", addrbuf);
                strncpy(inside,lb+1,rb-lb-1); inside[rb-lb-1]=0;
                strcpy(after, rb+1);
                {
                    char *ip[4]; int ic=split_ops(inside,ip,4);
                    rn = reg_num(ip[0]);
                    if (ic>=2 && parse_imm(ip[1],&imm)) { off=imm; }
                }
                if (after[0]=='!') { W=1; }
                else if (strchr(after,'#')) {
                    /* post-index: [rn], #imm */
                    long pv; char *h=strchr(after,'#'); parse_imm(h,&pv); off=pv; P=0; W=0;
                }
                if (off<0){U=0; off=-off;}
                if (half) {
                    int sh = sign ? (byte?2:3) : 1; /* SH: 01 H,10 SB,11 SH */
                    int immbit = 1; /* immediate offset form */
                    int hi=(off>>4)&0xF, lo=off&0xF;
                    return ((unsigned)cc<<28)|(0u<<25)|(P<<24)|(U<<23)|(immbit<<22)|
                           (W<<21)|((unsigned)isload<<20)|(rn<<16)|(rd<<12)|(hi<<8)|
                           (1<<7)|(sh<<5)|(1<<4)|lo;
                } else {
                    return ((unsigned)cc<<28)|(1u<<26)|(P<<24)|(U<<23)|((unsigned)byte<<22)|
                           (W<<21)|((unsigned)isload<<20)|(rn<<16)|(rd<<12)|(off&0xFFF);
                }
            }
        }
    }

    fatal("unknown instruction", base);
    return 0;
}

/* label lookup for branch encoding (pass 2). */
static int find_label_addr(const char *n, int *known)
{
    int r = sym_ref(n, 0);
    if (ref_defined(r)) { *known = 1; return ref_addr(r); }
    *known = 0; return 0;
}

/* ================= two-pass streaming file assembler ================== */
enum { SEC_TEXT, SEC_RODATA, SEC_DATA, SEC_BSS, NSEC };
static int secoff[NSEC];              /* running offset within each section */
static int secbase[NSEC];             /* final base address of each section */
static int secsize[NSEC];             /* sizes found by pass 1 */
static int cursec;
static int objmode;                   /* emit a.out object instead of flat bin */
static int verbose;                   /* -v: print the symbol table to stderr */

/* ---- a.out reloc/symbol constants (match tools/aoututils/include) ---- */
#define R_ABS32   0x01                /* RBYTE32: 32-bit absolute address */
#define R_PC24    0x06                /* ARM b/bl PC-relative (new for ARM) */
#define RSEG_ABS  0x00
#define RSEG_TEXT 0x20
#define RSEG_DATA 0x30
#define RSEG_BSS  0x40
#define RSEG_EXT  0x70
#define RSMASK    0x70
#define N_UNDF 0x00
#define N_TEXT 0x02
#define N_DATA 0x03
#define N_BSS  0x04
#define N_EXT  0x20
#define OBJ_RMAGIC 0406               /* relocatable object, MID=0 */

/*
 * Section output.  Pass 2 produces every section strictly in order, and
 * pass 1 has fixed the sizes, so each section's bytes go to a fixed place
 * in the output file through a small buffer.  put() is the one way bytes
 * are added (it also advances the offset in pass 1, where nothing is
 * written); bss has no file image.
 */
static FILE *outf;
static long filebase[NSEC];
static unsigned char wbuf[NSEC][WBUF];
static int wlen[NSEC];
static long wstart[NSEC];             /* section offset of wbuf[sec][0] */

static void sec_flush(int sec)
{
    if (wlen[sec] && (fseek(outf, filebase[sec] + wstart[sec], SEEK_SET) < 0 ||
        fwrite(wbuf[sec], 1, wlen[sec], outf) != (size_t)wlen[sec]))
        fatal("write error", "");
    wstart[sec] += wlen[sec];
    wlen[sec] = 0;
}

static void put(int sec, unsigned v, int n, int pass)
{
    int k;
    if (pass == 2 && sec != SEC_BSS) {
        if (wstart[sec] + wlen[sec] != secoff[sec])
            fatal("internal: section output out of order", "");
        for (k = 0; k < n; k++) {
            wbuf[sec][wlen[sec]++] = (v >> (8 * k)) & 0xFF;
            if (wlen[sec] == WBUF) sec_flush(sec);
        }
    }
    secoff[sec] += n;
}

/*
 * Relocations.  The a.out format has one record per 4-byte word of text and
 * of data (1 byte: none / local segment, 4 bytes: external symbol).  Text
 * records are produced in order during pass 2 and go to a temp file, with
 * an external's NAMED-SYMBOL INDEX in place of its final symbol number (the
 * order of undefined symbols is known only at the end); they are renumbered
 * when copied into the object.  Data and rodata share one record stream
 * (rodata follows data), so their few relocations are kept in memory.
 * As before, the first relocation for a word wins and one at an offset that
 * is not word-aligned is dropped.
 */
static FILE *trf;                     /* text relocation records */
static int trnext;                    /* text offset of the next record */
struct drel { int doff, seq, kind, ref; };
static struct drel *drels;
static int ndrel, maxdrel;

static void rec(FILE *f, int r, int kind, const int *map)
{
    if (ref_defined(r)) {
        int seg = ref_sec(r);
        fputc(((seg == SEC_TEXT) ? RSEG_TEXT : (seg == SEC_BSS) ? RSEG_BSS : RSEG_DATA) | kind, f);
    } else {
        int idx = map ? map[r] : r;
        fputc(RSEG_EXT | kind, f);
        fputc(idx & 0xff, f); fputc((idx >> 8) & 0xff, f); fputc((idx >> 16) & 0xff, f);
    }
}

static void addrel(int sec, int off, int kind, const char *name)
{
    int r = sym_ref(name, 1);         /* an external enters the table here */
    if (sec == SEC_TEXT) {
        if (off & 3 || off < trnext) return;
        while (trnext < off) { fputc(0, trf); trnext += 4; }
        rec(trf, r, kind, NULL);
        trnext = off + 4;
    } else if (sec == SEC_DATA || sec == SEC_RODATA) {
        if (ndrel == maxdrel) {
            maxdrel = maxdrel ? maxdrel * 2 : 64;
            drels = xrealloc(drels, maxdrel * sizeof(struct drel));
        }
        drels[ndrel].doff = sec == SEC_DATA ? off : secsize[SEC_DATA] + off;
        drels[ndrel].seq = ndrel;
        drels[ndrel].kind = kind;
        drels[ndrel].ref = r;
        ndrel++;
    }
}

static int sym_addr_final(const char *n, int *known)
{
    return find_label_addr(n, known);
}

static char *skipws(char *p){ while(*p==' '||*p=='\t')p++; return p; }

/*
 * Literal pools.  "ldr rd, =x" loads x from a pool slot, pc-relative.  The
 * literals since the last flush form a group that is placed at the next
 * .ltorg (or island, below).  Pass 1 records only where each group lands;
 * pass 2 knows a literal's slot from that and its index in the group, and
 * keeps the group's values until it writes them.
 */
struct pend { unsigned val; char *name; };     /* name: symbol literal */
static struct pend pend[MAXPEND];
static int npend;
static int pend_first;                /* text offset of the group's first ldr */
static int *grp_off;                  /* pass 1: where each group is placed */
static int ngrp, maxgrp, cgrp;

static void flush_pool(int pass)
{
    int k;
    if (!npend) return;
    if (pass == 1) {
        if (ngrp == maxgrp) {
            maxgrp = maxgrp ? maxgrp * 2 : 64;
            grp_off = xrealloc(grp_off, maxgrp * sizeof(int));
        }
        grp_off[ngrp++] = secoff[SEC_TEXT];
    } else
        cgrp++;
    for (k = 0; k < npend; k++) {
        unsigned v = pend[k].val;
        if (pass == 2 && pend[k].name) {
            int kn;
            v = sym_addr_final(pend[k].name, &kn);
            if (objmode) addrel(SEC_TEXT, secoff[SEC_TEXT], R_ABS32, pend[k].name);
            free(pend[k].name);
        }
        put(SEC_TEXT, v, 4, pass);
    }
    npend = 0;
}

/*
 * Literal-pool islands.  An ldr reaches its slot with a 12-bit pc offset
 * (4 KB), but the compiler emits .ltorg only at the end of each function,
 * so a big function would leave its early literals out of reach.  After an
 * unconditional "b label" (code never falls through, so data may follow),
 * place the group if its first literal is more than ISLAND bytes back.  The
 * test uses only offsets that are the same in both passes, so pass 1 and
 * pass 2 place the islands at the same spots.
 */
#define ISLAND 1024
static void maybe_island(int pass)
{
    if (npend && secoff[SEC_TEXT] - pend_first > ISLAND)
        flush_pool(pass);
}

static void data_bytes(int sz, const char *args, int pass);

static void do_line(char *raw, int pass)
{
    char line[512];
    char *p, *colon;
    strncpy(line, raw, sizeof(line)-1); line[sizeof(line)-1]=0;
    { char *nl=line+strlen(line); while(nl>line && (nl[-1]=='\n'||nl[-1]=='\r')) *--nl=0; }
    /* strip comment starting with @ (not inside a string) */
    {
        int instr=0; char *c=line;
        for(;*c;c++){ if(*c=='"')instr=!instr; else if(*c=='@'&&!instr){*c=0;break;} }
    }
    p = skipws(line);
    if (!*p) return;

    /* label(s): leading token ending in ':' */
    while ((colon = strchr(p, ':')) != NULL) {
        char *sp = p;
        /* ensure the ':' terminates a bare label (no spaces before it) */
        char *q = p; int ok=1;
        while (q<colon){ if(*q==' '||*q=='\t'||*q==','){ok=0;break;} q++; }
        if(!ok) break;
        *colon=0;
        if (pass==1) sym_define(sp, cursec, secoff[cursec]);
        p = skipws(colon+1);
        if(!*p) return;
    }

    /* directive? */
    if (*p=='.') {
        char dir[32]; int i=0;
        char *d=p+1; while(*d && !isspace((unsigned char)*d)){ if(i<31)dir[i++]=*d; d++; }
        dir[i]=0;
        char *args=skipws(d);
        if(!strcmp(dir,"text")) cursec=SEC_TEXT;
        else if(!strcmp(dir,"data")) cursec=SEC_DATA;
        else if(!strcmp(dir,"bss")) cursec=SEC_BSS;
        else if(!strcmp(dir,"section")){ if(strstr(args,"rodata"))cursec=SEC_RODATA; else if(strstr(args,"data"))cursec=SEC_DATA; else if(strstr(args,"bss"))cursec=SEC_BSS; else cursec=SEC_TEXT; }
        else if(!strcmp(dir,"syntax")||!strcmp(dir,"arm")||!strcmp(dir,"thumb")||!strcmp(dir,"file")||!strcmp(dir,"ident")||!strcmp(dir,"size")||!strcmp(dir,"type")) return;
        else if(!strcmp(dir,"globl")){
            if(pass==1){
                int v=numlabel(args);
                if(v>=0 && v<maxlab && lab_sec[v]) fatal("local label cannot be global", args);
                int k=named_get(args);   /* may move syms[] */
                syms[k].global=1;
            }
        }
        else if(!strcmp(dir,"align")){ int a=atoi(args); int m=(1<<a)-1; while(secoff[cursec]&m) put(cursec,0,1,pass); }
        else if(!strcmp(dir,"word")) data_bytes(4,args,pass);
        else if(!strcmp(dir,"short")||!strcmp(dir,"hword")) data_bytes(2,args,pass);
        else if(!strcmp(dir,"byte")) data_bytes(1,args,pass);
        else if(!strcmp(dir,"space")){ int n=atoi(args); int k; for(k=0;k<n;k++) put(cursec,0,1,pass); }
        else if(!strcmp(dir,"ascii")){ /* "..." possibly with escapes */
            char *q=strchr(args,'"'); if(q){ q++; while(*q && *q!='"'){ int ch; if(*q=='\\'){ q++; if(*q=='n')ch='\n'; else if(*q=='t')ch='\t'; else if(*q=='0'||(*q>='0'&&*q<='7')){ ch=strtol(q,&q,8); goto putch;} else ch=*q; } else ch=*q; q++; putch: put(cursec,ch,1,pass); } } }
        else if(!strcmp(dir,"ltorg")) flush_pool(pass); /* literals since last flush */
        else { /* ignore unknown directives */ }
        return;
    }

    /* instruction: mnemonic + operands */
    {
        char mnem[16]; int i=0; char *m=p;
        while(*m && !isspace((unsigned char)*m)){ if(i<15)mnem[i++]=*m; m++; }
        mnem[i]=0;
        char *ops=skipws(m);

        /* ldr rd, =value  -> becomes a pc-relative load into a pool slot */
        if((!strcmp(mnem,"ldr")) && strchr(ops,'=')){
            char *eq=strchr(ops,'='); char *comma=strchr(ops,',');
            char rds[8]; int rd; unsigned w=0;
            *comma=0; strncpy(rds,skipws(ops),7); rds[7]=0; rd=reg_num(rds);
            char *v=skipws(eq+1);
            if(npend>=MAXPEND) fatal("too many literals before a pool","");
            if(!npend) pend_first=secoff[SEC_TEXT];
            if(pass==2){
                int slot = grp_off[cgrp] + 4*npend;
                int off = (secbase[SEC_TEXT]+slot) - (secbase[SEC_TEXT]+secoff[SEC_TEXT]+8);
                if(isdigit((unsigned char)*v)||*v=='-'||*v=='+') { pend[npend].val=strtol(v,NULL,0); pend[npend].name=NULL; }
                else { char *e=v+strlen(v); while(e>v && (e[-1]==' '||e[-1]=='\t')) e--;
                       pend[npend].val=0; pend[npend].name=xrealloc(NULL,(e-v)+1);
                       memcpy(pend[npend].name,v,e-v); pend[npend].name[e-v]=0; }
                w = 0xE5900000u | (15<<16) | (rd<<12); /* ldr rd,[pc,#..] */
                if(off<0){ w=0xE5100000u | (15<<16)|(rd<<12); off=-off; }
                if(off>4095) fatal("literal pool out of range (no unconditional branch within 4 KB)", ops);
                w |= (off&0xFFF);
            }
            npend++;
            put(SEC_TEXT,w,4,pass);
            return;
        }

        /* normal instruction */
        {
            unsigned w=0;
            if(pass==2){
                char extname[64]; int extkind;
                int addr = secbase[cursec]+secoff[cursec];
                w = encode(mnem, ops, addr, extname, &extkind);
                if(objmode && extkind==1 && extname[0])
                    addrel(SEC_TEXT, secoff[SEC_TEXT], R_PC24, extname);
            }
            put(cursec,w,4,pass);
        }
        if(cursec==SEC_TEXT && !strcmp(mnem,"b")) maybe_island(pass);
    }
}

static void data_bytes(int sz, const char *args, int pass)
{
    char buf[256]; strncpy(buf,args,255); buf[255]=0;
    char *parts[32]; int np=split_ops(buf,parts,32),i;
    for(i=0;i<np;i++){
        long v=0; char *a=parts[i];
        if(isdigit((unsigned char)*a)||*a=='-'||*a=='+') v=strtol(a,NULL,0);
        else { /* symbol [+ofs] */
            char nm[64]; int j=0; char *s=a; while(*s&&*s!='+'&&*s!='-'&&*s!=' '){ if(j<63)nm[j++]=*s; s++; } nm[j]=0;
            long ofs=0; if(*s)ofs=strtol(s,NULL,0);
            if(pass==2){ int kn; v=sym_addr_final(nm,&kn)+ofs;
                if(objmode && sz==4) addrel(cursec, secoff[cursec], R_ABS32, nm); }
        }
        put(cursec,(unsigned)v,sz,pass);
    }
}

static void put4(FILE*f,unsigned v){ fputc(v&0xff,f);fputc((v>>8)&0xff,f);fputc((v>>16)&0xff,f);fputc((v>>24)&0xff,f); }

static int drel_cmp(const void *a, const void *b)
{
    const struct drel *x = a, *y = b;
    if (x->doff != y->doff) return x->doff < y->doff ? -1 : 1;
    return x->seq - y->seq;
}

static void list_syms(void)
{
    int i;
    for(i=0;i<nsym;i++) if(syms[i].defined){
        if(objmode) fprintf(stderr,"SYM %s %d sec=%d g=%d\n",syms[i].name,syms[i].addr,syms[i].sec,syms[i].global);
        else fprintf(stderr,"SYM %s %d\n",syms[i].name,syms[i].addr); }
    for(i=0;i<maxlab;i++) if(lab_sec[i]){
        if(objmode) fprintf(stderr,"SYM .L%d %d sec=%d g=0\n",i,lab_addr[i],lab_sec[i]-1);
        else fprintf(stderr,"SYM .L%d %d\n",i,lab_addr[i]); }
    for(i=0;i<nsym;i++) if(!syms[i].defined) fprintf(stderr,"UND %s\n",syms[i].name);
}

/* finish a DiscoBSD a.out relocatable object: sections are already in place */
static void write_object(void)
{
    int textsz=secsize[SEC_TEXT];
    int datasz=secsize[SEC_DATA]+secsize[SEC_RODATA];
    int datapad=(datasz+3)&~3;
    int bsssz=(secsize[SEC_BSS]+3)&~3;
    int *emitidx, no=0, i, rt=0, rd=0, st=0, c, off, p;
    FILE*f=outf;

    /* symbol order: defined globals, then undefined, each in creation order */
    emitidx=xrealloc(NULL,(nsym+1)*sizeof(int));
    for(i=0;i<nsym;i++) emitidx[i]=-1;
    for(i=0;i<nsym;i++) if(syms[i].defined&&syms[i].global) emitidx[i]=no++;
    for(i=0;i<nsym;i++) if(!syms[i].defined) emitidx[i]=no++;

    fseek(f,32+textsz+datasz,SEEK_SET);
    for(i=datasz;i<datapad;i++) fputc(0,f);

    /* text relocations: copy the temp records, renumbering externals */
    while(trnext<textsz){ fputc(0,trf); trnext+=4; }
    rewind(trf);
    while((c=getc(trf))!=EOF){
        fputc(c,f); rt++;
        if((c&RSMASK)==RSEG_EXT){
            int idx=getc(trf); idx|=getc(trf)<<8; idx|=getc(trf)<<16;
            idx=emitidx[idx];
            fputc(idx&0xff,f); fputc((idx>>8)&0xff,f); fputc((idx>>16)&0xff,f); rt+=3;
        }
    }
    while(rt&3){fputc(0,f);rt++;}

    /* data(+rodata) relocations, by data-space offset */
    if(ndrel) qsort(drels,ndrel,sizeof(struct drel),drel_cmp);
    for(off=0,p=0;off<datapad;off+=4){
        while(p<ndrel && drels[p].doff<off) p++;
        if(p<ndrel && drels[p].doff==off){
            int r=drels[p].ref;
            rec(f,r,drels[p].kind,emitidx);
            rd += ref_defined(r) ? 1 : 4;
            while(p<ndrel && drels[p].doff==off) p++;
        } else { fputc(0,f); rd++; }
    }
    while(rd&3){fputc(0,f);rd++;}

    /* symbols */
    for(i=0;i<nsym;i++) if(syms[i].defined&&syms[i].global) {
        int len=strlen(syms[i].name), seg=syms[i].sec;
        int nt=((seg==SEC_TEXT)?N_TEXT:(seg==SEC_BSS)?N_BSS:N_DATA)|N_EXT;
        fputc(len,f); fputc(nt,f); put4(f,syms[i].addr); fwrite(syms[i].name,1,len,f); st+=6+len; }
    for(i=0;i<nsym;i++) if(!syms[i].defined) {
        int len=strlen(syms[i].name);
        fputc(len,f); fputc(N_UNDF|N_EXT,f); put4(f,0); fwrite(syms[i].name,1,len,f); st+=6+len; }

    fseek(f,0,SEEK_SET);
    put4(f,OBJ_RMAGIC); put4(f,textsz); put4(f,datapad); put4(f,bsssz);
    put4(f,rt); put4(f,rd); put4(f,st); put4(f,0);
    free(emitidx);
}

static int assemble(const char *in, const char *out)
{
    FILE *f;
    char buf[512];
    int i, sec;

    for(i=0;i<NHASH;i++) hashtab[i]=-1;
    f=fopen(in,"r"); if(!f){perror(in);return 1;}
    infile=in;

    /* pass 1: sizes, labels, literal-pool placement */
    cursec=SEC_TEXT; npend=0;
    lineno=0;
    while(fgets(buf,sizeof(buf),f)){ lineno++; do_line(buf,1); }
    flush_pool(1);                    /* trailing literals at end of text */
    for(sec=0;sec<NSEC;sec++) secsize[sec]=secoff[sec];

    /* section bases; symbol addresses become final */
    if(objmode){
        /* segment-relative: text@0, data@0, rodata folded after data, bss@0 */
        secbase[SEC_TEXT]=0; secbase[SEC_DATA]=0;
        secbase[SEC_RODATA]=secsize[SEC_DATA]; secbase[SEC_BSS]=0;
        filebase[SEC_TEXT]=32;
        filebase[SEC_DATA]=32+secsize[SEC_TEXT];
        filebase[SEC_RODATA]=32+secsize[SEC_TEXT]+secsize[SEC_DATA];
    } else {
        secbase[SEC_TEXT]=0;
        secbase[SEC_RODATA]=secbase[SEC_TEXT]+secsize[SEC_TEXT];
        secbase[SEC_DATA]=secbase[SEC_RODATA]+secsize[SEC_RODATA];
        secbase[SEC_BSS]=secbase[SEC_DATA]+secsize[SEC_DATA];
        filebase[SEC_TEXT]=0;
        filebase[SEC_RODATA]=secsize[SEC_TEXT];
        filebase[SEC_DATA]=secsize[SEC_TEXT]+secsize[SEC_RODATA];
    }
    for(i=0;i<nsym;i++) if(syms[i].defined) syms[i].addr += secbase[syms[i].sec];
    for(i=0;i<maxlab;i++) if(lab_sec[i]) lab_addr[i] += secbase[lab_sec[i]-1];

    /* pass 2: emit */
    outf=fopen(out,"w+"); if(!outf){perror(out);return 1;}
    if(objmode){
        tmpname=xrealloc(NULL,strlen(out)+6);
        strcpy(tmpname,out); strcat(tmpname,".rtmp");
        trf=fopen(tmpname,"w+"); if(!trf){perror(tmpname);return 1;}
    }
    memset(secoff,0,sizeof(secoff)); cursec=SEC_TEXT; npend=0; cgrp=0;
    rewind(f); lineno=0;
    while(fgets(buf,sizeof(buf),f)){ lineno++; do_line(buf,2); }
    flush_pool(2);
    fclose(f);
    for(sec=0;sec<NSEC;sec++){
        if(sec!=SEC_BSS) sec_flush(sec);
        if(secoff[sec]!=secsize[sec]) fatal("internal: pass 2 size differs from pass 1","");
    }

    if(objmode){
        write_object();
        fclose(trf); remove(tmpname); tmpname=NULL;
    } else {
        /* flat image: text, rodata, data, then bss as zeros */
        fseek(outf,secsize[SEC_TEXT]+secsize[SEC_RODATA]+secsize[SEC_DATA],SEEK_SET);
        for(i=0;i<secsize[SEC_BSS];i++) fputc(0,outf);
    }
    if(fclose(outf)!=0) fatal("write error","");
    if(verbose) list_syms();
    return 0;
}

/* ---- expression-mode entry for the oracle ---- */
static void do_expr(const char *instr)
{
    char buf[256], *m, *ops;
    char extname[64]; int extkind;
    strncpy(buf, instr, sizeof(buf)-1); buf[sizeof(buf)-1]=0;
    /* split mnemonic and operands */
    m = buf; while(*m==' '||*m=='\t')m++;
    ops = m; while(*ops && *ops!=' '&&*ops!='\t')ops++;
    if(*ops){*ops++=0;} while(*ops==' '||*ops=='\t')ops++;
    printf("%08X\n", encode(m, ops, 0, extname, &extkind));
}

int main(int argc, char **argv)
{
    const char *in=NULL, *out=NULL;
    int i, flat=0;
    for (i=1; i<argc; i++) {
        if (!strcmp(argv[i],"-e")) { do_expr(argv[i+1]?argv[i+1]:""); return 0; }
        else if (!strcmp(argv[i],"-o")) { out=argv[++i]; }
        else if (!strcmp(argv[i],"-b")) { flat=1; }   /* flat image (dev/test) */
        else if (!strcmp(argv[i],"-v")) { verbose=1; }
        else if (argv[i][0]=='-') { /* ignore other flags (e.g. from cc) */ }
        else { if (!in) in=argv[i]; else if (!out) out=argv[i]; }
    }
    if (!in)  { fprintf(stderr,"as_arm: no input file\n"); return 2; }
    if (!out) { fprintf(stderr,"as_arm: no output file (use -o)\n"); return 2; }
    objmode = !flat;                  /* default: emit an a.out object */
    return assemble(in, out);
}
