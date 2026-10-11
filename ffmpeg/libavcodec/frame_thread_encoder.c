/*
 * Copyright (c) 2012 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "frame_thread_encoder.h"

#include <retro_spsc.h>
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/retro_thread.h"
#include "avcodec.h"
#include "internal.h"
#include "thread.h"

#define MAX_THREADS 64
#define BUFFER_SIZE (2*MAX_THREADS)

typedef struct{
    void *indata;
    void *outdata;
    int64_t return_code;
    unsigned index;
} Task;

typedef struct EncoderWorker {
    AVCodecContext *avctx;
    sthread_t *thread;
    retro_spsc_t jobs;
} EncoderWorker;
typedef struct {
    AVCodecContext *parent_avctx;
    Task finished_tasks[BUFFER_SIZE];
    retro_atomic_int_t ready[BUFFER_SIZE];
    unsigned task_index;
    unsigned finished_task_index;
    unsigned submitted;
    unsigned returned;
    EncoderWorker workers[MAX_THREADS];
    int workers_created;
    retro_atomic_int_t exit;
} ThreadContext;

static void attribute_align_arg worker(void *v)
{
    EncoderWorker *w = v;
    AVCodecContext *avctx = w->avctx;
    ThreadContext *c = avctx->internal->frame_thread_encoder;
    for (;;) {
        Task task;
        AVFrame *frame;
        AVPacket *pkt;
        int got_packet = 0, ret;
        FF_RETRO_WAIT(&w->jobs, retro_spsc_read_avail(&w->jobs) >= sizeof(task) ||
                                  retro_atomic_load_acquire_int(&c->exit));
        if (retro_atomic_load_acquire_int(&c->exit)) break;
        av_assert0(retro_spsc_read(&w->jobs, &task, sizeof(task)) == sizeof(task));
        FF_RETRO_NOTIFY(&w->jobs);
        frame = task.indata;
        pkt = av_mallocz(sizeof(*pkt));
        if (pkt) {
            av_init_packet(pkt);
            ret = avcodec_encode_video2(avctx, pkt, frame, &got_packet);
            if (got_packet) {
                int ref_result = av_packet_make_refcounted(pkt);
                if (ref_result < 0) { av_packet_unref(pkt); ret = ref_result; }
            } else av_packet_unref(pkt);
        } else ret = AVERROR(ENOMEM);
        av_frame_free(&frame);
        c->finished_tasks[task.index].outdata = pkt;
        c->finished_tasks[task.index].return_code = ret;
        retro_atomic_store_release_int(&c->ready[task.index], 1);
        FF_RETRO_NOTIFY(&c->ready[task.index]);
    }
}

int ff_frame_thread_encoder_init(AVCodecContext *avctx, AVDictionary *options){
    int i=0;
    ThreadContext *c;


    if(   !(avctx->thread_type & FF_THREAD_FRAME)
       || !(avctx->codec->capabilities & AV_CODEC_CAP_INTRA_ONLY))
        return 0;

    if(   !avctx->thread_count
       && avctx->codec_id == AV_CODEC_ID_MJPEG
       && !(avctx->flags & AV_CODEC_FLAG_QSCALE)) {
        av_log(avctx, AV_LOG_DEBUG,
               "Forcing thread count to 1 for MJPEG encoding, use -thread_type slice "
               "or a constant quantizer if you want to use multiple cpu cores\n");
        avctx->thread_count = 1;
    }
    if(   avctx->thread_count > 1
       && avctx->codec_id == AV_CODEC_ID_MJPEG
       && !(avctx->flags & AV_CODEC_FLAG_QSCALE))
        av_log(avctx, AV_LOG_WARNING,
               "MJPEG CBR encoding works badly with frame multi-threading, consider "
               "using -threads 1, -thread_type slice or a constant quantizer.\n");

    if (avctx->codec_id == AV_CODEC_ID_HUFFYUV ||
        avctx->codec_id == AV_CODEC_ID_FFVHUFF) {
        int warn = 0;
        int context_model = 0;
        AVDictionaryEntry *con = av_dict_get(options, "context", NULL, AV_DICT_MATCH_CASE);

        if (con && con->value)
            context_model = atoi(con->value);

        if (avctx->flags & AV_CODEC_FLAG_PASS1)
            warn = 1;
        else if(context_model > 0) {
            AVDictionaryEntry *t = av_dict_get(options, "non_deterministic",
                                               NULL, AV_DICT_MATCH_CASE);
            warn = !t || !t->value || !atoi(t->value) ? 1 : 0;
        }
        // huffyuv does not support these with multiple frame threads currently
        if (warn) {
            av_log(avctx, AV_LOG_WARNING,
               "Forcing thread count to 1 for huffyuv encoding with first pass or context 1\n");
            avctx->thread_count = 1;
        }
    }

    if(!avctx->thread_count) {
        avctx->thread_count = av_cpu_count();
        avctx->thread_count = FFMIN(avctx->thread_count, MAX_THREADS);
    }

    if(avctx->thread_count <= 1)
        return 0;

    if(avctx->thread_count > MAX_THREADS)
        return AVERROR(EINVAL);

    av_assert0(!avctx->internal->frame_thread_encoder);
    c = avctx->internal->frame_thread_encoder = av_mallocz(sizeof(ThreadContext));
    if(!c)
        return AVERROR(ENOMEM);

    c->parent_avctx = avctx;

    retro_atomic_int_init(&c->exit, 0);
    for (i = 0; i < BUFFER_SIZE; ++i) retro_atomic_int_init(&c->ready[i], 0);
    i = 0;


    for(i=0; i<avctx->thread_count ; i++){
        AVDictionary *tmp = NULL;
        void *tmpv;
        AVCodecContext *thread_avctx = avcodec_alloc_context3(avctx->codec);
        if(!thread_avctx)
            goto fail;
        tmpv = thread_avctx->priv_data;
        *thread_avctx = *avctx;
        thread_avctx->priv_data = tmpv;
        thread_avctx->internal = NULL;
        memcpy(thread_avctx->priv_data, avctx->priv_data, avctx->codec->priv_data_size);
        thread_avctx->thread_count = 1;
        thread_avctx->active_thread_type &= ~FF_THREAD_FRAME;

        av_dict_copy(&tmp, options, 0);
        av_dict_set(&tmp, "threads", "1", 0);
        if(avcodec_open2(thread_avctx, avctx->codec, &tmp) < 0) {
            av_dict_free(&tmp);
            avcodec_free_context(&thread_avctx);
            goto fail;
        }
        av_dict_free(&tmp);
        av_assert0(!thread_avctx->internal->frame_thread_encoder);
        thread_avctx->internal->frame_thread_encoder = c;
        c->workers[i].avctx = thread_avctx;
        if (!retro_spsc_init(&c->workers[i].jobs, 4 * sizeof(Task))) {
            avcodec_free_context(&c->workers[i].avctx);
            goto fail;
        }
        c->workers[i].thread = sthread_create(worker, &c->workers[i]);
        if (!c->workers[i].thread) {
            retro_spsc_free(&c->workers[i].jobs);
            avcodec_free_context(&c->workers[i].avctx);
            goto fail;
        }
        ++c->workers_created;
    }

    avctx->active_thread_type = FF_THREAD_FRAME;

    return 0;
fail:
    avctx->thread_count = i;
    av_log(avctx, AV_LOG_ERROR, "ff_frame_thread_encoder_init failed\n");
    ff_frame_thread_encoder_free(avctx);
    return -1;
}

void ff_frame_thread_encoder_free(AVCodecContext *avctx)
{
    ThreadContext *c = avctx->internal->frame_thread_encoder;
    int i;
    if (!c) return;
    retro_atomic_store_release_int(&c->exit, 1);
    for (i = 0; i < c->workers_created; ++i) FF_RETRO_NOTIFY(&c->workers[i].jobs);
    for (i = 0; i < c->workers_created; ++i) {
        EncoderWorker *w = &c->workers[i];
        Task task;
        sthread_join(w->thread);
        while (retro_spsc_read(&w->jobs, &task, sizeof(task)) == sizeof(task)) {
            AVFrame *frame = task.indata;
            av_frame_free(&frame);
        }
        retro_spsc_free(&w->jobs);
        w->avctx->internal->frame_thread_encoder = NULL;
        avcodec_free_context(&w->avctx);
    }
    for (i = 0; i < BUFFER_SIZE; ++i) {
        AVPacket *pkt = c->finished_tasks[i].outdata;
        if (pkt) { av_packet_unref(pkt); av_free(pkt); }
    }
    av_freep(&avctx->internal->frame_thread_encoder);
}

int ff_thread_video_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                 const AVFrame *frame, int *got_packet_ptr)
{
    ThreadContext *c = avctx->internal->frame_thread_encoder;
    Task task;
    int result;
    unsigned index;
    av_assert1(!*got_packet_ptr);
    if (frame) {
        EncoderWorker *w = &c->workers[c->submitted % c->workers_created];
        AVFrame *copy = av_frame_alloc();
        if (!copy) return AVERROR(ENOMEM);
        result = av_frame_ref(copy, frame);
        if (result < 0) { av_frame_free(&copy); return result; }
        task.index = c->task_index;
        task.indata = copy;
        FF_RETRO_WAIT(&w->jobs, retro_spsc_write_avail(&w->jobs) >= sizeof(task));
        av_assert0(retro_spsc_write(&w->jobs, &task, sizeof(task)) == sizeof(task));
        FF_RETRO_NOTIFY(&w->jobs);
        ++c->submitted;
        c->task_index = (c->task_index + 1) % BUFFER_SIZE;
        if (!retro_atomic_load_acquire_int(&c->ready[c->finished_task_index]) &&
            c->submitted - c->returned <= avctx->thread_count)
            return 0;
    }
    if (c->submitted == c->returned) return 0;
    index = c->finished_task_index;
    FF_RETRO_WAIT(&c->ready[index], retro_atomic_load_acquire_int(&c->ready[index]));
    task = c->finished_tasks[index];
    result = task.return_code;
    if (task.outdata) {
        AVPacket *out = task.outdata;
        *pkt = *out;
        if (pkt->data) *got_packet_ptr = 1;
        av_free(out);
    }
    c->finished_tasks[index].outdata = NULL;
    retro_atomic_store_release_int(&c->ready[index], 0);
    ++c->returned;
    c->finished_task_index = (index + 1) % BUFFER_SIZE;
    return result;
}
