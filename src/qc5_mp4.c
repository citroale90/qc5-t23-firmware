#include "qc5_mp4.h"
#include <string.h>
#include <limits.h>
struct box {uint8_t b[2048];size_t n,mark[16];unsigned depth;int bad;};
static void bytes(struct box*b,const void*p,size_t n){if(n>sizeof(b->b)-b->n){b->bad=1;return;}memcpy(b->b+b->n,p,n);b->n+=n;}
static void u8(struct box*b,unsigned v){uint8_t x=(uint8_t)v;bytes(b,&x,1);}
static void u16(struct box*b,unsigned v){u8(b,v>>8);u8(b,v);}
static void u32(struct box*b,uint32_t v){u16(b,v>>16);u16(b,v);}
static void u64(struct box*b,uint64_t v){u32(b,(uint32_t)(v>>32));u32(b,(uint32_t)v);}
static void zeros(struct box*b,unsigned n){while(n--)u8(b,0);}
static void start(struct box*b,const char*t){if(b->depth>=16){b->bad=1;return;}b->mark[b->depth++]=b->n;u32(b,0);bytes(b,t,4);}
static void end(struct box*b){if(!b->depth){b->bad=1;return;}size_t at=b->mark[--b->depth],n=b->n-at;if(at+4>b->n||n>UINT32_MAX){b->bad=1;return;}b->b[at]=(uint8_t)(n>>24);b->b[at+1]=(uint8_t)(n>>16);b->b[at+2]=(uint8_t)(n>>8);b->b[at+3]=(uint8_t)n;}
static void full(struct box*b,const char*t,unsigned flags){start(b,t);u32(b,flags);}
static void matrix(struct box*b){u32(b,0x10000);u32(b,0);u32(b,0);u32(b,0);u32(b,0x10000);u32(b,0);u32(b,0);u32(b,0);u32(b,0x40000000);}
struct bits {uint8_t b[256];size_t n,pos;int bad;};
static unsigned bit(struct bits*b){if(b->pos>=b->n*8){b->bad=1;return 0;}unsigned r=(b->b[b->pos/8]>>(7-b->pos%8))&1;b->pos++;return r;}
static unsigned bits(struct bits*b,unsigned n){unsigned r=0;while(n--)r=r*2+bit(b);return r;}
static unsigned ue(struct bits*b){unsigned z=0;while(!b->bad&&!bit(b)){if(++z>24){b->bad=1;return 0;}}return ((1u<<z)-1)+bits(b,z);}
static int se(struct bits*b){unsigned n=ue(b);return n&1?(int)((n+1)/2):-(int)(n/2);}
static void rbsp(struct bits*b,const uint8_t*p,size_t n){memset(b,0,sizeof(*b));unsigned zero=0;for(size_t i=1;i<n;i++){if(zero>=2&&p[i]==3){zero=0;continue;}if(b->n==sizeof(b->b)){b->bad=1;return;}b->b[b->n++]=p[i];zero=p[i]?0:zero+1;}}
static int dimensions(const uint8_t*p,size_t n,unsigned*w,unsigned*h){
 struct bits b;rbsp(&b,p,n);unsigned profile=bits(&b,8);bits(&b,16);ue(&b);unsigned chroma=1,separate=0;
 if(profile==100||profile==110||profile==122||profile==244||profile==44||profile==83||profile==86||profile==118||profile==128||profile==138||profile==139||profile==134||profile==135){
  chroma=ue(&b);if(chroma>3)return -1;if(chroma==3)separate=bit(&b);ue(&b);ue(&b);bit(&b);
  if(bit(&b))for(unsigned i=0;i<(chroma==3?12u:8u);i++)if(bit(&b)){int last=8,next=8;unsigned count=i<6?16:64;for(unsigned j=0;j<count;j++){if(next)next=(last+se(&b)+256)%256;last=next?next:last;}}
 }
 ue(&b);unsigned order=ue(&b);if(order==0)ue(&b);else if(order==1){bit(&b);se(&b);se(&b);unsigned count=ue(&b);if(count>256)return -1;for(unsigned i=0;i<count;i++)se(&b);}else if(order>2)return -1;
 ue(&b);bit(&b);unsigned mw=ue(&b),mh=ue(&b),frame=bit(&b);if(!frame)bit(&b);bit(&b);
 unsigned left=0,right=0,top=0,bottom=0;if(bit(&b)){left=ue(&b);right=ue(&b);top=ue(&b);bottom=ue(&b);}
 unsigned array=separate?0:chroma,cx=(array==1||array==2)?2:1,cy=(array==1?2:1)*(2-frame);
 if(b.bad||mw>255||mh>255||left>4096||right>4096||top>4096||bottom>4096)return -1;
 unsigned x=(mw+1)*16,y=(mh+1)*16*(2-frame);if((left+right)*cx>=x||(top+bottom)*cy>=y)return -1;
 *w=x-(left+right)*cx;*h=y-(top+bottom)*cy;return *w>=16&&*h>=16?0:-1;
}
static size_t prefix(const uint8_t*p,size_t n,size_t at){if(at+3<=n&&p[at]==0&&p[at+1]==0){if(p[at+2]==1)return 3;if(at+4<=n&&p[at+2]==0&&p[at+3]==1)return 4;}return 0;}
struct nal {const uint8_t*p;size_t n;};
static int next_nal(const uint8_t*p,size_t n,size_t*at,struct nal*x){size_t z=0;while(*at<n&&!(z=prefix(p,n,*at)))(*at)++;if(!z)return 0;size_t begin=*at+z;*at=begin;while(*at<n&&!prefix(p,n,*at))(*at)++;size_t finish=*at;while(finish>begin&&p[finish-1]==0)finish--;if(finish==begin)return -1;x->p=p+begin;x->n=finish-begin;return 1;}
static void empty_table(struct box*b,const char*t){full(b,t,0);u32(b,0);end(b);}
static void track(struct box*b,struct qc5_mp4*m,unsigned id){
 int video=id==1;start(b,"trak");full(b,"tkhd",7);u32(b,0);u32(b,0);u32(b,id);u32(b,0);u32(b,0);zeros(b,8);u16(b,0);u16(b,0);u16(b,video?0:0x100);u16(b,0);matrix(b);u32(b,video?m->width<<16:0);u32(b,video?m->height<<16:0);end(b);
 start(b,"mdia");full(b,"mdhd",0);u32(b,0);u32(b,0);u32(b,video?90000:m->rate);u32(b,0);u16(b,0x55c4);u16(b,0);end(b);
 full(b,"hdlr",0);u32(b,0);bytes(b,video?"vide":"soun",4);zeros(b,12);bytes(b,video?"QC5 video":"QC5 audio",10);end(b);
 start(b,"minf");full(b,video?"vmhd":"smhd",video?1:0);zeros(b,video?8:4);end(b);
 start(b,"dinf");full(b,"dref",0);u32(b,1);full(b,"url ",1);end(b);end(b);end(b);
 start(b,"stbl");full(b,"stsd",0);u32(b,1);
 if(video){start(b,m->rate||m->live_avc1?"avc1":"avc3");zeros(b,6);u16(b,1);zeros(b,16);u16(b,m->width);u16(b,m->height);u32(b,0x480000);u32(b,0x480000);u32(b,0);u16(b,1);zeros(b,32);u16(b,24);u16(b,65535);
  start(b,"avcC");u8(b,1);bytes(b,m->sps+1,3);u8(b,255);u8(b,225);u16(b,(unsigned)m->sps_len);bytes(b,m->sps,m->sps_len);u8(b,1);u16(b,(unsigned)m->pps_len);bytes(b,m->pps,m->pps_len);end(b);end(b);
 }else{start(b,"mp4a");zeros(b,6);u16(b,1);zeros(b,8);u16(b,1);u16(b,16);u16(b,0);u16(b,0);u32(b,m->rate<<16);
  full(b,"esds",0);u8(b,3);u8(b,25);u16(b,2);u8(b,0);u8(b,4);u8(b,17);u8(b,0x40);u8(b,0x15);zeros(b,3);u32(b,32000);u32(b,32000);u8(b,5);u8(b,2);u16(b,(2<<11)|((m->rate==16000?8:11)<<7)|(1<<3));u8(b,6);u8(b,1);u8(b,2);end(b);end(b);
 }
 end(b);empty_table(b,"stts");empty_table(b,"stsc");full(b,"stsz",0);u32(b,0);u32(b,0);end(b);empty_table(b,"stco");end(b);end(b);end(b);end(b);
}
static int header(struct qc5_mp4*m){
 struct box b={0};start(&b,"ftyp");bytes(&b,"iso6",4);u32(&b,1);bytes(&b,"iso6isommp41dash",16);end(&b);
 start(&b,"moov");full(&b,"mvhd",0);u32(&b,0);u32(&b,0);u32(&b,1000);u32(&b,0);u32(&b,0x10000);u16(&b,0x100);zeros(&b,10);matrix(&b);zeros(&b,24);u32(&b,3);end(&b);
 track(&b,m,1);if(m->rate)track(&b,m,2);start(&b,"mvex");for(unsigned id=1;id<=(m->rate?2u:1u);id++){full(&b,"trex",0);u32(&b,id);u32(&b,1);u32(&b,id==1?90000/m->fps:1024);u32(&b,0);u32(&b,0);end(&b);}end(&b);end(&b);
 if(b.bad||b.depth)return -1;
 return m->sink(m->opaque,b.b,b.n)?-2:0;
}
static int fragment(struct qc5_mp4*m,unsigned id,uint64_t stamp,unsigned size,int key){
 struct box b={0};start(&b,"moof");full(&b,"mfhd",0);u32(&b,++m->sequence);end(&b);start(&b,"traf");full(&b,"tfhd",0x20000);u32(&b,id);end(&b);full(&b,"tfdt",0x1000000);u64(&b,stamp);end(&b);
 full(&b,"trun",0x701);u32(&b,1);size_t offset=b.n;u32(&b,0);u32(&b,id==1?(m->duration90?m->duration90:90000/m->fps):1024);u32(&b,size);u32(&b,key?0x02000000:0x01010000);end(&b);end(&b);end(&b);
 unsigned n=(unsigned)b.n+8;b.b[offset]=(uint8_t)(n>>24);b.b[offset+1]=(uint8_t)(n>>16);b.b[offset+2]=(uint8_t)(n>>8);b.b[offset+3]=(uint8_t)n;
 u32(&b,size+8);bytes(&b,"mdat",4);if(b.bad||b.depth)return -1;return m->sink(m->opaque,b.b,b.n)?-2:0;
}
int qc5_mp4_init(struct qc5_mp4*m,unsigned rate,unsigned fps,qc5_mp4_sink sink,void*opaque){if(!m||!sink||(rate!=0&&rate!=8000&&rate!=16000)||fps<1||fps>30)return -1;memset(m,0,sizeof(*m));m->rate=rate;m->fps=fps;m->sink=sink;m->opaque=opaque;return 0;}
static int video_impl(struct qc5_mp4*m,const uint8_t*p,size_t n,uint64_t ms){
 if(!m||!p||!n||n>512*1024)return -1;
 size_t at=0,total=0;int key=0,vcl=0,refresh=0,r;struct nal x;
 while((r=next_nal(p,n,&at,&x))>0){unsigned type=x.p[0]&31;if(x.p[0]&128)return -1;
  if(type==7||type==8){uint8_t*dst=type==7?m->sps:m->pps;size_t*len=type==7?&m->sps_len:&m->pps_len;if(x.n>256||(type==7&&x.n<4))return -1;
   if(m->started&&(*len!=x.n||memcmp(dst,x.p,x.n))){
    /* Use avc1 for native Apple decoders. Refresh its initialization segment
     * at an IDR when parameter sets change, retaining the existing timeline. */
    if(m->rate)return -3;
    if(type==7){unsigned width=0,height=0;if(memcmp(m->sps+1,x.p+1,3)||dimensions(x.p,x.n,&width,&height)||width!=m->width||height!=m->height)return -3;}
    refresh=m->live_avc1!=0;
   }
   memcpy(dst,x.p,x.n);*len=x.n;
  }
  if(type==1||type==5){struct bits b;/* Slice header needs only its first two Exp-Golomb fields. */size_t small=x.n<64?x.n:64;rbsp(&b,x.p,small);ue(&b);unsigned slice=ue(&b);if(b.bad||slice>9||slice%5==1)return -1;vcl=1;if(type==5)key=1;}
  total+=4+x.n;
 }
 if(r<0||!vcl||!total||total>UINT32_MAX-8)return -1;
 if(!m->started){if(!key||!m->sps_len||!m->pps_len)return 0;if(dimensions(m->sps,m->sps_len,&m->width,&m->height))return -1;m->origin_ms=ms;r=header(m);if(r)return r;m->started=1;}
 else if(refresh){if(!key)return -3;r=header(m);if(r)return r;}
 if(ms<m->origin_ms||(m->have_video&&ms<=m->video_ms))return -3;
 if(ms-m->origin_ms>UINT64_MAX/90)return -1;
 r=fragment(m,1,(ms-m->origin_ms)*90,(unsigned)total,key);if(r)return r;
 at=0;while(next_nal(p,n,&at,&x)>0){uint8_t length[4]={(uint8_t)(x.n>>24),(uint8_t)(x.n>>16),(uint8_t)(x.n>>8),(uint8_t)x.n};if(m->sink(m->opaque,length,4)||m->sink(m->opaque,x.p,x.n))return -2;}
 m->video_ms=ms;m->have_video=1;return 0;
}
int qc5_mp4_video_timed(struct qc5_mp4*m,const uint8_t*p,size_t n,uint64_t ms,unsigned duration_ms){if(!m||!duration_ms||duration_ms>2000)return -1;m->duration90=duration_ms*90;return video_impl(m,p,n,ms);}
int qc5_mp4_video(struct qc5_mp4*m,const uint8_t*p,size_t n,uint64_t ms){if(!m||!m->fps)return -1;m->duration90=90000/m->fps;return video_impl(m,p,n,ms);}
int qc5_mp4_audio(struct qc5_mp4*m,const uint8_t*p,size_t n,uint64_t ms){
 if(!m||!m->rate||!p||n<7||p[0]!=255||(p[1]&0xf6)!=0xf0)return -1;
 unsigned size=((p[3]&3u)<<11)|(unsigned)p[4]<<3|p[5]>>5,head=(p[1]&1)?7:9;
 unsigned index=(p[2]>>2)&15,channels=((p[2]&1)<<2)|(p[3]>>6);
 if(size!=n||n<=head||channels!=1||index!=(m->rate==16000?8u:11u)||(p[2]>>6)!=1||(p[6]&3))return -1;
 if(!m->started||ms<m->origin_ms)return 0;
 if(m->have_audio&&ms<=m->audio_ms)return -3;
 if(ms-m->origin_ms>UINT64_MAX/m->rate)return -1;
 int r=fragment(m,2,(ms-m->origin_ms)*m->rate/1000,(unsigned)n-head,1);if(r)return r;
 if(m->sink(m->opaque,p+head,n-head))return -2;
 m->audio_ms=ms;m->have_audio=1;return 0;
}
