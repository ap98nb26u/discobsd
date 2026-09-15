/*
 * ldarm - minimal ARM linker for the DiscoBSD/GBA toolchain (Phase 1c).
 *
 * Development prototype. Reads DiscoBSD a.out relocatable objects produced by
 * asarm (RMAGIC, MID=0), concatenates their text/data/bss segments, resolves
 * global symbols across objects, applies the ARM relocations (R_ABS32 and the
 * new R_PC24 for b/bl), and writes a flat linked image plus a symbol map.
 * Internal b/bl are already PC-relative (resolved by asarm, RABS here).
 *
 * Thumb interworking veneers: not needed while everything is ARM (asarm emits
 * ARM). Left as the next step for linking against a Thumb libc.
 *
 *   ldarm -Ttext=<hex> -e <entry> -o <out.bin> obj1.o obj2.o ...
 * Prints "ENTRY <hex>" and "SYM <name> <hex>" lines to stderr.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define MAXOBJ 64
#define MAXSYM 8192

struct osym { char name[64]; int type; unsigned val; };
struct obj {
    unsigned char *buf; long len;
    unsigned magic,textsz,datasz,bsssz,rtsz,rdsz,symsz;
    unsigned char *text,*data,*treloc,*dreloc,*sym;
    struct osym syms[2048]; int nsym;
    unsigned toff,doff,boff;      /* offset of this object's segs in combined */
};
static struct obj objs[MAXOBJ]; static int nobj;

/* global symbol table (defined globals from all objects) */
struct gsym { char name[64]; unsigned addr; };
static struct gsym gsyms[MAXSYM]; static int ngsym;

static unsigned base_text, base_data, base_bss;

static unsigned rd32(unsigned char*p){ return p[0]|p[1]<<8|p[2]<<16|(unsigned)p[3]<<24; }
static void wr32(unsigned char*p,unsigned v){ p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24; }
static void put4(FILE*f,unsigned v){ fputc(v&0xff,f);fputc((v>>8)&0xff,f);fputc((v>>16)&0xff,f);fputc((v>>24)&0xff,f); }

static void load_obj(const char*fn)
{
    FILE*f=fopen(fn,"rb"); if(!f){perror(fn);exit(1);}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    struct obj*o=&objs[nobj++];
    o->buf=malloc(n); o->len=n; fread(o->buf,1,n,f); fclose(f);
    unsigned*h=(unsigned*)o->buf;
    o->magic=rd32(o->buf+0); o->textsz=rd32(o->buf+4); o->datasz=rd32(o->buf+8);
    o->bsssz=rd32(o->buf+12); o->rtsz=rd32(o->buf+16); o->rdsz=rd32(o->buf+20);
    o->symsz=rd32(o->buf+24); (void)h;
    unsigned p=32;
    o->text=o->buf+p; p+=o->textsz;
    o->data=o->buf+p; p+=o->datasz;
    o->treloc=o->buf+p; p+=o->rtsz;
    o->dreloc=o->buf+p; p+=o->rdsz;
    o->sym=o->buf+p; p+=o->symsz;
    /* parse symtab: n_len(1) n_type(1) n_value(4) name */
    { unsigned q=0; o->nsym=0;
      while(q<o->symsz){ int len=o->sym[q++]; int typ=o->sym[q++]; unsigned val=rd32(o->sym+q); q+=4;
        struct osym*s=&o->syms[o->nsym++]; memcpy(s->name,o->sym+q,len); s->name[len]=0; q+=len;
        s->type=typ; s->val=val; }
    }
}

static void gsym_add(const char*n,unsigned a){ strncpy(gsyms[ngsym].name,n,63); gsyms[ngsym].addr=a; ngsym++; }
static int gsym_find(const char*n,unsigned*a){ int i; for(i=0;i<ngsym;i++) if(!strcmp(gsyms[i].name,n)){*a=gsyms[i].addr;return 1;} return 0; }

/* apply one segment's per-word relocations */
static void relocate(struct obj*o, unsigned char*seg, unsigned segsz,
                     unsigned char*rel, unsigned char*rel_end,
                     unsigned seg_base_addr)
{
    unsigned off;
    unsigned char*rp=rel;
    for(off=0; off<segsz; off+=4){
        int flags = *rp++;
        int rseg = flags & RSMASK;
        int fmt  = flags & RFMASK;
        int idx=0;
        if(rseg==RSEG_EXT){ idx=rp[0]|rp[1]<<8|rp[2]<<16; rp+=3; }
        if(fmt==RHIGH16||fmt==RHIGH16S){ rp+=2; }
        if(flags==0) continue;                          /* RABS */
        unsigned char*w=seg+off;
        unsigned val=rd32(w);
        unsigned target=0;
        if(rseg==RSEG_EXT){
            const char*nm=o->syms[idx].name;
            if(!gsym_find(nm,&target)){ fprintf(stderr,"ldarm: undefined symbol: %s\n",nm); exit(1); }
        } else {
            unsigned segbase = (rseg==RSEG_TEXT)?(base_text+o->toff)
                             : (rseg==RSEG_BSS)?(base_bss+o->boff)
                             : (base_data+o->doff);
            target=segbase;                             /* add to stored offset */
        }
        if(fmt==R_ABS32){
            if(rseg==RSEG_EXT) wr32(w, val+target);     /* val = addend */
            else               wr32(w, val+target);     /* val = seg-rel offset */
        } else if(fmt==R_PC24){
            unsigned P = seg_base_addr + off;           /* final addr of this insn */
            int delta = (int)(target - (P+8)) >> 2;
            unsigned nw = (val & 0xFF000000u) | (delta & 0x00FFFFFF);
            wr32(w, nw);
        }
        (void)rel_end;
    }
}

