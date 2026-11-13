#ifndef QC5_MEMORY_H
#define QC5_MEMORY_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Frame storage is initially untouched after fork. Charge new payload pages
 * before writing them instead of reserving the whole 512 KiB for tiny frames.
 * WebSocket and RTSP keep the same active reserve and parser capacity. */
#define QC5_AUDIO_ADMISSION_KB 2048
/* A recording reader duplicates gateway bookkeeping when forked. Keep a
 * larger admission margin, then stop that reader if the budget deteriorates. */
#define QC5_RECORD_DOWNLOAD_ADMISSION_KB 3072
#define QC5_RECORD_DOWNLOAD_RESERVE_KB 1536
/* Live video commits payload pages only after the per-frame growth check.
 * Reserve 512 KiB above the active floor for session setup instead of charging
 * the complete, untouched maximum frame buffer a second time at admission. */
#define QC5_VIDEO_ADMISSION_KB 1536
#define QC5_STREAM_RESERVE_KB 1024
#define QC5_VIDEO_GROWTH_OVERHEAD_KB 64
#define QC5_VIDEO_MEMORY_LIMIT (-31)
#define QC5_MEMORY_POLL_MS 250
/* Admission control, not a promise of a successful future allocation. Linux
 * 3.10 lacks MemAvailable. On this vendor kernel Cached can exceed the file
 * LRU even at OOM. Cap it to that LRU, discount reclaim costs and keep the
 * 768 KiB high watermark before admitting another native encoder session. */
static long qc5_memory_available_kb(void){
 const char *path="/proc/meminfo";
#ifdef QC5_HOST_TEST
 const char *fixture=getenv("QC5_TEST_MEMINFO");if(fixture&&*fixture)path=fixture;
#endif
 FILE*f=fopen(path,"r");if(!f)return -1;
 long available=-1,free_kb=-1,buffers=0,cached=0,reclaim=0,shmem=0,dirty=0,writeback=0,active_file=0,inactive_file=0;char line[128],key[40];long value;
 while(fgets(line,sizeof(line),f))if(sscanf(line,"%39s %ld",key,&value)==2&&value>=0){
  if(!strcmp(key,"MemAvailable:"))available=value;
  else if(!strcmp(key,"MemFree:"))free_kb=value;
  else if(!strcmp(key,"Buffers:"))buffers=value;
  else if(!strcmp(key,"Cached:"))cached=value;
  else if(!strcmp(key,"SReclaimable:"))reclaim=value;
  else if(!strcmp(key,"Shmem:"))shmem=value;
  else if(!strcmp(key,"Dirty:"))dirty=value;
  else if(!strcmp(key,"Writeback:"))writeback=value;
  else if(!strcmp(key,"Active(file):"))active_file=value;
  else if(!strcmp(key,"Inactive(file):"))inactive_file=value;
 }
 fclose(f);if(available>=0)return available;if(free_kb<0)return -1;
 long cache=cached-shmem-dirty-writeback;if(cache<0)cache=0;
 long file_lru=active_file+inactive_file;if(cache>file_lru)cache=file_lru;
 long usable=free_kb+buffers/2+cache/2+reclaim/4-768;
 return usable>0?usable:0;
}
/* A rejected growth request must not touch the new pages. Keep extra room
 * for mux output and control traffic in addition to the active reserve. */
static inline int qc5_memory_video_growth_ok(size_t committed,size_t needed){
 if(needed<=committed)return 1;
 size_t growth=needed-committed,growth_kb=growth/1024+(growth%1024!=0);
 long available=qc5_memory_available_kb();
 long reserve=QC5_STREAM_RESERVE_KB+QC5_VIDEO_GROWTH_OVERHEAD_KB;
 return available>=reserve&&(unsigned long)(available-reserve)>=growth_kb;
}
/* Check an active stream as well as its initial admission. A missing sample
 * fails closed; do not continue loading an encoder with an unknown budget.
 * The caller closes only its subscription, never the native SDK or camera. */
static inline int qc5_memory_stream_ok(long long now,long long *checked){
 if(*checked>=0&&now>=*checked&&now-*checked<QC5_MEMORY_POLL_MS)return 1;
 *checked=now;return qc5_memory_available_kb()>=QC5_STREAM_RESERVE_KB;
}
#endif
