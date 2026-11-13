#ifndef QC5_MP4_H
#define QC5_MP4_H
#include <stddef.h>
#include <stdint.h>
/* Streaming fragmented MP4. H264 is copied; AAC must already be encoded.
 * The sink must either consume the entire piece or return failure. */
typedef int (*qc5_mp4_sink)(void *,const void *,size_t);
struct qc5_mp4 {
 qc5_mp4_sink sink;void *opaque;
 uint8_t sps[256],pps[256];size_t sps_len,pps_len;
 unsigned rate,fps,width,height,sequence,started,have_video,have_audio,duration90;
 uint64_t origin_ms,video_ms,audio_ms;
 /* Live MSE can accept repeated initialization segments; MP4 files keep
  * their existing avc1/audio or avc3/video-only sample-entry behavior. */
 unsigned live_avc1;
};
int qc5_mp4_init(struct qc5_mp4 *,unsigned,unsigned,qc5_mp4_sink,void *);
/* 0: accepted/waiting for keyframe; -1: malformed/unsupported; -2: sink failed;
 * -3: codec or clock changed: close the current MP4 and start a new one. */
/* rate=0 selects video-only MP4; timed frames preserve the camera clock. */
int qc5_mp4_video_timed(struct qc5_mp4 *,const uint8_t *,size_t,uint64_t,unsigned);
/* Direct Helix access units use the existing 786944-byte reserved arena.
 * Video-only: no transcoding, allocation, or change to the legacy512KiB API. */
#define QC5_MP4_NATIVE_AU_MAX 786944u
int qc5_mp4_video_native(struct qc5_mp4 *,const uint8_t *,size_t,uint64_t,unsigned);
int qc5_mp4_video(struct qc5_mp4 *,const uint8_t *,size_t,uint64_t);
int qc5_mp4_audio(struct qc5_mp4 *,const uint8_t *,size_t,uint64_t);
#endif
