/* SPDX-License-Identifier: GPL-3.0-only
 * Native H264 RFC6184 / PCMA RTP interleaving. Caller supplies real capture
 * timestamps, a shared origin and a bounded nonblocking emission callback.
 * No vendor clock, camera RPC, resolver, allocation, frame queue or encoder.
 */
#ifndef QC5_NATIVE_RTP_H
#define QC5_NATIVE_RTP_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* Same access-unit capacity as the native encoder/browser mux. This is a
 * length limit, never an allocation: large keyframes use the same1216B stack.
 */
#define QNR_AU_MAX 786944u
#define QNR_AUDIO_MAX 4096u
struct qnr_session {
 void *opaque;int (*emit)(void *,const uint8_t *,size_t);
 uint64_t origin_us,last_us[2];uint32_t ssrc[2],packets[2],octets[2];
 uint16_t sequence[2];unsigned channel[2],seen[2],failed;
};
static void qnr_be32(uint8_t *p,uint32_t v)
{p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static int qnr_begin(struct qnr_session *s,void *opaque,int (*emit)(void *,const uint8_t *,size_t),
                     uint64_t origin,uint32_t video_ssrc,uint32_t audio_ssrc)
{
 if(!s||!emit||!origin||!video_ssrc||!audio_ssrc||video_ssrc==audio_ssrc)return 0;
 memset(s,0,sizeof *s);s->opaque=opaque;s->emit=emit;s->origin_us=origin;
 s->ssrc[0]=video_ssrc;s->ssrc[1]=audio_ssrc;s->channel[1]=2;
 s->sequence[0]=(uint16_t)video_ssrc;s->sequence[1]=(uint16_t)audio_ssrc;return 1;
}
static int qnr_fail(struct qnr_session *s){if(s)s->failed=1;return 0;}
static int qnr_clock(struct qnr_session *s,unsigned track,uint64_t us,uint32_t *stamp)
{
 if(!s||!stamp||!s->emit||s->failed||track>1||us<s->origin_us||
    (s->seen[track]&&us<=s->last_us[track]))return qnr_fail(s);
 uint64_t delta=us-s->origin_us;unsigned rate=track?16000:90000;
 *stamp=(uint32_t)((delta/1000000u)*rate+(delta%1000000u)*rate/1000000u);
 s->last_us[track]=us;s->seen[track]=1;return 1;
}
static int qnr_header(struct qnr_session *s,unsigned track,size_t n,uint32_t stamp,int marker)
{
 if(!s||s->failed||track>1||!n||n>60000||s->channel[track]>254)return qnr_fail(s);
 /* TCP interleaving permits a complete NAL. Emit the tiny header then
  * the existing capture bytes; never copy a large keyframe onto the stack. */
 uint8_t b[16];size_t bytes=n+12;b[0]='$';b[1]=(uint8_t)s->channel[track];
 b[2]=(uint8_t)(bytes>>8);b[3]=(uint8_t)bytes;b[4]=0x80;
 b[5]=(uint8_t)((track?97:96)|(marker?128:0));uint16_t seq=s->sequence[track];
 b[6]=(uint8_t)(seq>>8);b[7]=(uint8_t)seq;qnr_be32(b+8,stamp);qnr_be32(b+12,s->ssrc[track]);
 return s->emit(s->opaque,b,sizeof b)?1:qnr_fail(s);
}
static int qnr_commit(struct qnr_session *s,unsigned track,size_t n)
{s->sequence[track]++;s->packets[track]++;s->octets[track]+=(uint32_t)n;return 1;}
static int qnr_packet(struct qnr_session *s,unsigned track,const uint8_t *p,size_t n,uint32_t stamp,int marker)
{
 if(!p||!qnr_header(s,track,n,stamp,marker))return qnr_fail(s);
 if(!s->emit(s->opaque,p,n))return qnr_fail(s);return qnr_commit(s,track,n);
}
static unsigned qnr_prefix(const uint8_t *p,size_t n,size_t at)
{
 if(at>n)return 0;
 if(n-at>=4&&!p[at]&&!p[at+1]&&!p[at+2]&&p[at+3]==1)return 4;
 return n-at>=3&&!p[at]&&!p[at+1]&&p[at+2]==1?3:0;
}
/* Aggregate a bounded access unit as STAP-A on TCP. All original NAL bytes
 * stay intact and are emitted directly from capture memory, without a copy.
 * Large AUs retain the existing FU-A fallback. */
static int qnr_stap(struct qnr_session *s,const uint8_t *p,size_t n,uint32_t stamp)
{
 size_t at=0,total=1;unsigned count=0,nri=0,oversized=0;
 while(at<n){
  unsigned prefix=qnr_prefix(p,n,at);if(!prefix)return -1;
  size_t first=at+prefix;at=first;while(at<n&&!qnr_prefix(p,n,at))at++;
  size_t end=at;while(end>first&&!p[end-1])end--;
  if(end==first||(p[first]&128)||!(p[first]&31)||(p[first]&31)>23)return -1;
  size_t bytes=end-first;if(bytes>65535)oversized=1;total+=2+bytes;count++;
  if((p[first]&0x60)>nri)nri=p[first]&0x60;
 }
 if(count<2||oversized||total>60000)return 0;
 if(!qnr_header(s,0,total,stamp,1))return -1;
 uint8_t type=(uint8_t)(nri|24);if(!s->emit(s->opaque,&type,1))return -1;
 for(at=0;at<n;){
  size_t first=at+qnr_prefix(p,n,at);at=first;while(at<n&&!qnr_prefix(p,n,at))at++;
  size_t end=at;while(end>first&&!p[end-1])end--;size_t bytes=end-first;
  uint8_t size[2]={(uint8_t)(bytes>>8),(uint8_t)bytes};
  if(!s->emit(s->opaque,size,2)||!s->emit(s->opaque,p+first,bytes))return -1;
 }
 return qnr_commit(s,0,total);
}
static int qnr_nal(struct qnr_session *s,const uint8_t *p,size_t n,uint32_t stamp,int marker)
{
 if(!p||!n||(p[0]&128)||!(p[0]&31)||(p[0]&31)>23)return qnr_fail(s);
 if(n<=60000)return qnr_packet(s,0,p,n,stamp,marker);
 uint8_t b[1200],header=p[0];p++;n--;int first=1;
 while(n){
  size_t take=n>1198?1198:n;b[0]=(uint8_t)((header&0xe0)|28);
  b[1]=(uint8_t)((header&31)|(first?128:0)|(take==n?64:0));memcpy(b+2,p,take);
  if(!qnr_packet(s,0,b,take+2,stamp,marker&&take==n))return 0;
  first=0;p+=take;n-=take;
 }
 return 1;
}
static int qnr_video(struct qnr_session *s,const uint8_t *p,size_t n,uint64_t capture_us)
{
 if(!p||!n||n>QNR_AU_MAX||!qnr_prefix(p,n,0))return qnr_fail(s);
 uint32_t stamp;if(!qnr_clock(s,0,capture_us,&stamp))return 0;
 int aggregate=qnr_stap(s,p,n,stamp);if(aggregate<0)return qnr_fail(s);if(aggregate)return 1;
 size_t at=0;unsigned nals=0;
 while(at<n){
  unsigned prefix=qnr_prefix(p,n,at);if(!prefix)return qnr_fail(s);
  size_t start=at+prefix;at=start;while(at<n&&!qnr_prefix(p,n,at))at++;
  size_t end=at;while(end>start&&!p[end-1])end--;
  if(end==start||!qnr_nal(s,p+start,end-start,stamp,at==n))return qnr_fail(s);
  nals++;
 }
 return nals!=0;
}
static int qnr_audio(struct qnr_session *s,const uint8_t *p,size_t n,uint64_t first_sample_us)
{
 if(!p||!n||n>QNR_AUDIO_MAX)return qnr_fail(s);
 uint32_t stamp;if(!qnr_clock(s,1,first_sample_us,&stamp))return 0;
 size_t at=0;while(at<n){size_t take=n-at>1200?1200:n-at;
  if(!qnr_packet(s,1,p+at,take,stamp+(uint32_t)at,!s->packets[1]))return 0;at+=take;}
 return 1;
}
#endif
