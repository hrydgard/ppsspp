#include "UnitTest.h"
#include <vector>
#include <cstdint>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>

#ifdef TEST_BUNDLED_FFMPEG
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/buffer.h"
#include "libavutil/threadmessage.h"
int ff_retro_serial_call(int (*call)(void *), void *arg);
void ff_retro_serial_shutdown();
}

namespace {
constexpr int kMessages = 4000;
struct QueueWorker {
 AVThreadMessageQueue *queue;
 AVBufferPool *pool;
 int id;
 bool ok = true;
 std::vector<int> received;
};
void Produce(void *opaque) {
 auto *w = static_cast<QueueWorker *>(opaque);
 for (int i = 0; i < kMessages; ++i) {
  AVBufferRef *b = av_buffer_pool_get(w->pool);
  if (!b) { w->ok = false; break; }
  memset(b->data, w->id, b->size);
  AVBufferRef *ref = av_buffer_ref(b);
  av_buffer_unref(&b);
  if (!ref || ref->data[0] != w->id) w->ok = false;
  av_buffer_unref(&ref);
  int value = w->id * kMessages + i;
  if (av_thread_message_queue_send(w->queue, &value, 0) < 0) { w->ok = false; break; }
 }
}
void Consume(void *opaque) {
 auto *w = static_cast<QueueWorker *>(opaque);
 int value;
 while (av_thread_message_queue_recv(w->queue, &value, 0) == 0) w->received.push_back(value);
}
int freedMessages;
void FreeMessage(void *) { ++freedMessages; }

int serialValue;
int AddSerial(void *) { return ++serialValue; }
int NestedSerial(void *) { return ff_retro_serial_call(AddSerial, nullptr); }
struct InitWorker { bool ok = true; };
void InitCalls(void *opaque) {
 auto *w = static_cast<InitWorker *>(opaque);
 for (int i = 0; i < 1000; ++i)
  if (ff_retro_serial_call(NestedSerial, nullptr) <= 0) w->ok = false;
}

bool QueuesAndPools() {
 for (int capacity : {1, 3, 64}) {
  AVThreadMessageQueue *q = nullptr;
  EXPECT_EQ_INT(av_thread_message_queue_alloc(&q, capacity, sizeof(int)), 0);
  int value = 7;
  EXPECT_EQ_INT(av_thread_message_queue_recv(q, &value, AV_THREAD_MESSAGE_NONBLOCK), AVERROR(EAGAIN));
  for (int i = 0; i < capacity; ++i)
   EXPECT_EQ_INT(av_thread_message_queue_send(q, &value, AV_THREAD_MESSAGE_NONBLOCK), 0);
  EXPECT_EQ_INT(av_thread_message_queue_send(q, &value, AV_THREAD_MESSAGE_NONBLOCK), AVERROR(EAGAIN));
  freedMessages = 0;
  av_thread_message_queue_set_free_func(q, FreeMessage);
  av_thread_message_flush(q);
  EXPECT_EQ_INT(freedMessages, capacity);
  AVBufferPool *pool = av_buffer_pool_init(128, nullptr);
  EXPECT_TRUE(pool != nullptr);
  QueueWorker producers[4], consumers[4];
  sthread_t *senders[4], *receivers[4];
  for (int i = 0; i < 4; ++i) {
   producers[i].queue = consumers[i].queue = q;
   producers[i].pool = pool;
   producers[i].id = i;
   receivers[i] = sthread_create(Consume, &consumers[i]);
   senders[i] = sthread_create(Produce, &producers[i]);
   EXPECT_TRUE(senders[i] && receivers[i]);
  }
  for (auto *t : senders) sthread_join(t);
  av_thread_message_queue_set_err_recv(q, AVERROR_EOF);
  for (auto *t : receivers) sthread_join(t);
  std::vector<int> all;
  for (int i = 0; i < 4; ++i) {
   EXPECT_TRUE(producers[i].ok);
   all.insert(all.end(), consumers[i].received.begin(), consumers[i].received.end());
  }
  std::sort(all.begin(), all.end());
  EXPECT_EQ_INT(all.size(), 4 * kMessages);
  for (int i = 0; i < 4 * kMessages; ++i) EXPECT_EQ_INT(all[i], i);
  AVBufferRef *survivor = av_buffer_pool_get(pool);
  EXPECT_TRUE(survivor != nullptr);
  av_buffer_pool_uninit(&pool);
  memset(survivor->data, 0x55, survivor->size);
  av_buffer_unref(&survivor);
  av_thread_message_queue_free(&q);
 }
 return true;
}

int SliceJob(AVCodecContext *, void *arg, int job, int thread) {
 auto *seen = static_cast<int *>(arg);
 seen[job] = thread + 1;
 return job * 3;
}
bool SliceJobs() {
 AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
 AVCodecContext *ctx = avcodec_alloc_context3(codec);
 EXPECT_TRUE(codec && ctx);
 ctx->thread_count = 4;
 ctx->thread_type = FF_THREAD_SLICE;
 EXPECT_EQ_INT(avcodec_open2(ctx, codec, nullptr), 0);
 EXPECT_EQ_INT(ctx->active_thread_type, FF_THREAD_SLICE);
 for (int count : {1, 3, 4, 5, 127}) {
  for (int iteration = 0; iteration < 100; ++iteration) {
   std::vector<int> seen(count), results(count);
   EXPECT_EQ_INT(ctx->execute2(ctx, SliceJob, seen.data(), results.data(), count), 0);
   for (int i = 0; i < count; ++i) {
    EXPECT_TRUE(seen[i] >= 1 && seen[i] <= 4);
    EXPECT_EQ_INT(results[i], i * 3);
   }
  }
 }
 avcodec_free_context(&ctx);
 return true;
}

// Encode a deterministic sequence without external media. Huffyuv exercises
// frame encoder queues; MPEG4 supplies reference/B frames to decoder workers.
bool Encode(AVCodecID id, int threads, std::vector<std::vector<uint8_t>> &packets, std::vector<uint8_t> &extra) {
 AVCodec *codec = avcodec_find_encoder(id);
 AVCodecContext *ctx = avcodec_alloc_context3(codec);
 EXPECT_TRUE(codec && ctx);
 ctx->width = 64; ctx->height = 48; ctx->time_base = {1, 30};
 ctx->pix_fmt = id == AV_CODEC_ID_HUFFYUV ? AV_PIX_FMT_YUV422P : AV_PIX_FMT_YUV420P;
 ctx->thread_count = threads;
 ctx->thread_type = id == AV_CODEC_ID_HUFFYUV ? FF_THREAD_FRAME : FF_THREAD_SLICE;
 ctx->max_b_frames = id == AV_CODEC_ID_MPEG4 ? 2 : 0;
 EXPECT_EQ_INT(avcodec_open2(ctx, codec, nullptr), 0);
 if (threads > 1) EXPECT_EQ_INT(ctx->active_thread_type, ctx->thread_type);
 if (ctx->extradata_size) extra.assign(ctx->extradata, ctx->extradata + ctx->extradata_size);
 AVFrame *f = av_frame_alloc();
 f->format = ctx->pix_fmt; f->width = ctx->width; f->height = ctx->height;
 EXPECT_EQ_INT(av_frame_get_buffer(f, 32), 0);
 for (int n = 0; n < 180 + 16; ++n) {
  if (n < 180) {
   EXPECT_EQ_INT(av_frame_make_writable(f), 0);
   for (int plane = 0; plane < 3; ++plane) {
    int width = plane ? 32 : 64;
    int height = plane && id != AV_CODEC_ID_HUFFYUV ? 24 : 48;
    for (int y = 0; y < height; ++y)
     for (int x = 0; x < width; ++x) f->data[plane][y * f->linesize[plane] + x] = (n + x + 3 * y + plane * 41) & 255;
   }
   f->pts = n;
  }
  AVPacket pkt{}; av_init_packet(&pkt);
  int got = 0;
  EXPECT_TRUE(avcodec_encode_video2(ctx, &pkt, n < 180 ? f : nullptr, &got) >= 0);
  if (got) packets.emplace_back(pkt.data, pkt.data + pkt.size);
  av_packet_unref(&pkt);
 }
 av_frame_free(&f);
 avcodec_free_context(&ctx);
 EXPECT_EQ_INT(packets.size(), 180);
 return true;
}
bool Decode(const std::vector<std::vector<uint8_t>> &packets, const std::vector<uint8_t> &extra, int threads, int type, std::vector<uint64_t> &hashes) {
 AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MPEG4);
 AVCodecContext *ctx = avcodec_alloc_context3(codec);
 EXPECT_TRUE(codec && ctx);
 ctx->thread_count = threads; ctx->thread_type = type;
 if (!extra.empty()) {
  ctx->extradata = static_cast<uint8_t *>(av_mallocz(extra.size() + AV_INPUT_BUFFER_PADDING_SIZE));
  EXPECT_TRUE(ctx->extradata != nullptr);
  memcpy(ctx->extradata, extra.data(), extra.size()); ctx->extradata_size = (int)extra.size();
 }
 EXPECT_EQ_INT(avcodec_open2(ctx, codec, nullptr), 0);
 if (threads > 1) EXPECT_EQ_INT(ctx->active_thread_type, type);
 AVFrame *f = av_frame_alloc();
 for (size_t n = 0; n < packets.size() + 16; ++n) {
  AVPacket pkt{}; av_init_packet(&pkt);
  if (n < packets.size()) {
   EXPECT_EQ_INT(av_new_packet(&pkt, (int)packets[n].size()), 0);
   memcpy(pkt.data, packets[n].data(), packets[n].size());
  }
  int got = 0;
  EXPECT_TRUE(avcodec_decode_video2(ctx, f, &got, &pkt) >= 0);
  if (got) {
   uint64_t h = 1469598103934665603ULL;
   for (int plane = 0; plane < 3; ++plane)
    for (int y = 0; y < (plane ? f->height / 2 : f->height); ++y)
     for (int x = 0; x < (plane ? f->width / 2 : f->width); ++x) h = (h ^ f->data[plane][y * f->linesize[plane] + x]) * 1099511628211ULL;
   hashes.push_back(h);
  }
  av_packet_unref(&pkt);
 }
 avcodec_flush_buffers(ctx);
 av_frame_free(&f); avcodec_free_context(&ctx);
 EXPECT_EQ_INT(hashes.size(), packets.size());
 return true;
}
}
#endif

