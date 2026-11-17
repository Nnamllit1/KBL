// A small Linux PID 1. It runs only from the probe's independent initramfs.
// No libc, Android userspace, mounts or block-device access are needed.
static long call(long n,long a=0,long b=0,long c=0,long d=0) {
    register long x8 __asm__("x8")=n;
    register long x0 __asm__("x0")=a;
    register long x1 __asm__("x1")=b;
    register long x2 __asm__("x2")=c;
    register long x3 __asm__("x3")=d;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8),"r"(x1),"r"(x2),"r"(x3) : "memory","cc");
    return x0;
}
static void write(long fd,const char* s) {
    long size=0;while(s[size]) ++size;
    while(fd>=0 && size) {
        const auto n=call(64,fd,reinterpret_cast<long>(s),size);
        if(n<=0) break;
        s+=n;size-=n;
    }
}
struct Utsname {char sysname[65],nodename[65],release[65],version[65],machine[65],domainname[65];};
extern "C" [[noreturn]] void _start() {
    const long console=call(56,-100,reinterpret_cast<long>("/dev/console"),1);
    const long log=call(56,-100,reinterpret_cast<long>("/dev/kmsg"),1);
    write(console,"\nKBL_LINUX_INIT_OK: independent Linux userspace is running.\n");
    write(log,"KBL_LINUX_INIT_OK: independent Linux userspace is running.\n");
    Utsname info;
    if(call(160,reinterpret_cast<long>(&info))==0) {
        write(console,info.sysname);write(console," ");write(console,info.release);write(console," ");
        write(console,info.machine);write(console," / PID 1\n");
        char message[256];unsigned used=0;
        const char* fields[]={"KBL Linux ",info.release," ",info.machine," / PID 1\n"};
        for(const auto field:fields) for(unsigned i=0;field[i] && used+1<sizeof(message);++i) message[used++]=field[i];
        message[used]=0;write(log,message);
        write(log,"KBL Linux probe reached PID 1; Android storage is not mounted.\n");
    }
    for(;;) {
        const long delay[2]={5,0};call(101,reinterpret_cast<long>(delay));
        write(log,"KBL_LINUX_HEARTBEAT: PID 1 remains alive.\n");
    }
}
