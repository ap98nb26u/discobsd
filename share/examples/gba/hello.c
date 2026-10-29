/*
 * On-device native-toolchain smoke test (GBAED only: the compiler needs its
 * 249K user window, the GBA kernel's is 64K):
 *   cc /usr/share/examples/gba/hello.c -o /tmp/hello && /tmp/hello
 */
int main(void)
{
    write(1, "hello from on-device cc!\n", 25);
    return 0;
}
