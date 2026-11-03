/* Exec a program with stdin/stdout/stderr only. Inherited vendor device handles
 * must not leak into maintenance daemons and pin exclusive camera drivers. */
#ifdef QC5_HOST_TEST
#include <unistd.h>
static long call3(long n,long a,long b,long c) {
    if(n==4006)return close((int)a);
    if(n==4011)return execve((const char *)a,(char *const *)b,(char *const *)c);
    if(n==4004)return write((int)a,(const void *)b,(unsigned long)c);
    return -1;
}
#else
static long call3(long n,long x,long y,long z) {
    register long v0 __asm__("$2")=n;
    register long a0 __asm__("$4")=x,a1 __asm__("$5")=y,a2 __asm__("$6")=z;
    register long a3 __asm__("$7");
    __asm__ volatile("syscall" : "+r"(v0),"=r"(a3) : "r"(a0),"r"(a1),"r"(a2)
        : "memory","$1","$3","$8","$9","$10","$11","$12","$13","$14","$15","$24","$25","hi","lo");
    return a3?-v0:v0;
}
#endif
int qc5_main(int argc,char **argv) {
    int fd;char **envp=argv+argc+1;
    if(argc<2 || argv[1][0]!='/')return 64;
    /* Camera RLIMIT_NOFILE is checked by the caller; no descriptors above
     * this fixed bound are accepted for a real launch. Linux 3.10 has no
     * close_range syscall. Each close affects this child process only. */
    for(fd=3;fd<4096;fd++)call3(4006,fd,0,0);
    call3(4011,(long)argv[1],(long)(argv+1),(long)envp);
    call3(4004,2,(long)"QC5 clean exec failed\n",22);
    return 126;
}
#ifdef QC5_HOST_TEST
int main(int argc,char **argv){return qc5_main(argc,argv);}
#else
__asm__(".text\n.globl _start\n.ent _start\n_start:\n"
        "lw $4,0($29)\naddiu $5,$29,4\naddiu $29,$29,-16\n"
        "jal qc5_main\nnop\nmove $4,$2\nli $2,4001\nsyscall\n"
        "1: b 1b\nnop\n.end _start\n");
#endif
