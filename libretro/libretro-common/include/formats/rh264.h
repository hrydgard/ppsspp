/* rh264 -- self-contained H.264 decoder for libretro-common.
 *
 * Decodes H.264 (AVC) I, P and B pictures with either entropy coding
 * (CAVLC and CABAC): NAL/SPS/PPS/slice parsing; 4x4, 8x8 and 16x16 luma
 * and chroma intra prediction; inter prediction with quarter-pel motion
 * compensation, multiple reference pictures (a DPB with sliding-window
 * and MMCO marking and reference list modifications), weighted and
 * implicit bi-prediction, and spatial and temporal direct modes; the
 * 4x4 and 8x8 integer transforms with scaling matrices and the Hadamard
 * DC transforms; dequantisation with correct chroma-QP derivation; the
 * in-loop deblocking filter; and picture order count types 0, 1 and 2
 * with display-order output.  Reconstruction is 8-bit 4:2:0 and 4:2:2,
 * which covers the baseline, main and high profiles as commonly emitted.
 *
 * Out-of-scope streams are refused at the parameter-set or slice level
 * rather than decoded wrongly: 4:4:4, monochrome, high bit depths and
 * lossless transform bypass; SP/SI switching slices; FMO/ASO and
 * redundant pictures; field-coded B and CABAC pictures and field-coded
 * macroblock pairs (frame-coded MBAFF pairs decode; field pictures
 * decode for CAVLC I/P).
 *
 * The CAVLC VLC tables are extracted verbatim from the encoder tables in
 * libopenh264 (verified prefix-free); no table is hand-transcribed.
 *
 * The persistent video API mirrors rvp8_video so a demuxer (e.g. the MP4
 * glue in rmp4_video.c) can dispatch H.264 exactly like it dispatches VP8/
 * VP9:
 *
 *   rh264_video *v = rh264_video_open();
 *   rh264_video_set_extradata(v, avcc, avcc_len);   (the avcC box payload)
 *   for each frame:
 *      if (rh264_video_decode(v, data, len) < 0) fail;   (1 = picture ready)
 *      y = rh264_video_plane(v, 0, &ystride, &w, &h);   (also 1=U, 2=V)
 *   rh264_video_close(v);
 *
 * Frame data may be either Annex-B (start-code delimited) or the
 * length-prefixed AVCC form carried in MP4 'mdat'; the NAL length size is
 * taken from the avcC extradata when present (default 4).  The returned
 * plane pointers stay valid until the next decode call. */
#ifndef __LIBRETRO_SDK_FORMAT_RH264_H__
#define __LIBRETRO_SDK_FORMAT_RH264_H__

#include <stdint.h>
#include <stddef.h>

#include <retro_common_api.h>

#include <formats/video_hw_frame.h>

RETRO_BEGIN_DECLS

typedef struct rh264_video rh264_video;

/* Create a decoder. Returns NULL on allocation failure. */
rh264_video *rh264_video_open(void);

/* Supply the avcC (AVCDecoderConfigurationRecord) extradata from the sample
 * description, which carries the SPS/PPS and the NAL length size. Optional
 * for Annex-B input that carries in-band SPS/PPS, required for bare AVCC
 * sample data. Returns 0 on success. Safe to call once before decoding. */
int rh264_video_set_extradata(rh264_video *v, const uint8_t *avcc, size_t len);

/* Decode one access unit (one coded picture worth of NAL units) to internal
 * I420 planes. Accepts Annex-B or length-prefixed AVCC data. IDR pictures and
 * decoded picture leaves in display order, which with B pictures is not
 * decode order: returns 1 when a picture is ready through rh264_video_plane,
 * 0 when the data was consumed but the picture is still held for reordering,
 * and negative on malformed input or an unsupported (high-profile) stream. */
int rh264_video_decode(rh264_video *v, const uint8_t *data, size_t len);

/* Hand out the next pending picture in display order without feeding more
 * data, for end of stream. Returns 0 when a picture became available through
 * rh264_video_plane, -1 when nothing is pending. */
int rh264_video_drain(rh264_video *v);

/* Bits per sample of the decoded pictures (8..14).  Above 8 the planes
 * hold uint16_t samples: rh264_video_plane still returns a byte
 * pointer (cast it; the stride counts samples). */
