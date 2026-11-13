/* Bounded browser transmit gain; no camera DSP changes. */
const QC5Segments=[0xff,0x1ff,0x3ff,0x7ff,0xfff,0x1fff,0x3fff,0x7fff];
class QC5Capture extends AudioWorkletProcessor {
 constructor(options={}){super();const g=options.processorOptions&&options.processorOptions.gain;
  this.targetGain=this.currentGain=Number.isFinite(g)&&g>=0.5&&g<=2?g:1;
  this.alpha=1-Math.exp(-1/(sampleRate*0.01));
  this.levelPeak=0;this.levelEnergy=0;this.levelSamples=0;this.active=true;
  this.ratio=sampleRate/16000;this.sum=0;this.weight=0;
  this.frame=new Uint8Array(320);this.used=0;
  this.port.onmessage=e=>{if(e.data==='stop')this.active=false;
   else if(this.active&&e.data&&Number.isFinite(e.data.qc5Gain)&&e.data.qc5Gain>=0.5&&e.data.qc5Gain<=2)this.targetGain=e.data.qc5Gain;};}
 static alaw(sample){
  let pcm=Math.max(-32768,Math.min(32767,Math.round(sample*32768))),mask;
  if(pcm>=0)mask=0xd5;else{mask=0x55;pcm=-pcm-1;}
  let segment=0;while(segment<8&&pcm>QC5Segments[segment])segment++;
  const value=segment>=8?0x7f:(segment<<4)|((pcm>>(segment<2?4:segment+3))&15);
  return value^mask;}
 process(inputs,outputs){
  for(const output of outputs)for(const channel of output)channel.fill(0);
  if(!this.active)return false;
  const channels=inputs[0];if(!channels||!channels.length)return true;
  for(let i=0;i<channels[0].length;i++){
   let value=0;for(const channel of channels)value+=Number.isFinite(channel[i])?channel[i]:0;
   value=Math.max(-1,Math.min(1,value/channels.length));
   this.levelPeak=Math.max(this.levelPeak,Math.abs(value));
   this.levelEnergy+=value*value;this.levelSamples++;
   if(this.levelSamples>=sampleRate/5){this.port.postMessage({qc5Level:1,peak:this.levelPeak,
    rms:Math.sqrt(this.levelEnergy/this.levelSamples)});this.levelPeak=0;this.levelEnergy=0;this.levelSamples=0;}
   this.currentGain+=this.alpha*(this.targetGain-this.currentGain);
   value=Math.max(-0.95,Math.min(0.95,value*this.currentGain));
   let remaining=1;
   while(remaining>1e-8){const take=Math.min(remaining,this.ratio-this.weight);
    this.sum+=value*take;this.weight+=take;remaining-=take;
    if(this.weight+1e-8>=this.ratio){this.frame[this.used++]=QC5Capture.alaw(this.sum/this.ratio);
     this.sum=0;this.weight=0;
     if(this.used===320){const ready=this.frame;this.frame=new Uint8Array(320);this.used=0;
      this.port.postMessage(ready.buffer,[ready.buffer]);}}}
  }
  return true;}
}
registerProcessor('qc5-capture',QC5Capture);
