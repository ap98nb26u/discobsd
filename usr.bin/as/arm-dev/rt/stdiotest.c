/*
 * stdiotest.c - checks the native libc's stdio/printf/ctype against a host
 * build of the same file.  Native:  see the "test" target in Makefile.
 * Host:  gcc -DHOST -o stdiotest-host stdiotest.c   (run in an empty dir)
 * Both must print identical output and leave identical files t1/t2/t3.
 */
#ifdef HOST
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#define POS fpos_t
#else
#define FILE void
#define EOF (-1)
#define POS long
FILE *fopen(char *, char *);
int fclose(FILE *);
int fgetc(FILE *);
int fputc(int, FILE *);
int fputs(char *, FILE *);
int fprintf(FILE *, char *, ...);
int printf(char *, ...);
int sprintf(char *, char *, ...);
int fgetpos(FILE *, POS *);
int fsetpos(FILE *, POS *);
int puts(char *);
int putchar(int);
int strlen(char *);
int strcmp(char *, char *);
int isalpha(int);
int isdigit(int);
int isalnum(int);
int isspace(int);
#endif

int sum(char *name)
{
    FILE *f;
    int c, n, h;

    f = fopen(name, "r");
    if (!f) {
        printf("%s: cannot open\n", name);
        return -1;
    }
    n = 0;
    h = 0;
    while ((c = fgetc(f)) != EOF) {
        h = h * 31 + c;
        n++;
    }
    fclose(f);
    printf("%s: %d bytes, hash %08X\n", name, n, h);
    return n;
}

int main(void)
{
    FILE *f;
    POS mark, end;
    char buf[64];
    int i, a, d, al, s;

    /* printf formats used by smlrc, plus the usual ones */
    printf("[%d] [%u] [%x] [%X] [%o] [%c] [%s] [%%]\n",
           -42, 42u, 255, 255, 8, 'Z', "str");
    printf("[%08X] [%02X] [%03o] [%+d] [%+d] [% d]\n",
           0xBEEF, 7, 9, 5, -5, 3);
    printf("[%5d] [%-5d] [%05d] [%.3d] [%5s] [%-5s] [%.2s] [%*d]\n",
           12, 12, -12, 7, "ab", "ab", "abcdef", 6, 99);
    printf("[%p] [%ld] [%lu] [%hd]\n", (void *)0, 123456789L, 4000000000UL, 7);
    printf("[%d] [%d] [%u]\n", 0, -2147483647 - 1, 4294967295U);

    i = sprintf(buf, "%s-%d-%04x", "sp", 3, 0xab);
    printf("sprintf: \"%s\" ret %d\n", buf, i);

    a = 0; d = 0; al = 0; s = 0;
    for (i = -1; i < 256; i++) {
        if (isalpha(i)) a++;
        if (isdigit(i)) d++;
        if (isalnum(i)) al++;
        if (isspace(i)) s++;
    }
    printf("ctype: alpha %d digit %d alnum %d space %d\n", a, d, al, s);

    /* write + back-patch through fgetpos/fsetpos, like smlrc's prologs */
    f = fopen("t1", "w");
    fputs("header\n", f);
    fgetpos(f, &mark);
    fputs("PLACEHOLDER\n", f);
    for (i = 0; i < 300; i++)
        fprintf(f, "line %d of the body\n", i);
    fgetpos(f, &end);
    fsetpos(f, &mark);
    fputs("patched....\n", f);
    fsetpos(f, &end);
    fputs("trailer\n", f);
    fclose(f);
    sum("t1");

    /* character-at-a-time copy */
    {
        FILE *in, *out;
        int c;
        in = fopen("t1", "r");
        out = fopen("t2", "w");
        while ((c = fgetc(in)) != EOF)
            fputc(c, out);
        fclose(in);
        fclose(out);
    }
    sum("t2");

    if (fopen("no/such/file", "r") == 0)
        puts("missing file: NULL");

    /* left open on purpose: exit() must flush it */
    f = fopen("t3", "w");
    for (i = 0; i < 50; i++)
        fputc('a' + i % 26, f);
    putchar('o');
    putchar('k');
    putchar(10);
    return 0;
}
