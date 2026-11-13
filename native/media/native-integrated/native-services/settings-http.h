/* SPDX-License-Identifier: GPL-3.0-only
 * Persist user choices before acknowledging them. Detector activity never
 * writes the SD. A failed capture apply is retried without restarting it.
 */
#ifndef QC5_SETTINGS_HTTP_H
#define QC5_SETTINGS_HTTP_H
#include "settings.h"
static struct qcs_settings qnc_applied;static unsigned qnc_have;static uint64_t qnc_due;
static int qnc_directory(void)
{return qc5_sd_mounted(sdroot,1)?open(sdroot,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC):-1;}
static void qnc_tick(void)
{
 uint64_t now=(uint64_t)now_ms();if(now<qnc_due)return;qnc_due=now+1000;
 int dir=qnc_directory();if(dir<0)return;char text[8193];struct qcs_settings s;
 int r=qcs_read(dir,text,&s);if(!r){int lock=qcs_lock(dir);if(lock>=0){r=qcs_read(dir,text,&s);if(!r)r=qcs_store_locked(dir,text,&s)?1:-1;close(lock);}}
 close(dir);memset(text,0,sizeof text);if(r<0)return;
 if(qnc_have&&qcs_equal(&s,&qnc_applied))return;
 if(!qnc_have||s.ir!=qnc_applied.ir){if(!qdf_prepare()||!qnd_request(&qdf_state,s.ir,0))return;qdf_due=0;}
 struct qfs_status status;
 if(!qfh_exchange(QFS_CONTINUOUS,s.continuous,&status)||!qfh_exchange(QFS_MOTION,s.motion,&status)||
  !qfh_exchange(QFS_SENSITIVITY,s.sensitivity,&status)||!qfh_exchange(6,s.post,&status))return;
 qnc_applied=s;qnc_have=1;
}
static int qnc_save(unsigned field,unsigned value)
{
 int dir=qnc_directory();if(dir<0)return 0;int lock=qcs_lock(dir);if(lock<0){close(dir);return 0;}
 char text[8193];struct qcs_settings s;int r=qcs_read(dir,text,&s),ok=0;
 if(r>=0){if(field==1)s.continuous=value;else if(field==2)s.motion=value;else if(field==3)s.ir=value;else goto done;ok=qcs_store_locked(dir,text,&s);}
done:memset(text,0,sizeof text);close(lock);close(dir);if(ok)qnc_due=0;return ok;
}
static int qnc_command(int fd,const char *name)
{
 unsigned mode;if(!strcmp(name,"irauto"))mode=2;else if(!strcmp(name,"ircutday"))mode=0;else if(!strcmp(name,"ircutnig"))mode=1;else return 0;
 if(qnc_save(3,mode))qdf_command(fd,name);else{const char *e="{\"ok\":false,\"result\":\"Configurazione SD non salvata\"}";http_reply(fd,503,"application/json",e,strlen(e));}return 1;
}
static void qnc_handle(int fd,const char *method,const char *path,const char *headers,char *body,size_t available,long long deadline)
{
 unsigned field=0,value=0,stop=0;
 if(!strcmp(method,"POST")){
  if(!strcmp(path,"/api/rec/start"))field=1,value=1;
  else if(!strcmp(path,"/api/rec/stop"))field=1;
  else if(!strcmp(path,"/api/rec/stop-now"))field=1,stop=1;
  else if(!strcmp(path,"/api/motion/config"))field=2;
 }
 if(!field){qfh_handle(fd,method,path,headers,body,available,deadline);return;}
 char form[257];int ok=available<=256;
 if(ok){memcpy(form,body,available);ok=qfh_form(fd,headers,form,available,deadline);}
 if(ok&&field==2){char flag[2];ok=query(form,"enabled",flag,sizeof flag)==1&&(!strcmp(flag,"0")||!strcmp(flag,"1"));if(ok)value=flag[0]-'0';}
 if(ok)ok=qnc_save(field,value);
 if(ok){struct qfs_status status;ok=qfh_exchange(stop?QFS_STOP:field==1?QFS_CONTINUOUS:QFS_MOTION,value,&status);qnc_tick();}
 const char *message=ok?"{\"ok\":true,\"result\":\"Configurazione salvata e applicata\"}":"{\"ok\":false,\"result\":\"Salvataggio o applicazione non confermati\"}";
 http_reply(fd,ok?200:503,"application/json",message,strlen(message));
}
#endif
