// SPDX-License-Identifier: GPL-2.0-only
/* Kernel-owned PTZ/filter sequencing. Closing or killing the UI/client
 * cancels the request and de-energizes the filter independently of userspace.
 * Module load only puts the dedicated speaker gate LOW. Motor/filter actions
 * require explicit requests; no arbitrary GPIO/I2C or flash operation. */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include "peripheral-abi.h"
#include "speaker-abi.h"
#include "speaker-gate.h"
typedef u8 uint8_t;typedef u16 uint16_t;typedef u32 uint32_t;typedef u64 uint64_t;
#define UINT64_MAX U64_MAX
#define UINT64_C(x) x##ULL
#include "board-qc5.h"
#include "i2c2-bus.h"
#include "motor-sequence.h"
#include "motor-direction.h"
#include "ptz-counter.h"
#include "move-progress.h"
static DEFINE_MUTEX(control_lock);
static atomic_t claimed=ATOMIC_INIT(0);
static void __iomem *regions[3];
static struct qbus transport;
static struct qmc_state sequence;
static struct qcm_status status;
static unsigned closing,command;
static struct qmp_move movement;
static struct qcm_progress progress;
static u64 sample_at;
static unsigned cancelled;
/* Original VP_*axis_motor_solution globals persist across Stop and init. */
static unsigned motor_directions;
static void control_work(struct work_struct *unused);
static DECLARE_DELAYED_WORK(work,control_work);
#ifdef QC5_PERIPHERAL_EMULATED
static u32 model_clock[64],model_gpio[64],model_i2c[64];
static u8 model_device[256];static unsigned model_selected,model_rx,model_raw;
static unsigned emulated_coil,emulated_power;
static unsigned emulated_raw_x,emulated_raw_y;
module_param(emulated_raw_x,uint,0600);module_param(emulated_raw_y,uint,0600);
module_param(emulated_coil,uint,0444);module_param(emulated_power,uint,0444);
#endif
static uint32_t bus_read(void *unused,unsigned region,uint32_t off)
{
 (void)unused;if(region>2||off>0xfc||off&3)return ~0u;
#ifdef QC5_PERIPHERAL_EMULATED
 if(region==QM_CPM)return model_clock[off/4];if(region==QM_GPIO_B)return model_gpio[off/4];
 if(off==0x70)return 2|(model_rx?8:0);if(off==0x34)return model_raw;
 if(off==0x40){model_raw=0;return 0;}if(off==0x10){model_rx=0;
  if(model_selected==0x1a)return (emulated_raw_x>>8)&255;if(model_selected==0x1b)return emulated_raw_x&255;
  if(model_selected==0x18)return (emulated_raw_y>>8)&255;if(model_selected==0x19)return emulated_raw_y&255;
  return model_device[model_selected];}
 if(off==0x9c)return model_i2c[0x6c/4];return model_i2c[off/4];
#else
 return readl(regions[region]+off);
#endif
}
static int bus_write(void *unused,unsigned region,uint32_t off,uint32_t value)
{
 (void)unused;if(region>2||off>0xfc||off&3)return 0;
 if(region==QM_GPIO_B&&((off!=0x24&&off!=0x28)||(value&~(3u<<20))))return 0;
 if(region==QM_CPM&&off!=0x20)return 0;
 if(region==QM_I2C2){switch(off){case 0:if(value!=0x63)return 0;break;case 4:if(value!=0x10)return 0;break;
  case 0x10:if(value>0x700)return 0;break;case 0x30:case 0x88:if(value)return 0;break;case 0x6c:if(value>1)return 0;break;
  case 0x14:case 0x18:case 0x7c:case 0x94:if(value>1000)return 0;break;default:return 0;}}
#ifdef QC5_PERIPHERAL_EMULATED
 if(region==QM_CPM)model_clock[off/4]=value;
 else if(region==QM_GPIO_B){if(off==0x24)model_gpio[8]|=value;else model_gpio[8]&=~value;}
 else{model_i2c[off/4]=value;if(off==0x10){if(value==0x700){model_rx=1;model_raw=0x200;}else if(value&0x200){model_device[model_selected]=(u8)value;if(model_selected==0x1a)emulated_raw_x=(emulated_raw_x&255)|((value&255)<<8);if(model_selected==0x1b)emulated_raw_x=(emulated_raw_x&0xff00)|(value&255);if(model_selected==0x18)emulated_raw_y=(emulated_raw_y&255)|((value&255)<<8);if(model_selected==0x19)emulated_raw_y=(emulated_raw_y&0xff00)|(value&255);if(model_selected==0x16)emulated_coil=(u8)value;if(model_selected==0x17)emulated_power=(u8)value;model_raw=0x200;}else model_selected=value;}}
#else
 /* Single-core T23: make the shared clock-gate RMW indivisible with other
  * clock users; SET/CLEAR GPIO aliases affect only the two I2C route pins. */
 if(region==QM_CPM){unsigned long flags;local_irq_save(flags);
  u32 gate_now=readl(regions[region]+off);writel((gate_now&~(1u<<9))|(value&(1u<<9)),regions[region]+off);local_irq_restore(flags);
 }else writel(value,regions[region]+off);
#endif
 return 1;
}
static void bus_delay(void *unused,unsigned us){(void)unused;usleep_range(us,us+50);}
static u64 clock_us(void){return div_u64(ktime_get_ns(),1000);}
static uint64_t actor_time(void *unused){(void)unused;return clock_us();}
static struct qmc_io actor={NULL,NULL,actor_time};
static int actor_write(void *unused,unsigned reg,unsigned value)
{
 (void)unused;if(!qb_motor_write(&transport,reg,value))return 0;
 if(reg==0x16||reg==0x17){unsigned actual=0,mask=reg==0x16?3:31;
  return qb_transfer(&transport,reg,&actual,1)&&((actual^value)&mask)==0;}
 return 1;
}
/* Reject a torn high/low pair and retry a bounded three times. The decoded
 * values have the OEM four-step quantum; this is not encoder angle feedback. */
