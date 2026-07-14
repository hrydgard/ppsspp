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

#include "ppsspp_config.h"
#ifdef DBG_NEW
#undef new
#undef free
#undef malloc
#undef realloc
#endif

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include "Common/Log.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/Data/Format/IniFile.h"
#include "Common/Data/Format/PngLoad.h"
#include "Common/GPU/thin3d.h"
#include "GPU/Common/Slang/SlangFilterChain.h"
#include "GPU/Common/Slang/SlangpParser.h"
#include "GPU/Common/Slang/SlangResolution.h"

// Helper: release a single Draw object
template <typename T>
static void DoRelease(T *&obj) {
	if (obj)
		obj->Release();
	obj = nullptr;
}

// Helper: release all objects in a vector
template <typename T>
static void DoReleaseVector(std::vector<T *> &list) {
	for (auto &obj : list)
		if (obj)
			obj->Release();
	list.clear();
}

// Read a slang asset: try the VFS first (bundled assets), then the real filesystem
// (custom shader dir). Returns true + fills *out on success.
static bool ReadSlangFile(const Path &path, std::string *out) {
	size_t sz = 0;
	uint8_t *data = g_VFS.ReadFile(path.c_str(), &sz);
	if (data) {
		out->assign((const char *)data, sz);
		delete[] data;
		return true;
	}
	// Fallback to real filesystem (custom shader dir)
	return File::ReadBinaryFileToString(path, out);
}

// Read binary slang asset (LUT PNG): try VFS first, then real filesystem.
// Returns allocated data (caller must delete[]) or nullptr.
static uint8_t *ReadSlangBinaryFile(const Path &path, size_t *outSize) {
	*outSize = 0;
	uint8_t *data = g_VFS.ReadFile(path.c_str(), outSize);
	if (data) return data;
	// Fallback to real filesystem
	return File::ReadLocalFile(path, outSize);
}

SlangFilterChain::SlangFilterChain(Draw::DrawContext *draw) : draw_(draw) {
}

SlangFilterChain::~SlangFilterChain() {
	DeviceLost();
}

