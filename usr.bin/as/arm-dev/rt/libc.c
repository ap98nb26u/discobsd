int write(int, char*, int);
char* sbrk(int);

int strlen(char* s){ int n; n=0; while(*s){ n=n+1; s=s+1; } return n; }
int strcmp(char* a, char* b){ while(*a && *a==*b){ a=a+1; b=b+1; } return *a - *b; }
int strncmp(char* a, char* b, int n){ while(n>0 && *a && *a==*b){ a=a+1; b=b+1; n=n-1; } if(n==0) return 0; return *a - *b; }
char* strcpy(char* d, char* s){ char* r; r=d; while(*s){ *d=*s; d=d+1; s=s+1; } *d=0; return r; }
char* strncpy(char* d, char* s, int n){ char* r; r=d; while(n>0 && *s){ *d=*s; d=d+1; s=s+1; n=n-1; } while(n>0){ *d=0; d=d+1; n=n-1; } return r; }
char* strcat(char* d, char* s){ char* r; r=d; while(*d) d=d+1; while(*s){ *d=*s; d=d+1; s=s+1; } *d=0; return r; }
char* strchr(char* s, int c){ while(*s){ if(*s==c) return s; s=s+1; } if(c==0) return s; return 0; }
char* memcpy(char* d, char* s, int n){ char* r; r=d; while(n>0){ *d=*s; d=d+1; s=s+1; n=n-1; } return r; }
char* memmove(char* d, char* s, int n){ char* r; r=d; if(d<s){ while(n>0){ *d=*s; d=d+1; s=s+1; n=n-1; } } else { d=d+n; s=s+n; while(n>0){ d=d-1; s=s-1; *d=*s; n=n-1; } } return r; }
char* memset(char* d, int c, int n){ char* r; r=d; while(n>0){ *d=c; d=d+1; n=n-1; } return r; }
int atoi(char* s){ int n; int neg; n=0; neg=0; while(*s==32) s=s+1; if(*s=='-'){ neg=1; s=s+1; } while(*s>='0' && *s<='9'){ n=n*10+(*s-'0'); s=s+1; } if(neg) return -n; return n; }

char* malloc(int n){ n = (n + 7) & (0 - 8); return sbrk(n); }
int free(char* p){ return 0; }
char* calloc(int a, int b){ int n; char* p; n=a*b; p=malloc(n); if(p) memset(p,0,n); return p; }

char* g_buf;
int emit1(int c){ char b; if(g_buf){ *g_buf=c; g_buf=g_buf+1; } else { b=c; write(1,&b,1); } return c; }
int emitu(unsigned n, int base){ char buf[12]; int i; char* dig; dig="0123456789abcdef"; i=0; if(n==0){ emit1('0'); return 0; } while(n>0){ buf[i]=dig[n%base]; i=i+1; n=n/base; } while(i>0){ i=i-1; emit1(buf[i]); } return 0; }
int dofmt(char* fmt, char* ap){
    int iv; unsigned uv; char* sv;
    while(*fmt){
        if(*fmt=='%'){
            fmt=fmt+1;
            if(*fmt=='d'){ iv=*((int*)ap); ap=ap+4; if(iv<0){ emit1('-'); emitu((unsigned)(0-iv),10); } else emitu((unsigned)iv,10); }
            else if(*fmt=='u'){ uv=*((unsigned*)ap); ap=ap+4; emitu(uv,10); }
            else if(*fmt=='x'){ uv=*((unsigned*)ap); ap=ap+4; emitu(uv,16); }
            else if(*fmt=='c'){ iv=*((int*)ap); ap=ap+4; emit1(iv); }
            else if(*fmt=='s'){ sv=*((char**)ap); ap=ap+4; while(*sv){ emit1(*sv); sv=sv+1; } }
            else if(*fmt=='%'){ emit1('%'); }
            fmt=fmt+1;
        } else { emit1(*fmt); fmt=fmt+1; }
    }
    return 0;
}
int printf(char* fmt, ...){ char* ap; ap=(char*)&fmt+4; g_buf=0; dofmt(fmt,ap); return 0; }
int sprintf(char* buf, char* fmt, ...){ char* ap; ap=(char*)&fmt+4; g_buf=buf; dofmt(fmt,ap); *g_buf=0; g_buf=0; return 0; }
int putchar(int c){ char b; b=c; write(1,&b,1); return c; }
int puts(char* s){ while(*s){ emit1(*s); s=s+1; } emit1(10); return 0; }
