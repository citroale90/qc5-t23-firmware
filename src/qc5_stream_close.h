#ifndef QC5_STREAM_CLOSE_H
#define QC5_STREAM_CLOSE_H
/* A native stop reply shares the reliable UDP transport with frames already
 * in flight. Keep acknowledging them while discarding their payload: the
 * browser may be gone and the frame parser may have stopped with an error.
 * Do not reconnect, parse frames, allocate buffers or forward to a closed FD.
 */
static unsigned stream_close_drained;
static int stream_close_packet(int n,const struct sockaddr_in*source){
 if(n<8||!connected||source->sin_addr.s_addr!=peer.sin_addr.s_addr||source->sin_port!=peer.sin_port||wire[1]!=0xd0||!wire[5])return 0;
 unsigned char ack[6]={0xd1,wire[5],0,1,wire[6],wire[7]};
 if(!packet(0xd1,ack,sizeof(ack),&peer))stream_close_drained++;
 return 0;
}
static void native_stream_release(int live){
 stream_close_drained=0;
 if(live&&!stopping&&connected){
  media_packet=stream_close_packet;long long start=now_ms();
  int result=-1,failed=camera_rpc("livestream","streamid=16",500);
  if(!failed&&(field_int((char*)response+8,"result",&result)||result))failed=1;
  fprintf(stderr,"QC5_STREAM_STOP result=%s elapsed_ms=%lld drained=%u\n",failed?"failed":"confirmed",now_ms()-start,stream_close_drained);
 }
 media_packet=NULL;disconnect_camera();
}
#endif