bool SlangFilterChain::Load(const Path &presetPath, std::string *error) {
	presetPath_ = presetPath;
	valid_ = false;

	// Release any existing resources (but keep draw_ — we need it to build the new chain).
	ReleaseResources();

	// Read the .slangp preset file (try VFS, then real filesystem)
	std::string presetText;
	if (!ReadSlangFile(presetPath, &presetText)) {
		*error = "failed to read preset file: " + presetPath.ToString();
		return false;
	}

	// Parse the preset
	Path baseDir = Path(presetPath.GetDirectory());
	if (!ParseSlangPreset(presetText, baseDir, &preset_, error)) {
		return false;
	}

	if (preset_.passes.empty()) {
		*error = "preset has no passes";
		return false;
	}

	// Create device objects (quad vertex buffer, samplers)
	using namespace Draw;

	// Full-screen quad: positions + UVs + color (same layout as PresentationCommon)
	struct Vertex {
		float x, y, z;
		float u, v;
		uint32_t color;
	};
	Vertex quadVerts[4] = {
		{-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0xFFFFFFFF},  // TL
		{ 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0xFFFFFFFF},  // TR
		{-1.0f,  1.0f, 0.0f, 0.0f, 1.0f, 0xFFFFFFFF},  // BL
		{ 1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF},  // BR
	};

	quad_ = draw_->CreateBuffer(sizeof(quadVerts), BufferUsageFlag::DYNAMIC | BufferUsageFlag::VERTEXDATA);
	draw_->UpdateBuffer(quad_, (const uint8_t *)quadVerts, 0, sizeof(quadVerts), Draw::UPDATE_DISCARD);

	samplerLinear_ = draw_->CreateSamplerState({
		TextureFilter::LINEAR, TextureFilter::LINEAR, TextureFilter::LINEAR,
		0.0f, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE
	});
	samplerNearest_ = draw_->CreateSamplerState({
		TextureFilter::NEAREST, TextureFilter::NEAREST, TextureFilter::NEAREST,
		0.0f, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE
	});

	// Load LUT textures
	for (const auto &lut : preset_.luts) {
		// Guard: LUT must have a path
		if (lut.path.empty()) {
			*error = "LUT '" + lut.name + "' has no path";
			ReleaseResources();
			return false;
		}

		// Read PNG file (try VFS, then real filesystem)
		size_t lutSz = 0;
		uint8_t *lutData = ReadSlangBinaryFile(Path(lut.path), &lutSz);
		if (!lutData) {
			*error = "failed to load LUT: " + lut.name;
			ReleaseResources();
			return false;
		}

		// Decode PNG
		int w = 0, h = 0;
		unsigned char *pixels = nullptr;
		int pngResult = pngLoadPtr(lutData, lutSz, &w, &h, &pixels);
		delete[] lutData;
		if (pngResult != 1 || !pixels) {
			*error = "failed to load LUT: " + lut.name;
			ReleaseResources();
			return false;
		}

		// Calculate mip levels: floor(log2(max(w,h))) + 1
		int maxDim = std::max(w, h);
		int mipLevels = lut.mipmap ? (int)(std::floor(std::log2((float)maxDim)) + 1) : 1;

		// Create texture
		TextureDesc texDesc{};
		texDesc.type = TextureType::LINEAR2D;
		texDesc.format = DataFormat::R8G8B8A8_UNORM;
		texDesc.width = w;
		texDesc.height = h;
		texDesc.depth = 1;
		texDesc.mipLevels = mipLevels;
		texDesc.generateMips = lut.mipmap;
		texDesc.swizzle = TextureSwizzle::DEFAULT;
		texDesc.tag = "slang-lut";
		texDesc.initData.push_back((const uint8_t *)pixels);

		Texture *tex = draw_->CreateTexture(texDesc);
		free(pixels);
		if (!tex) {
			*error = "failed to load LUT: " + lut.name;
			ReleaseResources();
			return false;
		}

		// Map SlangWrapMode to TextureAddressMode
		TextureAddressMode wrapMode;
		switch (lut.wrapMode) {
		case SlangWrapMode::ClampToBorder:
			wrapMode = TextureAddressMode::CLAMP_TO_BORDER;
			break;
		case SlangWrapMode::ClampToEdge:
			wrapMode = TextureAddressMode::CLAMP_TO_EDGE;
			break;
		case SlangWrapMode::Repeat:
			wrapMode = TextureAddressMode::REPEAT;
			break;
		case SlangWrapMode::MirroredRepeat:
			wrapMode = TextureAddressMode::REPEAT_MIRROR;
			break;
		default:
			wrapMode = TextureAddressMode::CLAMP_TO_EDGE;
			break;
		}

		// Create sampler
		TextureFilter filter = lut.linear ? TextureFilter::LINEAR : TextureFilter::NEAREST;
		SamplerState *sampler = draw_->CreateSamplerState({
			filter, filter, filter,
			0.0f, wrapMode, wrapMode, wrapMode
		});
		if (!sampler) {
			tex->Release();
			*error = "failed to load LUT: " + lut.name;
			ReleaseResources();
			return false;
		}

		lutTextures_.push_back(tex);
		lutSamplers_.push_back(sampler);
		lutSizes_.push_back({w, h});
	}

	// Compile each pass
	passes_.resize(preset_.passes.size());
	for (size_t i = 0; i < preset_.passes.size(); i++) {
		const SlangPassDesc &passDesc = preset_.passes[i];

		// Read the .slang shader file (try VFS, then real filesystem)
		std::string shaderSrc;
		if (!ReadSlangFile(Path(passDesc.shaderPath), &shaderSrc)) {
			*error = "failed to read shader: " + passDesc.shaderPath;
			ReleaseResources();
			return false;
		}

		// Resolve #include directives recursively
		Path shaderDir(Path(passDesc.shaderPath).GetDirectory());
		SlangFileReader reader = [](const Path &path, std::string *out) -> bool {
			return ReadSlangFile(path, out);
		};
		std::string resolvedSrc;
		if (!ResolveSlangIncludes(shaderSrc, shaderDir, reader, &resolvedSrc, error)) {
			ReleaseResources();
			return false;
		}

		// Split into vertex + fragment stages
		SlangSource src;
		if (!SplitSlangSource(resolvedSrc, &src, error)) {
			ReleaseResources();
			return false;
		}

		// Collect the #pragma parameter defaults declared in this .slang into preset_.params
		// so Run() can supply their values to UserParameter uniforms. A .slangp-level override
		// (already in preset_.params from ParseSlangPreset) wins and is not overwritten.
		for (const auto &p : src.params) {
			bool exists = false;
			for (const auto &existing : preset_.params) {
				if (existing.name == p.name) { exists = true; break; }
			}
			if (!exists) {
				preset_.params.push_back(p);
			}
		}

		// Build classification context for Phase 2 semantics
		SlangClassifyContext ctx;
		for (const auto &p : preset_.params) ctx.paramNames.push_back(p.name);
		for (const auto &pass : preset_.passes) {
			ctx.aliasNames.push_back(pass.alias);  // index-aligned: aliasNames[i] is pass i's alias ("" if none)
		}
		for (const auto &lut : preset_.luts) ctx.lutNames.push_back(lut.name);

		// Compile to pipeline
		if (!CompileSlangPass(draw_, src, ctx, &passes_[i], error)) {
			ReleaseResources();
			return false;
		}
	}

	// Compute history depth: max OriginalHistory index referenced across ALL passes
	historyDepth_ = 0;
	for (const auto &pass : passes_) {
		for (const auto &tex : pass.reflection.textures) {
			if (tex.semantic == SlangSemantic::TexOriginalHistory && tex.index > historyDepth_) {
				historyDepth_ = tex.index;
			}
		}
		for (const auto &m : pass.reflection.uboMembers) {
			if (m.semantic == SlangSemantic::OriginalHistorySize && m.index > historyDepth_) {
				historyDepth_ = m.index;
			}
		}
	}

	// Compute which passes need feedback buffers: any pass referenced by TexPassFeedback/PassFeedbackSize,
	// or the global preset_.feedbackPass (if >= 0).
	passHasFeedback_.clear();
	passHasFeedback_.resize(passes_.size(), false);
	for (const auto &pass : passes_) {
		for (const auto &tex : pass.reflection.textures) {
			if (tex.semantic == SlangSemantic::TexPassFeedback && tex.index >= 0 && (size_t)tex.index < passHasFeedback_.size()) {
				passHasFeedback_[tex.index] = true;
			}
		}
		for (const auto &m : pass.reflection.uboMembers) {
			if (m.semantic == SlangSemantic::PassFeedbackSize && m.index >= 0 && (size_t)m.index < passHasFeedback_.size()) {
				passHasFeedback_[m.index] = true;
			}
		}
	}
	if (preset_.feedbackPass >= 0 && (size_t)preset_.feedbackPass < passHasFeedback_.size()) {
		passHasFeedback_[preset_.feedbackPass] = true;
	}

	valid_ = true;
	return true;
}

