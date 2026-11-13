/* SPDX-License-Identifier: GPL-2.0-only */
.set noreorder
.set noat
.section .text.entry,"ax",@progbits
.globl _start,__start
__start:
_start:
 lui $t0,%hi(__bss_start)
 addiu $t0,$t0,%lo(__bss_start)
 lui $t1,%hi(__bss_end)
 addiu $t1,$t1,%lo(__bss_end)
1:
 beq $t0,$t1,2f
 nop
 sw $zero,0($t0)
 b 1b
 addiu $t0,$t0,4
2:
 lui $sp,%hi(__stack_top)
 addiu $sp,$sp,%lo(__stack_top)
 jal qfd_start
 nop
3:
 b 3b
 nop
