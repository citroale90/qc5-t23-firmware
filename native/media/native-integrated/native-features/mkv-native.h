/* SPDX-License-Identifier: GPL-3.0-only
 * Streaming Matroska, existing Annex-B H264 + PCMA16k mono. No transcoding,
 * heap allocation, frame copies, indexing table or second encoder.
 * Segment/cluster unknown sizes allow already complete blocks to survive a
 * truncated tail. The caller owns file lifecycle and capacity checks.
 */
#ifndef QC5_MKV_NATIVE_H
#define QC5_MKV_NATIVE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int (*qkv_sink)(void *,const void *,size_t);
struct qkv_mux {qkv_sink sink;void *opaque;uint64_t origin_ms,cluster_ms,video_ms,audio_ms,frames,samples,bytes;unsigned started,failed,width,height;};
struct qkv_buf {uint8_t b[1536];size_t n;};
static unsigned qkv_size(uint8_t out[8],uint64_t n)
{for(unsigned k=1;k<=8;k++)if(n<((UINT64_C(1)<<(7*k))-1)){for(unsigned j=0;j<k;j++)out[k-1-j]=(uint8_t)(n>>(j*8));out[0]|=(uint8_t)(1u<<(8-k));return k;}return 0;}
static unsigned qkv_id(uint8_t out[4],uint32_t id)
{unsigned n=id>0xffffff?4:id>0xffff?3:id>0xff?2:1;for(unsigned i=0;i<n;i++)out[n-i-1]=(uint8_t)(id>>(8*i));return n;}
static int qkv_append(struct qkv_buf *b,const void *p,size_t n)
{if(!b||!p||n>sizeof b->b-b->n)return 0;memcpy(b->b+b->n,p,n);b->n+=n;return 1;}
static int qkv_element(struct qkv_buf *b,uint32_t id,const void *p,size_t n)
{uint8_t a[4],s[8];unsigned an=qkv_id(a,id),sn=qkv_size(s,n);return sn&&qkv_append(b,a,an)&&qkv_append(b,s,sn)&&qkv_append(b,p,n);}
static int qkv_uint(struct qkv_buf *b,uint32_t id,uint64_t n)
{uint8_t v[8];unsigned bytes=1;while(bytes<8&&(n>>(8*bytes)))bytes++;for(unsigned k=0;k<bytes;k++)v[bytes-k-1]=(uint8_t)(n>>(8*k));return qkv_element(b,id,v,bytes);}
static int qkv_text(struct qkv_buf *b,uint32_t id,const char *s)
{return qkv_element(b,id,s,strlen(s));}
static int qkv_put(struct qkv_mux *m,const void *p,size_t n)
{if(!m||m->failed||!m->sink||!p||!n||m->bytes>UINT64_MAX-n)return 0;if(!m->sink(m->opaque,p,n)){m->failed=1;return 0;}m->bytes+=n;return 1;}
static unsigned qkv_prefix(const uint8_t *p,size_t n,size_t at)
{if(at+3<=n&&!p[at]&&!p[at+1]&&p[at+2]==1)return 3;if(at+4<=n&&!p[at]&&!p[at+1]&&!p[at+2]&&p[at+3]==1)return 4;return 0;}
struct qkv_nal {size_t at,n;};
static unsigned qkv_nals(const uint8_t *p,size_t n,struct qkv_nal out[128])
{
 if(!p||!n||n>786944||!qkv_prefix(p,n,0))return 0;size_t at=0;unsigned count=0;
 while(at<n){unsigned prefix=qkv_prefix(p,n,at);if(!prefix||count==128)return 0;size_t first=at+prefix;at=first;
  while(at<n&&!qkv_prefix(p,n,at))at++;size_t end=at;while(end>first&&!p[end-1])end--;
  if(end==first||(p[first]&0x80)||!(p[first]&31)||(p[first]&31)>=24)return 0;
  out[count++]=(struct qkv_nal){first,end-first};}
 return count;
}
static int qkv_header(struct qkv_mux *m,const uint8_t *au,const struct qkv_nal *nal,unsigned count)
{
 const uint8_t *sps=NULL,*pps=NULL;size_t ns=0,np=0;
 for(unsigned k=0;k<count;k++){unsigned type=au[nal[k].at]&31;if(type==7){sps=au+nal[k].at;ns=nal[k].n;}if(type==8){pps=au+nal[k].at;np=nal[k].n;}}
 if(!sps||!pps||ns<4||ns>256||np>256)return 0;
 uint8_t avcc[523]={1,sps[1],sps[2],sps[3],255,225,(uint8_t)(ns>>8),(uint8_t)ns};
 memcpy(avcc+8,sps,ns);size_t at=8+ns;avcc[at++]=1;avcc[at++]=(uint8_t)(np>>8);avcc[at++]=(uint8_t)np;memcpy(avcc+at,pps,np);at+=np;
 struct qkv_buf top={0},body={0},tracks={0},entry={0},video={0},audio={0};
 if(!qkv_uint(&body,0x4286,1)||!qkv_uint(&body,0x42f7,1)||!qkv_uint(&body,0x42f2,4)||!qkv_uint(&body,0x42f3,8)||
  !qkv_text(&body,0x4282,"matroska")||!qkv_uint(&body,0x4287,4)||!qkv_uint(&body,0x4285,2)||!qkv_element(&top,0x1a45dfa3,body.b,body.n))return 0;
 if(!qkv_put(m,top.b,top.n))return 0;
 static const uint8_t segment[]={0x18,0x53,0x80,0x67,1,255,255,255,255,255,255,255};if(!qkv_put(m,segment,sizeof segment))return 0;
 top.n=body.n=0;if(!qkv_uint(&body,0x2ad7b1,1000000)||!qkv_text(&body,0x4d80,"qc5-native")||!qkv_text(&body,0x5741,"qc5-native")||
  !qkv_element(&top,0x1549a966,body.b,body.n)||!qkv_put(m,top.b,top.n))return 0;
 if(!qkv_uint(&entry,0xd7,1)||!qkv_uint(&entry,0x73c5,1)||!qkv_uint(&entry,0x83,1)||!qkv_text(&entry,0x86,"V_MPEG4/ISO/AVC")||
  !qkv_element(&entry,0x63a2,avcc,at)||!qkv_uint(&entry,0x23e383,40000000)||!qkv_uint(&video,0xb0,m->width)||!qkv_uint(&video,0xba,m->height)||
  !qkv_element(&entry,0xe0,video.b,video.n)||!qkv_element(&tracks,0xae,entry.b,entry.n))return 0;
 /* WAVEFORMATEX, format tag0x0006 = A-law, 16kHz, mono,8bit. */
 static const uint8_t wave[]={6,0,1,0,0x80,0x3e,0,0,0x80,0x3e,0,0,1,0,8,0,0,0};
 static const uint8_t rate[]={0x40,0xcf,0x40,0,0,0,0,0};
 entry.n=0;if(!qkv_uint(&entry,0xd7,2)||!qkv_uint(&entry,0x73c5,2)||!qkv_uint(&entry,0x83,2)||!qkv_text(&entry,0x86,"A_MS/ACM")||
  !qkv_element(&entry,0x63a2,wave,sizeof wave)||!qkv_element(&audio,0xb5,rate,sizeof rate)||!qkv_uint(&audio,0x9f,1)||!qkv_uint(&audio,0x6264,8)||
  !qkv_element(&entry,0xe1,audio.b,audio.n)||!qkv_element(&tracks,0xae,entry.b,entry.n))return 0;
 top.n=0;return qkv_element(&top,0x1654ae6b,tracks.b,tracks.n)&&qkv_put(m,top.b,top.n);
}
static int qkv_cluster(struct qkv_mux *m,uint64_t ms)
{
 static const uint8_t cluster[]={0x1f,0x43,0xb6,0x75,1,255,255,255,255,255,255,255};struct qkv_buf b={0};
 if(!qkv_put(m,cluster,sizeof cluster)||!qkv_uint(&b,0xe7,ms)||!qkv_put(m,b.b,b.n))return 0;m->cluster_ms=ms;return 1;
}
static int qkv_block_header(struct qkv_mux *m,unsigned track,unsigned key,uint64_t ms,size_t bytes)
{
 if(ms>INT64_MAX||m->cluster_ms>INT64_MAX||bytes>786944u+512)return 0;
 int64_t delta=(int64_t)ms-(int64_t)m->cluster_ms;if(delta< -32768||delta>32767)return 0;
 uint8_t h[13]={0xa3};unsigned n=qkv_size(h+1,bytes+4);if(!n)return 0;h[1+n]=(uint8_t)(0x80|track);
 h[2+n]=(uint8_t)((uint16_t)delta>>8);h[3+n]=(uint8_t)delta;h[4+n]=key?0x80:0;
 return qkv_put(m,h,n+5);
}
static int qkv_init(struct qkv_mux *m,qkv_sink sink,void *opaque,unsigned width,unsigned height)
{if(!m||!sink||!width||width>4096||!height||height>4096)return 0;*m=(struct qkv_mux){.sink=sink,.opaque=opaque,.width=width,.height=height};return 1;}
static int qkv_video(struct qkv_mux *m,const uint8_t *au,size_t n,uint64_t capture_us,unsigned key)
{
 if(!m||m->failed||!capture_us||key>1)return 0;struct qkv_nal nal[128];unsigned count=qkv_nals(au,n,nal);if(!count)return 0;
 unsigned has_idr=0;size_t bytes=0;for(unsigned k=0;k<count;k++){if((au[nal[k].at]&31)==5)has_idr=1;bytes+=4+nal[k].n;}if(key!=has_idr)return 0;
 uint64_t ms=capture_us/1000;if(m->frames&&ms<=m->video_ms)return 0;
 if(!m->started){if(!key)return 1;if(!qkv_header(m,au,nal,count))return 0;m->origin_ms=ms;m->started=1;if(!qkv_cluster(m,0))return 0;}
 if(ms<m->origin_ms)return 0;uint64_t relative=ms-m->origin_ms;
 if(relative>m->cluster_ms+2000&& !qkv_cluster(m,relative))return 0;
 if(!qkv_block_header(m,1,key,relative,bytes))return 0;
 for(unsigned k=0;k<count;k++){uint32_t len=(uint32_t)nal[k].n;uint8_t size[4]={(uint8_t)(len>>24),(uint8_t)(len>>16),(uint8_t)(len>>8),(uint8_t)len};
  if(!qkv_put(m,size,4)||!qkv_put(m,au+nal[k].at,nal[k].n))return 0;}
 m->video_ms=ms;m->frames++;return 1;
}
static int qkv_audio(struct qkv_mux *m,const uint8_t *pcma,size_t n,uint64_t capture_us)
{
 if(!m||m->failed||!pcma||!n||n>4096||!capture_us)return 0;if(!m->started)return 1;uint64_t ms=capture_us/1000;
 if(ms<m->origin_ms)return 1;if(m->samples&&ms<=m->audio_ms)return 0;
 uint64_t rel=ms-m->origin_ms;if(rel>m->cluster_ms+2000&&!qkv_cluster(m,rel))return 0;
 if(!qkv_block_header(m,2,1,rel,n)||!qkv_put(m,pcma,n))return 0;m->audio_ms=ms;m->samples+=n;return 1;
}
#endif
