/* SPDX-License-Identifier: GPL-3.0-only
 * Isolated Linux MTD backend for the native 7.3.1 capsule.
 * The production runtime never contains this writer.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/utsname.h>
#include <mtd/mtd-user.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include "full-recovery.h"
#include "qc5_sha256.h"
#include "release.generated.h"
#include "watchdog-counter.h"
#include "boot-policy.h"
#include "env-helper.generated.h"
#include "factory-profile.generated.h"
#include "factory-mac-check.h"
struct state {int fd,log;unsigned writes;uint64_t until;volatile uint32_t *wd;u8 head[32],tail[32],before[32];};
static uint64_t now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static int wd_read(void *ctx,unsigned off,uint32_t *out)
{struct state *s=ctx;if(!s->wd||off>12||off%4)return 0;*out=s->wd[off/4];return 1;}
static int wd_write(void *ctx,unsigned off,uint32_t value)
{struct state *s=ctx;if(!s->wd||off!=8||value)return 0;s->wd[2]=0;__sync_synchronize();return 1;}
static int feed(struct state *s)
{
 struct qhw_io io={s,wd_read,wd_write};uint32_t value;
 if(!qhw_geometry(&io,&value)||now()>s->until)return 0;
 return value<64||qhw_refresh(&io);
}
static int whole(int fd,u32 at,u32 n,u8 *out,int writing)
{
 while(n){ssize_t got=writing?pwrite(fd,out,n,at):pread(fd,out,n,at);if(got<0&&errno==EINTR)continue;if(got<=0)return -1;at+=(u32)got;out+=got;n-=(u32)got;}return 0;
}
static int read_flash(void *ctx,u32 at,u32 n,u8 *out)
{struct state *s=ctx;if(at>0x800000||n>0x800000-at||!feed(s))return -1;return whole(s->fd,at,n,out,0);}
static int writable(u32 at,u32 n)
{return n==0x8000&&at>=0x40000&&at<=0x790000-n&&!(at%0x8000);}
static int erase_flash(void *ctx,u32 at,u32 n)
{
 struct state *s=ctx;if(!s->writes||!writable(at,n)||!feed(s))return -1;
 struct erase_info_user e={at,n};return ioctl(s->fd,MEMERASE,&e);
}
static int program_flash(void *ctx,u32 at,u32 n,const u8 *data)
{struct state *s=ctx;if(!s->writes||!writable(at,n)||!feed(s))return -1;return whole(s->fd,at,n,(u8 *)data,1);}
static void progress(void *ctx,u32 phase,u32 address)
{
 struct state *s=ctx;if(!phase)return;
 dprintf(s->log,"phase=%u address=%08x writes_enabled=%u\n",phase,address,s->writes);fsync(s->log);
}
static int region_hash(struct state *s,u32 at,u32 n,const u8 wanted[32])
{Sha h;init(&h);u8 b[4096],out[32];while(n){u32 k=n<sizeof b?n:sizeof b;if(read_flash(s,at,k,b))return 0;add(&h,b,k);at+=k;n-=k;}finish(&h,out);return !memcmp(out,wanted,32);}
static int protected_ok(struct state *s)
{return region_hash(s,0,0x40000,s->head)&&region_hash(s,0x790000,0x70000,s->tail);}
static void *package(const char *name,size_t bytes,int *fd)
{
 struct stat st;*fd=open(name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(*fd<0||fstat(*fd,&st)||!S_ISREG(st.st_mode)||st.st_size!=(off_t)bytes)return MAP_FAILED;
 return mmap(NULL,bytes,PROT_READ,MAP_PRIVATE,*fd,0);
}
static int no_vendor_processes(void)
{
 DIR *d=opendir("/proc");if(!d)return 0;struct dirent *e;int okay=1;
 while((e=readdir(d))){if(e->d_name[0]<'0'||e->d_name[0]>'9')continue;char p[96],comm[64];snprintf(p,sizeof p,"/proc/%.20s/comm",e->d_name);int fd=open(p,O_RDONLY|O_CLOEXEC);if(fd<0)continue;ssize_t n=read(fd,comm,sizeof comm-1);close(fd);if(n<=0)continue;comm[n]=0;
  if(!strcmp(comm,"vp_project\n")||!strcmp(comm,"auto_ota\n")||!strcmp(comm,"vp_update\n")||!strcmp(comm,"systemcall\n")){okay=0;break;}
 }closedir(d);return okay;
}
static int initialize(struct state *s,const char *logname)
{
 struct utsname u;if(geteuid()||!no_vendor_processes()||uname(&u)||strcmp(u.release,"3.10.14__isvp_pike_1.0__"))return 65;
 s->until=now()+600000000000ull;s->log=open(logname,O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);if(s->log<0)return 66;
 int mem=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);if(mem<0)return 67;
 s->wd=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,mem,0x10002000);close(mem);if(s->wd==MAP_FAILED)return 68;
 dprintf(s->log,"WDT data=%u enable=%u count=%u control=%u counter_only=1\n",s->wd[0],s->wd[1],s->wd[2],s->wd[3]);fsync(s->log);if(!feed(s))return 68;
 s->fd=open("/dev/mtd6ro",O_RDONLY|O_SYNC|O_CLOEXEC);if(s->fd<0)return 69;
 struct mtd_info_user info;if(ioctl(s->fd,MEMGETINFO,&info)||info.type!=MTD_NORFLASH||info.size!=0x800000||info.erasesize!=0x8000)return 70;
 return 0;
}
static void little(u8 *p,u32 n){p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
static u32 word(const u8 *p){return (u32)p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24;}
static int squashfs(const u8 *p,u32 capacity)
{return !memcmp(p,"hsqs",4)&&p[28]==4&&!p[29]&&!p[30]&&!p[31]&&!word(p+44)&&word(p+40)>=96&&word(p+40)<=capacity;}
static int new_file(const char *directory,const char *name,const u8 *head,size_t hn,const u8 *body,size_t n,struct state *s)
{
 char path[1024];if(snprintf(path,sizeof path,"%s/%s",directory,name)>=(int)sizeof path)return 0;
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 int okay=!whole(fd,0,hn,(u8 *)head,1);size_t at=0;
 while(okay&&at<n){u32 k=n-at<32768?n-at:32768;okay=feed(s)&&!whole(fd,hn+at,k,(u8 *)body+at,1);at+=k;}
 if(fsync(fd))okay=0;close(fd);return okay;
}
static int snapshot_file(struct state *s,const char *directory,const char *name,u8 digest[32])
{
 char path[1024];if(snprintf(path,sizeof path,"%s/%s",directory,name)>=(int)sizeof path)return 0;
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 Sha h;init(&h);u8 block[32768];int okay=1;
 for(u32 at=0;at<0x800000;at+=sizeof block){if(read_flash(s,at,sizeof block,block)||whole(fd,at,sizeof block,block,1)){okay=0;break;}add(&h,block,sizeof block);}
 finish(&h,digest);if(fsync(fd))okay=0;close(fd);return okay;
}
static int prepare_refused(struct state *s,int code,const char *stage)
{
 dprintf(s->log,"QC5_PREPARER_REFUSED stage=%s code=%d no_flash_writes=1\n",stage,code);
 fsync(s->log);return code;
}
static int preparation_files(struct state *s,const char *directory,const u8 *a,const char *helper_name)
{
 u8 env[16384],crc[4],journal[512]={0};
 if(!qenv_propose(a+0x3c000,env))return 0;
 const u8 *need=(const u8 *)(memcmp(env,a+0x3c000,16384)?"1\n":"0\n");
 if(!new_file(directory,"BOOT.original.bin",0,0,a,0x40000,s)||!new_file(directory,"BOOTENV.proposed.bin",0,0,env,16384,s)||!new_file(directory,"BOOT.expected.bin",a,0x3c000,env,16384,s)||!new_file(directory,"NEEDENV",0,0,need,2,s))return 0;
 memcpy(crc,env,4);memset(env,255,4);
 if(!new_file(directory,"ENV-PENDING.BIN",0,0,env,4096,s)||!new_file(directory,"ENV-CRC.BIN",0,0,crc,4,s)||!new_file(directory,"BOOT.pending.bin",a,0x3c000,env,16384,s))return 0;
 memset(env,255,4096);if(!new_file(directory,"BOOT.erased.bin",a,0x3c000,env,16384,s))return 0;
 if(helper_name){
  int fd=-1;u8 *mapped=package(helper_name,QENV_HELPER_BYTES,&fd),digest[32];static const u8 wanted[32]=QENV_HELPER_SHA_BYTES;
  if(mapped==MAP_FAILED)return 0;hash(mapped,QENV_HELPER_BYTES,digest);if(memcmp(digest,wanted,32))return 0;
  if(memcmp(mapped+QENV_HELPER_PREFIX_OFFSET,"QC5ENVUNITPREFIX",16))return 0;
  u8 *patched=malloc(QENV_HELPER_BYTES);if(!patched)return 0;memcpy(patched,mapped,QENV_HELPER_BYTES);memcpy(patched+QENV_HELPER_PREFIX_OFFSET,a+0x3c000,16);
  int okay=new_file(directory,"env-unit.ko",0,0,patched,QENV_HELPER_BYTES,s);free(patched);munmap(mapped,QENV_HELPER_BYTES);close(fd);if(!okay)return 0;
 }
 memcpy(journal,"QT4LOG\r\n",8);little(journal+8,1);static const u8 target[32]=QTU_PAYLOAD_SHA_BYTES;memcpy(journal+32,target,32);little(journal+508,qenv_crc(journal,508));
 const u8 zero[8]={0};
 return new_file(directory,"Q73LOG.BIN",0,0,journal,512,s)&&new_file(directory,"ARM-INSTALL.BIN",0,0,(const u8 *)"QC5U73\r\n",8,s)&&new_file(directory,"ARM-RESTORE.BIN",0,0,(const u8 *)"QC5R73\r\n",8,s)&&new_file(directory,"ARM-OFF.BIN",0,0,zero,8,s);
}
static int prepare(const char *payload_name,const char *base,const char *helper_name)
{
 struct stat st;struct statvfs space;if(geteuid()||!no_vendor_processes()||lstat(base,&st)||!S_ISDIR(st.st_mode))return 64;
 if(statvfs(base,&space)||(uint64_t)space.f_bavail*space.f_frsize<0x8000000ull)return 80;
 char directory[900],path[1024];if(snprintf(directory,sizeof directory,"%s/unit-XXXXXX",base)>=(int)sizeof directory||!mkdtemp(directory))return 66;
 snprintf(path,sizeof path,"%s/prepare.log",directory);struct state s={.fd=-1,.log=-1};int rc=initialize(&s,path);if(rc)return rc;
 int payload_fd=-1,a_fd=-1,b_fd=-1;u8 *payload=package(payload_name,0x750000,&payload_fd);if(payload==MAP_FAILED)return 71;
 static const u8 wanted[32]=QTU_PAYLOAD_SHA_BYTES;u8 digest[32],first[32],second[32];hash(payload,0x750000,digest);if(memcmp(digest,wanted,32))return 71;
 if(!snapshot_file(&s,directory,"original-1.bin",first)||!snapshot_file(&s,directory,"original-2.bin",second)||memcmp(first,second,32))return 77;
 snprintf(path,sizeof path,"%s/original-1.bin",directory);u8 *a=package(path,0x800000,&a_fd);snprintf(path,sizeof path,"%s/original-2.bin",directory);u8 *b=package(path,0x800000,&b_fd);
 if(a==MAP_FAILED||b==MAP_FAILED||memcmp(a,b,0x800000))return 77;
 hash(a,0x800000,digest);if(memcmp(digest,first,32))return 77;hash(b,0x800000,digest);if(memcmp(digest,second,32))return 77;
 /* The board/boot transport is checked by qtu_verify below. ROOT/SYSTEM
  * are replaced completely and may have any OEM build: inspect their layout,
  * never use a whole-filesystem software-version hash as a hardware identity. */
 if(!squashfs(a+0x198000,QC5_ROOT_BEFORE_CONFIG_BYTES)||!squashfs(a+0x4f8000,0x298000))return prepare_refused(&s,79,"factory_filesystem_layout");
 if(!qc5_factory_unused_bank(a+0x4f0000,0x4000))return prepare_refused(&s,79,"populated_config_bank");
 dprintf(s.log,"QC5_FACTORY_LAYOUT_ACCEPTED software_version_independent=1 config_padding=%02x\n",a[0x4f0000]);fsync(s.log);
 unsigned char mac[6];struct qcm_meta identity;
 int identity_rc=qcm_from_param(a+0x7c0000,0x40000,mac,&identity);
 if(identity_rc!=QCM_OK){dprintf(s.log,"QC5_FACTORY_MAC_REFUSED code=%d no_flash_writes=1\n",identity_rc);fsync(s.log);return 79;}
 static const char board[]="QC5_YA_CODE_V82-T23-GC1084-AIC8800DC-USB";
 u8 rollback[512]={0},forward[512];memcpy(rollback,"QC5R73",6);little(rollback+8,512);little(rollback+12,0x750000);little(rollback+16,0x40000);little(rollback+20,0x790000);little(rollback+24,0x8000);little(rollback+28,1);memcpy(rollback+32,board,sizeof board-1);
 hash(a,0x3c000,rollback+96);hash(a+0x790000,0x30000,rollback+128);hash(a+0x7c0000,0x40000,rollback+160);hash(a+0x40000,0x750000,rollback+192);hash(rollback,480,rollback+480);
 memcpy(forward,rollback,512);memcpy(forward,"QC5U73",6);little(forward+28,2);memcpy(forward+192,wanted,32);memcpy(forward+224,rollback+192,32);hash(forward,480,forward+480);
 if(!new_file(directory,"QC5FULL.BIN",rollback,512,a+0x40000,0x750000,&s)||!new_file(directory,"QC5UPD.BIN",forward,512,payload,0x750000,&s))return 78;
 int upfd=-1,backfd=-1;snprintf(path,sizeof path,"%s/QC5UPD.BIN",directory);u8 *up=package(path,QFR_PACKAGE_BYTES,&upfd);snprintf(path,sizeof path,"%s/QC5FULL.BIN",directory);u8 *back=package(path,QFR_PACKAGE_BYTES,&backfd);if(up==MAP_FAILED||back==MAP_FAILED)return 78;
 u8 *scratch=malloc(0x8000);if(!scratch)return 72;struct qfr_io io={&s,read_flash,0,0,progress};rc=qtu_verify(up,QFR_PACKAGE_BYTES,back,QFR_PACKAGE_BYTES,&io,scratch,0x8000);
 if(rc!=3){dprintf(s.log,"QC5_UNSUPPORTED_HARDWARE_OR_CHANGED_UNIT result=%d no_flash_writes=1\n",rc);fsync(s.log);return 79;}
 if(!preparation_files(&s,directory,a,helper_name))return 81;
 if(!region_hash(&s,0,0x800000,first))return 77;
 dprintf(s.log,"QC5_UNIT_PAIR_PREPARED independent_reads=2 per_unit_WWW_PARAM=1 no_UID_IP_dependency=1 no_flash_writes=1\n");fsync(s.log);
 snprintf(path,sizeof path,"%s/READY",directory);int ready=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(ready<0)return 78;const char *marker="QC5 native7.3 per-unit pair verified\n";if(write(ready,marker,strlen(marker))!=(ssize_t)strlen(marker)||fsync(ready)){close(ready);return 78;}close(ready);
 puts(directory);fflush(stdout);return 0;
}
int main(int argc,char **argv)
{
 if((argc==4||argc==5)&&!strcmp(argv[1],"--prepare"))return prepare(argv[2],argv[3],argc==5?argv[4]:0);
#ifdef QC5_PREPARE_ONLY
 /* The common first-boot preparer cannot reach a writable MTD open. */
 return 64;
#else
 /* One common binary for the supported hardware. Snapshot/rollback belong
  * to the camera being installed, never to a hard-coded UID/IP or unit hash.
  * argv: --check|--apply forward rollback full-snapshot log. */
 int apply=argc==6&&!strcmp(argv[1],"--apply");
 if(argc!=6||(!apply&&strcmp(argv[1],"--check"))||geteuid()||!no_vendor_processes())return 64;
 struct state s={.fd=-1,.log=-1};int initialized=initialize(&s,argv[5]);if(initialized)return initialized;
 struct mtd_info_user info;
 int upfd=-1,backfd=-1,snapfd=-1;u8 *up=package(argv[2],QFR_PACKAGE_BYTES,&upfd),*back=package(argv[3],QFR_PACKAGE_BYTES,&backfd),*snap=package(argv[4],0x800000,&snapfd);if(up==MAP_FAILED||back==MAP_FAILED||snap==MAP_FAILED)return 71;
 if(memcmp(snap+0x40000,back+512,0x750000))return 71;
 u8 digest[32];hash(snap,0x3c000,digest);if(memcmp(digest,back+96,32))return 71;
 hash(snap+0x790000,0x30000,digest);if(memcmp(digest,back+128,32))return 71;
 hash(snap+0x7c0000,0x40000,digest);if(memcmp(digest,back+160,32))return 71;
 hash(snap,0x40000,s.head);hash(snap+0x790000,0x70000,s.tail);hash(snap,0x800000,s.before);
 u8 *scratch=malloc(0x8000);if(!scratch)return 72;
 struct qfr_io io={&s,read_flash,0,0,progress};int result=qtu_verify(up,QFR_PACKAGE_BYTES,back,QFR_PACKAGE_BYTES,&io,scratch,0x8000);
 if(result!=3||!protected_ok(&s)){dprintf(s.log,"REFUSED result=%d no_flash_writes=1\n",result);fsync(s.log);return 73;}
 if(!apply){int same=region_hash(&s,0,0x800000,s.before);dprintf(s.log,"QC5_LINUX_INSTALL_PREFLIGHT_%s full_preimage_match=%d no_flash_writes=1\n",same?"PASS":"REFUSED",same);fsync(s.log);return same?0:76;}
 /* Only the explicit apply action opens a writable MTD. All earlier checks
  * have run through the read-only character device. */
 close(s.fd);s.fd=open("/dev/mtd6",O_RDWR|O_SYNC|O_CLOEXEC);if(s.fd<0||ioctl(s.fd,MEMGETINFO,&info)||info.size!=0x800000||info.erasesize!=0x8000||!protected_ok(&s)||!no_vendor_processes())return 74;
 /* No unlock is attempted by this backend. The isolated boot must already
  * leave the NOR writable; a protected chip makes MEMERASE fail safely. */
 s.writes=1;io.erase=erase_flash;io.program=program_flash;
 result=qtu_apply(up,QFR_PACKAGE_BYTES,back,QFR_PACKAGE_BYTES,&io,scratch,0x8000);s.writes=0;
 int okay=(result==QFR_RESTORED||result==QFR_ALREADY_RESTORED)&&protected_ok(&s);
 dprintf(s.log,"QC5_LINUX_INSTALL_FINISHED result=%d complete=%d protected_head_tail_unchanged=%d\n",result,okay,protected_ok(&s));fsync(s.log);
 close(s.fd);munmap(up,QFR_PACKAGE_BYTES);munmap(back,QFR_PACKAGE_BYTES);munmap(snap,0x800000);close(upfd);close(backfd);close(snapfd);free(scratch);return okay?0:75;
#endif
}
