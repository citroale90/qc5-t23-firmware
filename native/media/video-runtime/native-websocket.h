/* SPDX-License-Identifier: GPL-3.0-only
 * Exact existing media.js protocol: MIME text, then tagged <=4097-byte
 * binary messages. The callback consumes each complete message before return
 * and shares the producer's absolute send deadline. No frame queue/copy.
 */
#ifndef QC5_NATIVE_WEBSOCKET_H
#define QC5_NATIVE_WEBSOCKET_H
#include <stdio.h>
#include "native-media.h"
typedef int (*qnws_send)(void *,unsigned,const void *,size_t);
struct qc5_native_websocket {
 struct qc5_native_media media;
 qnws_send send;void *opaque;
 uint8_t output[4097];unsigned used,mime,failed;
};
static int qnws_flush(struct qc5_native_websocket *s)
{
 if(!s||s->failed)return 0;
 if(!s->used)return 1;
 s->output[0]=0;
 if(s->send(s->opaque,2,s->output,(size_t)s->used+1)!=1){s->failed=1;return 0;}
 s->used=0;return 1;
}
static int qnws_sink(void *opaque,const void *data,size_t n)
{
 struct qc5_native_websocket *s=opaque;
 if(s->failed||!data||!n)return -1;
 if(!s->mime){
  const struct qc5_mp4 *m=&s->media.mux;char info[100];
  if(m->sps_len<4){s->failed=1;return -1;}
  int length=snprintf(info,sizeof info,"{\"mime\":\"video/mp4; codecs=\\\"avc1.%02x%02x%02x\\\"\"}",m->sps[1],m->sps[2],m->sps[3]);
  if(length<=0||(size_t)length>=sizeof info||s->send(s->opaque,1,info,(size_t)length)!=1){s->failed=1;return -1;}
  s->mime=1;
 }
 const uint8_t *p=data;
 while(n){
  size_t take=4096-s->used;if(take>n)take=n;
  memcpy(s->output+1+s->used,p,take);s->used+=(unsigned)take;p+=take;n-=take;
  if(s->used==4096&&!qnws_flush(s))return -1;
 }
 return 0;
}
static int qnws_start(struct qc5_native_websocket *s,qnws_send send,void *opaque)
{
 if(!s||!send)return 0;memset(s,0,sizeof *s);s->send=send;s->opaque=opaque;
 return qnm_start(&s->media,qnws_sink,s);
}
static int qnws_frame(struct qc5_native_websocket *s,const struct qc5_frame_output *frame)
{
 if(!s||s->failed)return 0;
 if(!qnm_frame(&s->media,frame)||!qnws_flush(s)){s->failed=1;return 0;}
 return 1;
}
#endif
