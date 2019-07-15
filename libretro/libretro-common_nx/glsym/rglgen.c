/* Copyright (C) 2010-2018 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this libretro SDK code part (glsym).
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

#include <stdint.h>
#include <string.h>

#include <glsym/rglgen.h>
#include <glsym/glsym.h>

void rglgen_resolve_symbols_custom(rglgen_proc_address_t proc,
      const struct rglgen_sym_map *map)
{
   for (; map->sym; map++)
   {
      rglgen_func_t func = proc(map->sym);
      memcpy(map->ptr, &func, sizeof(func));
   }
}

void rglgen_resolve_symbols(rglgen_proc_address_t proc)
{
   if (!proc)
      return;

   rglgen_resolve_symbols_custom(proc, rglgen_symbol_map);
}
