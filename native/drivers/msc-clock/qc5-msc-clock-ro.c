// SPDX-License-Identifier: GPL-2.0-or-later
/* Read the clock inherited from U-Boot. Never writes CPM or SD registers. */
#include <linux/io.h>
#include <linux/math64.h>
#include "msc-clock-plan.h"
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
struct qc5_clock {struct clk_hw *clock;};
static int qc5_probe(struct platform_device *pdev)
{
    void __iomem *p=ioremap(0x10000000,4096);
    struct qc5_clock *s;u32 pll,cdr,gate,gate1,rate;int error;
    if(!p)return -ENOMEM;
    pll=readl(p+0x14);cdr=readl(p+0x68);gate=readl(p+0x20);gate1=readl(p+0x28);iounmap(p);
    rate=qc5_msc_inherited_rate(pll,cdr,gate,gate1);if(!rate)return -EINVAL;
    s=devm_kzalloc(&pdev->dev,sizeof(*s),GFP_KERNEL);if(!s)return -ENOMEM;
    s->clock=clk_hw_register_fixed_rate(&pdev->dev,"qc5-msc-inherited",NULL,0,rate);
    if(IS_ERR(s->clock))return PTR_ERR(s->clock);
    error=of_clk_add_hw_provider(pdev->dev.of_node,of_clk_hw_simple_get,s->clock);
    if(error){clk_hw_unregister_fixed_rate(s->clock);return error;}
    platform_set_drvdata(pdev,s);dev_info(&pdev->dev,"read-only inherited MSC clock %u Hz, CDR %08x\n",rate,cdr);return 0;
}
static void qc5_remove(struct platform_device *pdev)
{struct qc5_clock*s=platform_get_drvdata(pdev);of_clk_del_provider(pdev->dev.of_node);clk_hw_unregister_fixed_rate(s->clock);}
static const struct of_device_id qc5_ids[]={{.compatible="ingenic,t23-msc-inherited-clock"},{}};
MODULE_DEVICE_TABLE(of,qc5_ids);
static struct platform_driver qc5_driver={.probe=qc5_probe,.remove=qc5_remove,.driver={.name="qc5-msc-clock-ro",.of_match_table=qc5_ids,.probe_type=PROBE_FORCE_SYNCHRONOUS}};
module_platform_driver(qc5_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("QC5 inherited MSC clock provider, read-only");
