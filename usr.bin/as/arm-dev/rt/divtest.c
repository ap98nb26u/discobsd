/*
 * divtest.c - checks the native libc's division helpers (__aeabi_uidivmod
 * and friends, used for / and %) against a host build of the same file.
 * Divides every pair from a list of edge values, signed and unsigned, and
 * prints a hash of all the quotients and remainders plus a few samples.
 * Division by zero and INT_MIN / -1 are left out (undefined in C).
 * Host:  gcc -DHOST -o divtest-host divtest.c
 */
#ifdef HOST
#include <stdio.h>
#else
int printf(char *, ...);
#endif

unsigned uv[] = {
    0, 1, 2, 3, 7, 10, 16, 100, 255, 256, 1000, 4095, 65535, 65536,
    1000000, 123456789, 0x3fffffff, 0x40000000, 0x7fffffff, 0x80000000,
    0x80000001, 0xfffffffe, 0xffffffff
};

int main(void)
{
    int n = sizeof(uv) / sizeof(uv[0]);
    int i, j, cnt;
    unsigned h, a, b;
    int sa, sb;

    h = 0; cnt = 0;
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            a = uv[i]; b = uv[j];
            if (b == 0) continue;
            h = h * 31 + a / b;
            h = h * 31 + a % b;
            cnt++;
        }
    printf("unsigned: %d pairs, hash %08x\n", cnt, h);

    h = 0; cnt = 0;
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++) {
            sa = (int)uv[i]; sb = (int)uv[j];
            if (sb == 0) continue;
            if (sa == (-2147483647 - 1) && sb == -1) continue;
            h = h * 31 + (unsigned)(sa / sb);
            h = h * 31 + (unsigned)(sa % sb);
            sa = -sa;
            if (sa == (-2147483647 - 1) && sb == -1) continue;
            h = h * 31 + (unsigned)(sa / sb);
            h = h * 31 + (unsigned)(sa % sb);
            cnt++;
        }
    printf("signed: %d pairs, hash %08x\n", cnt, h);

    printf("%u %u %u %u\n", 0xffffffffu / 10, 0xffffffffu % 10,
           123456789u / 1000, 123456789u % 1000);
    printf("%d %d %d %d\n", -7 / 2, -7 % 2, 7 / -2, 7 % -2);
    printf("%d %d\n", (-2147483647 - 1) / 7, (-2147483647 - 1) % 7);
    return 0;
}
