/*
 * Camera-side segment destination worker. Bounded 32 KiB transfer buffer.
 * Reads literal config data; never executes shell commands or logs credentials.
 * Copies completed segments, or streams stdin, to SD / SMB2+ / passive FTP.
 * It never deletes its input and never publishes a partial transfer as complete.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include "config_parser.h"
#include "qc5_sd.h"
#include "qc5_version.h"

#define CHUNK 32768
struct settings {
 char target[12],sdroot[512],path[384],host[256],share[128];
 char user[128],password[256],domain[128];
 unsigned port,timeout;
};
static char config[8193],buffer[CHUNK];
static uint64_t transferred;
static uint64_t input_read_bytes;
static int file_input;
static int verify_existing;
static struct stat input_snapshot;
static const char*error_code="failed";
static const char*protocol="local";
static char smb_protocol[24];
static int result(int ok,const char*target){
 printf("{\"ok\":%s,\"target\":\"%s\",\"protocol\":\"%s\",\"bytes\":%llu,\"published\":%s",
        ok?"true":"false",target,protocol,(unsigned long long)transferred,ok?"true":"false");
 if(!ok)printf(",\"error\":\"%s\"",error_code);
 puts("}");return ok?0:1;
}
static int field(const char*key,char*out,size_t cap,const char*default_value){
 int r=qc5_config_get(config,strlen(config),key,out,cap);
 if(r<0)return -1;
 if(!r&&default_value){if(strlen(default_value)>=cap)return -1;strcpy(out,default_value);}
 return 0;
}
static int number(const char*key,unsigned*value,unsigned initial,unsigned maximum){
 char text[24];if(field(key,text,sizeof(text),NULL))return -1;
 if(!text[0]){*value=initial;return 0;}
 char*end;errno=0;unsigned long n=strtoul(text,&end,10);
 if(errno||*end||!n||n>maximum)return -1;
 *value=(unsigned)n;return 0;
}
static int line_safe(const char*s){return !strchr(s,'\r')&&!strchr(s,'\n');}
static int relative_path(const char*s){
 size_t length=strlen(s);
 if(*s=='/'||*s=='\\'||(length&&s[length-1]=='/'))return 0;
 for(const char*p=s;*p;){
  const char*e=strchr(p,'/');if(!e)e=p+strlen(p);
  size_t n=(size_t)(e-p);
  if(!n||(n==1&&p[0]=='.')||(n==2&&p[0]=='.'&&p[1]=='.'))return 0;
  for(const char*q=p;q<e;q++)if((unsigned char)*q<32||*q=='\\')return 0;
  p=*e?e+1:e;
 }
 return 1;
}
static int safe_id(const char*s,size_t max,int dot){
 size_t n=strlen(s);if(!n||n>max||*s=='.')return 0;
 for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)s[i])&&s[i]!='-'&&s[i]!='_'&&(!dot||s[i]!='.'))return 0;
 return 1;
}
static int load_settings(const char*path,struct settings*s){
 memset(s,0,sizeof(*s));FILE*f=fopen(path,"rb");if(!f)return -1;
 size_t n=fread(config,1,sizeof(config),f);int failed=ferror(f);fclose(f);
 if(failed||n>=sizeof(config)||memchr(config,0,n))return -1;
 config[n]=0;
 if(field("RECORD_TARGET",s->target,sizeof(s->target),"sd")||
    field("RECORD_SD_ROOT",s->sdroot,sizeof(s->sdroot),"/mnt/sda0")||
    field("RECORD_PATH",s->path,sizeof(s->path),"recordings")||
    number("RECORD_IO_TIMEOUT",&s->timeout,8,60)||!relative_path(s->path))return -1;
 if(!strcmp(s->target,"sd"))return 0;
 const char*prefix=!strcmp(s->target,"smb")?"SMB":!strcmp(s->target,"ftp")?"FTP":NULL;
 if(!prefix)return -1;
 char key[40];
#define READ_REMOTE(name,member) do{snprintf(key,sizeof(key),"%s_%s",prefix,name);if(field(key,s->member,sizeof(s->member),NULL))return -1;}while(0)
 READ_REMOTE("HOST",host);READ_REMOTE("USER",user);READ_REMOTE("PASSWORD",password);
#undef READ_REMOTE
 if(!s->host[0]||!s->user[0])return -1;
 if(!strcmp(s->target,"smb")){
  if(field("SMB_SHARE",s->share,sizeof(s->share),NULL)||!s->share[0]||
     strchr(s->share,'/')||strchr(s->share,'\\')||
     field("SMB_DOMAIN",s->domain,sizeof(s->domain),""))return -1;
 }else{
  if(number("RECORD_FTP_PORT",&s->port,21,65535)||!line_safe(s->user)||!line_safe(s->password)||!line_safe(s->path))return -1;
 }
 return 0;
}
static int name_paths(const struct settings*s,const char*id,const char*name,char*directory,char*final,char*part,size_t cap){
 if(!safe_id(id,79,0)||!safe_id(name,96,1))return -1;
 int n=snprintf(directory,cap,"%s%s%s",s->path,s->path[0]?"/":"",id);
 if(n<0||(size_t)n>=cap)return -1;
 n=snprintf(final,cap,"%s/%s",directory,name);
 if(n<0||(size_t)n>=cap)return -1;
 char hex[17];if(qc5_random_hex(hex))return -1;
 n=snprintf(part,cap,"%s.%s.part",final,hex);
 return n<0||(size_t)n>=cap?-1:0;
}
static int copy_read(int input,ssize_t*count){
 size_t capacity=sizeof(buffer);
 if(file_input){
  if(input_read_bytes==(uint64_t)input_snapshot.st_size){
   struct stat current;
   if(fstat(input,&current)||current.st_size!=input_snapshot.st_size||
      current.st_mtim.tv_sec!=input_snapshot.st_mtim.tv_sec||current.st_mtim.tv_nsec!=input_snapshot.st_mtim.tv_nsec||
      current.st_ctim.tv_sec!=input_snapshot.st_ctim.tv_sec||current.st_ctim.tv_nsec!=input_snapshot.st_ctim.tv_nsec){
    error_code="input_changed";*count=-1;return -1;
   }
   *count=0;return 0;
  }
  uint64_t left=(uint64_t)input_snapshot.st_size-input_read_bytes;
  if(left<capacity)capacity=(size_t)left;
 }
 do{*count=read(input,buffer,capacity);}while(*count<0&&errno==EINTR);
 if(*count<0){error_code="input_read_failed";return -1;}
 if(file_input&&!*count){error_code="input_changed";*count=-1;return -1;}
 input_read_bytes+=(uint64_t)*count;return 0;
}
static int write_fd(int fd,const void*bytes,size_t count){
 const char*p=bytes;
 while(count){ssize_t n=write(fd,p,count);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;count-=(size_t)n;}
 return 0;
}
typedef ssize_t (*existing_reader)(void*,void*,size_t);
static ssize_t existing_fd_read(void*context,void*out,size_t n){
 int fd=*(int*)context;ssize_t got;do{got=read(fd,out,n);}while(got<0&&errno==EINTR);return got;
}
/* A crash after publishing but before saving the receipt is safe to retry:
 * compare the entire existing segment, rather than accepting size alone. */
