/* SPDX-License-Identifier: GPL-3.0-only
 * Private RAM mailbox shared by the native controller and HTTP portal.
 * No credentials appear in status files; only the controller writes the SD.
 */
#ifndef QC5_NATIVE_NETWORK_FILES_H
#define QC5_NATIVE_NETWORK_FILES_H
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qc5_wifi_store.h"
#include "qc5_sd.h"
#include "config_read.h"
static int qnf_directory(const char *path)
{
 int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
 struct stat st;if(fstat(fd,&st)||st.st_uid!=geteuid()||(st.st_mode&077)){close(fd);return -1;}return fd;
}
static int qnf_read(int dir,const char *name,char *out,size_t cap)
{
 if(cap<2)return -1;
 int fd=openat(dir,name,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return -1;
 struct stat st;long n=-1;
 if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&077)&&st.st_size>0&&(uint64_t)st.st_size<cap)
  n=qc5_config_read_fd(fd,out,cap);
 close(fd);if(n<=0||(size_t)n>=cap)return -1;out[n]=0;return (int)n;
}
static int qnf_publish(int dir,const char *name,const char *text,size_t size)
{
 /* Atomic replacement, without rewriting an unchanged state four times/sec. */
 if(!size||size>12287||strchr(name,'/'))return 0;
 int old=openat(dir,name,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);struct stat st;
 if(old>=0){
  int equal=!fstat(old,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&077)&&(uint64_t)st.st_size==size;
  char block[256];size_t at=0;
  while(equal&&at<size){size_t wanted=size-at<sizeof block?size-at:sizeof block;ssize_t n=read(old,block,wanted);
   if(n<0&&errno==EINTR)continue;if(n<=0||memcmp(block,text+at,(size_t)n)){equal=0;break;}at+=(size_t)n;}
  close(old);if(equal&&at==size)return 1;
 }
 char nonce[17],temporary[64];if(qc5_random_hex(nonce))return 0;
 snprintf(temporary,sizeof temporary,".state-%s",nonce);
 int fd=openat(dir,temporary,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return 0;
 size_t at=0;while(at<size){ssize_t n=write(fd,text+at,size-at);if(n<0&&errno==EINTR)continue;if(n<=0)break;at+=(size_t)n;}
 int good=at==size;if(close(fd))good=0;
 if(good&&renameat(dir,temporary,dir,name))good=0;if(!good)unlinkat(dir,temporary,0);return good;
}
static int qnf_text(int dir,const char *name,const char *text){return qnf_publish(dir,name,text,strlen(text));}
static int qnf_busy_state(const char *state)
{return !strcmp(state,"connessione_in_corso\n")||!strcmp(state,"wifi_connesso_salvataggio_non_confermato\n");}
static int qnf_busy(int dir)
{
 struct stat st;if(!fstatat(dir,"wifi-request",&st,AT_SYMLINK_NOFOLLOW))return 1;
 char state[96];return qnf_read(dir,"wifi-native-state",state,sizeof state)>0&&qnf_busy_state(state);
}
/* Return 1 valid request, 0 none, -1 malformed/unsafe. Mark it busy before
 * unlinking, so a second POST cannot be accepted in the consume/stage gap. */
static int qnf_request(int dir,struct qc5_wifi_profile *profile)
{
 struct stat st;if(fstatat(dir,"wifi-request",&st,AT_SYMLINK_NOFOLLOW))return errno==ENOENT?0:-1;
 char text[256],canonical[256];int size=qnf_read(dir,"wifi-request",text,sizeof text);
 int valid=size>0&&qc5_wifi_config_profile(text,(unsigned long)size,profile)==1;
 if(valid){int n=snprintf(canonical,sizeof canonical,"SSID=\"%s\"\nPWD=\"%s\"\nWIFI_OPEN=%u\n",profile->ssid,profile->password,profile->open);
  valid=n==size&&!memcmp(text,canonical,(size_t)size);}
 const char *state=valid?"connessione_in_corso\n":"richiesta_non_accettata\n";
 int published=qnf_text(dir,"wifi-native-state",state);
 if(published&&unlinkat(dir,"wifi-request",0))published=0;
 memset(text,0,sizeof text);memset(canonical,0,sizeof canonical);
 if(!valid||!published){memset(profile,0,sizeof *profile);return -1;}return 1;
}
#endif