bool TestFFmpegThreading() {
#ifdef TEST_BUNDLED_FFMPEG
 avcodec_register_all();
 RET(QueuesAndPools());
 for (int cycle = 0; cycle < 3; ++cycle) {
  serialValue = 0;
  InitWorker workers[4]; sthread_t *threads[4];
  for (int i = 0; i < 4; ++i) { threads[i] = sthread_create(InitCalls, &workers[i]); EXPECT_TRUE(threads[i]); }
  for (auto *t : threads) sthread_join(t);
  for (auto &w : workers) EXPECT_TRUE(w.ok);
  EXPECT_EQ_INT(serialValue, 4000);
  ff_retro_serial_shutdown();
 }
 RET(SliceJobs());
 std::vector<std::vector<uint8_t>> single, parallel, mpeg;
 std::vector<uint8_t> extra1, extra4, extraMpeg;
 RET(Encode(AV_CODEC_ID_HUFFYUV, 1, single, extra1));
 RET(Encode(AV_CODEC_ID_HUFFYUV, 4, parallel, extra4));
 EXPECT_TRUE(single == parallel && extra1 == extra4);
 RET(Encode(AV_CODEC_ID_MPEG4, 4, mpeg, extraMpeg));
 std::vector<uint64_t> reference;
 RET(Decode(mpeg, extraMpeg, 1, FF_THREAD_FRAME, reference));
 for (int type : {FF_THREAD_FRAME}) {
  std::vector<uint64_t> hashes;
  RET(Decode(mpeg, extraMpeg, 4, type, hashes));
  EXPECT_TRUE(hashes == reference);
 }
 ff_retro_serial_shutdown();
#endif
 return true;
}
