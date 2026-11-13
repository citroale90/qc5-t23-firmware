/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QWF_LINUX_H
#define QWF_LINUX_H
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mtd/mtd-user.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include "wifi-flash-core.h"
struct qwf_fd {int fd;};
static int qwf_read_fd(void *ctx,unsigned at,void *b,size_t n)
{struct qwf_fd *f=ctx;if(at>8192||n>8192-at)return -1;size_t got=0;while(got<n){ssize_t r=pread(f->fd,(char*)b+got,n-got,at+got);if(r<0&&errno==EINTR)continue;if(r<=0)return -1;got+=(size_t)r;}return 0;}
static int qwf_write_fd(void *ctx,unsigned at,const void *b,size_t n)
{struct qwf_fd *f=ctx;if((at!=0&&at!=4096)||n!=512)return -1;ssize_t r;do{r=pwrite(f->fd,b,n,at);}while(r<0&&errno==EINTR);return r==(ssize_t)n?0:-1;}
static int qwf_erase_fd(void *ctx,unsigned at)
{struct qwf_fd *f=ctx;struct erase_info_user e={at,4096};return (at!=0&&at!=4096)?-1:ioctl(f->fd,MEMERASE,&e);}
static int qwf_open(int writable)
{
 for(unsigned n=0;n<8;n++){
  char path[96],name[64];snprintf(path,sizeof path,"/sys/class/mtd/mtd%u/name",n);
  int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)continue;
  ssize_t len=read(fd,name,sizeof name-1);close(fd);if(len<=0)continue;name[len]=0;if(strcmp(name,"qc5-wifi-config\n"))continue;
  snprintf(path,sizeof path,"/dev/mtd%u",n);fd=open(path,(writable?O_RDWR:O_RDONLY)|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
  struct stat st;struct mtd_info_user info;
  if(fstat(fd,&st)||!S_ISCHR(st.st_mode)||ioctl(fd,MEMGETINFO,&info)||info.type!=MTD_NORFLASH||info.size!=8192||info.erasesize!=4096||info.writesize!=1||(writable&&!(info.flags&MTD_WRITEABLE))){close(fd);return -1;}
  return fd;
 }return -1;
}
static int qwf_mac(const char *iface,u8 mac[6])
{
 char path[96],text[20];if(!iface||strspn(iface,"abcdefghijklmnopqrstuvwxyz0123456789")!=strlen(iface)||strlen(iface)>15)return 0;
 snprintf(path,sizeof path,"/sys/class/net/%s/address",iface);int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 ssize_t n=read(fd,text,sizeof text);close(fd);if(n!=18||text[17]!='\n')return 0;
 for(unsigned i=0;i<6;i++){unsigned v=0;for(unsigned j=0;j<2;j++){char c=text[i*3+j];unsigned x=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;if(x>15)return 0;v=(v<<4)|x;}mac[i]=(u8)v;if(i<5&&text[i*3+2]!=':')return 0;}
 return !(mac[0]&1)&&(mac[0]|mac[1]|mac[2]|mac[3]|mac[4]|mac[5]);
}
static int qwf_linux_load(const char *iface,struct qc5_wifi_profile *p)
{
 u8 mac[6];memset(p,0,sizeof *p);if(!qwf_mac(iface,mac))return -1;struct qwf_fd f={qwf_open(0)};if(f.fd<0)return 0;
 struct qwf_io io={&f,qwf_read_fd,NULL,NULL};uint64_t gen;int bank;int rc=qwf_load(&io,mac,p,&gen,&bank);close(f.fd);return rc;
}
static int qwf_linux_store(const char *iface,const struct qc5_wifi_profile *p)
{
 u8 mac[6];if(!qwf_mac(iface,mac))return -1;
 int lock=open("/run/qc5-wifi-flash.lock",O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);if(lock<0)return -1;
 if(flock(lock,LOCK_EX|LOCK_NB)){close(lock);return -1;}
 struct qwf_fd f={qwf_open(1)};int rc=-1;
 if(f.fd>=0){struct qwf_io io={&f,qwf_read_fd,qwf_erase_fd,qwf_write_fd};rc=qwf_store(&io,mac,p);close(f.fd);}
 close(lock);return rc;
}
#endif
