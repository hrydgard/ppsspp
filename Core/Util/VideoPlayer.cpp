#include <algorithm>
#include <cstring>
#include <cstdint>
#include <memory>

#include "Common/Swap.h"
#include "Core/HW/MpegDemux.h"
#include "Core/HW/SimpleAudioDec.h"
#include "VideoPlayer.h"

bool pmf_has_audio(const uint8_t *data, size_t size) {
	if (size < 0x10 || memcmp(data, "PSMF", 4) != 0) {
		return false;
	}
	// The audio is in MPEG private stream 1 packets (start code 00 00 01 BD).
	static const uint8_t privateStream1[4] = { 0x00, 0x00, 0x01, 0xBD };
	const uint8_t *end = data + size;
	return std::search(data + std::min<size_t>(ReadBE32(data + 8), size), end, privateStream1, privateStream1 + 4) != end;
}

std::vector<int16_t> pmf_decode_audio(const uint8_t *data, size_t size) {
	std::vector<int16_t> pcm;
	if (size < 0x10 || memcmp(data, "PSMF", 4) != 0) {
		return pcm;
	}
	// Where the stream starts, after the PSMF header.
	const uint32_t offset = ReadBE32(data + 8);
	if (offset >= size) {
		return pcm;
	}

	MpegDemux demux((int)size + 2048, (int)offset);
	demux.addStreamData(data, (int)size);
	std::unique_ptr<AudioDecoder> decoder(CreateAudioDecoder(PSP_CODEC_AT3PLUS));
	if (!decoder) {
		return pcm;
	}

	int16_t buffer[2048 * 2];
	for (int frames = 0; frames < 10000; frames++) {
		demux.demux(0);
		uint8_t *frame = nullptr;
		int headerCode1 = 0, headerCode2 = 0;
		const int frameSize = demux.getNextAudioFrame(&frame, &headerCode1, &headerCode2);
		if (frameSize <= 0 || !frame) {
			break;
		}
		if (headerCode1 == 0x24) {
			decoder->SetChannels(1);  // Mono. Still decodes to stereo.
		}
		int consumed = 0;
		int samples = 0;
		if (!decoder->Decode(frame, frameSize, &consumed, 2, buffer, &samples) || samples <= 0) {
			continue;
		}
		samples = std::min(samples, 2048);
		pcm.insert(pcm.end(), buffer, buffer + samples * 2);
	}
	return pcm;
}

#ifdef USE_FFMPEG

extern "C" {
#include "libavformat/avformat.h"
#include "libavutil/samplefmt.h"
#include "libavutil/imgutils.h"
#include "libavutil/version.h"
#include "libswscale/swscale.h"
#include "libavcodec/avcodec.h"
#include "libavutil/time.h" // For av_gettime and av_usleep
}

#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(57, 33, 100)

#define HAVE_VIDEO_PLAYER

#endif

#endif

#ifdef HAVE_VIDEO_PLAYER

#include "Common/Log.h"
#include "Core/FFMPEGCompat.h"

bool pmf_player_available() {
	return true;
}

struct MemBuffer {
	const uint8_t* ptr;
	size_t size;
	size_t pos;
};

struct PMFPlayer {
	AVFormatContext* fmt_ctx;
	AVCodecContext* video_ctx;
	struct SwsContext* sws_ctx;
	AVFrame* frame;
	AVFrame* rgb_frame;
	uint8_t* avio_ctx_buffer;
	AVIOContext* avio_ctx;
	MemBuffer* mem_stream;
	int video_stream_idx;
	int audio_stream_idx;
	double last_pts;
	int64_t frame_count; // Fallback for missing PTS
	bool reached_end;
	double last_time;  // What pmf_update was last asked for.
};

// --- Custom IO Callbacks ---

static int read_packet(void* opaque, uint8_t* buf, int buf_size) {
	MemBuffer* mb = static_cast<MemBuffer*>(opaque);
	int remaining = static_cast<int>(mb->size - mb->pos);
	if (remaining <= 0) return AVERROR_EOF;

	int to_read = (buf_size < remaining) ? buf_size : remaining;
	std::memcpy(buf, mb->ptr + mb->pos, to_read);
	mb->pos += to_read;
	return to_read;
}

