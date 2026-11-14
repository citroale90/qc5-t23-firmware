/* SPDX-License-Identifier: GPL-3.0-only
 * Existing UI command names, native kernel-owned motion and durable presets.
 * No shell, vendor daemon, IP/serial binding or saved calibration shortcut.
 */
#ifndef QC5_PTZ_HTTP_H
#define QC5_PTZ_HTTP_H
#include "preset-store.h"
static int qfh_motor_fd=-1,qph_store_error;
static struct qpt_controller qph_motor;
static unsigned qph_filter_pending,qph_filter_night,qph_diagonal;
#include "startup-calibration.h"
static struct qsc_state qph_startup;
static int qph_read(void *unused,struct qcm_status *s,struct qcm_progress *p)
{(void)unused;return qfh_motor_fd>=0&&!ioctl(qfh_motor_fd,QCM_READ_STATUS,s)&&!ioctl(qfh_motor_fd,QCM_READ_PROGRESS,p);}
static int qph_submit(void *unused,const struct qcm_request *p)
{(void)unused;return qfh_motor_fd>=0&&!ioctl(qfh_motor_fd,QCM_SUBMIT,p);}
static int qph_state_directory(unsigned create)
{
 int root=open(sdroot,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(root<0)return -1;
 if(create&&mkdirat(root,"qc5-state",0700)&&errno!=EEXIST){close(root);return -1;}
 int fd=openat(root,"qc5-state",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);close(root);
 if(fd>=0&&!qpt_directory(fd)){close(fd);fd=-1;}return fd;
}
static int qph_storage_ready(void)
{
#ifdef QC5_PORTAL_HOST_TEST
 /* Isolated test directory only. Real-camera build always checks the MMC
  * gate; the test never accesses or unlocks a block device. */
 return 1;
#else
 int fd=open("/sys/bus/platform/devices/13450000.mmc/record_volume_ready",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;
 char text[3];ssize_t n=read(fd,text,sizeof text);close(fd);return n==2&&!memcmp(text,"1\n",2);
#endif
}
static int qfh_motor_open(void)
{
 if(qfh_motor_fd<0){
  qfh_motor_fd=open("/dev/qc5-peripheral",O_RDWR|O_CLOEXEC|O_NOFOLLOW);if(qfh_motor_fd<0)return 0;
  struct qcm_status hw;struct qcm_progress p;
  if(!qph_read(NULL,&hw,&p)||p.version!=QCM_PROGRESS_VERSION||!qpt_init(&qph_motor,(struct qpt_io){NULL,qph_read,qph_submit})){close(qfh_motor_fd);qfh_motor_fd=-1;return 0;}
  qph_store_error=0;int dir=qph_state_directory(0);
  if(dir>=0){qph_store_error=!qpt_load_presets(&qph_motor,dir);close(dir);}
 }
 return 1;
}
/* Once per owner boot, independently of WebUI requests or Wi-Fi state.
 * Opening/calibrating uses the existing exclusive kernel owner. Stop cancels
 * this attempt; a fault never starts an unattended retry loop. */
static int qph_startup_start(void *unused,uint64_t now)
{
 (void)unused;
 if(qdf_busy()||qph_filter_pending||qph_diagonal||!qfh_motor_open()||qph_motor.action!=QPT_IDLE)return 0;
 if(!qph_read(NULL,&qph_motor.hw,&qph_motor.progress))return -1;
 if(qph_motor.hw.busy)return 0;
 return qpt_home(&qph_motor,now)?1:-1;
}
static void qph_startup_tick(uint64_t now)
{(void)qsc_tick(&qph_startup,now,qph_startup_start,NULL);}
static void qfh_tick(void)
{
 qrf_reap();uint64_t now=(uint64_t)now_ms()*1000;qph_startup_tick(now);if(qfh_motor_fd<0)return;
 if(!qpt_tick(&qph_motor,now)&&qph_motor.error==2){close(qfh_motor_fd);qfh_motor_fd=-1;qph_filter_pending=qph_diagonal=0;return;}
 if(qph_motor.error){qph_diagonal=0;if(qph_filter_pending){qpt_unknown(&qph_motor);qph_filter_pending=0;}}
 if(qph_filter_pending&&!qph_motor.hw.busy){
  if(qph_motor.hw.last_error){qpt_unknown(&qph_motor);qph_filter_pending=0;return;}
  if(qph_filter_pending==1){struct qcm_request p={.command=QCM_FILTER,.value=qph_filter_night};
   if(!qph_submit(NULL,&p)){qph_filter_pending=0;return;}qph_filter_pending=2;
  }else qph_filter_pending=0;
 }
 if(qph_diagonal&&qph_motor.action==QPT_IDLE&&!qph_motor.hw.busy&&!qph_filter_pending){
  unsigned dir=qph_diagonal-1;qph_diagonal=0;(void)qpt_jog(&qph_motor,1,dir,now);
 }
}
static int qph_busy(void)
{return qph_motor.action!=QPT_IDLE||qph_filter_pending||qph_diagonal;}
static void qph_status(int fd)
{
 char body[512];const struct qcm_progress *p=&qph_motor.progress;
 int n=snprintf(body,sizeof body,"{\"ok\":true,\"calibrated\":%u,\"calibration_phase\":%u,\"busy\":%u,\"error\":%u,\"x\":%u,\"y\":%u,\"generation\":%u,\"axis\":%u,\"requested\":%u,\"steps\":%u,\"active\":%u,\"reason\":%u}",qph_motor.homed,qph_motor.home_phase,!!(qph_busy()||qph_motor.hw.busy),qph_motor.error,qph_motor.position.axis[0].position,qph_motor.position.axis[1].position,p->generation,p->axis,p->requested,p->steps,p->active,p->reason);
 if(n>0&&(size_t)n<sizeof body)http_reply(fd,200,"application/json",body,n);
}
static int qph_list(int fd)
{
 char body[1024];size_t at=0;int n=snprintf(body,sizeof body,"{\"ok\":true,\"calibrated\":%s,\"file_error\":%s,\"presets\":[",qph_motor.homed?"true":"false",qph_store_error?"true":"false");if(n<=0)return 0;at=n;
 unsigned count=0;for(unsigned k=0;k<32;k++)if(qph_motor.preset[k].name[0]){
  n=snprintf(body+at,sizeof body-at,"%s\"%s\"",count++?",":"",qph_motor.preset[k].name);if(n<=0||(size_t)n>=sizeof body-at)return 0;at+=n;
 }if(sizeof body-at<3)return 0;memcpy(body+at,"]}",3);http_reply(fd,200,"application/json",body,at+2);return 1;
}
static int qfh_command(int fd,const char *name,const char *arguments)
{
 if(strncmp(name,"ptz",3)&&strncmp(name,"preset_",7)&&strcmp(name,"ircutday")&&strcmp(name,"ircutnig")&&strcmp(name,"irauto"))return 0;
 const char *message="Motori occupati o non disponibili";int ok=0;
 if(!qfh_motor_open())goto reply;qfh_tick();uint64_t now=(uint64_t)now_ms()*1000;
 if(!strcmp(name,"ptzstatus")){qph_status(fd);return 1;}
 if(!strcmp(name,"preset_list")){if(qph_list(fd))return 1;goto reply;}
 if(!strcmp(name,"ptzstop")){qph_filter_pending=qph_diagonal=0;ok=qpt_stop(&qph_motor,now);qdf_cancel();message=ok?"Arresto nativo richiesto":"Arresto non confermato";goto reply;}
 if(qdf_busy()||qph_busy()||qph_motor.hw.busy){if(qph_motor.action==QPT_HOME)message="Calibrazione in corso: attendi oppure premi Stop";goto reply;}
 if(!strcmp(name,"ptzhome")){ok=qpt_home(&qph_motor,now);message=ok?"Calibrazione avviata":"Calibrazione non avviata";goto reply;}
 if(!strncmp(name,"preset_",7)){
  char preset[21];if(!arguments||query(arguments,"preset_name",preset,sizeof preset)!=1||!qpt_name(preset)){message="Nome preset non valido";goto reply;}
  if(!strcmp(name,"preset_go")){ok=qpt_recall(&qph_motor,preset,now);message=ok?"Richiamo preset avviato":"Calibra la posizione prima di richiamare il preset";goto reply;}
  if(strcmp(name,"preset_save")&&strcmp(name,"preset_del")){message="Comando preset non valido";goto reply;}
  if(!qph_storage_ready()){message="SD non pronta: preset non salvato";goto reply;}
  int dir=qph_state_directory(1);if(dir<0){message="SD non pronta: preset non salvato";goto reply;}
  ok=!strcmp(name,"preset_save")?qpt_save_durable(&qph_motor,dir,preset):qpt_delete_durable(&qph_motor,dir,preset);close(dir);
  if(ok)qph_store_error=0;message=ok?"Preset aggiornato sulla SD":"Preset non salvato: controlla calibrazione e SD";goto reply;
 }
 unsigned axis=0,dir=0,diagonal=0;
 if(!strcmp(name,"ptzl"))axis=0,dir=0;else if(!strcmp(name,"ptzr"))axis=0,dir=1;
 else if(!strcmp(name,"ptzu"))axis=1,dir=0;else if(!strcmp(name,"ptzd"))axis=1,dir=1;
 else if(!strcmp(name,"ptzlu"))axis=0,dir=0,diagonal=1;else if(!strcmp(name,"ptzru"))axis=0,dir=1,diagonal=1;
 else if(!strcmp(name,"ptzld"))axis=0,dir=0,diagonal=2;else if(!strcmp(name,"ptzrd"))axis=0,dir=1,diagonal=2;
 else{message="Comando nativo non valido";goto reply;}
 ok=qpt_jog(&qph_motor,axis,dir,now);if(ok)qph_diagonal=diagonal;message=ok?"Movimento nativo avviato":"Movimento non avviato";
reply:;
 char quoted[512],body[600];json_string(quoted,sizeof quoted,message);int n=snprintf(body,sizeof body,"{\"ok\":%s,\"result\":%s}",ok?"true":"false",quoted);
 if(n>0&&(size_t)n<sizeof body)http_reply(fd,ok?200:503,"application/json",body,n);return 1;
}
#endif
