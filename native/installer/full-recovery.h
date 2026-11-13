/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QC5_FULL_RECOVERY_H
#define QC5_FULL_RECOVERY_H
typedef unsigned char u8;typedef unsigned int u32;typedef unsigned long long u64;typedef unsigned long usize;
struct qfr_io {
 void *context;
 int (*read)(void *,u32 address,u32 bytes,u8 *);
 int (*erase)(void *,u32 address,u32 bytes);
 int (*program)(void *,u32 address,u32 bytes,const u8 *);
 void (*progress)(void *,u32 phase,u32 address);
};
enum {QFR_RESTORED=1,QFR_ALREADY_RESTORED=2,QFR_BAD_PACKAGE=-1,QFR_BAD_UNIT=-2,QFR_READ=-3,QFR_ERASE=-4,QFR_PROGRAM=-5,QFR_READBACK=-6};
#define QFR_BEGIN 0x40000u
#define QFR_END 0x790000u
#define QFR_ERASE_BYTES 0x8000u
#define QFR_PACKAGE_BYTES (512u+QFR_END-QFR_BEGIN)
int qfr_restore(const u8 *,u32,const struct qfr_io *,u8 *,u32);
int qtu_verify(const u8 *,u32,const u8 *,u32,const struct qfr_io *,u8 *,u32);
int qtu_apply(const u8 *,u32,const u8 *,u32,const struct qfr_io *,u8 *,u32);
#endif
