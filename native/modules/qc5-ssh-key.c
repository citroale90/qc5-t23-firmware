// SPDX-License-Identifier: GPL-2.0-only
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mutex.h>
#include <linux/sizes.h>

#define SFC_GLB			0x0000
#define SFC_DEV_CONF		0x0004
#define SFC_TRAN_CONF		0x0014
#define SFC_TRAN_LEN		0x002c
#define SFC_DEV_ADDR		0x0030
#define SFC_DEV_ADDR_PLUS	0x0048
#define SFC_TRIG		0x0064
#define SFC_SR			0x0068
#define SFC_SCR			0x006c
#define SFC_INTC		0x0070
#define SFC_CGE			0x0078
#define SFC_DR			0x1000
#define SFC_THRESHOLD_SHIFT	7
#define SFC_PHASE_COUNT_SHIFT	3
#define SFC_ADDR_WIDTH_SHIFT	26
#define SFC_COMMAND_EN		BIT(24)
#define SFC_DATA_EN		BIT(16)
#define SFC_START		BIT(0)
#define SFC_STOP		BIT(1)
#define SFC_FLUSH		BIT(2)
#define SFC_ERRORS		GENMASK(1, 0)
#define SFC_END			BIT(4)
#define SFC_FIFO_WORDS		GENMASK(22, 16)
#define SFC_FIFO_WORDS_SHIFT	16
#define SFC_IRQ_MASK		GENMASK(4, 0)
#define SFC_TIMEOUT_US		20000
#define SFC_RDID		0x9f
#define SFC_READ		0x03
#define SFC_WRITE_DIR BIT(13)
#define SFC_TX_REQ BIT(3)
#define CONFIG_BASE 0x4f2000u
#define CONFIG_SIZE 0x2000u
static bool key_writes;
module_param(key_writes,bool,0400);
static unsigned diag_sr,diag_flash_status,diag_step;
static int diag_result;
module_param(diag_flash_status,uint,0444);
module_param(diag_step,uint,0444);
module_param(diag_result,int,0444);
/* Exact prefix of the hash-pinned original read-only driver's private data.
 * Do not alter its geometry, callbacks, clock or registered partitions. */