int main(int argc,char**argv)
{
    unsigned Ttext=0x10000; const char*entry="_start"; const char*out="a.out";
    int i, verbose=0, omagic=1, tset=0;
    for(i=1;i<argc;i++){
        if(!strncmp(argv[i],"-Ttext=",7)){ Ttext=strtoul(argv[i]+7,0,0); tset=1; }
        else if(!strcmp(argv[i],"-T")){ Ttext=strtoul(argv[++i],0,0); tset=1; }
        else if(!strcmp(argv[i],"-e")) entry=argv[++i];
        else if(!strcmp(argv[i],"-o")) out=argv[++i];
        else if(!strcmp(argv[i],"-v")) verbose=1;
        else if(!strcmp(argv[i],"-b")) omagic=0;          /* flat image (dev/Unicorn) */
        else if(!strcmp(argv[i],"-A")) omagic=1;          /* a.out OMAGIC executable */
        else if(argv[i][0]=='-') { /* ignore other flags (e.g. -X, -N from cc) */ }
        else load_obj(argv[i]);
    }
    /* OMAGIC executables load at the GBA user base unless -T given */
    if(omagic && !tset) Ttext=0x02001800;
    /* layout: text region, then data, then bss */
    { unsigned t=0,d=0,b=0; for(i=0;i<nobj;i++){ objs[i].toff=t; objs[i].doff=d; objs[i].boff=b;
        t+=(objs[i].textsz+3)&~3; d+=(objs[i].datasz+3)&~3; b+=(objs[i].bsssz+3)&~3; }
      base_text=Ttext; base_data=Ttext+t; base_bss=Ttext+t+d;
      /* build global symbol table */
      for(i=0;i<nobj;i++){ int k; for(k=0;k<objs[i].nsym;k++){ struct osym*s=&objs[i].syms[k];
        if((s->type&N_EXT) && (s->type&0x1f)!=N_UNDF){
            unsigned a = (((s->type&0x1f)==N_TEXT)?base_text+objs[i].toff
                        :((s->type&0x1f)==N_BSS)?base_bss+objs[i].boff
                        :base_data+objs[i].doff) + s->val;
            gsym_add(s->name,a);
        } } }
      /* linker-defined symbols (end of segments) if referenced/undefined */
      { unsigned tb=0; unsigned e; for(i=0;i<nobj;i++) tb+=(objs[i].bsssz+3)&~3;
        e=base_bss+tb;
        if(!gsym_find("_end",&e)) gsym_add("_end", base_bss+tb);
        if(!gsym_find("end",&e))  gsym_add("end",  base_bss+tb);
        if(!gsym_find("_edata",&e)) gsym_add("_edata", base_bss);
        if(!gsym_find("edata",&e))  gsym_add("edata",  base_bss);
        if(!gsym_find("_etext",&e)) gsym_add("_etext", base_data);
        if(!gsym_find("etext",&e))  gsym_add("etext",  base_data);
      }
      /* apply relocations */
      for(i=0;i<nobj;i++){
        relocate(&objs[i], objs[i].text, objs[i].textsz, objs[i].treloc, objs[i].treloc+objs[i].rtsz, base_text+objs[i].toff);
        relocate(&objs[i], objs[i].data, objs[i].datasz, objs[i].dreloc, objs[i].dreloc+objs[i].rdsz, base_data+objs[i].doff);
      }
      /* segment sizes (word-padded, matching the relocated layout) */
      { unsigned a_text=0,a_data=0,a_bss=0; unsigned ea=0; int pad;
        for(i=0;i<nobj;i++){ a_text+=(objs[i].textsz+3)&~3; a_data+=(objs[i].datasz+3)&~3; a_bss+=(objs[i].bsssz+3)&~3; }
        if(!gsym_find(entry,&ea)) fprintf(stderr,"ld_arm: no entry symbol %s\n",entry);
        {
          FILE*of=fopen(out,"wb"); if(!of){perror(out);return 1;}
          if(omagic){
            /* DiscoBSD a.out OMAGIC executable: 8-word header, then text+data.
               a_midmag = OMAGIC(0407) | MID_ZERO(0) = 0x00000107. bss implied. */
            put4(of,0x00000107); put4(of,a_text); put4(of,a_data); put4(of,a_bss);
            put4(of,0); put4(of,0); put4(of,0); put4(of,ea);   /* reltext,reldata,syms,entry */
          }
          for(i=0;i<nobj;i++){ fwrite(objs[i].text,1,objs[i].textsz,of);
              for(pad=((objs[i].textsz+3)&~3)-objs[i].textsz; pad>0; pad--) fputc(0,of); }
          for(i=0;i<nobj;i++){ fwrite(objs[i].data,1,objs[i].datasz,of);
              for(pad=((objs[i].datasz+3)&~3)-objs[i].datasz; pad>0; pad--) fputc(0,of); }
          if(!omagic){ unsigned k; for(k=0;k<a_bss;k++) fputc(0,of); }  /* flat: zero bss */
          fclose(of);
        }
        fprintf(stderr,"ENTRY %x\n",ea);
        if(verbose){ fprintf(stderr,"a_text=%u a_data=%u a_bss=%u\n",a_text,a_data,a_bss);
          for(i=0;i<ngsym;i++) fprintf(stderr,"SYM %s %x\n",gsyms[i].name,gsyms[i].addr); }
      }
    }
    return 0;
}
