/* SPDX-License-Identifier: GPL-2.0-or-later
 * Generic local PCM interface between the separate codec process and the
 * WebUI server. This header contains no codec/device implementation. One
 * SOCK_SEQPACKET record, bounded sizes, explicit LE wire fields and nonce.
 */
#ifndef QC5_AUDIO_STREAM_IPC_H
#define QC5_AUDIO_STREAM_IPC_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define QAS_IPC_MAGIC UINT32_C(0x37334153)
#define QAS_IPC_HEADER 56u
#define QAS_IPC_PCM 1u
#define QAS_IPC_FINAL 2u
#define QAS_IPC_PCM_BYTES 2000u
#define QAS_IPC_FINAL_BYTES 80u
#define QAS_IPC_MAX (QAS_IPC_HEADER+QAS_IPC_PCM_BYTES)
struct qas_ipc_record {uint32_t kind,bytes;uint64_t serial,stamp;const uint8_t *payload;};
static uint32_t qasi_hash(const uint8_t *p,size_t n){uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static void qasi_le32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void qasi_le64(uint8_t *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(8*i));}
static uint32_t qasi_u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t qasi_u64(const uint8_t *p){return qasi_u32(p)|(uint64_t)qasi_u32(p+4)<<32;}
static int qasi_pack(uint8_t *out,size_t capacity,uint32_t kind,const void *payload,size_t bytes,
                     uint64_t serial,uint64_t stamp,const uint8_t nonce[16])
{
 if(!out||!payload||!nonce||!serial||!stamp||
    (kind==QAS_IPC_PCM?bytes!=QAS_IPC_PCM_BYTES:kind==QAS_IPC_FINAL?bytes!=QAS_IPC_FINAL_BYTES:1)||
    capacity<QAS_IPC_HEADER+bytes)return 0;
 memset(out,0,QAS_IPC_HEADER);qasi_le32(out,QAS_IPC_MAGIC);qasi_le32(out+4,1);qasi_le32(out+8,kind);qasi_le32(out+12,(uint32_t)bytes);
 qasi_le64(out+16,serial);qasi_le64(out+24,stamp);memcpy(out+32,nonce,16);
 memcpy(out+QAS_IPC_HEADER,payload,bytes);qasi_le32(out+48,qasi_hash(out+QAS_IPC_HEADER,bytes));
 return (int)(QAS_IPC_HEADER+bytes);
}
static int qasi_decode(const uint8_t *raw,size_t bytes,const uint8_t nonce[16],struct qas_ipc_record *out)
{
 if(!raw||!nonce||!out||bytes<QAS_IPC_HEADER||bytes>QAS_IPC_MAX||
    qasi_u32(raw)!=QAS_IPC_MAGIC||qasi_u32(raw+4)!=1||memcmp(raw+32,nonce,16)||qasi_u32(raw+52))return 0;
 uint32_t kind=qasi_u32(raw+8),n=qasi_u32(raw+12);
 if((kind==QAS_IPC_PCM?n!=QAS_IPC_PCM_BYTES:kind==QAS_IPC_FINAL?n!=QAS_IPC_FINAL_BYTES:1)||
    bytes!=QAS_IPC_HEADER+n||!qasi_u64(raw+16)||!qasi_u64(raw+24)||qasi_u32(raw+48)!=qasi_hash(raw+QAS_IPC_HEADER,n))return 0;
 *out=(struct qas_ipc_record){kind,n,qasi_u64(raw+16),qasi_u64(raw+24),raw+QAS_IPC_HEADER};return 1;
}
/* The descriptor is inherited over socketpair, not a named/public socket.
 * A full receive queue discards only this private packet; it never blocks
 * the ADC/capture loop, restarts DMA or sends a partial message. */
static int qasi_send(int fd,const uint8_t *raw,size_t bytes)
{
 if(fd<0||!raw||bytes<QAS_IPC_HEADER||bytes>QAS_IPC_MAX)return 0;
 return send(fd,raw,bytes,MSG_NOSIGNAL|MSG_DONTWAIT)==(ssize_t)bytes;
}
#endif
