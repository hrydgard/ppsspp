// Copyright (C) 2003 Dolphin Project / 2012 PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#pragma once

#include "ppsspp_config.h"

#include <cstring>
#include <cstdint>
#ifndef offsetof
#include <stddef.h>
#endif

#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Common/Swap.h"
#include "Core/Opcode.h"

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

namespace Memory {
// Base is a pointer to the base of the memory map. Yes, some MMU tricks
// are used to set up a full GC or Wii memory map in process memory.	on
// 32-bit, you have to mask your offsets with 0x3FFFFFFF. This means that
// some things are mirrored too many times, but eh... it works.

// In 64-bit, this might point to "high memory" (above the 32-bit limit),
// so be sure to load it into a 64-bit register.
extern u8 *base; 

// This replaces RAM_NORMAL_SIZE at runtime.
extern u32 g_MemorySize;
extern u32 g_PSPModel;

// UWP has such limited memory management that we need to mask
// even in 64-bit mode. Also, when using the sanitizer, we need to mask as well.
#if PPSSPP_ARCH(32BIT) || PPSSPP_PLATFORM(UWP) || USE_ASAN || PPSSPP_PLATFORM(IOS) || defined(__EMSCRIPTEN__)
#define MASKED_PSP_MEMORY
#endif

enum {
	// This may be adjusted by remaster games.
	RAM_NORMAL_SIZE = 0x02000000,
	// Used if the PSP model is PSP-2000 (Slim).
	RAM_DOUBLE_SIZE = RAM_NORMAL_SIZE * 2,

	VRAM_SIZE       = 0x00200000,

