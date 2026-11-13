/* SPDX-License-Identifier: GPL-2.0-or-later
 * QC5 H.264 byte-stream framing. Implements NAL emulation prevention from
 * the byte-stream syntax, independently of the external OpenIMP helper.
 * Caller retains ownership of both buffers; no allocation or hardware IO.
 */
#ifndef QC5_ANNEXB_NATIVE_H
#define QC5_ANNEXB_NATIVE_H
#include <stddef.h>
#include <stdint.h>
struct qna_writer {uint8_t *data;size_t used,capacity;unsigned zeros,failed;};
static size_t qna_bound(size_t payload)
{
 size_t extra=payload/2+payload%2;
 return payload>SIZE_MAX-5||extra>SIZE_MAX-5-payload?SIZE_MAX:5+payload+extra;
}
static int qna_begin(struct qna_writer *w,uint8_t *out,size_t capacity,unsigned type,unsigned priority)
{
 if(!w)return 0;
 *w=(struct qna_writer){.failed=1};
 if(!out||capacity<5||!type||type>31||priority>3)return 0;
 out[0]=out[1]=out[2]=0;out[3]=1;out[4]=(uint8_t)((priority<<5)|type);
 *w=(struct qna_writer){out,5,capacity,0,0};return 1;
}
static int qna_append(struct qna_writer *w,const uint8_t *payload,size_t bytes)
{
 if(!w||w->failed)return 0;
 if((!payload&&bytes)||!w->data||w->used>w->capacity){w->failed=1;return 0;}
 uintptr_t src=(uintptr_t)payload,dst=(uintptr_t)w->data;
 if(bytes&&(src>UINTPTR_MAX-bytes||dst>UINTPTR_MAX-w->capacity||
    (src<dst+w->capacity&&dst<src+bytes))){w->failed=1;return 0;}
 for(size_t n=0;n<bytes;n++){
  unsigned value=payload[n];
  if(w->zeros==2&&value<=3){
   if(w->used==w->capacity){w->failed=1;return 0;}
   w->data[w->used++]=3;w->zeros=0;
  }
  if(w->used==w->capacity){w->failed=1;return 0;}
  w->data[w->used++]=(uint8_t)value;
  w->zeros=value?0:w->zeros+1;
 }
 return 1;
}
#endif
