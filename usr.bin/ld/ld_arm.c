/*
 * ldarm - minimal ARM linker for the DiscoBSD/GBA toolchain.
 *
 * Reads DiscoBSD a.out relocatable objects produced by asarm (RMAGIC,
 * MID=0) and BSD ar archives of them, concatenates their text/data/bss
 * segments, resolves global symbols across objects, applies the ARM
 * relocations (R_ABS32 and R_PC24 for b/bl), and writes an OMAGIC a.out
 * executable (default) or, with -b, a flat image for the test harnesses.
 * Internal b/bl are already PC-relative (resolved by asarm, RABS here).
 *
 * Thumb interworking veneers: not needed while everything is ARM (asarm emits
 * ARM). Left as the next step for linking against a Thumb libc.
 *
 *   ldarm [-v] [-b] [-T addr] [-e entry] [-L dir] [-o out] obj.o ... [-l lib]
 * With -v prints "ENTRY <hex>", the segment sizes and "SYM <name> <hex>"
 * lines to stderr.
 *
 * Memory use.  ld_arm runs in the target user window (GBAED = 250 KB), so it
 * never holds an object in memory.  Pass 1 reads only headers and symbol
 * tables (an archive member is pulled in when it defines a symbol that is
 * still wanted, in the same order as before).  Pass 2 reads each segment and
 * its relocation records side by side, a word at a time, and writes the
 * relocated word straight to the output.  What stays in memory is the global
 * symbol table and, per object, its sizes and a map from its symbol numbers
 * to global symbols.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>	/* chmod() - a linked executable must be runnable */

#define RSMASK 0x70
#define RFMASK 0x07
#define RSEG_TEXT 0x20
#define RSEG_DATA 0x30
#define RSEG_BSS  0x40
#define RSEG_EXT  0x70
#define R_ABS32 0x01
#define R_PC24  0x06
#define RHIGH16 0x02
#define RHIGH16S 0x03
#define N_UNDF 0x00
#define N_TEXT 0x02
#define N_DATA 0x03
#define N_BSS  0x04
#define N_EXT  0x20

#define NHASH 512

static void die(const char *fmt, const char *arg)
{
    fprintf(stderr, "ld_arm: ");
    fprintf(stderr, fmt, arg);
    fprintf(stderr, "\n");
    exit(1);
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (!p) die("out of memory%s", "");
    return p;
}

/*
 * Global symbols, by name.  "defined" is set by the first object that
 * defines the name (the first definition wins, as before); "wanted" by any
 * object that has it undefined (used to pick archive members).
 */
struct gsym { char *name; unsigned addr; int next; char defined, wanted; };
static struct gsym *gsyms;
static int ngsym, maxgsym;
static int ghash[NHASH];
static int *gorder;                   /* defined symbols, in definition order */
static int ngorder;

static unsigned hashname(const char *n)
{
    unsigned h = 5381;
    while (*n) h = h * 33 + (unsigned char)*n++;
    return h % NHASH;
}

static int gsym_lookup(const char *n, int create)
{
    unsigned h = hashname(n);
    int i;
    for (i = ghash[h]; i >= 0; i = gsyms[i].next)
        if (!strcmp(gsyms[i].name, n))
            return i;
    if (!create) return -1;
    if (ngsym == maxgsym) {
        maxgsym = maxgsym ? maxgsym * 2 : 128;
        gsyms = xrealloc(gsyms, maxgsym * sizeof(struct gsym));
        gorder = xrealloc(gorder, maxgsym * sizeof(int));
    }
    i = ngsym++;
    gsyms[i].name = xrealloc(NULL, strlen(n) + 1);
    strcpy(gsyms[i].name, n);
    gsyms[i].addr = 0; gsyms[i].defined = 0; gsyms[i].wanted = 0;
    gsyms[i].next = ghash[h];
    ghash[h] = i;
    return i;
}

static int gsym_find(const char *n, unsigned *a)
{
    int i = gsym_lookup(n, 0);
    if (i < 0 || !gsyms[i].defined) return 0;
    *a = gsyms[i].addr;
    return 1;
}

static void gsym_define(int i, unsigned a)
{
    if (gsyms[i].defined) return;
    gsyms[i].defined = 1;
    gsyms[i].addr = a;
    gorder[ngorder++] = i;
}

/*
 * Objects.  An object is a file, or a member at offset "off" in an archive.
 * Symbol definitions are kept as (global, type, value) so their addresses
 * can be set once the layout is known.
 */
