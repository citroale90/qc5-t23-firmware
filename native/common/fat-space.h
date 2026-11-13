/* SPDX-License-Identifier: GPL-3.0-only
 * Bounded FAT32 space estimate. Never invoke statfs/statvfs on the UI thread:
 * an unknown kernel free count scans the whole FAT on the PIO SD controller.
 * FSINFO is only a display estimate, never an allocation authorization.
 */
#ifndef QC5_FAT_SPACE_H
#define QC5_FAT_SPACE_H
#include <stdint.h>
struct qsf_space {uint64_t total,free;unsigned fsinfo,cluster_bytes,clusters,known;};
static unsigned qsf16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t qsf32(const unsigned char *p){return qsf16(p)|(uint32_t)qsf16(p+2)<<16;}
static int qsf_geometry(const unsigned char b[512],struct qsf_space *v)
{
 *v=(struct qsf_space){0};unsigned spc=b[13],reserved=qsf16(b+14),fats=b[16];
 uint32_t sectors=qsf32(b+32),fat=qsf32(b+36),info=qsf16(b+48);
 if(qsf16(b+510)!=0xaa55||qsf16(b+11)!=512||!spc||spc>128||(spc&(spc-1))||!reserved||
  (fats!=1&&fats!=2)||qsf16(b+17)||qsf16(b+19)||qsf16(b+22)||!fat||!info||info>=reserved)return 0;
 uint64_t overhead=(uint64_t)fats*fat+reserved;if(sectors<=overhead)return 0;
 uint64_t count=(sectors-overhead)/spc;if(count<65525||count>0x0fffffee||count+2>(uint64_t)fat*128)return 0;
 v->clusters=count;v->cluster_bytes=spc*512;v->total=count*v->cluster_bytes;v->fsinfo=info;return 1;
}
static int qsf_fsinfo(const unsigned char b[512],struct qsf_space *v)
{
 v->known=0;v->free=0;
 if(!v->clusters||qsf32(b)!=0x41615252||qsf32(b+484)!=0x61417272||qsf32(b+508)!=0xaa550000)return 0;
 uint32_t free=qsf32(b+488);if(free>v->clusters)return 0;
 v->free=(uint64_t)free*v->cluster_bytes;v->known=1;return 1;
}
#ifndef QC5_FAT_SPACE_PURE
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
static int qsf_probe(const char *root,struct qsf_space *v)
{
 *v=(struct qsf_space){0};struct stat mounted,device;
 int fd=open("/dev/mmcblk0p1",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return 0;
 int valid=!fstat(fd,&device)&&S_ISBLK(device.st_mode)&&!stat(root,&mounted)&&mounted.st_dev==device.st_rdev;
 unsigned char b[512];
 if(valid)valid=pread(fd,b,sizeof b,0)==sizeof b&&qsf_geometry(b,v);
 if(valid&&pread(fd,b,sizeof b,(off_t)v->fsinfo*512)==sizeof b)(void)qsf_fsinfo(b,v);
 close(fd);return valid;
}
#endif
#endif
