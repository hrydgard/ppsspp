// Copyright (C) 2003 Dolphin Project / 2012 PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

// Includes
#include <string>
#include "Common.h"
#include "CommonTypes.h"

#include "HDRemaster.h"

// PPSSPP is very aggressive about trying to do memory accesses directly, for speed.
// This can be a problem when debugging though, as stray memory reads and writes will
// crash the whole emulator.
// If safe memory is enabled and JIT is disabled, all memory access will go through the proper
// memory access functions, and thus won't crash the emu when they go out of bounds.
#if defined(_DEBUG)
//#define SAFE_MEMORY
#endif

// Global declarations
class PointerWrap;

typedef void (*writeFn8 )(const u8, const u32);
typedef void (*writeFn16)(const u16,const u32);
typedef void (*writeFn32)(const u32,const u32);
typedef void (*writeFn64)(const u64,const u32);

typedef void (*readFn8 )(u8&,  const u32);
typedef void (*readFn16)(u16&, const u32);
typedef void (*readFn32)(u32&, const u32);
typedef void (*readFn64)(u64&, const u32);

namespace Memory
{
// Base is a pointer to the base of the memory map. Yes, some MMU tricks
// are used to set up a full GC or Wii memory map in process memory.  on
// 32-bit, you have to mask your offsets with 0x3FFFFFFF. This means that
// some things are mirrored too many times, but eh... it works.

// In 64-bit, this might point to "high memory" (above the 32-bit limit),
// so be sure to load it into a 64-bit register.
extern u8 *base; 

// These are guaranteed to point to "low memory" addresses (sub-32-bit).
// 64-bit: Pointers to low-mem (sub-0x10000000) mirror
// 32-bit: Same as the corresponding physical/virtual pointers.
extern u8 *m_pRAM;
extern u8 *m_pScratchPad;
extern u8 *m_pVRAM;

// 64-bit: Pointers to high-mem mirrors
// 32-bit: Same as above
extern u8 *m_pPhysicalRAM;
extern u8 *m_pUncachedRAM;

extern u8 *m_pPhysicalVRAM;
extern u8 *m_pUncachedVRAM;

// These replace RAM_NORMAL_SIZE and RAM_NORMAL_MASK, respectively.
extern u32 g_MemorySize;
extern u32 g_MemoryMask;
extern u32 g_PSPModel;

enum
{
	// This may be adjusted by remaster games.
	RAM_NORMAL_SIZE = 0x02000000,
	RAM_NORMAL_MASK = RAM_NORMAL_SIZE - 1,

	// Used if the PSP model is PSP-2000 (Slim).
	RAM_DOUBLE_SIZE = RAM_NORMAL_SIZE * 2,

	VRAM_SIZE       = 0x200000,
	VRAM_MASK       = VRAM_SIZE - 1,

	SCRATCHPAD_SIZE = 0x4000,
	SCRATCHPAD_MASK = SCRATCHPAD_SIZE - 1,


#if defined(_M_IX86) || defined(_M_ARM32) || defined(_XBOX)
  // This wraparound should work for PSP too.
	MEMVIEW32_MASK  = 0x3FFFFFFF,
#endif
};

// Init and Shutdown
void Init();
void Shutdown();
void DoState(PointerWrap &p);
void Clear();

struct Opcode {
	Opcode() {
	}

	explicit Opcode(u32 v) : encoding (v) {
	}

	u32 operator & (const u32 &arg) const {
		return encoding & arg;
	}

	u32 operator >> (const u32 &arg) const {
		return encoding >> arg;
	}

	bool operator == (const u32 &arg) const {
		return encoding == arg;
	}

	bool operator != (const u32 &arg) const {
		return encoding != arg;
	}

