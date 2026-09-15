unsigned __aeabi_uidiv(unsigned n,unsigned d){unsigned q;q=0;while(n>=d){n=n-d;q=q+1;}return q;}
int __aeabi_idiv(int n,int d){int neg;unsigned q;neg=0;if(n<0){n=-n;neg=neg^1;}if(d<0){d=-d;neg=neg^1;}q=__aeabi_uidiv(n,d);if(neg)return -(int)q;return q;}
