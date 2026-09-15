/*
 * asarm - minimal ARM (ARMv4T) assembler for the DiscoBSD/GBA toolchain.
 *
 * Development prototype (Phase 1b). Parses the exact assembly subset that
 * usr.bin/smlrc's cgarm.c emits and encodes it to ARM machine code. Here it
 * runs as a host tool producing a flat .text image (internal labels + literal
 * pools resolved; externals listed) so the encoder can be validated
 * byte-for-byte against arm-none-eabi-as and functionally in Unicorn. Once
 * solid, the encoder moves into usr.bin/as (+ tools/aoututils/as) with the
 * a.out object framing.
 *
 * Two modes:
 *   asarm -e "<one instruction>"   -> print the 8-hex-digit encoding (oracle)
 *   asarm <file.s> <out.bin>       -> assemble; write flat .text; report syms
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAXSYM   4096
#define MAXLIT   4096
#define TEXTMAX  (1<<20)

/* symbols */
struct sym { char name[64]; int addr; int defined; int global; };
static struct sym syms[MAXSYM];
static int nsym;

static const char *infile = "<expr>";
static int lineno;

static void fatal(const char *msg, const char *arg)
{
    fprintf(stderr, "asarm: %s:%d: %s%s%s\n", infile, lineno, msg,
            arg ? " " : "", arg ? arg : "");
    exit(1);
}

