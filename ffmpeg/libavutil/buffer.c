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

#include <stdint.h>
#include <string.h>

#include "atomic.h"
#include "buffer_internal.h"
#include "common.h"
#include "mem.h"
#include "thread.h"

AVBufferRef *av_buffer_create(uint8_t *data, int size,
                              void (*free)(void *opaque, uint8_t *data),
                              void *opaque, int flags)
{
    AVBufferRef *ref = NULL;
    AVBuffer    *buf = NULL;

    buf = av_mallocz(sizeof(*buf));
    if (!buf)
        return NULL;

    buf->data     = data;
    buf->size     = size;
    buf->free     = free ? free : av_buffer_default_free;
    buf->opaque   = opaque;
    retro_atomic_int_init(&buf->refcount, 1);

    if (flags & AV_BUFFER_FLAG_READONLY)
        buf->flags |= BUFFER_FLAG_READONLY;

    ref = av_mallocz(sizeof(*ref));
    if (!ref) {
        av_freep(&buf);
        return NULL;
    }

    ref->buffer = buf;
    ref->data   = data;
    ref->size   = size;

    return ref;
}

void av_buffer_default_free(void *opaque, uint8_t *data)
{
    av_free(data);
}

AVBufferRef *av_buffer_alloc(int size)
{
    AVBufferRef *ret = NULL;
    uint8_t    *data = NULL;

    data = av_malloc(size);
    if (!data)
        return NULL;

    ret = av_buffer_create(data, size, av_buffer_default_free, NULL, 0);
    if (!ret)
        av_freep(&data);

    return ret;
}

AVBufferRef *av_buffer_allocz(int size)
{
    AVBufferRef *ret = av_buffer_alloc(size);
    if (!ret)
        return NULL;

    memset(ret->data, 0, size);
    return ret;
}

AVBufferRef *av_buffer_ref(AVBufferRef *buf)
{
    AVBufferRef *ret = av_mallocz(sizeof(*ret));

    if (!ret)
        return NULL;

    *ret = *buf;

    retro_atomic_fetch_add_int(&buf->buffer->refcount, 1);

    return ret;
}

static void buffer_replace(AVBufferRef **dst, AVBufferRef **src)
{
    AVBuffer *b;

    b = (*dst)->buffer;

    if (src) {
        **dst = **src;
        av_freep(src);
    } else
        av_freep(dst);

    if (retro_atomic_fetch_sub_int(&b->refcount, 1) == 1) {
        b->free(b->opaque, b->data);
        av_freep(&b);
    }
}

void av_buffer_unref(AVBufferRef **buf)
{
    if (!buf || !*buf)
        return;

    buffer_replace(buf, NULL);
}

int av_buffer_is_writable(const AVBufferRef *buf)
{
    if (buf->buffer->flags & AV_BUFFER_FLAG_READONLY)
        return 0;

    return retro_atomic_load_acquire_int(&buf->buffer->refcount) == 1;
}

void *av_buffer_get_opaque(const AVBufferRef *buf)
{
    return buf->buffer->opaque;
}

int av_buffer_get_ref_count(const AVBufferRef *buf)
{
    return retro_atomic_load_acquire_int(&buf->buffer->refcount);
}

int av_buffer_make_writable(AVBufferRef **pbuf)
{
    AVBufferRef *newbuf, *buf = *pbuf;

    if (av_buffer_is_writable(buf))
        return 0;

    newbuf = av_buffer_alloc(buf->size);
    if (!newbuf)
        return AVERROR(ENOMEM);

    memcpy(newbuf->data, buf->data, buf->size);

    buffer_replace(pbuf, &newbuf);

    return 0;
}

