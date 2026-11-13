/* SPDX-License-Identifier: GPL-3.0-only
 * One literal SD configuration. No shell expansion, flash or vendor NVS.
 * Use the same advisory lock in the native Wi-Fi writer and HTTP writer.
 */
#ifndef QC5_NATIVE_SETTINGS_H
#define QC5_NATIVE_SETTINGS_H
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include "config_read.h"
#include "config-lock.h"
#define qc5_config_get qcs_literal_get
#include "config_parser.h"
#undef qc5_config_get
struct qcs_settings {unsigned continuous,motion,post,sensitivity,ir,ntp;char peer[16];};
static const char qcs_defaults[]=
 "# QC5 native7.3: literal data, never executed\nSSID=\"\"\nPWD=\"\"\nWIFI_APPLY=1\nWIFI_OPEN=0\n"
 "RECORD_CONTINUOUS=0\nMOTION_ENABLE=0\nMOTION_POST_SECONDS=5\nMOTION_SENSITIVITY=60\n"
 "IR_MODE=auto\nNTP_ENABLE=1\nNTP_SERVER=auto\nRECORD_TARGET=sd\nRECORD_EXPORT_EXISTING=0\n";
static int qcs_private4(const char *s)
{
 unsigned a,b,c,d;char extra;int n=0;
 if(!s||sscanf(s,"%u.%u.%u.%u%n%c",&a,&b,&c,&d,&n,&extra)!=4||!n||s[n]||a>255||b>255||c>255||d>254||!d)return 0;
 char canon[16];snprintf(canon,sizeof canon,"%u.%u.%u.%u",a,b,c,d);
 return !strcmp(canon,s)&&(a==10||(a==172&&b>=16&&b<=31)||(a==192&&b==168)||(a==169&&b==254));
}
static int qcs_uint(const char *text,size_t bytes,const char *key,unsigned *value,unsigned low,unsigned high)
{
 char v[16];int r=qcs_literal_get(text,bytes,key,v,sizeof v);if(r<0)return 0;if(!r)return 1;
 if(!*v)return 0;unsigned n=0;
 for(const char *p=v;*p;p++){unsigned d=(unsigned char)*p-'0';if(d>9||n>(high-d)/10||d>high)return 0;n=n*10+d;}
 if(n<low||n>high)return 0;*value=n;return 1;
}
static int qcs_parse(const char *text,size_t n,struct qcs_settings *s)
{
 if(!text||!s||n>8192)return 0;
 *s=(struct qcs_settings){.post=5,.sensitivity=60,.ir=2,.ntp=1};strcpy(s->peer,"auto");char v[32];
 if(qcs_literal_get(text,n,"_SYNTAX_ONLY",v,sizeof v)<0||
  !qcs_uint(text,n,"RECORD_CONTINUOUS",&s->continuous,0,1)||!qcs_uint(text,n,"MOTION_ENABLE",&s->motion,0,1)||
  !qcs_uint(text,n,"MOTION_POST_SECONDS",&s->post,1,60)||!qcs_uint(text,n,"MOTION_SENSITIVITY",&s->sensitivity,0,100)||!qcs_uint(text,n,"NTP_ENABLE",&s->ntp,0,1))return 0;
 int r=qcs_literal_get(text,n,"IR_MODE",v,sizeof v);if(r<0)return 0;
 if(r){if(!strcmp(v,"auto"))s->ir=2;else if(!strcmp(v,"day"))s->ir=0;else if(!strcmp(v,"night"))s->ir=1;else return 0;}
 r=qcs_literal_get(text,n,"NTP_SERVER",v,sizeof v);if(r<0)return 0;
 if(r){if(strcmp(v,"auto")&&!qcs_private4(v))return 0;strcpy(s->peer,v);}return 1;
}
static int qcs_read(int directory,char text[8193],struct qcs_settings *s)
{
 int fd=openat(directory,"config.txt",O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
 if(fd<0){if(errno!=ENOENT)return -1;memcpy(text,qcs_defaults,sizeof qcs_defaults);return qcs_parse(text,sizeof qcs_defaults-1,s)?0:-1;}
 struct stat st;int good=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&st.st_nlink==1&&st.st_size>=0&&st.st_size<=8192;
 long n=good?qc5_config_read_fd(fd,text,8193):-1;close(fd);if(n<0||n>8192)return -1;text[n]=0;
 return qcs_parse(text,n,s)?1:-1;
}
static int qcs_equal(const struct qcs_settings *a,const struct qcs_settings *b)
{return a->continuous==b->continuous&&a->motion==b->motion&&a->post==b->post&&a->sensitivity==b->sensitivity&&a->ir==b->ir&&a->ntp==b->ntp&&!strcmp(a->peer,b->peer);}
static int qcs_write_file(int directory,const char *name,const char *text,size_t n)
{
 int fd=openat(directory,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
 size_t at=0;while(at<n){ssize_t k=write(fd,text+at,n-at);if(k<0&&errno==EINTR)continue;if(k<=0)break;at+=k;}
 int ok=at==n&&!fsync(fd);if(close(fd))ok=0;if(!ok)unlinkat(directory,name,0);return ok;
}
static int qcs_managed_key(const char *p,size_t n)
{
 while(n&&qc5_space(*p)){p++;n--;}size_t key=0;while(key<n&&p[key]!='=')key++;if(key==n)return 0;while(key&&qc5_space(p[key-1]))key--;
 static const char *keys[]={"RECORD_CONTINUOUS","MOTION_ENABLE","MOTION_POST_SECONDS","MOTION_SENSITIVITY","IR_MODE","NTP_ENABLE","NTP_SERVER"};
 for(unsigned i=0;i<sizeof keys/sizeof *keys;i++)if(strlen(keys[i])==key&&!memcmp(p,keys[i],key))return 1;return 0;
}
static int qcs_store_locked(int directory,const char *old,const struct qcs_settings *s)
{
 if(!old||!s||strlen(old)>8192)return 0;char next[8193],suffix[240];size_t used=0,at=0,n=strlen(old);
 if(n>=3&&!memcmp(old,"\xef\xbb\xbf",3)){memcpy(next,old,3);used=at=3;}
 while(at<n){size_t begin=at;while(at<n&&old[at]!='\n')at++;if(at<n)at++;
  if(!qcs_managed_key(old+begin,at-begin)){if(used+at-begin>=sizeof next)return 0;memcpy(next+used,old+begin,at-begin);used+=at-begin;}}
 if(used&&next[used-1]!='\n'){if(used+1>=sizeof next)return 0;next[used++]='\n';}
 int count=snprintf(suffix,sizeof suffix,"RECORD_CONTINUOUS=%u\nMOTION_ENABLE=%u\nMOTION_POST_SECONDS=%u\nMOTION_SENSITIVITY=%u\nIR_MODE=%s\nNTP_ENABLE=%u\nNTP_SERVER=%s\n",s->continuous,s->motion,s->post,s->sensitivity,s->ir==2?"auto":s->ir?"night":"day",s->ntp,s->peer);
 if(count<=0||(size_t)count>=sizeof suffix||used+(size_t)count>=sizeof next)return 0;memcpy(next+used,suffix,count);used+=count;next[used]=0;
 struct qcs_settings check;if(!qcs_parse(next,used,&check)||!qcs_equal(&check,s))return 0;
 /* A power cut can leave a temporary file. Use a new exclusive name so
  * the next boot/update is never permanently blocked by that orphan. */
 uint8_t nonce[8];int random=open("/dev/urandom",O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(random<0)return 0;
 ssize_t got=read(random,nonce,sizeof nonce);close(random);if(got!=sizeof nonce)return 0;
 char hex[17],fresh[48],backup[48];for(unsigned i=0;i<8;i++){hex[2*i]="0123456789abcdef"[nonce[i]>>4];hex[2*i+1]="0123456789abcdef"[nonce[i]&15];}hex[16]=0;
 snprintf(fresh,sizeof fresh,".qc5-settings-new-%s",hex);snprintf(backup,sizeof backup,".qc5-settings-old-%s",hex);
 if(!qcs_write_file(directory,fresh,next,used))return 0;
 if(!qcs_write_file(directory,backup,old,n)){unlinkat(directory,fresh,0);return 0;}
 int ok=!renameat(directory,backup,directory,".qc5-settings.previous");
 if(ok)ok=!renameat(directory,fresh,directory,"config.txt");
 if(ok)ok=!fsync(directory);
 if(!ok){unlinkat(directory,fresh,0);unlinkat(directory,backup,0);}memset(next,0,sizeof next);return ok;
}
#endif
