#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifdef _XBOX
#include <xtl.h>
#else
#include <Windows.h>
#endif

#undef min
#undef max
#endif