/* SPDX-License-Identifier: GPL-3.0-only
 * Single browser consumer of the existing native producer. The public
 * WebUI's MIME/fMP4 protocol is retained; no frame copies, second encoder,
 * subprocess or refreshed per-chunk timeout is introduced.
 */
#ifndef QC5_NATIVE_SERVICE_WEB_H
#define QC5_NATIVE_SERVICE_WEB_H
#include <strings.h>
#include "native-websocket.h"
#include "qc5_websocket.h"
#ifdef QC5_AUDIO_DUPLEX
#include "../audio-duplex/talk-parent.h"
static struct qtp_parent service_talk;
#endif
static struct {
 struct qc5_native_websocket ws;
 struct qc5_live_net *net;
 uint64_t epoch,deadline,mux_bytes,messages,frames;
 uint32_t mux_hash;
#ifdef QC5_AUDIO_DUPLEX
 uint8_t incoming[1024];unsigned used,duplex_announced;
#else
 uint8_t incoming[256];unsigned used;
#endif
} service_web;
/* Header names are exact case-insensitive fields. Duplicate/oversized values
 * and obsolete folded lines fail closed, including for non-required fields. */
static int qsw_header(const char *request,const char *name,char *out,size_t cap)
{
 if(!request||!name||!out||!cap)return 0;
 out[0]=0;unsigned found=0;size_t key=strlen(name);
 const char *p=strstr(request,"\r\n");if(!p)return 0;p+=2;
 while(*p){
  const char *end=strstr(p,"\r\n");if(!end)return 0;
  if(end==p)return found==1;
  if(*p==' '||*p=='\t')return 0;
  const char *colon=memchr(p,':',(size_t)(end-p));if(!colon||colon==p)return 0;
  /* This endpoint accepts bodyless browser upgrades only. */
  size_t field=(size_t)(colon-p);
  if((field==14&&!strncasecmp(p,"Content-Length",14))||(field==17&&!strncasecmp(p,"Transfer-Encoding",17)))return 0;
  for(const char *v=p;v<colon;v++)if(!((*v>='A'&&*v<='Z')||(*v>='a'&&*v<='z')||(*v>='0'&&*v<='9')||*v=='-'))return 0;
  for(const char *v=colon+1;v<end;v++)if(((unsigned char)*v<32&&*v!='\t')||(unsigned char)*v==127)return 0;
  if((size_t)(colon-p)==key&&!strncasecmp(p,name,key)){
   if(found++)return 0;
   const char *a=colon+1,*z=end;while(a<z&&(*a==' '||*a=='\t'))a++;while(z>a&&(z[-1]==' '||z[-1]=='\t'))z--;
   size_t n=(size_t)(z-a);if(!n||n>=cap)return 0;
   memcpy(out,a,n);out[n]=0;
  }
  p=end+2;
 }
 return 0;
}
static int qsw_upgrade_token(const char *value)
{
 for(const char *p=value;*p;){
  const char *end=strchr(p,',');if(!end)end=p+strlen(p);
  const char *a=p,*z=end;while(a<z&&(*a==' '||*a=='\t'))a++;while(z>a&&(z[-1]==' '||z[-1]=='\t'))z--;
  if(z-a==7&&!strncasecmp(a,"upgrade",7))return 1;p=*end?end+1:end;
 }
 return 0;
}
static int qc5_service_upgrade(struct qc5_live_net *s)
{
 char key[25],accept[29],version[8],upgrade[32],connection[128],host[256],origin[280],expected[280];
 int ok=s&&!strncmp(s->request,"GET /media HTTP/1.1\r\n",21)&&
  qsw_header(s->request,"Sec-WebSocket-Key",key,sizeof key)&&!qc5_ws_accept(key,accept)&&
  qsw_header(s->request,"Sec-WebSocket-Version",version,sizeof version)&&!strcmp(version,"13")&&
  qsw_header(s->request,"Upgrade",upgrade,sizeof upgrade)&&!strcasecmp(upgrade,"websocket")&&
  qsw_header(s->request,"Connection",connection,sizeof connection)&&qsw_upgrade_token(connection)&&
  qsw_header(s->request,"Host",host,sizeof host)&&qsw_header(s->request,"Origin",origin,sizeof origin);
 if(ok){
  int n=snprintf(expected,sizeof expected,"http://%s",host);ok=n>0&&(size_t)n<sizeof expected&&!strcmp(origin,expected);
  n=snprintf(expected,sizeof expected,"https://%s",host);ok|=n>0&&(size_t)n<sizeof expected&&!strcmp(origin,expected);
 }
 if(!ok){
  static const char bad[]="HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
  if(s){(void)qc5_net_write(s,bad,sizeof bad-1,0);qc5_net_close(s);}return 0;
 }
 char reply[384];int n=snprintf(reply,sizeof reply,
  "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\nX-QC5-Kernel: %s\r\n%s\r\n",accept,s->release,s->extra_header);
 if(n<=0||(size_t)n>=sizeof reply||!qc5_net_write(s,reply,(unsigned)n,0)){qc5_net_close(s);return 0;}
 s->stream=2;return 1;
}
static int qsw_send(void *opaque,unsigned opcode,const void *data,size_t bytes)
{
 struct qc5_live_net *s=opaque;
 if(!s||s!=service_web.net||s->stream!=2||!data||!bytes||bytes>4097||
    (opcode!=1&&opcode!=2&&opcode!=10)||service_web.messages==UINT64_MAX)return 0;
 if(opcode==2&&(bytes<2||((const uint8_t*)data)[0]||service_web.mux_bytes>UINT64_MAX-(bytes-1)))return 0;
 uint8_t header[4]={(uint8_t)(128|opcode),(uint8_t)bytes,0,0};unsigned h=2;
 if(bytes>=126){header[1]=126;header[2]=(uint8_t)(bytes>>8);header[3]=(uint8_t)bytes;h=4;}
 if(!qc5_net_write_pair_until(s,header,h,data,(unsigned)bytes,0,service_web.deadline))return 0;
 service_web.messages++;
 if(opcode==2){
  const uint8_t *p=data;for(size_t i=1;i<bytes;i++)service_web.mux_hash=(service_web.mux_hash^p[i])*16777619u;
  service_web.mux_bytes+=bytes-1;
 }
 return 1;
}
#ifdef QC5_AUDIO_DUPLEX
static int qsw_talk_reply(void *opaque,uint64_t epoch,uint32_t id,unsigned event)
{
 (void)opaque;struct qc5_live_net *net=service_web.net;
 if(!net||net->client<0||net->stream!=2||epoch!=service_web.epoch)return 1;
 char text[224];int n;
 if(event==QTP_READY)n=snprintf(text,sizeof text,"{\"talk\":true,\"talkId\":%u,\"ready\":true,\"codec\":\"PCMA\",\"sampleRate\":16000,\"frameSamples\":320,\"maxSeconds\":30}",id);
 else n=snprintf(text,sizeof text,"{\"talk\":false,\"talkId\":%u,\"reason\":\"%s\"}",id,
  event==QTP_STOPPED?"stopped":event==QTP_BUSY?"busy":"audio_error");
 uint64_t now=qc5_net_us(),saved=service_web.deadline;
 if(n<=0||(size_t)n>=sizeof text||!now||now>UINT64_MAX-10000)return 0;
 service_web.deadline=now+10000;
 int ok=qsw_send(net,1,text,(size_t)n);service_web.deadline=saved;
 if(!ok)qc5_net_close(net);return ok;
}
#endif
static int qsw_controls(struct qc5_live_net *s)
{
#ifdef QC5_AUDIO_DUPLEX
 if(!service_web.duplex_announced&&qtp_available(&service_talk)){
  static const char ready[]="{\"ready\":true,\"duplex\":true}";
  if(!qsw_send(s,1,ready,sizeof ready-1))return 0;service_web.duplex_announced=1;
 }
#endif
 if(service_web.used>=sizeof service_web.incoming)return 0;
 ssize_t n=recv(s->client,service_web.incoming+service_web.used,sizeof service_web.incoming-service_web.used,MSG_DONTWAIT);
 if(n==0)return 0;
 if(n<0)return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR;
 service_web.used+=(unsigned)n;
 unsigned at=0;
 while(at<service_web.used){
  unsigned op;
#ifdef QC5_AUDIO_DUPLEX
  uint8_t payload[320];
#else
  uint8_t payload[125];
#endif
  size_t length;
  int got=qc5_ws_decode(service_web.incoming+at,service_web.used-at,&op,payload,sizeof payload,&length);
  if(got<0)return 0;if(!got)break;
  if(op==8)return 0;
  if(op==2){
#ifdef QC5_AUDIO_DUPLEX
   if(!qtp_browser(&service_talk,service_web.epoch,op,payload,length,qc5_net_us()))return 0;
#else
   return 0;
#endif
  }
  if(op==9&&length&&!qsw_send(s,10,payload,length))return 0;
  if(op==9&&!length){
   const uint8_t pong[2]={0x8a,0};if(!qc5_net_write_until(s,pong,2,0,service_web.deadline))return 0;
  }
  at+=(unsigned)got;
 }
 if(at){service_web.used-=at;memmove(service_web.incoming,service_web.incoming+at,service_web.used);}
 return service_web.used<sizeof service_web.incoming;
}
static int qc5_service_publish(struct qc5_live_net *s,uint64_t epoch,const struct qc5_frame_output *frame,uint64_t deadline)
{
 if(!s||s->client<0||!s->stream||!epoch||!frame||!frame->bytes)return 0;
 if(s->stream==1)return qc5_net_write_until(s,frame->data,frame->bytes,1,deadline);
 if(s->stream!=2)return 0;
 if(service_web.epoch!=epoch){
  /* Producer forces an IDR for every new reader. Image processing and
   * capture buffers continue unchanged across this consumer reset. */
  memset(&service_web,0,sizeof service_web);service_web.net=s;service_web.epoch=epoch;service_web.mux_hash=2166136261u;
  if(!qnws_start(&service_web.ws,qsw_send,s))return 0;
 }
 service_web.deadline=deadline;
 if(s->frames==QC5_NET_COUNT_MAX||s->bytes>QC5_NET_COUNT_MAX-frame->bytes||service_web.frames==UINT64_MAX||
    !qsw_controls(s)||!qnws_frame(&service_web.ws,frame))return 0;
 ++s->frames;s->bytes+=frame->bytes;service_web.frames++;return 1;
}
#endif
