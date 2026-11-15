/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QC5_HOSTNAME_HTTP_H
#define QC5_HOSTNAME_HTTP_H
static int qhh_value(const char *file,char out[64])
{
 char line[66];int n=qnf_read(portal_directory,file,line,sizeof line);
 out[0]=0;if(n<1||line[n-1]!='\n')return 0;line[n-1]=0;
 return qc5_hostname_normalize(out,line)&&!strcmp(out,line);
}
static void qhh_reply(int fd,int code,const char *message)
{http_reply(fd,code,"application/json",message,strlen(message));}
static void qhh_handle(int fd,const char *method,const char *headers,char *body,size_t available,long long deadline)
{
 if(!strcmp(method,"GET")){
  char current[64]={0},custom[64]={0},state[64]="unavailable",line[66];
  if(!qhh_value("hostname-current",current)){gethostname(current,sizeof current-1);current[63]=0;if(!qc5_hostname_valid(current))strcpy(current,"unknown");}
  (void)qhh_value("hostname-custom",custom);
  int n=qnf_read(portal_directory,"hostname-state",line,sizeof line);
  if(n>1&&(size_t)n<=sizeof state&&line[n-1]=='\n'){line[n-1]=0;if(strspn(line,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_")==strlen(line))strcpy(state,line);}
  char response[384];int used=snprintf(response,sizeof response,"{\"ok\":true,\"current\":\"%s\",\"configured\":\"%s\",\"state\":\"%s\",\"token\":\"%s\"}",current,custom,state,provision_token);
  if(used>0&&(size_t)used<sizeof response)http_reply(fd,200,"application/json",response,used);return;
 }
 if(strcmp(method,"POST")){qhh_reply(fd,405,"{\"ok\":false,\"error\":\"method\"}");return;}
 char form[257],input[64],name[64],request[96];int ok=available<=256;
 if(ok){memcpy(form,body,available);ok=qfh_form(fd,headers,form,available,deadline);}
 if(ok){unsigned fields=0;for(char *at=form;*at;){char *end=strchr(at,'&');size_t n=end?(size_t)(end-at):strlen(at);
   if(!((n>=9&&!memcmp(at,"hostname=",9))||(n>=6&&!memcmp(at,"token=",6)))){ok=0;break;}
   fields++;if(!end)break;if(!end[1]){ok=0;break;}at=end+1;
  }ok=ok&&fields==2&&query(form,"hostname",input,sizeof input)==1&&qc5_hostname_normalize(name,input);}
 if(!ok){qhh_reply(fd,400,"{\"ok\":false,\"error\":\"invalid_hostname_or_form\"}");return;}
 struct stat st;char state[64];int pending=qnf_read(portal_directory,"hostname-state",state,sizeof state)>0&&(!strcmp(state,"queued\n")||!strcmp(state,"applying\n"));
 if(qnf_busy(portal_directory)||pending||!fstatat(portal_directory,"hostname-request",&st,AT_SYMLINK_NOFOLLOW)){qhh_reply(fd,409,"{\"ok\":false,\"error\":\"busy\"}");return;}
 snprintf(request,sizeof request,"HOSTNAME=\"%s\"\n",name);
 if(!qnf_text(portal_directory,"hostname-state","queued\n")||!qnf_text(portal_directory,"hostname-request",request)){
  (void)qnf_text(portal_directory,"hostname-state","queue_failed\n");qhh_reply(fd,503,"{\"ok\":false,\"error\":\"queue_failed\"}");return;
 }
 qhh_reply(fd,202,"{\"ok\":true,\"queued\":true}");
}
#endif
