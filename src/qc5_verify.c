/* Read-only QC5 container verifier. No erase/write/ioctl interfaces exist here.
 * Freestanding Linux/MIPS build uses only open(O_RDONLY), read, close, write
 * (stdout/stderr) and exit. Host build exercises exactly the same parser/hash.
 */
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned char u8;
typedef unsigned long usize;
#ifdef QC5_HOST_TEST
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static long ropen(const char *p) {
    /* File-backed hardware fixtures exist only in the host-test build. */
    const char *root=getenv("QC5_VERIFY_TEST_ROOT");char path[2048];int n;
    if(root && (!strcmp(p,"/proc/mtd") || !strncmp(p,"/dev/mtd",8))) {
        n=snprintf(path,sizeof(path),"%s%s",root,p);
        if(n<0 || (usize)n>=sizeof(path)) return -1;
        p=path;
    }
    return open(p, O_RDONLY);
}
static long rread(int fd, void *p, usize n) {
    const char *limit=getenv("QC5_VERIFY_TEST_READ_MAX");
    if(limit) {usize max=strtoul(limit,0,10);if(max && n>max) n=max;}
    return read(fd, p, n);
}
static long rclose(int fd) { return close(fd); }
static long output(int fd, const void *p, usize n) { return write(fd, p, n); }
#else
static long call3(long n, long x, long y, long z) {
    register long v0 __asm__("$2") = n;
    register long a0 __asm__("$4") = x;
    register long a1 __asm__("$5") = y;
    register long a2 __asm__("$6") = z;
    register long a3 __asm__("$7");
    __asm__ volatile("syscall" : "+r"(v0), "=r"(a3)
                     : "r"(a0), "r"(a1), "r"(a2)
                     : "memory", "$1", "$3", "$8", "$9", "$10", "$11",
                       "$12", "$13", "$14", "$15", "$24", "$25", "hi", "lo");
    return a3 ? -v0 : v0;
}
static long ropen(const char *p) { return call3(4005, (long)p, 0, 0); }
static long rread(int fd, void *p, usize n) { return call3(4003, fd, (long)p, (long)n); }
static long rclose(int fd) { return call3(4006, fd, 0, 0); }
static long output(int fd, const void *p, usize n) { return call3(4004, fd, (long)p, (long)n); }
#endif

#define SIZE 0x298000u
#define OFFSET 0x4f8000u
#include "qc5_update_profile.h"

static usize length(const char *s) { usize n = 0; while (s[n]) ++n; return n; }
static int equal(const void *av, const void *bv, usize n) {
    const u8 *a = av, *b = bv; usize i;
    for (i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}
static int streq(const char *a, const char *b) {
    return length(a) == length(b) && equal(a, b, length(a));
}
static void say(const char *s) {
    usize n = length(s); long r;
    while (n) { r = output(1, s, n); if (r <= 0) return; s += r; n -= (usize)r; }
}
static u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1]<<8 | (u32)p[2]<<16 | (u32)p[3]<<24; }
#include "qc5_sha256.h"

