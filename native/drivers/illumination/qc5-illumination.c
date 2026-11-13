// SPDX-License-Identifier: GPL-2.0-only
/* Two named, mutually exclusive outputs. Never touches the TCU/watchdog,
 * loads a vendor module, modifies SD/NOR or drives anything at module load.
 * Replaces (must not coexist with) the older white-only GPIO module.
 */
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include "illumination-core.h"
#include "board-qc5.h"
static DEFINE_RAW_SPINLOCK(light_lock);
#ifdef QC5_ILLUMINATION_EMULATED
MODULE_INFO(qc5_emulator_only,"true-no-mmio");
static unsigned model[0x100/4];
static int light_read(void *unused,unsigned off,unsigned *v)
{(void)unused;if(!qil_read_allowed(off))return 0;*v=model[off/4];return 1;}
static int light_write(void *unused,unsigned off,unsigned v)
{(void)unused;if(!qil_write_allowed(off,v))return 0;if(off==0x18)model[0x10/4]&=~v;else if(off==0x24)model[0x20/4]|=v;else if(off==0x38)model[0x30/4]&=~v;else if(off==0x44)model[0x40/4]|=v;else if(off==0x48)model[0x40/4]&=~v;return 1;}
#else
static void __iomem *bank;
static_assert(QCB_GPIO_B==0x10011000u);
static int light_read(void *unused,unsigned off,unsigned *v)
{(void)unused;if(!qil_read_allowed(off))return 0;*v=readl(bank+off);return 1;}
static int light_write(void *unused,unsigned off,unsigned v)
{(void)unused;if(!qil_write_allowed(off,v))return 0;writel(v,bank+off);return 1;}
#endif
static struct qil_io light_io={NULL,light_read,light_write};
static struct gpio_chip chips[2];
static int light_request(struct gpio_chip *gc,unsigned off)
{(void)gc;return off?-EINVAL:0;} /* Ownership only, no output action. */
static int light_direction(struct gpio_chip *gc,unsigned off)
{unsigned long flags;unsigned state;int ok;(void)gc;if(off)return -EINVAL;raw_spin_lock_irqsave(&light_lock,flags);ok=qil_mode(&light_io,&state);raw_spin_unlock_irqrestore(&light_lock,flags);return ok?0:1;}
static int light_output(struct gpio_chip *gc,unsigned off,int on)
{unsigned long flags;int ok;if(off||(on!=0&&on!=1))return -EINVAL;raw_spin_lock_irqsave(&light_lock,flags);ok=qil_line(&light_io,gc==&chips[1],on);raw_spin_unlock_irqrestore(&light_lock,flags);return ok?0:-EIO;}
static int light_input(struct gpio_chip *gc,unsigned off)
{(void)gc;(void)off;return -EOPNOTSUPP;}
static int light_get(struct gpio_chip *gc,unsigned off)
{unsigned long flags;unsigned state;int ok;if(off)return -EINVAL;raw_spin_lock_irqsave(&light_lock,flags);ok=qil_mode(&light_io,&state);raw_spin_unlock_irqrestore(&light_lock,flags);return ok?state==(gc==&chips[1]?QIL_IR_ON:QIL_WHITE_ON):-EIO;}
static const char * const white_names[]={"white-led"},* const ir_names[]={"ir-led"};
static int __init light_init(void)
{
 int rc;if(!of_machine_is_compatible("ingenic,t23"))return -ENODEV;
#ifndef QC5_ILLUMINATION_EMULATED
 bank=ioremap(QCB_GPIO_B,0x100);if(!bank)return -ENOMEM;
#endif
 for(unsigned n=0;n<2;n++){
  chips[n]=(struct gpio_chip){.label=n?"qc5-ir-led":"qc5-white-led",.owner=THIS_MODULE,.base=-1,.ngpio=1,.names=n?ir_names:white_names,.request=light_request,.get_direction=light_direction,.direction_output=light_output,.direction_input=light_input,.get=light_get,.set=light_output,.can_sleep=false};
  rc=gpiochip_add_data(&chips[n],NULL);if(rc){if(n)gpiochip_remove(&chips[0]);goto fail;}
 }return 0;
fail:
#ifndef QC5_ILLUMINATION_EMULATED
 iounmap(bank);bank=NULL;
#endif
 return rc;
}
static void __exit light_exit(void)
{gpiochip_remove(&chips[1]);gpiochip_remove(&chips[0]);
#ifndef QC5_ILLUMINATION_EMULATED
 iounmap(bank);bank=NULL;
#endif
}
module_init(light_init);module_exit(light_exit);
MODULE_DESCRIPTION("QC5 full-brightness white/IR gates with shared output and mutual exclusion");
MODULE_LICENSE("GPL");
