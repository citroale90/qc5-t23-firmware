/* SPDX-License-Identifier: GPL-3.0-only */
.set noreorder
.section .text.start,"ax",@progbits
.globl _start,__start
__start:
_start:
 lui $sp,0x80f0
 # Retain the caller's live U-Boot GP and capture its relocation offset.
 lui $t0,0x8014
 addiu $t0,$t0,-19808
 subu $t1,$gp,$t0
 lui $t0,%hi(q73_uboot_relocation)
 sw $t1,%lo(q73_uboot_relocation)($t0)
 jal qc5_sd_recovery_start
 nop
1:
 b 1b
 nop
