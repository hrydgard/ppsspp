// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#pragma once

#include "../../Globals.h"

// For easy parameter parsing and return value processing.

template<void func()> void Wrap() {
  func();
}

template<u32 func()> void Wrap() {
  RETURN(func());
}

template<float func()> void Wrap() {
  RETURNF(func());
}

template<u32 func(u32)> void Wrap() {
  u32 retval = func(PARAM(0));
  RETURN(retval);
}
template<int func(int)> void Wrap() {
  int retval = func(PARAM(0));
  RETURN(retval);
}
template<u32 func(int)> void Wrap() {
  u32 retval = func(PARAM(0));
  RETURN(retval);
}

template<void func(u32)> void Wrap() {
  func(PARAM(0));
}

template <u32 func(time_t *)> void Wrap() {
  u32 retval = func((time_t*)Memory::GetPointer(PARAM(0)));
  RETURN(retval);
}

template<void func(u32, u32)> void Wrap() {
  func(PARAM(0), PARAM(1));
}

template<u32 func(u32, u32)> void Wrap() {
  u32 retval = func(PARAM(0), PARAM(1));
  RETURN(retval);
}
template<u32 func(timeval *, u32)> void Wrap() {
  u32 retval = func((timeval*)Memory::GetPointer(PARAM(0)), PARAM(1));
  RETURN(retval);
}
template<u32 func(int, u32)> void Wrap() {
  u32 retval = func(PARAM(0), PARAM(1));
  RETURN(retval);
}

template<u32 func(const char *, u32)> void Wrap() {
  int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1));
  RETURN((u32)retval);
}
template<int func(const char *, u32)> void Wrap() {
  int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1));
  RETURN((u32)retval);
}
template<u32 func(const char *, int)> void Wrap() {
  u32 retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1));
  RETURN(retval);
}

template<u32 func(const char *, u32, u32)> void Wrap() {
  int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
  RETURN((u32)retval);
}
template<u32 func(const char *, int, int)> void Wrap() {
  int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
  RETURN((u32)retval);
}

template<u32 func(u32, u32, u32)> void Wrap() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2));
  RETURN(retval);
}
template<u32 func(int, int, u32)> void Wrap() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2));
  RETURN(retval);
}
template<u32 func(int, u32, u32)> void Wrap() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2));
  RETURN(retval);
}

template<void func(u32, u32, u32)> void Wrap() {
  func(PARAM(0), PARAM(1), PARAM(2));
}

template<u32 func(u32, u32, u32, u32)> void Wrap() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
  RETURN(retval);
}
template<u32 func(u32, int, int, int)> void Wrap() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
  RETURN(retval);
}
template<u32 func(u32, u32, u32, int)> void Wrap() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
  RETURN(retval);
}

template<u32 func(u32, u32, u32, u32, u32)> void Wrap() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
  RETURN(retval);
}

template<u32 func(u32, u32, u32, u32, u32, u32)> void Wrap() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
  RETURN(retval);
}
template<u32 func(u32, int, u32, int, u32, int)> void Wrap() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
  RETURN(retval);
}

template<u32 func(u64)> void Wrap() {
  u32 retval = func(((u64)PARAM(1) << 32) | PARAM(0));
  RETURN(retval);
}

template<u64 func(u32, u64, u32)> void Wrap() {
  u64 retval = func(PARAM(0), ((u64)PARAM(3) << 32) | PARAM(2), PARAM(4));
  RETURN(retval);
  RETURN2(retval >> 32);
}
template<u64 func(int, s64, u32)> void Wrap() {
  u64 retval = func(PARAM(0), ((u64)PARAM(3) << 32) | PARAM(2), PARAM(4));
  RETURN(retval);
  RETURN2(retval >> 32);
}

template<u32 func(int, s64, u32)> void Wrap() {
  u32 retval = func(PARAM(0), ((u64)PARAM(3) << 32) | PARAM(2), PARAM(4));
  RETURN(retval);
}

