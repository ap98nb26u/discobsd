/*
 * Exercises shipped headers + expanded libc (needs a writable root, i.e. GBAED):
 *   cc /usr/share/examples/gba/ctest.c -o /tmp/ctest && /tmp/ctest
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
int main(void)
{
    char *s = "DiscoBSD GBA 42";
    int i, digits = 0, alpha = 0;
    for (i = 0; s[i]; i++) {
        if (isdigit(s[i])) digits++;
        if (isalpha(s[i])) alpha++;
    }
    printf("find=%s rchr=%s digits=%d alpha=%d up=%c abs=%d\n",
           strstr(s, "GBA"), strrchr(s, 'B'), digits, alpha,
           toupper('a'), abs(-5));
    return 0;
}