/* ---- symbol table ---- */
static struct sym *sym_find(const char *n)
{
    int i;
    for (i = 0; i < nsym; i++)
        if (!strcmp(syms[i].name, n))
            return &syms[i];
    return NULL;
}
static struct sym *sym_get(const char *n)
{
    struct sym *s = sym_find(n);
    if (s) return s;
    if (nsym >= MAXSYM) fatal("too many symbols", n);
    s = &syms[nsym++];
    strncpy(s->name, n, sizeof(s->name)-1);
    s->name[sizeof(s->name)-1] = 0;
    s->addr = 0; s->defined = 0; s->global = 0;
    return s;
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
   does not fit. */
static int enc_imm8r(unsigned v)
{
    int i;
    for (i = 0; i < 16; i++) {
        unsigned r = i ? ((v << (2*i)) | (v >> (32-2*i))) : v;
        if ((r & 0xFFFFFF00u) == 0)
            return (i ? (16 - i) : 0) << 8 | (r & 0xFF);
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
    struct sym *s = sym_find(n);
    if (s && s->defined) { *known = 1; return s->addr; }
    *known = 0; return 0;
}

/* ================= full two-pass file assembler ==================== */
enum { SEC_TEXT, SEC_RODATA, SEC_DATA, SEC_BSS, NSEC };
static int secoff[NSEC];              /* running offset within each section */
static int secbase[NSEC];             /* final base address of each section */
static unsigned char secbuf[NSEC][TEXTMAX];

/* per-section: store section id in the symbol */
struct symx { int sec; };             /* parallel to syms[] */
static struct symx symx[MAXSYM];

/* literal pool: each ldr= in source order gets a slot (text-local offset). */
struct pool { unsigned val; char sym[64]; int issym; int slotoff; int emitted; };
static struct pool pool[MAXLIT];
static int npool;                     /* total ldr= seen */
static int poolflush;                 /* index of first not-yet-placed entry */

static char *lines[65536];
static int nlines;

static int cursec;
static int objmode;                   /* emit a.out object instead of flat bin */

/* ---- a.out reloc/symbol constants (match tools/aoututils/include) ---- */
#define R_ABS32   0x01                /* RBYTE32: 32-bit absolute address */
#define R_PC24    0x06                /* ARM b/bl PC-relative (new for ARM) */
#define RSEG_ABS  0x00
#define RSEG_TEXT 0x20
#define RSEG_DATA 0x30
#define RSEG_BSS  0x40
#define RSEG_EXT  0x70
#define N_UNDF 0x00
#define N_TEXT 0x02
#define N_DATA 0x03
#define N_BSS  0x04
#define N_EXT  0x20
#define OBJ_RMAGIC 0406               /* relocatable object, MID=0 */

/* relocations recorded in pass 2 (object mode) */
struct areloc { int sec; int off; int kind; char sym[64]; };
static struct areloc rels[MAXLIT*4];
static int nrel;
static struct sym *sym_get(const char *n);   /* fwd */
static void addrel(int sec, int off, int kind, const char *sym)
{
    sym_get(sym);   /* ensure the referenced symbol is in the table (may be
                       an undefined external that must land in the symtab) */
    rels[nrel].sec=sec; rels[nrel].off=off; rels[nrel].kind=kind;
    strncpy(rels[nrel].sym,sym,63); rels[nrel].sym[63]=0; nrel++;
}

/* resolve a symbol name to a final address (pass 2). *known set. */
static int sym_addr_final(const char *n, int *known)
{
    struct sym *s = sym_find(n);
    if (s && s->defined) { *known=1; return s->addr; }
    *known=0; return 0;
}

/* trim leading blanks */
static char *skipws(char *p){ while(*p==' '||*p=='\t')p++; return p; }

/* is this token a directive/label/instruction? dispatch in a pass. */
/* pass: 1 = size/labels, 2 = emit. */
static int curldr;   /* running index into pool[] during a pass */

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
        if (pass==1) {
            struct sym *s=sym_get(sp);
            s->defined=1; s->addr=secoff[cursec]; symx[s-syms].sec=cursec;
        }
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
        else if(!strcmp(dir,"globl")){ if(pass==1){ struct sym*s=sym_get(args); s->global=1; } }
        else if(!strcmp(dir,"align")){ int a=atoi(args); int m=(1<<a)-1; while(secoff[cursec]&m){ if(pass==2)secbuf[cursec][secoff[cursec]]=0; secoff[cursec]++; } }
        else if(!strcmp(dir,"word")) data_bytes(4,args,pass);
        else if(!strcmp(dir,"short")||!strcmp(dir,"hword")) data_bytes(2,args,pass);
        else if(!strcmp(dir,"byte")) data_bytes(1,args,pass);
        else if(!strcmp(dir,"space")){ int n=atoi(args); int k; for(k=0;k<n;k++){ if(pass==2)secbuf[cursec][secoff[cursec]]=0; secoff[cursec]++; } }
        else if(!strcmp(dir,"ascii")){ /* "..." possibly with escapes */
            char *q=strchr(args,'"'); if(q){ q++; while(*q && *q!='"'){ int ch; if(*q=='\\'){ q++; if(*q=='n')ch='\n'; else if(*q=='t')ch='\t'; else if(*q=='0'||(*q>='0'&&*q<='7')){ ch=strtol(q,&q,8); goto put;} else ch=*q; } else ch=*q; q++; put: if(pass==2)secbuf[cursec][secoff[cursec]]=ch; secoff[cursec]++; } } }
        else if(!strcmp(dir,"ltorg")){ /* flush literals seen since last .ltorg */
            int k;
            for(k=poolflush;k<curldr;k++){ pool[k].slotoff=secoff[SEC_TEXT]; secoff[SEC_TEXT]+=4; }
            if(pass==2){ for(k=poolflush;k<curldr;k++){ unsigned v=pool[k].val; if(pool[k].issym){int kn; v=sym_addr_final(pool[k].sym,&kn); if(objmode)addrel(SEC_TEXT,pool[k].slotoff,R_ABS32,pool[k].sym);} unsigned char*b=&secbuf[SEC_TEXT][pool[k].slotoff]; b[0]=v;b[1]=v>>8;b[2]=v>>16;b[3]=v>>24; } }
            poolflush=curldr;
        }
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
            char rds[8]; int rd; long val=0; int issym=0; char symn[64];
            *comma=0; strncpy(rds,skipws(ops),7); rds[7]=0; rd=reg_num(rds);
            char *v=skipws(eq+1);
            if(isdigit((unsigned char)*v)||*v=='-'||*v=='+') { val=strtol(v,NULL,0); issym=0; }
            else { issym=1; strncpy(symn,v,63); symn[63]=0; }
            if(pass==1){ pool[npool].val=val; pool[npool].issym=issym; if(issym)strcpy(pool[npool].sym,symn); npool++; }
            {
                int slot = pool[curldr].slotoff;   /* known in pass2 (set at ltorg in pass1) */
                if(pass==2){
                    int instr_addr = secbase[SEC_TEXT]+secoff[SEC_TEXT];
                    int slot_addr  = secbase[SEC_TEXT]+slot;
                    int off = slot_addr - (instr_addr+8);
                    unsigned w = 0xE5900000u | (15<<16) | (rd<<12); /* ldr rd,[pc,#..] */
                    if(off<0){ w=0xE5100000u | (15<<16)|(rd<<12); off=-off; }
                    w |= (off&0xFFF);
                    unsigned char*b=&secbuf[SEC_TEXT][secoff[SEC_TEXT]]; b[0]=w;b[1]=w>>8;b[2]=w>>16;b[3]=w>>24;
                }
                curldr++;
                secoff[SEC_TEXT]+=4;
            }
            return;
        }

