// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

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

template<void func()> void WrapV_V() {
  func();
}

template<u32 func()> void WrapU_V() {
  RETURN(func());
}

template<float func()> void WrapF_V() {
  RETURNF(func());
}

template<u32 func(u32)> void WrapU_U() {
  u32 retval = func(PARAM(0));
  RETURN(retval);
}

template<int func(u32)> void WrapI_U() {
  int retval = func(PARAM(0));
  RETURN(retval);
}

template<int func(int)> void WrapI_I() {
  int retval = func(PARAM(0));
  RETURN(retval);
}

template<void func(u32)> void WrapV_U() {
  func(PARAM(0));
}

template<void func(u32, u32)> void WrapV_UU() {
  func(PARAM(0), PARAM(1));
}

template<u32 func(u32, u32)> void WrapU_UU() {
  u32 retval = func(PARAM(0), PARAM(1));
  RETURN(retval);
}

template<int func(int, u32)> void WrapI_IU() {
  int retval = func(PARAM(0), PARAM(1));
  RETURN(retval);
}

template<int func(const char *, u32)> void WrapI_CU() {
  int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1));
  RETURN(retval);
}

template<u32 func(const char *, u32)> void WrapU_CU() {
  int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1));
  RETURN((u32)retval);
}

template<u32 func(const char *, u32, u32)> void WrapU_CUU() {
  int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2));
  RETURN((u32)retval);
}

template<u32 func(u32, u32, u32)> void WrapU_UUU() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2));
  RETURN(retval);
}

template<int func(int, u32, u32)> void WrapI_IUU() {
  int retval = func(PARAM(0), PARAM(1), PARAM(2));
  RETURN(retval);
}

template<void func(u32, u32, u32)> void WrapV_UUU() {
  func(PARAM(0), PARAM(1), PARAM(2));
}

template<void func(u32, u32, u32, u32)> void WrapV_UUUU() {
  func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
}

template<u32 func(u32, u32, u32, u32)> void WrapU_UUUU() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3));
  RETURN(retval);
}

template<u32 func(u32, u32, u32, u32, u32)> void WrapU_UUUUU() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
  RETURN(retval);
}

template<int func(const char *, int, u32, int, u32)> void WrapU_CIUIU() {
  int retval = func(Memory::GetCharPointer(PARAM(0)), PARAM(1), PARAM(2), PARAM(3), PARAM(4));
  RETURN(retval);
}

template<u32 func(u32, u32, u32, u32, u32, u32)> void WrapU_UUUUUU() {
  u32 retval = func(PARAM(0), PARAM(1), PARAM(2), PARAM(3), PARAM(4), PARAM(5));
  RETURN(retval);
}
