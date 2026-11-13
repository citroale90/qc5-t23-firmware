/* SPDX-License-Identifier: GPL-3.0-only
 * Bounded RTSP/1.0 control for the native RTP producer. No camera/vendor RPC,
 * capture ownership, resolver, subprocess, frame queue or encoder here.
 * Return0 keeps this client;1 closes it;-1 denotes failed bounded output.
 */
#ifndef QC5_NATIVE_RTSP_CONTROL_H
#define QC5_NATIVE_RTSP_CONTROL_H
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include "rtp-native.h"
struct qrs_config {uint8_t sps[256],pps[256];unsigned sps_n,pps_n;};
struct qrs_client {
 struct qnr_session rtp;const struct qrs_config *config;
 uint64_t play_us;unsigned described,setup[2],playing,wait_key;char session[17],base[280];
};
static int qrs_begin(struct qrs_client *r,const struct qrs_config *config,void *opaque,
 int (*emit)(void *,const uint8_t *,size_t),uint64_t origin,uint32_t vs,uint32_t as,const char *session)
{
 if(!r||!config||!session||strlen(session)!=16)return 0;
 for(unsigned i=0;i<16;i++)if(!((session[i]>='0'&&session[i]<='9')||(session[i]>='a'&&session[i]<='f')))return 0;
 memset(r,0,sizeof *r);if(!qnr_begin(&r->rtp,opaque,emit,origin,vs,as))return 0;
 r->play_us=origin;r->config=config;memcpy(r->session,session,17);return 1;
}
static int qrs_header(const char *headers,const char *wanted,char *value,size_t cap)
{
 if(!headers||!wanted||!value||cap<2)return -1;value[0]=0;unsigned found=0;size_t key=strlen(wanted);
 for(const char *p=headers;*p;){
  const char *end=strstr(p,"\r\n");if(!end)return -1;if(end==p)return (int)found;
  if(*p==' '||*p=='\t')return -1;
  const char *colon=memchr(p,':',(size_t)(end-p));if(!colon||colon==p)return -1;
  for(const char *x=p;x<colon;x++)if(!((*x>='a'&&*x<='z')||(*x>='A'&&*x<='Z')||(*x>='0'&&*x<='9')||*x=='-'))return -1;
  for(const char *x=colon+1;x<end;x++)if(((unsigned char)*x<32&&*x!='\t')||(unsigned char)*x==127)return -1;
  if((size_t)(colon-p)==key&&!strncasecmp(p,wanted,key)){
   if(found++)return -1;const char *a=colon+1,*z=end;while(a<z&&(*a==' '||*a=='\t'))a++;while(z>a&&(z[-1]==' '||z[-1]=='\t'))z--;
   if(z==a||(size_t)(z-a)>=cap)return -1;memcpy(value,a,(size_t)(z-a));value[z-a]=0;
  }
  p=end+2;
 }
 return -1;
}
static int qrs_number(const char *s,unsigned *value)
{
 if(!s||!*s||!value)return 0;unsigned n=0;
 for(;*s;s++){unsigned d=(unsigned char)*s-'0';if(d>9||n>(2147483647u-d)/10)return 0;n=n*10+d;}
 *value=n;return 1;
}
static int qrs_channels(const char *value,unsigned *first,unsigned *last)
{
 static const char prefix[]="RTP/AVP/TCP;unicast;interleaved=";
 if(!value||!first||!last||strncmp(value,prefix,sizeof prefix-1))return 0;
 const char *pair=value+sizeof prefix-1,*hyphen=strchr(pair,'-');
 if(!hyphen||hyphen==pair||hyphen-pair>3||!hyphen[1]||strlen(hyphen+1)>3)return 0;
 char a[4];memcpy(a,pair,(size_t)(hyphen-pair));a[hyphen-pair]=0;
 return qrs_number(a,first)&&qrs_number(hyphen+1,last)&&*first<=254&&*last==*first+1;
}
static int qrs_reply(struct qrs_client *r,unsigned seq,unsigned code,const char *extra,const char *body)
{
 const char *reason=code==200?"OK":code==400?"Bad Request":code==404?"Not Found":code==454?"Session Not Found":code==455?"Method Not Valid in This State":code==461?"Unsupported Transport":code==503?"Service Unavailable":"Not Implemented";
 char b[2304];int n=snprintf(b,sizeof b,"RTSP/1.0 %u %s\r\nCSeq: %u\r\nServer: QC5-native\r\n%sContent-Length: %zu\r\n\r\n%s",code,reason,seq,extra?extra:"",body?strlen(body):0,body?body:"");
 if(n<=0||(size_t)n>=sizeof b||!r->rtp.emit(r->rtp.opaque,(const uint8_t*)b,(size_t)n))return -1;return 0;
}
static void qrs_b64(const uint8_t *p,unsigned n,char *out)
{
 static const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";unsigned used=0;
 for(unsigned i=0;i<n;i+=3){unsigned left=n-i;uint32_t v=(uint32_t)p[i]<<16;if(left>1)v|=(uint32_t)p[i+1]<<8;if(left>2)v|=p[i+2];
  out[used++]=table[v>>18];out[used++]=table[(v>>12)&63];out[used++]=left>1?table[(v>>6)&63]:'=';out[used++]=left>2?table[v&63]:'=';}
 out[used]=0;
}
static int qrs_describe(struct qrs_client *r,unsigned seq,const char *url,const char *path)
{
 const struct qrs_config *c=r->config;
 if(c->sps_n<4||c->sps_n>256||!c->pps_n||c->pps_n>256||(c->sps[0]&31)!=7||(c->pps[0]&31)!=8)return qrs_reply(r,seq,503,NULL,NULL);
 size_t prefix=(size_t)(path-url);if(prefix+7>=sizeof r->base)return qrs_reply(r,seq,400,NULL,NULL);
 memcpy(r->base,url,prefix);memcpy(r->base+prefix,"/live/",7);
 char sps[345],pps[345],sdp[1300],headers[400];qrs_b64(c->sps,c->sps_n,sps);qrs_b64(c->pps,c->pps_n,pps);
 int n=snprintf(sdp,sizeof sdp,"v=0\r\no=- 1 1 IN IP4 0.0.0.0\r\ns=QC5 native\r\nc=IN IP4 0.0.0.0\r\nt=0 0\r\na=control:*\r\nm=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1;profile-level-id=%02x%02x%02x;sprop-parameter-sets=%s,%s\r\na=control:track0\r\nm=audio 0 RTP/AVP 97\r\na=rtpmap:97 PCMA/16000/1\r\na=control:track1\r\n",c->sps[1],c->sps[2],c->sps[3],sps,pps);
 if(n<=0||(size_t)n>=sizeof sdp)return -1;
 n=snprintf(headers,sizeof headers,"Content-Type: application/sdp\r\nContent-Base: %s\r\n",r->base);if(n<=0||(size_t)n>=sizeof headers)return -1;
 int result=qrs_reply(r,seq,200,headers,sdp);if(!result)r->described=1;return result;
}
static int qrs_request(struct qrs_client *r,const char *request,size_t length)
{
 if(!r||!r->rtp.emit||!request||!length||length>2048||memchr(request,0,length))return -1;
 char copy[2049];memcpy(copy,request,length);copy[length]=0;
 if(length<4||memcmp(copy+length-4,"\r\n\r\n",4))return -1;
 char *line=strstr(copy,"\r\n");if(!line)return -1;*line=0;
 char method[24],url[280],version[16],tail,value[280];unsigned seq,body=0;
 if(sscanf(copy,"%23s %279s %15s %c",method,url,version,&tail)!=3||strcmp(version,"RTSP/1.0"))return -1;
 const char *headers=line+2;
 if(strstr(headers,"\r\n\r\n")!=copy+length-4)return -1;
 for(const unsigned char *p=(const unsigned char*)url;*p;p++)if(*p<33||*p>126)return -1;
 if(qrs_header(headers,"CSeq",value,sizeof value)!=1||!qrs_number(value,&seq))return -1;
 int has=qrs_header(headers,"Content-Length",value,sizeof value);
 if(has<0||(has&&(!qrs_number(value,&body)||body)))return qrs_reply(r,seq,400,NULL,NULL)<0?-1:1;
 if(qrs_header(headers,"Transfer-Encoding",value,sizeof value)!=0)return qrs_reply(r,seq,400,NULL,NULL)<0?-1:1;
 int session=qrs_header(headers,"Session",value,sizeof value);if(session<0)return -1;
 if(session){char *semi=strchr(value,';');if(semi)*semi=0;if(strcmp(value,r->session))return qrs_reply(r,seq,454,NULL,NULL);}
 if(!strcmp(method,"OPTIONS"))return qrs_reply(r,seq,200,"Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, GET_PARAMETER, TEARDOWN\r\n",NULL);
 if(strncmp(url,"rtsp://",7))return qrs_reply(r,seq,404,NULL,NULL);
 const char *path=strchr(url+7,'/');if(!path||path==url+7)return qrs_reply(r,seq,404,NULL,NULL);
 unsigned aggregate=!strcmp(path,"/live")||!strcmp(path,"/live/");int track=!strcmp(path,"/live/track0")?0:!strcmp(path,"/live/track1")?1:-1;
 if(!aggregate&&track<0)return qrs_reply(r,seq,404,NULL,NULL);
 if(!strcmp(method,"DESCRIBE"))return aggregate?qrs_describe(r,seq,url,path):qrs_reply(r,seq,404,NULL,NULL);
 char reply[900];
 if(!strcmp(method,"SETUP")){
  if(track<0||!r->described||r->playing)return qrs_reply(r,seq,455,NULL,NULL);
  if((r->setup[0]||r->setup[1])&&!session)return qrs_reply(r,seq,454,NULL,NULL);
  if(qrs_header(headers,"Transport",value,sizeof value)!=1)return qrs_reply(r,seq,461,NULL,NULL);
  unsigned first,last;
  if(!qrs_channels(value,&first,&last)||
    (r->setup[1-track]&&(first==r->rtp.channel[1-track]||first==r->rtp.channel[1-track]+1||last==r->rtp.channel[1-track])))return qrs_reply(r,seq,461,NULL,NULL);
  r->setup[track]=1;r->rtp.channel[track]=first;
  int n=snprintf(reply,sizeof reply,"Session: %s;timeout=60\r\nTransport: RTP/AVP/TCP;unicast;interleaved=%u-%u;ssrc=%08X\r\n",r->session,first,last,r->rtp.ssrc[track]);if(n<=0||(size_t)n>=sizeof reply)return -1;
  return qrs_reply(r,seq,200,reply,NULL);
 }
 if(!r->setup[0]&&!r->setup[1])return qrs_reply(r,seq,455,NULL,NULL);
 if(!session)return qrs_reply(r,seq,454,NULL,NULL);
 snprintf(reply,sizeof reply,"Session: %s;timeout=60\r\n",r->session);
 if(!strcmp(method,"PLAY")){
  if(!aggregate)return qrs_reply(r,seq,455,NULL,NULL);
  /* NPT zero is the current PLAY instant on the shared capture clock.
   * A live keyframe can arrive later. Neither RTP track is restarted. */
  int used=snprintf(reply,sizeof reply,"Session: %s;timeout=60\r\nRange: npt=0.000-\r\nRTP-Info: ",r->session);
  if(used<=0||(size_t)used>=sizeof reply)return -1;unsigned entries=0;
  for(unsigned t=0;t<2;t++)if(r->setup[t]){
   uint64_t elapsed=r->play_us>=r->rtp.origin_us?r->play_us-r->rtp.origin_us:0;unsigned rate=t?16000:90000;
   uint32_t stamp=(uint32_t)((elapsed/1000000u)*rate+(elapsed%1000000u)*rate/1000000u);
   int n=snprintf(reply+used,sizeof reply-(size_t)used,"%surl=%strack%u;seq=%u;rtptime=%u",entries?",":"",r->base,t,r->rtp.sequence[t],stamp);
   if(n<=0||(size_t)n>=sizeof reply-(size_t)used)return -1;used+=n;entries++;
  }
  if((size_t)used+3>sizeof reply)return -1;memcpy(reply+used,"\r\n",3);
  if(!r->playing)r->wait_key=1;r->playing=1;return qrs_reply(r,seq,200,reply,NULL);
 }
 if(!strcmp(method,"PAUSE")){r->playing=0;return qrs_reply(r,seq,200,reply,NULL);}
 if(!strcmp(method,"GET_PARAMETER"))return qrs_reply(r,seq,200,reply,NULL);
 if(!strcmp(method,"TEARDOWN"))return qrs_reply(r,seq,200,reply,NULL)<0?-1:1;
 return qrs_reply(r,seq,501,NULL,NULL);
}
#endif