        /* normal instruction */
        if(pass==2){
            char extname[64]; int extkind;
            int addr = secbase[cursec]+secoff[cursec];
            unsigned w = encode(mnem, ops, addr, extname, &extkind);
            unsigned char*b=&secbuf[cursec][secoff[cursec]]; b[0]=w;b[1]=w>>8;b[2]=w>>16;b[3]=w>>24;
            if(objmode && extkind==1 && extname[0])
                addrel(SEC_TEXT, secoff[SEC_TEXT], R_PC24, extname);
        }
        secoff[cursec]+=4;
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
        if(pass==2){ unsigned char*b=&secbuf[cursec][secoff[cursec]]; int k; for(k=0;k<sz;k++)b[k]=(v>>(8*k))&0xFF; }
        secoff[cursec]+=sz;
    }
}

static void put4(FILE*f,unsigned v){ fputc(v&0xff,f);fputc((v>>8)&0xff,f);fputc((v>>16)&0xff,f);fputc((v>>24)&0xff,f); }

static int emitidx[MAXSYM];   /* symtab emit-index per syms[] entry */

/* find a reloc at a given segment offset; is_text selects TEXT vs DATA space.
   Returns the rels[] index or -1. */
static int find_reloc(int is_text, int segoff)
{
    int i, datasz=secoff[SEC_DATA];
    for(i=0;i<nrel;i++){
        if(is_text){ if(rels[i].sec==SEC_TEXT && rels[i].off==segoff) return i; }
        else {
            int doff = (rels[i].sec==SEC_DATA)?rels[i].off : (rels[i].sec==SEC_RODATA? datasz+rels[i].off : -1);
            if(doff==segoff) return i;
        }
    }
    return -1;
}

/* emit the per-word reloc record for one word; return bytes written */
static int emit_word_reloc(FILE*f, int is_text, int segoff)
{
    int ri=find_reloc(is_text,segoff);
    if(ri<0){ fputc(0x00,f); return 1; }            /* RABS */
    {
        struct sym*s=sym_find(rels[ri].sym);
        int kind=rels[ri].kind;
        if(s && s->defined){
            int seg = symx[s-syms].sec;
            int sf = (seg==SEC_TEXT)?RSEG_TEXT : (seg==SEC_BSS)?RSEG_BSS : RSEG_DATA;
            fputc(sf|kind,f); return 1;
        } else {
            int idx = s?emitidx[s-syms]:0;
            fputc(RSEG_EXT|kind,f);
            fputc(idx&0xff,f); fputc((idx>>8)&0xff,f); fputc((idx>>16)&0xff,f);
            return 4;
        }
    }
}

/* write a DiscoBSD a.out relocatable object */
static void write_object(const char*out)
{
    int textsz=secoff[SEC_TEXT];
    int datasz=secoff[SEC_DATA]+secoff[SEC_RODATA];
    int datapad=(datasz+3)&~3;
    int bsssz=(secoff[SEC_BSS]+3)&~3;
    int order[MAXSYM], no=0, i, off;
    FILE*f=fopen(out,"wb"); if(!f){perror(out);exit(1);}

    for(i=0;i<nsym;i++) emitidx[i]=-1;
    for(i=0;i<nsym;i++) if(syms[i].defined&&syms[i].global){ emitidx[i]=no; order[no++]=i; }
    for(i=0;i<nsym;i++) if(!syms[i].defined){ emitidx[i]=no; order[no++]=i; }

    { int k; for(k=0;k<32;k++) fputc(0,f); }                 /* header placeholder */
    fwrite(secbuf[SEC_TEXT],1,textsz,f);
    fwrite(secbuf[SEC_DATA],1,secoff[SEC_DATA],f);
    fwrite(secbuf[SEC_RODATA],1,secoff[SEC_RODATA],f);
    { for(i=datasz;i<datapad;i++) fputc(0,f); }
    { int rt=0; for(off=0;off<textsz;off+=4) rt+=emit_word_reloc(f,1,off); while(rt&3){fputc(0,f);rt++;}
      { int rd=0; for(off=0;off<datapad;off+=4) rd+=emit_word_reloc(f,0,off); while(rd&3){fputc(0,f);rd++;}
        { int st=0, j;
          for(j=0;j<no;j++){ i=order[j]; int len=strlen(syms[i].name);
            int seg=symx[i].sec, nt;
            if(!syms[i].defined) nt=N_UNDF|N_EXT;
            else { nt=(seg==SEC_TEXT)?N_TEXT:(seg==SEC_BSS)?N_BSS:N_DATA; if(syms[i].global)nt|=N_EXT; }
            fputc(len,f); fputc(nt,f); put4(f,syms[i].defined?syms[i].addr:0);
            fwrite(syms[i].name,1,len,f); st+=1+1+4+len; }
          /* header */
          fseek(f,0,SEEK_SET);
          put4(f,OBJ_RMAGIC); put4(f,textsz); put4(f,datapad); put4(f,bsssz);
          put4(f,rt); put4(f,rd); put4(f,st); put4(f,0);
        }
      }
    }
    fclose(f);
    for(i=0;i<nsym;i++) if(syms[i].defined) fprintf(stderr,"SYM %s %d sec=%d g=%d\n",syms[i].name,syms[i].addr,symx[i].sec,syms[i].global);
    for(i=0;i<nsym;i++) if(!syms[i].defined) fprintf(stderr,"UND %s\n",syms[i].name);
}