struct osym { int g; unsigned char type; unsigned val; };
struct obj {
    const char *file; long off;
    unsigned textsz, datasz, bsssz, rtsz, rdsz, symsz;
    unsigned toff, doff, boff;        /* offset of this object's segs in combined */
    int *map; int nsym;               /* symbol number -> global symbol */
    struct osym *defs; int ndef;      /* the symbols it defines */
};
static struct obj *objs;
static int nobj, maxobj;
static unsigned base_text, base_data, base_bss;

static unsigned rd32(unsigned char *p){ return p[0]|p[1]<<8|p[2]<<16|(unsigned)p[3]<<24; }
static void put4(FILE*f,unsigned v){ fputc(v&0xff,f);fputc((v>>8)&0xff,f);fputc((v>>16)&0xff,f);fputc((v>>24)&0xff,f); }

static FILE *xopen(const char *fn)
{
    FILE *f = fopen(fn, "r");
    if (!f) { perror(fn); exit(1); }
    return f;
}

static void xread(FILE *f, unsigned char *b, int n, const char *fn)
{
    if ((int)fread(b, 1, n, f) != n) die("%s: truncated object", fn);
}

/* read an object's header at the current position */
static void read_header(FILE *f, struct obj *o, const char *fn)
{
    unsigned char h[32];
    xread(f, h, 32, fn);
    o->textsz = rd32(h+4); o->datasz = rd32(h+8); o->bsssz = rd32(h+12);
    o->rtsz = rd32(h+16); o->rdsz = rd32(h+20); o->symsz = rd32(h+24);
}

/* read one symbol record (len, type, value, name); returns bytes used */
static int read_sym(FILE *f, char *name, int *type, unsigned *val, const char *fn)
{
    unsigned char h[6];
    int len;
    xread(f, h, 6, fn);
    len = h[0];
    *type = h[1];
    *val = rd32(h+2);
    xread(f, (unsigned char *)name, len, fn);
    name[len] = 0;
    return 6 + len;
}

/* add the object at (fn, off): read its symbols, note defs and wants */
static void add_obj(const char *fn, long off)
{
    struct obj *o;
    FILE *f;
    char name[256];
    unsigned q;
    int type, maxsym;
    unsigned val;

    if (nobj == maxobj) {
        maxobj = maxobj ? maxobj * 2 : 16;
        objs = xrealloc(objs, maxobj * sizeof(struct obj));
    }
    o = &objs[nobj++];
    o->file = fn; o->off = off;
    f = xopen(fn);
    fseek(f, off, SEEK_SET);
    read_header(f, o, fn);
    fseek(f, off + 32 + o->textsz + o->datasz + o->rtsz + o->rdsz, SEEK_SET);
    /* each record is >= 6 bytes, so symsz/6 + 1 bounds the count */
    maxsym = o->symsz / 6 + 1;
    o->map = xrealloc(NULL, maxsym * sizeof(int));
    o->defs = xrealloc(NULL, maxsym * sizeof(struct osym));
    o->nsym = 0; o->ndef = 0;
    for (q = 0; q < o->symsz; ) {
        int g;
        q += read_sym(f, name, &type, &val, fn);
        g = gsym_lookup(name, 1);
        o->map[o->nsym++] = g;
        if ((type & N_EXT) && (type & 0x1f) != N_UNDF) {
            o->defs[o->ndef].g = g; o->defs[o->ndef].type = type;
            o->defs[o->ndef].val = val; o->ndef++;
            gsyms[g].defined = 1;     /* for archive selection; address later */
        } else if ((type & 0x1f) == N_UNDF)
            gsyms[g].wanted = 1;
    }
    fclose(f);
}

/* does the member at off define a symbol that is wanted and not defined? */
static int member_defines_wanted(FILE *f, long off, const char *fn)
{
    struct obj h;
    char name[256];
    unsigned q;
    int type;
    unsigned val;
    fseek(f, off, SEEK_SET);
    read_header(f, &h, fn);
    fseek(f, off + 32 + h.textsz + h.datasz + h.rtsz + h.rdsz, SEEK_SET);
    for (q = 0; q < h.symsz; ) {
        int g;
        q += read_sym(f, name, &type, &val, fn);
        if ((type & N_EXT) && (type & 0x1f) != N_UNDF) {
            g = gsym_lookup(name, 0);
            if (g >= 0 && gsyms[g].wanted && !gsyms[g].defined) return 1;
        }
    }
    return 0;
}

