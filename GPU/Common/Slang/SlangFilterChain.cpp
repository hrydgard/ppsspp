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
#include "Common/Log.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Data/Format/IniFile.h"
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

	// Read the .slangp preset file
	size_t sz = 0;
	char *data = (char *)g_VFS.ReadFile(presetPath.c_str(), &sz);
	if (!data) {
		*error = "failed to read preset file: " + presetPath.ToString();
		return false;
	}
	std::string presetText(data, sz);
	delete[] data;

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

	// Compile each pass
	passes_.resize(preset_.passes.size());
	for (size_t i = 0; i < preset_.passes.size(); i++) {
		const SlangPassDesc &passDesc = preset_.passes[i];

		// Read the .slang shader file
		size_t shaderSz = 0;
		char *shaderData = (char *)g_VFS.ReadFile(passDesc.shaderPath.c_str(), &shaderSz);
		if (!shaderData) {
			*error = "failed to read shader: " + passDesc.shaderPath;
			ReleaseResources();
			return false;
		}
		std::string shaderSrc(shaderData, shaderSz);
		delete[] shaderData;

		// Split into vertex + fragment stages
		SlangSource src;
		if (!SplitSlangSource(shaderSrc, &src, error)) {
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
			if (!pass.alias.empty()) ctx.aliasNames.push_back(pass.alias);
		}
		for (const auto &lut : preset_.luts) ctx.lutNames.push_back(lut.name);

		// Compile to pipeline
		if (!CompileSlangPass(draw_, src, ctx, &passes_[i], error)) {
			ReleaseResources();
			return false;
		}
	}

	valid_ = true;
	return true;
}

Draw::Framebuffer *SlangFilterChain::Run(Draw::Framebuffer *source, int sourceW, int sourceH,
                                          int viewportW, int viewportH, int frameCount) {
	if (!valid_ || !source) {
		return nullptr;
	}

	// Ensure we have enough framebuffer slots (lazy allocation, will resize as needed)
	if (passFramebuffers_.size() < passes_.size()) {
		passFramebuffers_.resize(passes_.size(), nullptr);
	}

	Draw::Framebuffer *prevOutput = source;
	int prevW = sourceW, prevH = sourceH;

	for (size_t i = 0; i < passes_.size(); i++) {
		const SlangPassDesc &passDesc = preset_.passes[i];
		const SlangCompiledPass &pass = passes_[i];

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
			passFramebuffers_[i] = draw_->CreateFramebuffer({
				outputSize.w, outputSize.h, 1, 1, 0, false, "slang-pass"
			});
			if (!passFramebuffers_[i]) {
				ERROR_LOG(Log::G3D, "SlangFilterChain: failed to create framebuffer %dx%d", outputSize.w, outputSize.h);
				return nullptr;
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
				// Find the parameter in preset_.params by name, use its initial value
				float val = 0.0f;
				for (const auto &p : preset_.params) {
					if (p.name == m.name) {
						val = p.initial;
						break;
					}
				}
				memcpy(dst, &val, std::min((size_t)4, avail));
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

		// Bind input textures + samplers by semantic (source framebuffer, prev pass output).
		// NOTE: slang shaders declare the UBO at descriptor binding 0 and samplers at
		// binding 1..N. PPSSPP's thin3d, however, indexes textures by a 0-based *slot*
		// (slot 0 -> descriptor binding 1, slot 1 -> binding 2, ...). So convert the
		// reflected descriptor binding to a thin3d texture slot with (binding - 1).
		Draw::SamplerState *sampler = passDesc.filterLinear ? samplerLinear_ : samplerNearest_;
		for (const auto &tex : pass.reflection.textures) {
			int slot = tex.binding - 1;
			if (slot < 0) {
				continue;  // binding 0 is the UBO, not a texture slot.
			}
			Draw::Framebuffer *inputFB = nullptr;
			if (tex.semantic == SlangSemantic::TexSource) {
				inputFB = (i == 0) ? source : passFramebuffers_[i - 1];
			} else if (tex.semantic == SlangSemantic::TexOriginal) {
				inputFB = source;
			}
			if (inputFB) {
				draw_->BindFramebufferAsTexture(inputFB, slot, Draw::Aspect::COLOR_BIT, 0);
			}
			draw_->BindSamplerStates(slot, 1, &sampler);
		}

		// Bind pipeline BEFORE updating the dynamic uniform buffer — PPSSPP's Vulkan
		// UpdateDynamicUniformBuffer writes into curPipeline_, so a pipeline must be bound first.
		draw_->BindPipeline(pass.pipeline);
		if (pass.reflection.uboSizeBytes > 0) {
			draw_->UpdateDynamicUniformBuffer(uboScratch.data(), pass.reflection.uboSizeBytes);
		}
		draw_->BindVertexBuffer(quad_, 0);
		draw_->Draw(4, 0);

		// Update for next pass
		prevOutput = passFramebuffers_[i];
		prevW = outputSize.w;
		prevH = outputSize.h;
	}
	return passFramebuffers_.back();
}

void SlangFilterChain::ReleaseResources() {
	DoReleaseVector(passFramebuffers_);
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