Draw::Framebuffer *SlangFilterChain::Run(Draw::Framebuffer *source, int sourceW, int sourceH,
                                          int viewportW, int viewportH, int frameCount) {
	if (!valid_ || !source) {
		return nullptr;
	}

	// sourceW/sourceH are the CONTENT's native resolution (reported to shaders as SourceSize).
	// The actual `source` framebuffer texture is typically larger (PPSSPP's upscaled render target).
	// History copies must use the REAL framebuffer pixel size, not the native size, or the blit
	// copies only a native-sized corner of the upscaled source (produces a scaled/offset duplicate).
	int srcActualW = sourceW, srcActualH = sourceH;
	draw_->GetFramebufferDimensions(source, &srcActualW, &srcActualH);

	// Ensure we have enough framebuffer slots (lazy allocation, will resize as needed)
	if (passFramebuffers_.size() < passes_.size()) {
		passFramebuffers_.resize(passes_.size(), nullptr);
	}

	// Allocate history ring: historyRing_[k] will hold the input from k frames ago (k >= 1).
	// OriginalHistory0 is the current source (no ring slot needed); indices 1..historyDepth_ need retained copies.
	// historyRing_[0] = newest (1 frame ago), historyRing_[historyDepth_-1] = oldest.
	if (historyDepth_ > 0) {
		if (historyRing_.size() != (size_t)historyDepth_) {
			DoReleaseVector(historyRing_);
			historyRing_.resize(historyDepth_, nullptr);
		}
		// Allocate or resize if source dimensions changed
		for (int k = 0; k < historyDepth_; k++) {
			bool needsResize = false;
			if (historyRing_[k]) {
				int fbW, fbH;
				draw_->GetFramebufferDimensions(historyRing_[k], &fbW, &fbH);
				needsResize = (fbW != srcActualW || fbH != srcActualH);
			}
			if (!historyRing_[k] || needsResize) {
				DoRelease(historyRing_[k]);
				using namespace Draw;
				historyRing_[k] = draw_->CreateFramebuffer({
					srcActualW, srcActualH, 1, 1, 0, false, "slang-history"
				});
				if (!historyRing_[k]) {
					ERROR_LOG(Log::G3D, "SlangFilterChain: failed to create history framebuffer %dx%d", srcActualW, srcActualH);
					return nullptr;
				}
				// Clear on first allocation to avoid binding garbage on the first frame
				draw_->BindFramebufferAsRenderTarget(historyRing_[k], {
					Draw::RPAction::CLEAR, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE
				}, "slang-history-clear");
			}
		}
	}

	// Allocate feedback buffers: feedbackBuffers_[j] holds previous-frame output of pass j
	// (for passes with passHasFeedback_[j] true). Size-matched to the pass output at runtime.
	// Strategy: single retained buffer per feedback pass; after pass j renders, blit its output
	// into feedbackBuffers_[j] so next frame reads previous-frame content. First frame (no history)
	// binds a cleared buffer (CreateFramebuffer clears on allocation via RPAction::CLEAR).
	if (feedbackBuffers_.size() != passes_.size()) {
		DoReleaseVector(feedbackBuffers_);
		feedbackBuffers_.resize(passes_.size(), nullptr);
	}

	Draw::Framebuffer *prevOutput = source;
	int prevW = sourceW, prevH = sourceH;

	for (size_t i = 0; i < passes_.size(); i++) {
		const SlangPassDesc &passDesc = preset_.passes[i];
		const SlangCompiledPass &pass = passes_[i];

		// Warn once if mipmapInput is requested (framebuffer mips not supported)
		if (passDesc.mipmapInput) {
			static bool warned = false;
			if (!warned) {
				WARN_LOG(Log::G3D, "slang: mipmap_input on framebuffer inputs not supported, sampling base level only (LUT mips work)");
				warned = true;
			}
		}

		// Compute output size for this pass
		SlangSize inputSize = { prevW, prevH };
		SlangSize viewportSize = { viewportW, viewportH };
		SlangSize outputSize = ResolvePassSize(passDesc, inputSize, viewportSize);

		// Allocate or reuse framebuffer (check if size changed)
		bool needsResize = false;
		if (passFramebuffers_[i]) {
			int fbW, fbH;
			draw_->GetFramebufferDimensions(passFramebuffers_[i], &fbW, &fbH);
			needsResize = (fbW != outputSize.w || fbH != outputSize.h);
		}
		if (!passFramebuffers_[i] || needsResize) {
			DoRelease(passFramebuffers_[i]);
			using namespace Draw;

			// Select color format based on pass requirements
			DataFormat colorFormat = DataFormat::R8G8B8A8_UNORM;  // default
			if (passDesc.formatOverride == SlangFbFormat::Srgb || passDesc.srgbFramebuffer) {
				colorFormat = DataFormat::R8G8B8A8_UNORM_SRGB;
			} else if (passDesc.formatOverride == SlangFbFormat::Float || passDesc.floatFramebuffer) {
				colorFormat = DataFormat::R16G16B16A16_FLOAT;
			}

			// Check format support and fall back if unsupported
			if (colorFormat != DataFormat::R8G8B8A8_UNORM) {
				uint32_t support = draw_->GetDataFormatSupport(colorFormat);
				if (!(support & FMT_RENDERTARGET)) {
					static bool warned = false;
					if (!warned) {
						WARN_LOG(Log::G3D, "slang: %s framebuffer unsupported, falling back to UNORM (colors may differ)",
							colorFormat == DataFormat::R8G8B8A8_UNORM_SRGB ? "sRGB" : "float");
						warned = true;
					}
					colorFormat = DataFormat::R8G8B8A8_UNORM;
				}
			}

			passFramebuffers_[i] = draw_->CreateFramebuffer({
				outputSize.w, outputSize.h, 1, 1, 0, false, "slang-pass", colorFormat
			});
			if (!passFramebuffers_[i]) {
				ERROR_LOG(Log::G3D, "SlangFilterChain: failed to create framebuffer %dx%d", outputSize.w, outputSize.h);
				return nullptr;
			}
		}

		// Allocate feedback buffer if this pass needs one (lazy, at pass output size)
		if (i < passHasFeedback_.size() && passHasFeedback_[i]) {
			bool feedbackNeedsResize = false;
			if (feedbackBuffers_[i]) {
				int fbW, fbH;
				draw_->GetFramebufferDimensions(feedbackBuffers_[i], &fbW, &fbH);
				feedbackNeedsResize = (fbW != outputSize.w || fbH != outputSize.h);
			}
			if (!feedbackBuffers_[i] || feedbackNeedsResize) {
				DoRelease(feedbackBuffers_[i]);
				using namespace Draw;

				// Select color format (same logic as pass framebuffer)
				DataFormat colorFormat = DataFormat::R8G8B8A8_UNORM;
				if (passDesc.formatOverride == SlangFbFormat::Srgb || passDesc.srgbFramebuffer) {
					colorFormat = DataFormat::R8G8B8A8_UNORM_SRGB;
				} else if (passDesc.formatOverride == SlangFbFormat::Float || passDesc.floatFramebuffer) {
					colorFormat = DataFormat::R16G16B16A16_FLOAT;
				}
				if (colorFormat != DataFormat::R8G8B8A8_UNORM) {
					uint32_t support = draw_->GetDataFormatSupport(colorFormat);
					if (!(support & FMT_RENDERTARGET)) {
						colorFormat = DataFormat::R8G8B8A8_UNORM;  // silent fallback (already warned above)
					}
				}

				feedbackBuffers_[i] = draw_->CreateFramebuffer({
					outputSize.w, outputSize.h, 1, 1, 0, false, "slang-feedback", colorFormat
				});
				if (!feedbackBuffers_[i]) {
					ERROR_LOG(Log::G3D, "SlangFilterChain: failed to create feedback framebuffer %dx%d", outputSize.w, outputSize.h);
					return nullptr;
				}
				// Clear on first allocation to avoid binding garbage on the first frame
				draw_->BindFramebufferAsRenderTarget(feedbackBuffers_[i], {
					Draw::RPAction::CLEAR, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE
				}, "slang-feedback-clear");
			}
		}

		// Build UBO scratch buffer
		std::vector<uint8_t> uboScratch(pass.reflection.uboSizeBytes, 0);
		for (const auto &m : pass.reflection.uboMembers) {
			uint8_t *dst = uboScratch.data() + m.offsetBytes;
			// Guard against a malformed shader whose named member is smaller/mis-typed than the
			// semantic's expected write width (would otherwise overflow the std140 scratch buffer).
			size_t avail = (m.offsetBytes <= uboScratch.size()) ? (uboScratch.size() - m.offsetBytes) : 0;
			switch (m.semantic) {
			case SlangSemantic::MVP: {
				// Identity mat4 (PPSSPP uses pre-transformed quad)
				float identity[16] = {
					1, 0, 0, 0,
					0, 1, 0, 0,
					0, 0, 1, 0,
					0, 0, 0, 1
				};
				memcpy(dst, identity, std::min((size_t)64, avail));
				break;
			}
			case SlangSemantic::SourceSize: {
				float v[4] = { (float)inputSize.w, (float)inputSize.h,
				               1.0f / (float)std::max(1, inputSize.w), 1.0f / (float)std::max(1, inputSize.h) };
				memcpy(dst, v, std::min((size_t)16, avail));
				break;
			}
			case SlangSemantic::OriginalSize: {
				// In Phase 1 (linear chain), OriginalSize = source size
				float v[4] = { (float)sourceW, (float)sourceH,
				               1.0f / (float)std::max(1, sourceW), 1.0f / (float)std::max(1, sourceH) };
				memcpy(dst, v, std::min((size_t)16, avail));
				break;
			}
			case SlangSemantic::OutputSize: {
				float v[4] = { (float)outputSize.w, (float)outputSize.h,
				               1.0f / (float)std::max(1, outputSize.w), 1.0f / (float)std::max(1, outputSize.h) };
				memcpy(dst, v, std::min((size_t)16, avail));
				break;
			}
			case SlangSemantic::FinalViewportSize: {
				float v[4] = { (float)viewportW, (float)viewportH,
				               1.0f / (float)std::max(1, viewportW), 1.0f / (float)std::max(1, viewportH) };
				memcpy(dst, v, std::min((size_t)16, avail));
				break;
			}
			case SlangSemantic::FrameCount: {
				uint32_t fc = (uint32_t)frameCount;
				// Apply frameCountMod if specified for this pass
				if (passDesc.frameCountMod > 0) {
					fc = (uint32_t)(frameCount % passDesc.frameCountMod);
				}
				memcpy(dst, &fc, std::min((size_t)4, avail));
				break;
			}
			case SlangSemantic::FrameDirection: {
				int fd = 1;
				memcpy(dst, &fd, std::min((size_t)4, avail));
				break;
			}
			case SlangSemantic::Rotation: {
				int rot = 0;
				memcpy(dst, &rot, std::min((size_t)4, avail));
				break;
			}
			case SlangSemantic::UserParameter: {
				float val = ResolveParamValue(m.name, preset_.params, paramOverrides_);
				memcpy(dst, &val, std::min((size_t)4, avail));
				break;
			}
			case SlangSemantic::PassOutputSize: {
				// Range-check: m.index must be >= 0 and < i (causal: only earlier passes)
				if (m.index >= 0 && m.index < (int)i && (size_t)m.index < passFramebuffers_.size() && passFramebuffers_[m.index]) {
					int fbW, fbH;
					draw_->GetFramebufferDimensions(passFramebuffers_[m.index], &fbW, &fbH);
					float v[4] = { (float)fbW, (float)fbH,
					               1.0f / (float)std::max(1, fbW), 1.0f / (float)std::max(1, fbH) };
					memcpy(dst, v, std::min((size_t)16, avail));
				} else {
					// Out of range or malformed: write source dims as safe fallback
					float v[4] = { (float)sourceW, (float)sourceH,
					               1.0f / (float)std::max(1, sourceW), 1.0f / (float)std::max(1, sourceH) };
					memcpy(dst, v, std::min((size_t)16, avail));
				}
				break;
			}
			case SlangSemantic::OriginalHistorySize: {
				// All history frames are source-sized; index doesn't matter for dimensions
				float v[4] = { (float)sourceW, (float)sourceH,
				               1.0f / (float)std::max(1, sourceW), 1.0f / (float)std::max(1, sourceH) };
				memcpy(dst, v, std::min((size_t)16, avail));
				break;
			}
			case SlangSemantic::LutSize: {
				// Range-check m.index against lutSizes_
				if (m.index >= 0 && (size_t)m.index < lutSizes_.size()) {
					int w = lutSizes_[m.index].first;
					int h = lutSizes_[m.index].second;
					float v[4] = { (float)w, (float)h,
					               1.0f / (float)std::max(1, w), 1.0f / (float)std::max(1, h) };
					memcpy(dst, v, std::min((size_t)16, avail));
				} else {
					// Out of range: write 1x1 as safe fallback
					float v[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
					memcpy(dst, v, std::min((size_t)16, avail));
				}
				break;
			}
			case SlangSemantic::PassFeedbackSize: {
				// Range-check m.index and retrieve feedback buffer dimensions
				if (m.index >= 0 && (size_t)m.index < feedbackBuffers_.size() && feedbackBuffers_[m.index]) {
					int fbW, fbH;
					draw_->GetFramebufferDimensions(feedbackBuffers_[m.index], &fbW, &fbH);
					float v[4] = { (float)fbW, (float)fbH,
					               1.0f / (float)std::max(1, fbW), 1.0f / (float)std::max(1, fbH) };
					memcpy(dst, v, std::min((size_t)16, avail));
				} else {
					// Out of range or unallocated: write source dims as safe fallback
					float v[4] = { (float)sourceW, (float)sourceH,
					               1.0f / (float)std::max(1, sourceW), 1.0f / (float)std::max(1, sourceH) };
					memcpy(dst, v, std::min((size_t)16, avail));
				}
				break;
			}
			default:
				break;
			}
		}

		// Bind framebuffer as render target and clear (must precede pipeline/texture binds).
		draw_->BindFramebufferAsRenderTarget(passFramebuffers_[i], {
			Draw::RPAction::CLEAR, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE
		}, "slang-pass");

		// Set viewport and scissor
		Draw::Viewport vp = { 0, 0, (float)outputSize.w, (float)outputSize.h, 0.0f, 1.0f };
		draw_->SetViewport(vp);
		draw_->SetScissorRect(0, 0, outputSize.w, outputSize.h);

		// Bind input textures + samplers by semantic (source, PassOutput, history, LUTs).
		// NOTE: slang shaders declare the UBO at descriptor binding 0 and samplers at
		// binding 1..N. PPSSPP's thin3d, however, indexes textures by a 0-based *slot*
		// (slot 0 -> descriptor binding 1, slot 1 -> binding 2, ...). So convert the
		// reflected descriptor binding to a thin3d texture slot with (binding - 1).

		// Create per-pass sampler based on filterLinear + wrapMode
		Draw::TextureFilter filter = passDesc.filterLinear ? Draw::TextureFilter::LINEAR : Draw::TextureFilter::NEAREST;
		Draw::TextureAddressMode wrapMode;
		switch (passDesc.wrapMode) {
		case SlangWrapMode::ClampToBorder: wrapMode = Draw::TextureAddressMode::CLAMP_TO_BORDER; break;
		case SlangWrapMode::ClampToEdge:   wrapMode = Draw::TextureAddressMode::CLAMP_TO_EDGE; break;
		case SlangWrapMode::Repeat:        wrapMode = Draw::TextureAddressMode::REPEAT; break;
		case SlangWrapMode::MirroredRepeat: wrapMode = Draw::TextureAddressMode::REPEAT_MIRROR; break;
		default:                           wrapMode = Draw::TextureAddressMode::CLAMP_TO_EDGE; break;
		}
		Draw::SamplerState *sampler = draw_->CreateSamplerState({
			filter, filter, filter, 0.0f, wrapMode, wrapMode, wrapMode
		});
		for (const auto &tex : pass.reflection.textures) {
			int slot = tex.binding - 1;
			if (slot < 0) {
				continue;  // binding 0 is the UBO, not a texture slot.
			}
			if (slot >= (int)Draw::MAX_TEXTURE_SLOTS) {
				// A malformed/hostile shader can declare a sampler at an arbitrarily high
				// binding; passing slot >= MAX_TEXTURE_SLOTS to thin3d would index past the
				// backend's bound-texture arrays. Skip it (leaves the sampler unfed, which
				// glslang/reflection already tolerates for unused-in-practice bindings).
				static bool logged = false;
				if (!logged) {
					ERROR_LOG(Log::G3D, "SlangFilterChain: texture '%s' binding %d (slot %d) exceeds MAX_TEXTURE_SLOTS %u; skipping",
						tex.name.c_str(), tex.binding, slot, (unsigned)Draw::MAX_TEXTURE_SLOTS);
					logged = true;
				}
				continue;
			}

			Draw::Framebuffer *inputFB = nullptr;
			Draw::SamplerState *useSampler = sampler;  // default to pass sampler
			bool isLut = false;

			switch (tex.semantic) {
			case SlangSemantic::TexSource:
				inputFB = (i == 0) ? source : passFramebuffers_[i - 1];
				break;
			case SlangSemantic::TexOriginal:
				inputFB = source;
				break;
			case SlangSemantic::TexOriginalHistory:
				// OriginalHistory0 = current source; indices 1..historyDepth_ = ring slots
				if (tex.index == 0) {
					inputFB = source;
				} else if (tex.index >= 1 && tex.index <= historyDepth_ && (size_t)(tex.index - 1) < historyRing_.size()) {
					// historyRing_[0] = 1 frame ago, historyRing_[k-1] = k frames ago
					inputFB = historyRing_[tex.index - 1];
				} else {
					// Out of range: bind source as safe fallback
					inputFB = source;
				}
				break;
			case SlangSemantic::TexPassOutput:
				// Range-check: tex.index must be >= 0 and < i (causal: only earlier passes)
				if (tex.index >= 0 && tex.index < (int)i && (size_t)tex.index < passFramebuffers_.size()) {
					inputFB = passFramebuffers_[tex.index];
				} else {
					// Out of range or malformed: log once and bind source as safe fallback
					static bool logged = false;
					if (!logged) {
						ERROR_LOG(Log::G3D, "SlangFilterChain: PassOutput index %d out of range (current pass %d)", tex.index, (int)i);
						logged = true;
					}
					inputFB = source;
				}
				break;
			case SlangSemantic::TexPassFeedback:
				// Range-check tex.index and verify feedback buffer exists
				if (tex.index >= 0 && (size_t)tex.index < feedbackBuffers_.size() && feedbackBuffers_[tex.index]) {
					inputFB = feedbackBuffers_[tex.index];
				} else {
					// Out of range or unallocated: log once and bind source as safe fallback
					static bool logged = false;
					if (!logged) {
						ERROR_LOG(Log::G3D, "SlangFilterChain: PassFeedback index %d out of range or unallocated", tex.index);
						logged = true;
					}
					inputFB = source;
				}
				break;
			case SlangSemantic::TexLut:
				// Range-check tex.index against BOTH lutTextures_ and lutSamplers_
				if (tex.index >= 0 && (size_t)tex.index < lutTextures_.size() && (size_t)tex.index < lutSamplers_.size()) {
					isLut = true;
					draw_->BindTexture(slot, lutTextures_[tex.index]);
					useSampler = lutSamplers_[tex.index];
				} else {
					// Out of range: bind source as safe fallback
					static bool logged = false;
					if (!logged) {
						ERROR_LOG(Log::G3D, "SlangFilterChain: LUT index %d out of range", tex.index);
						logged = true;
					}
					inputFB = source;
				}
				break;
			default:
				break;
			}

			// Bind framebuffer inputs (if not a LUT)
			if (!isLut && inputFB) {
				draw_->BindFramebufferAsTexture(inputFB, slot, Draw::Aspect::COLOR_BIT, 0);
			}
			// Bind sampler for all texture types
			if (isLut || inputFB) {
				draw_->BindSamplerStates(slot, 1, &useSampler);
			}
		}

		// Bind pipeline BEFORE updating the dynamic uniform buffer — PPSSPP's Vulkan
		// UpdateDynamicUniformBuffer writes into curPipeline_, so a pipeline must be bound first.
		draw_->BindPipeline(pass.pipeline);
		if (pass.reflection.uboSizeBytes > 0) {
			draw_->UpdateDynamicUniformBuffer(uboScratch.data(), pass.reflection.uboSizeBytes);
		}
		draw_->BindVertexBuffer(quad_, 0);
		draw_->Draw(4, 0);

		// Release per-pass sampler
		sampler->Release();

		// Update for next pass
		prevOutput = passFramebuffers_[i];
		prevW = outputSize.w;
		prevH = outputSize.h;
	}

	// Update feedback buffers: for each pass with feedback enabled, blit this frame's
	// output into its feedback buffer so next frame reads previous-frame content.
	for (size_t j = 0; j < passes_.size(); j++) {
		if (j < passHasFeedback_.size() && passHasFeedback_[j] && feedbackBuffers_[j] && passFramebuffers_[j]) {
			int fbW, fbH;
			draw_->GetFramebufferDimensions(passFramebuffers_[j], &fbW, &fbH);
			draw_->BlitFramebuffer(passFramebuffers_[j], 0, 0, fbW, fbH,
			                       feedbackBuffers_[j], 0, 0, fbW, fbH,
			                       Draw::Aspect::COLOR_BIT, Draw::FB_BLIT_NEAREST, "slang-feedback");
		}
	}

	// Advance history ring: copy current source into the newest slot and rotate.
	// historyRing_ is maintained newest-first: [0] = 1 frame ago, [historyDepth_-1] = oldest.
	// Rotation: move the oldest frame to the front, then blit source into it (it becomes the newest).
	if (historyDepth_ > 0 && !historyRing_.empty()) {
		// Rotate: move last element to front
		Draw::Framebuffer *oldest = historyRing_.back();
		historyRing_.pop_back();
		historyRing_.insert(historyRing_.begin(), oldest);
		// Blit the FULL source framebuffer (real pixel size) into the newest slot.
		draw_->BlitFramebuffer(source, 0, 0, srcActualW, srcActualH,
		                       historyRing_[0], 0, 0, srcActualW, srcActualH,
		                       Draw::Aspect::COLOR_BIT, Draw::FB_BLIT_NEAREST, "slang-history");
	}

	return passFramebuffers_.back();
}

void SlangFilterChain::ReleaseResources() {
	DoReleaseVector(passFramebuffers_);
	DoReleaseVector(historyRing_);
	DoReleaseVector(feedbackBuffers_);
	DoReleaseVector(lutTextures_);
	DoReleaseVector(lutSamplers_);
	lutSizes_.clear();
	historyDepth_ = 0;
	passHasFeedback_.clear();
	for (auto &pass : passes_) {
		DoRelease(pass.pipeline);
	}
	passes_.clear();
	DoRelease(quad_);
	DoRelease(samplerLinear_);
	DoRelease(samplerNearest_);
	valid_ = false;
}

void SlangFilterChain::DeviceLost() {
	// Full device teardown: release resources AND drop the device pointer.
	// DeviceRestore() supplies a fresh device before reloading.
	ReleaseResources();
	draw_ = nullptr;
}

void SlangFilterChain::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	if (!presetPath_.empty()) {
		std::string error;
		if (!Load(presetPath_, &error)) {
			ERROR_LOG(Log::G3D, "SlangFilterChain::DeviceRestore failed: %s", error.c_str());
		}
	}
}

void SlangFilterChain::SetParamOverrides(const std::map<std::string, float> &overrides) {
	paramOverrides_ = overrides;
}

float SlangFilterChain::ResolveParamValue(const std::string &name,
		const std::vector<SlangParamDesc> &params, const std::map<std::string, float> &overrides) {
	auto it = overrides.find(name);
	if (it != overrides.end()) return it->second;
	for (const auto &p : params) if (p.name == name) return p.initial;
	return 0.0f;
}
