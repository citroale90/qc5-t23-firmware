/* SPDX-License-Identifier: GPL-3.0-only
 * Minimal flash bootstrap: mount one hash-pinned, read-only SYSTEM image,
 * publish regular read-only bindings, then exec its native startup. No SD,
 * NOR writer, shell parser or vendor ROOT is needed to reach this point.
 */
#define _GNU_SOURCE
#include <linux/fs.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#define main qrr_publish
#include "bind-native.c"
#undef main
#include "runtime.generated.h"
static void fail(unsigned stage)
{
 fprintf(stderr,"QC5_FLASH_INIT_FAILED stage=%u errno=%d no_flash_writes=1\n",stage,errno);
 for(;;)sleep(1); /* The boot shim's already active watchdog owns recovery. */
}
static void directory(const char *name)
{if(mkdir(name,0700)&&errno!=EEXIST)fail(1);}
static void load_module(const char *path)
{
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)fail(2);
 int rc=syscall(SYS_finit_module,fd,"",0);close(fd);if(rc)fail(3);
}
static int runtime_device(char path[64])
{
 for(unsigned attempt=0;attempt<100;attempt++){
  for(unsigned n=0;n<8;n++){
   char file[96],name[64];snprintf(file,sizeof file,"/sys/class/mtd/mtd%u/name",n);
   int fd=open(file,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)continue;
   ssize_t got=read(fd,name,sizeof name-1);close(fd);if(got<=0)continue;name[got]=0;
   if(strcmp(name,"qc5-native-runtime\n"))continue;
   snprintf(path,64,"/dev/mtdblock%u",n);return 1;
  }usleep(20000);
 }return 0;
}
static int verify_runtime(const char *path)
{
 int fd=open(path,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
 struct stat st;uint64_t size=0;int readonly=0;
 int ok=!fstat(fd,&st)&&S_ISBLK(st.st_mode)&&!ioctl(fd,BLKGETSIZE64,&size)&&size==0x298000&&!ioctl(fd,BLKROGET,&readonly)&&readonly;
 Sha hash;init(&hash);unsigned char buffer[4096],digest[32];size_t left=QC5_RUNTIME_BYTES;
 while(ok&&left){size_t wanted=left<sizeof buffer?left:sizeof buffer;ssize_t got=read(fd,buffer,wanted);if(got<0&&errno==EINTR)continue;if(got<=0){ok=0;break;}add(&hash,buffer,got);left-=got;}
 finish(&hash,digest);close(fd);
 for(unsigned i=0;ok&&i<32;i++)if(QC5_RUNTIME_SHA[2*i]!="0123456789abcdef"[digest[i]>>4]||QC5_RUNTIME_SHA[2*i+1]!="0123456789abcdef"[digest[i]&15])ok=0;
 return ok&&!left;
}
int main(int argc,char **argv)
{
 (void)argv;struct utsname kernel;if(argc!=1||geteuid()||uname(&kernel)||strcmp(kernel.release,"7.3.1-qc5-sd-tiny"))return 64;
 umask(077);directory("/proc");directory("/sys");directory("/dev");directory("/run");directory("/qc5root");
 if(mount("proc","/proc","proc",MS_NODEV|MS_NOSUID|MS_NOEXEC,NULL)||mount("sysfs","/sys","sysfs",MS_NODEV|MS_NOSUID|MS_NOEXEC,NULL)||mount("devtmpfs","/dev","devtmpfs",MS_NOSUID,NULL))fail(4);
 load_module("/bootstrap/qc5-sfc-runtime.ko");char device[64];if(!runtime_device(device)||!verify_runtime(device))fail(5);
 if(mount(device,"/qc5root","squashfs",MS_RDONLY|MS_NODEV|MS_NOSUID,NULL))fail(6);
 struct statfs fs;struct statvfs vfs;if(statfs("/qc5root",&fs)||(unsigned long)fs.f_type!=0x73717368u||statvfs("/qc5root",&vfs)||!(vfs.f_flag&ST_RDONLY))fail(7);
 if(qrr_publish(1,NULL))fail(8);
 puts("QC5_FLASH_RUNTIME_READY from_SYSTEM=1 no_SD_required=1 read_only=1 no_payload_copies=1");fflush(stdout);
 char *args[]={"/bin/busybox-full","sh","/bin/qc5-native-init",NULL};execv(args[0],args);fail(9);return 70;
}
