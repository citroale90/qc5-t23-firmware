/* SPDX-License-Identifier: GPL-3.0-only
 * A short, private tmpfs heartbeat, updated only after a verified encoded
 * frame. It never stores an image, credential, flash or SD data.
 */
#ifndef QC5_INTEGRATED_MEDIA_STATUS_H
#define QC5_INTEGRATED_MEDIA_STATUS_H
#include "client-handoff.h"
#include "../network-native/network-files.h"
#define QMI_VIDEO 1u
#define QMI_AUDIO 2u
#define QMI_TALK 4u
#define QMI_MAX_AGE_US UINT64_C(2000000)
struct qmi_state {uint64_t timestamp,frames;unsigned flags;};
static int qmi_read(int directory,struct qmi_state *state)
{
 if(!state)return 0;memset(state,0,sizeof *state);char text[96],extra;
 unsigned long long stamp,frames;unsigned flags;
 int n=qnf_read(directory,"media-state",text,sizeof text);
 if(n<=0||sscanf(text,"QCM7 %llu %llu %u %c",&stamp,&frames,&flags,&extra)!=3)return 0;
 char canonical[96];int size=snprintf(canonical,sizeof canonical,"QCM7 %llu %llu %u\n",stamp,frames,flags);
 uint64_t now=qcf_now();
 if(size!=n||memcmp(text,canonical,(size_t)n)||!now||!stamp||stamp>now||now-stamp>QMI_MAX_AGE_US||!frames||!(flags&QMI_VIDEO)||(flags&~7u)||(flags&QMI_TALK&&!(flags&QMI_AUDIO)))return 0;
 *state=(struct qmi_state){stamp,frames,flags};return 1;
}
#ifndef QCI_FRONTEND_ONLY
struct qmi_writer {int directory;uint64_t timestamp,frames;unsigned flags;};
/* The media owner's lifetime flock serializes this private tmpfs pathname.
 * Do not call qnf_publish here: its random temporary name can invoke the
 * early kernel entropy generator, blocking the active capture for seconds.
 * O_EXCL/NOFOLLOW and fd-relative rename preserve atomic replacement without
 * randomness, durable-storage I/O, or an additional frame allocation.
 */
static int qmi_replace(int directory,const char *text,size_t bytes)
{
 if(directory<0||!text||!bytes||bytes>=96)return 0;
 static const char temporary[]=".media-state.next";struct stat st;
 if(!fstatat(directory,temporary,&st,AT_SYMLINK_NOFOLLOW)){
  if(!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&077)||st.st_nlink!=1||unlinkat(directory,temporary,0))return 0;
 }else if(errno!=ENOENT)return 0;
 int fd=openat(directory,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);if(fd<0)return 0;
 size_t used=0;while(used<bytes){ssize_t n=write(fd,text+used,bytes-used);if(n<0&&errno==EINTR)continue;if(n<=0)break;used+=(size_t)n;}
 int ok=used==bytes;if(close(fd))ok=0;
 if(ok&&renameat(directory,temporary,directory,"media-state"))ok=0;
 if(!ok)unlinkat(directory,temporary,0);return ok;
}
static int qmi_publish(struct qmi_writer *writer,uint64_t frames,unsigned flags)
{
 if(!writer||writer->directory<0||!frames||frames<writer->frames||!(flags&QMI_VIDEO)||(flags&~7u)||(flags&QMI_TALK&&!(flags&QMI_AUDIO)))return 0;
 uint64_t now=qcf_now();if(!now||now<writer->timestamp)return 0;
 if(writer->timestamp&&now-writer->timestamp<1000000&&flags==writer->flags)return 1;
 if(writer->timestamp&&frames==writer->frames)return 0;
 char text[96];int n=snprintf(text,sizeof text,"QCM7 %llu %llu %u\n",(unsigned long long)now,(unsigned long long)frames,flags);
 if(n<=0||(size_t)n>=sizeof text||!qmi_replace(writer->directory,text,(size_t)n))return 0;
 writer->timestamp=now;writer->frames=frames;writer->flags=flags;return 1;
}
static void qmi_stop(struct qmi_writer *writer)
{if(writer&&writer->directory>=0){static const char stopped[]="QCM7 0 0 0\n";(void)qmi_replace(writer->directory,stopped,sizeof stopped-1);close(writer->directory);writer->directory=-1;}}
#endif
#endif
