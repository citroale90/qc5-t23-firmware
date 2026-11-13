/* SPDX-License-Identifier: GPL-3.0-only
 * Atomic RAM publication under the helper's exclusive lock/private directory.
 * A killed previous invocation may leave a temp file; never truncate through it.
 */
#ifndef QC5_SSH_KEY_FILE_H
#define QC5_SSH_KEY_FILE_H
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "key-core.h"
#ifndef QKF_WRITE
#define QKF_WRITE write
#endif
static int qks_publish_key(const char *path,const u8 key[QKS_KEY])
{
 char temp[256];int n=snprintf(temp,sizeof temp,"%s.flash-next",path);
 if(n<=0||(size_t)n>=sizeof temp)return -1;
 if(unlink(temp)&&errno!=ENOENT)return -1;
 int fd=open(temp,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
 if(fd<0)return -1;
 size_t used=0;int good=1;
 while(used<QKS_KEY){
  ssize_t wrote=QKF_WRITE(fd,key+used,QKS_KEY-used);
  if(wrote<0&&errno==EINTR)continue;
  if(wrote<=0){good=0;break;}used+=(size_t)wrote;
 }
 if(good){int rc;do rc=fsync(fd);while(rc<0&&errno==EINTR);if(rc)good=0;}
 if(close(fd))good=0;
 if(good&&!rename(temp,path))return 0;
 unlink(temp);return -1;
}
#endif
