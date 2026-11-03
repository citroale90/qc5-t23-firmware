/* Serial completed-segment exporter. The native recorder owns SD rotation;
 * this process never removes or modifies a source recording. */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "config_parser.h"
#include "qc5_record_files.h"
#include "qc5_memory.h"
#include "qc5_version.h"
#include "qc5_child.h"
static const char*config_path,*serial,*sdroot="/mnt/sda0",*run="/tmp/qc5-hack",*store="/qc5/bin/qc5-store";
static char text[8193],target[8],namespace[192];
static int include_existing,once;
static volatile sig_atomic_t stop;
static unsigned long copied;
static pid_t pending_transfer;
static long long milliseconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static void signal_stop(int s){(void)s;stop=1;}
static int field(const char*key,char*out,size_t n){return qc5_config_get(text,strlen(text),key,out,n)<0?-1:0;}
static uint64_t tag_bytes(uint64_t h,const char*s){do{h^=(unsigned char)*s;h*=1099511628211ULL;}while(*s++);return h;}
static int settings(void){
 int fd=open(config_path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
 size_t n=0;while(n<sizeof(text)){ssize_t got=read(fd,text+n,sizeof(text)-n);if(got<0&&errno==EINTR)continue;if(got<0){close(fd);return -1;}if(!got)break;n+=(size_t)got;}
 close(fd);if(n>=sizeof(text)||memchr(text,0,n))return -1;text[n]=0;
 char backfill[8],value[512];if(field("RECORD_TARGET",target,sizeof(target))||field("RECORD_EXPORT_EXISTING",backfill,sizeof(backfill)))return -1;
 if(!strcmp(target,"sd")||!target[0])return 1;
 const char*prefix=!strcmp(target,"smb")?"SMB":!strcmp(target,"ftp")?"FTP":NULL;if(!prefix)return -1;
 if(backfill[0]&&strcmp(backfill,"0")&&strcmp(backfill,"1"))return -1;include_existing=!strcmp(backfill,"1");
 uint64_t h=14695981039346656037ULL;h=tag_bytes(h,target);
 const char*fields[]={"HOST","SHARE","USER","DOMAIN",NULL};
 for(int i=0;fields[i];i++){char key[32];snprintf(key,sizeof(key),"%s_%s",prefix,fields[i]);if(field(key,value,sizeof(value)))return -1;h=tag_bytes(h,value);}
 if(field("RECORD_PATH",value,sizeof(value)))return -1;h=tag_bytes(h,value[0]?value:"recordings");
 if(field("RECORD_FTP_PORT",value,sizeof(value)))return -1;h=tag_bytes(h,value[0]?value:"21");
 snprintf(namespace,sizeof(namespace),"qc5/%s/export/%016llx",serial,(unsigned long long)h);
 memset(value,0,sizeof(value));return 0;
}
static int atomic_text(int dir,const char*name,const char*value){
 char nonce[17],temporary[40];if(qc5_random_hex(nonce))return -1;snprintf(temporary,sizeof(temporary),".new-%s",nonce);
 int fd=openat(dir,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return -1;
 size_t n=strlen(value),at=0;while(at<n){ssize_t k=write(fd,value+at,n-at);if(k<0&&errno==EINTR)continue;if(k<=0)break;at+=(size_t)k;}
 int bad=at!=n||fsync(fd);if(close(fd))bad=1;
 if(!bad&&renameat(dir,temporary,dir,name)==0){if(fsync(dir)==0)return 0;return -1;}
 unlinkat(dir,temporary,0);return -1;
}
static void status(const char*phase,const char*error){
 int d=open(run,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(d<0)return;
 char b[320];snprintf(b,sizeof(b),"{\"phase\":\"%s\",\"target\":\"%s\",\"copied\":%lu,\"error\":\"%s\"}\n",phase,target,copied,error?error:"");
 atomic_text(d,"record-export.json",b);close(d);
}
static int fingerprint(char out[192],const struct stat*s){
 return snprintf(out,192,"QC5_EXPORT_1\n%llu %llu %llu %lld %ld %lld %ld\n",(unsigned long long)s->st_dev,(unsigned long long)s->st_ino,(unsigned long long)s->st_size,(long long)s->st_mtim.tv_sec,s->st_mtim.tv_nsec,(long long)s->st_ctim.tv_sec,s->st_ctim.tv_nsec);
}
static int receipt_dir(int state,const char*key,int create){
 char day[11];memcpy(day,key,10);day[10]=0;
 if(create&&mkdirat(state,day,0700)&&errno!=EEXIST)return -1;
 return openat(state,day,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
}
static int receipt_matches(int state,const char*key,const struct stat*s){
 int dir=receipt_dir(state,key,0);if(dir<0)return 0;
 char name[80],expected[192],data[193];snprintf(name,sizeof(name),"%s.done",key+11);int wanted=fingerprint(expected,s);
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);close(dir);if(fd<0)return 0;
 struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==wanted;
 ssize_t n=ok?read(fd,data,sizeof(data)):-1;close(fd);return n==wanted&&!memcmp(data,expected,(size_t)wanted);
}
static int receipt_write(int state,const char*key,const struct stat*s){
 int dir=receipt_dir(state,key,1);if(dir<0)return -1;
 char name[80],data[192];snprintf(name,sizeof(name),"%s.done",key+11);fingerprint(data,s);int r=atomic_text(dir,name,data);close(dir);return r;
}
static int cutoff(int state,time_t*out){
 int fd=openat(state,"start",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);char b[40];
 if(fd>=0){struct stat st;ssize_t n=-1;if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<39)n=read(fd,b,sizeof(b)-1);close(fd);if(n<=0)return -1;b[n]=0;char*end;errno=0;long long v=strtoll(b,&end,10);if(errno||v<0||strcmp(end,"\n"))return -1;*out=(time_t)v;return 0;}
 if(errno!=ENOENT)return -1;
 time_t now=time(NULL);if(!include_existing&&now<1577836800)return 1;
 *out=include_existing?0:now;snprintf(b,sizeof(b),"%lld\n",(long long)*out);return atomic_text(state,"start",b);
}
static int select_segment(int root,int state,time_t after,char selected[80],struct stat*s){
 int copy=dup(root);DIR*days=copy<0?NULL:fdopendir(copy);if(!days){if(copy>=0)close(copy);return -1;}
 struct dirent*day;int count=0;selected[0]=0;
 while((day=readdir(days))&&!stop){
  if(!qc5_record_day(day->d_name))continue;
  int dir=qc5_record_day_directory(root,day->d_name);if(dir<0)continue;DIR*files=fdopendir(dir);if(!files){close(dir);continue;}
  struct dirent*entry;
  while((entry=readdir(files))&&!stop){
   if(!qc5_record_name(entry->d_name))continue;if(++count>16384)break;
   char key[80];snprintf(key,sizeof(key),"%.10s/%.63s",day->d_name,entry->d_name);
   if(selected[0]&&strcmp(key,selected)>=0)continue;
   struct stat st;int fd=qc5_record_open(root,key,0,&st);if(fd<0)continue;close(fd);
   if(st.st_mtime<after||receipt_matches(state,key,&st))continue;
   strcpy(selected,key);*s=st;
  }
  closedir(files);if(count>16384)break;
 }
 closedir(days);return selected[0]?1:0;
}
static void oom_priority(const char*value){int fd=open("/proc/self/oom_score_adj",O_WRONLY|O_CLOEXEC);if(fd>=0){ssize_t n=write(fd,value,strlen(value));(void)n;close(fd);}}
static int export_segment(int input,const char*key,char error[80]){
 char id[80],name[96];snprintf(id,sizeof(id),"qc5-%s",serial);for(char*p=id;*p;p++)*p=(char)tolower((unsigned char)*p);
 snprintf(name,sizeof(name),"%s",key);name[10]='_';
 /* The destination must stay paired with its receipt if config.txt changes
  * while a transfer is starting. This private RAM copy is never logged. */
 char nonce[17],snapshot[64],config_snapshot[768];int configdir=open(run,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(configdir<0||qc5_random_hex(nonce)){if(configdir>=0)close(configdir);return -1;}
 snprintf(snapshot,sizeof(snapshot),".record-config-%s",nonce);
 if(atomic_text(configdir,snapshot,text)){close(configdir);return -1;}
 if(snprintf(config_snapshot,sizeof(config_snapshot),"%s/%s",run,snapshot)>=(int)sizeof(config_snapshot)){unlinkat(configdir,snapshot,0);close(configdir);return -1;}
 int p[2];if(pipe(p)){unlinkat(configdir,snapshot,0);close(configdir);return -1;}pid_t pid=fork();if(pid<0){close(p[0]);close(p[1]);unlinkat(configdir,snapshot,0);close(configdir);return -1;}
 if(!pid){
  dup2(input,0);dup2(p[1],1);int null=open("/dev/null",O_WRONLY);if(null>=0)dup2(null,2);
  for(int fd=3;fd<4096;fd++)close(fd);oom_priority("500");
  execl(store,store,"--config",config_snapshot,"--input","-","--file-stdin","--name",name,"--camera-id",id,"--verify-existing",(char*)0);_exit(127);
 }
 memset(text,0,sizeof(text));
 close(p[1]);
 if(fcntl(p[0],F_SETFL,O_NONBLOCK)){
  close(p[0]);pending_transfer=pid;(void)qc5_child_stop(&pending_transfer,NULL,1000,200);
  unlinkat(configdir,snapshot,0);close(configdir);strcpy(error,"transfer_pipe_unavailable");return -1;
 }
 char reply[2048];size_t used=0;long long transfer_ms=120000;
#ifdef QC5_HOST_TEST
 const char *fixture=getenv("QC5_TEST_TRANSFER_TIMEOUT_MS");
 if(fixture){char *end;long n=strtol(fixture,&end,10);if(!*end&&n>0&&n<=120000)transfer_ms=n;}
#endif
 long long deadline=milliseconds()+transfer_ms;int result=0,finished=0,reply_eof=0;
 while(!stop&&milliseconds()<deadline){
  if(!reply_eof&&used<sizeof(reply)-1){ssize_t n=read(p[0],reply+used,sizeof(reply)-1-used);if(n>0)used+=(size_t)n;else if(!n)reply_eof=1;}
  if(waitpid(pid,&result,WNOHANG)==pid){finished=1;break;}
  /* EOF/POLLHUP and a full bounded reply buffer are always ready. Polling
   * either would burn a CPU core until the two-minute transfer deadline. */
  struct pollfd ready={!reply_eof&&used<sizeof(reply)-1?p[0]:-1,POLLIN,0};poll(&ready,1,100);
 }
 /* A clean exit caused by our cancellation is still an interrupted
  * transfer. Never turn an early success-looking line into a receipt. */
 int cancelled=!finished;
 if(cancelled){pending_transfer=pid;(void)qc5_child_stop(&pending_transfer,&result,1000,200);}
 while(used<sizeof(reply)-1){ssize_t n=read(p[0],reply+used,sizeof(reply)-1-used);if(n<=0)break;used+=(size_t)n;}close(p[0]);reply[used]=0;
 unlinkat(configdir,snapshot,0);close(configdir);
 if(!cancelled&&finished&&WIFEXITED(result)&&!WEXITSTATUS(result)&&strstr(reply,"\"ok\":true"))return 0;
 strcpy(error,stop?"stopped":cancelled?"transfer_timeout":"transfer_failed");char*e=strstr(reply,"\"error\":\"");if(!cancelled&&e){e+=9;size_t n=0;while(n<79&&(islower((unsigned char)e[n])||e[n]=='_'))n++;if(n&&e[n]=='\"'){memcpy(error,e,n);error[n]=0;}}
 return -1;
}
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("QC5_RECORD_WORKER_VERSION:" QC5_VERSION);return 0;}
 for(int i=1;i<argc;i++){
  if(!strcmp(argv[i],"--once")){once=1;continue;}if(i+1>=argc)return 2;const char*k=argv[i++],*v=argv[i];
  if(!strcmp(k,"--config"))config_path=v;else if(!strcmp(k,"--serial"))serial=v;else if(!strcmp(k,"--sd"))sdroot=v;else if(!strcmp(k,"--run-dir"))run=v;else if(!strcmp(k,"--store-bin"))store=v;else return 2;
 }
 if(!config_path||!serial||!qc5_record_id(serial))return 2;
 umask(0077);signal(SIGTERM,signal_stop);signal(SIGINT,signal_stop);signal(SIGPIPE,SIG_IGN);oom_priority("250");unsigned failures=0;int last=0;
 do{
  int r=settings();if(r){strcpy(target,"sd");status(r>0?"disabled":"configuration_error",r>0?NULL:"configuration_invalid");return r>0?0:1;}
  if(pending_transfer>0&&qc5_child_reap(&pending_transfer,NULL)<=0){status("waiting_transfer_stop","transfer_stopping");last=1;goto wait;}
  int state=qc5_sd_directory(sdroot,namespace,1,1);if(state<0){status("waiting_sd","sd_not_mounted");last=1;goto wait;}
  int lock=openat(state,"worker.lock",O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);struct flock held={0};held.l_type=F_WRLCK;held.l_whence=SEEK_SET;
  if(lock<0||fcntl(lock,F_SETLK,&held)){if(lock>=0)close(lock);close(state);status("busy","worker_busy");return 1;}
  time_t after;r=cutoff(state,&after);if(r){status(r>0?"waiting_clock":"state_error",r>0?"clock_unset":"state_unwritable");close(lock);close(state);last=r<0;goto wait;}
  if(qc5_memory_available_kb()<2560){status("waiting_memory",NULL);close(lock);close(state);last=0;goto wait;}
  int root=qc5_record_root(sdroot,serial,0);if(root<0){status("waiting_recordings",NULL);close(lock);close(state);last=0;goto wait;}
  char key[80];struct stat before;r=select_segment(root,state,after,key,&before);
  if(r>0){
   int fd=qc5_record_open(root,key,0,&before);char error[80]="input_unavailable";int ok=0;
   if(fd>=0){status("transferring",NULL);ok=export_segment(fd,key,error)==0;close(fd);}
   if(ok&&receipt_write(state,key,&before)==0){copied++;failures=0;last=0;status("idle",NULL);}else{failures++;last=1;status("retry",ok?"receipt_unwritable":error);}
  }else{last=r<0;status(r<0?"scan_error":"idle",r<0?"source_unavailable":NULL);}
  close(root);close(lock);close(state);
  wait:memset(text,0,sizeof(text));if(once||stop)break;
  unsigned delay=failures?(failures<6?failures*30:180):10;for(unsigned i=0;i<delay&&!stop;i++)sleep(1);
 }while(!stop);
 if(stop)status("stopped",NULL);return last;
}
