(function(){
'use strict';
const video=document.getElementById('video'),message=document.getElementById('message');
let socket=null,media=null,buffer=null,url=null,queue=[],bytes=0,generation=0,timer=null,health=null,closed=false,lastProgress=performance.now(),lastTime=0,retry=0,duplex=false,voice=null,talkSerial=0;
function parentCall(name,value){try{if(typeof window[name]==='function')window[name](value);else if(parent!==window&&typeof parent[name]==='function')parent[name](value);}catch(error){}}
function state(text,visible=true){message.textContent=text;message.hidden=!visible;try{const label=parent.document.getElementById('video-status');if(label)label.textContent=text;}catch(error){}}
function talkCommand(active,id){const data=new Uint8Array(12);data.set([81,67,84,49,active?1:0]);for(let i=0;i<4;i++)data[8+i]=(id>>>(8*i))&255;return data.buffer;}
function endVoice(reason){
 const active=voice;voice=null;if(active){active.readyState=3;if(active.onclose)active.onclose({reason:reason||'media_closed'});}
}
window.qc5CreateTalkTransport=function(){
 if(!duplex||!socket||socket.readyState!==1||voice||closed)return null;
 const ws=socket,mine=generation;talkSerial=(talkSerial+1)>>>0;if(!talkSerial)talkSerial=1;const id=talkSerial;
 const transport={readyState:1,onmessage:null,onclose:null,onerror:null,
  send(data){if(this.readyState!==1||voice!==this||ws!==socket||mine!==generation)throw Error(qc5Text("Interfono disconnesso"));if(Object.prototype.toString.call(data)!=='[object ArrayBuffer]'||data.byteLength!==320)throw Error(qc5Text("Pacchetto audio non valido"));ws.send(data);},
  close(){if(this.readyState!==1)return;this.readyState=3;if(voice===this)voice=null;if(ws===socket&&mine===generation&&ws.readyState===1)ws.send(talkCommand(false,id));}
 };
 Object.defineProperty(transport,'bufferedAmount',{get(){return ws.bufferedAmount||0;}});
 transport.talkId=id;voice=transport;ws.send(talkCommand(true,id));return transport;
};
function cleanup(releaseAudio=false){
 generation++;duplex=false;parentCall('qc5LiveIntercomReady',false);endVoice();clearTimeout(timer);clearInterval(health);
 if(socket){socket.onclose=socket.onerror=socket.onmessage=null;try{socket.close();}catch(error){}socket=null;}
 /* The live player keeps ownership while reconnecting. Releasing it here
  * lets a pending PTT release open /listen just before the next /media. */
 parentCall('qc5MediaTransport',!releaseAudio);
 queue=[];bytes=0;buffer=null;media=null;video.removeAttribute('src');video.load();if(url){URL.revokeObjectURL(url);url=null;}
}
function recover(text){if(closed)return;cleanup();state(text||qc5Text("Ricollegamento video…"));timer=setTimeout(connect,Math.min(5000,750+retry++*500));}
function append(){
 if(!buffer||buffer.updating||closed)return;
 try{
  if(buffer.buffered.length&&video.currentTime-buffer.buffered.start(0)>12){buffer.remove(buffer.buffered.start(0),video.currentTime-8);return;}
  if(queue.length){const data=queue.shift();bytes-=data.byteLength;buffer.appendBuffer(data);}
 }catch(error){recover(qc5Text("Ripristino del lettore video…"));}
}
function connect(){
 if(closed)return;cleanup();const mine=generation;
 if(!window.MediaSource){parentCall('qc5MediaTransport',false);state(qc5Text("Questo browser non supporta il video locale H.264"));return;}
 parentCall('qc5MediaTransport',true);media=new MediaSource();url=URL.createObjectURL(media);video.muted=true;video.src=url;lastProgress=performance.now();lastTime=0;
 media.addEventListener('sourceopen',()=>{
  if(mine!==generation)return;
  const endpoint=new URL('/media',location.href);endpoint.protocol=endpoint.protocol==='https:'?'wss:':'ws:';
  const ws=new WebSocket(endpoint.href);socket=ws;ws.binaryType='arraybuffer';
  ws.onmessage=event=>{
   if(mine!==generation)return;
   try{
    if(typeof event.data==='string'){
     const info=JSON.parse(event.data);
     if(info.ready===true&&info.duplex===true){duplex=true;parentCall('qc5LiveIntercomReady',true);}
     if(info.talk===true&&voice&&info.talkId===voice.talkId){if(voice.onmessage)voice.onmessage({data:event.data});return;}
     if(info.talk===false&&voice&&info.talkId===voice.talkId){endVoice(info.reason);return;}
     if(info.mime){if(buffer||!/^video\/mp4; codecs="avc[13]\.[0-9a-f]{6}"$/.test(info.mime)||!MediaSource.isTypeSupported(info.mime))throw Error('codec');buffer=media.addSourceBuffer(info.mime);buffer.addEventListener('updateend',()=>{if(mine===generation)append();});buffer.addEventListener('error',()=>{if(mine===generation)recover();});}
     return;
    }
    if(!(event.data instanceof ArrayBuffer)||event.data.byteLength<2||event.data.byteLength>4098)throw Error('packet');
    const data=new Uint8Array(event.data);
    if(data[0]===1){parentCall('qc5ReceiveAudio',data.slice(1).buffer);return;}
    if(data[0]!==0||!buffer)throw Error('video');
    const part=data.slice(1);bytes+=part.byteLength;if(bytes>2*1024*1024)throw Error('backpressure');queue.push(part);append();
   }catch(error){recover(qc5Text("Ricollegamento del flusso video…"));}
  };
  ws.onclose=()=>{if(mine===generation)recover();};ws.onerror=()=>{};
 },{once:true});
 video.play().catch(()=>{});
 health=setInterval(()=>{
  if(mine!==generation||document.hidden)return;
  const now=performance.now();
  if(video.readyState>=2&&video.videoWidth>0&&video.currentTime>lastTime+.01){lastTime=video.currentTime;lastProgress=now;retry=0;state(qc5Text("Video dal vivo"),false);}
  if(buffer&&buffer.buffered.length&&!buffer.updating){const end=buffer.buffered.end(buffer.buffered.length-1);if(end-video.currentTime>1.2||video.currentTime<buffer.buffered.start(0)){video.currentTime=Math.max(buffer.buffered.start(buffer.buffered.length-1),end-.25);video.play().catch(()=>{});}}
  if(video.paused&&video.readyState>=2)video.play().catch(()=>{});
  if(now-lastProgress>12000)recover();
 },500);
}
window.addEventListener('pagehide',()=>{closed=true;cleanup(true);});
window.addEventListener('pageshow',()=>{if(closed){closed=false;connect();}});
connect();
})();
