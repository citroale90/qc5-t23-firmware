/* SPDX-License-Identifier: GPL-2.0-or-later
 * Private inherited SOCK_SEQPACKET protocol. Distinct from the browser
 * QCT1 format and microphone messages; no public/named listener is created.
 */
#ifndef QC5_TALK_IPC_H
#define QC5_TALK_IPC_H
#include <errno.h>
#include "../audio-continuous/stream-ipc.h"
#define QTIP_MAGIC UINT32_C(0x37335451)
#define QTIP_HEADER 56u
#define QTIP_MAX (QTIP_HEADER+320u)
enum qtip_kind {QTIP_START=1,QTIP_STOP,QTIP_VOICE,QTIP_READY,QTIP_STOPPED,QTIP_FAULT};
struct qtip_record {uint32_t kind,id,bytes;uint64_t serial,stamp;const uint8_t *payload;};
struct qtip_inbox {uint64_t serial,stamp;unsigned fault;};
static int qtip_shape(unsigned kind,const uint8_t *p,size_t bytes)
{
 switch(kind){
 case QTIP_START:return p&&bytes==4&&qasi_u32(p)>=1&&qasi_u32(p)<=30;
 case QTIP_VOICE:return p&&bytes==320;
 case QTIP_FAULT:return p&&bytes==4&&qasi_u32(p)!=0;
 case QTIP_STOP:case QTIP_READY:case QTIP_STOPPED:return bytes==0;
 default:return 0;
 }
}
static int qtip_pack(uint8_t *out,size_t capacity,unsigned kind,uint32_t id,
                     uint64_t serial,uint64_t stamp,const uint8_t nonce[16],const uint8_t *payload,size_t bytes)
{
 if(!out||!nonce||!id||!serial||!stamp||!qtip_shape(kind,payload,bytes)||capacity<QTIP_HEADER+bytes)return 0;
 qasi_le32(out,QTIP_MAGIC);qasi_le32(out+4,1);qasi_le32(out+8,kind);qasi_le32(out+12,(uint32_t)bytes);
 qasi_le64(out+16,serial);qasi_le64(out+24,stamp);memcpy(out+32,nonce,16);
 qasi_le32(out+48,qasi_hash(payload,bytes));qasi_le32(out+52,id);
 if(bytes)memcpy(out+QTIP_HEADER,payload,bytes);return (int)(QTIP_HEADER+bytes);
}
static int qtip_decode(const uint8_t *raw,size_t bytes,const uint8_t nonce[16],struct qtip_record *out)
{
 if(!raw||!nonce||!out||bytes<QTIP_HEADER||bytes>QTIP_MAX||qasi_u32(raw)!=QTIP_MAGIC||qasi_u32(raw+4)!=1||memcmp(raw+32,nonce,16))return 0;
 uint32_t kind=qasi_u32(raw+8),length=qasi_u32(raw+12),id=qasi_u32(raw+52);
 if(length!=bytes-QTIP_HEADER||!qtip_shape(kind,raw+QTIP_HEADER,length)||!id||!qasi_u64(raw+16)||!qasi_u64(raw+24)||qasi_u32(raw+48)!=qasi_hash(raw+QTIP_HEADER,length))return 0;
 *out=(struct qtip_record){kind,id,length,qasi_u64(raw+16),qasi_u64(raw+24),raw+QTIP_HEADER};return 1;
}
/* Fail closed on replay, gaps, cross-direction records or expired input.
 * STOP shares this validation; its caller must shut down on any fault,
 * rather than leaving audio active because a STOP record was rejected. */
static int qtip_accept(struct qtip_inbox *s,const uint8_t *raw,size_t bytes,const uint8_t nonce[16],
                       unsigned requests,uint64_t now,struct qtip_record *out)
{
 if(!s||s->fault)return 0;struct qtip_record r;
 if(!out||!qtip_decode(raw,bytes,nonce,&r)||requests>1||
    (requests?(r.kind>QTIP_VOICE):(r.kind<QTIP_READY))||s->serial==UINT64_MAX||
    r.serial!=s->serial+1||r.stamp<s->stamp||!now||r.stamp>now||now-r.stamp>250000){s->fault=1;return 0;}
 s->serial=r.serial;s->stamp=r.stamp;*out=r;return 1;
}
/* 1 complete record;0 would block;-1 truncated/closed/error. Never wait in
 * the capture loop. MSG_TRUNC detects oversized records on Linux/MIPS. */
static int qtip_receive(int fd,uint8_t raw[QTIP_MAX],size_t *bytes)
{
 if(fd<0||!raw||!bytes)return -1;
 ssize_t got=recv(fd,raw,QTIP_MAX,MSG_DONTWAIT|MSG_TRUNC);
 if(got<0)return errno==EAGAIN||errno==EWOULDBLOCK?0:-1;
 if(got<(ssize_t)QTIP_HEADER||got>(ssize_t)QTIP_MAX)return -1;
 *bytes=(size_t)got;return 1;
}
#endif
