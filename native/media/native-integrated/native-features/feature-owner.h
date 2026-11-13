/* SPDX-License-Identifier: GPL-3.0-only
 * Runs in the existing capture owner. One small local command per frame;
 * no detached encoder, full-frame copy, vendor process or cloud endpoint.
 */
#ifndef QC5_FEATURE_OWNER_H
#define QC5_FEATURE_OWNER_H
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <stddef.h>
#include "feature-abi.h"
#include "record-runtime.h"
#include "motion-local.h"
#include "../native-continuous/image-mode.h"
struct qfs_owner {struct qdi_owner image;struct qrr_recording record;struct qml_motion detector;int listener,pending;uint64_t pending_deadline;unsigned initialized,failed,sensitivity,last_gain,last_exposure;};
static uint64_t qfs_epoch(void)
{
 /* Never reuse a build nonce as a recording ID after every reboot. Also
  * never block capture while the entropy pool is still starting. */
 uint64_t value=0;if(getrandom(&value,sizeof value,GRND_NONBLOCK)==sizeof value&&value)return value;
 int fd=open("/proc/sys/kernel/random/boot_id",O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 char uuid[38];ssize_t n=read(fd,uuid,sizeof uuid);close(fd);if(n!=37||uuid[36]!='\n')return 0;
 value=UINT64_C(14695981039346656037);
 for(unsigned i=0;i<36;i++){
  unsigned char c=uuid[i];if(i==8||i==13||i==18||i==23){if(c!='-')return 0;}
  else if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return 0;
  value=(value^c)*UINT64_C(1099511628211);
 }return value;
}
static int qfs_init(struct qfs_owner *s,const char *path,uint64_t epoch,const char *directory,const char *program)
{
 if(!s||!path||strlen(path)>=sizeof(((struct sockaddr_un*)0)->sun_path)||!qrr_init(&s->record,epoch,directory,program))return 0;
 s->listener=s->pending=-1;s->sensitivity=60;
 struct sockaddr_un address={.sun_family=AF_UNIX};strcpy(address.sun_path,path);
 /* The run directory is privately owned by init; do not unlink arbitrary
  * existing nodes. A restart must explicitly remove its old socket in init. */
 int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return 0;
 if(bind(fd,(struct sockaddr*)&address,sizeof address)||chmod(path,0600)||listen(fd,2)){close(fd);return 0;}
 s->listener=fd;s->initialized=1;return 1;
}
static void qfs_status(const struct qfs_owner *s,struct qfs_status *r,unsigned serial,unsigned ok)
{
 *r=(struct qfs_status){.magic=QFS_MAGIC,.serial=serial,.ok=ok,.phase=s->record.phase,
  .continuous=s->record.policy.continuous,.motion=s->record.policy.motion,.inhibit=s->record.policy.inhibit,
  .moving=s->record.moving,.sensitivity=s->sensitivity,.failed=s->record.failed||s->failed,
  .frames=s->record.mux.frames,.samples=s->record.mux.samples,.clips=s->record.clips,.errors=s->record.errors};
}
static int qfs_apply(struct qfs_owner *s,const struct qfs_request *p,struct qfs_status *r,uint64_t now)
{
 unsigned ok=0;
 if(s&&p&&r&&p->magic==QFS_MAGIC&&p->serial){
  switch(p->operation){
  case QFS_STATUS:ok=p->value==0;break;
  case QFS_CONTINUOUS:ok=qrr_configure(&s->record,p->value,s->record.policy.motion,now);break;
  case QFS_MOTION:ok=qrr_configure(&s->record,s->record.policy.continuous,p->value,now);break;
  case QFS_STOP:ok=p->value==0&&qrr_stop(&s->record,now);break;
  case 6:if(p->value>=1&&p->value<=60){s->record.post_us=(uint64_t)p->value*1000000;ok=1;}break;
  case QFS_SENSITIVITY:if(p->value<=100){s->sensitivity=p->value;s->detector.seeded=s->detector.hits=0;ok=1;}break;
  }
 }
 if(!s||!r)return 0;qfs_status(s,r,p?p->serial:0,ok);return 1;
}
static void qfs_commands(struct qfs_owner *s,uint64_t now)
{
 if(!s||s->listener<0)return;
 int fd=s->pending;
 if(fd<0){fd=accept4(s->listener,NULL,NULL,SOCK_NONBLOCK|SOCK_CLOEXEC);if(fd<0)return;s->pending=fd;s->pending_deadline=now+200000;}
 struct ucred peer;socklen_t bytes=sizeof peer;
 if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&bytes)||bytes!=sizeof peer||peer.uid!=geteuid()){close(fd);s->pending=-1;return;}
 struct qfs_request p;struct qfs_status reply;char oversized[sizeof p+1];
 ssize_t n=recv(fd,oversized,sizeof oversized,MSG_DONTWAIT);
 if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)&&now<s->pending_deadline)return;
 if(n==sizeof p){
  memcpy(&p,oversized,sizeof p);
  if(p.magic==QDI_MAGIC){struct qdi_request image;struct qdi_status status;memcpy(&image,&p,sizeof image);if(qdi_apply(&s->image,&image,&status,now))(void)send(fd,&status,sizeof status,MSG_DONTWAIT|MSG_NOSIGNAL);}
  else if(qfs_apply(s,&p,&reply,now))(void)send(fd,&reply,sizeof reply,MSG_DONTWAIT|MSG_NOSIGNAL);
 }
 close(fd);s->pending=-1;
}
static int qfs_luma(struct qfs_owner *s,const uint8_t *y,unsigned width,unsigned height,unsigned stride,
 unsigned gain,unsigned exposure,unsigned filter_transition,uint64_t now)
{
 if(!s||!s->initialized)return 1;
 qfs_commands(s,now);
 (void)qdi_sample(&s->image,y,width,height,stride,exposure,gain,now);
 /* A recorder failure must not take image control or video down with it. */
 if(s->failed)return 1;
 if(!qrr_tick(&s->record,now)){s->failed=1;return 1;}
 unsigned transition=filter_transition||now<s->image.hold_until;
 if(s->detector.seeded){unsigned a=exposure>s->last_exposure?exposure-s->last_exposure:s->last_exposure-exposure;
  if(gain!=s->last_gain||a>32)transition=1;}
 s->last_gain=gain;s->last_exposure=exposure;unsigned moving=0;
 if(!qml_sample(&s->detector,y,width,height,stride,s->sensitivity,transition,now,&moving)||!qrr_activity(&s->record,now,moving))s->failed=1;
 return 1;
}
static int qfs_video(struct qfs_owner *s,const uint8_t *au,size_t n,uint64_t now,unsigned key)
{return !s||!s->initialized||s->failed?1:qrr_video(&s->record,au,n,now,key);}
static int qfs_audio(struct qfs_owner *s,const uint8_t *pcma,size_t n,uint64_t now)
{return !s||!s->initialized||s->failed?1:qrr_audio(&s->record,pcma,n,now);}
static void qfs_close(struct qfs_owner *s,uint64_t now)
{if(!s||!s->initialized)return;qrr_stop(&s->record,now);if(s->listener>=0)close(s->listener);if(s->pending>=0)close(s->pending);s->listener=s->pending=-1;}
#endif
