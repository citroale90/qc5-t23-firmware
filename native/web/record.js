/* Native camera recorder. Same origin; no NAS service or serial/IP setting. */
var recActive=false,recEnabled=false,recToken='',recPollBusy=false,playbackId=0,playbackRequest=null,playbackUrl=null,playbackAudio=null;
function recReleaseDownload(){if(playbackAudio){playbackAudio.close();playbackAudio=null;}if(playbackRequest){playbackRequest.abort();playbackRequest=null}if(playbackUrl){URL.revokeObjectURL(playbackUrl);playbackUrl=null}}
function cameraControlRequest(path,form,cb,timeout){
 var xhr=new XMLHttpRequest();xhr.open(form?'POST':'GET',path,true);xhr.timeout=timeout||15000;
 if(form)xhr.setRequestHeader('Content-Type','application/x-www-form-urlencoded');
 xhr.onload=function(){try{cb(JSON.parse(xhr.responseText))}catch(e){cb({ok:false,result:qc5Text("Richiesta non riuscita: ricarica la pagina")})}};
 xhr.onerror=xhr.ontimeout=function(){cb({ok:false,result:qc5Text("Camera non raggiungibile")})};xhr.send(form||null);
}
function recRequest(path,form,cb){cameraControlRequest('/api/rec/'+path,form,cb)}
function recCheckStatus(){
 if(recPollBusy)return;recPollBusy=true;
 recRequest('status',null,function(d){
  recPollBusy=false;if(!d.ok||!d.status){recToken='';document.getElementById('rec-stop-now').disabled=true;document.getElementById('record-status').textContent=qc5Text(d.result)||qc5Text("Stato non disponibile");return;}
  recToken=d.token||'';var s=d.status;recActive=!!s.recording;recEnabled=!!s.enabled;
  document.getElementById('rec-dot').classList.toggle('active',recActive);
  var button=document.getElementById('rec-btn');button.textContent=recEnabled?qc5Text("■ Ferma registrazione continua"):qc5Text("● Registra continuamente");
  button.disabled=recBusy||!!s.custom_schedule||!s.sd_ready;
  var stop=document.getElementById('rec-stop-now');stop.hidden=!(recActive||recEnabled||s.mode==='motion');stop.disabled=recBusy||!!s.custom_schedule||!recToken;
  document.getElementById('rec-time').textContent=s.mode==='motion'?qc5Text("Registrazione su movimento"):recActive?qc5Text("In registrazione"):recEnabled?qc5Text("In attesa del registratore"):'';
  var destination={sd:'SD',smb:'Samba',ftp:'FTP'}[s.target]||'SD';
  document.getElementById('record-status').textContent=(s.custom_schedule?qc5Text("Piano personalizzato conservato. "):recEnabled?qc5Text("Registrazione continua abilitata. "):s.mode==='motion'?qc5Text("Registrazione su movimento in corso. "):s.motion_suppressed?qc5Text("Evento fermato: in attesa che la scena torni ferma. "):qc5Text("Registrazione continua disabilitata. "))+(s.sd_ready?qc5Text("SD: {free} MB liberi su {total} MB. ", {free: s.free_mb==null?"—":(s.space_estimated?"≈":"")+s.free_mb, total: s.total_mb}):qc5Text("SD non pronta. "))+qc5Text("Durata massima dei segmenti: {minutes} minuti. Destinazione: {destination}.", {minutes: s.segment_minutes, destination});
  if(s.failed)document.getElementById('record-status').textContent=qc5Text("Registrazione non riuscita: controlla la SD.")+' '+document.getElementById('record-status').textContent;
  if(s.target!=='sd'){var phases={idle:qc5Text("Trasferimenti aggiornati"),transferring:qc5Text("Invio del segmento al NAS"),retry:qc5Text("Trasferimento non riuscito: nuovo tentativo automatico"),waiting_memory:qc5Text("In attesa di memoria libera"),waiting_sd:qc5Text("In attesa della SD"),waiting_clock:qc5Text("Ora della camera da impostare"),waiting_recordings:qc5Text("In attesa di un video completato"),waiting:qc5Text("Avvio del trasferimento")};var e=s.exporter||{};document.getElementById('record-status').textContent+=' '+(e.available?(phases[e.phase]||qc5Text("Trasferimento in attesa")):qc5Text("Trasferimento remoto non attivo in questa revisione"))+qc5Text(". I file restano sulla SD.");}
 });
}
function recToggle(){
 if(recBusy||!recToken)return;recBusy=true;document.getElementById('rec-btn').disabled=true;
 recRequest(recEnabled?'stop':'start','token='+encodeURIComponent(recToken),function(d){recBusy=false;toast(qc5Text(d.result)||qc5Text("Esito non disponibile"),d.ok?'ok':'err');recCheckStatus();recLoadList()});
}
function recStopNow(){
 if(recBusy||!recToken)return;recBusy=true;document.getElementById('rec-stop-now').disabled=true;
 recRequest('stop-now','token='+encodeURIComponent(recToken),function(d){recBusy=false;toast(qc5Text(d.result)||qc5Text("Esito non disponibile"),d.ok?'ok':'err');recCheckStatus();motionCheckStatus();recLoadList()});
}
function recLoadList(){
 recRequest('list',null,function(d){
  var el=document.getElementById('rec-list');el.replaceChildren();
  if(!d.ok){el.textContent=qc5Text(d.result)||qc5Text("Elenco non disponibile");return;}
  if(!d.files.length){el.textContent=qc5Text("Nessun video completato. Il segmento in corso comparirà alla chiusura.");return;}
  d.files.forEach(function(file){
   var row=document.createElement('div');row.className='rec-item';
   var text=document.createElement('span');text.className='rec-info';text.textContent=file.name;row.appendChild(text);
   var size=document.createElement('span');size.className='rec-size';size.textContent=file.size_mb+' MB';row.appendChild(size);
   var play=document.createElement('button');play.className='btn';play.textContent='▶';play.title=qc5Text("Riproduci");play.onclick=function(){recPlay(file.name,file.size_bytes)};row.appendChild(play);
   var link=document.createElement('a');link.className='btn';link.textContent=qc5Text("Scarica");link.href='/api/rec/download?file='+encodeURIComponent(file.name);link.download=file.name.replace('/','_');row.appendChild(link);
   var del=document.createElement('button');del.className='btn off';del.textContent=qc5Text("Elimina");del.onclick=function(){recDel(file.name)};row.appendChild(del);el.appendChild(row);
  });
  if(d.truncated){var note=document.createElement('p');note.textContent=qc5Text("Sono mostrati i 64 video completati più recenti.");el.appendChild(note);}
 });
}
function recPlay(name,size){
 var id=++playbackId,v=document.getElementById('rec-video'),label=document.getElementById('rec-player-status');v.onerror=null;v.pause();recReleaseDownload();
 videoPauseForRecording();v.removeAttribute('src');v.load();document.getElementById('rec-player').style.display='block';
 playbackAudio=QC5RecordedMedia.audioPlayer(v);
 label.textContent=qc5Text("Preparazione del lettore…");
 if(size>32*1024*1024){label.textContent=qc5Text("Filmato oltre 32 MB: usa Scarica per aprirlo nel tuo lettore.");return;}
 v.onerror=function(){if(id===playbackId)label.textContent=qc5Text("Il browser non legge questo formato: usa Scarica e aprilo in VLC.")};
 var attempts=0;
 function startWhenReady(){
  if(id!==playbackId)return;
  recRequest('ready',null,function(d){
   if(id!==playbackId)return;
   if(!d.ok){label.textContent=qc5Text(d.result)||qc5Text("Lettore non disponibile");return;}
   if(!d.ready){if(++attempts<12)setTimeout(startWhenReady,500);else label.textContent=qc5Text("Camera occupata: chiudi il filmato e riprova.");return;}
   /* Buffer only in the browser. One bounded HTTP transfer avoids competing
    * metadata/seek requests to the camera's single download slot. */
   var xhr=new XMLHttpRequest();playbackRequest=xhr;xhr.open('GET','/api/rec/download?file='+encodeURIComponent(name),true);xhr.responseType='blob';xhr.timeout=120000;
   xhr.onprogress=function(e){if(id!==playbackId)return;if(e.loaded>32*1024*1024||(e.lengthComputable&&e.total>32*1024*1024)){xhr.abort();playbackRequest=null;label.textContent=qc5Text("Filmato troppo grande: usa Scarica.");return;}label.textContent=qc5Text("Caricamento del filmato: ")+(e.loaded/1048576).toFixed(1)+' MB';};
   xhr.onload=function(){
    if(id!==playbackId)return;playbackRequest=null;
    if(xhr.status!==200||!xhr.response||!xhr.response.size||xhr.response.size>32*1024*1024){label.textContent=qc5Text("Filmato non disponibile: chiudi il lettore e riprova.");return;}
    xhr.response.arrayBuffer().then(function(bytes){
     if(id!==playbackId)return;try{
      var movie=QC5RecordedMedia.parse(bytes);if(playbackAudio)playbackAudio.load(movie);
      playbackUrl=URL.createObjectURL(new Blob(QC5RecordedMedia.mp4Parts(movie),{type:'video/mp4'}));v.src=playbackUrl;v.load();
      label.textContent=qc5Text("Filmato caricato. Premi Play per riprodurlo.");var p=v.play();if(p&&p.catch)p.catch(function(){});
     }catch(error){if(playbackAudio){playbackAudio.close();playbackAudio=null;}label.textContent=qc5Text("Il browser non legge questo formato: usa Scarica e aprilo in VLC.");}
    }).catch(function(){if(id===playbackId)label.textContent=qc5Text("Filmato non disponibile: chiudi il lettore e riprova.");});
   };
   xhr.onerror=xhr.ontimeout=function(){if(id===playbackId){playbackRequest=null;label.textContent=qc5Text("Caricamento interrotto: chiudi il lettore e riprova.");}};
   xhr.send();
  });
 }
 startWhenReady();
}
function recPlayerClose(){var v=document.getElementById('rec-video');playbackId++;recReleaseDownload();v.onerror=null;v.pause();v.removeAttribute('src');v.load();document.getElementById('rec-player').style.display='none';videoResumeAfterRecording()}
function recDel(name){
 if(recBusy||!recToken||!confirm(qc5Text("Eliminare {name} dalla SD?", {name})))return;
 recBusy=true;recRequest('delete','token='+encodeURIComponent(recToken)+'&file='+encodeURIComponent(name),function(d){recBusy=false;toast(qc5Text(d.result)||qc5Text("Esito non disponibile"),d.ok?'ok':'err');if(d.ok)recLoadList();recCheckStatus()});
}
