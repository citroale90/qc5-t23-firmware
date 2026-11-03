#ifndef QC5_RECORD_POLICY_H
#define QC5_RECORD_POLICY_H
#include "qc5_record_config.h"
#include "qc5_motion_ipc.h"
static struct qc5_motion_ipc record_ipc={.fd=-1};
static int record_counter_ready;
#ifndef QC5_HOST_TEST
static int record_counter_checked;
#endif
static char record_counter_native[128];
static long long record_policy_next;
static int record_policy_applied=-1;
static int record_counter(uint32_t*out){
 if(record_ipc.fd<0){
  if(!*record_counter_native){
#ifdef QC5_HOST_TEST
   if(native_record_policy_supported(native_app_path()))snprintf(record_counter_native,sizeof(record_counter_native),"%s",native_app_path());
#else
   if(record_counter_checked)return -1;record_counter_checked=1;
   DIR*d=opendir("/proc");if(!d)return -1;struct dirent*e;
   while((e=readdir(d))){
    char*end;long pid=strtol(e->d_name,&end,10);if(*end||pid<1)continue;
    char path[100],comm[32];snprintf(path,sizeof(path),"/proc/%ld/comm",pid);FILE*f=fopen(path,"r");if(!f)continue;
    int ok=fgets(comm,sizeof(comm),f)!=NULL;fclose(f);if(!ok||strcmp(comm,"vp_project\n"))continue;
    snprintf(path,sizeof(path),"/proc/%ld/exe",pid);if(native_record_policy_supported(path))snprintf(record_counter_native,sizeof(record_counter_native),"%s",path);break;
   }
   closedir(d);
#endif
  }
  if(!*record_counter_native)return -1;
  char path[600];int n=snprintf(path,sizeof(path),"%s/motion.pipe",provision_run);
  if(n<0||(size_t)n>=sizeof(path))return -1;
#ifndef QC5_HOST_TEST
  if(strcmp(path,QC5_MOTION_FIFO_PATH))return -1;
#endif
  if(qc5_motion_ipc_open(&record_ipc,path)){record_counter_ready=0;return -1;}
 }
 if(qc5_motion_ipc_read(&record_ipc,now_ms)){record_counter_ready=0;return -1;}
 record_counter_ready=1;*out=record_ipc.sequence;return 0;
}
static int record_native_enable(int enable,int temporary){
 int old,values[21];if(record_plan(&old,values))return -1;
 for(int i=0;i<21;i++)if(values[i]!=-1&&values[i]!=0)return -1;
 if(old==enable)return 0;
 if(enable){
  /* The plan ACK disables scheduling, not necessarily the previous writer.
   * Do not allocate a new recording session while its MP4 is still open.
   * Keep the requested policy pending; the main loop retries after closure.
   * Stop still takes effect immediately and can cancel a pending start. */
  char id[64];if(record_identity(id))return -1;
  int active=record_open_files(id,NULL);if(active<0)return -1;if(active)return 1;
 }
 /* Never fall back to a persistent NVS write on an event edge, including
  * when a counter read fails. Verify the actual native executable again. */
 if(temporary&&(!*record_counter_native||!native_record_policy_supported(record_counter_native)))return -1;
 char params[700];size_t used=(size_t)snprintf(params,sizeof(params),"cmd=2017&command=3&record_plan_enable=%d",enable+(temporary?256:0));
 for(int i=1;i<=21;i++)used+=(size_t)snprintf(params+used,sizeof(params)-used,"&record_plan%d=-1",i);
 int status=-1;
 if(camera_rpc("trans_cmd_string",params,2000)||field_int((char*)response+8,"status",&status)||status||record_plan(&old,values)||old!=enable)return -1;
 for(int i=0;i<21;i++)if(values[i]!=-1)return -1;return 0;
}
static int record_policy_apply(void){
 int wanted=qc5_rec_policy_wanted(&record_policy);
 if(!qc5_sd_mounted(sdroot,0))wanted=0;
 if(wanted==record_policy_applied)return 0;
 int r=record_native_enable(wanted,record_policy_capable);
 if(r<0){record_policy_applied=-1;return -1;}
 if(r>0)return 0;
 record_policy_applied=wanted;return 0;
}
static int record_boolean(const char*key,int fallback){char value[8];cfg(key,value,sizeof(value));return !strcmp(value,"1")?1:!strcmp(value,"0")?0:fallback;}
static int record_policy_initialize(void){
 if(record_policy_initialized)return 0;
 if(load_config())return -1;
 int enabled,values[21];if(record_plan(&enabled,values))return -1;
 for(int i=0;i<21;i++)if(values[i]!=-1&&values[i]!=0)return -1;
 struct motion_state state;if(motion_read(&state))return -1;
 record_policy.continuous=record_boolean("RECORD_CONTINUOUS",0);
 record_policy.motion=record_boolean("MOTION_ENABLE",state.detector==1&&state.armed);
 uint32_t sequence=0;int counter_result=record_counter(&sequence);record_policy_capable=record_counter_ready;
 if(counter_result&&*record_counter_native)return -1;
 if(record_policy_capable){
  if(motion_custom(&state)||!native_motion_local_only())return -1;
  int plan[21];for(int i=0;i<21;i++)plan[i]=-1;
  if((state.record&&motion_set_plan(1,0,plan))||
     (state.detector!=record_policy.motion&&motion_set_plan(2,record_policy.motion,plan))||
     (state.armed!=record_policy.motion&&motion_set_detector(record_policy.motion,state.sensitivity)))return -1;
  qc5_rec_policy_sample(&record_policy,sequence,1,now_ms(),record_policy_post);
 }
 record_policy_applied=enabled;
 if(record_policy_apply())return -1;
 record_policy_initialized=1;return 0;
}
static int record_policy_action(int enabled,int stop_now){
 if(record_policy_initialize())return -1;
 if(stop_now&&record_policy.motion&&!record_policy_capable)return -1;
 struct qc5_rec_policy_state old=record_policy;
 if(stop_now)qc5_rec_policy_stop(&record_policy,now_ms());else record_policy.continuous=enabled;
 if(record_policy_apply()){record_policy=old;record_policy_apply();return -1;}
 if(record_config_store(record_policy.continuous,record_policy.motion)){
  record_policy=old;record_policy_apply();return -1;
 }
 load_config();return 0;
}
static void record_policy_tick(void){
#ifdef QC5_HOST_TEST
 /* Protocol fault-injection tests control exactly which RPC goes first.
  * Dedicated policy tests enable autonomous startup explicitly. */
 const char*autostart=getenv("QC5_TEST_RECORD_POLICY");if(!record_policy_initialized&&autostart&&!strcmp(autostart,"0"))return;
#endif
 long long now=now_ms();if(now<record_policy_next)return;record_policy_next=now+500;
 if(record_policy_initialize()){record_policy_next=now+3000;return;}
 char text[16];cfg("MOTION_POST_SECONDS",text,sizeof(text));char*end;long value=strtol(text,&end,10);
 record_policy_post=*text&&!*end&&value>=1&&value<=60?value*1000:5000;
 if(record_policy_capable){uint32_t sequence=0;int valid=record_counter(&sequence)==0;now=now_ms();qc5_rec_policy_sample_at(&record_policy,sequence,valid,now,record_ipc.last_ms,record_policy_post);}
 if(record_policy_apply())record_policy_next=now+2000;
}
#endif
