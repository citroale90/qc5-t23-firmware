// SPDX-License-Identifier: GPL-2.0-only
/*
 * Read-only QC5 T23 serial flash bring-up.
 * Legacy single-phase PIO register layout from Ingenic's T23 BSP.
 */
#include <linux/io.h>
#include <linux/math64.h>
#include "sfc-clock.h"
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
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

/* These read-only diagnostics survive a failed platform probe. */
static unsigned int diag_stage, diag_jedec, diag_sr, diag_cdr;
static int diag_error;
module_param(diag_stage, uint, 0444);
module_param(diag_jedec, uint, 0444);
module_param(diag_sr, uint, 0444);
module_param(diag_cdr, uint, 0444);
module_param(diag_error, int, 0444);

struct qc5_sfc {
	void __iomem *base;
	void __iomem *cpm;
	struct mutex lock;
	struct mtd_info mtd;
};

static void qc5_sfc_stop(struct qc5_sfc *sfc)
{
	writel(SFC_STOP, sfc->base + SFC_TRIG);
	writel(SFC_FLUSH, sfc->base + SFC_TRIG);
	writel(SFC_IRQ_MASK, sfc->base + SFC_SCR);
}

/* Callers serialize transfers and only issue RDID or 24-bit READ. */
static int qc5_sfc_receive(struct qc5_sfc *sfc, u32 command, u32 address,
			   u8 *buffer, size_t len)
{
	void __iomem *base = sfc->base;
	u32 status;
	int ret;

	/* A read-only controller contract, including all internal call sites. */
	if (!buffer || !len || len > SZ_4K ||
	    (command != SFC_RDID && command != (SFC_READ | (3U << SFC_ADDR_WIDTH_SHIFT))) ||
	    (command == SFC_RDID && (address || len != 3)) ||
	    (command != SFC_RDID && (address >= SZ_8M || len > SZ_8M - address)))
		return -EINVAL;

	qc5_sfc_stop(sfc);
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

static int qc5_sfc_read(struct mtd_info *mtd, loff_t from, size_t len,
			size_t *retlen, u_char *buffer)
{
	struct qc5_sfc *sfc = container_of(mtd, struct qc5_sfc, mtd);
	int ret = 0;

	*retlen = 0;
	if (from < 0 || from > mtd->size || len > mtd->size - from)
		return -EINVAL;

	mutex_lock(&sfc->lock);
	while (len) {
		size_t chunk = min_t(size_t, len, SZ_4K);

		ret = qc5_sfc_receive(sfc, SFC_READ | (3U << SFC_ADDR_WIDTH_SHIFT),
				      from, buffer, chunk);
		if (ret)
			break;
		from += chunk;
		buffer += chunk;
		len -= chunk;
		*retlen += chunk;
	}
	mutex_unlock(&sfc->lock);
	return ret;
}

static int qc5_sfc_prepare_clock(struct qc5_sfc *sfc)
{
    u32 cdr,gate,busy,planned;
    sfc->cpm=devm_ioremap(sfc->mtd.dev.parent,0x10000000,SZ_4K);
    if (!sfc->cpm) return -ENOMEM;
    cdr=readl(sfc->cpm+0x74);gate=readl(sfc->cpm+0x20);
    if (!qc5_sfc_clock_plan(readl(sfc->cpm+0x14),cdr,&planned))return -EINVAL;
    if (readl_poll_timeout(sfc->cpm+0x74,busy,!(busy&BIT(27)),1,20000))return -ETIMEDOUT;
    /* Only SFC clock and gate. No PLL, DDR, UART, GPIO or flash commands. */
    writel(gate&~BIT(20),sfc->cpm+0x20);
    writel(planned|BIT(28),sfc->cpm+0x74);
    if (readl_poll_timeout(sfc->cpm+0x74,busy,!(busy&BIT(27)),1,20000))return -ETIMEDOUT;
    writel(planned,sfc->cpm+0x74);
    diag_cdr=readl(sfc->cpm+0x74);
    return ((readl(sfc->cpm+0x74)^planned)&0xfc0000ffu)?-EIO:0;
}
static const struct mtd_partition qc5_readonly_parts[] = {
    {.name="qc5-native-runtime",.offset=0x4f8000,.size=0x298000,.mask_flags=MTD_WRITEABLE},
    {.name="qc5-factory-param",.offset=0x7c0000,.size=0x40000,.mask_flags=MTD_WRITEABLE},
};
static int qc5_sfc_probe(struct platform_device *pdev)
{

	struct device *dev = &pdev->dev;
	struct qc5_sfc *sfc;
	u8 jedec[3] = {0};
	int ret;

	sfc = devm_kzalloc(dev, sizeof(*sfc), GFP_KERNEL);
	if (!sfc)
		return -ENOMEM;
	sfc->base = devm_ioremap_resource(dev, platform_get_resource(pdev, IORESOURCE_MEM, 0));
	if (IS_ERR(sfc->base))
		return PTR_ERR(sfc->base);
	mutex_init(&sfc->lock);
	diag_stage=1;
	sfc->mtd.dev.parent=dev;
	ret=qc5_sfc_prepare_clock(sfc);
	if(ret){diag_error=ret;return ret;}
	diag_stage=2;

	writel(SFC_IRQ_MASK, sfc->base + SFC_INTC);
	qc5_sfc_stop(sfc);
	writel(0, sfc->base + SFC_CGE);
	/* Inactive CS, HOLD and WP. Single-bit SPI and one PIO phase. */
	writel(GENMASK(2, 0), sfc->base + SFC_DEV_CONF);
	/* Native BSP RDID uses one FIFO word, including partial transfers. */
	writel((1U << SFC_THRESHOLD_SHIFT) | (1U << SFC_PHASE_COUNT_SHIFT),
	       sfc->base + SFC_GLB);

	ret = qc5_sfc_receive(sfc, SFC_RDID, 0, jedec, sizeof(jedec));
	diag_jedec=(jedec[0]<<16)|(jedec[1]<<8)|jedec[2];
	if (ret) {
		diag_error=ret;
		dev_err(dev, "SFC JEDEC read failed: %d\n", ret);
		return ret;
	}
	/* Strict physical QC5 XM25QH64C ID. Emulator builds are separate. */
#ifdef QC5_SFC_EMULATOR_ONLY
    if (jedec[0] != 0xef || jedec[1] != 0x40 || jedec[2] != 0x18) {
#else
    if (jedec[0] != 0x20 || jedec[1] != 0x40 || jedec[2] != 0x17) {
#endif
		diag_error=-ENODEV;
		dev_err(dev, "Unexpected JEDEC %*phN\n", (int)sizeof(jedec), jedec);
		return -ENODEV;
	}
	diag_stage=3;

	sfc->mtd.name = "qc5-nor-ro";
	sfc->mtd.type = MTD_NORFLASH;
	sfc->mtd.flags = MTD_NO_ERASE; /* no MTD_WRITEABLE, no erase/program callbacks */
	sfc->mtd.size = SZ_8M;
	sfc->mtd.erasesize = SZ_32K;
	sfc->mtd.writesize = 1;
	sfc->mtd.writebufsize = 1;
	sfc->mtd.owner = THIS_MODULE;
	sfc->mtd.dev.parent = dev;
	sfc->mtd._read = qc5_sfc_read;
	platform_set_drvdata(pdev, sfc);
	dev_info(dev, "read-only PIO NOR, JEDEC %*phN, 8 MiB QC5 layout\n",
		 (int)sizeof(jedec), jedec);
	ret=mtd_device_register(&sfc->mtd, qc5_readonly_parts, ARRAY_SIZE(qc5_readonly_parts));
	diag_error=ret;
	if(!ret)diag_stage=5;
	return ret;
}

#ifdef QC5_SFC_LINUX44
static int qc5_sfc_remove(struct platform_device *pdev)
#else
static void qc5_sfc_remove(struct platform_device *pdev)
#endif
{
	struct qc5_sfc *sfc = platform_get_drvdata(pdev);

#ifdef QC5_SFC_LINUX44
	return mtd_device_unregister(&sfc->mtd);
#else
	mtd_device_unregister(&sfc->mtd);
#endif
}

static const struct of_device_id qc5_sfc_of_match[] = {
#ifdef QC5_SFC_LINUX44
	{ .compatible = "ingenic,sfc" },
#else
	{ .compatible = "ingenic,t23-sfc-qc5-ro" },
#endif
	{ }
};
MODULE_DEVICE_TABLE(of, qc5_sfc_of_match);

static struct platform_driver qc5_sfc_driver = {
	.probe = qc5_sfc_probe,
	.remove = qc5_sfc_remove,
	.driver = {
		.name = "qc5-t23-sfc-ro",
		.of_match_table = qc5_sfc_of_match,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(qc5_sfc_driver);

MODULE_DESCRIPTION("Read-only T23 QC5 SPI NOR diagnostic; no erase/program support");
MODULE_LICENSE("GPL");
