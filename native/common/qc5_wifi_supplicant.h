#ifndef QC5_WIFI_SUPPLICANT_H
#define QC5_WIFI_SUPPLICANT_H
/* This vendor embeds wpa_supplicant in vp_project. Its AP helper frees the
 * supplicant before its detached wpa_run thread has exited. Stop the original
 * event loop and confirm that exact thread is gone BEFORE calling that helper.
 * No signals, pthread cancellation, SDK restart, configuration or flash writes. */
#ifndef QC5_SUPPLICANT_TEST
extern long syscall(long,...);
extern int poll(void*,unsigned long,int);
extern int *__errno_location(void);
struct qc5_supplicant_poll {int fd;short events,revents;};
static int qc5_supplicant_name(int tid){
 char path[64],name[17];int n=snprintf(path,sizeof(path),"/proc/self/task/%d/comm",tid);
 if(n<0||n>=(int)sizeof(path))return -1;
 int fd=open(path,0);if(fd<0)return *__errno_location()==2?0:-1;
 long size=read(fd,name,sizeof(name));close(fd);
 if(size<0)return -1;
 return size==8&&!memcmp(name,"wpa_run\n",8)?1:2;
}
static int qc5_supplicant_scan(int *tid){
 unsigned char buf[2048];int fd=open("/proc/self/task",0);if(fd<0)return -1;
 int count=0,bad=0;*tid=0;
 /* Linux MIPS O32 getdents64: 64-bit ino/off, 16-bit reclen, type, name.
  * Parse the kernel record rather than guessing uClibc struct dirent ABI. */
 for(unsigned page=0;page<16;page++){
  long n=syscall(4219L,fd,buf,sizeof(buf));if(!n)goto done;
  if(n<0||n>(long)sizeof(buf)){bad=1;break;}
  usize at=0;
  while(at<(usize)n){
   if((usize)n-at<20){bad=1;break;}
   unsigned len=(unsigned)buf[at+16]|(unsigned)buf[at+17]<<8;
   if(len<20||len>(usize)n-at||!memchr(buf+at+19,0,len-19)){bad=1;break;}
   const unsigned char *p=buf+at+19;unsigned value=0,digits=0;
   while(*p>='0'&&*p<='9'&&digits<8){value=value*10+*p++-'0';digits++;}
   if(digits&&!*p&&value>0&&value<=0x7fffffffU){
    int name=qc5_supplicant_name((int)value);
    if(name<0){bad=1;break;}
    if(name==1){if(++count>1){bad=1;break;}*tid=(int)value;}
    /* Other task names are expected; disappearing tasks are harmless. */
   }
   at+=len;
  }
  if(bad)break;
 }
 bad=1; /* A directory larger than the fixed budget is not a safe teardown. */
 done:close(fd);return bad?-1:count;
}
static int qc5_supplicant_stop(int tid){
 if(qc5_supplicant_name(tid)!=1)return -1;
 /* Version-guarded eloop_terminate and its original notification pipe. */
 #ifdef QC5_SUPPLICANT_BACKEND_TEST
 int writer=qc5_test_eloop_pipe();
#else
 int writer=*(volatile int*)0x008bfa48u;
#endif
 if(writer<3||writer>=1024)return -1;
 long flags=syscall(4055L,writer,3,0L); /* O32 fcntl F_GETFL. */
 if(flags<0||syscall(4055L,writer,4,flags|0x80L)<0)return -1;
#ifdef QC5_SUPPLICANT_BACKEND_TEST
 qc5_test_eloop_terminate();
#else
 ((void(*)(void))0x004edfccu)();
#endif
 struct qc5_supplicant_poll p={writer,4,0};
 if(poll(&p,1,0)>0&&(p.revents&4)){
  /* The native pipe consumes fixed 8-byte records. Type zero only wakes
   * select; it neither dispatches a callback nor changes any credentials. */
  const unsigned char wake[8]={0};(void)write(writer,wake,sizeof(wake));
 }
 (void)syscall(4055L,writer,4,flags);
 return 0;
}
#else
static int qc5_supplicant_scan(int*),qc5_supplicant_stop(int),qc5_supplicant_name(int);
#endif
static int qc5_supplicant_quiesce(void){
 int tid=0,count=qc5_supplicant_scan(&tid);
 if(count<0)return -1;if(!count)return 0;
 if(qc5_supplicant_stop(tid))return -1;
 for(unsigned seconds=0;seconds<8;seconds++){
  int state=qc5_supplicant_name(tid);
  if(!state){
   /* Another native caller may have started a replacement while the old
    * detached task exited. A vanished old TID alone is not enough. */
   int replacement=0;return qc5_supplicant_scan(&replacement)==0?0:-1;
  }
  if(state!=1)return -1;
  sleep(1);
 }
 /* Never free a live supplicant. The controller can retry after it exits. */
 return -1;
}
#endif
