#ifndef rmp3_h
#define rmp3_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>

#define RMP3_TRUE       1
#define RMP3_FALSE      0

#define RMP3_MAX_SAMPLES_PER_FRAME (1152*2)


/* Low Level Push API
 * ================== */
typedef struct
{
    int frame_bytes;
    int channels;
    int hz;
    int layer;
    int bitrate_kbps;
} rmp3dec_frame_info;

typedef struct
{
    /* IMDCT overlap history; same dual-format convention as qmf_state. */
    union
    {
        float   f[2][9*32];
        int32_t q[2][9*32];
    } mdct_overlap;
    /* QMF synthesis overlap history: float while the float pipeline is
     * active, Q28 fixed point while the s16 pipeline is active (the
     * active format follows the latched output mode). */
    union
    {
        float   f[15*2*32];
        int32_t q[15*2*32];
    } qmf_state;
    int reserv;
    int free_format_bytes;
    unsigned char header[4];
    unsigned char reserv_buf[511];
    /* Per-instance decode scratch (bitstream state, main-data buffer,
     * granule/synthesis working sets).  This lived on
     * rmp3dec_decode_frame's stack as a ~17 KB local: over a quarter
     * of a 64 KB task-thread stack per call, and ~275 cachelines of
     * L1 evicted per decoded audio frame for data that is dead at
     * return.  It is opaque sized storage here so the internal layout
     * stays private to rmp3.c, which asserts at compile time that it
     * fits; the pointer/float members guarantee sufficient alignment
     * for everything the layout contains.
     *
     * Consequence: rmp3dec (and rmp3, which embeds it) are large
     * objects and should be heap-allocated, as the in-tree consumer
     * already does. */
    union
    {
        void         *align_ptr;
        float         align_f;
        int32_t       align_q;
        unsigned char bytes[18432];
    } scratch;
} rmp3dec;

/* One frame at a time, for a caller that frames the stream itself (a
 * console's audio codec interface hands over a buffer and asks for one
 * frame out of it). rmp3dec_decode_s16 decodes the first frame in @mp3,
 * skipping whatever comes before its sync, into @pcm - up to
 * RMP3_MAX_SAMPLES_PER_FRAME samples, interleaved at the frame's
 * channel count - and returns the samples a channel. info->frame_bytes
 * is what the frame and anything skipped before it took of @mp3: 0
 * when no whole frame was found, and with a return of 0 bytes skipped
 * without a frame (junk, or a frame whose bit reservoir reaches back
 * before the decoder joined the stream). The decoder keeps the bit
 * reservoir from call to call; rmp3dec_init starts it afresh, as at a
 * seek. s16 only: the synthesis runs in fixed point throughout. */
void rmp3dec_init(rmp3dec *dec);
int rmp3dec_decode_s16(rmp3dec *dec, const uint8_t *mp3, int mp3_bytes,
      int16_t *pcm, rmp3dec_frame_info *info);

/* One decoded MPEG frame, in whichever format the synthesis ran in.
 * The two pipelines share the storage through this union; a format
 * switch converts the contents in place. */
typedef union
{
    int16_t s16[RMP3_MAX_SAMPLES_PER_FRAME];
    float   f32[RMP3_MAX_SAMPLES_PER_FRAME];
} rmp3_frame_buf;

/* Main API (Pull API)
 * ===================*/

typedef struct
{
    rmp3dec decoder;
    uint32_t channels;          /* Channel count of the stream (from the first frame). */
    uint32_t sampleRate;        /* Sample rate of the stream (from the first frame). */
    uint32_t frameChannels;     /* Channels in the currently decoded frame. Internal. */
    uint32_t framesConsumed;    /* PCM frames of the current block already returned. Internal. */
    uint32_t framesRemaining;   /* PCM frames of the current block still to return. Internal. */
    uint32_t f32_mode;          /* 0: synthesis outputs s16; 1: native float. Internal. */
    rmp3_frame_buf frames;      /* Current decoded frame in the latched format. */
    const uint8_t* pData;       /* Caller's buffer (borrowed, never freed). */
    size_t dataSize;
    size_t readPos;             /* Read cursor into pData. */
    uint32_t atEnd;
} rmp3;


/* Initializes an MP3 decoder from a block of memory.
 *
 * This does not create a copy of the data. It is up to the application to ensure the buffer remains valid for
 * the lifetime of the rmp3 object.
 *
 * The buffer should contain the contents of the entire MP3 file.
 */
uint32_t rmp3_init_memory(rmp3* pMP3, const void* pData, size_t dataSize);

/* Uninitializes an MP3 decoder. */
void rmp3_uninit(rmp3* pMP3);

/* Reads PCM frames as interleaved 32-bit IEEE floating point PCM.
 *
 * Note that framesToRead specifies the number of PCM frames to read, _not_ the number of MP3 frames.
 */
uint64_t rmp3_read_f32(rmp3* pMP3, uint64_t framesToRead, float* pBufferOut);

/* Reads PCM frames as native signed 16-bit; the decoder's synthesis
 * quantises directly, with no float round-trip. A given decoder
 * instance is normally used with one output format; switching formats
 * re-decodes the currently buffered frame. */
uint64_t rmp3_read_s16(rmp3* pMP3, uint64_t framesToRead, int16_t* pBufferOut);

/* Seeks to a specific frame.
 *
 * Note that this is _not_ an MP3 frame, but rather a PCM frame.
 */
uint32_t rmp3_seek_to_frame(rmp3* pMP3, uint64_t frameIndex);


