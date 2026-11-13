/* SPDX-License-Identifier: GPL-2.0-or-later
 * Map only the validated, kernel-excluded 1.5 MiB MDNS reservation.
 */
#ifndef QC5_MDNS_MEMORY_MAP_H
#define QC5_MDNS_MEMORY_MAP_H
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "mdns-reservation.h"
#ifndef QC5_MDNS_FS
#define QC5_MDNS_FS ""
#endif
#ifdef QC5_MDNS_MAP_TEST
_Static_assert(sizeof(QC5_MDNS_FS)>1,"Mapping tests must use an explicit owned fixture root");
#endif
static int qc5_mdns_read_file(const char *path,uint8_t *data,size_t capacity,size_t *length)
{
 int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 size_t used=0;int valid=0;
 while(used<capacity){
  ssize_t n=read(fd,data+used,capacity-used);
  if(n<0){if(errno==EINTR)continue;goto done;}
  if(!n){valid=1;goto done;}used+=(size_t)n;
 }
 for(;;){uint8_t extra;ssize_t n=read(fd,&extra,1);if(n<0&&errno==EINTR)continue;valid=n==0;break;}
done:
 close(fd);if(valid&&length)*length=used;return valid;
}
static void *qc5_mdns_map_authorized(uint32_t *stage)
{
 if(!stage)return MAP_FAILED;*stage=1;
#ifndef QC5_MDNS_MAP_TEST
 struct utsname u;if(uname(&u)||strcmp(u.release,"7.3.1-qc5-sd-tiny"))return MAP_FAILED;
#endif
 uint8_t dt[32];size_t size=0;*stage=2;
 static const struct {const char *path;unsigned offset,bytes;} properties[]={
  {QC5_MDNS_FS "/proc/device-tree/reserved-memory/qc5-mdns@1280000/reg",0,8},
  {QC5_MDNS_FS "/proc/device-tree/reserved-memory/#address-cells",8,4},
  {QC5_MDNS_FS "/proc/device-tree/reserved-memory/#size-cells",12,4},
  {QC5_MDNS_FS "/proc/device-tree/memory@0/reg",16,16}
 };
 for(unsigned n=0;n<sizeof properties/sizeof properties[0];n++){
  if(!qc5_mdns_read_file(properties[n].path,dt+properties[n].offset,properties[n].bytes,&size)||size!=properties[n].bytes)return MAP_FAILED;
 }
 if(!qc5_mdns_read_file(QC5_MDNS_FS "/proc/device-tree/reserved-memory/qc5-mdns@1280000/no-map",NULL,0,&size)||
    !qc5_mdns_dt_authority(dt,sizeof dt,1,size))return MAP_FAILED;
 char iomem[8192];*stage=3;
 if(!qc5_mdns_read_file(QC5_MDNS_FS "/proc/iomem",(uint8_t*)iomem,sizeof iomem,&size)||!qc5_mdns_iomem_authority(iomem,size))return MAP_FAILED;
 *stage=4;int fd=open(QC5_MDNS_FS "/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return MAP_FAILED;
 struct stat st;*stage=5;int kind=fstat(fd,&st)==0;
#ifdef QC5_MDNS_MAP_TEST
 kind=kind&&S_ISREG(st.st_mode)&&st.st_size==33554432;
#else
 kind=kind&&S_ISCHR(st.st_mode);
#endif
 if(!kind){close(fd);return MAP_FAILED;}
 *stage=6;void *p=mmap(NULL,QC5_MDNS_RESERVE_END-QC5_MDNS_RESERVE_START,PROT_READ|PROT_WRITE,MAP_SHARED,fd,QC5_MDNS_RESERVE_START);
 close(fd);if(p!=MAP_FAILED)*stage=7;return p;
}
#endif