static u8 header[512],buffer[4096],sqfs[96];
static unsigned format_version;
static int read_exact(int fd, u8 *p, usize n) {
    long r;while(n) {r=rread(fd,p,n);if(r<=0) return 0;p+=r;n-=(usize)r;}return 1;
}
static int field(const u8 *p, const char *s) {
    unsigned n=(unsigned)length(s),i;if(n>=64 || !equal(p,s,n)) return 0;
    for(i=n;i<64;++i) if(p[i]) return 0;
    return 1;
}
static int release_ok(void) {
    unsigned i=0,j;u8 c;
    while(i<64 && (c=header[96+i])!=0) {
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-')) return 0;
        ++i;
    }
    if(!i || i>=64) return 0;
    for(j=i;j<64;++j) if(header[96+j]) return 0;
    return 1;
}
static int payload_part(int fd,u32 size,const u8 *expected,const u8 *stock) {
    Sha s;u8 value[32];u32 n=size,take;int first=1;unsigned i;
    init(&s);
    while(n) {
        take=n>sizeof(buffer)?sizeof(buffer):n;
        if(!read_exact(fd,buffer,take)) return 0;
        if(first) {for(i=0;i<sizeof(sqfs);++i) sqfs[i]=buffer[i];first=0;}
        add(&s,buffer,take);n-=take;
    }
    finish(&s,value);
    if(!equal(value,expected,32)) return 0;
    if(le32(header+28)==2 && !equal(value,stock,32)) return 0;
    return le32(sqfs)==0x73717368u && le32(sqfs+12)==65536 &&
           sqfs[20]==4 && !sqfs[21] && sqfs[28]==4 && !sqfs[29] && !sqfs[30] && !sqfs[31] &&
           le32(sqfs+44)==0 && le32(sqfs+40)>=96 && le32(sqfs+40)<=size;
}
static int payload_ok(int fd) {
    u8 last;
    if(format_version==2) {
        if(!payload_part(fd,0x360000,header+288,profile_root) ||
           !payload_part(fd,SIZE,header+320,profile_stock)) return 0;
    } else if(format_version==3) {
        if(!payload_part(fd,SIZE,header+288,profile_stock) ||
           !payload_part(fd,0x30000,header+320,profile_www)) return 0;
    } else if(!payload_part(fd,SIZE,header+288,profile_stock)) return 0;
    return rread(fd,&last,1)==0;
}
static int container(const char *path) {
    long fd=ropen(path);u8 value[32];unsigned i;int ok=0;
    if(fd<0) return 0;
    if(!read_exact((int)fd,header,sizeof(header))) goto done;
    format_version=equal(header,"QC5UPD1\0",8)?1:equal(header,"QC5UPD2\0",8)?2:equal(header,"QC5UPD3\0",8)?3:0;
    if(!format_version || le32(header+8)!=512 || le32(header+12)!=(format_version==2?0x5f8000:format_version==3?0x2c8000:SIZE) ||
       le32(header+16)!=(format_version==2?0x198000:OFFSET) || le32(header+20)!=0x8000 || le32(header+24)!=format_version ||
       (le32(header+28)!=1 && le32(header+28)!=2) || !field(header+32,profile_board) || !release_ok()) goto done;
    for(i=format_version==3?480:format_version==2?448:352;i<480;++i) if(header[i]) goto done;
    if(!equal(header+160,profile_layout,32) || !equal(header+192,profile_kernel,32) ||
       !equal(header+256,profile_www,32)) goto done;
    if(format_version==1) {
        if(!equal(header+224,profile_root,32)||!equal(header+320,profile_stock,32)) goto done;
    } else if(format_version==2) {
        unsigned root_nonzero=0,system_nonzero=0;
        for(i=0;i<32;++i){root_nonzero|=header[224+i];system_nonzero|=header[416+i];}
        if(!root_nonzero||!system_nonzero||!equal(header+352,profile_root,32)||!equal(header+384,profile_stock,32)) goto done;
    } else {
        unsigned system_nonzero=0,www_nonzero=0;
        for(i=0;i<32;++i){system_nonzero|=header[416+i];www_nonzero|=header[448+i];}
        if(!system_nonzero||!www_nonzero||!equal(header+224,profile_root,32)||
           !equal(header+352,profile_stock,32)||!equal(header+384,profile_www,32)) goto done;
    }
    hash(header,480,value);if(!equal(value,header+480,32)) goto done;
    ok=payload_ok((int)fd);
done:
    rclose((int)fd);return ok;
}
static const char *device_error="unknown";
static const char *device_partition="layout";
static long device_read_result=0;
static void say_number(long v) {
    char s[24];usize n=0;unsigned long x;
    if(v<0) {say("-");x=(unsigned long)(-(v+1))+1;}else x=(unsigned long)v;
    do {s[n++]=(char)('0'+x%10);x/=10;}while(x);
    while(n) {char c=s[--n];output(1,&c,1);}
}
static int raw_hash_span(const char *path, u32 size, u32 hashed, const u8 *expected) {
    long fd=ropen(path),r;u8 value[32],last;u32 take,part;Sha s;int ok=0;
    device_partition=path;
    if(fd<0) {device_error="open";return 0;}init(&s);
    while(size) {take=size>sizeof(buffer)?sizeof(buffer):size;
        if(!read_exact((int)fd,buffer,take)) {device_error="read";goto done;}
        part=take<hashed?take:hashed;
        if(part) add(&s,buffer,part);
        hashed-=part;size-=take;
    }
    r=rread((int)fd,&last,1);if(r!=0) {device_error="end-of-partition";goto done;}
    finish(&s,value);ok=equal(value,expected,32);if(!ok) device_error="hash";
done:
    rclose((int)fd);return ok;
}
static int raw_hash(const char *path,u32 size,const u8 *expected) {
    return raw_hash_span(path,size,size,expected);
}
static int readable_partition(const char *path) {
    long fd=ropen(path);device_partition=path;
    if(fd<0) {device_error="open";return 0;}
    rclose((int)fd);return 1;
}
static int device(int recovery,int target) {
    long fd=ropen("/proc/mtd"),n,m;usize used=0,i=0,j;u8 last;
    if(fd<0) {device_error="open";return 0;}
    /* A short successful read is not EOF. Retain strict byte-for-byte layout
     * checks even when procfs supplies its output in multiple reads. */
    do {
        n=rread((int)fd,buffer+used,sizeof(buffer)-1-used);
        if(n>0) used+=(usize)n;
    } while(n>0 && used<sizeof(buffer)-1);
    m=n==0?0:rread((int)fd,&last,1);rclose((int)fd);
    if(n<0 || !used || m!=0) {device_error="read";device_read_result=n<0?n:m;return 0;}
    n=(long)used;
    buffer[n]=0;while(i<(usize)n && buffer[i]!='\n') ++i;if(i==(usize)n) {device_error="header";return 0;}++i;
    for(j=0;j<7;++j) {
        usize len=length(profile_mtd[j]);
        if(i+len+1>(usize)n || !equal(buffer+i,profile_mtd[j],len) || buffer[i+len]!='\n') {device_error="partition-layout";return 0;}
        i+=len+1;
    }
    if(i!=(usize)n) {device_error="extra-partitions";return 0;}
    if(format_version==3) {
        /* The ROOT-resident SD loader is immutable for application updates.
         * A recovery payload can repair interrupted SYSTEM/WWW writes only
         * while that independent loader, BOOT and kernel remain original. */
        if(!raw_hash_span("/dev/mtd0",0x40000,0x3c000,profile_boot_code) ||
           !raw_hash("/dev/mtd1",0x158000,profile_kernel) ||
           !raw_hash("/dev/mtd2",0x360000,profile_root)) return 0;
        if(recovery) return readable_partition("/dev/mtd3") && readable_partition("/dev/mtd4");
        return raw_hash("/dev/mtd3",SIZE,header+(target?288:416)) &&
               raw_hash("/dev/mtd4",0x30000,header+(target?320:448));
    }
    if(recovery) {
        /* A failed ROOT/SYSTEM write cannot be required to match its old
         * source hashes. Only an exact original paired payload may use this
         * branch; all immutable code/layout checks still apply. No write or
         * reset follows this read-only eligibility check. */
        return raw_hash_span("/dev/mtd0",0x40000,0x3c000,profile_boot_code) &&
               raw_hash("/dev/mtd1",0x158000,profile_kernel) &&
               raw_hash("/dev/mtd4",0x30000,profile_www) &&
               readable_partition("/dev/mtd2") && readable_partition("/dev/mtd3");
    }
    return raw_hash("/dev/mtd1",0x158000,profile_kernel) &&
           raw_hash("/dev/mtd2",0x360000,format_version==2?header+224:profile_root) &&
           raw_hash("/dev/mtd4",0x30000,profile_www) &&
           (format_version!=2 || raw_hash("/dev/mtd3",SIZE,header+416));
}
int qc5_main(int argc, char **argv) {
    int hardware=0,recovery=0,target=0;const char *path;
    if(argc==2) path=argv[1];
    else if(argc==3 && streq(argv[1],"--device")) {hardware=1;path=argv[2];}
    else if(argc==3 && streq(argv[1],"--recovery-check")) {hardware=1;recovery=1;path=argv[2];}
    else if(argc==3 && streq(argv[1],"--target-device")) {hardware=1;target=1;path=argv[2];}
    else {say("{\"ok\":false,\"error\":\"Uso: qc5-verify [--device|--recovery-check|--target-device] pacchetto\",\"flash_written\":false}\n");return 2;}
    if(!container(path)) {say("{\"ok\":false,\"error\":\"Pacchetto non valido per QC5 V82\",\"flash_written\":false}\n");return 1;}
    if(target && format_version!=3) {say("{\"ok\":false,\"error\":\"Verifica destinazione disponibile solo per SYSTEM e WWW\",\"flash_written\":false}\n");return 1;}
    if(recovery && ((format_version!=2 && format_version!=3) || le32(header+28)!=2)) {
        if(format_version==3)
            say("{\"ok\":false,\"error\":\"Il controllo recupero richiede SYSTEM e WWW originali verificati\",\"write_enabled\":false,\"flash_written\":false}\n");
        else say("{\"ok\":false,\"error\":\"Il controllo recupero richiede ROOT e SYSTEM originali verificati\",\"write_enabled\":false,\"flash_written\":false}\n");
        return 1;
    }
    if(hardware && !device(recovery,target)) {
        say("{\"ok\":false,\"error\":\"Layout o firmware base della camera diversi\",\"check\":\"");
        say(device_partition);say("\",\"detail\":\"");say(device_error);
        say("\",\"read_result\":");say_number(device_read_result);
        say(",\"flash_written\":false}\n");return 1;
    }
    say("{\"ok\":true,\"format\":");say_number(format_version);say(",\"release\":\"");say((char *)header+96);
    say("\",\"kind\":\"");say(le32(header+28)==2?"stock":"modified");
    say("\",\"downgrade_allowed\":true,\"integrity_verified\":true,\"signature_verified\":false,\"base_firmware_verified\":");
    say(hardware&&!recovery&&!target?"true":"false");
    if(target) say(",\"target_firmware_verified\":true");
    if(recovery) {
        say(",\"mode\":\"stock_recovery_check\",\"immutable_firmware_verified\":true,\"recovery_bootstrap_verified\":false");
        if(format_version==3) say(",\"original_root_required\":true,\"source_system_www_required\":false");
        else say(",\"source_root_system_required\":false");
    }
    say(",\"write_enabled\":false,\"flash_written\":false}\n");return 0;
}
#ifdef QC5_HOST_TEST
int main(int argc, char **argv) { return qc5_main(argc,argv); }
#else
__asm__(".text\n.globl _start\n.ent _start\n_start:\n"
        "lw $4,0($sp)\naddiu $5,$sp,4\naddiu $sp,$sp,-32\n"
        "jal qc5_main\nnop\nmove $4,$2\nli $2,4001\nsyscall\n"
        "1: b 1b\nnop\n.end _start\n");
#endif