/* While @skip is set, a non-reference picture is consumed without
 * being decoded: rh264_video_decode() returns 0 for it, as for any
 * sample that yields no picture, and the stream moves on. Nothing
 * predicts from such a picture, so what follows decodes unchanged.
 * For a caller that has fallen behind its clock and would rather
 * drop a frame than show every one late. */
void rh264_video_set_skip_nonref(rh264_video *v, int skip);
/* Whether the last decode call passed a picture over under that
 * setting: it returned 0 as it does for a picture held back for
 * reordering, but this one's presentation slot has gone. */
int rh264_video_dropped(const rh264_video *v);

/* For the decoder's own samples: how many times a reference read found
 * the rows it needed not yet final. On a single thread that is zero
 * by construction, and a sample asserts it; it is the count a
 * threaded decoder would have waited on. */
int rh264_video_ref_wait_misses(void);

/* How many pictures the decoder keeps in rotation, 1 to 4. Each new
 * picture takes the next context round; with one, the same every
 * time. Today the pictures still decode one after the other, so this
 * changes which memory a picture uses and nothing else - which is
 * what lets a sample prove the per-picture state complete before the
 * pictures decode concurrently. */
void rh264_video_set_contexts(rh264_video *v, int n);

/* Decode pictures concurrently on @pool (a tpool_t), up to @threads at
 * a time: a picture's slices are handed to the pool when the next
 * picture opens, and a picture reading from one still decoding waits
 * for the rows it needs and no more. Output is the same, in the same
 * order, later by the pictures in flight; rh264_video_drain() lands
 * them all. NULL, or fewer than two, is the decoder on the calling
 * thread. No effect without HAVE_THREADS. */
void rh264_video_set_thread_pool(rh264_video *v, void *pool, int threads);

/* Test knob: hold each row's publication for up to @max_yields thread
 * yields, drawn at random, so that pictures reading from a picture in flight
 * wait for their rows rather than nearly always finding them. Output
 * must be byte-exact under it. 0 is off. Debug only. */
void rh264_video_set_publish_delay(int max_yields);

/* What the pipeline did, for a bench: pictures posted to the pool, the
 * mean pictures in flight at a post (x100), posts that found the most
 * possible in flight, joins that had to wait for a context, output
 * pops that held a due picture for being incomplete, and pops that
 * waited for one because the queue was full. */
void rh264_video_stats(const rh264_video *v, int *posted, int *inflight_x100,
      int *at_max, int *join_waits, int *pop_held, int *pop_waits);

/* Reference reads that had to wait for rows since the last call, and
 * the rows they were short by on average (x100): how often a picture
 * in flight stalls on the one it predicts from, and how far behind it
 * stands when it does. Process-wide; reading resets. */
void rh264_video_row_wait_stats(int *waits, int *rows_short_x100);

/* The most pictures the pool had decoding at one moment since the last
 * call; reading resets. Process-wide. One means they never overlapped. */
int rh264_video_jobs_at_once(void);

int rh264_video_bit_depth(const rh264_video *v);

/* Borrow a decoded plane (0=Y, 1=U, 2=V). Valid until the next decode call. */
const uint8_t *rh264_video_plane(const rh264_video *v, int plane,
      int *stride, int *width, int *height);

void rh264_video_close(rh264_video *v);

/* ---- Hardware decoding ----
 *
 * The decoder keeps the sequencing - parameter sets, picture order
 * counts, reference marking, output order - and a backend (a GPU
 * video API) reconstructs the pictures. Progressive 8-bit 4:2:0
 * streams only; anything else, or a backend that refuses the stream,
 * decodes in software as before. */

/* The parameter sets as a hardware API takes them. Scaling lists are
 * in the order they are coded (zig-zag), with the present and
 * use-default flags of each list as bit masks. */
typedef struct rh264_hw_scaling
{
   uint16_t present_mask;
   uint16_t default_mask;
   uint8_t  list4x4[6][16];
   uint8_t  list8x8[6][64];
} rh264_hw_scaling_t;

