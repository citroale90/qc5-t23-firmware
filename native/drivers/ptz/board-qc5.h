/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef QC5_NATIVE_BOARD_H
#define QC5_NATIVE_BOARD_H
#define QCB_GPIO_BASE 0x10010000u
#define QCB_GPIO_STRIDE 0x1000u
#define QCB_GPIO_B (QCB_GPIO_BASE+QCB_GPIO_STRIDE)
#define QCB_I2C2 0x10052000u
#define QCB_MOTOR_ADDRESS 0x10u
#define QCB_MOTOR_ID 0x53u
#define QCB_PAN_MAX 8000u
#define QCB_TILT_MAX 2500u
#define QCB_WHITE_PIN 18u
/* IR LED GPIO deliberately has no guessed default. The filter is on the
 * motor-controller H bridge register0x16, independently of LED illumination. */
#endif
