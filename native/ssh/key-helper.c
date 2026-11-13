/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <mtd/mtd-user.h>
#include "key-core.h"
#include "key-file.h"
#ifndef QKS_OUTPUT
#define QKS_OUTPUT "/run/qc5-ssh/hostkey"
#endif
static int rd(void*c,unsigned at,void*b,size_t n){if(at>8192||n>8192-at)return -1;ssize_t got;do{got=pread(*(int*)c,b,n,at);}while(got<0&&errno==EINTR);return got==(ssize_t)n?0:-1;}
static int wr(void*c,unsigned at,const void*b,size_t n){if((at!=0&&at!=4096)||n!=512)return -1;ssize_t got;do{got=pwrite(*(int*)c,b,n,at);}while(got<0&&errno==EINTR);return got==(ssize_t)n?0:-1;}
static int er(void*c,unsigned at){if(at!=0&&at!=4096)return -1;struct erase_info_user e={at,4096};return ioctl(*(int*)c,MEMERASE,&e);}
static int open_flash(int write){
 for(unsigned i=0;i<16;i++){
  char path[96],name[64];snprintf(path,sizeof path,"/sys/class/mtd/mtd%u/name",i);int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)continue;ssize_t n=read(fd,name,sizeof name-1);close(fd);if(n<=0)continue;name[n]=0;if(strcmp(name,"qc5-ssh-key\n"))continue;
  snprintf(path,sizeof path,"/dev/mtd%u",i);fd=open(path,(write?O_RDWR:O_RDONLY)|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;struct stat st;struct mtd_info_user info;
  if(fstat(fd,&st)||!S_ISCHR(st.st_mode)||ioctl(fd,MEMGETINFO,&info)||info.type!=MTD_NORFLASH||info.size!=8192||info.erasesize!=4096||info.writesize!=1||(write&&!(info.flags&MTD_WRITEABLE))){close(fd);return -1;}return fd;
 }return -1;
}
static int mac_read(u8 mac[6]){
 int fd=open("/run/factory-mac",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);char s[20];if(fd<0)return 0;struct stat st;ssize_t n=read(fd,s,sizeof s);int good=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&st.st_size==18&&n==18;close(fd);if(!good||s[17]!='\n')return 0;
 for(unsigned i=0;i<6;i++){unsigned v=0;for(unsigned j=0;j<2;j++){char c=s[i*3+j];unsigned a=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:16;if(a>15)return 0;v=v*16+a;}mac[i]=v;if(i<5&&s[i*3+2]!=':')return 0;}return !(mac[0]&1)&&(mac[0]|mac[1]|mac[2]|mac[3]|mac[4]|mac[5]);
}
static int secure_parent(void){
 struct stat st;struct statfs fs;return !lstat("/run/qc5-ssh",&st)&&S_ISDIR(st.st_mode)&&!st.st_uid&&!(st.st_mode&077)&&!statfs("/run/qc5-ssh",&fs)&&((unsigned long)fs.f_type==0x01021994u||(unsigned long)fs.f_type==0x858458f6u);
}
int main(int argc,char**argv){
 struct utsname u;if(argc!=2||geteuid()||uname(&u)||strcmp(u.release,"7.3.1-qc5-sd-tiny")||!secure_parent())return 64;
 int store=!strcmp(argv[1],"store");if(!store&&strcmp(argv[1],"load"))return 64;u8 mac[6],key[QKS_KEY];if(!mac_read(mac))return 65;
 int lock=open("/run/qc5-ssh-flash.lock",O_WRONLY|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 66;
 int fd=open_flash(store);if(fd<0)return 67;struct qks_io io={&fd,rd,er,wr};uint64_t gen;unsigned mask;int rc=1;
 if(store){
  int k=open(QKS_OUTPUT,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(k<0)goto done;struct stat st;
  int good=!fstat(k,&st)&&S_ISREG(st.st_mode)&&!st.st_uid&&st.st_nlink==1&&!(st.st_mode&077)&&st.st_size==QKS_KEY&&read(k,key,sizeof key)==sizeof key;close(k);
  if(good&&!qks_store(&io,mac,key)){u8 check[QKS_KEY];if(qks_load(&io,mac,check,&gen,&mask)==1&&mask==3&&!memcmp(check,key,QKS_KEY))rc=0;memset(check,0,sizeof check);}
 }else{
  int found=qks_load(&io,mac,key,&gen,&mask);if(!found){rc=2;goto done;}if(found!=1)goto done;
  if(!qks_publish_key(QKS_OUTPUT,key))rc=0;
 }
done:memset(key,0,sizeof key);close(fd);close(lock);if(!rc)fprintf(stderr,"QC5_SSH_IDENTITY %s=flash verified=1\n",store?"saved":"loaded");return rc;
}