	u32 encoding;
};

// used by JIT to read instructions
Opcode Read_Opcode_JIT(const u32 _Address);
// used by JIT. uses iCacheJIT. Reads in the "Locked cache" mode
void Write_Opcode_JIT(const u32 _Address, const Opcode _Value);
// this is used by Debugger a lot. 
// For now, just reads from memory!
Opcode Read_Instruction(const u32 _Address);


// For use by emulator

u8  Read_U8(const u32 _Address);
u16 Read_U16(const u32 _Address);
u32 Read_U32(const u32 _Address);
u64 Read_U64(const u32 _Address);

#if (defined(ARM) || defined(_ARM)) && !defined(_M_ARM)
#define _M_ARM
#endif

inline u8* GetPointerUnchecked(const u32 address) {
#if defined(_M_IX86) || defined(_M_ARM32)
	return (u8 *)(base + (address & MEMVIEW32_MASK));
#else
	return (u8 *)(base + address);
#endif
}

#ifdef SAFE_MEMORY
u32 ReadUnchecked_U32(const u32 _Address);
// ONLY for use by GUI and fast interpreter
u8 ReadUnchecked_U8(const u32 _Address);
u16 ReadUnchecked_U16(const u32 _Address);
void WriteUnchecked_U8(const u8 _Data, const u32 _Address);
void WriteUnchecked_U16(const u16 _Data, const u32 _Address);
void WriteUnchecked_U32(const u32 _Data, const u32 _Address);
#else

inline u32 ReadUnchecked_U32(const u32 address) {
#if defined(_M_IX86) || defined(_M_ARM32) || defined (_XBOX)
  return *(u32_le *)(base + (address & MEMVIEW32_MASK));
#else
  return *(u32_le *)(base + address);
#endif
}

inline u16 ReadUnchecked_U16(const u32 address) {
#if defined(_M_IX86) || defined(_M_ARM32) || defined (_XBOX)
	return *(u16_le *)(base + (address & MEMVIEW32_MASK));
#else
	return *(u16_le *)(base + address);
#endif
}

inline u8 ReadUnchecked_U8(const u32 address) {
#if defined(_M_IX86) || defined(_M_ARM32) || defined (_XBOX)
	return (*(u8 *)(base + (address & MEMVIEW32_MASK))); 
#else
	return (*(u8 *)(base + address));
#endif
}

inline void WriteUnchecked_U32(u32 data, u32 address) {
#if defined(_M_IX86) || defined(_M_ARM32) || defined (_XBOX)
	*(u32_le *)(base + (address & MEMVIEW32_MASK)) = data;
#else
	*(u32_le *)(base + address) = data;
#endif
}

inline void WriteUnchecked_U16(u16 data, u32 address) {
#if defined(_M_IX86) || defined(_M_ARM32) || defined (_XBOX)
	*(u16_le *)(base + (address & MEMVIEW32_MASK)) = data;
#else
	*(u16_le *)(base + address) = data;
#endif
}

inline void WriteUnchecked_U8(u8 data, u32 address) {
#if defined(_M_IX86) || defined(_M_ARM32) || defined (_XBOX)
	(*(u8 *)(base + (address & MEMVIEW32_MASK))) = data;
#else
	(*(u8 *)(base + address)) = data;
#endif
}

#endif

inline float Read_Float(u32 address) 
{
  u32 ifloat = Read_U32(address);
  float f;
  memcpy(&f, &ifloat, sizeof(float));
  return f;
}

// used by JIT. Return zero-extended 32bit values
u32 Read_U8_ZX(const u32 address);
u32 Read_U16_ZX(const u32 address);

void Write_U8(const u8 data, const u32 address);
void Write_U16(const u16 data, const u32 address);
void Write_U32(const u32 data, const u32 address);
void Write_U64(const u64 data, const u32 address);

inline void Write_Float(float f, u32 address)
{
  u32 u;
  memcpy(&u, &f, sizeof(float));
  Write_U32(u, address);
}

// Reads a zero-terminated string from memory at the address.
void GetString(std::string& _string, const u32 _Address);
u8* GetPointer(const u32 address);
bool IsRAMAddress(const u32 address);
bool IsVRAMAddress(const u32 address);

inline const char* GetCharPointer(const u32 address) {
  return (const char *)GetPointer(address);
}

void Memset(const u32 _Address, const u8 _Data, const u32 _iLength);

inline void Memcpy(const u32 to_address, const void *from_data, const u32 len)
{
	u8 *to = GetPointer(to_address);
	if (to) {
		memcpy(to, from_data, len);
	}
	// if not, GetPointer will log.
}

inline void Memcpy(void *to_data, const u32 from_address, const u32 len)
{
	const u8 *from = GetPointer(from_address);
	if (from) {
		memcpy(to_data, from, len);
	}
	// if not, GetPointer will log.
}

inline void MemcpyUnchecked(void *to_data, const u32 from_address, const u32 len)
{
	memcpy(to_data, GetPointerUnchecked(from_address), len);
}

inline bool IsValidAddress(const u32 address) {
	if ((address & 0x3E000000) == 0x08000000) {
		return true;
	}
	else if ((address & 0x3F800000) == 0x04000000) {
		return true;
	}
	else if ((address & 0xBFFF0000) == 0x00010000) {
		return true;
	}
	else if ((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) {
		return true;
	}
	else
		return false;
}


template<class T>
void ReadStruct(u32 address, T *ptr)
{
	size_t sz = sizeof(*ptr);
	memcpy(ptr, GetPointer(address), sz);
}

template<class T>
void WriteStruct(u32 address, T *ptr)
{
	size_t sz = sizeof(*ptr);
	memcpy(GetPointer(address), ptr, sz);
}

// Expect this to be some form of auto class on big endian.
template<class T>
T *GetStruct(u32 address)
{
	return (T *)GetPointer(address);
}

const char *GetAddressName(u32 address);

};

template <typename T>
struct PSPPointer
{
	u32_le ptr;

