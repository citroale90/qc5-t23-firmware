/* Native detector. The NAS is an optional recording destination only. */
var motionBusy=false,motionPollBusy=false,motionEnabled=false,motionToken='';
function motionCheckStatus(){
 if(motionBusy||motionPollBusy)return;motionPollBusy=true;
 cameraControlRequest('/api/motion/status',null,function(d){
  motionPollBusy=false;var label=document.getElementById('motion-status'),button=document.getElementById('motion-btn');
  if(!d.ok||!d.motion){motionToken='';button.disabled=true;label.textContent=qc5Text(d.result)||qc5Text("Stato del movimento non disponibile");return;}
  var m=d.motion;motionToken=d.token||'';motionEnabled=!!m.enabled;
  button.textContent=motionEnabled?qc5Text("Disattiva registrazione su movimento"):qc5Text("Attiva registrazione su movimento");
  button.disabled=!motionToken||!m.local_only||!!m.custom_schedule;
  if(!m.local_only){label.textContent=qc5Text("Il controllo del movimento sarà disponibile con il firmware locale. Il programma originale della camera è ancora installato.");return;}
  if(m.custom_schedule){label.textContent=qc5Text("Piano personalizzato o rilevamento persone presente: impostazioni conservate.");return;}
  label.textContent=motionEnabled&&m.event_recording?qc5Text("Rilevamento nella camera e registrazione degli eventi abilitati."):qc5Text("Registrazione su movimento disabilitata.");
  if(motionEnabled&&m.auto_stop)label.textContent+=qc5Text(" Il filmato termina dopo {seconds} secondi senza movimento.", {seconds: m.post_seconds || 5});
  if(m.suppressed)label.textContent+=qc5Text(" Evento fermato manualmente: si riattiva dopo che la scena torna ferma.");
  if(m.continuous)label.textContent+=qc5Text(" È attiva anche la registrazione continua: vengono conservate tutte le riprese.");
 });
}
function motionToggle(){
 if(motionBusy||motionPollBusy||!motionToken)return;motionBusy=true;document.getElementById('motion-btn').disabled=true;
 cameraControlRequest('/api/motion/config','token='+encodeURIComponent(motionToken)+'&enabled='+(motionEnabled?'0':'1'),function(d){
  motionBusy=false;toast(qc5Text(d.result)||qc5Text("Esito non disponibile"),d.ok?'ok':'err');motionCheckStatus();recCheckStatus();
 },60000);
}
