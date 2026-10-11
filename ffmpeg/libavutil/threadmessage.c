/*
 * Copyright (c) 2014 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "threadmessage.h"
#include "retro_thread.h"
#include "mem.h"
#include "error.h"
#include <limits.h>
#include <string.h>

/* Bounded MPMC sequence ring. A reserved slot has one writer or reader;
 * release publication transfers its payload. Waits register before checking
 * the slot again, so empty/full transitions cannot lose a notification. */
struct AVThreadMessageQueue {
    uint8_t *data;
    retro_atomic_int_t *sequence;
    retro_atomic_int_t enqueue;
    retro_atomic_int_t dequeue;
    retro_atomic_int_t reserved;
    retro_atomic_int_t err_send;
    retro_atomic_int_t err_recv;
    unsigned capacity, limit, elsize;
    retro_atomic_ptr_t free_func;
};

int av_thread_message_queue_alloc(AVThreadMessageQueue **mq, unsigned nelem, unsigned elsize)
{
    AVThreadMessageQueue *q;
    unsigned capacity = 2, i;
    if (!nelem || !elsize || nelem > INT_MAX / 2) return AVERROR(EINVAL);
    while (capacity < nelem) capacity *= 2;
    if (capacity > INT_MAX / elsize) return AVERROR(EINVAL);
    q = av_mallocz(sizeof(*q));
    if (!q) return AVERROR(ENOMEM);
    q->data = av_malloc_array(capacity, elsize);
    q->sequence = av_malloc_array(capacity, sizeof(*q->sequence));
    if (!q->data || !q->sequence) {
        av_free(q->data); av_free(q->sequence); av_free(q);
        return AVERROR(ENOMEM);
    }
    retro_atomic_ptr_init(&q->free_func, NULL);
    q->capacity = capacity; q->limit = nelem; q->elsize = elsize;
    retro_atomic_int_init(&q->enqueue, 0);
    retro_atomic_int_init(&q->dequeue, 0);
    retro_atomic_int_init(&q->reserved, 0);
    retro_atomic_int_init(&q->err_send, 0);
    retro_atomic_int_init(&q->err_recv, 0);
    retro_atomic_ptr_init(&q->free_func, NULL);
    for (i = 0; i < capacity; ++i) retro_atomic_int_init(&q->sequence[i], i);
    *mq = q;
    return 0;
}
void av_thread_message_queue_set_free_func(AVThreadMessageQueue *q, void (*fn)(void *))
{
    retro_atomic_store_release_ptr(&q->free_func, (void *)fn);
}

static int try_recv(AVThreadMessageQueue *q, void *msg, int bounded, unsigned stop)
{
    unsigned pos = (unsigned)retro_atomic_load_relaxed_int(&q->dequeue), slot;
    for (;;) {
        int difference;
        if (bounded && (int)(pos - stop) >= 0) return 0;
        slot = pos & (q->capacity - 1);
        difference = (int)((unsigned)retro_atomic_load_acquire_int(&q->sequence[slot]) - (pos + 1));
        if (difference < 0) return 0;
        if (!difference && retro_atomic_cas_int(&q->dequeue, (int)pos, (int)(pos + 1))) break;
        pos = (unsigned)retro_atomic_load_relaxed_int(&q->dequeue);
    }
    if (msg) memcpy(msg, q->data + slot * q->elsize, q->elsize);
    else {
        void (*free_func)(void *) = (void (*)(void *))retro_atomic_load_acquire_ptr(&q->free_func);
        if (free_func) free_func(q->data + slot * q->elsize);
    }
    retro_atomic_store_release_int(&q->sequence[slot], (int)(pos + q->capacity));
    retro_atomic_fetch_sub_int(&q->reserved, 1);
    FF_RETRO_NOTIFY(q);
    return 1;
}

int av_thread_message_queue_send(AVThreadMessageQueue *q, void *msg, unsigned flags)
{
    unsigned pos, slot;
    for (;;) {
        int error = retro_atomic_load_acquire_int(&q->err_send);
        int count = retro_atomic_load_acquire_int(&q->reserved);
        if (error) return error;
        if ((unsigned)count < q->limit && retro_atomic_cas_int(&q->reserved, count, count + 1)) break;
        if (flags & AV_THREAD_MESSAGE_NONBLOCK) return AVERROR(EAGAIN);
        FF_RETRO_WAIT(q, (unsigned)retro_atomic_load_acquire_int(&q->reserved) < q->limit ||
                         retro_atomic_load_acquire_int(&q->err_send));
    }
    for (;;) {
        pos = (unsigned)retro_atomic_load_acquire_int(&q->enqueue);
        slot = pos & (q->capacity - 1);
        if ((unsigned)retro_atomic_load_acquire_int(&q->sequence[slot]) == pos &&
            retro_atomic_cas_int(&q->enqueue, (int)pos, (int)(pos + 1))) break;
        if ((flags & AV_THREAD_MESSAGE_NONBLOCK) || retro_atomic_load_acquire_int(&q->err_send)) {
            int error = retro_atomic_load_acquire_int(&q->err_send);
            retro_atomic_fetch_sub_int(&q->reserved, 1);
            FF_RETRO_NOTIFY(q);
            return error ? error : AVERROR(EAGAIN);
        }
        FF_RETRO_WAIT(q, (unsigned)retro_atomic_load_acquire_int(&q->sequence[slot]) == pos ||
                         (unsigned)retro_atomic_load_acquire_int(&q->enqueue) != pos || retro_atomic_load_acquire_int(&q->err_send));
    }
    memcpy(q->data + slot * q->elsize, msg, q->elsize);
    retro_atomic_store_release_int(&q->sequence[slot], (int)(pos + 1));
    FF_RETRO_NOTIFY(q);
    return 0;
}
int av_thread_message_queue_recv(AVThreadMessageQueue *q, void *msg, unsigned flags)
{
    for (;;) {
        int error;
        unsigned pos, slot;
        if (try_recv(q, msg, 0, 0)) return 0;
        error = retro_atomic_load_acquire_int(&q->err_recv);
        if (error) return error;
        if (flags & AV_THREAD_MESSAGE_NONBLOCK) return AVERROR(EAGAIN);
        pos = (unsigned)retro_atomic_load_acquire_int(&q->dequeue);
        slot = pos & (q->capacity - 1);
        FF_RETRO_WAIT(q, (int)((unsigned)retro_atomic_load_acquire_int(&q->sequence[slot]) - (pos + 1)) >= 0 ||
                         retro_atomic_load_acquire_int(&q->err_recv));
    }
}
void av_thread_message_queue_set_err_send(AVThreadMessageQueue *q, int err)
{
    retro_atomic_store_release_int(&q->err_send, err);
    FF_RETRO_NOTIFY(q);
}
void av_thread_message_queue_set_err_recv(AVThreadMessageQueue *q, int err)
{
    retro_atomic_store_release_int(&q->err_recv, err);
    FF_RETRO_NOTIFY(q);
}
void av_thread_message_flush(AVThreadMessageQueue *q)
{
    /* Bound the drain to the messages claimed before the snapshot. Process
     * each payload in its owned slot, so flush/free cannot fail to allocate. */
    unsigned stop = (unsigned)retro_atomic_load_acquire_int(&q->enqueue);
    while (try_recv(q, NULL, 1, stop)) {}

}
void av_thread_message_queue_free(AVThreadMessageQueue **mq)
{
    AVThreadMessageQueue *q;
    if (!mq || !*mq) return;
    q = *mq; *mq = NULL;
    av_thread_message_flush(q);
    av_free(q->data); av_free(q->sequence); av_free(q);
}