int av_buffer_realloc(AVBufferRef **pbuf, int size)
{
    AVBufferRef *buf = *pbuf;
    uint8_t *tmp;

    if (!buf) {
        /* allocate a new buffer with av_realloc(), so it will be reallocatable
         * later */
        uint8_t *data = av_realloc(NULL, size);
        if (!data)
            return AVERROR(ENOMEM);

        buf = av_buffer_create(data, size, av_buffer_default_free, NULL, 0);
        if (!buf) {
            av_freep(&data);
            return AVERROR(ENOMEM);
        }

        buf->buffer->flags |= BUFFER_FLAG_REALLOCATABLE;
        *pbuf = buf;

        return 0;
    } else if (buf->size == size)
        return 0;

    if (!(buf->buffer->flags & BUFFER_FLAG_REALLOCATABLE) ||
        !av_buffer_is_writable(buf)) {
        /* cannot realloc, allocate a new reallocable buffer and copy data */
        AVBufferRef *new = NULL;

        av_buffer_realloc(&new, size);
        if (!new)
            return AVERROR(ENOMEM);

        memcpy(new->data, buf->data, FFMIN(size, buf->size));

        buffer_replace(pbuf, &new);
        return 0;
    }

    tmp = av_realloc(buf->buffer->data, size);
    if (!tmp)
        return AVERROR(ENOMEM);

    buf->buffer->data = buf->data = tmp;
    buf->buffer->size = buf->size = size;
    return 0;
}

AVBufferPool *av_buffer_pool_init(int size, AVBufferRef* (*alloc)(int size))
{
    AVBufferPool *pool = av_mallocz(sizeof(*pool));
    if (!pool) return NULL;
    retro_atomic_ptr_init(&pool->pool, NULL);
    retro_atomic_int_init(&pool->refcount, 1);
    pool->size = size;
    pool->alloc = alloc ? alloc : av_buffer_alloc;
    return pool;
}
static BufferPoolEntry *get_pool(AVBufferPool *pool)
{
    /* Detach the whole chain. No reader traverses a shared/recycled next link. */
    return retro_atomic_exchange_ptr(&pool->pool, NULL);
}
static void add_to_pool(BufferPoolEntry *buf)
{
    BufferPoolEntry *end, *head;
    AVBufferPool *pool;
    if (!buf) return;
    pool = buf->pool;
    end = buf;
    while (end->next) end = end->next;
    do {
        head = retro_atomic_load_acquire_ptr(&pool->pool);
        end->next = head;
    } while (!retro_atomic_cas_ptr(&pool->pool, head, buf));
}
static void buffer_pool_free(AVBufferPool *pool)
{
    BufferPoolEntry *buf = get_pool(pool);
    while (buf) {
        BufferPoolEntry *next = buf->next;
        buf->free(buf->opaque, buf->data);
        av_free(buf);
        buf = next;
    }
    av_free(pool);
}
void av_buffer_pool_uninit(AVBufferPool **ppool)
{
    AVBufferPool *pool;
    if (!ppool || !*ppool) return;
    pool = *ppool;
    *ppool = NULL;
    if (retro_atomic_fetch_sub_int(&pool->refcount, 1) == 1)
        buffer_pool_free(pool);
}
static void pool_release_buffer(void *opaque, uint8_t *data)
{
    BufferPoolEntry *buf = opaque;
    AVBufferPool *pool = buf->pool;
#if CONFIG_MEMORY_POISONING
    memset(buf->data, FF_MEMORY_POISON, pool->size);
#endif
    add_to_pool(buf);
    if (retro_atomic_fetch_sub_int(&pool->refcount, 1) == 1)
        buffer_pool_free(pool);
}
static AVBufferRef *pool_alloc_buffer(AVBufferPool *pool)
{
    BufferPoolEntry *buf;
    AVBufferRef     *ret;

    ret = pool->alloc(pool->size);
    if (!ret)
        return NULL;

    buf = av_mallocz(sizeof(*buf));
    if (!buf) {
        av_buffer_unref(&ret);
        return NULL;
    }

    buf->data   = ret->buffer->data;
    buf->opaque = ret->buffer->opaque;
    buf->free   = ret->buffer->free;
    buf->pool   = pool;

    ret->buffer->opaque = buf;
    ret->buffer->free   = pool_release_buffer;

    retro_atomic_fetch_add_int(&pool->refcount, 1);

    return ret;
}

AVBufferRef *av_buffer_pool_get(AVBufferPool *pool)
{
    AVBufferRef *ret;
    BufferPoolEntry *buf = get_pool(pool);
    if (!buf) return pool_alloc_buffer(pool);
    add_to_pool(buf->next);
    buf->next = NULL;
    ret = av_buffer_create(buf->data, pool->size, pool_release_buffer, buf, 0);
    if (!ret) { add_to_pool(buf); return NULL; }
    retro_atomic_fetch_add_int(&pool->refcount, 1);
    return ret;
}
