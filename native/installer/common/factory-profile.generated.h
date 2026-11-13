/* SPDX-License-Identifier: GPL-3.0-only
 * Supported V82 flash layout, independent of the ROOT/SYSTEM software build.
 * U-Boot code/kernel transport pins, NOR geometry, paired snapshots and the
 * per-unit PARAM decoder are validated separately. Component compatibility
 * remains scoped to T23DL / GC1084 / AIC8800DC USB; no generic T23 claim.
 */
#ifndef QC5_FACTORY_LAYOUT_H
#define QC5_FACTORY_LAYOUT_H
#define QC5_ROOT_BEFORE_CONFIG_BYTES 0x358000u
/* OEM images pad unused space either with erased NOR or with zeroes. Do not
 * accept populated configuration, mixed data or filesystem-owned bytes. */
static int qc5_factory_unused_bank(const unsigned char *p,unsigned n)
{
 if(!n || (p[0]!=0 && p[0]!=255))return 0;
 unsigned char fill=p[0];
 for(unsigned i=1;i<n;i++)if(p[i]!=fill)return 0;
 return 1;
}
#endif
