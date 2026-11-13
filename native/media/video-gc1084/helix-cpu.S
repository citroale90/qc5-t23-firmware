/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed, ELF-audited call site. The guard permits only action 12 here.
 */
    .set noreorder
    .text
    .globl qc5_helix_cpu_request
    .type qc5_helix_cpu_request,@function
qc5_helix_cpu_request:
    addiu $a0,$zero,12
    lui $a1,0x5143
    ori $a1,$a1,0x5746
    addiu $v0,$zero,4283
    .globl qc5_helix_cpu_syscall
qc5_helix_cpu_syscall:
    syscall
    jr $ra
     nop
    .size qc5_helix_cpu_request,.-qc5_helix_cpu_request
