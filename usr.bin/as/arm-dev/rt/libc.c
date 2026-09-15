int write(int, char*, int);

int strlen(char* s){ int n; n=0; while(*s){ n=n+1; s=s+1; } return n; }
int strcmp(char* a, char* b){ while(*a && *a==*b){ a=a+1; b=b+1; } return *a - *b; }
char* strcpy(char* d, char* s){ char* r; r=d; while(*s){ *d=*s; d=d+1; s=s+1; } *d=0; return r; }
char* strchr(char* s, int c){ while(*s){ if(*s==c) return s; s=s+1; } if(c==0) return s; return 0; }
char* memcpy(char* d, char* s, int n){ char* r; r=d; while(n>0){ *d=*s; d=d+1; s=s+1; n=n-1; } return r; }
char* memset(char* d, int c, int n){ char* r; r=d; while(n>0){ *d=c; d=d+1; n=n-1; } return r; }
int atoi(char* s){ int n; int neg; n=0; neg=0; while(*s==32) s=s+1; if(*s=='-'){ neg=1; s=s+1; } while(*s>='0' && *s<='9'){ n=n*10+(*s-'0'); s=s+1; } if(neg) return -n; return n; }

int putchar(int c){ char b; b=c; write(1, &b, 1); return c; }
int puts(char* s){ while(*s){ putchar(*s); s=s+1; } putchar(10); return 0; }

int putu_(unsigned n, int base){ char buf[12]; int i; char* dig; dig="0123456789abcdef"; i=0; if(n==0){ putchar('0'); return 0; } while(n>0){ buf[i]=dig[n%base]; i=i+1; n=n/base; } while(i>0){ i=i-1; putchar(buf[i]); } return 0; }

int printf(char* fmt, ...){
    char* ap; int iv; unsigned uv; char* sv;
    ap = (char*)&fmt + 4;
    while(*fmt){
        if(*fmt=='%'){
            fmt=fmt+1;
            if(*fmt=='d'){ iv=*((int*)ap); ap=ap+4; if(iv<0){ putchar('-'); putu_((unsigned)(0-iv),10); } else putu_((unsigned)iv,10); }
            else if(*fmt=='u'){ uv=*((unsigned*)ap); ap=ap+4; putu_(uv,10); }
            else if(*fmt=='x'){ uv=*((unsigned*)ap); ap=ap+4; putu_(uv,16); }
            else if(*fmt=='c'){ iv=*((int*)ap); ap=ap+4; putchar(iv); }
            else if(*fmt=='s'){ sv=*((char**)ap); ap=ap+4; while(*sv){ putchar(*sv); sv=sv+1; } }
            else if(*fmt=='%'){ putchar('%'); }
            fmt=fmt+1;
        } else { putchar(*fmt); fmt=fmt+1; }
    }
    return 0;
}
