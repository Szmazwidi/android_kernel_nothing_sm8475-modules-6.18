/* SPDX-License-Identifier: GPL-2.0-only */
/* Diskless AArch64 VM test of the real signed QRTR module and lookup budget. */
typedef long word;
static word logfd = 1;
static word call(word n, word a, word b, word c, word d, word e, word f)
{
    register word x0 __asm__("x0") = a, x1 __asm__("x1") = b;
    register word x2 __asm__("x2") = c, x3 __asm__("x3") = d;
    register word x4 __asm__("x4") = e, x5 __asm__("x5") = f;
    register word x8 __asm__("x8") = n;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8) : "memory");
    return x0;
}
static void report(const char *name, word value)
{
    char buf[100], digits[24]; int n=0, j=0;
    const char *prefix="PONG_QRTR ";
    while (*prefix) buf[j++]=*prefix++;
    while (*name) buf[j++]=*name++;
    buf[j++]=' ';
    if (value<0) { buf[j++]='-'; value=-value; }
    do { digits[n++]='0'+value%10; value/=10; } while (value);
    while (n) buf[j++]=digits[--n];
    buf[j++]='\n'; call(64,logfd,(word)buf,j,0,0,0);
}
struct address { unsigned short family, pad; unsigned node, port; };
struct pollfd { int fd; short events, revents; };
static int lookup(int fd, long timeout_ns)
{
    struct address addr={42,0,0,0}; unsigned size=sizeof(addr);
    unsigned query[5]={10,0,0,0,0}, reply[5]={0,0,0,0,0};
    if (call(204,fd,(word)&addr,(word)&size,0,0,0)<0) return -1;
    addr.port=0xfffffffeu;
    if (call(206,fd,(word)query,sizeof(query),0,(word)&addr,sizeof(addr))!=20) return -2;
    struct pollfd p={fd,1,0}; word timeout[2]={0,timeout_ns};
    word ready=call(73,(word)&p,1,(word)timeout,0,8,0);
    if (ready==0) return 0;
    if (ready<0) return -3;
    if (call(207,fd,(word)reply,sizeof(reply),0,0,0)!=20) return -4;
    return reply[0]==4 && reply[1]==0 && reply[2]==0 && reply[3]==0 && reply[4]==0 ? 1 : -5;
}
__attribute__((noreturn)) void _start(void)
{
    call(40,(word)"proc",(word)"/proc",(word)"proc",14,0,0);
    call(40,(word)"sysfs",(word)"/sys",(word)"sysfs",14,0,0);
    word fd=call(56,-100,(word)"/dev/kmsg",1,0,0,0);
    if (fd>=0) logfd=fd;
    char force[4]={0};
    fd=call(56,-100,(word)"/sys/module/module/parameters/sig_enforce",0,0,0,0);
    word n=fd<0 ? fd : call(63,fd,(word)force,sizeof(force),0,0,0);
    if (fd>=0) call(57,fd,0,0,0,0,0);
    report("ENFORCE",n>0 && force[0]=='Y');
    fd=call(56,-100,(word)"/qrtr.ko",0,0,0,0);
    word loaded=fd<0 ? fd : call(273,fd,(word)"",0,0,0,0);
    if (fd>=0) call(57,fd,0,0,0,0,0);
    report("MODULE",loaded);
    int sockets[256], created=0, failed=0;
    if (loaded==0) {
        for (int i=0;i<256;i++) {
            word s=call(198,42,2|0x80000,0,0,0,0);
            if (s<0) { failed=(int)s; break; }
            int found=lookup((int)s,500000000);
            if (found!=1) { failed=found; call(57,s,0,0,0,0,0); break; }
            sockets[created++]=(int)s;
        }
        report("CREATED",created); report("FIRST_FAILURE",failed);
        if (created==256) {
            word extra=call(198,42,2|0x80000,0,0,0,0);
            report("BOUND_RETAINED",extra>=0 && lookup((int)extra,200000000)==0);
            if (extra>=0) call(57,extra,0,0,0,0,0);
        }
        for (int i=0;i<created;i++) call(57,sockets[i],0,0,0,0,0);
        call(101,(word)(word[2]){0,100000000},0,0,0,0,0);
        word fresh=call(198,42,2|0x80000,0,0,0,0);
        report("CLEANUP",fresh>=0 && lookup((int)fresh,500000000)==1);
        if (fresh>=0) call(57,fresh,0,0,0,0,0);
    }
    report("DONE",1);
    call(142,0xfee1dead,0x28121969,0x4321fedc,0,0,0);
    for (;;) call(101,(word)(word[2]){1,0},0,0,0,0,0);
}
