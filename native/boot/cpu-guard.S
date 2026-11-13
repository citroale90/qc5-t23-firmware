/* SPDX-License-Identifier: GPL-3.0-only
 * Only the audited native CPU request, exact EPC/magic/index. All ordinary
 * TLS syscalls go to Linux. Retire the former raw-SD diagnostic entry.
 */
.set noreorder
.section .text.trampoline,"ax",@progbits
.globl qc5_wifi_marker
qc5_wifi_marker:
 lw $t0,180($sp)
 lw $t1,160($sp)
 andi $t1,$t1,0x18
 addiu $t2,$zero,0x10
 bne $t1,$t2,9f
 nop
 addiu $t1,$zero,12
 bne $a0,$t1,9f
 lui $t1,((QC5_H264_MARKER_PC >> 16) & 0xffff)
 ori $t1,$t1,(QC5_H264_MARKER_PC & 0xffff)
 bne $t0,$t1,9f
 lui $t1,0x5143
 ori $t1,$t1,0x5746
 bne $a1,$t1,9f
 nop
 j qc5_kernel_checkpoint
 nop
9:
 j qc5_set_thread_area
 nop
