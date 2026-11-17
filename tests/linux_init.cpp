// Freestanding ARM64 PID 1 for the VM handoff test. No libc or Android runtime.
static long syscall4(long n, long a, long b, long c, long d) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}
extern "C" __attribute__((noreturn)) void _start() {
    const char message[] = "\nKBL_LINUX_INIT_OK\n";
    syscall4(64, 1, reinterpret_cast<long>(message), sizeof(message) - 1, 0);
    syscall4(142, 0xfee1dead, 672274793, 0x4321fedc, 0); // Linux power off.
    for (;;) syscall4(124, 0, 0, 0, 0);
}
