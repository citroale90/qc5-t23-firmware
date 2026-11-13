// SPDX-License-Identifier: GPL-2.0-only
/* Generated diagnostic extension of the unchanged RO host; default read-only. */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include "msc-write-pio.h"
#include "sd-volume.h"
static bool recording_writes;
static unsigned expected_cid[4];
module_param(recording_writes,bool,0444);

module_param_array(expected_cid,uint,NULL,0444);
MODULE_PARM_DESC(recording_writes,"Opt-in writes only to a twice-verified FAT32 volume on an exact CID");
struct qmr_host {struct mmc_host *mmc;struct clk *clk;void __iomem *base;struct qmr_io io;bool init,blockaddr,writable,cid_verified;struct qmw_permit permit;struct qsv_volume volume;unsigned card_sectors,cached_card_sectors;unsigned calls,rca,denied_reports;int fault;};
static unsigned qmr_reg_read(void *p,unsigned offset){return readl(((struct qmr_host *)p)->base+offset);}
static void qmr_reg_write(void *p,unsigned offset,unsigned value){writel(value,((struct qmr_host *)p)->base+offset);}
static unsigned long long qmr_clock(void *p){return div_u64(ktime_get_ns(),1000);}
static void qmr_pause(void *p){udelay(1);if(!in_atomic()&&!irqs_disabled())cond_resched();}
static int qmr_errno(int e){return e==QMR_TIMEOUT?-ETIMEDOUT:e==QMR_DENIED?-EROFS:e?-EIO:0;}
static void qmr_request(struct mmc_host *mmc,struct mmc_request *req)
{
    struct qmr_host *h=mmc_priv(mmc);struct mmc_command *cmd=req->cmd;struct mmc_data *data=req->data;
    unsigned bytes=data?data->blksz*data->blocks:0,format=0;u8 buffer[512];int status;
    if(++h->calls<=24)dev_info(mmc_dev(mmc),"QC5_MSC_CMD %u bytes=%u flags=%x\n",cmd->opcode,bytes,cmd->flags);
    if(h->fault){cmd->error=h->fault;goto done;}
    if(req->sbc){cmd->error=-EOPNOTSUPP;goto done;}
    if(data&&(!bytes||bytes>4096||!data->blocks||data->blocks>8)){cmd->error=-EROFS;goto done;}
    if(data&&(data->flags&MMC_DATA_WRITE)&&(!h->writable||!h->cid_verified||data->blksz!=512||
         !qsv_authorize(&h->volume,expected_cid,h->blockaddr?cmd->arg:cmd->arg/512,data->blocks,data->blksz))){
         if(h->denied_reports++<8)dev_warn(mmc_dev(mmc),"QC5_RECORD_DENIED writable=%u cid=%u capacity=%u stage=%u valid=%u lba=%u blocks=%u\n",h->writable,h->cid_verified,h->card_sectors,h->volume.stage,h->volume.valid,h->blockaddr?cmd->arg:cmd->arg/512,data->blocks);
         cmd->error=-EROFS;goto done;}
    if(cmd->opcode==0){if(h->volume.stage)dev_info(mmc_dev(mmc),"QC5_RECORD_GATE_RESET stage=%u valid=%u\n",h->volume.stage,h->volume.valid);h->cid_verified=false;h->card_sectors=0;memset(&h->volume,0,sizeof(h->volume));}
    if(cmd->flags&MMC_RSP_136)format=2;
    else if(cmd->flags&MMC_RSP_PRESENT)format=(cmd->flags&MMC_RSP_CRC)?1:3;
    if(cmd->flags&MMC_RSP_BUSY)format|=64;
    if(h->init){format|=128;h->init=false;}
    if(!data){
        status=qmr_command(&h->io,cmd->opcode,cmd->arg,format,0,cmd->resp);cmd->error=qmr_errno(status);
        if(!status&&cmd->opcode==41)h->blockaddr=!!(cmd->resp[0]&(1u<<30));
        if(!status&&cmd->opcode==3)h->rca=cmd->resp[0]&0xffff0000u;
        /* SDHC/SDXC CSDv2 capacity, same response word layout as MMC core.
         * Legacy CSDv1 remains read-only; a format is never guessed. */
        if(!status&&cmd->opcode==9&&(cmd->resp[0]>>30)==1){
            u64 sectors=((u64)(((cmd->resp[1]&0x3fu)<<16)|(cmd->resp[2]>>16))+1)*1024;
            h->card_sectors=sectors<=0xffffffffu?(unsigned)sectors:0;
            if(h->cid_verified)h->cached_card_sectors=h->card_sectors;
        }
        if(!status&&(cmd->opcode==2||cmd->opcode==10)){
            h->cid_verified=qmw_cid_matches(cmd->resp,expected_cid);
            if(h->cid_verified)h->card_sectors=h->cached_card_sectors;
            else{h->card_sectors=h->cached_card_sectors=0;memset(&h->volume,0,sizeof(h->volume));}
        }
    }else{
        unsigned long long started=qmr_clock(h);
        for(unsigned i=0;i<data->blocks;++i){
            unsigned opcode,address,offset;
            if(data->flags&MMC_DATA_WRITE){
                opcode=24;status=qmw_plan(cmd->opcode,cmd->arg,data->blksz,data->blocks,h->blockaddr,i,&h->permit,&address,&offset);
            }else status=qmr_chunk_plan(cmd->opcode,cmd->arg,data->blksz,data->blocks,h->blockaddr,i,&opcode,&address,&offset);
            if(status){cmd->error=qmr_errno(status);break;}
            if(qmr_clock(h)-started>2000000){data->error=-ETIMEDOUT;break;}
            if(data->flags&MMC_DATA_WRITE){
                if(sg_pcopy_to_buffer(data->sg,data->sg_len,buffer,data->blksz,offset)!=data->blksz){data->error=-EIO;break;}
                status=qmw_sector(&h->io,address,h->blockaddr,format,buffer,&h->permit,cmd->resp);
                data->error=qmr_errno(status);if(status)break;
            }else{
                status=qmr_command(&h->io,opcode,address,format,data->blksz,cmd->resp);cmd->error=qmr_errno(status);if(status)break;
                status=qmr_read(&h->io,buffer,data->blksz);data->error=qmr_errno(status);if(status)break;
                if(h->writable&&h->cid_verified&&h->card_sectors&&data->blksz==512){
                    unsigned lba=h->blockaddr?address:address/512;
                    if(!h->volume.card_sectors)qsv_init(&h->volume,expected_cid,h->card_sectors);
                    qsv_observe(&h->volume,expected_cid,lba,buffer);
                    h->permit=(struct qmw_permit){h->volume.first,h->volume.valid?h->volume.sectors:0};
                }
                if(sg_pcopy_from_buffer(data->sg,data->sg_len,buffer,data->blksz,offset)!=data->blksz){data->error=-EIO;break;}
            }
            data->bytes_xfered+=data->blksz;
        }
        /* CMD18 was split into completed CMD17 reads; there is no card-side
         * multiblock transfer left to stop. Read the actual ready R1 with
         * CMD13 instead of issuing an invalid CMD12 in transfer state. */
        if(req->stop&&!cmd->error&&!data->error){status=qmr_command(&h->io,13,h->rca,1,0,req->stop->resp);req->stop->error=qmr_errno(status);}
    }
done:
    if(cmd->error||(data&&data->error))dev_warn(mmc_dev(mmc),"QC5_MSC_CMD_END cmd=%u error=%d data_error=%d status=%08x flags=%08x\n",cmd->opcode,cmd->error,data?data->error:0,readl(h->base+4),readl(h->base+0x28));
    mmc_request_done(mmc,req);
}
static void qmr_set_ios(struct mmc_host *mmc,struct mmc_ios *ios)
{
    struct qmr_host *h=mmc_priv(mmc);unsigned divider=0,rate=clk_get_rate(h->clk);
    writel(1,h->base);
    int status=qmr_wait(&h->io,4,256,0,10000,0);
    if(status){h->fault=qmr_errno(status);dev_err(mmc_dev(mmc),"QC5_MSC_CLOCK_STOP error=%d\n",h->fault);return;}
    if(ios->power_mode==MMC_POWER_UP){h->fault=qmr_errno(qmr_reset(&h->io));h->init=true;if(h->fault)return;}
    if(!ios->clock)return;
    while(rate>ios->clock&&divider<7){rate>>=1;++divider;}
    writel(divider,h->base+8);writel(0,h->base+0x40);
    mmc->actual_clock=rate;
}
static int qmr_ro(struct mmc_host *mmc){struct qmr_host*h=mmc_priv(mmc);return !(h->writable&&h->cid_verified&&h->card_sectors);}
static int qmr_cd(struct mmc_host *mmc){return 1;}
static const struct mmc_host_ops qmr_ops={.request=qmr_request,.set_ios=qmr_set_ios,.get_ro=qmr_ro,.get_cd=qmr_cd};
static ssize_t record_volume_ready_show(struct device *dev,struct device_attribute *attr,char *buf)
{
    struct qmr_host *h=dev_get_drvdata(dev);(void)attr;
    return sysfs_emit(buf,"%u\n",!!(READ_ONCE(h->writable)&&READ_ONCE(h->cid_verified)&&
        READ_ONCE(h->volume.valid)&&READ_ONCE(h->volume.stage)==4));
}
static DEVICE_ATTR_RO(record_volume_ready);
static struct attribute *record_gate_attrs[]={&dev_attr_record_volume_ready.attr,NULL};
ATTRIBUTE_GROUPS(record_gate);
static int qmr_probe(struct platform_device *pdev)
{
    struct mmc_host *mmc=devm_mmc_alloc_host(&pdev->dev,sizeof(struct qmr_host));struct qmr_host*h;int error;
    if(!mmc)return -ENOMEM;
    h=mmc_priv(mmc);h->mmc=mmc;
    memset(&h->permit,0,sizeof(h->permit));
    h->writable=recording_writes&&of_property_read_bool(pdev->dev.of_node,"ingenic,allow-recording-writes");
    if(h->writable&&!qmw_cid_matches(expected_cid,expected_cid))return -EINVAL;
    h->base=devm_platform_ioremap_resource(pdev,0);if(IS_ERR(h->base))return PTR_ERR(h->base);
    h->clk=devm_clk_get(&pdev->dev,"mmc");if(IS_ERR(h->clk))return PTR_ERR(h->clk);
    error=clk_prepare_enable(h->clk);if(error)return error;
    h->io=(struct qmr_io){h,qmr_reg_read,qmr_reg_write,qmr_clock,qmr_pause};
    dev_info(&pdev->dev,"QC5_MSC_PROBE reset, 32-bit PIO, no IRQ/DMA, read-only\n");
    error=qmr_errno(qmr_reset(&h->io));if(error)goto disable;
    error=mmc_of_parse(mmc);if(error)goto disable;
    mmc->ops=&qmr_ops;mmc->f_min=clk_get_rate(h->clk)/128;mmc->f_max=24000000;
    mmc->ocr_avail=MMC_VDD_32_33|MMC_VDD_33_34;
    mmc->caps2|=MMC_CAP2_NO_SDIO|MMC_CAP2_NO_MMC;
    static_assert(PAGE_SIZE==4096);
    mmc->max_blk_size=512;mmc->max_blk_count=8;mmc->max_req_size=4096;mmc->max_seg_size=4096;mmc->max_segs=1;
    mmc->max_busy_timeout=500;
    platform_set_drvdata(pdev,h);error=mmc_add_host(mmc);if(error)goto disable;error=devm_device_add_group(&pdev->dev,&record_gate_group);if(error){mmc_remove_host(mmc);goto disable;}
    dev_info(&pdev->dev,"QC5_MSC_PROBE registered, clock %lu Hz\n",clk_get_rate(h->clk));return 0;
disable:clk_disable_unprepare(h->clk);return error;
}
static void qmr_remove(struct platform_device*pdev)
{struct qmr_host*h=platform_get_drvdata(pdev);dev_info(&pdev->dev,"QC5_SD_REMOVE before_mmc_remove_host\n");mmc_remove_host(h->mmc);dev_info(&pdev->dev,"QC5_SD_REMOVE after_mmc_remove_host\n");writel(~0u,h->base+0x24);writel(~0u,h->base+0x28);writel(0,h->base+0x44);writel(1,h->base);dev_info(&pdev->dev,"QC5_SD_REMOVE before_clock_disable\n");clk_disable_unprepare(h->clk);dev_info(&pdev->dev,"QC5_SD_REMOVE done\n");}
static const struct of_device_id qmr_ids[]={{.compatible="ingenic,t23-mmc-recording-pio"},{}};
MODULE_DEVICE_TABLE(of,qmr_ids);
static struct platform_driver qmr_driver={.probe=qmr_probe,.remove=qmr_remove,.driver={.name="qc5-mmc-recording",.of_match_table=qmr_ids,.probe_type=PROBE_FORCE_SYNCHRONOUS}};
module_platform_driver(qmr_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("T23 volume-limited FAT32 PIO MMC host, default read-only, exact CID and metadata gates");
