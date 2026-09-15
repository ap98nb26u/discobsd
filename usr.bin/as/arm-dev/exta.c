int f_params(int a,int b,int c){return a+b+c;}
int f_if(int x){int r;if(x>10)r=1;else r=2;return r;}
int f_while(int n){int i;int s;i=1;s=0;while(i<=n){s=s+i;i=i+1;}return s;}
int f_for(int n){int i;int s;s=0;for(i=0;i<n;i=i+1){s=s+i;}return s;}
int f_lshift(int x,int k){return x<<k;}
int f_scmp(int a,int b){return a<b;}
int f_ucmp(unsigned a,unsigned b){return a<b;}
int f_bits(int a,int b){return (a&b)|(a^b);}
int f_and(int x){return x&255;}
int f_bigimm(int x){return x+0x12345678;}
int f_land(int a,int b){return a&&b;}
int f_neg(int x){return -x;}
int f_range(int x){return x>=5&&x<10;}
int f_arr(void){int a[3];a[0]=10;a[1]=20;a[2]=a[0]+a[1];return a[2];}
int f_schar(int x){signed char c;c=x;return c;}
int f_ushort(int x){unsigned short s;s=x;return s;}
int callee(int a,int b){return a*10+b;}
int caller(void){return callee(3,4);}
int fact(int n){if(n<=1)return 1;return n*fact(n-1);}
int fib(int n){if(n<2)return n;return fib(n-1)+fib(n-2);}
int sumc(int n){if(n==0)return 0;return n+sumc(n-1);}
