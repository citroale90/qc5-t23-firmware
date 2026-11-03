/* All setup requests stay on the camera. No credentials in URLs or storage. */
(()=>{'use strict';
 const $=selector=>document.querySelector(selector),form=$('#wifi-form'),ssid=$('#ssid'),pwd=$('#pwd'),open=$('#open'),button=$('#connect'),status=$('#status'),reveal=$('#reveal'),networks=$('#networks'),manualLabel=$('#manual-label');
 let token='',busy=false,enabled=false,pollTimer=0;
 const say=(text,error=false)=>{status.textContent=text;status.dataset.error=String(error);};
 const bytes=text=>new TextEncoder().encode(text).length;
 function api(path,body){return new Promise((resolve,reject)=>{
  const xhr=new XMLHttpRequest();xhr.open(body===undefined?'GET':'POST',path,true);xhr.timeout=6000;
  if(body!==undefined)xhr.setRequestHeader('Content-Type','application/x-www-form-urlencoded');
  xhr.onload=()=>{if(xhr.status<200||xhr.status>=300){reject(Error('request'));return;}try{resolve(JSON.parse(xhr.responseText));}catch(_){reject(Error('response'));}};
  xhr.onerror=xhr.ontimeout=()=>reject(Error('connection'));xhr.send(body===undefined?null:body);
 });}
 function selected(){const option=networks.options[networks.selectedIndex];return option&&option.value&&option.value!=='manual'?option:null;}
 function networkChanged(){
  const manual=networks.value==='manual';ssid.hidden=!manual;manualLabel.hidden=!manual;ssid.disabled=!manual;ssid.required=manual;
  const option=selected();if(option){open.checked=option.dataset.open==='1';pwd.disabled=open.checked;pwd.required=!open.checked;}
 }
 function loadNetworks(){return api('/setup/networks').then(data=>{
  const old=networks.value,seen=new Set();networks.textContent='';
  const first=document.createElement('option');first.value='';first.disabled=true;first.textContent=qc5Text("Seleziona la tua rete");networks.append(first);
  const list=Array.isArray(data.networks)?data.networks:[];list.sort((a,b)=>(b.signal||0)-(a.signal||0));
  for(const network of list){
   const hex=network.ssid_hex;if(typeof hex!=='string'||!/^([0-9a-f]{2}){1,32}$/i.test(hex))continue;
   const key=hex+String(network.secured);if(seen.has(key))continue;seen.add(key);
   const raw=new Uint8Array(hex.match(/../g).map(value=>parseInt(value,16))),name=new TextDecoder('utf-8').decode(raw);
   const option=document.createElement('option');option.value=hex;option.dataset.open=network.secured===false?'1':'0';
   const strength=network.signal>=70?qc5Text("segnale forte"):network.signal>=40?qc5Text("segnale medio"):qc5Text("segnale debole");
   option.textContent=(network.secured===false?'':'🔒 ')+name+' · '+strength;networks.append(option);
  }
  const manual=document.createElement('option');manual.value='manual';manual.textContent=qc5Text("Rete nascosta o non presente…");networks.append(manual);
  networks.value=Array.from(networks.options).some(option=>option.value===old)?old:'';networkChanged();
 }).catch(()=>{networks.textContent='';const option=document.createElement('option');option.value='manual';option.textContent=qc5Text("Inserisci il nome della rete");networks.append(option);networkChanged();});}
 function finishBusy(){busy=false;clearInterval(pollTimer);pollTimer=0;button.disabled=!enabled;}
 async function state(){
  try{
   const data=await api('/setup/status');enabled=data.enabled===true;token=data.token||'';button.disabled=!enabled||busy;
   if(data.state==='wifi_salvato_camera_e_sd'){if(!busy)say(qc5Text("Pronta. Scegli la rete Wi-Fi da utilizzare."));}
   else if(data.state==='wifi_salvato_in_camera_sd_non_disponibile')say(qc5Text("Wi-Fi salvato nella telecamera; la SD non è scrivibile."));
   else if(data.state==='wifi_salvato_sd_native_in_attesa')say(qc5Text("Wi-Fi salvato sulla SD. Il salvataggio interno della telecamera è ancora in attesa."));
   else if(data.state==='wifi_connesso_salvataggio_non_confermato')say(qc5Text("Wi-Fi connesso, ma il salvataggio non è confermato. Controlla che la SD sia scrivibile."),true);
   else if(!enabled)say(qc5Text("La configurazione Wi-Fi non è attiva in questa modalità."));
   else if(data.state==='richiesta_non_accettata'){finishBusy();say(qc5Text("Tentativo non avviato. Attendi qualche secondo e riprova."),true);}
   else if(data.state==='configurazione_hotspot'&&!busy)say(qc5Text("Scegli la tua rete Wi-Fi e inserisci la password."));
   else if(!busy)say(qc5Text("Pronta per la configurazione."));
  }catch(_){if(!busy){button.disabled=true;say(qc5Text("Telecamera non raggiungibile. Verifica la rete Wi-Fi a cui sei collegato."),true);}}
 }
 reveal.addEventListener('click',()=>{const show=pwd.type==='password';pwd.type=show?'text':'password';reveal.textContent=show?qc5Text("Nascondi"):qc5Text("Mostra");reveal.setAttribute('aria-pressed',String(show));});
 open.addEventListener('change',()=>{pwd.disabled=open.checked;pwd.required=!open.checked;});
 networks.addEventListener('change',networkChanged);
 form.addEventListener('submit',async event=>{
  event.preventDefault();if(busy||!enabled||!token)return;
  const option=selected(),manual=networks.value==='manual',password=open.checked?'':pwd.value;
  if((!option&&!manual)||(manual&&(!bytes(ssid.value)||bytes(ssid.value)>32))||(!open.checked&&(bytes(password)<8||bytes(password)>63))){say(qc5Text("Scegli una rete. La password deve contenere da 8 a 63 byte."),true);return;}
  busy=true;button.disabled=true;say(qc5Text("Avvio della connessione…"));
  const data=new URLSearchParams({pwd:password,open:open.checked?'1':'0',token});
  data.set(option?'ssid_hex':'ssid',option?option.value:ssid.value);
  try{
   const reply=await api('/setup/wifi',data.toString());say(qc5Text(reply.result),!reply.ok);
   if(reply.ok){pwd.value='';pollTimer=setInterval(state,4000);setTimeout(()=>{if(busy){finishBusy();state();loadNetworks();}},50000);return;}
  }catch(_){say(qc5Text("Se l’hotspot scompare, torna sulla rete scelta. Altrimenti attendi e riprova."),true);}
  finishBusy();
 });
 loadNetworks();state();
})();
