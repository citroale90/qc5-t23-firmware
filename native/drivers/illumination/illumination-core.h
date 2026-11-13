/* SPDX-License-Identifier: GPL-2.0-only
 * QC5 full-brightness only: PB18 white, PB29 IR, shared PB17 output.
 * PWM0 normal polarity has high as its initial/full-duty level. We select
 * static100% for explicit ON, rather than borrowing the watchdog's TCU block.
 * This does not implement intermediate PWM brightness levels.
 * Physical7.3 LED routing remains to be qualified before deployment.
 */
#ifndef QC5_ILLUMINATION_CORE_H
#define QC5_ILLUMINATION_CORE_H
#define QIL_COMMON (1u<<17)
#define QIL_WHITE (1u<<18)
#define QIL_IR (1u<<29)
#define QIL_GATES (QIL_WHITE|QIL_IR)
#define QIL_PINS (QIL_GATES|QIL_COMMON)
enum {QIL_OFF=0,QIL_WHITE_ON=1,QIL_IR_ON=2};
struct qil_io {void *opaque;int (*read)(void *,unsigned,unsigned *);int (*write)(void *,unsigned,unsigned);};
static int qil_write_allowed(unsigned offset,unsigned value)
{return value&&!(value&~QIL_PINS)&&(offset==0x18||offset==0x24||offset==0x38||offset==0x44||offset==0x48);}
static int qil_read_allowed(unsigned offset)
{return offset==0x10||offset==0x20||offset==0x30||offset==0x40;}
static int qil_mode(const struct qil_io *io,unsigned *mode)
{
 unsigned interrupt,mask,pat1,pat0;
 if(!io||!io->read||!io->write||!mode||!io->read(io->opaque,0x10,&interrupt)||!io->read(io->opaque,0x20,&mask)||!io->read(io->opaque,0x30,&pat1)||!io->read(io->opaque,0x40,&pat0)||
    (interrupt&QIL_PINS)||(mask&QIL_PINS)!=QIL_PINS||(pat1&QIL_PINS))return 0;
 unsigned state=pat0&QIL_PINS;
 if(!state)*mode=QIL_OFF;
 else if(state==(QIL_COMMON|QIL_WHITE))*mode=QIL_WHITE_ON;
 else if(state==(QIL_COMMON|QIL_IR))*mode=QIL_IR_ON;
 else return 0;
 return 1;
}
static int qil_output(const struct qil_io *io,unsigned mask,unsigned on)
{
 /* Latch low before muxing gate pins, then choose GPIO output. SET/CLEAR
  * aliases preserve every unrelated pin, including motor I2C and Wi-Fi. */
 return io->write(io->opaque,on?0x44:0x48,mask)&&io->write(io->opaque,0x18,mask)&&io->write(io->opaque,0x24,mask)&&io->write(io->opaque,0x38,mask);
}
static int qil_set(const struct qil_io *io,unsigned mode)
{
 if(!io||!io->read||!io->write||mode>QIL_IR_ON)return 0;
 if(!qil_output(io,QIL_GATES,0)||!qil_output(io,QIL_COMMON,mode!=QIL_OFF))goto fail;
 if(mode!=QIL_OFF&&!io->write(io->opaque,0x44,mode==QIL_WHITE_ON?QIL_WHITE:QIL_IR))goto fail;
 unsigned actual;if(qil_mode(io,&actual)&&actual==mode)return 1;
fail:
 /* Best effort on a failing I/O callback. Never report an unverified ON. */
 (void)io->write(io->opaque,0x48,QIL_GATES);(void)io->write(io->opaque,0x48,QIL_COMMON);return 0;
}
static int qil_line(const struct qil_io *io,unsigned ir,unsigned on)
{
 if(ir>1||on>1)return 0;
 unsigned active_mode=QIL_OFF,selected=ir?QIL_IR_ON:QIL_WHITE_ON;
 (void)qil_mode(io,&active_mode);return qil_set(io,on?selected:active_mode==selected?QIL_OFF:active_mode);
}
#endif
