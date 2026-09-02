// Copyright (c) 2012- PPSSPP Project.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "Common/File/Path.h"

void VSHGameLifecycleResetSession();
void VSHGameLifecycleSetMountedUmd(const Path &path);
bool VSHGameLifecycleRequestBoot(std::string_view guestPath, std::string *errorString,
	const void *vshMainArgs = nullptr, size_t vshMainArgsSize = 0);
bool VSHGameLifecycleRequestReturn(std::string *errorString);
Path VSHGameLifecycleVshPath();
bool VSHGameLifecycleShouldReturn();
bool VSHGameLifecycleFreshVshBootPending();
void VSHGameLifecyclePreserveVshMainArgs(const void *args, size_t size);
const void *VSHGameLifecyclePrepareReturnVshMainArgs(size_t *size);
void VSHGameLifecycleSetNativeImposeActive(bool active);
bool VSHGameLifecycleNativeImposeActive();
void VSHGameLifecycleNotifyVshBooted();
std::string VSHGameLifecycleStatus();