static int read_counter(unsigned axis,unsigned *value)
{
 unsigned reg=axis?0x18:0x1a;
 for(unsigned attempt=0;attempt<3;attempt++){
  unsigned hi,lo,again;uint16_t decoded;
  if(!qb_transfer(&transport,reg,&hi,1)||!qb_transfer(&transport,reg+1,&lo,1)||!qb_transfer(&transport,reg,&again,1))return 0;
  if(hi==again){if(!qpc_decode(axis,hi,lo,&decoded)||decoded>32764||(decoded&3))return 0;*value=decoded;return 1;}
 }return 0;
}
static void publish_progress(void)
{
 progress.steps=movement.done;progress.reason=movement.reason;progress.active=movement.active;
 progress.valid=!movement.active&&movement.reason==QMP_COMPLETE;
}
static void control_work(struct work_struct *unused)
{
 (void)unused;mutex_lock(&control_lock);
 if(closing||!status.busy)goto done;
 u64 now=clock_us();int result;
 if(status.phase==2){
  if(now<sample_at)goto again;
  unsigned count=0;int ok=read_counter(movement.selected,&count);
  if(ok){if(movement.selected)status.y_counter=count;else status.x_counter=count;}
  unsigned reason=qmp_sample(&movement,count,ok,now);publish_progress();
  if(reason!=QMP_RUNNING)pr_info("QC5_PTZ_COUNTER phase=2 reason=%u raw=%u valid=%u base=%u last=%u done=%u requested=%u\n",reason,count,ok,movement.base,movement.last,movement.done,movement.requested);
  if(reason==QMP_RUNNING){sample_at=now+20000;goto again;}
  struct qmc_plan stop;
  if(!qmc_stop_directed(&stop,3,motor_directions)||!qmc_begin(&sequence,&stop,now,1)){(void)qmc_abort(&sequence,&actor);status.last_error=EIO;status.busy=0;goto done;}
  status.phase=3;
 }
 /* The original MCU setup uses10/50us gaps. A delayed work tick is10ms
  * with the pinned HZ=100 kernel and cannot represent those gaps. Keep
  * only this bounded setup burst in the worker; long IR/stop waits yield. */
 for(unsigned budget=0;;budget++){
  now=clock_us();
  if(sequence.active&&sequence.due>now&&sequence.due-now<=50){udelay((unsigned)(sequence.due-now));now=clock_us();}
  result=qmc_tick(&sequence,&actor,now);
  if(result||budget>=31||!sequence.active)break;
  now=clock_us();if(sequence.due>now&&sequence.due-now>50)break;
 }
 status.ir_energized=sequence.ir_energized;
 if(result<0){movement.active=0;movement.reason=QMP_IO;publish_progress();status.last_error=EIO;status.busy=0;goto done;}
 if(result==1){
  if((command==QCM_JOG||command==QCM_MOVE||command==QCM_HOME)&&status.phase==1){status.phase=2;sample_at=now;goto again;}
  if(command==QCM_INITIALIZE){status.initialized=1;memset(&movement,0,sizeof movement);memset(&progress,0,sizeof progress);progress.version=2;}
  if(movement.active){
   unsigned count=0;int ok=read_counter(movement.selected,&count);
   if(ok){if(movement.selected)status.y_counter=count;else status.x_counter=count;}
   unsigned before=movement.reason;unsigned reason=qmp_finish(&movement,count,ok,now,cancelled);publish_progress();
   pr_info("QC5_PTZ_COUNTER phase=3 before=%u reason=%u raw=%u valid=%u base=%u last=%u done=%u requested=%u\n",before,reason,count,ok,movement.base,movement.last,movement.done,movement.requested);
   if(reason!=QMP_COMPLETE&&reason!=QMP_CANCEL)status.last_error=reason==QMP_TIMEOUT?ETIMEDOUT:EIO;
  }
  status.busy=status.phase=0;status.sequence++;goto done;
 }
again:schedule_delayed_work(&work,msecs_to_jiffies(1));
done:mutex_unlock(&control_lock);
}
static int device_open(struct inode *inode,struct file *file)
{
 (void)inode;(void)file;if(atomic_cmpxchg(&claimed,0,1))return -EBUSY;
 mutex_lock(&control_lock);memset(&status,0,sizeof status);memset(&sequence,0,sizeof sequence);memset(&movement,0,sizeof movement);memset(&progress,0,sizeof progress);progress.version=2;closing=cancelled=0;
 struct qm_io io={NULL,bus_read,bus_write,bus_delay};int ok=qb_open(&transport,&io);
 actor.write=actor_write;mutex_unlock(&control_lock);
 if(!ok){atomic_set(&claimed,0);return -ENODEV;}return 0;
}
static int device_release(struct inode *inode,struct file *file)
{
 (void)inode;(void)file;mutex_lock(&control_lock);closing=1;mutex_unlock(&control_lock);cancel_delayed_work_sync(&work);
 mutex_lock(&control_lock);
 if(status.initialized||sequence.ir_energized||status.busy||status.sequence)(void)qmc_abort(&sequence,&actor);
 (void)qb_close(&transport);memset(&status,0,sizeof status);mutex_unlock(&control_lock);atomic_set(&claimed,0);return 0;
}
static long device_ioctl(struct file *file,unsigned int request,unsigned long arg)
{
 (void)file;long result=0;mutex_lock(&control_lock);
 if(request==QCM_READ_STATUS){status.ir_energized=sequence.ir_energized;if(copy_to_user((void __user *)arg,&status,sizeof status))result=-EFAULT;goto done;}
 if(request==QCM_READ_PROGRESS){publish_progress();if(copy_to_user((void __user *)arg,&progress,sizeof progress))result=-EFAULT;goto done;}
 if(request!=QCM_SUBMIT){result=-ENOTTY;goto done;}
 struct qcm_request p;if(copy_from_user(&p,(void __user *)arg,sizeof p)){result=-EFAULT;goto done;}
 if(closing||p.command<QCM_INITIALIZE||p.command>QCM_HOME||p.command==QCM_STATUS||p.axis>1||p.direction>1){result=-EINVAL;goto done;}
 if(p.command==QCM_COUNTERS){
  if(status.busy){result=-EBUSY;goto done;}
  unsigned values[4]={0};const unsigned regs[]={0x1a,0x1b,0x18,0x19};
  for(unsigned i=0;i<4;i++)if(!qb_transfer(&transport,regs[i],values+i,1)){result=-EIO;goto done;}
  uint16_t x,y;if(!qpc_decode(0,values[0],values[1],&x)||!qpc_decode(1,values[2],values[3],&y)){result=-EIO;goto done;}
  status.x_counter=x;status.y_counter=y;goto done;
 }
 if(status.busy&&p.command!=QCM_STOP){result=-EBUSY;goto done;}
 if(!status.initialized&&p.command!=QCM_INITIALIZE&&p.command!=QCM_STOP){result=-EAGAIN;goto done;}
 struct qmc_plan plan;int ok=0;
 if(p.command==QCM_INITIALIZE){ok=qmc_initial(&plan);}
 else if(p.command==QCM_JOG||p.command==QCM_MOVE||p.command==QCM_HOME){
  unsigned maximum=p.axis?QCB_TILT_MAX:QCB_PAN_MAX;
  ok=p.value>=4&&p.value<=maximum&&!(p.value&3)&&(p.command!=QCM_JOG||p.value<=256)&&
   (p.command!=QCM_HOME||p.value==maximum)&&progress.generation!=~0u&&
   qmc_jog(&plan,p.axis,qmd_hardware_direction(p.axis,p.direction),p.value,maximum,p.command==QCM_HOME?1:2);
  if(ok)ok=qmp_begin(&movement,p.axis,p.direction,p.value,clock_us());
  if(ok){progress=(struct qcm_progress){.version=2,.generation=progress.generation+1,.axis=p.axis,.requested=p.value,.active=1};}
 }
 else if(p.command==QCM_FILTER)ok=qmc_filter(&plan,p.value);
 else if(p.command==QCM_STOP){
  /* Preempt the motion plan directly. Power-off before the OEM stop ramp
   * would add an operation absent from Motor_stop; abort is needed only
   * when an IR bridge pulse must first be de-energized. */
  ok=(!sequence.ir_energized||qmc_abort(&sequence,&actor))&&
   !sequence.ir_energized&&qmc_stop_directed(&plan,3,motor_directions);
 }
 if(!ok||!qmc_begin(&sequence,&plan,clock_us(),p.command==QCM_STOP)){result=-EINVAL;goto done;}
 if(p.command==QCM_JOG||p.command==QCM_MOVE||p.command==QCM_HOME)
  motor_directions=(motor_directions&~(1u<<p.axis))|(qmd_hardware_direction(p.axis,p.direction)<<p.axis);
 cancelled=p.command==QCM_STOP;command=p.command;status.busy=1;status.phase=1;status.last_error=0;schedule_delayed_work(&work,0);
done:mutex_unlock(&control_lock);return result;
}
static DEFINE_MUTEX(speaker_lock);
static atomic_t speaker_claimed=ATOMIC_INIT(0);
static unsigned speaker_present;
static struct qsg_io speaker_io;
static struct qsg_state speaker_state;
static uint32_t speaker_read(void *opaque,unsigned off)
{
 (void)opaque;
 if(off!=QSG_PIN&&off!=QSG_INT&&off!=QSG_MASK&&off!=QSG_PAT1&&off!=QSG_PAT0)return 0;
#ifdef QC5_PERIPHERAL_EMULATED
 if(off/4>=64)return 0;
 return model_gpio[off/4];
#else
 if(!regions[1])return 0;
 return readl(regions[1]+off);
#endif
}
static int speaker_write(void *opaque,unsigned off,uint32_t value)
{
 (void)opaque;
 if(value!=QSG_BIT)return 0;
 switch(off){case QSG_INTCLR:case QSG_MASKSET:case QSG_PAT1CLR:case QSG_PAT0SET:case QSG_PAT0CLR:break;default:return 0;}
#ifdef QC5_PERIPHERAL_EMULATED
 switch(off){
  case QSG_INTCLR:model_gpio[QSG_INT/4]&=~QSG_BIT;break;
  case QSG_MASKSET:model_gpio[QSG_MASK/4]|=QSG_BIT;break;
  case QSG_PAT1CLR:model_gpio[QSG_PAT1/4]&=~QSG_BIT;break;
  case QSG_PAT0SET:model_gpio[QSG_PAT0/4]|=QSG_BIT;model_gpio[QSG_PIN/4]|=QSG_BIT;break;
  case QSG_PAT0CLR:model_gpio[QSG_PAT0/4]&=~QSG_BIT;model_gpio[QSG_PIN/4]&=~QSG_BIT;break;
 }
 return 1;
#else
 if(!regions[1])return 0;
 writel(value,regions[1]+off);
 return 1;
#endif
}
static int speaker_low_locked(void)
{
 if(!speaker_io.read||!speaker_io.write)return 0;
 return qsg_off(&speaker_io,&speaker_state);
}
static int speaker_configure_low(void)
{
 mutex_lock(&speaker_lock);
 speaker_io=(struct qsg_io){NULL,speaker_read,speaker_write};
 memset(&speaker_state,0,sizeof speaker_state);
 int ok=qsg_off(&speaker_io,&speaker_state);
 mutex_unlock(&speaker_lock);
 return ok;
}
static int speaker_force_low(void)
{
 mutex_lock(&speaker_lock);
 int ok=0;
 if(speaker_io.read&&speaker_io.write)ok=qsg_off(&speaker_io,&speaker_state);
 mutex_unlock(&speaker_lock);
 return ok;
}
static int speaker_open(struct inode *inode,struct file *file)
{
 (void)inode;(void)file;
 if(atomic_cmpxchg(&speaker_claimed,0,1))return -EBUSY;
 mutex_lock(&speaker_lock);
 int ok=speaker_low_locked();
 mutex_unlock(&speaker_lock);
 if(!ok){atomic_set(&speaker_claimed,0);return -EIO;}
 return 0;
}
static int speaker_release(struct inode *inode,struct file *file)
{
 (void)inode;(void)file;
 mutex_lock(&speaker_lock);
 int ok=speaker_low_locked();
 mutex_unlock(&speaker_lock);
 atomic_set(&speaker_claimed,0);
 return ok?0:-EIO;
}
static long speaker_ioctl(struct file *file,unsigned int request,unsigned long arg)
{
 (void)file;long result=0;
 if(request!=QSM_SET)return -ENOTTY;
 __u32 value;if(copy_from_user(&value,(void __user *)arg,sizeof value))return -EFAULT;
 if(value>1)return -EINVAL;
 mutex_lock(&speaker_lock);
 if(!speaker_io.read||!speaker_io.write){result=-ENODEV;goto done;}
 int ok=value?qsg_on(&speaker_io,&speaker_state):qsg_off(&speaker_io,&speaker_state);
 result=ok?0:-EIO;
done:mutex_unlock(&speaker_lock);return result;
}
static const struct file_operations speaker_ops={.owner=THIS_MODULE,.open=speaker_open,.release=speaker_release,.unlocked_ioctl=speaker_ioctl,.llseek=noop_llseek};
static struct miscdevice speaker_device={.minor=MISC_DYNAMIC_MINOR,.name="qc5-speaker",.fops=&speaker_ops,.mode=0600};
static const struct file_operations ops={.owner=THIS_MODULE,.open=device_open,.release=device_release,.unlocked_ioctl=device_ioctl,.llseek=noop_llseek};
static struct miscdevice device={.minor=MISC_DYNAMIC_MINOR,.name="qc5-peripheral",.fops=&ops,.mode=0600};
static int __init control_init(void)
{
 if(!of_machine_is_compatible("ingenic,t23"))return -ENODEV;
 int error=-ENOMEM;
#ifdef QC5_PERIPHERAL_EMULATED
 model_clock[0]=(1u<<30)|(1u<<24);model_clock[8]=0xaaaa0200;model_gpio[8]=0x33300000;model_gpio[12]=3u<<20;model_device[0x1d]=0x53;
#else
 if(!request_mem_region(QCB_I2C2,0x100,"qc5-peripheral"))return -EBUSY;
 const unsigned addresses[]={0x10000000,QCB_GPIO_B,QCB_I2C2};
 for(unsigned i=0;i<3;i++){regions[i]=ioremap(addresses[i],0x100);if(!regions[i])goto bad;}
#endif
 speaker_present=0;
 if(!speaker_configure_low()){error=-EIO;goto bad;}
 error=misc_register(&device);if(error)goto bad;
 error=misc_register(&speaker_device);
 if(error){misc_deregister(&device);goto bad;}
 speaker_present=1;return 0;
bad:
 (void)speaker_force_low();
#ifndef QC5_PERIPHERAL_EMULATED
 for(unsigned i=0;i<3;i++)if(regions[i])iounmap(regions[i]);release_mem_region(QCB_I2C2,0x100);
#endif
 return error;
}
static void __exit control_exit(void)
{if(speaker_present)misc_deregister(&speaker_device);(void)speaker_force_low();misc_deregister(&device);cancel_delayed_work_sync(&work);
#ifndef QC5_PERIPHERAL_EMULATED
 for(unsigned i=0;i<3;i++)if(regions[i])iounmap(regions[i]);release_mem_region(QCB_I2C2,0x100);
#endif
}
module_init(control_init);module_exit(control_exit);MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("QC5 T23 bounded PTZ/IR-cut sequencer and exclusive speaker gate");
#ifdef QC5_PERIPHERAL_EMULATED
MODULE_INFO(qc5_emulator_only,"true-no-mmio");
#endif
