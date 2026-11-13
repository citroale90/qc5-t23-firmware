/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef QC5_SPEAKER_GATE_H
#define QC5_SPEAKER_GATE_H
#ifdef __KERNEL__
#include <linux/types.h>
typedef u32 qsg_u32;
#else
#include <stdint.h>
typedef uint32_t qsg_u32;
#endif
#define QSG_BIT ((qsg_u32)1u<<31)
enum {QSG_PIN=0x00,QSG_INT=0x10,QSG_INTCLR=0x18,QSG_MASK=0x20,QSG_MASKSET=0x24,QSG_PAT1=0x30,QSG_PAT1CLR=0x38,QSG_PAT0=0x40,QSG_PAT0SET=0x44,QSG_PAT0CLR=0x48};
struct qsg_io {void *opaque;qsg_u32 (*read)(void *,unsigned);int (*write)(void *,unsigned,qsg_u32);};
struct qsg_state {unsigned level,confirmed;qsg_u32 pin,interrupt,mask,pat1,pat0;};
static int qsg_level_readback(struct qsg_io *io,struct qsg_state *st,unsigned *level)
{
 if(!st)return -1;st->confirmed=0;
 if(!io||!io->read)return -1;
 st->interrupt=io->read(io->opaque,QSG_INT);st->mask=io->read(io->opaque,QSG_MASK);st->pat1=io->read(io->opaque,QSG_PAT1);st->pat0=io->read(io->opaque,QSG_PAT0);st->pin=io->read(io->opaque,QSG_PIN);
 unsigned pin=!!(st->pin&QSG_BIT),pat0=!!(st->pat0&QSG_BIT);
 if((st->interrupt&QSG_BIT)||!(st->mask&QSG_BIT)||(st->pat1&QSG_BIT)||pin!=pat0)return -1;
 if(level)*level=pin;st->level=pin;st->confirmed=1;return (int)pin;
}
static inline int qsg_validate(struct qsg_io *io,struct qsg_state *st)
{return qsg_level_readback(io,st,0);}
static int qsg_off(struct qsg_io *io,struct qsg_state *st)
{
 if(!st)return 0;st->confirmed=0;
 if(!io||!io->write)return 0;
 for(unsigned attempt=0;attempt<3;attempt++){
  /* Set the low latch BEFORE switching a possibly unknown pin to output. */
  int ok=io->write(io->opaque,QSG_PAT0CLR,QSG_BIT);
  ok=io->write(io->opaque,QSG_INTCLR,QSG_BIT)&&ok;
  ok=io->write(io->opaque,QSG_MASKSET,QSG_BIT)&&ok;
  ok=io->write(io->opaque,QSG_PAT1CLR,QSG_BIT)&&ok;
  int level=qsg_level_readback(io,st,0);
  if(ok&&level==0)return 1;
 }
 st->confirmed=0;return 0;
}
static int qsg_on(struct qsg_io *io,struct qsg_state *st)
{
 if(!st)return 0;
 if(!io||!io->write){st->confirmed=0;return 0;}
 int initial=qsg_level_readback(io,st,0);
 if(initial==1)return 1; /* An idempotent ON never pulses the output low. */
 if(initial<0&&!qsg_off(io,st))return 0;
 st->confirmed=0;
 if(io->write(io->opaque,QSG_PAT0SET,QSG_BIT)&&qsg_level_readback(io,st,0)==1)return 1;
 (void)qsg_off(io,st);return 0; /* Failed ON cannot leave an asserted gate. */
}
#endif
