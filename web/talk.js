(function(root){
'use strict';
const text = root.qc5Text || (message => message);
class QC5Talk {
 constructor(button,status,secureLink,env=root){
  this.env=env;this.button=button;this.status=status;this.secureLink=secureLink;this.available=false;this.pressed=false;this.generation=0;this.media=null;this.context=null;this.socket=null;this.node=null;this.savedPlayback=[];
  const begin=e=>{if(e.type==='keydown'&&![' ','Enter'].includes(e.key))return;if(e.repeat||e.button>0)return;e.preventDefault();if(e.pointerId!==undefined&&button.setPointerCapture)button.setPointerCapture(e.pointerId);this.start();};
  const end=e=>{if(e.type==='keyup'&&![' ','Enter'].includes(e.key))return;this.stop();};
  button.addEventListener('pointerdown',begin);button.addEventListener('keydown',begin);
  for(const event of ['pointerup','pointercancel','lostpointercapture','keyup'])button.addEventListener(event,end);
  env.addEventListener('blur',()=>this.stop());env.addEventListener('pagehide',()=>this.stop());
  env.document.addEventListener('visibilitychange',()=>{if(env.document.hidden)this.stop();});
  this.update(false);
 }
 say(text){this.status.textContent=text;}
 update(available){
  this.available=!!available;
  const secure=this.env.isSecureContext&&this.env.navigator.mediaDevices&&this.env.navigator.mediaDevices.getUserMedia;
  const supported=secure&&this.env.AudioContext&&this.env.AudioWorkletNode&&this.env.WebSocket;
  this.button.disabled=!this.available||!supported;
  if(!this.available&&!this.pressed)this.say(text("Interfono in attesa della camera"));
  else if(!secure)this.say(text("Per usare il microfono, apri l’indirizzo HTTPS configurato nel tuo reverse proxy."));
  else if(!supported)this.say(text("Il browser non supporta la trasmissione audio richiesta."));
  else if(!this.pressed)this.say(text("Tieni premuto per parlare · massimo 30 secondi"));
  if(this.secureLink)this.secureLink.hidden=true;
 }
 mute(active){
  if(typeof this.env.qc5MuteListen==='function')this.env.qc5MuteListen(active);
  const frame=this.env.document.getElementById('live-frame');if(!frame)return;
  try{
   const doc=frame.contentDocument;
   if(doc){if(active&&!this.savedPlayback.length)this.savedPlayback=Array.from(doc.querySelectorAll('video,audio')).map(el=>[el,el.muted]);
    for(const [el,before] of this.savedPlayback)el.muted=active?true:before;
    if(!active)this.savedPlayback=[];
   }
   const destination=new URL(frame.src||this.env.location.href,this.env.location.href).origin;
   if(destination!=='null'&&frame.contentWindow)frame.contentWindow.postMessage({qc5TalkActive:active},destination);
  }catch(error){/* The local player also accepts the bounded mute message. */}
 }
 async start(){
  if(this.pressed||this.button.disabled)return;
  this.pressed=true;this.congestedAt=0;this.preparationDeadline=this.env.setTimeout(()=>this.stop(text("Avvio interfono non completato")),12000);const generation=++this.generation;const current=()=>this.pressed&&this.generation===generation;
  this.button.setAttribute('aria-pressed','true');this.say(text("Apertura microfono…"));this.mute(true);
  try{if(typeof this.env.qc5PrepareListen==='function')this.env.qc5PrepareListen();const frame=this.env.document.getElementById('live-frame');if(frame&&frame.contentWindow&&typeof frame.contentWindow.qc5PrepareListen==='function')frame.contentWindow.qc5PrepareListen();}catch(error){}
  try{
   const option=this.env.document.getElementById('talk-processing'),processing=!!(option&&option.checked);
   const media=await this.env.navigator.mediaDevices.getUserMedia({audio:{channelCount:1,echoCancellation:processing,noiseSuppression:processing,autoGainControl:false},video:false});
   if(!current()){media.getTracks().forEach(track=>track.stop());return;}this.media=media;
   media.getTracks().forEach(track=>{if(track.addEventListener)track.addEventListener('ended',()=>{if(current())this.stop(text("Microfono disconnesso"));},{once:true});});
   const context=new this.env.AudioContext();this.context=context;await context.resume();
   if(!current())return;
   if(!context.audioWorklet)throw new Error(text("Elaborazione audio non supportata dal browser"));
   await context.audioWorklet.addModule(new URL('talk-worklet.js',this.env.location.href).href);if(!current())return;
   const url=new URL('/intercom',this.env.location.href);url.protocol=url.protocol==='https:'?'wss:':'ws:';
   let shared=null,liveTransport=false;
   try{const frame=this.env.document.getElementById('live-frame');if(frame&&frame.contentWindow&&typeof frame.contentWindow.qc5CreateTalkTransport==='function'){liveTransport=true;shared=frame.contentWindow.qc5CreateTalkTransport();}}catch(error){if(liveTransport)throw error;}
   if(liveTransport&&!shared)throw new Error(text("Attendi il collegamento del video prima di parlare"));
   const socket=shared||new this.env.WebSocket(url.href);this.socket=socket;socket.binaryType='arraybuffer';
   const ready=await new Promise((resolve,reject)=>{
    this.cancelConnect=reject;
    this.connectDeadline=this.env.setTimeout(()=>reject(new Error(text("La camera non risponde all’interfono"))),10000);
    socket.onmessage=e=>{try{const r=JSON.parse(e.data);if(r.ready!==true||r.codec!=='PCMA'||r.sampleRate!==16000||r.frameSamples!==320)throw new Error(text("Formato audio non compatibile"));resolve(r);}catch(error){reject(error);}};
    socket.onerror=()=>reject(new Error(text("Connessione interfono non disponibile")));
    socket.onclose=()=>{reject(new Error(text("Interfono disconnesso")));if(current())this.stop(text("Interfono disconnesso"));};
   });
   this.env.clearTimeout(this.connectDeadline);this.env.clearTimeout(this.preparationDeadline);this.cancelConnect=null;if(!current()){socket.close();return;}
   socket.onmessage=()=>{};
   const node=new this.env.AudioWorkletNode(context,'qc5-capture',{numberOfInputs:1,numberOfOutputs:1,outputChannelCount:[1]});this.node=node;node.onprocessorerror=()=>this.stop(text("Elaborazione audio interrotta"));
   node.port.onmessage=e=>{if(!current()||socket.readyState!==1)return;if(!(e.data instanceof ArrayBuffer)||e.data.byteLength!==320){this.stop(text("Pacchetto audio non valido"));return;}if(socket.bufferedAmount>3200){if(!this.congestedAt)this.congestedAt=Date.now();if(Date.now()-this.congestedAt>900)this.stop(text("Rete lenta: trasmissione interrotta"));return;}this.congestedAt=0;socket.send(e.data);};
   this.source=context.createMediaStreamSource(media);
   if(processing){
    this.filter=context.createBiquadFilter();this.filter.type='highpass';this.filter.frequency.value=90;this.filter.Q.value=.707;
    this.source.connect(this.filter);this.filter.connect(node);
   }else this.source.connect(node);
   node.connect(context.destination);this.mute(true);
   this.say(text("Stai parlando · rilascia per ascoltare"));
   this.limit=this.env.setTimeout(()=>this.stop(text("Limite di 30 secondi raggiunto")),Math.min(30,ready.maxSeconds||30)*1000);
  }catch(error){if(current())this.stop(error&&error.name==='NotAllowedError'?text("Accesso al microfono non consentito"):(error.message||text("Microfono non disponibile")));}
 }
 stop(message){
  if(!this.pressed&&!this.media&&!this.socket&&!this.context)return;
  this.pressed=false;const generation=++this.generation;this.env.clearTimeout(this.limit);this.env.clearTimeout(this.connectDeadline);this.env.clearTimeout(this.preparationDeadline);
  if(this.cancelConnect){const cancel=this.cancelConnect;this.cancelConnect=null;cancel(new Error(text("Trasmissione annullata")));}
  if(this.node){this.node.port.onmessage=null;this.node.port.postMessage('stop');this.node.disconnect();this.node=null;}
  if(this.source){this.source.disconnect();this.source=null;}
  if(this.filter){this.filter.disconnect();this.filter=null;}
  if(this.compressor){this.compressor.disconnect();this.compressor=null;}
  if(this.media){this.media.getTracks().forEach(track=>track.stop());this.media=null;}
  if(this.socket){this.socket.onclose=this.socket.onerror=this.socket.onmessage=null;try{this.socket.close(1000);}catch(error){}this.socket=null;}
  if(this.context){const context=this.context;this.context=null;context.close().catch(()=>{});}
  this.button.setAttribute('aria-pressed','false');this.say(message||text("Tieni premuto per parlare · massimo 30 secondi"));
  this.env.setTimeout(()=>{if(this.generation===generation&&!this.pressed)this.mute(false);},700);
 }
}
root.QC5Talk=QC5Talk;
if(root.document){const button=root.document.getElementById('talk-button');if(button)root.qc5TalkController=new QC5Talk(button,root.document.getElementById('talk-status'),root.document.getElementById('talk-secure-link'));}
})(typeof window!=='undefined'?window:globalThis);
