#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qc5_wifi_store.h"
#include "qc5_sd.h"
#include "config_read.h"
/* The gateway already has one literal parser; keep this TU independent. */
#define qc5_config_get qc5_store_config_get
#include "config_parser.h"
#undef qc5_config_get
int qc5_wifi_config_profile(const char*b,unsigned long n,struct qc5_wifi_profile*p){
 char apply[8],open_net[8];memset(p,0,sizeof(*p));
 if(qc5_store_config_get(b,n,"SSID",p->ssid,sizeof(p->ssid))<0||qc5_store_config_get(b,n,"PWD",p->password,sizeof(p->password))<0||
    qc5_store_config_get(b,n,"WIFI_OPEN",open_net,sizeof(open_net))<0||qc5_store_config_get(b,n,"WIFI_APPLY",apply,sizeof(apply))<0)return -1;
 if((apply[0]&&strcmp(apply,"0")&&strcmp(apply,"1"))||(open_net[0]&&strcmp(open_net,"0")&&strcmp(open_net,"1")))return -1;
 p->open=!strcmp(open_net,"1");if(!p->ssid[0]||!strcmp(apply,"0"))return 0;
 return qc5_wifi_valid(p)?1:-1;
}
static int wifi_key(const char*b,size_t n){
 while(n&&qc5_space(*b)){b++;n--;}
 size_t key=0;while(key<n&&b[key]!='=')key++;
 if(key==n)return 0;while(key&&qc5_space(b[key-1]))key--;
 return (key==4&&!memcmp(b,"SSID",4))||(key==3&&!memcmp(b,"PWD",3))||
        (key==10&&!memcmp(b,"WIFI_APPLY",10))||(key==9&&!memcmp(b,"WIFI_OPEN",9));
}
static int write_synced(int directory,const char*name,const void*b,size_t n){
 int fd=openat(directory,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return -1;
 size_t at=0;int failed=0;
 while(at<n){ssize_t count=write(fd,(const char*)b+at,n-at);if(count<0&&errno==EINTR)continue;if(count<=0){failed=1;break;}at+=(size_t)count;}
 if(!failed&&fsync(fd))failed=1;
 int error=errno;if(close(fd)&&!failed){failed=1;error=errno;}
 if(failed){unlinkat(directory,name,0);errno=error;return -1;}return 0;
}
int qc5_wifi_store(const char*sd,const char*seed,unsigned long seed_size,const struct qc5_wifi_profile*p){
 if(!p||!qc5_wifi_valid(p)||seed_size>8192||(!seed&&seed_size)){errno=EINVAL;return -1;}
 if(!qc5_sd_mounted(sd,1)){errno=ENODEV;return -1;}
 int dir=open(sd,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return -1;
 char old[8193],next[8193],nonce[17],fresh[48],backup[48],unused[1];long size;int existing=0,result=-1;
 struct qc5_wifi_profile previous;
 int fd=openat(dir,"config.txt",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(fd<0){if(errno!=ENOENT)goto done;if(seed_size)memcpy(old,seed,seed_size);size=(long)seed_size;}
 else{
  struct stat info;if(fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_size>8192){close(fd);errno=EINVAL;goto done;}
  size=qc5_config_read_fd(fd,old,sizeof(old));close(fd);if(size<0||size>8192){errno=EINVAL;goto done;}existing=1;
 }
 old[size]=0;
 /* Reject malformed unrelated syntax, but allow repairing invalid Wi-Fi values. */
 if(qc5_store_config_get(old,(unsigned long)size,"_QC5_SYNTAX_ONLY",unused,sizeof(unused))<0){errno=EINVAL;goto done;}
 int parsed=qc5_wifi_config_profile(old,(unsigned long)size,&previous);
 if(existing&&parsed==1&&qc5_wifi_equal(p,&previous)){result=0;goto done;}
 size_t used=0,at=0;
 /* Preserve a UTF-8 BOM and every unrelated literal line/comment. */
 if(size>=3&&!memcmp(old,"\xef\xbb\xbf",3)){memcpy(next,old,3);used=at=3;}
 while(at<(size_t)size){size_t start=at;while(at<(size_t)size&&old[at]!='\n')at++;if(at<(size_t)size)at++;
  if(!wifi_key(old+start,at-start)){if(used+at-start>=sizeof(next)){errno=EFBIG;goto done;}memcpy(next+used,old+start,at-start);used+=at-start;}
 }
 if(used&&next[used-1]!='\n'){if(used+1>=sizeof(next)){errno=EFBIG;goto done;}next[used++]='\n';}
 int count=snprintf(next+used,sizeof(next)-used,"SSID=\"%s\"\nPWD=\"%s\"\nWIFI_APPLY=1\nWIFI_OPEN=%u\n",p->ssid,p->password,p->open);
 if(count<0||(size_t)count>=sizeof(next)-used){errno=EFBIG;goto done;}used+=(size_t)count;
 struct qc5_wifi_profile check;if(qc5_wifi_config_profile(next,used,&check)!=1||!qc5_wifi_equal(&check,p)){errno=EINVAL;goto done;}
 if(qc5_random_hex(nonce))goto done;
 snprintf(fresh,sizeof(fresh),".qc5-wifi-new-%s",nonce);snprintf(backup,sizeof(backup),".qc5-wifi-old-%s",nonce);
 if(write_synced(dir,fresh,next,used))goto done;
 if(existing&&(write_synced(dir,backup,old,(size_t)size)||renameat(dir,backup,dir,".qc5-config.previous"))){int error=errno;unlinkat(dir,fresh,0);unlinkat(dir,backup,0);errno=error;goto done;}
 if(renameat(dir,fresh,dir,"config.txt")){int error=errno;unlinkat(dir,fresh,0);errno=error;goto done;}
 result=fsync(dir);
 done:{int error=errno;close(dir);memset(old,0,sizeof(old));memset(next,0,sizeof(next));memset(&previous,0,sizeof(previous));errno=error;return result;}
}
