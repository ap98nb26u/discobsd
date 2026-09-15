unsigned __aeabi_uidiv(unsigned,unsigned);
int __aeabi_idiv(int,int);
int gsum;
int arith(void){int a;int b;a=7;b=5;return a*b-(a+b)/3;}
int fact(int n){if(n<=1)return 1;return n*fact(n-1);}
int fib(int n){if(n<2)return n;return fib(n-1)+fib(n-2);}
int gcd2(int a,int b){while(b>0){int t;t=a-(a/b)*b;a=b;b=t;}return a;}
int sumn(int n){int i;int s;s=0;for(i=1;i<=n;i=i+1)s=s+i;return s;}
int usearr(void){int a[4];int i;int s;a[0]=2;a[1]=4;a[2]=6;a[3]=8;s=0;for(i=0;i<4;i=i+1)s=s+a[i];return s;}
int setg(int v){gsum=v*v;return gsum;}
int divchain(int x){return x/2/3;}
int mainall(void){return arith()+fact(4)+fib(7)+gcd2(48,36)+sumn(10)+usearr()+setg(5)+divchain(60);}
