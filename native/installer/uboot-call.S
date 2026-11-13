/* SPDX-License-Identifier: GPL-3.0-only
 * Enter audited PIC U-Boot routines from a no-abicalls standalone loader.
 * U-Boot prologues derive $gp from $t9. A plain jalr without setting $t9
 * silently addresses the wrong GOT; mocked function callbacks hid this.
 */
.set noreorder
.text
.globl q73_uboot_call
.ent q73_uboot_call
q73_uboot_call:
 addiu $sp,$sp,-32
 sw $ra,28($sp)
 sw $gp,24($sp)
 move $t9,$a0
 move $a0,$a1
 move $a1,$a2
 move $a2,$a3
 lw $a3,48($sp)
 jalr $t9
 nop
 lw $gp,24($sp)
 lw $ra,28($sp)
 addiu $sp,$sp,32
 jr $ra
 nop
.end q73_uboot_call
