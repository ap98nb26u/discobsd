int putc1(int c){ char b; b=c; return write(1, &b, 1); }
int putn(unsigned n){ if(n>=10) putn(n/10); putc1('0'+n%10); return 0; }
int main(void){ putn(12345); putc1(32); putn(1000000/7); putc1(10); return 0; }
