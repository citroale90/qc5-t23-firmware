/* SPDX-License-Identifier: GPL-3.0-only
 * Real adapters for the native image-owner IPC, kernel filter and named
 * white/IR GPIO outputs. Kept out of the capture/encoder execution path.
 */
#ifndef QC5_CONTINUOUS_DAYNIGHT_FRONTEND_H
#define QC5_CONTINUOUS_DAYNIGHT_FRONTEND_H
#include "daynight-controller.h"
static struct qnd_state qdf_state;
static unsigned qdf_initialized,qdf_serial,qdf_filter_sequence;
static uint64_t qdf_due;
static int qdf_exchange(unsigned op,unsigned value,struct qdi_status *reply)
{
 char path[108];int n=snprintf(path,sizeof path,"%s/features.sock",frontend_run);if(n<=0||(size_t)n>=sizeof path)return 0;
 int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return 0;
 struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,path);
 if(connect(fd,(void*)&a,sizeof a)){close(fd);return 0;}
 if(!++qdf_serial)qdf_serial=1;struct qdi_request p={QDI_MAGIC,qdf_serial,op,value};
 int ok=send(fd,&p,sizeof p,MSG_DONTWAIT|MSG_NOSIGNAL)==sizeof p;
 struct pollfd wait={fd,POLLIN,0};if(!ok||poll(&wait,1,200)<=0||!(wait.revents&POLLIN)){close(fd);return 0;}
 char packet[sizeof *reply+1];ssize_t got=recv(fd,packet,sizeof packet,MSG_DONTWAIT);close(fd);
 if(got!=sizeof *reply)return 0;memcpy(reply,packet,sizeof *reply);
 return reply->magic==QDI_MAGIC&&reply->serial==p.serial&&reply->ok==1;
}
static int qdf_meter(void *unused,struct qdi_status *r)
{(void)unused;return qdf_exchange(QDI_STATUS,0,r);}
static int qdf_image(void *unused,unsigned night)
{(void)unused;struct qdi_status r;return qdf_exchange(QDI_SET_NIGHT,night,&r);}
static int qdf_hold(void *unused,unsigned seconds)
{(void)unused;struct qdi_status r;return qdf_exchange(QDI_TRANSITION,seconds,&r);}
static int qdf_ir(unsigned on)
{
 if(on>1)return 0;
 for(unsigned chip=0;chip<64;chip++){
  char path[64];snprintf(path,sizeof path,"/dev/gpiochip%u",chip);int fd=open(path,O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(fd<0)continue;
  struct stat st;struct gpiochip_info info={0};
  if(fstat(fd,&st)||!S_ISCHR(st.st_mode)||ioctl(fd,GPIO_GET_CHIPINFO_IOCTL,&info)||info.lines!=1||memcmp(info.label,"qc5-ir-led",sizeof "qc5-ir-led")){close(fd);continue;}
  struct gpio_v2_line_request request={.num_lines=1};memcpy(request.consumer,"qc5-daynight",sizeof "qc5-daynight");
  request.config.flags=GPIO_V2_LINE_FLAG_OUTPUT;request.config.num_attrs=1;request.config.attrs[0].attr.id=GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;request.config.attrs[0].attr.values=on;request.config.attrs[0].mask=1;
  int rc=ioctl(fd,GPIO_V2_GET_LINE_IOCTL,&request);close(fd);if(rc||request.fd<0)return 0;
  struct gpio_v2_line_values v={.mask=1};int ok=!ioctl(request.fd,GPIO_V2_LINE_GET_VALUES_IOCTL,&v)&&v.bits==on;close(request.fd);return ok;
 }return 0;
}
static int qdf_lights(void *unused,unsigned mode)
{
 (void)unused;if(mode>2)return 0;
 /* Execute both OFF operations even if one readback fails. */
 int white_off=set_light(0),ir_off=qdf_ir(0);if(!white_off||!ir_off)return 0;
 return mode==1?set_light(1):mode==2?qdf_ir(1):1;
}
static int qdf_filter_start(void *unused,unsigned night)
{
 (void)unused;if(!qfh_motor_open())return -1;
 if(qph_busy()||qph_motor.hw.busy)return 0;
 qdf_filter_sequence=qph_motor.hw.sequence;qph_filter_night=night;qph_filter_pending=1;qph_motor.error=0;
 if(!qph_motor.hw.initialized){struct qcm_request p={.command=QCM_INITIALIZE};qpt_unknown(&qph_motor);if(!qph_submit(NULL,&p)){qph_filter_pending=0;return -1;}}
 return 1;
}
static int qdf_filter_done(void *unused)
{
 (void)unused;if(qfh_motor_fd<0)return -1;if(qph_filter_pending||qph_motor.hw.busy)return 0;
 return qph_motor.hw.last_error||qph_motor.hw.ir_energized||qph_motor.hw.sequence==qdf_filter_sequence?-1:1;
}
static int qdf_prepare(void)
{
 if(!qdf_initialized)qdf_initialized=qnd_init(&qdf_state,(struct qnd_io){NULL,qdf_meter,qdf_image,qdf_hold,qdf_filter_start,qdf_filter_done,qdf_lights});
 return qdf_initialized;
}
static void qdf_tick(void)
{
 uint64_t now=(uint64_t)now_ms()*1000;if(!now||now<qdf_due||!qdf_prepare())return;
 qdf_due=now+250000;if(qph_motor.action==QPT_HOME)return;
 (void)qnd_tick(&qdf_state,now);
}
static unsigned qdf_busy(void){return qdf_initialized&&qnd_busy(&qdf_state);}
static void qdf_cancel(void)
{
 if(qdf_busy()){uint64_t now=(uint64_t)now_ms()*1000;if(now&&now<=UINT64_MAX-60000000)(void)qnd_fail(&qdf_state,9,now);}
}
static unsigned qdf_mode(void){return qdf_state.mode;}
static unsigned qdf_night(void){return qdf_state.night;}
static unsigned qdf_error(void){return qdf_state.error;}
static unsigned qdf_ready(void){return qdf_initialized&&qdf_state.known&&!qdf_state.error;}
static void qdf_status(int fd)
{
 char body[640];const struct qdi_status *m=&qdf_state.meter;
 int n=snprintf(body,sizeof body,"{\"ok\":true,\"available\":%s,\"mode\":%u,\"night\":%u,\"white\":%u,\"busy\":%u,\"probe\":%u,\"error\":%u,\"mean\":%u,\"exposure_lines\":%u,\"gain_index\":%u}",qdf_ready()?"true":"false",qdf_state.mode,qdf_state.night,qdf_state.applied_white,qdf_busy(),qdf_state.phase==QND_PROBE,qdf_state.error,m->mean,m->exposure,m->gain);
 if(n>0&&(size_t)n<sizeof body)http_reply(fd,200,"application/json",body,n);
}
static int qdf_command(int fd,const char *name)
{
 if(strcmp(name,"ircutday")&&strcmp(name,"ircutnig")&&strcmp(name,"irauto")&&strcmp(name,"wlon")&&strcmp(name,"wlof")&&strcmp(name,"daynightstatus"))return 0;
 qdf_prepare();if(!strcmp(name,"daynightstatus")){qdf_status(fd);return 1;}
 /* OFF remains possible even while the image service is unavailable. */
 if(!strcmp(name,"wlof"))(void)set_light(0);
 if(!strcmp(name,"ircutday"))(void)qdf_ir(0);
 unsigned mode=!strcmp(name,"irauto")?QND_AUTO:!strcmp(name,"ircutnig")?QND_NIGHT:QND_DAY;
 int ok=qnd_request(&qdf_state,mode,!strcmp(name,"wlon"));
 const char *body=ok?"{\"ok\":true,\"result\":\"Modalità richiesta; immagine, filtro e luce vengono confermati in sequenza\"}":"{\"ok\":false,\"result\":\"Modalità non valida\"}";
 http_reply(fd,ok?200:503,"application/json",body,strlen(body));qdf_due=0;return 1;
}
#endif
