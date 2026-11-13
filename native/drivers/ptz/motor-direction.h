/* SPDX-License-Identifier: GPL-2.0-only
 * Logical UI direction0 means left/up;1 means right/down.
 * Exact OEM Motor_run_* -> VP_*axis_motor_solution conversion:
 * X command1/2 -> hardware0/1; Y command1/2 -> hardware1/0.
 * Keep position bookkeeping in logical coordinates; convert only at I/O.
 */
#ifndef QC5_MOTOR_DIRECTION_H
#define QC5_MOTOR_DIRECTION_H
static unsigned qmd_hardware_direction(unsigned axis,unsigned logical)
{return logical ^ (axis==1);}
#endif
