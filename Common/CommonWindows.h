#pragma once

#ifdef _WIN32
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