int write(int, char*, int);
char* sbrk(int);

int strncmp(char* a, char* b, int n){ while(n>0 && *a && *a==*b){ a=a+1; b=b+1; n=n-1; } if(n==0) return 0; return *a - *b; }
char* strcpy(char* d, char* s){ char* r; r=d; while(*s){ *d=*s; d=d+1; s=s+1; } *d=0; return r; }
char* strncpy(char* d, char* s, int n){ char* r; r=d; while(n>0 && *s){ *d=*s; d=d+1; s=s+1; n=n-1; } while(n>0){ *d=0; d=d+1; n=n-1; } return r; }
char* strcat(char* d, char* s){ char* r; r=d; while(*d) d=d+1; while(*s){ *d=*s; d=d+1; s=s+1; } *d=0; return r; }
char* memset(char* d, int c, int n){ char* r; r=d; while(n>0){ *d=c; d=d+1; n=n-1; } return r; }
int atoi(char* s){ int n; int neg; n=0; neg=0; while(*s==32) s=s+1; if(*s=='-'){ neg=1; s=s+1; } while(*s>='0' && *s<='9'){ n=n*10+(*s-'0'); s=s+1; } if(neg) return -n; return n; }

char* malloc(int n){ n = (n + 7) & (0 - 8); return sbrk(n); }
int free(char* p){ return 0; }
char* calloc(int a, int b){ int n; char* p; n=a*b; p=malloc(n); if(p) memset(p,0,n); return p; }


char _ctype_[1+256] = { 0,040,040,040,040,040,040,040,040,040,040|010,040|010,040|010,040|010,040|010,040,040,040,040,040,040,040,040,040,040,040,040,040,040,040,040,040,040,010|0200,020,020,020,020,020,020,020,020,020,020,020,020,020,020,020,04,04,04,04,04,04,04,04,04,04,020,020,020,020,020,020,020,01|0100,01|0100,01|0100,01|0100,01|0100,01|0100,01,01,01,01,01,01,01,01,01,01,01,01,01,01,01,01,01,01,01,01,020,020,020,020,020,020,02|0100,02|0100,02|0100,02|0100,02|0100,02|0100,02,02,02,02,02,02,02,02,02,02,02,02,02,02,02,02,02,02,02,02,020,020,020,020,040 };

char* strrchr(char* s, int c){ char* r; r=0; while(*s){ if(*s==c) r=s; s=s+1; } if(c==0) return s; return r; }
char* strstr(char* h, char* n){ char* a; char* b; if(*n==0) return h; while(*h){ a=h; b=n; while(*a && *b && *a==*b){ a=a+1; b=b+1; } if(*b==0) return h; h=h+1; } return 0; }
char* strncat(char* d, char* s, int n){ char* r; r=d; while(*d) d=d+1; while(n>0 && *s){ *d=*s; d=d+1; s=s+1; n=n-1; } *d=0; return r; }
char* memchr(char* s, int c, int n){ while(n>0){ if(*s==c) return s; s=s+1; n=n-1; } return 0; }
int abs(int x){ if(x<0) return 0-x; return x; }
