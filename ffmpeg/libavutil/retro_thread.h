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

#ifndef AVUTIL_RETRO_THREAD_H
#define AVUTIL_RETRO_THREAD_H
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>

/* Process-lifetime parking shards are supplied by the libretro host. A
 * notification only hashes its address, so publishing completion may hand
 * ownership back to a caller which immediately frees the completed object. */
retro_eventcount_t *ff_retro_parking_for(const void *address);
int ff_retro_serial_call(int (*call)(void *), void *arg);
void ff_retro_serial_shutdown(void);

#define FF_RETRO_WAIT(address, predicate) do { \
    retro_eventcount_t *ec = ff_retro_parking_for(address); \
    while (!(predicate)) { \
        int key = retro_eventcount_prepare_wait(ec); \
        if (predicate) retro_eventcount_cancel_wait(ec); \
        else retro_eventcount_commit_wait(ec, key); \
    } \
} while (0)
#define FF_RETRO_NOTIFY(address) retro_eventcount_notify(ff_retro_parking_for(address))

typedef retro_atomic_int_t AVOnce;
#define AV_ONCE_INIT RETRO_ATOMIC_INT_INITIALIZER(0)
static inline int ff_thread_once(AVOnce *control, void (*routine)(void))
{
    if (retro_atomic_load_acquire_int(control) == 2) return 0;
    if (retro_atomic_cas_int(control, 0, 1)) {
        routine();
        retro_atomic_store_release_int(control, 2);
        FF_RETRO_NOTIFY(control);
    } else {
        FF_RETRO_WAIT(control, retro_atomic_load_acquire_int(control) == 2);
    }
    return 0;
}
#endif
