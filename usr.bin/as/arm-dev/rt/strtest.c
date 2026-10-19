/*
 * strtest.c - checks the native libc's hand-written strcmp, strlen,
 * strchr, memmove and memcpy (clib.s) against a host build of the same
 * file: every alignment, length and overlap direction for the copies, and
 * a set of string pairs (including bytes >= 0x80, compared as unsigned
 * char) for the string functions.  Prints hashes and a few samples.
 * Host:  gcc -DHOST -o strtest-host strtest.c
 */
#ifdef HOST
#include <stdio.h>
#include <string.h>
#define CP char *
#else
int printf(char *, ...);
int strcmp(char *, char *);
int strlen(char *);
char *strchr(char *, int);
char *memmove(char *, char *, int);
char *memcpy(char *, char *, int);
#define CP char *
#endif

char buf[96], ref[96];
char *strs[] = {
    "", "a", "b", "ab", "abc", "abd", "abcd", "ABC", "hello", "hello!",
    "\200", "\377", "a\200", "a\177", "zz", "z"
};

int sgn(int x) { return x < 0 ? -1 : x > 0 ? 1 : 0; }

void fill(char *p) { int i; for (i = 0; i < 96; i++) p[i] = i * 7 + 3; }

unsigned hashbuf(unsigned h)
{
    int i;
    for (i = 0; i < 96; i++) h = h * 31 + (buf[i] & 255);
    return h;
}

int main(void)
{
    int n = sizeof(strs) / sizeof(strs[0]);
    int i, j, d, s, len, c;
    unsigned h;
    char *r;

    h = 0;
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
            h = h * 3 + (unsigned)(sgn(strcmp(strs[i], strs[j])) + 1);
    printf("strcmp hash %08x\n", h);

    h = 0;
    for (i = 0; i < n; i++) {
        h = h * 31 + strlen(strs[i]);
        for (c = 0; c < 256; c += 17) {
            r = strchr(strs[i], c);
            h = h * 31 + (unsigned)(r ? r - strs[i] + 1 : 0);
        }
        r = strchr(strs[i], 0);
        h = h * 31 + (unsigned)(r - strs[i]);
    }
    printf("strlen/strchr hash %08x\n", h);

    /* memmove: all src/dst offsets 0..7 and lengths 0..40, both overlap
       directions inside one buffer; memcpy between separate buffers */
    h = 0;
    for (d = 0; d < 8; d++)
        for (s = 0; s < 8; s++)
            for (len = 0; len <= 40; len++) {
                fill(buf);
                r = memmove(buf + 16 + d, buf + 16 + s, len);
                if (r != buf + 16 + d) printf("memmove return wrong\n");
                h = hashbuf(h);
                fill(buf);
                r = memmove(buf + 16 + s, buf + 16 + d + 3, len);
                h = hashbuf(h);
                fill(buf); fill(ref);
                r = memcpy(buf + d, ref + 40 + s, len);
                if (r != buf + d) printf("memcpy return wrong\n");
                h = hashbuf(h);
            }
    printf("memmove/memcpy hash %08x\n", h);

    printf("%d %d %d %d\n", sgn(strcmp("abc", "abd")), sgn(strcmp("b", "a")),
           sgn(strcmp("\200", "a")), (int)strlen("hello, world"));
    return 0;
}
