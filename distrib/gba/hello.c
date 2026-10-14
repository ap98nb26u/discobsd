/* On-device native-toolchain smoke test: cc /hello.c -o /hello && /hello */
int main(void)
{
    write(1, "hello from on-device cc!\n", 25);
    return 0;
}
