/*
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

/* Slice workers own stable thread IDs. Job descriptors are published once
 * per execute and remain immutable until every worker acknowledges completion.
 * Keep each worker's initial job equal to its ID: progress2's predecessor
 * dependencies rely on that assignment, not arbitrary pool scheduling. */
#include "config.h"
#include "avcodec.h"
#include "internal.h"
#include "pthread_internal.h"
#include "thread.h"
#include "libavutil/avassert.h"
#include "libavutil/cpu.h"
#include "libavutil/mem.h"
#include "libavutil/retro_thread.h"

typedef int (action_func)(AVCodecContext *, void *);
typedef int (action_func2)(AVCodecContext *, void *, int, int);
typedef struct SliceWorker {
    AVCodecContext *avctx;
    sthread_t *thread;
    int id;
} SliceWorker;
typedef struct SliceThreadContext {
    SliceWorker *workers;
    action_func *func;
    action_func2 *func2;
    void *args;
    int *rets;
    int job_count;
    int job_size;
    int workers_created;
    retro_atomic_int_t generation;
    retro_atomic_int_t next_job;
    retro_atomic_int_t remaining;
    retro_atomic_int_t done;
    retro_atomic_int_t *entries;
    int entries_count;
    int thread_count;
} SliceThreadContext;

static void attribute_align_arg worker(void *arg)
{
    SliceWorker *w = arg;
    AVCodecContext *avctx = w->avctx;
    SliceThreadContext *c = avctx->internal->thread_ctx;
    int seen = 0;
    for (;;) {
        int job;
        FF_RETRO_WAIT(&c->generation,
            retro_atomic_load_acquire_int(&c->generation) != seen ||
            retro_atomic_load_acquire_int(&c->done));
        if (retro_atomic_load_acquire_int(&c->done)) return;
        seen = retro_atomic_load_acquire_int(&c->generation);
        job = w->id;
        while (job < c->job_count) {
            int result = c->func ? c->func(avctx, (char *)c->args + job * c->job_size) :
                                  c->func2(avctx, c->args, job, w->id);
            if (c->rets) c->rets[job] = result;
            job = retro_atomic_fetch_add_int(&c->next_job, 1);
        }
        if (retro_atomic_fetch_sub_int(&c->remaining, 1) == 1)
            FF_RETRO_NOTIFY(&c->remaining);
    }
}

void ff_slice_thread_free(AVCodecContext *avctx)
{
    SliceThreadContext *c = avctx->internal->thread_ctx;
    int i;
    if (!c) return;
    retro_atomic_store_release_int(&c->done, 1);
    FF_RETRO_NOTIFY(&c->generation);
    FF_RETRO_NOTIFY(c);
    for (i = 0; i < c->workers_created; ++i) sthread_join(c->workers[i].thread);
    av_freep(&c->entries);
    av_freep(&c->workers);
    av_freep(&avctx->internal->thread_ctx);
}

static int execute(AVCodecContext *avctx, action_func *func, action_func2 *func2,
                   void *args, int *rets, int jobs, int size)
{
    SliceThreadContext *c = avctx->internal->thread_ctx;
    if (!(avctx->active_thread_type & FF_THREAD_SLICE) || avctx->thread_count <= 1)
        return func ? avcodec_default_execute(avctx, func, args, rets, jobs, size) :
                      avcodec_default_execute2(avctx, func2, args, rets, jobs);
    if (jobs <= 0) return 0;
    c->func = func;
    c->func2 = func2;
    c->args = args;
    c->rets = rets;
    c->job_count = jobs;
    c->job_size = size;
    retro_atomic_store_relaxed_int(&c->next_job, avctx->thread_count);
    retro_atomic_store_relaxed_int(&c->remaining, avctx->thread_count);
    retro_atomic_fetch_add_int(&c->generation, 1);
    FF_RETRO_NOTIFY(&c->generation);
    FF_RETRO_WAIT(&c->remaining, retro_atomic_load_acquire_int(&c->remaining) == 0);
    return 0;
}
static int thread_execute(AVCodecContext *ctx, action_func *func, void *args,
                          int *rets, int jobs, int size)
{
    return execute(ctx, func, NULL, args, rets, jobs, size);
}
static int thread_execute2(AVCodecContext *ctx, action_func2 *func, void *args,
                           int *rets, int jobs)
{
    return execute(ctx, NULL, func, args, rets, jobs, 0);
}

int ff_slice_thread_init(AVCodecContext *avctx)
{
    SliceThreadContext *c;
    int i, count = avctx->thread_count;
    if (av_codec_is_encoder(avctx->codec) && avctx->codec_id == AV_CODEC_ID_MPEG1VIDEO && avctx->height > 2800)
        count = avctx->thread_count = 1;
    if (!count) {
        int cpus = av_cpu_count();
        if (avctx->height) cpus = FFMIN(cpus, (avctx->height + 15) / 16);
        count = avctx->thread_count = cpus > 1 ? FFMIN(cpus + 1, MAX_AUTO_THREADS) : 1;
    }
    if (count <= 1) { avctx->active_thread_type = 0; return 0; }
    c = av_mallocz(sizeof(*c));
    if (!c) return AVERROR(ENOMEM);
    c->workers = av_mallocz_array(count, sizeof(*c->workers));
    if (!c->workers) { av_free(c); return AVERROR(ENOMEM); }
    retro_atomic_int_init(&c->generation, 0);
    retro_atomic_int_init(&c->next_job, 0);
    retro_atomic_int_init(&c->remaining, 0);
    retro_atomic_int_init(&c->done, 0);
    avctx->internal->thread_ctx = c;
    for (i = 0; i < count; ++i) {
        SliceWorker *w = &c->workers[i];
        w->avctx = avctx;
        w->id = i;
        w->thread = sthread_create(worker, w);
        if (!w->thread) { ff_slice_thread_free(avctx); return AVERROR(ENOMEM); }
        ++c->workers_created;
    }
    avctx->execute = thread_execute;
    avctx->execute2 = thread_execute2;
    return 0;
}

void ff_thread_report_progress2(AVCodecContext *avctx, int field, int thread, int n)
{
    SliceThreadContext *c = avctx->internal->thread_ctx;
    retro_atomic_fetch_add_int(&c->entries[field], n);
    FF_RETRO_NOTIFY(c);
}
void ff_thread_await_progress2(AVCodecContext *avctx, int field, int thread, int shift)
{
    SliceThreadContext *c = avctx->internal->thread_ctx;
    if (!c->entries || !field) return;
    FF_RETRO_WAIT(c, retro_atomic_load_acquire_int(&c->entries[field - 1]) -
                     retro_atomic_load_acquire_int(&c->entries[field]) >= shift ||
                     retro_atomic_load_acquire_int(&c->done));
}
int ff_alloc_entries(AVCodecContext *avctx, int count)
{
    if (avctx->active_thread_type & FF_THREAD_SLICE) {
        SliceThreadContext *c = avctx->internal->thread_ctx;
        int i;
        retro_atomic_int_t *entries = av_malloc_array(count, sizeof(*entries));
        if (!entries) return AVERROR(ENOMEM);
        for (i = 0; i < count; ++i) retro_atomic_int_init(&entries[i], 0);
        av_free(c->entries);
        c->entries = entries;
        c->entries_count = count;
        c->thread_count = avctx->thread_count;
    }
    return 0;
}
void ff_reset_entries(AVCodecContext *avctx)
{
    SliceThreadContext *c = avctx->internal->thread_ctx;
    int i;
    for (i = 0; i < c->entries_count; ++i) retro_atomic_store_relaxed_int(&c->entries[i], 0);
}
