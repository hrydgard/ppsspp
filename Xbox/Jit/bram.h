/*
 * Helper for use with the PSP Software Development Kit - http://www.pspdev.org
 * -----------------------------------------------------------------------
 * Licensed under GPL
 *
 * vram.c - Standard C high performance VRAM allocation routines.
 *
 * Copyright (c) 2007 Alexander Berl 'Raphael' <raphael@fx-world.org>
 * http://wordpress.fx-world.org
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef vram_h__
#define vram_h__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* brelptr( void *ptr );		// make a pointer relative to memory base address (ATTENTION: A NULL rel ptr is not illegal/invalid!)
void* babsptr( void *ptr );		// make a pointer absolute (default return type of valloc)

_Check_return_ _Ret_opt_bytecap_(_Size) _CRTIMP _CRT_JIT_INTRINSIC _CRTNOALIAS _CRTRESTRICT void * __cdecl balloc(_In_ size_t _Size);
//void* balloc( size_t size );
_CRTIMP _CRTNOALIAS void   __cdecl bfree(_Inout_opt_ void * _Memory);

//void bfree( void* ptr );
size_t bmemavail();
size_t blargestblock();


#ifdef _DEBUG
// Debug printf (to stdout) a trace of the current Memblocks
void __memwalk();
#endif


#ifdef __cplusplus
}
#endif

#endif  // ifdef vram_h__
