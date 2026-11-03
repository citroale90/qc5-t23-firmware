/* Native detector and event recorder. No snapshots, decoder or NAS detector.
 * The vendor calls detector mode/schedule "motion_push"; notification producers
 * must be disabled before we may use that detector for local recordings. */
#include "qc5_native_guard.h"
struct motion_state {int armed,sensitivity,continuous,record,detector,record_plan[21],detector_plan[21];};
static int motion_read(struct motion_state*s){
 if(camera_rpc("get_params","",3000)||record_integer((char*)response+8,"alarm_motion_armed",&s->armed)||record_integer((char*)response+8,"alarm_motion_sensitivity",&s->sensitivity)||s->armed<0||s->armed>1||s->sensitivity<0||s->sensitivity>255)return -1;
 int values[21];
 return record_native_plan(1,&s->record,s->record_plan)||record_native_plan(2,&s->detector,s->detector_plan)||record_plan(&s->continuous,values)?-1:0;
}
static int motion_custom(const struct motion_state*s){
 if(s->detector!=0&&s->detector!=1)return 1;
 for(int i=0;i<21;i++)if((s->record_plan[i]!=-1&&s->record_plan[i]!=0)||(s->detector_plan[i]!=-1&&s->detector_plan[i]!=0))return 1;
 return 0;
}
static void motion_status(int fd){
 struct motion_state s;
 if(motion_read(&s)){result(fd,0,"Rilevatore nativo non raggiungibile");return;}
 char b[800];int enabled=record_policy_initialized?record_policy.motion:s.detector==1&&s.armed;
 int event_record=record_policy_capable?enabled:s.record;
 int n=snprintf(b,sizeof(b),"{\"ok\":true,\"token\":\"%s\",\"motion\":{\"local_only\":%s,\"enabled\":%s,\"detector_mode\":%d,\"event_recording\":%s,\"auto_stop\":%s,\"post_seconds\":%lld,\"suppressed\":%s,\"sensitivity\":%d,\"continuous\":%s,\"custom_schedule\":%s}}",record_token,native_motion_local_only()?"true":"false",enabled?"true":"false",s.detector,event_record?"true":"false",record_policy_capable?"true":"false",record_policy_post/1000,record_policy.inhibit?"true":"false",s.sensitivity,(record_policy_initialized?record_policy.continuous:s.continuous)?"true":"false",motion_custom(&s)?"true":"false");
 http_reply(fd,200,"application/json",b,(size_t)n);
}
static int motion_set_plan(int type,int enabled,const int values[21]){
 const char*prefix=type==1?"motion_record":"motion_push";
 char params[900];size_t used=(size_t)snprintf(params,sizeof(params),"cmd=2017&command=%d&%s_plan_enable=%d",type,prefix,enabled);
 for(int i=0;i<21;i++)used+=(size_t)snprintf(params+used,sizeof(params)-used,"&%s_plan%d=%d",prefix,i+1,values[i]);
 int status=-1,actual=-1,readback[21];
 if(camera_rpc("trans_cmd_string",params,3000)||field_int((char*)response+8,"status",&status)||status||record_native_plan(type,&actual,readback)||actual!=enabled)return -1;
 return memcmp(values,readback,sizeof(readback))?-1:0;
}
static int motion_set_detector(int armed,int sensitivity){
 char params[80];snprintf(params,sizeof(params),"motion_armed=%d&motion_sensitivity=%d",armed,sensitivity);int result_code=-1,actual=-1,sens=-1;
 if(camera_rpc("set_alarm",params,3000)||field_int((char*)response+8,"result",&result_code)||result_code)return -1;
 /* Detector startup can lose the first readback after the acknowledged write.
  * Retry only that read, once; camera_rpc opens a fresh session on timeout.
  * Never repeat set_alarm or accept an unconfirmed/mismatching setting. */
 int readback=camera_rpc("get_params","",3000);
 if(readback)readback=camera_rpc("get_params","",3000);
 if(readback||record_integer((char*)response+8,"alarm_motion_armed",&actual)||record_integer((char*)response+8,"alarm_motion_sensitivity",&sens))return -1;
 return actual==armed&&sens==sensitivity?0:-1;
}
static void motion_change(int fd,const char*body){
 char token[33],enabled[3];
 if(query(body,"token",token,sizeof(token))!=1||strcmp(token,record_token)){http_reply(fd,403,"text/plain","Riapri la pagina",16);return;}
 if(query(body,"enabled",enabled,sizeof(enabled))!=1||(strcmp(enabled,"0")&&strcmp(enabled,"1"))){result(fd,0,"Impostazioni movimento non valide");return;}
 int enable=enabled[0]-'0';
 if(!native_motion_local_only()){result(fd,0,"Attivazione disponibile con il programma locale privo delle notifiche cloud");return;}
 if(record_policy_initialize()){result(fd,0,"Controllo del registratore non pronto");return;}
 struct qc5_rec_policy_state old_policy=record_policy;
 struct motion_state old,after;
 if(motion_read(&old)){result(fd,0,"Stato movimento non leggibile");return;}
 if(motion_custom(&old)){result(fd,0,"Piano personalizzato o rilevamento persone presente: modifica annullata");return;}
 /* Keep the native sensitivity until its scale is verified on real events. */
 int sens=old.sensitivity;
 int values[21];for(int i=0;i<21;i++)values[i]=-1;
 int record_enable=record_policy_capable?0:enable;
 int ok=0,step=1;
 do{
  if(motion_set_plan(1,record_enable,values))break;step=2;
  if(motion_set_plan(2,enable,values))break;step=3;
  if(motion_set_detector(enable,sens))break;step=4;
  if(motion_read(&after))break;step=5;
  if(after.record!=record_enable||after.detector!=enable||after.armed!=enable||after.sensitivity!=sens||after.continuous!=old.continuous)break;
  record_policy.motion=enable;if(!enable)record_policy.event_active=0;
  if(record_policy_capable){uint32_t sequence;if(!record_counter(&sequence)){record_policy.sequence=sequence;record_policy.seeded=1;}}
  step=6;if(record_policy_apply())break;step=7;
  if(record_config_store(record_policy.continuous,record_policy.motion))break;
  ok=1;
 }while(0);
 if(!ok){
  fprintf(stderr,"QC5_MOTION_APPLY_FAILED step=%d errno=%d\n",step,errno);
  record_policy=old_policy;
  /* Native settings are not transactional. Restore every field, even when an
   * earlier command timed out after being applied. Report unconfirmed rollback. */
  int restored=0;
  int a=motion_set_plan(2,old.detector,old.detector_plan),b=motion_set_plan(1,old.record,old.record_plan),c=motion_set_detector(old.armed,old.sensitivity);
  int d=record_policy_apply(),e=record_config_store(old_policy.continuous,old_policy.motion);
  restored=!a&&!b&&!c&&!d&&!e;
  result(fd,0,restored?"Modifica non confermata: impostazioni precedenti ripristinate":"Modifica incompleta: ricontrolla lo stato del movimento");return;
 }
 load_config();
 result(fd,1,enable?"Rilevamento e registrazione su movimento abilitati nella camera":"Registrazione su movimento disabilitata");
}
static void motion_post(int fd,const char*headers,char*body,size_t available,long long deadline){
 static const char*const keys[]={"token","enabled"};
 if(control_form(fd,headers,body,available,deadline,keys,2,3))motion_change(fd,body);
}
