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

#include "GPU/Common/Slang/LibrashaderFilterChain.h"
#if USE_LIBRASHADER

#include <algorithm>
#include <string>

#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/GPU/thin3d.h"
#include "Core/System.h"
#include "GPU/Common/Slang/SlangPreset.h"
#include "GPU/Common/Slang/SlangpParser.h"

// Read a slang asset: VFS first (bundled assets), then the real filesystem (custom shader dir).
// Same lookup order as the in-tree chain's private helper of the same name.
static bool ReadSlangFile(const Path &path, std::string *out) {
	size_t sz = 0;
	uint8_t *data = g_VFS.ReadFile(path.c_str(), &sz);
	if (data) {
		out->assign((const char *)data, sz);
		delete[] data;
		return true;
	}
	return File::ReadBinaryFileToString(path, out);
}

static bool IsIdentifierChar(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// True if the text references OriginalHistory1..9 or OriginalHistorySize1..9. Index 0 is the
// current frame, which librashader binds as a view of the real image and therefore never
// snapshots, so it does not need the native-sized input.
bool ReferencesOriginalHistory(const std::string &src) {
	static const char *kNeedle = "OriginalHistory";
	const size_t needleLen = strlen(kNeedle);
	for (size_t pos = src.find(kNeedle); pos != std::string::npos; pos = src.find(kNeedle, pos + needleLen)) {
		if (pos > 0 && IsIdentifierChar(src[pos - 1]))
			continue;  // tail of a longer identifier, e.g. MyOriginalHistory1
		size_t after = pos + needleLen;
		if (src.compare(after, 4, "Size") == 0)
			after += 4;  // OriginalHistorySizeN
		if (after < src.size() && src[after] >= '1' && src[after] <= '9')
			return true;
	}
	return false;
}

static bool IsSpaceChar(char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// True if the text reads the input image's size, i.e. mentions SourceSize or OriginalSize as whole
// identifiers (so OriginalHistorySizeN and FinalViewportSize do not count). Such a preset renders
// differently depending on what size librashader believes the input is - which is exactly what a
// RequiresNativeSizedInput() backend cannot declare.
//
// The uniform-block *declaration* (`vec4 SourceSize;`) does not count: virtually every RetroArch
// shader declares both sizes in its Push/UBO block whether it uses them or not, and treating the
// declaration as a read would put every preset - stock.slangp included - on the downscaled input.
// A declaration is an occurrence preceded by a type name (identifier + whitespace) and followed by
// `;`; a read is member access (`params.SourceSize.xy`) or an expression operand, never both.
bool ReferencesSourceSize(const std::string &src) {
	static const char *kNeedles[] = { "SourceSize", "OriginalSize" };
	for (const char *needle : kNeedles) {
		const size_t needleLen = strlen(needle);
		for (size_t pos = src.find(needle); pos != std::string::npos; pos = src.find(needle, pos + needleLen)) {
			if (pos > 0 && IsIdentifierChar(src[pos - 1]))
				continue;
			size_t after = pos + needleLen;
			if (after < src.size() && IsIdentifierChar(src[after]))
				continue;
			// Declaration? Look back over whitespace for a type identifier, forward over whitespace for ';'.
			size_t before = pos;
			while (before > 0 && IsSpaceChar(src[before - 1]))
				before--;
			const bool typeBefore = before > 0 && before < pos && IsIdentifierChar(src[before - 1]);
			while (after < src.size() && IsSpaceChar(src[after]))
				after++;
			const bool semicolonAfter = after < src.size() && src[after] == ';';
			if (typeBefore && semicolonAfter)
				continue;
			return true;
		}
	}
	return false;
}

// What a preset needs from its input image, as far as the .slangp and the pass sources can tell.
struct PresetInputNeeds {
	bool usesHistory = false;           // samples OriginalHistory[1-9] / OriginalHistorySize[1-9]
	bool readsSourceSize = false;       // reads SourceSize / OriginalSize
	bool sourceRelativePasses = false;  // an intermediate pass is sized off its input
};

// librashader's C API exposes no way to enumerate a preset's semantics, so re-parse the preset with
// the in-tree parser and scan the (include-resolved) pass sources. On any read/parse failure every
// answer is "no" - that is the pre-existing behaviour, and librashader's own parser is the one that
// decides whether the preset loads at all.
static PresetInputNeeds ScanPresetInputNeeds(const Path &presetPath) {
	PresetInputNeeds needs;
	std::string presetText;
	if (!ReadSlangFile(presetPath, &presetText)) {
		WARN_LOG(Log::G3D, "LibrashaderFilterChain: could not re-read '%s' to scan its input needs", presetPath.c_str());
		return needs;
	}
	SlangPreset preset;
	std::string err;
	if (!ParseSlangPreset(presetText, Path(presetPath.GetDirectory()), &preset, &err)) {
		WARN_LOG(Log::G3D, "LibrashaderFilterChain: input-needs scan skipped, preset re-parse failed: %s", err.c_str());
		return needs;
	}
	// An intermediate pass with scale_type = source renders at a multiple of its *input* size, so its
	// resolution - and everything sampled from it - follows the declared input size. The final pass
	// is excluded: its output is our viewport-sized output framebuffer either way.
	for (size_t i = 0; i + 1 < preset.passes.size(); i++) {
		if (preset.passes[i].scaleTypeX == SlangScaleType::Source || preset.passes[i].scaleTypeY == SlangScaleType::Source) {
			needs.sourceRelativePasses = true;
			break;
		}
	}
	const SlangFileReader reader = [](const Path &path, std::string *out) -> bool {
		return ReadSlangFile(path, out);
	};
	for (const SlangPassDesc &pass : preset.passes) {
		if (needs.usesHistory && needs.readsSourceSize)
			break;
		std::string shaderSrc;
		if (!ReadSlangFile(Path(pass.shaderPath), &shaderSrc))
			continue;
		std::string resolved;
		if (!ResolveSlangIncludes(shaderSrc, Path(Path(pass.shaderPath).GetDirectory()), reader, &resolved, &err))
			resolved = shaderSrc;  // includes unresolved: scan what we could read
		needs.usesHistory = needs.usesHistory || ReferencesOriginalHistory(resolved);
		needs.readsSourceSize = needs.readsSourceSize || ReferencesSourceSize(resolved);
	}
	return needs;
}

LibrashaderFilterChain::LibrashaderFilterChain(Draw::DrawContext *draw)
	: draw_(draw), render_(std::make_shared<LibrashaderRenderState>()) {
	InitRuntime();
}

void LibrashaderFilterChain::InitRuntime() {
	runtimeError_.clear();
	runtime_ = CreateLibrashaderRuntime(GetGPUBackend());
	if (!runtime_) {
		runtimeError_ = "no librashader runtime for the current graphics backend";
		return;
	}
	if (!runtime_->Init(draw_, &runtimeError_)) {
		runtime_.reset();
		if (runtimeError_.empty())
			runtimeError_ = "librashader runtime init failed";
	}
}

LibrashaderFilterChain::~LibrashaderFilterChain() {
	ReleaseChain(false);
	ReleaseOutput();
}

bool LibrashaderFilterChain::Load(const Path &presetPath, std::string *error) {
	if (!runtime_) {
		if (error) *error = runtimeError_;
		if (!loggedRuntimeError_) {
			ERROR_LOG(Log::G3D, "LibrashaderFilterChain: %s", runtimeError_.c_str());
			loggedRuntimeError_ = true;
		}
		return false;
	}
	std::string loadErr;
	if (!Librashader::Load(&loadErr)) {
		if (error) *error = "librashader not available: " + loadErr;
		return false;
	}
	const libra_instance_t &lib = Librashader::Instance();

	// Frees the previous preset/chain through the deletion queue and installs a fresh, empty
	// RenderState which nothing else references yet - so we can fill it in from this thread.
	ReleaseChain(false);
	valid_ = false;
	loggedError_ = false;
	warnedNativeSize_ = false;
	needsNativeInput_ = false;
	presetPath_ = presetPath;

	libra_shader_preset_t preset = nullptr;
	libra_preset_ctx_t ctx = nullptr;
	std::string err = Librashader::ErrorToString(lib.preset_ctx_create(&ctx));
	if (err.empty()) err = Librashader::ErrorToString(lib.preset_ctx_set_core_name(&ctx, "PPSSPP"));
	if (err.empty()) err = Librashader::ErrorToString(lib.preset_ctx_set_runtime(&ctx, runtime_->PresetRuntime()));
	if (err.empty()) err = Librashader::ErrorToString(lib.preset_create_with_options(presetPath.c_str(), &ctx, nullptr, &preset));
	// preset_create_with_options invalidates the context and nulls it (like every other
	// consuming librashader entry point), so this only frees it if an earlier step failed.
	if (ctx) (void)Librashader::ErrorToString(lib.preset_ctx_free(&ctx));
	if (!err.empty()) {
		if (error) *error = "librashader preset load failed: " + err;
		if (preset) (void)Librashader::ErrorToString(lib.preset_free(&preset));
		return false;
	}
	// Hand the preset over to the render-thread state; it is consumed by chain creation, or
	// freed by the deletion-queue callback if the chain is never created.
	render_->preset = preset;
	valid_ = true;
	const PresetInputNeeds needs = ScanPresetInputNeeds(presetPath);
	// A history preset needs the native-sized input on every backend (librashader snapshots history at
	// the *declared* size). On a backend whose frame API cannot declare an input size at all, every
	// preset whose result depends on that size needs it too, so all backends behave the same - see
	// spec §12 and §13 Q4.
	const bool sizeDependent = needs.readsSourceSize || needs.sourceRelativePasses;
	needsNativeInput_ = needs.usesHistory || (runtime_->RequiresNativeSizedInput() && sizeDependent);
	const char *inputMode = "upscaled framebuffer with declared native size";
	if (needs.usesHistory)
		inputMode = "native-sized copy (history)";
	else if (needsNativeInput_)
		inputMode = "native-sized copy (D3D11: preset depends on SourceSize)";
	INFO_LOG(Log::G3D, "LibrashaderFilterChain: preset parsed: %s (input mode: %s)", presetPath.c_str(), inputMode);
	return true;
}

bool LibrashaderFilterChain::EnsureOutput(int w, int h) {
	if (output_ && outputW_ == w && outputH_ == h)
		return true;
	if (output_) {
		output_->Release();
		output_ = nullptr;
	}
	outputW_ = outputH_ = 0;
	// Plain RGBA8, no MSAA, single layer: the NATIVE_CALLBACK step only transitions dst->color.
	Draw::FramebufferDesc desc{};
	desc.width = w;
	desc.height = h;
	desc.depth = 1;
	desc.numLayers = 1;
	desc.multiSampleLevel = 0;
	desc.z_stencil = false;
	desc.tag = "librashader_output";
	output_ = draw_->CreateFramebuffer(desc);
	if (!output_) {
		ERROR_LOG(Log::G3D, "LibrashaderFilterChain: failed to create %dx%d output framebuffer", w, h);
		return false;
	}
	outputW_ = w;
	outputH_ = h;
	return true;
}

bool LibrashaderFilterChain::EnsureNativeInput(int w, int h) {
	if (nativeInput_ && nativeInputW_ == w && nativeInputH_ == h)
		return true;
	if (nativeInput_) {
		nativeInput_->Release();
		nativeInput_ = nullptr;
	}
	nativeInputW_ = nativeInputH_ = 0;
	Draw::FramebufferDesc desc{};
	desc.width = w;
	desc.height = h;
	desc.depth = 1;
	desc.numLayers = 1;
	desc.multiSampleLevel = 0;
	desc.z_stencil = false;
	desc.tag = "librashader_native";
	nativeInput_ = draw_->CreateFramebuffer(desc);
	if (!nativeInput_) {
		ERROR_LOG(Log::G3D, "LibrashaderFilterChain: failed to create %dx%d native input framebuffer", w, h);
		return false;
	}
	nativeInputW_ = w;
	nativeInputH_ = h;
	return true;
}

Draw::Framebuffer *LibrashaderFilterChain::Run(Draw::Framebuffer *source, int sourceW, int sourceH,
                                               int viewportW, int viewportH, int frameCount) {
	if (!runtime_ || !valid_ || !source)
		return nullptr;
	if (render_->failed.load()) {
		if (!loggedError_) {
			std::lock_guard<std::mutex> guard(render_->errorLock);
			ERROR_LOG(Log::G3D, "LibrashaderFilterChain: disabled after librashader error (%s)", render_->lastError.c_str());
			loggedError_ = true;
		}
		return nullptr;
	}
	if (!EnsureOutput(std::max(1, viewportW), std::max(1, viewportH)))
		return nullptr;

	// Spec §12, verified in Task 8: librashader reads SourceSize/OriginalSize and the pass scale
	// base from the declared input image size (libra_image_{vk,gl}_t::width/height) - good, we
	// report the native PSP size, so
	// SourceSize-driven masks tile at the same frequency as the in-tree chain - but it also uses
	// those numbers as the copy extent when it snapshots the input into its OriginalHistoryN ring.
	// With PPSSPP's upscaled render target that snapshot would capture only the native-sized
	// top-left corner, so for presets that actually sample OriginalHistoryN (detected in Load) we
	// downscale into a native-sized intermediate first, making the declared size the true extent.
	// The same intermediate makes the size-declaring backends and D3D11 agree: D3D11's frame API has
	// no width/height to declare (RequiresNativeSizedInput()), so Load() also routes size-dependent
	// presets through it there. Everything else keeps sampling the full upscaled framebuffer.
	int actualW = sourceW, actualH = sourceH;
	draw_->GetFramebufferDimensions(source, &actualW, &actualH);
	Draw::Framebuffer *chainInput = source;
	if (needsNativeInput_ && (actualW != sourceW || actualH != sourceH)) {
		if (!EnsureNativeInput(sourceW, sourceH))
			return nullptr;
		if (draw_->GetDeviceCaps().framebufferBlitSupported) {
			draw_->BlitFramebuffer(source, 0, 0, actualW, actualH, nativeInput_, 0, 0, sourceW, sourceH,
				Draw::Aspect::COLOR_BIT, Draw::FB_BLIT_LINEAR, "librashader_native");
			chainInput = nativeInput_;
		} else if (rasterBlit_) {
			// D3D11's thin3d has no framebuffer blit at all (calling it is fatal), so the framebuffer
			// manager hands us its 2D raster path instead - a linear-filtered quad draw.
			rasterBlit_(source, actualW, actualH, nativeInput_, sourceW, sourceH);
			chainInput = nativeInput_;
		} else if (!warnedNativeSize_) {
			WARN_LOG(Log::G3D, "LibrashaderFilterChain: this preset wants a %dx%d input but the backend can "
				"neither blit framebuffers nor raster-blit; passing the %dx%d framebuffer instead",
				sourceW, sourceH, actualW, actualH);
			warnedNativeSize_ = true;
		}
	} else if (!needsNativeInput_ && !warnedNativeSize_) {
		if (actualW != sourceW || actualH != sourceH) {
			// Two different reasons, depending on the backend: either librashader is told the size we
			// want (GL/Vulkan), or the preset does not care what the size is (D3D11 - Load() would
			// have taken the native path otherwise).
			INFO_LOG(Log::G3D, "LibrashaderFilterChain: source is %dx%d but reported as %dx%d (native); "
				"harmless here: %s", actualW, actualH, sourceW, sourceH,
				runtime_->RequiresNativeSizedInput()
					? "this preset samples no OriginalHistory1+ and does not depend on the input size"
					: "librashader honours the declared size and this preset samples no OriginalHistory1+");
		}
		warnedNativeSize_ = true;
	}

	LibrashaderFrameArgs args;
	args.frameCount = frameCount;
	args.sourceW = sourceW;
	args.sourceH = sourceH;
	args.overrides = paramOverrides_;

	bool enqueued = draw_->RunNativeCallback(chainInput, output_, runtime_->MakeFrameCallback(render_, std::move(args)), "librashader");

	if (!enqueued)
		return nullptr;
	return render_->ready.load() ? output_ : nullptr;
}

void LibrashaderFilterChain::ReleaseChain(bool deviceLost) {
	if (!render_)
		return;
	std::shared_ptr<LibrashaderRenderState> rs = render_;
	render_ = std::make_shared<LibrashaderRenderState>();
	// No adapter means Load() always failed, so the state we just swapped out is empty.
	if (!runtime_) {
#if PPSSPP_PLATFORM(WINDOWS)
		_dbg_assert_msg_(!rs->preset && !rs->vkChain && !rs->glChain && !rs->d3d11Chain,
			"librashader state is non-empty although the runtime never initialized");
#else
		_dbg_assert_msg_(!rs->preset && !rs->vkChain && !rs->glChain,
			"librashader state is non-empty although the runtime never initialized");
#endif
		return;
	}
	// draw_ is read here, before any caller nulls it (DeviceLost); the runtime decides which
	// thread may touch the handles.
	runtime_->QueueFree(draw_, std::move(rs), deviceLost);
}

void LibrashaderFilterChain::ReleaseOutput() {
	if (output_) {
		output_->Release();
		output_ = nullptr;
	}
	outputW_ = outputH_ = 0;
	if (nativeInput_) {
		nativeInput_->Release();
		nativeInput_ = nullptr;
	}
	nativeInputW_ = nativeInputH_ = 0;
}

void LibrashaderFilterChain::DeviceLost() {
	ReleaseChain(true);
	ReleaseOutput();
	valid_ = false;
	draw_ = nullptr;
}

void LibrashaderFilterChain::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	// The old runtime cached handles from the dead context.
	loggedRuntimeError_ = false;
	InitRuntime();
	if (!presetPath_.empty()) {
		std::string err;
		if (!Load(presetPath_, &err))
			ERROR_LOG(Log::G3D, "LibrashaderFilterChain: reload after device restore failed: %s", err.c_str());
	}
}

#endif  // USE_LIBRASHADER
