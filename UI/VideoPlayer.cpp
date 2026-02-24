
extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/samplefmt.h"
#include "libavutil/imgutils.h"
#include "libavutil/version.h"
#include "libswscale/swscale.h"
#include "libavcodec/avcodec.h"
#include "libavutil/time.h" // For av_gettime and av_usleep

}
#include "Core/FFMPEGCompat.h"

typedef struct {
	AVFormatContext *fmt_ctx;
	AVCodecContext  *video_ctx;
	struct SwsContext *sws_ctx;
	AVFrame *frame;
	AVFrame *rgb_frame;
	uint8_t *rgb_buffer;
	int video_stream_idx;
	double last_pts; // Seconds
} PMFPlayer;

// User-defined callback for the UI
void display_pixels(uint8_t *data, int linesize, int width, int height);

int pmf_init(PMFPlayer *ps, const char *filename) {
	av_register_all();
	memset(ps, 0, sizeof(PMFPlayer));

	// Open file and skip Sony PMF header (typically 20 bytes)
	AVDictionary *options = NULL;
	av_dict_set(&options, "skip_initial_bytes", "20", 0);

	if (avformat_open_input(&ps->fmt_ctx, filename, NULL, &options) < 0) {
		return -1;
	}
	av_dict_free(&options);

	if (avformat_find_stream_info(ps->fmt_ctx, NULL) < 0) return -1;

	// Find video stream
	ps->video_stream_idx = -1;
	for (int i = 0; i < ps->fmt_ctx->nb_streams; i++) {
		if (ps->fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			ps->video_stream_idx = i;
			break;
		}
	}
	if (ps->video_stream_idx == -1) return -1;

	// Setup Decoder
	ps->video_ctx = ps->fmt_ctx->streams[ps->video_stream_idx]->codec;
	AVCodec *codec = avcodec_find_decoder(ps->video_ctx->codec_id);
	if (!codec || avcodec_open2(ps->video_ctx, codec, NULL) < 0) return -1;

	// Allocate Frames
	ps->frame = av_frame_alloc();
	ps->rgb_frame = av_frame_alloc();

	int size = avpicture_get_size(AV_PIX_FMT_RGB24, ps->video_ctx->width, ps->video_ctx->height);
	ps->rgb_buffer = (uint8_t *)av_malloc(size);
	avpicture_fill((AVPicture *)ps->rgb_frame, ps->rgb_buffer, AV_PIX_FMT_RGB24,
		ps->video_ctx->width, ps->video_ctx->height);

	ps->sws_ctx = sws_getContext(ps->video_ctx->width, ps->video_ctx->height, ps->video_ctx->pix_fmt,
		ps->video_ctx->width, ps->video_ctx->height, AV_PIX_FMT_RGB24,
		SWS_BILINEAR, NULL, NULL, NULL);
	ps->last_pts = -1.0;
	return 0;
}

void pmf_update(PMFPlayer *ps, double current_time_seconds) {
	AVPacket pkt;
	int got_frame = 0;
	AVStream *st = ps->fmt_ctx->streams[ps->video_stream_idx];

	// If the UI clock is still behind our last decoded frame, do nothing
	if (current_time_seconds <= ps->last_pts && ps->last_pts >= 0) return;

	while (av_read_frame(ps->fmt_ctx, &pkt) >= 0) {
		if (pkt.stream_index == ps->video_stream_idx) {
			avcodec_decode_video2(ps->video_ctx, ps->frame, &got_frame, &pkt);

			if (got_frame) {
				double pts = (double)ps->frame->pkt_pts * av_q2d(st->time_base);
				ps->last_pts = pts;

				// Only scale and display if this frame is "due"
				if (pts >= current_time_seconds) {
					sws_scale(ps->sws_ctx, (const uint8_t *const *)ps->frame->data, ps->frame->linesize,
						0, ps->video_ctx->height, ps->rgb_frame->data, ps->rgb_frame->linesize);

					display_pixels(ps->rgb_frame->data[0], ps->rgb_frame->linesize[0], ps->video_ctx->width, ps->video_ctx->height);

					av_packet_unref(&pkt);
					break; // We found the frame we needed
				}
			}
		}
		av_packet_unref(&pkt);
	}

	// Loop logic: if we hit the end of the file
	if (av_read_frame(ps->fmt_ctx, &pkt) < 0) {
		av_seek_frame(ps->fmt_ctx, ps->video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(ps->video_ctx);
		ps->last_pts = -1.0;
	}
}

void pmf_deinit(PMFPlayer *ps) {
	if (ps->video_ctx) avcodec_close(ps->video_ctx);
	if (ps->fmt_ctx) avformat_close_input(&ps->fmt_ctx);
	if (ps->sws_ctx) sws_freeContext(ps->sws_ctx);
	if (ps->frame) av_frame_free(&ps->frame);
	if (ps->rgb_frame) av_frame_free(&ps->rgb_frame);
	if (ps->rgb_buffer) av_free(ps->rgb_buffer);
}