static int64_t seek_packet(void* opaque, int64_t offset, int whence) {
	MemBuffer* mb = static_cast<MemBuffer*>(opaque);
	if (whence == AVSEEK_SIZE) return mb->size;

	int64_t new_pos = mb->pos;
	if (whence == SEEK_SET) new_pos = offset;
	else if (whence == SEEK_CUR) new_pos += offset;
	else if (whence == SEEK_END) new_pos = mb->size + offset;

	if (new_pos < 0 || new_pos > static_cast<int64_t>(mb->size)) return -1;
	mb->pos = static_cast<size_t>(new_pos);
	return mb->pos;
}

// --- Core API ---

PMFPlayer *pmf_create() {
	return new PMFPlayer();
}

void pmf_destroy(PMFPlayer* ps) {
	if (ps) {
		pmf_deinit(ps);
		delete ps;
	}
}

int pmf_init(PMFPlayer* ps, const uint8_t* data, size_t size, int* out_w, int* out_h) {
	av_register_all();
	std::memset(ps, 0, sizeof(PMFPlayer));
	ps->audio_stream_idx = -1;
	ps->video_stream_idx = -1;

	// Safety Check: Verify PSMF Magic (0x50 0x53 0x4D 0x46)
	if (size < 24 || std::memcmp(data, "PSMF", 4) != 0) {
		return -1;
	}

	const int PMF_HEADER_SIZE = 20;
	ps->mem_stream = static_cast<MemBuffer*>(av_malloc(sizeof(MemBuffer)));
	ps->mem_stream->ptr = data + PMF_HEADER_SIZE;
	ps->mem_stream->size = size - PMF_HEADER_SIZE;
	ps->mem_stream->pos = 0;

	const int io_buffer_size = 32768;
	ps->avio_ctx_buffer = static_cast<uint8_t*>(av_malloc(io_buffer_size));
	ps->avio_ctx = avio_alloc_context(ps->avio_ctx_buffer, io_buffer_size, 0,
		ps->mem_stream, &read_packet, nullptr, &seek_packet);

	ps->fmt_ctx = avformat_alloc_context();
	ps->fmt_ctx->pb = ps->avio_ctx;

	if (avformat_open_input(&ps->fmt_ctx, nullptr, nullptr, nullptr) < 0) return -1;
	if (avformat_find_stream_info(ps->fmt_ctx, nullptr) < 0) return -1;

	ps->video_stream_idx = -1;
	for (unsigned int i = 0; i < ps->fmt_ctx->nb_streams; i++) {
		if (ps->fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && ps->video_stream_idx == -1) {
			ps->video_stream_idx = i;
		}
		if (ps->fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && ps->audio_stream_idx == -1) {
			ps->audio_stream_idx = i;

			INFO_LOG(Log::System, "Audio stream found in PMF: stream index %d", i);
		}
	}
	if (ps->video_stream_idx == -1) {
		WARN_LOG(Log::System, "Video stream not found in PMF");
		return -1;
	}

	ps->video_ctx = ps->fmt_ctx->streams[ps->video_stream_idx]->codec;
	AVCodec* codec = avcodec_find_decoder(ps->video_ctx->codec_id);
	if (!codec || avcodec_open2(ps->video_ctx, codec, nullptr) < 0) return -1;

	// Reject absurd dimensions: PMF video on PSP never exceeds 720x480, and
	// larger values could overflow the caller's frame buffer allocation
	// (width * height * 4) and drive huge sws_scale writes.
	if (ps->video_ctx->width <= 0 || ps->video_ctx->height <= 0 ||
		ps->video_ctx->width > 720 || ps->video_ctx->height > 480) {
		return -1;
	}

	ps->frame = av_frame_alloc();
	ps->rgb_frame = av_frame_alloc();
	ps->last_pts = -1.0;

	*out_w = ps->video_ctx->width;
	*out_h = ps->video_ctx->height;

	ps->sws_ctx = sws_getContext(ps->video_ctx->width, ps->video_ctx->height, ps->video_ctx->pix_fmt,
		ps->video_ctx->width, ps->video_ctx->height, AV_PIX_FMT_RGBA,
		SWS_BILINEAR, nullptr, nullptr, nullptr);
	return 0;
}

