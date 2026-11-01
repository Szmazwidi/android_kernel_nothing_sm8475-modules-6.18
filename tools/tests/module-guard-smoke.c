/* SPDX-License-Identifier: GPL-2.0-only */
/* AArch64 initramfs test. No hardware, disks, network, enrollment or user data. */
typedef long word;
static word output_fd = 1;
static word call(word n, word a, word b, word c, word d, word e, word f)
{
    register word x0 __asm__("x0") = a, x1 __asm__("x1") = b;
    register word x2 __asm__("x2") = c, x3 __asm__("x3") = d;
    register word x4 __asm__("x4") = e, x5 __asm__("x5") = f;
    register word x8 __asm__("x8") = n;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8) : "memory");
    return x0;
}
static void say(const char *s)
{
    word n = 0;
    while (s[n]) n++;
    call(64, output_fd, (word)s, n, 0, 0, 0);
}
static void result_line(const char *name, word value)
{
    char digits[24], out[192];
    int n = 0, i = 0;
    const char *prefix = "PONG_GUARD_MODULE ";
    while (*prefix) out[i++] = *prefix++;
    while (*name) out[i++] = *name++;
    out[i++] = ' ';
    if (value < 0) { out[i++] = '-'; value = -value; }
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n) out[i++] = digits[--n];
    out[i++] = '\n';
    call(64, output_fd, (word)out, i, 0, 0, 0);
}
__attribute__((noreturn)) void _start(void)
{
    call(40, (word)"proc", (word)"/proc", (word)"proc", 14, 0, 0);
    call(40, (word)"sysfs", (word)"/sys", (word)"sysfs", 14, 0, 0);
    word log = call(56, -100, (word)"/dev/kmsg", 1, 0, 0, 0);
    if (log >= 0) output_fd = log;
    char state[4] = {0};
    word fd = call(56, -100, (word)"/sys/module/module/parameters/sig_enforce", 0, 0, 0, 0);
    word n = fd < 0 ? fd : call(63, fd, (word)state, sizeof(state), 0, 0, 0);
    if (fd >= 0) call(57, fd, 0, 0, 0, 0, 0);
    if (n > 0 && state[0] == 'Y') say("PONG_GUARD_ENFORCE Y\n");
    else say("PONG_GUARD_ENFORCE NOT_Y\n");
    const char *files[] = {"/unsigned.ko", "/wrong-key.ko", "/corrupt-signature.ko", "/changed-signed.ko", "/good.ko"};
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        fd = call(56, -100, (word)files[i], 0, 0, 0, 0);
        word result = fd < 0 ? fd : call(273, fd, (word)"", 0, 0, 0, 0);
        if (fd >= 0) call(57, fd, 0, 0, 0, 0, 0);
        result_line(files[i], result);
    }
    say("PONG_GUARD_DONE\n");
    call(142, 0xfee1dead, 0x28121969, 0x4321fedc, 0, 0, 0);
    for (;;) call(101, (word)(word[2]){1, 0}, 0, 0, 0, 0, 0);
}
