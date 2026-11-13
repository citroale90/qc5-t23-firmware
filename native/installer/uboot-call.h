/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QC5_UBOOT_CALL_H
#define QC5_UBOOT_CALL_H
#include "full-recovery.h"
extern u32 q73_uboot_relocation;
int q73_uboot_call(u32 target,u32 a0,u32 a1,u32 a2,u32 a3);
static int q73_context_valid(void)
{return q73_uboot_relocation==0||q73_uboot_relocation==0x01e90000u;}
static int q73_command(const char *text)
{return q73_uboot_call(0x80110484u+q73_uboot_relocation,(u32)text,0,0,0);}
#endif
