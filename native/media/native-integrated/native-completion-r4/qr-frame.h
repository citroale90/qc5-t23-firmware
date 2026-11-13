/* SPDX-License-Identifier: GPL-3.0-only
 * One grayscale setup frame, never a live encoder queue. Release/acquire
 * state protects pixels and timestamps across the independent QR process.
 */
#ifndef QC5_QR_FRAME_H
#define QC5_QR_FRAME_H
#include <stdatomic.h>
#include <stdint.h>
#define QQ_WIDTH 640u
#define QQ_HEIGHT 360u
#define QQ_FRAME_NAME "qr-frame"
#define QQ_MAGIC 0x34525151u
struct qq_frame {uint32_t magic;_Atomic uint32_t state;uint32_t stamp_lo,stamp_hi;uint8_t pixels[QQ_WIDTH*QQ_HEIGHT];};
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"QR handshake must not use a process-local lock");
static int qq_fill(struct qq_frame *f,const uint8_t *y,unsigned w,unsigned h,unsigned stride,uint64_t us)
{
 if(!f||!y||w!=1280||h!=720||stride!=1280||!us||f->magic!=QQ_MAGIC||atomic_load_explicit(&f->state,memory_order_acquire))return 0;
 for(unsigned row=0;row<QQ_HEIGHT;row++)for(unsigned col=0;col<QQ_WIDTH;col++)f->pixels[row*QQ_WIDTH+col]=y[(row*2)*stride+col*2];
 f->stamp_lo=(uint32_t)us;f->stamp_hi=(uint32_t)(us>>32);atomic_store_explicit(&f->state,1,memory_order_release);return 1;
}
#endif
