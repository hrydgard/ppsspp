#pragma once

#ifdef _WIN32
#pragma warning(disable:4091)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifdef _XBOX
#include <xtl.h>

extern "C" void _ReadWriteBarrier();
#pragma intrinsic(_ReadWriteBarrier)

extern "C" void _WriteBarrier();
#pragma intrinsic(_WriteBarrier)

extern "C" void _ReadBarrier();
#pragma intrinsic(_ReadBarrier)

#else
#include <Windows.h>
#endif

#undef min
#undef max
#endif