struct qc5_sfc {void __iomem *base,*cpm;struct mutex lock;struct mtd_info mtd;};
static struct mtd_info *view,*master,config;
static struct qc5_sfc *chip;
static void qc5_sfc_stop(struct qc5_sfc *sfc)
{
	writel(SFC_STOP, sfc->base + SFC_TRIG);
	writel(SFC_FLUSH, sfc->base + SFC_TRIG);
	writel(SFC_IRQ_MASK, sfc->base + SFC_SCR);
}
static int qc5_sfc_receive(struct qc5_sfc *sfc, u32 command, u32 address,
			   u8 *buffer, size_t len)
{
	void __iomem *base = sfc->base;
	u32 status;
	int ret;

	/* A read-only controller contract, including all internal call sites. */
	if (!buffer || !len || len > SZ_4K ||
	    (command != SFC_RDID && command != 5 && command != (SFC_READ | (3U << SFC_ADDR_WIDTH_SHIFT))) ||
	    (command == SFC_RDID && (address || len != 3)) ||
	    (command == 5 && (address || len != 1)) ||
	    (command != SFC_RDID && command != 5 && (address >= SZ_8M || len > SZ_8M - address)))
		return -EINVAL;

	qc5_sfc_stop(sfc);
	writel(readl(base+SFC_GLB)&~SFC_WRITE_DIR,base+SFC_GLB);
	writel(address, base + SFC_DEV_ADDR);
	writel(0, base + SFC_DEV_ADDR_PLUS);
	writel(len, base + SFC_TRAN_LEN);
	writel(command | SFC_COMMAND_EN | SFC_DATA_EN, base + SFC_TRAN_CONF);
	writel(SFC_START, base + SFC_TRIG);

	while (len) {
		unsigned int words;

		ret = readl_poll_timeout(base + SFC_SR, status,
					status & (SFC_FIFO_WORDS | SFC_ERRORS),
					1, SFC_TIMEOUT_US);
		if (ret)
			goto out;
		words = (status & SFC_FIFO_WORDS) >> SFC_FIFO_WORDS_SHIFT;
		if (!words || (status & SFC_ERRORS)) {
			ret = -EIO;
			goto out;
		}
		while (words-- && len) {
			__le32 data = cpu_to_le32(readl(base + SFC_DR));
			size_t bytes = min(len, sizeof(data));

			memcpy(buffer, &data, bytes);
			buffer += bytes;
			len -= bytes;
		}
	}

	ret = readl_poll_timeout(base + SFC_SR, status,
				status & (SFC_END | SFC_ERRORS), 1, SFC_TIMEOUT_US);
	if (!ret && (status & SFC_ERRORS))
		ret = -EIO;
out:
	diag_sr = readl(base + SFC_SR);
	qc5_sfc_stop(sfc);
	return ret;
}
static bool qc5_config_range(loff_t at,size_t len)
{return key_writes && at>=CONFIG_BASE && len && at<CONFIG_BASE+CONFIG_SIZE && len<=CONFIG_BASE+CONFIG_SIZE-at;}
static int qc5_sfc_send(struct qc5_sfc *sfc,u32 command,u32 at,const u8 *data,size_t len)
{
 void __iomem *b=sfc->base;u32 status;int ret;
 if(!key_writes || (command!=6 && command!=4 && command!=0x20 && command!=2) ||
    ((command==6||command==4)&&(at||len||data)) ||
    (command==0x20&&(!qc5_config_range(at,4096)||(at&4095)||len||data)) ||
    (command==2&&(!qc5_config_range(at,len)||!data||len>16||(at&255)+len>256)))return -EPERM;
 qc5_sfc_stop(sfc);writel(readl(b+SFC_GLB)|SFC_WRITE_DIR,b+SFC_GLB);
 writel(at,b+SFC_DEV_ADDR);writel(0,b+SFC_DEV_ADDR_PLUS);writel(len,b+SFC_TRAN_LEN);
 writel(command|SFC_COMMAND_EN|((command==2||command==0x20)?3u<<SFC_ADDR_WIDTH_SHIFT:0)|(len?SFC_DATA_EN:0),b+SFC_TRAN_CONF);
 writel(SFC_START,b+SFC_TRIG);
 if(len){
  ret=readl_poll_timeout(b+SFC_SR,status,status&(SFC_TX_REQ|SFC_ERRORS),1,SFC_TIMEOUT_US);
  if(ret || (status&SFC_ERRORS)){if(!ret)ret=-EIO;goto out;}
  writel(SFC_TX_REQ,b+SFC_SCR);
  while(len){u32 word=0xffffffff;size_t n=min_t(size_t,len,4);memcpy(&word,data,n);writel(le32_to_cpu((__force __le32)word),b+SFC_DR);data+=n;len-=n;}
 }
 ret=readl_poll_timeout(b+SFC_SR,status,status&(SFC_END|SFC_ERRORS),1,SFC_TIMEOUT_US);
 if(!ret&&(status&SFC_ERRORS))ret=-EIO;
 out:qc5_sfc_stop(sfc);writel(readl(b+SFC_GLB)&~SFC_WRITE_DIR,b+SFC_GLB);return ret;
}
static int qc5_ready(struct qc5_sfc *sfc)
{
 u8 sr;for(unsigned n=0;n<3000;n++){int rc=qc5_sfc_receive(sfc,5,0,&sr,1);if(rc)return rc;if(!(sr&1))return 0;usleep_range(1000,1500);}return -ETIMEDOUT;
}
static int qc5_write_enable(struct qc5_sfc *sfc)
{
 u8 sr;int ret=qc5_ready(sfc);if(ret)return ret;
 ret=qc5_sfc_send(sfc,6,0,NULL,0);if(ret)return ret;
 ret=qc5_sfc_receive(sfc,5,0,&sr,1);diag_flash_status=sr;return ret?ret:(sr&3)==2?0:-EROFS;
}
static int qc5_sfc_write(struct mtd_info *mtd,loff_t at,size_t len,size_t *retlen,const u_char *data)
{
 struct qc5_sfc *sfc=container_of(mtd,struct qc5_sfc,mtd);int ret=0;*retlen=0;
 if(!qc5_config_range(at,len))return -EPERM;
 mutex_lock(&sfc->lock);
 while(len){size_t n=min_t(size_t,len,16);n=min_t(size_t,n,256-(at&255));
  ret=qc5_write_enable(sfc);if(ret)break;
  ret=qc5_sfc_send(sfc,2,at,data,n);if(ret)break;
  ret=qc5_ready(sfc);if(ret)break;
  at+=n;data+=n;len-=n;*retlen+=n;
 }
 if(!ret)ret=qc5_sfc_send(sfc,4,0,NULL,0);
 mutex_unlock(&sfc->lock);return ret;
}
static int qc5_sfc_erase(struct mtd_info *mtd,struct erase_info *info)
{
 struct qc5_sfc *sfc=container_of(mtd,struct qc5_sfc,mtd);int ret=0;
 if(!qc5_config_range(info->addr,info->len)||(info->addr&4095)||(info->len&4095))return -EPERM;
 mutex_lock(&sfc->lock);
 for(u64 at=info->addr;at<info->addr+info->len;at+=4096){
  ret=qc5_write_enable(sfc);if(ret)break;
  ret=qc5_sfc_send(sfc,0x20,at,NULL,0);if(ret)break;
  ret=qc5_ready(sfc);if(ret)break;
 }
 if(!ret)ret=qc5_sfc_send(sfc,4,0,NULL,0);
 mutex_unlock(&sfc->lock);return ret;
}
static int cfg_read(struct mtd_info*m,loff_t at,size_t n,size_t*done,u_char*b)
{if(at<0||at>CONFIG_SIZE||n>CONFIG_SIZE-at)return -EINVAL;return mtd_read(master,CONFIG_BASE+at,n,done,b);}
static int cfg_write(struct mtd_info*m,loff_t at,size_t n,size_t*done,const u_char*b)
{if(at<0||at>CONFIG_SIZE||n>CONFIG_SIZE-at)return -EINVAL;diag_step=1;diag_result=qc5_sfc_write(master,CONFIG_BASE+at,n,done,b);diag_step=2;return diag_result;}
static int cfg_erase(struct mtd_info*m,struct erase_info*i)
{struct erase_info e=*i;if(e.addr>CONFIG_SIZE||e.len>CONFIG_SIZE-e.addr)return -EINVAL;e.addr+=CONFIG_BASE;diag_step=3;diag_result=qc5_sfc_erase(master,&e);diag_step=4;return diag_result;}
static int __init cfg_init(void)
{
 u8 id[3];int ret;view=get_mtd_device_nm("qc5-native-runtime");if(IS_ERR(view))return PTR_ERR(view);
 master=mtd_get_master(view);
 if(master->size!=SZ_8M||master->erasesize!=SZ_32K||master->writesize!=1||(master->flags&MTD_WRITEABLE)){ret=-EINVAL;goto fail;}
 chip=container_of(master,struct qc5_sfc,mtd);
 if((unsigned long)chip->base!=0xb3440000ul||(unsigned long)chip->cpm!=0xb0000000ul){ret=-EINVAL;goto fail;}
 mutex_lock(&chip->lock);ret=qc5_sfc_receive(chip,SFC_RDID,0,id,3);mutex_unlock(&chip->lock);
 if(ret||id[0]!=0x20||id[1]!=0x40||id[2]!=0x17){ret=-ENODEV;goto fail;}
 config.name="qc5-ssh-key";config.type=MTD_NORFLASH;config.flags=key_writes?MTD_CAP_NORFLASH:MTD_NO_ERASE;
 config.size=CONFIG_SIZE;config.erasesize=SZ_4K;config.writesize=1;config.writebufsize=1;config.owner=THIS_MODULE;config.dev.parent=&view->dev;
 config._read=cfg_read;if(key_writes){config._write=cfg_write;config._erase=cfg_erase;}
 ret=mtd_device_register(&config,NULL,0);if(!ret)return 0;
 fail:put_mtd_device(view);return ret;
}
static void __exit cfg_exit(void){mtd_device_unregister(&config);put_mtd_device(view);}
module_init(cfg_init);module_exit(cfg_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("QC5 bounded SSH host-key banks; original read-only driver and shared mutex retained");
