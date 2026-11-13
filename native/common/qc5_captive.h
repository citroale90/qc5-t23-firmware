/* AP-only HTTP and DNS, in the existing gateway process. No router rules. */
static int captive_http=-1,captive_dns=-1;
static unsigned captive_http_port=80,captive_dns_port=53;
static long long captive_retry;
static const char*captive_address="192.168.168.1";
static unsigned captive_main_port;
static in_addr_t captive_main_address;
static int captive_active,captive_shared;
static void captive_main_listener(const char*address,unsigned port){
 struct in_addr a;captive_main_port=port;
 captive_main_address=inet_pton(AF_INET,address,&a)==1?a.s_addr:INADDR_NONE;
}
static int captive_main_client(int fd){
 if(!captive_active||!captive_shared)return 0;
 struct sockaddr_in local;struct in_addr ap;socklen_t n=sizeof(local);
 return !getsockname(fd,(struct sockaddr*)&local,&n)&&n>=sizeof(local)&&local.sin_family==AF_INET&&
  local.sin_port==htons((uint16_t)captive_http_port)&&inet_pton(AF_INET,captive_address,&ap)==1&&local.sin_addr.s_addr==ap.s_addr;
}
static int captive_socket(int datagram,unsigned port){
 int fd=socket(AF_INET,datagram?SOCK_DGRAM:SOCK_STREAM,0);if(fd<0)return -1;
 int one=1,capacity=8192;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
 setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&capacity,sizeof(capacity));setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&capacity,sizeof(capacity));
 struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons((uint16_t)port);
 if(fcntl(fd,F_SETFL,O_NONBLOCK)||inet_pton(AF_INET,captive_address,&a.sin_addr)!=1||bind(fd,(struct sockaddr*)&a,sizeof(a))||(!datagram&&listen(fd,4))){fprintf(stderr,"QC5_CAPTIVE_BIND_FAILED type=%s errno=%d\n",datagram?"dns":"http",errno);close(fd);return -1;}
 return fd;
}
static void captive_close(void){if(captive_http>=0)close(captive_http);if(captive_dns>=0)close(captive_dns);captive_http=captive_dns=-1;captive_active=captive_shared=0;}
static void captive_update(int active){
#ifdef QC5_HOST_TEST
 const char*h=getenv("QC5_TEST_CAPTIVE_HTTP_PORT"),*d=getenv("QC5_TEST_CAPTIVE_DNS_PORT");
 if(!h||!d)active=0;else{captive_http_port=(unsigned)strtoul(h,NULL,10);captive_dns_port=(unsigned)strtoul(d,NULL,10);captive_address="127.0.0.1";if(!captive_http_port||captive_http_port>65535||!captive_dns_port||captive_dns_port>65535)active=0;}
#endif
 if(!active){captive_close();return;}
 struct in_addr ap;
 captive_active=1;captive_shared=captive_main_port==captive_http_port&&inet_pton(AF_INET,captive_address,&ap)==1&&
  (captive_main_address==htonl(INADDR_ANY)||captive_main_address==ap.s_addr);
 /* With the WebUI on port 80, its existing wildcard listener also owns the
  * AP address. Route by accepted socket's LOCAL address, never Host headers.
  * A second AP bind would fail and break OS captive-portal detection. */
 if(captive_shared&&captive_http>=0){close(captive_http);captive_http=-1;}
 if(now_ms()<captive_retry)return;captive_retry=now_ms()+3000;
 if(!captive_shared&&captive_http<0){captive_http=captive_socket(0,captive_http_port);if(captive_http>=0)fprintf(stderr,"QC5_CAPTIVE_HTTP_READY port=%u\n",captive_http_port);}
 if(captive_dns<0){captive_dns=captive_socket(1,captive_dns_port);if(captive_dns>=0)fprintf(stderr,"QC5_CAPTIVE_DNS_READY port=%u\n",captive_dns_port);}
}
static size_t captive_dns_reply(const unsigned char*q,size_t n,unsigned char*out,size_t cap){
 if(n<17||n>512||cap<528||(q[2]&0xf8)||q[4]||q[5]!=1)return 0;
 size_t at=12,labels=0;
 while(at<n&&q[at]){unsigned length=q[at++];if(length>63||at+length>=n||(labels+=length+1)>254)return 0;at+=length;}
 if(at>=n||at+5>n)return 0;at++;
 unsigned type=(unsigned)q[at]*256+q[at+1],class_id=(unsigned)q[at+2]*256+q[at+3];at+=4;
 if(class_id!=1)return 0;
 memcpy(out,q,at);out[2]=(unsigned char)(0x84|(q[2]&1));out[3]=0x80;out[6]=out[7]=out[8]=out[9]=out[10]=out[11]=0;
 if(type==1||type==255){
  static const unsigned char record[]={0xc0,0x0c,0,1,0,1,0,0,0,0,0,4,192,168,168,1};
  if(at+sizeof(record)>cap)return 0;memcpy(out+at,record,sizeof(record));at+=sizeof(record);out[7]=1;
 }
 return at;
}
static void captive_dns_client(void){
 unsigned char query_data[512],answer[600];struct sockaddr_in peer;socklen_t length=sizeof(peer);
 ssize_t n=recvfrom(captive_dns,query_data,sizeof(query_data),MSG_DONTWAIT,(struct sockaddr*)&peer,&length);if(n<=0)return;
 size_t count=captive_dns_reply(query_data,(size_t)n,answer,sizeof(answer));
 if(count)sendto(captive_dns,answer,count,MSG_DONTWAIT,(struct sockaddr*)&peer,length);
}
static void captive_redirect(int fd){
 char header[320],location[100];
 if(captive_http_port==80)snprintf(location,sizeof(location),"http://%s/setup",captive_address);
 else snprintf(location,sizeof(location),"http://%s:%u/setup",captive_address,captive_http_port);
 int n=snprintf(header,sizeof(header),"HTTP/1.1 302 Found\r\nLocation: %s\r\nContent-Length: 0\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",location);
 if(n>0&&(size_t)n<sizeof(header))send_all(fd,header,(size_t)n);
}
