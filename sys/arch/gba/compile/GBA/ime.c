void a(void);

void a(void) {
  volatile short * IME = (volatile short *)0x04000208;
  *(IME) = 0;
}
