#ifndef QC5_MOTION_IPC_H
#define QC5_MOTION_IPC_H
/* Bounded, nonblocking RAM-only event transport. No /proc/PID/mem reader. */
struct qc5_motion_ipc {
 int fd;uint32_t sequence;long long last_ms;
 unsigned char partial[8];unsigned used;
};
static int qc5_motion_ipc_open(struct qc5_motion_ipc*p,const char*path){
 struct stat before,after;
 if(mkfifo(path,0600)&&errno!=EEXIST)return -1;
 if(lstat(path,&before)||!S_ISFIFO(before.st_mode)||before.st_uid!=geteuid()||(before.st_mode&0022)){errno=EPERM;return -1;}
 int fd=open(path,O_RDWR|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
 if(fstat(fd,&after)||before.st_ino!=after.st_ino||before.st_dev!=after.st_dev||!S_ISFIFO(after.st_mode)){close(fd);errno=EPERM;return -1;}
 p->fd=fd;p->used=0;return 0;
}
static int qc5_motion_ipc_read(struct qc5_motion_ipc*p,long long(*clock_ms)(void)){
 if(p->fd<0)return -1;
 unsigned char data[256];
 for(unsigned reads=0;reads<16;reads++){
  ssize_t n=read(p->fd,data,sizeof(data));
  if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))return 0;
  if(n<0&&errno==EINTR)continue;
  if(n<0){close(p->fd);p->fd=-1;return -1;}
  if(!n)return 0;
  long long now=clock_ms(); /* Sample after read: a producer may run while we read. */
  for(ssize_t i=0;i<n;i++){
   p->partial[p->used++]=data[i];if(p->used<8)continue;
   if(memcmp(p->partial,"QC5M",4)){memmove(p->partial,p->partial+1,7);p->used=7;continue;}
   uint32_t stamp=le32(p->partial+4),age=(uint32_t)now-stamp;
   p->used=0;
   /* Same monotonic clock, including wrap after 49 days. A future or very
    * old packet cannot start a historical recording after a busy control. */
   if(age<=60000&&now>=(long long)age){if(now-age>p->last_ms)p->last_ms=now-age;p->sequence++;}
  }
 }
 return 0;
}
#endif
