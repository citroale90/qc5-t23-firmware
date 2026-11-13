/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int calls,mode;
static ssize_t fault_write(int fd,const void *data,size_t n)
{
 calls++;
 if(mode==1&&calls==1){errno=EINTR;return -1;}
 if(mode==2&&calls>1)return 0;
 if(n>7)n=7;
 return write(fd,data,n);
}
#define QKF_WRITE fault_write
#include "key-file.h"
static void put(const char *p,const void *data,size_t n)
{
 int fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);
 assert(write(fd,data,n)==(ssize_t)n);assert(!close(fd));
}
static void same(const char *p,const void *data,size_t n)
{
 unsigned char buf[QKS_KEY+1];int fd=open(p,O_RDONLY);assert(fd>=0);
 assert(read(fd,buf,sizeof buf)==(ssize_t)n);assert(!memcmp(buf,data,n));close(fd);
}
int main(void)
{
 char dir[]="/tmp/qc5-key-file-XXXXXX";assert(mkdtemp(dir));
 char key[256],temp[280],other[256];
 snprintf(key,sizeof key,"%s/key",dir);snprintf(temp,sizeof temp,"%s.flash-next",key);
 snprintf(other,sizeof other,"%s/unrelated",dir);
 u8 data[QKS_KEY],old[QKS_KEY];memset(data,0x35,sizeof data);memset(old,0x64,sizeof old);
 /* A killed helper left its incomplete publication behind. */
 put(temp,"partial",7);mode=1;calls=0;
 assert(!qks_publish_key(key,data));assert(calls>2);same(key,data,sizeof data);
 struct stat st;assert(!stat(key,&st));assert((st.st_mode&0777)==0600);
 assert(lstat(temp,&st)&&errno==ENOENT);
 /* Removing a stale symlink must never truncate its target. */
 put(other,"untouched",9);assert(!symlink(other,temp));mode=0;calls=0;
 assert(!qks_publish_key(key,old));same(other,"untouched",9);same(key,old,sizeof old);
 /* A short write followed by an error must retain the previous complete key. */
 mode=2;calls=0;assert(qks_publish_key(key,data));same(key,old,sizeof old);
 assert(lstat(temp,&st)&&errno==ENOENT);
 /* A failed rename must clean up its temporary, never replace a directory. */
 assert(!unlink(key));assert(!mkdir(key,0700));mode=0;calls=0;
 assert(qks_publish_key(key,data));assert(!stat(key,&st)&&S_ISDIR(st.st_mode));
 assert(lstat(temp,&st)&&errno==ENOENT);
 assert(!rmdir(key));assert(!unlink(other));assert(!rmdir(dir));
 puts("stale_temp=1 symlink_target_preserved=1 interrupted_short_writes=1 atomic_failure=1 mode_0600=1");
 return 0;
}