typedef struct rh264_hw_sps
{
   int sps_id, profile_idc, constraint_flags, level_idc;
   int chroma_format_idc, bit_depth;
   int log2_max_frame_num, poc_type, log2_max_poc_lsb;
   int delta_pic_order_always_zero, offset_for_non_ref_pic;
   int offset_for_top_to_bottom_field, num_ref_frames_in_poc_cycle;
   const int32_t *offset_for_ref_frame;
   int max_num_ref_frames, gaps_allowed;
   int width_in_mbs, height_in_mbs;
   int direct_8x8_inference, transform_bypass;
   int frame_cropping, crop_left, crop_right, crop_top, crop_bottom;
   int width, height, crop_x, crop_y;   /* the picture shown */
   int scaling_present;
   rh264_hw_scaling_t scaling;
} rh264_hw_sps_t;

typedef struct rh264_hw_pps
{
   int pps_id, sps_id;
   int entropy_coding_mode, bottom_field_pic_order_in_frame_present;
   int num_ref_idx_l0_default, num_ref_idx_l1_default;  /* 1.. */
   int weighted_pred, weighted_bipred_idc;
   int pic_init_qp, pic_init_qs;
   int chroma_qp_index_offset, second_chroma_qp_index_offset;
   int deblocking_filter_control_present, constrained_intra_pred;
   int transform_8x8_mode;
   int scaling_present;
   rh264_hw_scaling_t scaling;
} rh264_hw_pps_t;

/* A picture the backend holds: the slot it was decoded into. */
typedef struct rh264_hw_ref
{
   int slot;
   int frame_num;     /* FrameNum, or LongTermFrameIdx when long-term */
   int poc[2];        /* top, bottom */
   int long_term;
} rh264_hw_ref_t;

typedef struct rh264_hw_pic
{
   const rh264_hw_sps_t *sps;
   const rh264_hw_pps_t *pps;
   /* Raised whenever either parameter set changes. */
   unsigned params_gen;
   /* The picture's slice NAL units, each after a 00 00 01 start code,
    * and where each starts in @data. */
   const uint8_t *data;
   size_t size;
   const uint32_t *slice_offsets;
   unsigned num_slices;
   int slot;          /* the slot the picture is decoded into */
   int frame_num, idr_pic_id;
   int poc[2];
   int idr, reference, intra;
   /* Stored as a long-term reference under @long_term_idx once
    * decoded (an IDR's long_term_reference_flag, or marking 6). */
   int long_term, long_term_idx;
   /* Every picture held for reference, short- and long-term. */
   const rh264_hw_ref_t *refs;
   unsigned num_refs;
} rh264_hw_pic_t;

typedef struct rh264_hw_ops
{
   void *user;
   /* A decoder for @sps's pictures with @slots slots, 0 to slots - 1;
    * NULL when the backend cannot take the stream. On the decoding
    * thread, as are the rest. */
   void *(*open)(void *user, const rh264_hw_sps_t *sps, unsigned slots);
   /* Decode @pic into its slot; 0, or negative on failure. */
   int (*decode)(void *dec, const rh264_hw_pic_t *pic);
   /* The picture in @slot is to be shown: a frame of it into @out that
    * stays good while the slot is decoded into again. 0, or negative. */
   int (*output)(void *dec, unsigned slot, video_hw_frame_t *out);
   void (*close)(void *dec);
} rh264_hw_ops_t;

/* The backend decoders try when asked to (rh264_video_set_hw);
 * process-wide. NULL withdraws it: decoders already open on it keep it
 * until they close, so it outlives them. */
void rh264_set_hw_backend(const rh264_hw_ops_t *ops);

/* Ask for hardware decoding before the first picture. True when the
 * stream will decode on the backend - its parameter sets must already
 * be known (rh264_video_set_extradata) - false when it decodes in
 * software. Not with a thread pool. */
int rh264_video_set_hw(rh264_video *v, int enable);
/* Whether the pictures come from the backend. */
int rh264_video_is_hw(const rh264_video *v);
/* The picture decode or drain made ready, as a hardware frame; 0 when
 * there is one. rh264_video_plane has none to give then. */
int rh264_video_hw_frame(const rh264_video *v, video_hw_frame_t *out);

RETRO_END_DECLS

#endif
