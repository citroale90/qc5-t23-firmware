#ifndef QC5_RECORD_CONFIG_H
#define QC5_RECORD_CONFIG_H
/* Store user choices on SD only. Detector edges never call this function. */
static int record_config_key(const char*s,size_t n){
 while(n&&qc5_space(*s)){s++;n--;}size_t k=0;while(k<n&&s[k]!='=')k++;
 if(k==n)return 0;while(k&&qc5_space(s[k-1]))k--;
 return (k==17&&!memcmp(s,"RECORD_CONTINUOUS",17))||(k==13&&!memcmp(s,"MOTION_ENABLE",13));
}
static int record_config_write(int dir,const char*name,const char*data,size_t n){
 int f=openat(dir,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return -1;
 size_t at=0;int bad=0;while(at<n){ssize_t k=write(f,data+at,n-at);if(k<0&&errno==EINTR)continue;if(k<=0){bad=1;break;}at+=(size_t)k;}
 if(!bad&&fsync(f))bad=1;int error=errno;if(close(f)&&!bad){bad=1;error=errno;}
 if(bad){unlinkat(dir,name,0);errno=error;return -1;}return 0;
}
static int record_config_store(int continuous,int motion){
 char expected[600];int k=snprintf(expected,sizeof(expected),"%s/config.txt",sdroot);
 if(k<0||(size_t)k>=sizeof(expected)||strcmp(expected,config_path)||!qc5_sd_mounted(sdroot,1)){errno=ENODEV;return -1;}
 int dir=open(sdroot,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return -1;
 char old[8193],next[8193],nonce[17],fresh[64],backup[64],check[8];long n=0;int result=-1;
 int fd=openat(dir,"config.txt",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
 if(fd<0)goto done;
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size>8192){close(fd);errno=EINVAL;goto done;}
 n=qc5_config_read_fd(fd,old,sizeof(old));close(fd);if(n<0||n>8192)goto done;old[n]=0;
 if(qc5_config_get(old,(unsigned long)n,"_SYNTAX",check,sizeof(check))<0){errno=EINVAL;goto done;}
 char a[8],b[8];
 if(qc5_config_get(old,(unsigned long)n,"RECORD_CONTINUOUS",a,sizeof(a))>=0&&qc5_config_get(old,(unsigned long)n,"MOTION_ENABLE",b,sizeof(b))>=0&&a[0]=='0'+continuous&&!a[1]&&b[0]=='0'+motion&&!b[1]){result=0;goto done;}
 size_t used=0,at=0;
 if(n>=3&&!memcmp(old,"\xef\xbb\xbf",3)){memcpy(next,old,3);used=at=3;}
 while(at<(size_t)n){size_t start=at;while(at<(size_t)n&&old[at]!='\n')at++;if(at<(size_t)n)at++;
  if(!record_config_key(old+start,at-start)){if(used+at-start>=sizeof(next)){errno=EFBIG;goto done;}memcpy(next+used,old+start,at-start);used+=at-start;}
 }
 if(used&&next[used-1]!='\n'){if(used+1>=sizeof(next)){errno=EFBIG;goto done;}next[used++]='\n';}
 k=snprintf(next+used,sizeof(next)-used,"RECORD_CONTINUOUS=%d\nMOTION_ENABLE=%d\n",continuous,motion);
 if(k<0||(size_t)k>=sizeof(next)-used){errno=EFBIG;goto done;}used+=(size_t)k;
 if(qc5_random_hex(nonce))goto done;
 snprintf(fresh,sizeof(fresh),".qc5-record-new-%s",nonce);snprintf(backup,sizeof(backup),".qc5-record-old-%s",nonce);
 if(record_config_write(dir,fresh,next,used))goto done;
 if(record_config_write(dir,backup,old,(size_t)n)||renameat(dir,backup,dir,".qc5-record-config.previous")){int error=errno;unlinkat(dir,fresh,0);unlinkat(dir,backup,0);errno=error;goto done;}
 if(renameat(dir,fresh,dir,"config.txt")){int error=errno;unlinkat(dir,fresh,0);errno=error;goto done;}
 result=fsync(dir);
 done:{int error=errno;memset(old,0,sizeof(old));memset(next,0,sizeof(next));close(dir);errno=error;return result;}
}
#endif
