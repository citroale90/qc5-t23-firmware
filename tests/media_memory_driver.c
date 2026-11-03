/* Reused payload must be overwritten before parsing; capacity stays 512 KiB. */
#define main gateway_main
#define QC5_HOST_TEST 1
#include "../src/qc5_gateway.c"
#undef main
#include <sys/resource.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"media memory check failed at line %d\n",__LINE__);return 1;}}while(0)
static struct video_parser sample;
static void frame_header(unsigned char*h,unsigned kind,unsigned size){
 memset(h,0,32);memcpy(h,"\x55\xaa\x15\xa8",4);h[4]=(unsigned char)kind;
 for(unsigned i=0;i<4;i++)h[16+i]=(unsigned char)(size>>(8*i));
}
static int measure(int optimized){
 pid_t child=fork();if(child<0)return 1;
 if(!child){
  struct rusage before,after;getrusage(RUSAGE_SELF,&before);
  if(optimized)video_parser_init(&sample,-1);else{memset(&sample,0,sizeof(sample));sample.fd=-1;sample.last_video=now_ms();qc5_mp4_init(&sample.mux,0,15,video_sink,&sample);}
  getrusage(RUSAGE_SELF,&after);
  printf("%s initialization: %ld minor page faults, page size %ld bytes\n",optimized?"metadata-only":"whole-buffer",after.ru_minflt-before.ru_minflt,sysconf(_SC_PAGESIZE));fflush(stdout);_exit(0);
 }
 int status=0;if(waitpid(child,&status,0)!=child)return 1;return !WIFEXITED(status)||WEXITSTATUS(status);
}
static int memory_fixture(const char*path,unsigned kb){
 FILE*f=fopen(path,"w");if(!f)return -1;
 int ok=fprintf(f,"MemAvailable: %u kB\n",kb)>0;
 return fclose(f)||!ok?-1:0;
}
static int growth_tests(const char*path){
 CHECK(!setenv("QC5_TEST_MEMINFO",path,1));
 unsigned char h[32],payload[8192];memset(payload,0x36,sizeof(payload));
 memset(&sample,0xa5,sizeof(sample));video_parser_init(&sample,-1);
 CHECK(!memory_fixture(path,1120));frame_header(h,0,16);
 CHECK(video_bytes(&sample,h,32)==0&&sample.video_committed==0);
 CHECK(video_bytes(&sample,payload,16)==0&&sample.frames==1&&sample.video_committed>0);
 CHECK(sample.video[16]==0xa5);
 video_parser_init(&sample,-1);CHECK(!memory_fixture(path,1400));
 frame_header(h,0,sizeof(sample.video));
 CHECK(video_bytes(&sample,h,32)==-1&&sample.error==QC5_VIDEO_MEMORY_LIMIT);
 CHECK(sample.video_committed==0&&sample.used==0&&sample.video[1000]==0xa5);
 video_parser_init(&sample,-1);CHECK(!memory_fixture(path,2048));
 CHECK(video_bytes(&sample,h,32)==0&&sample.video_committed==0);
 CHECK(video_bytes(&sample,payload,16)==0&&sample.used==16&&sample.video_committed>0);
 size_t committed=sample.video_committed;
 CHECK(!memory_fixture(path,1088));
 CHECK(video_bytes(&sample,payload,sizeof(payload))==-1&&sample.error==QC5_VIDEO_MEMORY_LIMIT);
 CHECK(sample.used==16&&sample.video_committed==committed&&sample.video[16]==0xa5);
 CHECK(!memory_fixture(path,1024));
 CHECK(qc5_memory_video_growth_ok(sample.video_committed,sample.video_committed));
 CHECK(!qc5_memory_video_growth_ok(0,SIZE_MAX));
 video_parser_init(&sample,-1);CHECK(!unlink(path));frame_header(h,0,16);
 CHECK(video_bytes(&sample,h,32)==-1&&sample.error==QC5_VIDEO_MEMORY_LIMIT&&sample.video_committed==0);
 puts("6 video growth reserve checks passed");return 0;
}
static long resident_kib(void){
 FILE*f=fopen("/proc/self/status","r");if(!f)return -1;
 char line[160];long value=-1;
 while(fgets(line,sizeof(line),f))if(sscanf(line,"VmRSS: %ld kB",&value)==1)break;
 fclose(f);return value;
}
static unsigned delivered;
static int exact_frame(void*opaque,const unsigned char*bytes,size_t size,uint64_t stamp,unsigned duration){
 (void)opaque;(void)stamp;(void)duration;
 size_t expected=delivered==1?16*1024:sizeof(sample.video);
 unsigned char value=delivered==2?0x47:0x36;
 if(delivered>=3||size!=expected)return -1;
 for(size_t i=0;i<size;i++)if(bytes[i]!=value)return -1;
 delivered++;return 0;
}
static int feed_pattern(unsigned size,unsigned char value){
 unsigned char chunk[4096];memset(chunk,value,sizeof(chunk));
 while(size){unsigned n=size>sizeof(chunk)?sizeof(chunk):size;
  if(video_bytes(&sample,chunk,n))return -1;size-=n;}
 return 0;
}
static int trim_test(void){
 /* Synthetic payload only, bounded to the existing 512 KiB parser capacity.
  * It opens no camera session, media device, network socket or recording. */
 CHECK(qc5_memory_available_kb()>=3000);
 video_parser_init(&sample,-1);sample.ready=1;sample.frame_handler=exact_frame;
 unsigned char h[32];frame_header(h,0,sizeof(sample.video));
 CHECK(!video_bytes(&sample,h,sizeof(h))&&!feed_pattern(sizeof(sample.video),0x36));
 long peak=resident_kib();size_t before=sample.video_committed;
 frame_header(h,1,16*1024);CHECK(!video_bytes(&sample,h,sizeof(h))&&delivered==1);
 long after=resident_kib();
#ifdef __linux__
 CHECK(before-sample.video_committed>=480*1024);
 CHECK(peak>=0&&after>=0&&peak-after>=384);
#else
 CHECK(sample.video_committed==before);
#endif
 CHECK(!feed_pattern(16*1024,0x36));
 frame_header(h,0,sizeof(sample.video));CHECK(!video_bytes(&sample,h,sizeof(h))&&delivered==2);
 CHECK(!feed_pattern(sizeof(sample.video),0x47));
 frame_header(h,1,16*1024);CHECK(!video_bytes(&sample,h,sizeof(h))&&delivered==3);
 printf("{\"frames_exact\":%u,\"maximum_frame_bytes\":%zu,\"rss_before_kib\":%ld,\"rss_after_kib\":%ld,\"rss_reclaimed_kib\":%ld,\"payload_capacity_unchanged\":true}\n",delivered,sizeof(sample.video),peak,after,peak>=0&&after>=0?peak-after:0);
 return 0;
}
int main(int argc,char**argv){
 if(argc==3&&!strcmp(argv[1],"--growth-tests"))return growth_tests(argv[2]);
 if(argc==2&&!strcmp(argv[1],"--trim-check"))return trim_test();
 memset(&sample,0xa5,sizeof(sample));
 if(argc==2&&!strcmp(argv[1],"--measure")){if(measure(0)||measure(1))return 1;return 0;}
 video_parser_init(&sample,-1);
 for(size_t i=0;i<sizeof(sample.video);i++)CHECK(sample.video[i]==0xa5);
 CHECK(sample.head==0&&sample.remaining==0&&sample.pending==0&&sample.expected==0&&sample.frames==0&&sample.clock.started==0&&sample.fd==-1);
 unsigned char h[32],payload[40];memset(payload,0x36,sizeof(payload));frame_header(h,1,sizeof(payload));
 CHECK(video_bytes(&sample,h,2)==0&&sample.head==2);
 CHECK(video_bytes(&sample,h+2,30)==0&&sample.remaining==sizeof(payload));
 CHECK(video_bytes(&sample,payload,7)==0&&sample.remaining==33&&sample.pending==0);
 CHECK(video_bytes(&sample,payload+7,33)==0&&sample.pending==40&&sample.frames==1);
 CHECK(!memcmp(sample.video,payload,40)&&sample.video[40]==0xa5);
 video_parser_init(&sample,-1);frame_header(h,12,8);
 CHECK(video_bytes(&sample,h,32)==0&&video_bytes(&sample,payload,8)==0);
 CHECK(sample.head==0&&sample.frames==0&&sample.pending==0&&sample.used==8&&!memcmp(sample.audio+2,payload,8));
 video_parser_init(&sample,-1);frame_header(h,0,sizeof(sample.video)+1);
 CHECK(video_bytes(&sample,h,32)==-1&&sample.error==-11);
 puts("4 media buffer reuse and size checks passed");return 0;
}