bool pmf_update(PMFPlayer* ps, double current_time_seconds, uint8_t* user_rgba_buffer) {
	bool bufferWritten = false;
	AVPacket pkt;
	int got_frame = 0;
	AVStream* st = ps->fmt_ctx->streams[ps->video_stream_idx];

	// Calculate duration for broken PTS files
	double frame_rate = av_q2d(st->avg_frame_rate);
	double frame_duration = (frame_rate > 0) ? (1.0 / frame_rate) : 0.033;

	// The clock went back (it follows a sound that looped): start over from the beginning.
	if (current_time_seconds + frame_duration < ps->last_time) {
		av_seek_frame(ps->fmt_ctx, ps->video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(ps->video_ctx);
		ps->frame_count = 0;
		ps->last_pts = -1.0;
	}
	ps->last_time = current_time_seconds;

	// Don't decode if the UI clock hasn't caught up to our last frame yet
	if (ps->last_pts >= current_time_seconds && ps->last_pts >= 0) return false;

	while (av_read_frame(ps->fmt_ctx, &pkt) >= 0) {
		if (pkt.stream_index == ps->video_stream_idx) {
			if (avcodec_decode_video2(ps->video_ctx, ps->frame, &got_frame, &pkt) >= 0 && got_frame) {
				double pts = 0;
				if (false && ps->frame->pkt_pts != AV_NOPTS_VALUE) {
					pts = static_cast<double>(ps->frame->pkt_pts) * av_q2d(st->time_base);
				} else {
					// Fallback (or actually, more reliable solution): Use frame count * duration
					pts = static_cast<double>(ps->frame_count) * frame_duration;
				}
				ps->last_pts = pts;
				ps->frame_count++;

				// If we've reached the point in the stream the UI requested
				if (pts >= current_time_seconds) {
					// Wrap the user's RGBA32 buffer for the scaler
					uint8_t* dest_data[4] = {user_rgba_buffer, nullptr, nullptr, nullptr};
					int dest_linesize[4] = {ps->video_ctx->width * 4, 0, 0, 0};

					sws_scale(ps->sws_ctx, ps->frame->data, ps->frame->linesize, 0,
						ps->video_ctx->height, dest_data, dest_linesize);

					av_packet_unref(&pkt);
					return true;
				}
			}
		}
		av_packet_unref(&pkt);
	}

	// EOF Reached: Seek to start and reset local counters
	ps->reached_end = true;
	av_seek_frame(ps->fmt_ctx, ps->video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
	avcodec_flush_buffers(ps->video_ctx);
	ps->last_pts = -1.0;
	return false;
}

bool pmf_reached_end(PMFPlayer* ps) {
	return ps && ps->reached_end;
}

void pmf_deinit(PMFPlayer* ps) {
	if (!ps) return;

	if (ps->video_ctx) {
		avcodec_close(ps->video_ctx);
		ps->video_ctx = nullptr;
	}

	if (ps->fmt_ctx) {
		// avformat_close_input will free the AVIOContext and the
		// avio_ctx_buffer because it owns them.
		avformat_close_input(&ps->fmt_ctx);
		ps->fmt_ctx = nullptr;

		// Note: Do NOT free ps->avio_ctx_buffer or ps->avio_ctx here.
	}

	// We still own the MemBuffer struct (the opaque pointer),
	// so we free that last.
	if (ps->mem_stream) {
		av_free(ps->mem_stream);
		ps->mem_stream = nullptr;
	}

	if (ps->sws_ctx) {
		sws_freeContext(ps->sws_ctx);
		ps->sws_ctx = nullptr;
	}

	if (ps->frame) {
		av_frame_free(&ps->frame);
		ps->frame = nullptr;
	}
}

#else

bool pmf_player_available() { return false; }

PMFPlayer *pmf_create() { return nullptr; }
void pmf_destroy(PMFPlayer* ps) {}

int pmf_init(PMFPlayer* ps, const uint8_t* data, size_t size, int* out_w, int* out_h) { return -1; }
bool pmf_update(PMFPlayer* ps, double current_time_seconds, uint8_t* user_rgb_buffer) { return false; }
void pmf_deinit(PMFPlayer* ps) {}
bool pmf_reached_end(PMFPlayer* ps) { return true; }

#endif
