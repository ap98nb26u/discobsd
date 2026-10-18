/*
 * On-device native-toolchain smoke test (needs a writable root, i.e. GBAED):
 *   cc /usr/share/examples/gba/hello.c -o /tmp/hello && /tmp/hello
 */
int main(void)
{
    write(1, "hello from on-device cc!\n", 25);
    return 0;
}
