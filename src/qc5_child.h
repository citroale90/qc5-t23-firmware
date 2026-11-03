#ifndef QC5_CHILD_H
#define QC5_CHILD_H
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#ifndef QC5_CHILD_WAITPID
#define QC5_CHILD_WAITPID waitpid
#endif
#ifndef QC5_CHILD_KILL
#define QC5_CHILD_KILL kill
#endif
#ifndef QC5_CHILD_NOW
static inline long long qc5_child_clock(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
#define QC5_CHILD_NOW qc5_child_clock
#endif
#ifndef QC5_CHILD_PAUSE
static inline void qc5_child_pause(void){struct timespec t={0,10000000};(void)nanosleep(&t,0);}
#define QC5_CHILD_PAUSE qc5_child_pause
#endif
/* Keep ownership while a child is alive, including uninterruptible kernel
 * sleep. ECHILD clears stale ownership before any signal can target a reused
 * PID. Every wait is nonblocking; SIGKILL does not guarantee immediate exit. */
static inline int qc5_child_reap(pid_t *pid,int *status){
 if(*pid<=0)return 1;
 pid_t r=QC5_CHILD_WAITPID(*pid,status,WNOHANG);
 if(r==*pid||(r<0&&errno==ECHILD)){*pid=0;return 1;}
 return r<0&&errno!=EINTR?-1:0;
}
static inline int qc5_child_wait(pid_t *pid,int *status,unsigned milliseconds){
 long long start=QC5_CHILD_NOW();if(start<0)return -1;
 long long end=start+milliseconds;
 for(;;){
  int r=qc5_child_reap(pid,status);if(r)return r;
  long long now=QC5_CHILD_NOW();if(now<0)return -1;if(now>=end)return 0;
  QC5_CHILD_PAUSE();
 }
}
/* 0: gone, 1: still owned/pending, -1: error with ownership retained.
 * A stuck child must not turn service shutdown into an unbounded wait. */
static inline int qc5_child_stop(pid_t *pid,int *status,unsigned grace_ms,unsigned kill_ms){
 int r=qc5_child_reap(pid,status);if(r)return r>0?0:-1;
 if(QC5_CHILD_KILL(*pid,SIGTERM)&&errno!=ESRCH)return -1;
 r=qc5_child_wait(pid,status,grace_ms);if(r>0)return 0;
 if(QC5_CHILD_KILL(*pid,SIGKILL)&&errno!=ESRCH)return -1;
 r=qc5_child_wait(pid,status,kill_ms);return r>0?0:r<0?-1:1;
}
#endif