	inline T &operator*() const
	{
#if defined(_M_IX86) || defined(_M_ARM32) || defined (_XBOX)
		return *(T *)(Memory::base + (ptr & Memory::MEMVIEW32_MASK));
#else
		return *(T *)(Memory::base + ptr);
#endif
	}

	inline T &operator[](int i) const
	{
#if defined(_M_IX86) || defined(_M_ARM32) || defined (_XBOX)
		return *((T *)(Memory::base + (ptr & Memory::MEMVIEW32_MASK)) + i);
#else
		return *((T *)(Memory::base + ptr) + i);
#endif
	}

	inline T *operator->() const
	{
#if defined(_M_IX86) || defined(_M_ARM32) || defined (_XBOX)
		return (T *)(Memory::base + (ptr & Memory::MEMVIEW32_MASK));
#else
		return (T *)(Memory::base + ptr);
#endif
	}

	inline PSPPointer<T> operator+(int i) const
	{
		PSPPointer other;
		other.ptr = ptr + i * sizeof(T);
		return other;
	}

	inline PSPPointer<T> &operator=(u32 p)
	{
		ptr = p;
		return *this;
	}

	inline PSPPointer<T> &operator+=(int i)
	{
		ptr = ptr + i * sizeof(T);
		return *this;
	}

	inline PSPPointer<T> operator-(int i) const
	{
		PSPPointer other;
		other.ptr = ptr - i * sizeof(T);
		return other;
	}

	inline PSPPointer<T> &operator-=(int i)
	{
		ptr = ptr - i * sizeof(T);
		return *this;
	}

	inline PSPPointer<T> &operator++()
	{
		ptr += sizeof(T);
		return *this;
	}

	inline PSPPointer<T> operator++(int i)
	{
		PSPPointer<T> other;
		other.ptr = ptr;
		ptr += sizeof(T);
		return other;
	}

	inline PSPPointer<T> &operator--()
	{
		ptr -= sizeof(T);
		return *this;
	}

	inline PSPPointer<T> operator--(int i)
	{
		PSPPointer<T> other;
		other.ptr = ptr;
		ptr -= sizeof(T);
		return other;
	}

	inline operator T*()
	{
#if defined(_M_IX86) || defined(_M_ARM32) || defined (_XBOX)
		return (T *)(Memory::base + (ptr & Memory::MEMVIEW32_MASK));
#else
		return (T *)(Memory::base + ptr);
#endif
	}

	bool IsValid() const
	{
		return Memory::IsValidAddress(ptr);
	}
};


inline u32 PSP_GetScratchpadMemoryBase() { return 0x00010000;}
inline u32 PSP_GetScratchpadMemoryEnd() { return 0x00014000;}

inline u32 PSP_GetKernelMemoryBase() { return 0x08000000;}
inline u32 PSP_GetUserMemoryEnd()  { return PSP_GetKernelMemoryBase() + Memory::g_MemorySize;}
inline u32 PSP_GetKernelMemoryEnd()  { return 0x08400000;} 
// "Volatile" RAM is between 0x08400000 and 0x08800000, can be requested by the
// game through sceKernelVolatileMemTryLock.

inline u32 PSP_GetUserMemoryBase() { return 0x08800000;}

inline u32 PSP_GetDefaultLoadAddress() { return 0x08804000;}
//inline u32 PSP_GetDefaultLoadAddress() { return 0x0898dab0;}
inline u32 PSP_GetVidMemBase() { return 0x04000000;}
inline u32 PSP_GetVidMemEnd() { return 0x04800000;}

template <typename T>
inline bool operator==(const PSPPointer<T> &lhs, const PSPPointer<T> &rhs)
{
	return lhs.ptr == rhs.ptr;
}

template <typename T>
inline bool operator!=(const PSPPointer<T> &lhs, const PSPPointer<T> &rhs)
{
	return lhs.ptr != rhs.ptr;
}

template <typename T>
inline bool operator<(const PSPPointer<T> &lhs, const PSPPointer<T> &rhs)
{
	return lhs.ptr < rhs.ptr;
}

template <typename T>
inline bool operator>(const PSPPointer<T> &lhs, const PSPPointer<T> &rhs)
{
	return lhs.ptr > rhs.ptr;
}

template <typename T>
inline bool operator<=(const PSPPointer<T> &lhs, const PSPPointer<T> &rhs)
{
	return lhs.ptr <= rhs.ptr;
}

template <typename T>
inline bool operator>=(const PSPPointer<T> &lhs, const PSPPointer<T> &rhs)
{
	return lhs.ptr >= rhs.ptr;
}
