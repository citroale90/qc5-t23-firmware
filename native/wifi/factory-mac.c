/* SPDX-License-Identifier: GPL-2.0-only
 * Only read the verified read-only PARAM view; never write flash/config.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "factory-mac.h"
struct qcm_mtd_info {unsigned char type;unsigned flags,size,erasesize,writesize,oobsize;unsigned long long padding;};
#define QCM_MEMGETINFO _IOR('M',1,struct qcm_mtd_info)
static int identity_view(void)
{
 for(unsigned n=0;n<16;n++){
  char path[96],text[64];snprintf(path,sizeof path,"/sys/class/mtd/mtd%u/name",n);
  int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)continue;
  ssize_t got=read(fd,text,sizeof text-1);close(fd);if(got<=0)continue;text[got]=0;
  if(strcmp(text,"qc5-factory-param\n"))continue;
  snprintf(path,sizeof path,"/dev/mtd%uro",n);fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
  struct stat st;struct qcm_mtd_info m;
  if(fd<0||fstat(fd,&st)||!S_ISCHR(st.st_mode)||major(st.st_rdev)!=90||st.st_uid||
     ioctl(fd,QCM_MEMGETINFO,&m)||m.type!=3||m.size!=QCM_BYTES||(m.erasesize!=0x1000&&m.erasesize!=0x8000)||m.writesize!=1||(m.flags&0x400)){
   if(fd>=0)close(fd);return -1;
  }return fd;
 }return -1;
}
static int verify(const char *iface,const char *text)
{
 size_t len=strlen(iface);if(len<5||len>=IFNAMSIZ||strncmp(iface,"wlan",4)||strspn(iface+4,"0123456789")!=len-4||strlen(text)!=17)return 64;
 unsigned char wanted[6];if(!qcm_parse(text,wanted))return 64;
 int fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(fd<0)return 68;
 struct ifreq request;memset(&request,0,sizeof request);memcpy(request.ifr_name,iface,len+1);
 int rc=ioctl(fd,SIOCGIFHWADDR,&request);close(fd);
 if(rc||request.ifr_hwaddr.sa_family!=ARPHRD_ETHER||!qcm_equal(request.ifr_hwaddr.sa_data,wanted,6))return 69;
 printf("QC5_FACTORY_MAC_VERIFIED iface=%s mac=%s source=PARAM\n",iface,text);return 0;
}
int main(int argc,char **argv)
{
#ifndef QCM_TEST
 struct utsname u;if(geteuid()||uname(&u)||strcmp(u.release,"7.3.1-qc5-sd-tiny"))return 64;
#endif
 if(argc==4&&!strcmp(argv[1],"--verify"))return verify(argv[2],argv[3]);
 int fd=-1;
#ifdef QCM_TEST
 if(argc!=3||strcmp(argv[1],"--read-file"))return 64;
 fd=open(argv[2],O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);struct stat st;
 if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size!=QCM_BYTES){if(fd>=0)close(fd);return 65;}
#else
 if(argc!=2||strcmp(argv[1],"--read"))return 64;fd=identity_view();
#endif
 if(fd<0){fputs("QC5_FACTORY_MAC_FAILED reason=readonly_PARAM_view\n",stderr);return 65;}
 unsigned char *data=malloc(QCM_BYTES);if(!data){close(fd);return 66;}
 unsigned at=0;while(at<QCM_BYTES){ssize_t n=read(fd,data+at,QCM_BYTES-at);if(n<0&&errno==EINTR)continue;if(n<=0)break;at+=(unsigned)n;}close(fd);
 unsigned char mac[6];struct qcm_meta meta={0};int status=qcm_from_param(data,at,mac,&meta);
 volatile unsigned char *wipe=data;for(unsigned i=0;i<QCM_BYTES;i++)wipe[i]=0;free(data);
 if(status!=QCM_OK){fprintf(stderr,"QC5_FACTORY_MAC_FAILED reason=%d bank_mask=%u\n",status,meta.bank_mask);return 65;}
 char text[18];qcm_format(mac,text);fprintf(stderr,"QC5_FACTORY_MAC_READ bank_mask=%u copy_mask=%u\n",meta.bank_mask,meta.copy_mask);puts(text);return 0;
}