/* Frees any memory that was allocated by a public rmp3 API. */


/* Streaming API
 * =============
 *
 * For an MP3 that is not resident: bytes are handed over a window at a
 * time and PCM comes back as they arrive, so what a decode costs in
 * memory is a frame rather than a file. The pull API above keeps a
 * borrowed pointer to the whole stream and re-scans it to seek, which a
 * caller reading from storage does not have and should not have to
 * fabricate.
 *
 *    rmp3_stream_t *s = rmp3_stream_new();
 *    rmp3_stream_set_out_s16(s, pcm, frames);
 *    for (;;)
 *    {
 *       rmp3_stream_set_in(s, buf, filled);
 *       r = rmp3_stream_process(s, &read, &wrote);
 *       memmove(buf, buf + read, filled -= read);
 *       if (r == RMP3_STREAM_NEED_IN)
 *          filled += pull_bytes(buf + filled, cap - filled);
 *       else if (r != RMP3_STREAM_OK)
 *          break;
 *    }
 *
 * Input is consumed, not borrowed: @read says how much was taken and the
 * rest must be presented again, so a window may slide freely. A frame
 * split across two windows is reassembled internally, so no alignment
 * is asked of the caller.
 *
 * MPEG audio states no length: a stream is however many frames it turns
 * out to contain, and short of a Xing header the only way to know is to
 * walk them. Setting no output does exactly that - frames are located
 * and counted, nothing is decoded - which is what @wrote reports. In
 * that mode process() returns after each frame, so the offset from
 * rmp3_stream_frame_offset() belongs to the frame just counted and a
 * caller can build a seek index as it walks.
 */

#define RMP3_STREAM_OK        0
#define RMP3_STREAM_NEED_IN   1  /* input window is spent           */
#define RMP3_STREAM_END       2  /* nothing further to decode       */
#define RMP3_STREAM_ERROR   (-1)

typedef struct rmp3_stream rmp3_stream_t;

rmp3_stream_t *rmp3_stream_new(void);
void rmp3_stream_free(rmp3_stream_t *s);

void rmp3_stream_set_in(rmp3_stream_t *s, const void *in, size_t in_size);

/**
 * rmp3_stream_set_out_s16:
 * rmp3_stream_set_out_f32:
 *
 * Sets the destination and its capacity in frames, interleaved at the
 * stream's own channel count. A null destination, or a capacity of
 * zero, is parse-only.
 *
 * The two entry points select the same two pipelines the pull API
 * runs: s16 synthesises in Q28 fixed point straight to s16, f32 in
 * native float. They may be mixed freely on one stream - the decoder's
 * persistent filter state and any undrained frame are converted in
 * place at the switch, exactly as the pull API's reads do - but a
 * parse-only call selects nothing, so a walk in the middle of a decode
 * does not disturb the pipeline the decode is in.
 */
void rmp3_stream_set_out_s16(rmp3_stream_t *s, int16_t *out, size_t out_frames);
void rmp3_stream_set_out_f32(rmp3_stream_t *s, float *out, size_t out_frames);

int rmp3_stream_process(rmp3_stream_t *s, size_t *read, size_t *wrote);

/**
 * rmp3_stream_info:
 *
 * Returns: nonzero once a frame has been located, which is the point
 * the channel count and rate are known - MPEG audio has no header
 * ahead of the stream to read them from, so they come off the first
 * frame's own header.  A parse-only walk locates frames too, so this
 * answers there without anything having been decoded.
 */
int rmp3_stream_info(const rmp3_stream_t *s, unsigned *channels, unsigned *rate);

/**
 * rmp3_stream_set_eof:
 *
 * States that no further input will be supplied.
 *
 * Required to decode the tail. A frame must be presented whole: given
 * fewer bytes than one occupies, the frame finder cannot tell the head
 * of an unfinished frame from junk and reports it as bytes to skip, so
 * the decoder waits for a full window rather than acting on that. At
 * the end of a stream that window never fills, and this is what says
 * the short tail is all there is.
 */
/**
 * rmp3_stream_frame_offset:
 *
 * Where in the stream the frame decoded by the last process() call
 * began, counted from the first byte ever handed to set_in() since the
 * last reset.
 *
 * MPEG audio carries no index and a frame is not addressable from its
 * position alone, so seeking means noting these offsets on the way past
 * and resuming from one. Resuming mid-stream loses whatever the bit
 * reservoir carried into that frame, so decode a frame or two before
 * the target and discard them.
 */
uint64_t rmp3_stream_frame_offset(const rmp3_stream_t *s);

/**
 * rmp3_stream_frames_in:
 *
 * MPEG frames consumed since the last reset, whether or not they
 * produced samples.
 *
 * Not every frame does. Layer III carries part of a frame's data in the
 * frames before it - the bit reservoir - so a decoder that joined the
 * stream partway meets frames whose data begins before where it
 * started, and those decode to nothing. How many depends on how the
 * encoder distributed its bits, not on anything a caller can compute:
 * it is one frame in some places and three in others.
 *
 * A caller resuming at a known frame therefore cannot assume its
 * position advances one frame per emission. This is what says where the
 * stream actually stands: resuming at frame N, the frame just emitted
 * is N + frames_in - 1.
 */
uint64_t rmp3_stream_frames_in(const rmp3_stream_t *s);

void rmp3_stream_set_eof(rmp3_stream_t *s);

void rmp3_stream_reset(rmp3_stream_t *s);

#ifdef __cplusplus
}
#endif
#endif  /* rmp3_h */
