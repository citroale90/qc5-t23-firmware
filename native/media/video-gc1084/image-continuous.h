/* SPDX-License-Identifier: GPL-3.0-only
 * Statistics may saturate; the image-control decisions must keep running.
 * No allocations, frame copies or changes to native sensor/register tables.
 */
#ifndef QC5_IMAGE_CONTINUOUS_H
#define QC5_IMAGE_CONTINUOUS_H
#include <stdint.h>
static uint32_t qc5_image_inc(uint32_t value)
{return value==UINT32_MAX?value:value+1;}
static uint32_t qc5_image_note_count(uint32_t value)
{return value>248?248:value;}
#endif