/* link a BSD ar archive: pull members that satisfy undefined symbols (iterate) */
static void load_archive(const char *fn)
{
    FILE *f = xopen(fn);
    unsigned char hdr[60];
    long p = 8, *moff = NULL;
    char *loaded = NULL;
    int nm = 0, maxm = 0, i, changed;

    xread(f, hdr, 8, fn);
    if (memcmp(hdr, "!<arch>\n", 8)) die("%s: not an archive", fn);
    while (fseek(f, p, SEEK_SET) == 0 && fread(hdr, 1, 60, f) == 60) {
        char sf[11]; long size;
        memcpy(sf, hdr+48, 10); sf[10] = 0;
        size = atol(sf);
        if (!(hdr[0] == '/' || hdr[0] == ' ' || !memcmp(hdr, "__.SYMDEF", 9))) {
            if (nm == maxm) {
                maxm = maxm ? maxm * 2 : 16;
                moff = xrealloc(moff, maxm * sizeof(long));
            }
            moff[nm++] = p + 60;
        }
        p += 60 + size + (size & 1);
    }
    loaded = xrealloc(NULL, nm + 1);
    for (i = 0; i < nm; i++) loaded[i] = 0;
    changed = 1;
    while (changed) {
        changed = 0;
        for (i = 0; i < nm; i++) {
            if (loaded[i]) continue;
            if (member_defines_wanted(f, moff[i], fn)) {
                add_obj(fn, moff[i]);
                loaded[i] = 1;
                changed = 1;
            }
        }
    }
    fclose(f);
    free(moff);
    free(loaded);
}

/*
 * Pass 2: copy one segment (size sz at file offset segat) to the output,
 * applying its per-word relocation records (at relat).  A record is one
 * byte (0 = none, else segment|format), plus a 3-byte symbol number for an
 * external and 2 more bytes for the RHIGH16 formats.
 */
static void copy_seg(struct obj *o, FILE *out, long segat, long relat,
                     unsigned sz, unsigned seg_base_addr)
{
    FILE *fs = xopen(o->file), *fr = xopen(o->file);
    unsigned off, pad;
    fseek(fs, segat, SEEK_SET);
    fseek(fr, relat, SEEK_SET);
    for (off = 0; off < sz; off += 4) {
        unsigned char w[4];
        int n = sz - off < 4 ? sz - off : 4, k;
        int flags, rseg, fmt, idx = 0;
        unsigned val, target = 0;

        for (k = 0; k < 4; k++) w[k] = 0;
        xread(fs, w, n, o->file);
        flags = getc(fr);
        if (flags == EOF) die("%s: truncated relocations", o->file);
        rseg = flags & RSMASK;
        fmt = flags & RFMASK;
        if (rseg == RSEG_EXT) { idx = getc(fr); idx |= getc(fr) << 8; idx |= getc(fr) << 16; }
        if (fmt == RHIGH16 || fmt == RHIGH16S) { getc(fr); getc(fr); }
        if (flags) {
            val = rd32(w);
            if (rseg == RSEG_EXT) {
                if (idx >= o->nsym || !gsyms[o->map[idx]].defined)
                    die("undefined symbol: %s", idx < o->nsym ? gsyms[o->map[idx]].name : "?");
                target = gsyms[o->map[idx]].addr;
            } else
                target = (rseg == RSEG_TEXT) ? base_text + o->toff
                       : (rseg == RSEG_BSS) ? base_bss + o->boff
                       : base_data + o->doff;   /* add to stored offset */
            if (fmt == R_ABS32)
                val = val + target;   /* val = addend or segment-relative offset */
            else if (fmt == R_PC24) {
                unsigned P = seg_base_addr + off;   /* final addr of this insn */
                int delta = (int)(target - (P + 8)) >> 2;
                val = (val & 0xFF000000u) | (delta & 0x00FFFFFF);
            }
            w[0] = val; w[1] = val >> 8; w[2] = val >> 16; w[3] = val >> 24;
        }
        fwrite(w, 1, n, out);
    }
    for (pad = ((sz + 3) & ~3) - sz; pad > 0; pad--) fputc(0, out);
    fclose(fs);
    fclose(fr);
}

