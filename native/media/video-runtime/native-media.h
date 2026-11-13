/* SPDX-License-Identifier: GPL-3.0-only
 * Reuse the existing WebUI muxer on the native encoder's owned access unit.
 * All pieces are consumed synchronously. There is no second video buffer,
 * worker, encoder, reference chain, or PPPP session in this adapter.
 */
#ifndef QC5_NATIVE_MEDIA_H
#define QC5_NATIVE_MEDIA_H
#include "producer.h"
#include "qc5_mp4.h"
struct qc5_native_media {
 struct qc5_mp4 mux;
 uint64_t serial,last_us,delivered,waiting;
 unsigned failed;
};
static int qnm_start(struct qc5_native_media *s,qc5_mp4_sink sink,void *opaque)
{
 if(!s||!sink)return 0;
 memset(s,0,sizeof *s);
 if(qc5_mp4_init(&s->mux,0,25,sink,opaque))return 0;
 s->mux.live_avc1=1;return 1;
}
static int qnm_frame(struct qc5_native_media *s,const struct qc5_frame_output *f)
{
 if(!s||s->failed)return 0;
 if(!f||!f->data||!f->bytes||f->bytes>QC5_MP4_NATIVE_AU_MAX||f->idr>1||!f->serial||!f->captured_us||
    (s->serial&&(s->serial==UINT64_MAX||f->serial!=s->serial+1||f->captured_us<=s->last_us)))goto failed;
 if(!s->mux.started&&!f->idr){
  if(s->waiting==UINT64_MAX)goto failed;
  s->waiting++;s->serial=f->serial;s->last_us=f->captured_us;return 1;
 }
 /* The sensor/bitstream has a25fps clock. Presentation timestamps retain
  * actual capture time, including scheduler gaps; no frame is duplicated. */
 if(s->delivered==UINT64_MAX||s->mux.sequence==UINT32_MAX||qc5_mp4_video_native(&s->mux,f->data,f->bytes,f->captured_us/1000u,40)||!s->mux.have_video)goto failed;
 s->delivered++;s->serial=f->serial;s->last_us=f->captured_us;return 1;
failed:
 s->failed=1;return 0;
}
#endif