static int assemble(const char *in, const char *out)
{
    FILE *f=fopen(in,"r"); if(!f){perror(in);return 1;}
    char buf[512];
    infile=in;
    while(fgets(buf,sizeof(buf),f)){ lines[nlines]=strdup(buf); if(++nlines>=65536)break; }
    fclose(f);

    /* pass 1: sizes, labels, pool slot offsets */
    cursec=SEC_TEXT; curldr=0; poolflush=0; npool=0;
    { int i; for(i=0;i<nlines;i++){ lineno=i+1; do_line(lines[i],1); } }
    /* flush any trailing pool entries at end of text */
    { int k; for(k=poolflush;k<curldr;k++){ pool[k].slotoff=secoff[SEC_TEXT]; secoff[SEC_TEXT]+=4; } poolflush=curldr; }

    /* compute section bases and finalize symbol addresses */
    if(objmode){
        /* segment-relative: text@0, data@0, rodata folded after data, bss@0 */
        secbase[SEC_TEXT]=0; secbase[SEC_DATA]=0;
        secbase[SEC_RODATA]=secoff[SEC_DATA]; secbase[SEC_BSS]=0;
    } else {
        secbase[SEC_TEXT]=0;
        secbase[SEC_RODATA]=secbase[SEC_TEXT]+secoff[SEC_TEXT];
        secbase[SEC_DATA]=secbase[SEC_RODATA]+secoff[SEC_RODATA];
        secbase[SEC_BSS]=secbase[SEC_DATA]+secoff[SEC_DATA];
    }
    { int i; for(i=0;i<nsym;i++) if(syms[i].defined) syms[i].addr += secbase[symx[i].sec]; }

    /* pass 2: emit */
    { int i;
      memset(secoff,0,sizeof(secoff)); cursec=SEC_TEXT; curldr=0; poolflush=0;
      for(i=0;i<nlines;i++){ lineno=i+1; do_line(lines[i],2); }
      { int k; for(k=poolflush;k<curldr;k++){ unsigned v=pool[k].val; if(pool[k].issym){int kn;v=sym_addr_final(pool[k].sym,&kn); if(objmode)addrel(SEC_TEXT,pool[k].slotoff,R_ABS32,pool[k].sym);} unsigned char*b=&secbuf[SEC_TEXT][pool[k].slotoff]; b[0]=v;b[1]=v>>8;b[2]=v>>16;b[3]=v>>24; } }
    }

    if(objmode){ write_object(out); return 0; }

    /* flat mode: concatenate text+rodata+data (bss appended as zeros) into out */
    {
        FILE *of=fopen(out,"wb"); if(!of){perror(out);return 1;}
        fwrite(secbuf[SEC_TEXT],1,secoff[SEC_TEXT],of);
        fwrite(secbuf[SEC_RODATA],1,secoff[SEC_RODATA],of);
        fwrite(secbuf[SEC_DATA],1,secoff[SEC_DATA],of);
        { int k; for(k=0;k<secoff[SEC_BSS];k++) fputc(0,of); }
        fclose(of);
    }
    /* report symbols to stderr for the harness */
    { int i; for(i=0;i<nsym;i++) if(syms[i].defined) fprintf(stderr,"SYM %s %d\n",syms[i].name,syms[i].addr);
      for(i=0;i<nsym;i++) if(!syms[i].defined) fprintf(stderr,"UND %s\n",syms[i].name); }
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
        else if (argv[i][0]=='-') { /* ignore other flags (e.g. from cc) */ }
        else { if (!in) in=argv[i]; else if (!out) out=argv[i]; }
    }
    if (!in)  { fprintf(stderr,"as_arm: no input file\n"); return 2; }
    if (!out) { fprintf(stderr,"as_arm: no output file (use -o)\n"); return 2; }
    objmode = !flat;                  /* default: emit an a.out object */
    return assemble(in, out);
}