static int compare_existing(int input,existing_reader reader,void*context){
 if(!verify_existing||!file_input||lseek(input,0,SEEK_SET)<0){error_code="already_exists";return -1;}
 input_read_bytes=transferred=0;unsigned char other[4096];ssize_t n;
 while(!copy_read(input,&n)&&n>0){
  size_t at=0;
  while(at<(size_t)n){
   size_t wanted=(size_t)n-at;if(wanted>sizeof(other))wanted=sizeof(other);
   ssize_t got=reader(context,other,wanted);
   if(got<=0||(size_t)got>wanted||memcmp(buffer+at,other,(size_t)got)){error_code="existing_content_differs";return -1;}
   at+=(size_t)got;transferred+=(uint64_t)got;
  }
 }
 if(n<0)return -1;
 if(reader(context,other,1)!=0){error_code="existing_content_differs";return -1;}
 return transferred?0:-1;
}
static int sd_directory(const char *root,const char *relative){
 int fd=qc5_sd_directory(root,relative,1,1);
 if(fd<0)error_code=errno==ENODEV?"sd_not_mounted":"sd_directory_failed";
 return fd;
}
static int store_sd(const struct settings*s,int input,const char*directory,const char*final,const char*part){
 int dir=sd_directory(s->sdroot,directory);if(dir<0)return -1;
 const char*name=strrchr(final,'/')+1,*temporary=strrchr(part,'/')+1;
 int lock=openat(dir,".qc5-write.lock",O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
 struct flock held;memset(&held,0,sizeof(held));held.l_type=F_WRLCK;held.l_whence=SEEK_SET;
 if(lock<0||fcntl(lock,F_SETLK,&held)<0){
  error_code=errno==EACCES||errno==EAGAIN?"destination_busy":"sd_lock_failed";
  if(lock>=0)close(lock);
  close(dir);return -1;
 }
 struct stat existing;
 if(fstatat(dir,name,&existing,AT_SYMLINK_NOFOLLOW)==0){
  int same=-1;
  if(verify_existing&&file_input&&S_ISREG(existing.st_mode)&&existing.st_size==input_snapshot.st_size){
   int prior=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
   if(prior>=0){same=compare_existing(input,existing_fd_read,&prior);close(prior);}
  }else error_code="already_exists";
  close(lock);close(dir);return same;
 }
 int fd=openat(dir,temporary,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW|O_CLOEXEC,0600);
 if(fd<0){close(lock);close(dir);return -1;}int ok=0;ssize_t n;
 while(!copy_read(input,&n)&&n>0){
  struct statvfs space;
  if(fstatvfs(fd,&space)||((uint64_t)space.f_bavail*space.f_frsize)<(uint64_t)n+1024*1024){error_code="sd_full";goto done;}
  if(write_fd(fd,buffer,(size_t)n)){error_code="sd_write_failed";goto done;}
  transferred+=(uint64_t)n;
 }
 if(n<0||!transferred)goto done;
 if(fsync(fd)){error_code="sd_sync_failed";goto done;}
 if(fstatat(dir,name,&existing,AT_SYMLINK_NOFOLLOW)==0){error_code="already_exists";goto done;}
 if(renameat(dir,temporary,dir,name)){error_code="sd_publish_failed";goto done;}
 (void)fsync(dir);ok=1;
 done:close(fd);if(!ok)unlinkat(dir,temporary,0);close(lock);close(dir);return ok?0:-1;
}
struct ftp {int control,data;unsigned timeout;};
static int socket_connect(const struct sockaddr*address,socklen_t length,unsigned timeout){
 int fd=socket(address->sa_family,SOCK_STREAM,0);if(fd<0)return -1;
 int flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)){close(fd);return -1;}
 int r=connect(fd,address,length);
 if(r<0&&errno!=EINPROGRESS){close(fd);return -1;}
 if(r<0){struct pollfd p={fd,POLLOUT,0};if(poll(&p,1,(int)timeout*1000)<=0){close(fd);return -1;}
  int error=0;socklen_t size=sizeof(error);if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)||error){close(fd);return -1;}}
 if(fcntl(fd,F_SETFL,flags)){close(fd);return -1;}
 struct timeval duration={(time_t)timeout,0};
 setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&duration,sizeof(duration));
 setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&duration,sizeof(duration));return fd;
}
static int connect_host(const char*host,unsigned port,unsigned timeout){
 char service[12];snprintf(service,sizeof(service),"%u",port);
 struct addrinfo hints,*addresses=NULL;memset(&hints,0,sizeof(hints));
 hints.ai_socktype=SOCK_STREAM;hints.ai_family=AF_UNSPEC;
 if(getaddrinfo(host,service,&hints,&addresses))return -1;
 int fd=-1;for(struct addrinfo*p=addresses;p;p=p->ai_next)if((fd=socket_connect(p->ai_addr,p->ai_addrlen,timeout))>=0)break;
 freeaddrinfo(addresses);return fd;
}
static long long monotonic_ms(void){
 struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return -1;
 return (long long)now.tv_sec*1000+now.tv_nsec/1000000;
}
static int ftp_reply(struct ftp*f,char*out,size_t cap){
 long long now=monotonic_ms();if(now<0)return -1;
 long long deadline=now+(long long)f->timeout*1000;
 char line[1024];int multiline=0,first=0;size_t total=0;
 for(int lines=0;lines<64;lines++){
  size_t n=0;
  while(n+1<sizeof(line)){
   now=monotonic_ms();if(now<0||now>=deadline)return -1;
   struct pollfd ready={f->control,POLLIN,0};int available=poll(&ready,1,(int)(deadline-now));
   if(available<0&&errno==EINTR)continue;
   if(available<=0)return -1;
   char c;ssize_t r=read(f->control,&c,1);
   if(r<0&&(errno==EINTR||errno==EAGAIN))continue;
   if(r!=1)return -1;
   line[n++]=c;if(c=='\n')break;
  }
  line[n]=0;total+=n;if(!n||line[n-1]!='\n'||total>16384)return -1;
  if(lines==0){
   if(n<4||!isdigit((unsigned char)line[0])||!isdigit((unsigned char)line[1])||!isdigit((unsigned char)line[2]))return -1;
   first=(line[0]-'0')*100+(line[1]-'0')*10+line[2]-'0';multiline=line[3]=='-';
  }
  int code=n>=4?(line[0]-'0')*100+(line[1]-'0')*10+line[2]-'0':0;
  if(!multiline||(code==first&&line[3]==' ')){if(out&&cap){if(n>=cap)return -1;memcpy(out,line,n+1);}return first;}
 }return -1;
}
static int ftp_command(struct ftp*f,char*reply,size_t cap,const char*verb,const char*arg){
 char command[1200];if(arg&&!line_safe(arg))return -1;
 int n=snprintf(command,sizeof(command),"%s%s%s\r\n",verb,arg?" ":"",arg?arg:"");
 if(n<0||(size_t)n>=sizeof(command)||write_fd(f->control,command,(size_t)n))return -1;
 return ftp_reply(f,reply,cap);
}
static int ftp_passive(struct ftp*f){
 char text[1024]={0};int code=ftp_command(f,text,sizeof(text),"EPSV",NULL),port=0;
 if(code==229){
  char*p=strchr(text,'(');if(!p||!p[1]||p[2]!=p[1]||p[3]!=p[1])return -1;
  char*end;long value=strtol(p+4,&end,10);if(end==p+4||*end!=p[1]||value<1||value>65535)return -1;port=(int)value;
 }else{
  code=ftp_command(f,text,sizeof(text),"PASV",NULL);if(code!=227)return -1;
  unsigned a,b,c,d,high,low;char*p=strchr(text,'(');
  if(!p||sscanf(p,"(%u,%u,%u,%u,%u,%u)",&a,&b,&c,&d,&high,&low)!=6||high>255||low>255)return -1;
  port=(int)(high*256+low);if(!port)return -1;
 }
 struct sockaddr_storage peer;memset(&peer,0,sizeof(peer));socklen_t length=sizeof(peer);
 if(getpeername(f->control,(struct sockaddr*)&peer,&length))return -1;
 /* Ignore the PASV-advertised address: data always goes to the configured server. */
 if(peer.ss_family==AF_INET)((struct sockaddr_in*)&peer)->sin_port=htons((uint16_t)port);
 else if(peer.ss_family==AF_INET6)((struct sockaddr_in6*)&peer)->sin6_port=htons((uint16_t)port);
 else return -1;
 return socket_connect((struct sockaddr*)&peer,length,f->timeout);
}
static int store_ftp(const struct settings*s,int input,const char*directory,const char*final,const char*part){
 struct ftp f={-1,-1,s->timeout};int ok=0;ssize_t n=0;char reply[1024];
 protocol="ftp-passive";
 f.control=connect_host(s->host,s->port,s->timeout);
 if(f.control<0){error_code="ftp_connection_failed";goto done;}
 if(ftp_reply(&f,NULL,0)!=220)goto done;
 int code=ftp_command(&f,NULL,0,"USER",s->user);
 if(code==331)code=ftp_command(&f,NULL,0,"PASS",s->password);
 if(code!=230){error_code="ftp_authentication_failed";goto done;}
 if(ftp_command(&f,NULL,0,"TYPE","I")!=200)goto done;
 char path[768];strcpy(path,directory);char*save=NULL;
 for(char*p=strtok_r(path,"/",&save);p;p=strtok_r(NULL,"/",&save)){
  code=ftp_command(&f,NULL,0,"CWD",p);
  if(code!=250){
   if(ftp_command(&f,NULL,0,"MKD",p)!=257||ftp_command(&f,NULL,0,"CWD",p)!=250){error_code="ftp_directory_failed";goto done;}
  }
 }
 const char*name=strrchr(final,'/')+1,*temporary=strrchr(part,'/')+1;
 code=ftp_command(&f,reply,sizeof(reply),"SIZE",name);
 if(code==213){
  unsigned long long size=0;
  if(!verify_existing||!file_input){error_code="already_exists";goto done;}
  if(sscanf(reply+3,"%llu",&size)!=1||size!=(uint64_t)input_snapshot.st_size){error_code="existing_content_differs";goto done;}
  f.data=ftp_passive(&f);if(f.data<0)goto done;
  code=ftp_command(&f,NULL,0,"RETR",name);if(code!=125&&code!=150)goto done;
  int compared=compare_existing(input,existing_fd_read,&f.data);close(f.data);f.data=-1;
  if(compared||ftp_reply(&f,NULL,0)!=226)goto done;
  ok=1;goto done;
 }
 if(code!=550){error_code="ftp_size_unavailable";goto done;}
 f.data=ftp_passive(&f);if(f.data<0)goto done;
 code=ftp_command(&f,NULL,0,"STOR",temporary);if(code!=125&&code!=150)goto done;
 while(!copy_read(input,&n)&&n>0){
  if(write_fd(f.data,buffer,(size_t)n)){error_code="ftp_write_failed";goto done;}
  transferred+=(uint64_t)n;
 }
 if(n<0||!transferred)goto done;
 shutdown(f.data,SHUT_WR);close(f.data);f.data=-1;
 if(ftp_reply(&f,NULL,0)!=226){error_code="ftp_transfer_unconfirmed";goto done;}
 code=ftp_command(&f,reply,sizeof(reply),"SIZE",temporary);
 unsigned long long remote=0;
 if(code!=213||sscanf(reply+3,"%llu",&remote)!=1||remote!=transferred){error_code="ftp_size_mismatch";goto done;}
 code=ftp_command(&f,NULL,0,"SIZE",name);
 if(code==213){error_code="already_exists";goto done;}
 if(code!=550){error_code="ftp_size_unavailable";goto done;}
 if(ftp_command(&f,NULL,0,"RNFR",temporary)!=350||ftp_command(&f,NULL,0,"RNTO",name)!=250){error_code="ftp_publish_failed";goto done;}
 ok=1;
 done:if(f.data>=0)close(f.data);if(f.control>=0)close(f.control);return ok?0:-1;
}
struct existing_smb {struct smb2_context*context;struct smb2fh*file;};
static ssize_t existing_smb_read(void*context,void*out,size_t n){struct existing_smb*r=context;return smb2_read(r->context,r->file,out,(uint32_t)n);}
static int store_smb(const struct settings*s,int input,const char*directory,const char*final,const char*part){
 struct smb2_context*c=smb2_init_context();struct smb2fh*f=NULL;int connected=0,ok=0;ssize_t n=0;
 if(!c)return -1;
 smb2_set_timeout(c,(int)s->timeout);smb2_set_user(c,s->user);smb2_set_password(c,s->password);
 if(s->domain[0])smb2_set_domain(c,s->domain);
 smb2_set_security_mode(c,SMB2_NEGOTIATE_SIGNING_ENABLED);
 if(smb2_connect_share(c,s->host,s->share,s->user)){
#ifdef QC5_HOST_TEST
  if(getenv("QC5_STORE_DEBUG"))fprintf(stderr,"Samba fixture: %s\n",smb2_get_error(c));
#endif
  error_code="smb_connection_failed";goto done;
 }connected=1;
 uint16_t dialect=smb2_get_dialect(c);
 if(dialect<0x0202){error_code="smb_protocol_unsupported";goto done;}
 snprintf(smb_protocol,sizeof(smb_protocol),"smb-%04x",dialect);protocol=smb_protocol;
 char path[768];strcpy(path,directory);
 for(char*p=path;*p;p++)if(*p=='/'){*p=0;int r=smb2_mkdir(c,path);*p='/';if(r&&r!=-EEXIST){error_code="smb_directory_failed";goto done;}}
 int mkdir_result=smb2_mkdir(c,path);
 if(mkdir_result&&mkdir_result!=-EEXIST){error_code="smb_directory_failed";goto done;}
 struct smb2_stat_64 info;
 int exists=smb2_stat(c,final,&info);
 if(!exists){
  if(!verify_existing||!file_input){error_code="already_exists";goto done;}
  if(info.smb2_size!=(uint64_t)input_snapshot.st_size){error_code="existing_content_differs";goto done;}
  f=smb2_open(c,final,O_RDONLY);if(!f){error_code="smb_open_existing_failed";goto done;}
  struct existing_smb reader={c,f};
  if(compare_existing(input,existing_smb_read,&reader))goto done;
  ok=1;goto done;
 }
 if(exists!=-ENOENT){error_code="smb_stat_failed";goto done;}
 f=smb2_open(c,part,O_CREAT|O_EXCL|O_WRONLY);if(!f){error_code="smb_create_failed";goto done;}
 while(!copy_read(input,&n)&&n>0){
  size_t at=0;
  while(at<(size_t)n){int count=smb2_write(c,f,(uint8_t*)buffer+at,(uint32_t)((size_t)n-at));
   if(count<=0){error_code="smb_write_failed";goto done;}at+=(size_t)count;transferred+=(uint64_t)count;}
 }
 if(n<0||!transferred)goto done;
 if(smb2_fsync(c,f)){error_code="smb_sync_failed";goto done;}
 if(smb2_close(c,f)){f=NULL;error_code="smb_close_failed";goto done;}f=NULL;
 if(smb2_stat(c,part,&info)||info.smb2_size!=transferred){error_code="smb_size_mismatch";goto done;}
 exists=smb2_stat(c,final,&info);
 if(!exists){error_code="already_exists";goto done;}
 if(exists!=-ENOENT){error_code="smb_stat_failed";goto done;}
 if(smb2_rename(c,part,final)){error_code="smb_publish_failed";goto done;}
 ok=1;
 done:if(f)smb2_close(c,f);if(connected)smb2_disconnect_share(c);smb2_destroy_context(c);return ok?0:-1;
}
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("QC5_STORE_VERSION:" QC5_VERSION);return 0;}
 const char*configuration=NULL,*input_path=NULL,*name=NULL,*id=NULL;int check=0,file_stdin=0;
 for(int inherited=3;inherited<4096;inherited++)close(inherited);
 umask(0077);signal(SIGPIPE,SIG_IGN);
 for(int i=1;i<argc;i++){
  if(!strcmp(argv[i],"--check")){check=1;continue;}
  if(!strcmp(argv[i],"--verify-existing")){verify_existing=1;continue;}
  if(!strcmp(argv[i],"--file-stdin")){file_stdin=1;continue;}
  if(i+1>=argc)return 2;
  const char*key=argv[i++],*value=argv[i];
  if(!strcmp(key,"--config"))configuration=value;
  else if(!strcmp(key,"--input"))input_path=value;
  else if(!strcmp(key,"--name"))name=value;
  else if(!strcmp(key,"--camera-id"))id=value;
  else return 2;
 }
 struct settings s;memset(&s,0,sizeof(s));
 if(!configuration||load_settings(configuration,&s)){error_code="configuration_invalid";return result(0,"unknown");}
 if(check){printf("{\"ok\":true,\"target\":\"%s\",\"transfer_started\":false}\n",s.target);memset(&s,0,sizeof(s));return 0;}
 char directory[768],final[768],part[768];
 if(!input_path||!name||!id||name_paths(&s,id,name,directory,final,part,sizeof(final))){error_code="name_invalid";return result(0,s.target);}
 if(file_stdin&&strcmp(input_path,"-")){error_code="input_invalid";return result(0,s.target);}
 file_input=file_stdin||strcmp(input_path,"-")!=0;
 int input=file_input&&!file_stdin?open(input_path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW):STDIN_FILENO;
 if(input<0){error_code="input_unavailable";return result(0,s.target);}
 if(file_input&&(fstat(input,&input_snapshot)||!S_ISREG(input_snapshot.st_mode))){
  close(input);error_code="input_not_regular";return result(0,s.target);
 }
 error_code="transfer_failed";
 int success=!strcmp(s.target,"sd")?store_sd(&s,input,directory,final,part):
             !strcmp(s.target,"smb")?store_smb(&s,input,directory,final,part):store_ftp(&s,input,directory,final,part);
 if(file_input)close(input);
 char target[sizeof(s.target)];memcpy(target,s.target,sizeof(target));memset(&s,0,sizeof(s));memset(config,0,sizeof(config));
 return result(success==0,target);
}
