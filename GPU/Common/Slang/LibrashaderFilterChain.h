// Copyright (c) 2026- PPSSPP Project.

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

#include "ppsspp_config.h"
#if USE_LIBRASHADER

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "Common/File/Path.h"
#include "Common/GPU/Librashader/LibrashaderLoader.h"
#include "GPU/Common/Slang/ISlangFilterChain.h"
#include "GPU/Common/Slang/LibrashaderRuntime.h"

namespace Draw { class DrawContext; class Framebuffer; }

// Token scans over a pass's #include-resolved slang source, used by Load() to pick the input mode.
// Free functions (not static) so the unit tests can cover them without a graphics device.
// True if the source samples OriginalHistory[1-9] / OriginalHistorySize[1-9] (index 0 is the current
// frame, which librashader never snapshots).
bool ReferencesOriginalHistory(const std::string &src);
// True if the source reads SourceSize / OriginalSize as whole identifiers, i.e. its result depends on
// the size librashader believes the input image has.
bool ReferencesSourceSize(const std::string &src);

// Slang filter chain backed by the librashader shared library. Backend-agnostic: the device
// handles, the render-thread frame callback and the free rules live in a LibrashaderRuntime
// adapter. All public methods are emu-thread only; the librashader runtime calls happen on the
// render thread inside native callback steps. See spec §6.5, §8, §9.
class LibrashaderFilterChain : public ISlangFilterChain {
public:
	explicit LibrashaderFilterChain(Draw::DrawContext *draw);
	~LibrashaderFilterChain() override;

	bool Load(const Path &presetPath, std::string *error) override;
	bool IsValid() const override { return valid_; }
	Draw::Framebuffer *Run(Draw::Framebuffer *source, int sourceW, int sourceH,
	                       int viewportW, int viewportH, int frameCount) override;
	void SetParamOverrides(const std::map<std::string, float> &overrides) override { paramOverrides_ = overrides; }
	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;
	SlangChainBackend Backend() const override { return SlangChainBackend::Librashader; }

private:
	// Creates the backend adapter and grabs its device handles; on failure runtimeError_ holds
	// the reason and Load() fails with it. Re-run by DeviceRestore for the new context.
	void InitRuntime();
	// Hands the librashader frees to the runtime and installs a fresh LibrashaderRenderState.
	// deviceLost tells the runtime the render thread is going away and will not run queued work.
	void ReleaseChain(bool deviceLost);
	void ReleaseOutput();
	bool EnsureOutput(int w, int h);
	bool EnsureNativeInput(int w, int h);

	Draw::DrawContext *draw_ = nullptr;
	std::unique_ptr<LibrashaderRuntime> runtime_;
	// Why runtime_ is null (unsupported backend, or Init failed). Surfaced by Load().
	std::string runtimeError_;
	Path presetPath_;
	std::shared_ptr<LibrashaderRenderState> render_;
	Draw::Framebuffer *output_ = nullptr;
	int outputW_ = 0, outputH_ = 0;
	// Native-PSP-sized copy of the source, used by presets that sample OriginalHistoryN (librashader
	// snapshots history at the *declared* size, so it must equal the real extents) and, on backends
	// that cannot declare an input size, by every preset whose result depends on that size.
	Draw::Framebuffer *nativeInput_ = nullptr;
	int nativeInputW_ = 0, nativeInputH_ = 0;
	std::map<std::string, float> paramOverrides_;
	bool valid_ = false;
	// Set by Load(): Run() must hand librashader a native-sized copy instead of the upscaled
	// framebuffer, because the preset samples OriginalHistoryN, or because this backend cannot
	// declare an input size (LibrashaderRuntime::RequiresNativeSizedInput) and the preset's result
	// depends on it.
	bool needsNativeInput_ = false;
	bool loggedError_ = false;
	bool loggedRuntimeError_ = false;
	bool warnedNativeSize_ = false;
};

#endif  // USE_LIBRASHADER