int main(int argc,char**argv)
{
    unsigned Ttext=0x10000; const char*entry="_start"; const char*out="a.out";
    int i, verbose=0, omagic=1, tset=0;
    const char* libdir[32]; int nld=0;
    const char* larch[32]; int nla=0;
    for(i=0;i<NHASH;i++) ghash[i]=-1;
    for(i=1;i<argc;i++){
        if(!strncmp(argv[i],"-Ttext=",7)){ Ttext=strtoul(argv[i]+7,0,0); tset=1; }
        else if(!strcmp(argv[i],"-T")){ Ttext=strtoul(argv[++i],0,0); tset=1; }
        else if(!strcmp(argv[i],"-e")) entry=argv[++i];
        else if(!strcmp(argv[i],"-o")) out=argv[++i];
        else if(!strcmp(argv[i],"-v")) verbose=1;
        else if(!strcmp(argv[i],"-b")) omagic=0;          /* flat image (dev/Unicorn) */
        else if(!strcmp(argv[i],"-A")) omagic=1;          /* a.out OMAGIC executable */
        else if(!strncmp(argv[i],"-L",2)){ if(nld<31) libdir[nld++] = argv[i][2]?argv[i]+2:argv[++i]; }
        else if(!strncmp(argv[i],"-l",2)){ if(nla<32) larch[nla++]  = argv[i][2]?argv[i]+2:argv[++i]; }
        else if(argv[i][0]=='-') { /* ignore other flags (e.g. -X, -N from cc) */ }
        else add_obj(argv[i], 0);
    }
    libdir[nld++] = "/usr/lib";   /* default system library path (searched after any -L) */
    /* pull in -l archives after all explicit objects (on-demand member linking) */
    { int j,d; for(j=0;j<nla;j++){
        char *path=xrealloc(NULL,512); int found=0;
        for(d=0;d<nld;d++){ FILE*t; snprintf(path,512,"%s/lib%s.a",libdir[d],larch[j]);
            t=fopen(path,"r"); if(t){ fclose(t); found=1; break; } }
        if(!found) snprintf(path,512,"lib%s.a",larch[j]);
        load_archive(path);           /* path is kept: objects refer to it */
    } }
    /* OMAGIC executables load at the GBA user base unless -T given */
    if(omagic && !tset) Ttext=0x02001800;

    /* layout: text region, then data, then bss */
    { unsigned t=0,d=0,b=0,ea=0;
      for(i=0;i<nobj;i++){ objs[i].toff=t; objs[i].doff=d; objs[i].boff=b;
        t+=(objs[i].textsz+3)&~3; d+=(objs[i].datasz+3)&~3; b+=(objs[i].bsssz+3)&~3; }
      base_text=Ttext; base_data=Ttext+t; base_bss=Ttext+t+d;

      /* global symbol addresses: the first definition of a name wins */
      for(i=0;i<ngsym;i++) gsyms[i].defined=0;
      for(i=0;i<nobj;i++){ int k; for(k=0;k<objs[i].ndef;k++){ struct osym*s=&objs[i].defs[k];
          unsigned a = (((s->type&0x1f)==N_TEXT)?base_text+objs[i].toff
                      :((s->type&0x1f)==N_BSS)?base_bss+objs[i].boff
                      :base_data+objs[i].doff) + s->val;
          gsym_define(s->g,a);
      } }
      /* linker-defined symbols (end of segments) if not defined */
      gsym_define(gsym_lookup("_end",1),   base_bss+b);
      gsym_define(gsym_lookup("end",1),    base_bss+b);
      gsym_define(gsym_lookup("_edata",1), base_bss);
      gsym_define(gsym_lookup("edata",1),  base_bss);
      gsym_define(gsym_lookup("_etext",1), base_data);
      gsym_define(gsym_lookup("etext",1),  base_data);

      if(!gsym_find(entry,&ea)) fprintf(stderr,"ld_arm: no entry symbol %s\n",entry);
      {
        FILE*of=fopen(out,"w"); if(!of){perror(out);return 1;}
        if(omagic){
          /* DiscoBSD a.out OMAGIC executable: 8-word header, then text+data.
             a_midmag = OMAGIC(0407) | MID_ZERO(0) = 0x00000107. bss implied. */
          put4(of,0x00000107); put4(of,t); put4(of,d); put4(of,b);
          put4(of,0); put4(of,0); put4(of,0); put4(of,ea);   /* reltext,reldata,syms,entry */
        }
        for(i=0;i<nobj;i++){ struct obj*o=&objs[i]; long seg=o->off+32;
          copy_seg(o,of,seg,seg+o->textsz+o->datasz,o->textsz,base_text+o->toff); }
        for(i=0;i<nobj;i++){ struct obj*o=&objs[i]; long seg=o->off+32+o->textsz;
          copy_seg(o,of,seg,seg+o->datasz+o->rtsz,o->datasz,base_data+o->doff); }
        if(!omagic){ unsigned k; for(k=0;k<b;k++) fputc(0,of); }  /* flat: zero bss */
        if(fclose(of)!=0){ perror(out); return 1; }
        /* An OMAGIC executable must be runnable; fopen() leaves it 0644.
           (Relocatable object output, -r, is left non-executable.) */
        if(omagic) chmod(out, 0755);
      }
      if(verbose) fprintf(stderr,"ENTRY %x\n",ea);
      if(verbose){ fprintf(stderr,"a_text=%u a_data=%u a_bss=%u\n",t,d,b);
        for(i=0;i<ngorder;i++) fprintf(stderr,"SYM %s %x\n",gsyms[gorder[i]].name,gsyms[gorder[i]].addr); }
    }
    return 0;
}
