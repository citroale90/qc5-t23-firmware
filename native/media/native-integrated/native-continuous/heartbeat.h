/* SPDX-License-Identifier: GPL-3.0-only
 * Small atomically replaced tmpfs records. No entropy wait, fsync or SD I/O.
 * Caller owns a private run directory and each pathname has one writer.
 */
#ifndef QC5_SERVICE_HEARTBEAT_H
#define QC5_SERVICE_HEARTBEAT_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
static int qhb_publish(int dir,const char *name,uint64_t now,uint64_t *sequence)
{
 if(dir<0||!name||!now||!sequence||*sequence==UINT64_MAX||strcmp(name,"frontend-health")&&strcmp(name,"network-health"))return 0;
 char text[96],temp[64];int n=snprintf(text,sizeof text,"QH7 %llu %llu\n",(unsigned long long)now,(unsigned long long)(*sequence+1));
 int len=snprintf(temp,sizeof temp,".%s.next",name);if(n<=0||n>=(int)sizeof text||len<=0||len>=(int)sizeof temp)return 0;
 struct stat st;if(!fstatat(dir,temp,&st,AT_SYMLINK_NOFOLLOW)){
  if(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&077)||st.st_nlink!=1||unlinkat(dir,temp,0))return 0;
 }else if(errno!=ENOENT)return 0;
 int fd=openat(dir,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 int ok=write(fd,text,n)==n;if(close(fd))ok=0;if(ok&&renameat(dir,temp,dir,name))ok=0;
 if(!ok)unlinkat(dir,temp,0);else ++*sequence;return ok;
}
static int qhb_read(int dir,const char *name,uint64_t *stamp,uint64_t *sequence)
{
 if(dir<0||!stamp||!sequence||strcmp(name,"frontend-health")&&strcmp(name,"network-health"))return 0;
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return 0;struct stat st;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&077)&&st.st_nlink==1&&st.st_size>0&&st.st_size<96;
 char text[96],canonical[96],extra;ssize_t n=ok?read(fd,text,sizeof text-1):-1;close(fd);if(n<=0)return 0;text[n]=0;
 unsigned long long a,b;if(sscanf(text,"QH7 %llu %llu %c",&a,&b,&extra)!=2||!a||!b)return 0;
 int len=snprintf(canonical,sizeof canonical,"QH7 %llu %llu\n",a,b);if(len!=n||memcmp(text,canonical,n))return 0;
 *stamp=a;*sequence=b;return 1;
}
#endif