	SCRATCHPAD_SIZE = 0x00004000,

#ifdef MASKED_PSP_MEMORY
	// This wraparound should work for PSP too.
	MEMVIEW32_MASK  = 0x3FFFFFFF,
#endif
};

enum {
	MV_MIRROR_PREVIOUS = 1,
	MV_IS_PRIMARY_RAM = 0x100,
	MV_IS_EXTRA1_RAM = 0x200,
	MV_IS_EXTRA2_RAM = 0x400,
	MV_KERNEL = 0x800,  // Can be skipped on platforms where memory is tight.
	MV_NULL_PAGE = 0x1000,
};

struct MemoryView {
	u8 **out_ptr;
	u32 virtual_address;
	u32 size;
	u32 flags;
};

enum class MemMapSetupFlags {
	Default = 0,
	AllocNullPage = 1,
};
ENUM_CLASS_BITOPS(MemMapSetupFlags);

// Init and Shutdown
bool Init(MemMapSetupFlags flags);
void Shutdown();
void DoState(PointerWrap &p);

// False when shutdown has already been called.
bool IsActive();

class MemoryInitedLock {
public:
	MemoryInitedLock();
	~MemoryInitedLock();
};

// This doesn't lock memory access or anything, it just makes sure memory isn't freed.
// Use it when accessing PSP memory from external threads.
MemoryInitedLock Lock();

// used by JIT to read instructions. Does not resolve replacements.
Opcode Read_Opcode_JIT(const u32 _Address);
// used by JIT. Reads in the "Locked cache" mode
void Write_Opcode_JIT(const u32 _Address, const Opcode& _Value);

// Should be used by analyzers, disassemblers etc. Does resolve replacements.
Opcode Read_Instruction(const u32 _Address, bool resolveReplacements = false);
Opcode ReadUnchecked_Instruction(const u32 _Address, bool resolveReplacements = false);

u8  Read_U8(const u32 _Address);
u16 Read_U16(const u32 _Address);
u32 Read_U32(const u32 _Address);
u64 Read_U64(const u32 _Address);

inline u8* GetPointerWriteUnchecked(const u32 address) {
#ifdef MASKED_PSP_MEMORY
	return (u8 *)(base + (address & MEMVIEW32_MASK));
#else
	return (u8 *)(base + address);
#endif
}

inline const u8* GetPointerUnchecked(const u32 address) {
#ifdef MASKED_PSP_MEMORY
	return (const u8 *)(base + (address & MEMVIEW32_MASK));
#else
	return (const u8 *)(base + address);
#endif
}

inline u32 ReadUnchecked_U32(const u32 address) {
#ifdef MASKED_PSP_MEMORY
	return *(u32_le *)(base + (address & MEMVIEW32_MASK));
#else
	return *(u32_le *)(base + address);
#endif
}

inline float ReadUnchecked_Float(const u32 address) {
#ifdef MASKED_PSP_MEMORY
	return *(float_le *)(base + (address & MEMVIEW32_MASK));
#else
	return *(float_le *)(base + address);
#endif
}

inline u16 ReadUnchecked_U16(const u32 address) {
#ifdef MASKED_PSP_MEMORY
	return *(u16_le *)(base + (address & MEMVIEW32_MASK));
#else
	return *(u16_le *)(base + address);
#endif
}

inline u8 ReadUnchecked_U8(const u32 address) {
#ifdef MASKED_PSP_MEMORY
	return (*(u8 *)(base + (address & MEMVIEW32_MASK)));
#else
	return (*(u8 *)(base + address));
#endif
}

inline void WriteUnchecked_U32(u32 data, u32 address) {
#ifdef MASKED_PSP_MEMORY
	*(u32_le *)(base + (address & MEMVIEW32_MASK)) = data;
#else
	*(u32_le *)(base + address) = data;
#endif
}

inline void WriteUnchecked_Float(float data, u32 address) {
#ifdef MASKED_PSP_MEMORY
	*(float_le *)(base + (address & MEMVIEW32_MASK)) = data;
#else
	*(float_le *)(base + address) = data;
#endif
}

inline void WriteUnchecked_U16(u16 data, u32 address) {
#ifdef MASKED_PSP_MEMORY
	*(u16_le *)(base + (address & MEMVIEW32_MASK)) = data;
#else
	*(u16_le *)(base + address) = data;
#endif
}

inline void WriteUnchecked_U8(u8 data, u32 address) {
#ifdef MASKED_PSP_MEMORY
	(*(u8 *)(base + (address & MEMVIEW32_MASK))) = data;
#else
	(*(u8 *)(base + address)) = data;
#endif
}

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

u8* GetPointerWrite(const u32 address);
const u8* GetPointer(const u32 address);

u8 *GetPointerWriteRange(const u32 address, const u32 size);
template<typename T>
T* GetTypedPointerWriteRange(const u32 address, const u32 size) {
	return reinterpret_cast<T*>(GetPointerWriteRange(address, size));
}

const u8 *GetPointerRange(const u32 address, const u32 size);
template<typename T>
const T* GetTypedPointerRange(const u32 address, const u32 size) {
	return reinterpret_cast<const T*>(GetPointerRange(address, size));
}

bool IsRAMAddress(const u32 address);
inline bool IsVRAMAddress(const u32 address) {
	return ((address & 0x3F800000) == 0x04000000);
}
inline bool IsDepthTexVRAMAddress(const u32 address) {
	return ((address & 0x3FE00000) == 0x04200000) || ((address & 0x3FE00000) == 0x04600000);
}

// 0x08000000 -> 0x08800000
inline bool IsKernelAddress(const u32 address) {
	return ((address & 0x3F800000) == 0x08000000);
}

// 0x08000000 -> 0x08400000
inline bool IsKernelAndNotVolatileAddress(const u32 address) {
	return ((address & 0x3FC00000) == 0x08000000);
}

bool IsScratchpadAddress(const u32 address);

inline void MemcpyUnchecked(void *to_data, const u32 from_address, const u32 len) {
	memcpy(to_data, GetPointerUnchecked(from_address), len);
}

inline void MemcpyUnchecked(const u32 to_address, const void *from_data, const u32 len) {
	memcpy(GetPointerWriteUnchecked(to_address), from_data, len);
}

inline void MemcpyUnchecked(const u32 to_address, const u32 from_address, const u32 len) {
	MemcpyUnchecked(GetPointerWriteUnchecked(to_address), from_address, len);
}

inline bool IsValidAddress(const u32 address) {
	if ((address & 0x3E000000) == 0x08000000) {
		return true;
	} else if ((address & 0x3F800000) == 0x04000000) {
		return address < 0x80000000;  // Let's disallow kernel-flagged VRAM. We don't have it mapped and I am not sure if it's accessible.
	} else if ((address & 0xBFFFC000) == 0x00010000) {
		return true;
	} else if ((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) {
		return true;
	} else {
		return false;
	}
}

inline bool IsValid4AlignedAddress(const u32 address) {
	if ((address & 0x3E000003) == 0x08000000) {
		return true;
	} else if ((address & 0x3F800003) == 0x04000000) {
		return address < 0x80000000;  // Let's disallow kernel-flagged VRAM. We don't have it mapped and I am not sure if it's accessible.
	} else if ((address & 0xBFFFC003) == 0x00010000) {
		return true;
	} else if ((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) {
		return (address & 3) == 0;
	} else {
		return false;
	}
}

inline u32 MaxSizeAtAddress(const u32 address){
	if ((address & 0x3E000000) == 0x08000000) {
		return 0x08000000 + g_MemorySize - (address & 0x3FFFFFFF);
	} else if ((address & 0x3F800000) == 0x04000000) {
		if (address & 0x80000000) {
			return 0;
		} else {
			return 0x04800000 - (address & 0x3FFFFFFF);
		}
	} else if ((address & 0xBFFFC000) == 0x00010000) {
		return 0x00014000 - (address & 0x3FFFFFFF);
	} else if ((address & 0x3F000000) >= 0x08000000 && (address & 0x3F000000) < 0x08000000 + g_MemorySize) {
		return 0x08000000 + g_MemorySize - (address & 0x3FFFFFFF);
	} else {
		return 0;
	}
}

inline const char *GetCharPointerUnchecked(const u32 address) {
	return (const char *)GetPointerUnchecked(address);
}

// NOTE: Unlike the similar IsValidRange/IsValidAddress functions, this one is linear cost vs the size of the string,
// for hopefully-obvious reasons.
inline bool IsValidNullTerminatedString(const u32 address) {
	u32 max_size = MaxSizeAtAddress(address);
	if (max_size == 0) {
		return false;
	}

	const char *c = GetCharPointerUnchecked(address);
	if (memchr(c, '\0', max_size)) {
		return true;
	}
	return false;
}

inline u32 ClampValidSizeAt(const u32 address, const u32 requestedSize) {
	u32 max_size = MaxSizeAtAddress(address);
	if (requestedSize > max_size) {
		return max_size;
	}
	return requestedSize;
}

// NOTE: If size == 0, any address will be accepted. This may not be ideal for all cases.
inline bool IsValidRange(const u32 address, const u32 size) {
	return ClampValidSizeAt(address, size) == size;
}

// NOTE: If size == 0, any address will be accepted. This may not be ideal for all cases.
// Also, length is not checked for alignment.
inline bool IsValid4AlignedRange(const u32 address, const u32 size) {
	if (address & 3) {
		return false;
	}
	return ClampValidSizeAt(address, size) == size;
}

// Used for auto-converted char * parameters, which can sometimes legitimately be null -
// so we don't want to get caught in GetPointer's crash reporting
// TODO: This should use IsValidNullTerminatedString, but may be expensive since this is used so much - needs evaluation.
inline const char *GetCharPointer(const u32 address) {
	if (address && IsValidAddress(address)) {
		return GetCharPointerUnchecked(address);
	} else {
		return nullptr;
	}
}

// Remaps the host pointer (potentially 64bit) into the 32bit virtual pointer, no checks are made
inline u32 GetAddressFromHostPointerUnchecked(const void* host_ptr) {
	auto address = static_cast<const u8*>(host_ptr) - base;
	return static_cast<u32>(address);
}

// Remaps the host pointer (potentially 64bit) into the 32bit virtual pointer with checks
inline u32 GetAddressFromHostPointer(const void* host_ptr) {
	u32 address = GetAddressFromHostPointerUnchecked(host_ptr);
	if (!IsValidAddress(address)) {
		// Somehow report the error?
		return 0;
	}
	return address;
}

// Like GetPointer, but bad values don't result in a memory exception, instead nullptr is returned.
inline const u8* GetPointerOrNull(const u32 address) {
	return IsValidAddress(address) ? GetPointerUnchecked(address) : nullptr;
}

}  // namespace Memory

// Avoiding a global include for NotifyMemInfo.
void PSPPointerNotifyRW(int rw, uint32_t ptr, uint32_t bytes, const char *tag, size_t tagLen);

// TODO: These are actually quite annoying because they can't be followed in the MSVC debugger...
// Need to find a solution for that. Can't just change the internal representation though, because
// these can be present in PSP-native structs.
template <typename T>
struct PSPPointer
{
	u32_le ptr;

	inline T &operator*() const
	{
#ifdef MASKED_PSP_MEMORY
		return *(T *)(Memory::base + (ptr & Memory::MEMVIEW32_MASK));
#else
		return *(T *)(Memory::base + ptr);
#endif
	}

	inline const T &operator[](int i) const
	{
#ifdef MASKED_PSP_MEMORY
		return *((T *)(Memory::base + (ptr & Memory::MEMVIEW32_MASK)) + i);
#else
		return *((const T *)(Memory::base + ptr) + i);
#endif
	}

	inline T &operator[](int i)
	{
#ifdef MASKED_PSP_MEMORY
		return *((T *)(Memory::base + (ptr & Memory::MEMVIEW32_MASK)) + i);
#else
		return *((T *)(Memory::base + ptr) + i);
#endif
	}

	inline const T *operator->() const
	{
#ifdef MASKED_PSP_MEMORY
		return (T *)(Memory::base + (ptr & Memory::MEMVIEW32_MASK));
#else
		return (const T *)(Memory::base + ptr);
#endif
	}

	inline T *operator->()
	{
#ifdef MASKED_PSP_MEMORY
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
#ifdef MASKED_PSP_MEMORY
		return (T *)(Memory::base + (ptr & Memory::MEMVIEW32_MASK));
#else
		return (T *)(Memory::base + ptr);
#endif
	}

	inline operator const T*() const
	{
#ifdef MASKED_PSP_MEMORY
		return (const T *)(Memory::base + (ptr & Memory::MEMVIEW32_MASK));
#else
		return (const T *)(Memory::base + ptr);
#endif
	}

	bool IsValid() const {
		return Memory::IsValidRange(ptr, (u32)sizeof(T));
	}

	void FillWithZero() {
		memset(Memory::GetPointerWrite(ptr), 0, sizeof(T));
	}

	bool Equals(u32 addr) const {
		return ptr == addr;
	}

	T *PtrOrNull() {
		if (IsValid())
			return (T *)*this;
		return nullptr;
	}

	const T *PtrOrNull() const {
		if (IsValid())
			return (const T *)*this;
		return nullptr;
	}

	template <size_t tagLen>
	void NotifyWrite(const char(&tag)[tagLen]) const {
		PSPPointerNotifyRW(1, (uint32_t)ptr, (uint32_t)sizeof(T), tag, tagLen - 1);
	}

	template <size_t tagLen>
	void NotifyRead(const char(&tag)[tagLen]) const {
		PSPPointerNotifyRW(2, (uint32_t)ptr, (uint32_t)sizeof(T), tag, tagLen - 1);
	}

	size_t ElementSize() const
	{
		return sizeof(T);
	}

	static PSPPointer<T> Create(u32 ptr) {
		PSPPointer<T> p;
		p = ptr;
		return p;
	}
};

constexpr u32 PSP_GetScratchpadMemoryBase() { return 0x00010000;}
constexpr u32 PSP_GetScratchpadMemoryEnd() { return 0x00014000;}

constexpr u32 PSP_GetKernelMemoryBase() { return 0x08000000;}
inline u32 PSP_GetUserMemoryEnd() { return PSP_GetKernelMemoryBase() + Memory::g_MemorySize;}
constexpr u32 PSP_GetKernelMemoryEnd() { return 0x08400000;}

// "Volatile" RAM is between 0x08400000 and 0x08800000, can be requested by the
// game through sceKernelVolatileMemTryLock.
constexpr u32 PSP_GetVolatileMemoryStart() { return 0x08400000; }
constexpr u32 PSP_GetVolatileMemoryEnd() { return 0x08800000; }

constexpr u32 PSP_GetUserMemoryBase() { return 0x08800000; }
constexpr u32 PSP_GetDefaultLoadAddress() { return 0; }
constexpr u32 PSP_GetVidMemBase() { return 0x04000000; }
constexpr u32 PSP_GetVidMemEnd() { return 0x04800000; }

template <typename T>
inline bool operator==(const PSPPointer<T> &lhs, const PSPPointer<T> &rhs) {
	return lhs.ptr == rhs.ptr;
}

template <typename T>
inline bool operator!=(const PSPPointer<T> &lhs, const PSPPointer<T> &rhs) {
	return lhs.ptr != rhs.ptr;
}

template <typename T>
inline bool operator<(const PSPPointer<T> &lhs, const PSPPointer<T> &rhs) {
	return lhs.ptr < rhs.ptr;
}

template <typename T>
inline bool operator>(const PSPPointer<T> &lhs, const PSPPointer<T> &rhs) {
	return lhs.ptr > rhs.ptr;
}

template <typename T>
inline bool operator<=(const PSPPointer<T> &lhs, const PSPPointer<T> &rhs) {
	return lhs.ptr <= rhs.ptr;
}

template <typename T>
inline bool operator>=(const PSPPointer<T> &lhs, const PSPPointer<T> &rhs) {
	return lhs.ptr >= rhs.ptr;
}
