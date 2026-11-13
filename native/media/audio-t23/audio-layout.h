/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared reservation contract only; contains no vendor codec implementation.
 */
#ifndef QC5_AUDIO_LAYOUT_H
#define QC5_AUDIO_LAYOUT_H
#include <stdint.h>
#define QA_RATE 16000u
#define QA_SAMPLES 32000u
#define QA_BYTES (QA_SAMPLES*2u)
#ifdef QC5_AUDIO_TAIL
/* Drain beyond the exported prefix while userspace observes its end. This
 * margin occupies existing reserved bytes; the final 512-byte guard stays.
 */
#define QA_RX_MARGIN 1024u
#else
#define QA_RX_MARGIN 0u
#endif
#define QA_DMA_BYTES (QA_BYTES+QA_RX_MARGIN)
#define QA_BASE UINT32_C(0x01cee000)
#define QA_DESCRIPTOR QA_BASE
#define QA_PCM (QA_BASE+4096u)
#define QA_END UINT32_C(0x01cff000)
#define QA_SIZE (QA_END-QA_BASE)
static int qa_owns_address(uint32_t a)
{return (a>=QA_DESCRIPTOR&&a-QA_DESCRIPTOR<32u)||(a>=QA_PCM&&a-QA_PCM<QA_DMA_BYTES);}
_Static_assert(QA_DMA_BYTES%16==0&&QA_PCM+QA_DMA_BYTES+512<=QA_END,"PCM, drain margin and tail guard fit");
#endif
