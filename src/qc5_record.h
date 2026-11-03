/* Included after qc5_talk.h. All paths are confined to the current device's
 * native SD recordings. No encoder, vendor restart or shell is started. */
#include "qc5_record_files.h"
static char record_token[33];
static pid_t record_download_pid;
static pid_t record_export_pid;
static int record_exports;
static char record_export_bin[512]="/qc5/bin/qc5-record-worker",record_store_bin[512]="/qc5/bin/qc5-store";
static long long record_export_next;
#include "qc5_record_policy_state.h"
static struct qc5_rec_policy_state record_policy;
static int record_policy_initialized,record_policy_capable;
static long long record_policy_post=5000;
static int record_policy_action(int,int);
static int record_policy_initialize(void),record_policy_apply(void);
static int record_config_store(int,int);
static int record_counter(uint32_t*);
static int record_identity(char id[64]){
 if(get_status(0))return -1;
 const char*p=strstr(status_cache,"realdeviceid=");
 if(!p)return -1;
 p+=13;while(*p==' '||*p=='\"'||*p=='\'')p++;
 size_t n=0;while(n<63&&((unsigned char)*p>32)&&*p!='\"'&&*p!='\''&&*p!=';')id[n++]=*p++;
 id[n]=0;return qc5_record_id(id)?0:-1;
}
static void record_export_tick(void){
 if(!record_exports||now_ms()<record_export_next)return;record_export_next=now_ms()+10000;
 reap_one(&record_export_pid);if(load_config())return;
 char destination[12];cfg("RECORD_TARGET",destination,sizeof(destination));
 if(strcmp(destination,"smb")&&strcmp(destination,"ftp")){stop_one(&record_export_pid);return;}
 if(record_export_pid||qc5_memory_available_kb()<2300)return;
 char id[64];if(record_identity(id))return;
 pid_t pid=fork();if(pid<0)return;
 if(!pid){
  for(int fd=3;fd<4096;fd++)close(fd);
  execl(record_export_bin,record_export_bin,"--config",config_path,"--sd",sdroot,"--serial",id,"--run-dir",provision_run,"--store-bin",record_store_bin,(char*)0);_exit(127);
 }
 record_export_pid=pid;
}
static void record_export_field(const char*body,const char*field,char*out,size_t cap){
 out[0]=0;char needle[40];snprintf(needle,sizeof(needle),"\"%s\":\"",field);const char*p=strstr(body,needle);if(!p)return;p+=strlen(needle);
 size_t n=0;while(n+1<cap&&(islower((unsigned char)p[n])||isdigit((unsigned char)p[n])||p[n]=='_'))n++;
 if(p[n]!='\"')return;memcpy(out,p,n);out[n]=0;
}
static void record_export_state(char phase[40],char error[80]){
 strcpy(phase,record_exports?"waiting":"disabled");error[0]=0;
 if(!record_exports||!record_export_pid)return;
 char path[600],body[512];snprintf(path,sizeof(path),"%s/record-export.json",provision_run);
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return;
 struct stat st;ssize_t n=-1;if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&(size_t)st.st_size<sizeof(body))n=read(fd,body,sizeof(body)-1);close(fd);if(n<=0)return;body[n]=0;
 record_export_field(body,"phase",phase,40);record_export_field(body,"error",error,80);
}
static int record_integer(const char*text,const char*key,int*out){
 size_t n=strlen(key);
 for(const char*p=text;(p=strstr(p,key));p++){
  if(p>text&&(isalnum((unsigned char)p[-1])||p[-1]=='_'))continue;
  const char*q=p+n;while(*q==' '||*q=='\t')q++;if(*q++!='=')continue;
  while(*q==' '||*q=='\"'||*q=='\'')q++;
  char*end;errno=0;long v=strtol(q,&end,10);
  if(end==q||errno||v<INT32_MIN||v>INT32_MAX)return -1;
  if(*end!=';'&&*end!='\"'&&*end!='\''&&*end!='\r'&&*end!='\n'&&*end)return -1;
  *out=(int)v;return 0;
 }
 return -1;
}
static int record_native_plan(int type,int*enabled,int values[21]){
 if(type<1||type>3)return -1;
 char params[64];snprintf(params,sizeof(params),"cmd=2017&command=11&type=%d",type);
 if(camera_rpc("trans_cmd_string",params,3000))return -1;
 const char*s=(char*)response+8;int cmd=-1,command=-1,echoed=-1,status=-1;
 if(field_int(s,"cmd",&cmd)||cmd!=2017||field_int(s,"command",&command)||command!=11||field_int(s,"type",&echoed)||echoed!=type||field_int(s,"status",&status)||status)return -1;
 const char*prefix=type==1?"motion_record":type==2?"motion_push":"record";
 char key[40];snprintf(key,sizeof(key),"%s%s",prefix,type==3?"_plan_enable":"_enable");
 if(record_integer(s,key,enabled)||*enabled<0||*enabled>(type==2?5:1))return -1;
 for(int i=0;i<21;i++){snprintf(key,sizeof(key),"%s_plan%d",prefix,i+1);if(record_integer(s,key,values+i))return -1;}
 return 0;
}
static int record_plan(int*enabled,int values[21]){return record_native_plan(3,enabled,values);}
static int record_open_files(const char*id,const struct stat*match){
#ifdef QC5_HOST_TEST
 const char*fixture_path=getenv("QC5_TEST_RECORD_ACTIVE_FILE");
 if(fixture_path&&*fixture_path){
  char value[3];int f=open(fixture_path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(f<0)return -1;
  ssize_t n=read(f,value,sizeof(value));close(f);
  return n==2&&value[1]=='\n'&&(value[0]=='0'||value[0]=='1')?value[0]-'0':-1;
 }
 const char*fixture_active=getenv("QC5_TEST_RECORD_ACTIVE");if(fixture_active&&(!strcmp(fixture_active,"0")||!strcmp(fixture_active,"1")))return fixture_active[0]-'0';
#endif
 /* Native app is the only producer. Check its fds rather than inferring
  * recording activity from the legacy record_time_enable field. */
 DIR*procs=opendir("/proc");if(!procs)return -1;
 struct dirent*entry;int active=0;
 while((entry=readdir(procs))){
  char*end;long pid=strtol(entry->d_name,&end,10);if(*end||pid<=0)continue;
  char path[128],comm[32];snprintf(path,sizeof(path),"/proc/%ld/comm",pid);
  FILE*f=fopen(path,"r");if(!f)continue;int ok=fgets(comm,sizeof(comm),f)!=NULL;fclose(f);
  if(!ok||strcmp(comm,"vp_project\n"))continue;
  snprintf(path,sizeof(path),"/proc/%ld/fd",pid);DIR*fds=opendir(path);if(!fds){active=-1;break;}
  struct dirent*e;
  while((e=readdir(fds))){
   if(e->d_name[0]=='.')continue;
   char link[180],target[768],prefix[600];
   int k=snprintf(link,sizeof(link),"%s/%s",path,e->d_name);if(k<0||(size_t)k>=sizeof(link))continue;
   ssize_t n=readlink(link,target,sizeof(target)-1);if(n<=0)continue;target[n]=0;
   snprintf(prefix,sizeof(prefix),"%s/%s/video/",sdroot,id);
   size_t length=strlen(target);
   int final=!strncmp(target,prefix,strlen(prefix))&&length>=4&&!strcmp(target+length-4,".mp4");
   snprintf(prefix,sizeof(prefix),"%s/.",sdroot);
   int temporary=!strncmp(target,prefix,strlen(prefix))&&length>=8&&!strcmp(target+length-8,".mp4.tmp");
   if(!final&&!temporary)continue;
   if(match){struct stat st;if(stat(link,&st)||st.st_dev!=match->st_dev||st.st_ino!=match->st_ino)continue;}
   active=1;break;
  }
  closedir(fds);if(active)break;
 }
 closedir(procs);return active;
}
static void record_status(int fd){
 char id[64];int enabled=0,values[21],custom=0;
 if(record_identity(id)||record_plan(&enabled,values)){result(fd,0,"Registratore nativo non raggiungibile");return;}
 for(int i=0;i<21;i++)if(values[i]!=-1&&values[i]!=0)custom=1;
 int total=0,free_mb=0,audio=0,minutes=0,sd=0;
 if(camera_rpc("get_record","",3000)){result(fd,0,"Stato SD non disponibile");return;}
 field_int((char*)response+8,"sdtotal",&total);field_int((char*)response+8,"sdfree",&free_mb);
 field_int((char*)response+8,"record_audio",&audio);field_int((char*)response+8,"record_timer",&minutes);field_int((char*)response+8,"record_sd_status",&sd);
 char body[1400],target[16],phase[40],error[80];cfg("RECORD_TARGET",target,sizeof(target));if(strcmp(target,"smb")&&strcmp(target,"ftp"))strcpy(target,"sd");record_export_state(phase,error);
 int active=record_open_files(id,NULL)>0;
 int logical=record_policy_initialized?record_policy.continuous:enabled;
 const char*mode=logical?"continuous":record_policy_capable&&record_policy.event_active?"motion":active?"stopping":"idle";
 int n=snprintf(body,sizeof(body),"{\"ok\":true,\"token\":\"%s\",\"status\":{\"enabled\":%s,\"recording\":%s,\"mode\":\"%s\",\"motion_suppressed\":%s,\"custom_schedule\":%s,\"sd_ready\":%s,\"total_mb\":%d,\"free_mb\":%d,\"audio\":%s,\"segment_minutes\":%d,\"target\":\"%s\",\"exporter\":{\"available\":%s,\"running\":%s,\"phase\":\"%s\",\"error\":\"%s\"}}}",record_token,logical?"true":"false",active?"true":"false",mode,record_policy.inhibit?"true":"false",custom?"true":"false",sd==1&&qc5_sd_mounted(sdroot,0)?"true":"false",total,free_mb,audio?"true":"false",minutes,target,record_exports?"true":"false",record_export_pid?"true":"false",phase,error);
 http_reply(fd,200,"application/json",body,(size_t)n);
}
struct record_item {char key[80];long long size;};
static void record_list(int fd){
 char id[64];if(record_identity(id)){result(fd,0,"Identita della camera non disponibile");return;}
 int root=qc5_record_root(sdroot,id,0);if(root<0){result(fd,0,"Registrazioni sulla SD non disponibili");return;}
 struct record_item items[64];int count=0,visited=0,truncated=0;DIR*days=fdopendir(root);if(!days){close(root);result(fd,0,"SD non leggibile");return;}
 struct dirent*day;
 while((day=readdir(days))){
  if(!qc5_record_day(day->d_name))continue;
  int directory=qc5_record_day_directory(root,day->d_name);if(directory<0)continue;
  DIR*files=fdopendir(directory);if(!files){close(directory);continue;}
  struct dirent*entry;
  while((entry=readdir(files))){
   if(!qc5_record_name(entry->d_name))continue;
   if(++visited>8192){truncated=1;break;}
   char key[80];snprintf(key,sizeof(key),"%.10s/%.63s",day->d_name,entry->d_name);
   int at=0;while(at<count&&strcmp(items[at].key,key)>0)at++;
   if(at==64){truncated=1;continue;}
   struct stat st;int file=qc5_record_open(root,key,0,&st);if(file<0)continue;close(file);
   if(count<64)count++;else truncated=1;
   for(int j=count-1;j>at;j--)items[j]=items[j-1];
   strcpy(items[at].key,key);items[at].size=(long long)st.st_size;
  }
  closedir(files);if(visited>8192)break;
 }
 closedir(days);
 char body[15000];size_t used=(size_t)snprintf(body,sizeof(body),"{\"ok\":true,\"truncated\":%s,\"files\":[",truncated?"true":"false");
 for(int i=0;i<count;i++)used+=(size_t)snprintf(body+used,sizeof(body)-used,"%s{\"name\":\"%s\",\"size_bytes\":%lld,\"size_mb\":\"%.2f\",\"source\":\"sd\"}",i?",":"",items[i].key,items[i].size,items[i].size/1048576.0);
 memcpy(body+used,"]}",3);http_reply(fd,200,"application/json",body,used+2);
}
static int record_range(const char*headers,long long size,long long*first,long long*last){
 char range[100];int present=header_value(headers,"Range",range,sizeof(range));
 if(!present){*first=0;*last=size-1;return 0;}
 if(present<0||strncmp(range,"bytes=",6)||strchr(range,','))return -1;
 const char*p=range+6;char*end;errno=0;
 if(*p=='-'){
  p++;if(*p<'0'||*p>'9')return -1;long long suffix=strtoll(p,&end,10);
  if(errno||*end||suffix<=0)return -1;
  *first=suffix<size?size-suffix:0;*last=size-1;return 1;
 }
 if(*p<'0'||*p>'9')return -1;*first=strtoll(p,&end,10);if(errno||*end!='-'||*first>=size)return -1;
 p=end+1;if(!*p){*last=size-1;return 1;}
 if(*p<'0'||*p>'9')return -1;errno=0;*last=strtoll(p,&end,10);
 if(errno||*end||*last<*first)return -1;if(*last>=size)*last=size-1;return 1;
}
static void record_reader_ready(int fd){
 reap_one(&record_download_pid);
 const char*body=(!record_download_pid&&qc5_memory_available_kb()>=QC5_RECORD_DOWNLOAD_ADMISSION_KB)?"{\"ok\":true,\"ready\":true}":"{\"ok\":true,\"ready\":false}";
 http_reply(fd,200,"application/json",body,strlen(body));
}
static int record_download_memory_ok(long long now,long long*checked){
 if(*checked>=0&&now>=*checked&&now-*checked<QC5_MEMORY_POLL_MS)return 1;
 *checked=now;
 if(qc5_memory_available_kb()>=QC5_RECORD_DOWNLOAD_RESERVE_KB)return 1;
 fprintf(stderr,"QC5_RECORD_DOWNLOAD_MEMORY_LIMIT\n");return 0;
}
static int record_download_send(int fd,const void*data,size_t n,long long*checked){
 const unsigned char*p=data;long long end=now_ms()+2500;
 while(n&&!stopping&&now_ms()<end){
  if(!record_download_memory_ok(now_ms(),checked))return -1;
  struct pollfd s={fd,POLLOUT,0};if(poll(&s,1,100)<=0)continue;
  ssize_t k=send(fd,p,n,MSG_NOSIGNAL|MSG_DONTWAIT);
  if(k<0&&(errno==EINTR||errno==EAGAIN))continue;if(k<=0)return -1;
  p+=k;n-=(size_t)k;
 }
 return n?-1:0;
}
static void record_download(int fd,const char*q,const char*headers){
 char id[64],key[80];struct stat st;
 if(query(q,"file",key,sizeof(key))!=1||record_identity(id)){http_reply(fd,400,"text/plain","File non valido",15);return;}
 int root=qc5_record_root(sdroot,id,0),file=root<0?-1:qc5_record_open(root,key,0,&st);if(root>=0)close(root);
 if(file<0){http_reply(fd,404,"text/plain","Video assente o ancora in scrittura",34);return;}
 long long first,last;int partial=record_range(headers,(long long)st.st_size,&first,&last);
 if(partial<0){char h[160];int n=snprintf(h,sizeof(h),"HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */%lld\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",(long long)st.st_size);send_all(fd,h,(size_t)n);close(file);return;}
 /* Safari probes bytes 0-1 before requesting the real range. Answer tiny
  * probes directly: otherwise the probe child may still be exiting when
  * the next request arrives, producing a spurious "reader busy" response. */
 int probe=partial&&last-first<16;
 if(!probe){
  reap_one(&record_download_pid);
  if(record_download_pid||qc5_memory_available_kb()<QC5_RECORD_DOWNLOAD_ADMISSION_KB){close(file);http_reply(fd,503,"text/plain","Lettore occupato: riprova",24);return;}
  pid_t pid=fork();if(pid<0){close(file);http_reply(fd,503,"text/plain","Lettore non disponibile",23);return;}
  if(pid){record_download_pid=pid;close(file);return;}
 }
 /* One bounded download child keeps controls, Wi-Fi and media responsive. */
 if(!probe)for(int i=3;i<4096;i++)if(i!=fd&&i!=file)close(i);
 stopping=0;char h[400],range[120]="";
 if(partial)snprintf(range,sizeof(range),"Content-Range: bytes %lld-%lld/%lld\r\n",first,last,(long long)st.st_size);
 int n=snprintf(h,sizeof(h),"HTTP/1.1 %d %s\r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\n%sContent-Length: %lld\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",partial?206:200,partial?"Partial Content":"OK",range,last-first+1);
 long long checked=-1;
 if((probe||record_download_memory_ok(now_ms(),&checked))&&send_all(fd,h,(size_t)n)==0){char data[16384];long long pos=first,deadline=now_ms()+120000;
  while(!stopping&&pos<=last&&now_ms()<deadline){
   if(!probe&&!record_download_memory_ok(now_ms(),&checked))break;
   size_t wanted=(size_t)(last-pos+1);if(wanted>sizeof(data))wanted=sizeof(data);
   ssize_t got=pread(file,data,wanted,(off_t)pos);
   if(got<=0||(probe?send_all(fd,data,(size_t)got):record_download_send(fd,data,(size_t)got,&checked)))break;pos+=got;
  }
 }
 close(file);if(!probe){close(fd);_exit(0);}
}
static void record_change(int fd,const char*action,const char*body){
 char token[33],key[80];if(query(body,"token",token,sizeof(token))!=1||strcmp(token,record_token)){http_reply(fd,403,"text/plain","Riapri la pagina",16);return;}
 if(!strcmp(action,"delete")){
  char id[64];struct stat st;
  if(query(body,"file",key,sizeof(key))!=1||record_identity(id)){result(fd,0,"File non valido");return;}
  int root=qc5_record_root(sdroot,id,1),file=root<0?-1:qc5_record_open(root,key,0,&st);if(file<0){if(root>=0)close(root);result(fd,0,"Video assente o ancora in scrittura");return;}
  if(record_open_files(id,&st)!=0){close(file);close(root);result(fd,0,"Il registratore sta usando il file");return;}
  char day[11];memcpy(day,key,10);day[10]=0;int directory=qc5_record_day_directory(root,day);close(root);
  struct stat current;int ok=directory>=0&&!fstatat(directory,key+11,&current,AT_SYMLINK_NOFOLLOW)&&current.st_dev==st.st_dev&&current.st_ino==st.st_ino&&current.st_size==st.st_size&&current.st_mtime==st.st_mtime&&unlinkat(directory,key+11,0)==0;
  if(ok)fsync(directory);if(directory>=0)close(directory);close(file);result(fd,ok,ok?"Registrazione eliminata":"File cambiato: riprova");return;
 }
 int enable=!strcmp(action,"start"),stop_now=!strcmp(action,"stop-now");
 if(strcmp(action,"start")&&strcmp(action,"stop")&&!stop_now){result(fd,0,"Azione non valida");return;}
 int ok=record_policy_action(enable,stop_now)==0;
 result(fd,ok,ok?(stop_now?"Registrazione fermata; nuovi eventi dopo che la scena torna ferma":enable?"Registrazione continua abilitata":"Registrazione continua disabilitata"):"Modifica non confermata: controlla SD e piano di registrazione");
}
/* Shared bounded form reader for recording and motion. Values remain encoded
 * until the caller validates them; duplicate or unknown keys are refused. */
static int control_form(int fd,const char*headers,char*body,size_t available,long long deadline,const char*const*keys,unsigned key_count,unsigned required){
 char length_text[24],type[80],transfer[40];
 int found=header_value(headers,"Content-Length",length_text,sizeof(length_text));
 if(found!=1||header_value(headers,"Content-Type",type,sizeof(type))!=1||strcmp(type,"application/x-www-form-urlencoded")||header_value(headers,"Transfer-Encoding",transfer,sizeof(transfer))!=0){http_reply(fd,400,"text/plain","Richiesta non valida",20);return 0;}
 size_t length=0;for(const char*p=length_text;*p;p++){if(*p<'0'||*p>'9'||length>512){http_reply(fd,400,"text/plain","Richiesta non valida",20);return 0;}length=length*10+(unsigned)(*p-'0');}
 if(!length||length>512||available>length){http_reply(fd,400,"text/plain","Richiesta non valida",20);return 0;}
 while(available<length&&now_ms()<deadline){struct pollfd p={fd,POLLIN,0};if(poll(&p,1,100)<=0)continue;ssize_t n=recv(fd,body+available,length-available,MSG_DONTWAIT);if(n<0&&(errno==EINTR||errno==EAGAIN))continue;if(n<=0)break;available+=(size_t)n;}
 if(available!=length||memchr(body,0,length)){http_reply(fd,400,"text/plain","Richiesta non valida",20);return 0;}body[length]=0;
 /* Reject duplicated/unknown form keys before query() extracts values. */
 unsigned seen=0;for(const char*p=body;*p;){const char*end=strchr(p,'&');if(!end)end=p+strlen(p);const char*eq=memchr(p,'=',(size_t)(end-p));size_t n=eq?(size_t)(eq-p):0;unsigned bit=0;for(unsigned i=0;i<key_count;i++)if(strlen(keys[i])==n&&!memcmp(p,keys[i],n))bit=1u<<i;if(!bit||(seen&bit)){http_reply(fd,400,"text/plain","Richiesta non valida",20);return 0;}seen|=bit;p=*end?end+1:end;}
 if(seen!=required){http_reply(fd,400,"text/plain","Richiesta non valida",20);return 0;}
 return 1;
}
static void record_post(int fd,const char*action,const char*headers,char*body,size_t available,long long deadline){
 static const char*const keys[]={"token","file"};
 if(!control_form(fd,headers,body,available,deadline,keys,2,!strcmp(action,"delete")?3u:1u))return;
 record_change(fd,action,body);
}
