(function(root){
'use strict';
const button=document.getElementById('listen-button'),status=document.getElementById('listen-status'),volume=document.getElementById('listen-volume');
let context=null,gain=null,socket=null,wanted=false,talking=false,next=0,retry=null,silence=null,heard=false,generation=0,action=0,mediaSource=false;const sources=new Set();
function say(text){status.textContent=text;}
function buttonState(active){
 if(button.dataset&&button.dataset.compact==='1'){const label=active?qc5Text("Disattiva audio"):qc5Text("Attiva audio");button.setAttribute('aria-label',label);button.setAttribute('title',label);button.setAttribute('aria-pressed',String(active));}
 else button.textContent=active?qc5Text("Disattiva audio"):qc5Text("Ascolta");
}
function clearAudio(){for(const source of sources){try{source.stop();}catch(error){}}sources.clear();next=0;}
function waitAudio(){clearTimeout(silence);heard=false;if(!wanted||talking)return;say(qc5Text("In attesa dell’audio della camera…"));silence=setTimeout(()=>{if(wanted&&!talking)say(qc5Text("La camera non sta inviando audio"));},3000);}
function audioReceived(){clearTimeout(silence);if(!heard){heard=true;say(qc5Text("Ascolto locale attivo"));}silence=setTimeout(()=>{heard=false;if(wanted&&!talking)say(qc5Text("La camera non sta inviando audio"));},3000);}
function decode(value){value^=85;let n=(value&15)<<4,segment=(value>>4)&7;n=segment===0?n+8:segment===1?n+264:(n+264)<<(segment-1);return (value&128?n:-n)/32768;}
function connect(){
 if(!wanted||socket||talking||mediaSource)return;
 const mine=++generation,url=new URL('/listen',location.href);url.protocol=url.protocol==='https:'?'wss:':'ws:';
 const ws=new WebSocket(url.href);socket=ws;ws.binaryType='arraybuffer';say(qc5Text("Apertura audio locale…"));
 ws.onmessage=e=>{
  if(generation!==mine)return;
  if(typeof e.data==='string'){try{const ready=JSON.parse(e.data);if(ready.ready!==true||ready.codec!=='PCMA')throw Error();waitAudio();}catch(error){ws.close();}return;}
  receive(e.data);
 };
 ws.onclose=()=>{if(socket===ws)socket=null;if(wanted&&generation===mine){say(qc5Text("Ricollegamento audio…"));clearTimeout(retry);retry=setTimeout(connect,1500);}};
 ws.onerror=()=>{};
}
function receive(dataBuffer){
 if(!wanted)return;
  if(Object.prototype.toString.call(dataBuffer)!=='[object ArrayBuffer]'||dataBuffer.byteLength<2||dataBuffer.byteLength>4097){return;}
  if(talking||!context||context.state!=='running')return;
  const data=new Uint8Array(dataBuffer),rate=data[0]===7?8000:data[0]===12?16000:0;if(!rate){return;}
  audioReceived();
  if(next>context.currentTime+.35)clearAudio();
  const buffer=context.createBuffer(1,data.length-1,rate),pcm=buffer.getChannelData(0);for(let i=1;i<data.length;i++)pcm[i-1]=decode(data[i]);
  const source=context.createBufferSource();source.buffer=buffer;source.connect(gain);sources.add(source);source.onended=()=>sources.delete(source);
  next=Math.max(next,context.currentTime+.06);source.start(next);next+=buffer.duration;
}
async function start(){
 wanted=true;const mine=++action;buttonState(true);
 try{if(!context){context=new AudioContext();gain=context.createGain();gain.gain.value=talking?0:Number(volume.value);gain.connect(context.destination);}await context.resume();if(wanted&&mine===action){connect();if(mediaSource){if(talking)say(qc5Text("Audio sospeso mentre parli"));else waitAudio();}}}
 catch(error){if(mine===action){stop();say(qc5Text("Il browser non ha attivato l’audio"));}}
}
function stop(){wanted=false;action++;generation++;clearTimeout(retry);clearTimeout(silence);heard=false;if(socket){socket.onclose=null;socket.close();socket=null;}clearAudio();if(context){context.close().catch(()=>{});context=null;gain=null;}buttonState(false);say(qc5Text("Audio disattivato"));}
function mute(active){talking=active;clearAudio();clearTimeout(silence);heard=false;if(active){generation++;clearTimeout(retry);if(socket){socket.onclose=null;socket.close();socket=null;}}else if(wanted)connect();if(gain)gain.gain.value=active?0:Number(volume.value);if(wanted){if(active)say(qc5Text("Audio sospeso mentre parli"));else waitAudio();}}
button.addEventListener('click',()=>wanted?stop():start());volume.addEventListener('input',()=>{if(gain&&!talking)gain.gain.value=Number(volume.value);});
root.qc5ReceiveAudio=receive;
root.qc5MediaTransport=function(active){mediaSource=active;if(active&&socket){generation++;socket.onclose=null;socket.close();socket=null;clearTimeout(retry);}if(wanted&&!talking)waitAudio();};
root.qc5MuteListen=mute;
root.qc5PrepareListen=function(){mute(true);return start();};
root.addEventListener('message',event=>{if(event.source!==parent||event.origin!==location.origin||!event.data||typeof event.data.qc5TalkActive!=='boolean')return;mute(event.data.qc5TalkActive);});
root.addEventListener('pagehide',stop);document.addEventListener('visibilitychange',()=>{if(document.hidden)stop();});
root.qc5ListenDecode=decode;
})(window);
