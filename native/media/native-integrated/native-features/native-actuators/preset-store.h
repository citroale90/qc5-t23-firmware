/* SPDX-License-Identifier: GPL-3.0-only
 * Atomic, bounded preset persistence. Never persists a current position or
 * a calibrated flag: a process restart must establish its reference again.
 */
#ifndef QC5_PRESET_STORE_H
#define QC5_PRESET_STORE_H
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <unistd.h>
#include "ptz-controller.h"
#define QPT_FILE ".ptz-presets-v2"
static int qpt_number(const char *first,const char *end,unsigned maximum,unsigned *out)
{
 if(!first||!end||!out||first==end)return 0;unsigned value=0;
 for(const char *p=first;p<end;p++){if(*p<'0'||*p>'9'||value>maximum/10)return 0;value=value*10+(unsigned)(*p-'0');if(value>maximum)return 0;}*out=value;return 1;
}
static int qpt_directory(int fd)
{struct stat st;return fd>=0&&!fstat(fd,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&077);}
static int qpt_serialize(const struct qpt_preset p[32],char out[1536],size_t *bytes)
{
 if(!p||!out||!bytes)return 0;size_t at=0;const char *head="QC5PTZ2 8000 2500\n";memcpy(out,head,strlen(head));at=strlen(head);
 for(unsigned n=0;n<32;n++)if(p[n].name[0]){
  if(!qpt_name(p[n].name)||p[n].x>8000||p[n].y>2500||(p[n].x&3)||(p[n].y&3))return 0;
  for(unsigned k=0;k<n;k++)if(!strcmp(p[k].name,p[n].name))return 0;
  int size=snprintf(out+at,1536-at,"%s %u %u\n",p[n].name,p[n].x,p[n].y);if(size<=0||(size_t)size>=1536-at)return 0;at+=(size_t)size;
 }
 *bytes=at;return 1;
}
static int qpt_load_presets(struct qpt_controller *s,int directory)
{
 if(!s||s->action!=QPT_IDLE||!qpt_directory(directory))return 0;
 int fd=openat(directory,QPT_FILE,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)return errno==ENOENT;
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&077)||st.st_size<=0||st.st_size>=1536){close(fd);return 0;}
 char text[1536],canonical[1536];size_t n=0;while(n<(size_t)st.st_size){ssize_t got=read(fd,text+n,(size_t)st.st_size-n);if(got<0&&errno==EINTR)continue;if(got<=0){close(fd);return 0;}n+=(size_t)got;}close(fd);
 if(memchr(text,0,n))return 0;text[n]=0;const char *head="QC5PTZ2 8000 2500\n";size_t at=strlen(head);if(n<at||memcmp(text,head,at))return 0;
 struct qpt_preset p[32]={0};unsigned count=0;
 while(at<n){
  char *end=strchr(text+at,'\n');if(!end||count==32)return 0;unsigned x,y;
  char *a=memchr(text+at,' ',(size_t)(end-(text+at)));if(!a||a==text+at||a-(text+at)>20)return 0;
  char *b=memchr(a+1,' ',(size_t)(end-a-1));if(!b||!qpt_number(a+1,b,8000,&x)||!qpt_number(b+1,end,2500,&y)||(x&3)||(y&3))return 0;
  memcpy(p[count].name,text+at,(size_t)(a-(text+at)));if(!qpt_name(p[count].name))return 0;
  p[count].x=x;p[count].y=y;count++;at=(size_t)(end-text)+1;
 }
 size_t length;if(!qpt_serialize(p,canonical,&length)||length!=n||memcmp(canonical,text,n))return 0;
 memcpy(s->preset,p,sizeof p);return 1;
}
static int qpt_store_presets(const struct qpt_preset p[32],int directory)
{
 if(!qpt_directory(directory))return 0;char text[1536];size_t size;if(!qpt_serialize(p,text,&size))return 0;
 struct stat old;if(!fstatat(directory,QPT_FILE,&old,AT_SYMLINK_NOFOLLOW)){
  if(!S_ISREG(old.st_mode)||old.st_uid!=geteuid()||(old.st_mode&077)||old.st_size<=0||old.st_size>=1536)return 0;
 }else if(errno!=ENOENT)return 0;
 uint64_t nonce;if(getrandom(&nonce,sizeof nonce,GRND_NONBLOCK)!=sizeof nonce)return 0;
 char temp[64];snprintf(temp,sizeof temp,".ptz-%016llx.tmp",(unsigned long long)nonce);
 int fd=openat(directory,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 size_t written=0;while(written<size){ssize_t got=write(fd,text+written,size-written);if(got<0&&errno==EINTR)continue;if(got<=0)break;written+=(size_t)got;}
 int ok=written==size&&!fsync(fd);if(close(fd))ok=0;
 if(ok&&renameat(directory,temp,directory,QPT_FILE))ok=0;
 if(ok&&fsync(directory))ok=0;if(!ok)unlinkat(directory,temp,0);return ok;
}
/* Commit the in-memory change only after durable filesystem publication. */
static int qpt_save_durable(struct qpt_controller *s,int directory,const char *name)
{
 if(!s)return 0;struct qpt_controller next=*s;if(!qpt_save(&next,name)||!qpt_store_presets(next.preset,directory))return 0;
 memcpy(s->preset,next.preset,sizeof s->preset);return 1;
}
static int qpt_delete_durable(struct qpt_controller *s,int directory,const char *name)
{
 if(!s)return 0;struct qpt_controller next=*s;if(!qpt_delete(&next,name)||!qpt_store_presets(next.preset,directory))return 0;
 memcpy(s->preset,next.preset,sizeof s->preset);return 1;
}
#endif
