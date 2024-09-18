/* Copyright  (C) 2010-2018 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (libco.h).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef LIBCO_H
#define LIBCO_H

#include <retro_common_api.h>

#ifdef LIBCO_C
  #ifdef LIBCO_MP
    #define thread_local __thread
  #else
    #define thread_local
  #endif
#endif

RETRO_BEGIN_DECLS

typedef void* cothread_t;

/**
 * co_active:
 *
 * Gets the currently active context.
 *
 * Returns: active context.
 **/
cothread_t co_active(void);

/**
 * co_create:
 * @int                : stack size
 * @funcptr            : thread entry function callback
 *
 * Create a co_thread.
 *
 * Returns: cothread if successful, otherwise NULL.
 */
cothread_t co_create(unsigned int, void (*)(void));

/**
 * co_delete:
 * @cothread           : cothread object
 *
 * Frees a co_thread.
 */
void co_delete(cothread_t cothread);

/**
 * co_switch:
 * @cothread           : cothread object to switch to
 *
 * Do a context switch to @cothread.
 */
void co_switch(cothread_t cothread);

RETRO_END_DECLS

/* ifndef LIBCO_H */
#endif
