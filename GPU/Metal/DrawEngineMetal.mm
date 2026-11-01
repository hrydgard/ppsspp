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

#include "ppsspp_config.h"

#include <algorithm>
#include <functional>

#include "Common/Log.h"
#include "Common/Profiler/Profiler.h"
#include "Common/GPU/thin3d.h"
#include "Core/Config.h"
#include "GPU/GPUState.h"
#include "GPU/GPUCommon.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Metal/DrawEngineMetal.h"
#include "GPU/Metal/ShaderManagerMetal.h"
#include "GPU/Metal/TextureCacheMetal.h"
#include "GPU/Metal/FramebufferManagerMetal.h"

static const MTLBlendFactor blendFactorToMetal[(size_t)BlendFactor::COUNT] = {
	MTLBlendFactorZero,
	MTLBlendFactorOne,
	MTLBlendFactorSourceColor,
	MTLBlendFactorOneMinusSourceColor,
	MTLBlendFactorDestinationColor,
	MTLBlendFactorOneMinusDestinationColor,
	MTLBlendFactorSourceAlpha,
	MTLBlendFactorOneMinusSourceAlpha,
	MTLBlendFactorDestinationAlpha,
	MTLBlendFactorOneMinusDestinationAlpha,
	MTLBlendFactorBlendColor,
	MTLBlendFactorOneMinusBlendColor,
	MTLBlendFactorBlendAlpha,
	MTLBlendFactorOneMinusBlendAlpha,
	MTLBlendFactorSource1Color,
	MTLBlendFactorOneMinusSource1Color,
	MTLBlendFactorSource1Alpha,
	MTLBlendFactorOneMinusSource1Alpha,
	MTLBlendFactorZero,  // INVALID
};

static const MTLBlendOperation blendEqToMetal[(size_t)BlendEq::COUNT] = {
	MTLBlendOperationAdd,
	MTLBlendOperationSubtract,
	MTLBlendOperationReverseSubtract,
	MTLBlendOperationMin,
	MTLBlendOperationMax,
};

// In GE order.
static const MTLCompareFunction compareToMetal[8] = {
	MTLCompareFunctionNever,
	MTLCompareFunctionAlways,
	MTLCompareFunctionEqual,
	MTLCompareFunctionNotEqual,
	MTLCompareFunctionLess,
	MTLCompareFunctionLessEqual,
	MTLCompareFunctionGreater,
	MTLCompareFunctionGreaterEqual,
};

static const MTLStencilOperation stencilOpToMetal[8] = {
	MTLStencilOperationKeep,
	MTLStencilOperationZero,
	MTLStencilOperationReplace,
	MTLStencilOperationInvert,
	MTLStencilOperationIncrementClamp,
	MTLStencilOperationDecrementClamp,
	MTLStencilOperationKeep,
	MTLStencilOperationKeep,
};

// Not a valid decoded format ID (those can be 0).
static constexpr uint32_t SOFTWARE_TRANSFORM_VTXFMT = 0xFFFFFFFF;

static MTLVertexFormat DecFmtToMetal(int fmt) {
	switch (fmt) {
	case DEC_FLOAT_1: return MTLVertexFormatFloat;
	case DEC_FLOAT_2: return MTLVertexFormatFloat2;
	case DEC_FLOAT_3: return MTLVertexFormatFloat3;
	case DEC_FLOAT_4: return MTLVertexFormatFloat4;
	case DEC_S8_3: return MTLVertexFormatChar4Normalized;
	case DEC_S16_3: return MTLVertexFormatShort4Normalized;
	case DEC_U8_1:
	case DEC_U8_2:
	case DEC_U8_3:
	case DEC_U8_4: return MTLVertexFormatUChar4Normalized;
	case DEC_U16_1:
	case DEC_U16_2: return MTLVertexFormatUShort2Normalized;
	case DEC_U16_3:
	case DEC_U16_4: return MTLVertexFormatUShort4Normalized;
	default: return MTLVertexFormatInvalid;
	}
}

static void SetAttribute(MTLVertexDescriptor *vd, PspAttributeLocation location, int fmt, int offset) {
	MTLVertexAttributeDescriptor *attr = vd.attributes[(int)location];
	attr.format = DecFmtToMetal(fmt);
	attr.offset = offset;
	attr.bufferIndex = METAL_VERTEX_BUFFER_INDEX;
}

static MTLVertexDescriptor *CreateVertexDescriptor(const DecVtxFormat *decFmt) {
	MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
	if (decFmt) {
		if (decFmt->w0fmt != 0) SetAttribute(vd, PspAttributeLocation::W1, decFmt->w0fmt, decFmt->w0off);
		if (decFmt->w1fmt != 0) SetAttribute(vd, PspAttributeLocation::W2, decFmt->w1fmt, decFmt->w1off);
		if (decFmt->uvfmt != 0) SetAttribute(vd, PspAttributeLocation::TEXCOORD, decFmt->uvfmt, decFmt->uvoff);
		if (decFmt->c0fmt != 0) SetAttribute(vd, PspAttributeLocation::COLOR0, decFmt->c0fmt, decFmt->c0off);
		if (decFmt->c1fmt != 0) SetAttribute(vd, PspAttributeLocation::COLOR1, decFmt->c1fmt, decFmt->c1off);
		if (decFmt->nrmfmt != 0) SetAttribute(vd, PspAttributeLocation::NORMAL, decFmt->nrmfmt, decFmt->nrmoff);
		SetAttribute(vd, PspAttributeLocation::POSITION, DecVtxFormat::PosFmt(), decFmt->posoff);
		vd.layouts[METAL_VERTEX_BUFFER_INDEX].stride = decFmt->stride;
	} else {
		// Software transformed. Unused attributes don't hurt.
		SetAttribute(vd, PspAttributeLocation::POSITION, DEC_FLOAT_4, offsetof(TransformedVertex, pos));
		SetAttribute(vd, PspAttributeLocation::TEXCOORD, DEC_FLOAT_3, offsetof(TransformedVertex, uv));
		SetAttribute(vd, PspAttributeLocation::COLOR0, DEC_U8_4, offsetof(TransformedVertex, color0_32));
		SetAttribute(vd, PspAttributeLocation::COLOR1, DEC_U8_4, offsetof(TransformedVertex, color1_32));
		SetAttribute(vd, PspAttributeLocation::NORMAL, DEC_FLOAT_1, offsetof(TransformedVertex, fog));
		vd.layouts[METAL_VERTEX_BUFFER_INDEX].stride = sizeof(TransformedVertex);
	}
	vd.layouts[METAL_VERTEX_BUFFER_INDEX].stepFunction = MTLVertexStepFunctionPerVertex;
	return vd;
}

DrawEngineMetal::DrawEngineMetal(Draw::DrawContext *draw) : draw_(draw), pipelines_(256) {
	decOptions_.expand8BitNormalsToFloat = false;
	InitDeviceObjects();
}

DrawEngineMetal::~DrawEngineMetal() {
	ClearPipelines();
	if (draw_) {
		draw_->SetInvalidationCallback(InvalidationCallback());
	}
}

void DrawEngineMetal::InitDeviceObjects() {
	rm_ = (MetalRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	id<MTLDevice> device = rm_->Device();
	MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
	sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
	sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
	sd.rAddressMode = MTLSamplerAddressModeClampToEdge;
	samplerNearest_ = [device newSamplerStateWithDescriptor:sd];
	sd.minFilter = MTLSamplerMinMagFilterLinear;
	sd.magFilter = MTLSamplerMinMagFilterLinear;
	samplerLinear_ = [device newSamplerStateWithDescriptor:sd];
	nullTexture_ = (__bridge id<MTLTexture>)(void *)draw_->GetNativeObject(Draw::NativeObject::NULL_IMAGEVIEW);
}

void DrawEngineMetal::DeviceLost() {
	ClearPipelines();
	depthStencilCache_.clear();
	vertexDescriptors_.clear();
	if (draw_) {
		draw_->SetInvalidationCallback(InvalidationCallback());
		invalidationCallbackInstalled_ = false;
	}
	boundSecondary_ = nil;
	boundDepal_ = nil;
	// Allocated from the old render manager's push buffer.
	baseUBO_ = MetalPushAlloc{};
	lightUBO_ = MetalPushAlloc{};
	uboFrame_ = (uint64_t)-1;
	draw_ = nullptr;
	rm_ = nullptr;
}

void DrawEngineMetal::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	InitDeviceObjects();
}

void DrawEngineMetal::ClearPipelines() {
	pipelines_.Iterate([&](const MetalPipelineKey &, Pipeline *pipeline) {
		if (rm_) {
			rm_->DeferRelease(pipeline->state);
		}
		delete pipeline;
	});
	pipelines_.Clear();
	lastPipeline_ = nullptr;
}

void DrawEngineMetal::BeginFrame() {
	DrawEngineCommon::BeginFrame();
	if (!invalidationCallbackInstalled_) {
		draw_->SetInvalidationCallback(std::bind(&DrawEngineMetal::Invalidate, this, std::placeholders::_1));
		invalidationCallbackInstalled_ = true;
	}
	lastPipeline_ = nullptr;
	boundDepal_ = nil;
	boundSecondary_ = nil;
	baseUBO_ = MetalPushAlloc{};
	lightUBO_ = MetalPushAlloc{};
	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
	AssertEmpty();
}

void DrawEngineMetal::Invalidate(InvalidationCallbackFlags flags) {
	if (flags & InvalidationCallbackFlags::RENDER_PASS_STATE) {
		// The render target might have a different format. The encoder state itself is tracked by the render manager.
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_BLEND_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
		lastPipeline_ = nullptr;
	}
}

id<MTLDepthStencilState> DrawEngineMetal::GetDepthStencilState(const MetalDepthStencilKey &key) {
	auto iter = depthStencilCache_.find(key.value);
	if (iter != depthStencilCache_.end()) {
		return iter->second;
	}
	MTLDepthStencilDescriptor *dd = [MTLDepthStencilDescriptor new];
	if (key.depthTestEnable) {
		dd.depthCompareFunction = compareToMetal[key.depthCompare];
		dd.depthWriteEnabled = key.depthWriteEnable;
	} else {
		dd.depthCompareFunction = MTLCompareFunctionAlways;
		dd.depthWriteEnabled = NO;
	}
	if (key.stencilTestEnable) {
		MTLStencilDescriptor *sd = [MTLStencilDescriptor new];
		sd.stencilCompareFunction = compareToMetal[key.stencilCompare];
		sd.stencilFailureOperation = stencilOpToMetal[key.stencilFail];
		sd.depthFailureOperation = stencilOpToMetal[key.stencilDepthFail];
		sd.depthStencilPassOperation = stencilOpToMetal[key.stencilPass];
		sd.readMask = (uint32_t)key.compareMask;
		sd.writeMask = (uint32_t)key.writeMask;
		dd.frontFaceStencil = sd;
		dd.backFaceStencil = sd;
	}
	id<MTLDepthStencilState> dss = [rm_->Device() newDepthStencilStateWithDescriptor:dd];
	depthStencilCache_[key.value] = dss;
	return dss;
}

DrawEngineMetal::Pipeline *DrawEngineMetal::GetPipeline(const VShaderID &vsid, const FShaderID &fsid, bool useHWTransform) {
	MetalPipelineKey key{};
	key.vsid = vsid;
	key.fsid = fsid;
	key.vtxFmtId = useHWTransform ? dec_->decFmt.id : SOFTWARE_TRANSFORM_VTXFMT;
	key.blend = blendKey_.value;
	key.targetFormat = rm_->CurTargetFormat().Key();
	return GetOrCreatePipeline(key, useHWTransform ? &dec_->decFmt : nullptr, rm_->CurTargetFormat());
}

DrawEngineMetal::Pipeline *DrawEngineMetal::GetOrCreatePipeline(const MetalPipelineKey &key, const DecVtxFormat *decFmt, const MetalRenderTargetFormat &format) {
	Pipeline *pipeline = pipelines_.GetOrNull(key);
	if (pipeline) {
		return pipeline;
	}

	MetalVertexShader *vs = shaderManager_->GetVertexShaderFromID(key.vsid);
	MetalFragmentShader *fs = shaderManager_->GetFragmentShaderFromID(key.fsid);
	pipeline = new Pipeline();
	pipeline->fragmentArrayTextureMask = fs->info.arrayTextureMask;
	pipeline->fragmentTextureMask = fs->info.textureMask;
	pipeline->state = [MetalPipelineState new];
	pipeline->format = format;
	pipelines_.Insert(key, pipeline);
	if (vs->Failed() || fs->Failed()) {
		[pipeline->state setPipeline:nil];
		return pipeline;
	}

	MTLVertexDescriptor *vd;
	auto iter = vertexDescriptors_.find(key.vtxFmtId);
	if (iter != vertexDescriptors_.end()) {
		vd = iter->second;
	} else {
		vd = CreateVertexDescriptor(decFmt);
		vertexDescriptors_[key.vtxFmtId] = vd;
	}

	MetalBlendKey blend;
	blend.value = key.blend;
	MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
	desc.vertexDescriptor = vd;
	desc.rasterSampleCount = format.sampleCount;
	desc.depthAttachmentPixelFormat = format.depth;
	desc.stencilAttachmentPixelFormat = format.stencil;
	if (format.color != MTLPixelFormatInvalid) {
		MTLRenderPipelineColorAttachmentDescriptor *att = desc.colorAttachments[0];
		att.pixelFormat = format.color;
		MTLColorWriteMask mask = MTLColorWriteMaskNone;
		if (blend.colorWriteMask & 1) mask |= MTLColorWriteMaskRed;
		if (blend.colorWriteMask & 2) mask |= MTLColorWriteMaskGreen;
		if (blend.colorWriteMask & 4) mask |= MTLColorWriteMaskBlue;
		if (blend.colorWriteMask & 8) mask |= MTLColorWriteMaskAlpha;
		att.writeMask = mask;
		att.blendingEnabled = blend.blendEnable;
		if (blend.blendEnable) {
			att.rgbBlendOperation = blendEqToMetal[blend.eqColor];
			att.alphaBlendOperation = blendEqToMetal[blend.eqAlpha];
			att.sourceRGBBlendFactor = blendFactorToMetal[blend.srcColor];
			att.destinationRGBBlendFactor = blendFactorToMetal[blend.dstColor];
			att.sourceAlphaBlendFactor = blendFactorToMetal[blend.srcAlpha];
			att.destinationAlphaBlendFactor = blendFactorToMetal[blend.dstAlpha];
		}
	}

	// The shader functions compile on first use (around 100ms each when not in Metal's cache), and then the
	// pipeline, so all of it goes to a compile thread. Draws record the pipeline state, and the render thread
	// waits for it if it gets there first.
	id<MTLDevice> device = rm_->Device();
	MetalPipelineState *state = pipeline->state;
	MetalShaderFunction *vsFunction = vs->function;
	MetalShaderFunction *fsFunction = fs->function;
	std::string description = vs->desc + " / " + fs->desc;
	RunOnCompileThread([=]() {
		desc.vertexFunction = [vsFunction function];
		desc.fragmentFunction = [fsFunction function];
		id<MTLRenderPipelineState> pso;
		if (desc.vertexFunction && desc.fragmentFunction) {
			NSError *error = nil;
			pso = [device newRenderPipelineStateWithDescriptor:desc error:&error];
			if (!pso) {
				ERROR_LOG(Log::G3D, "Failed to create pipeline (%s): %s", description.c_str(), error ? [[error localizedDescription] UTF8String] : "?");
			}
		}
		[state setPipeline:pso];
	});
	return pipeline;
}

struct StoredMetalPipeline {
	MetalPipelineKey key;
	uint32_t color;
	uint32_t depth;
	uint32_t stencil;
	uint32_t sampleCount;
};

uint32_t DrawEngineMetal::PipelineRecordSize() {
	return sizeof(StoredMetalPipeline);
}

bool DrawEngineMetal::SavePipelines(FILE *f) {
	bool ok = true;
	pipelines_.Iterate([&](const MetalPipelineKey &key, Pipeline *pipeline) {
		StoredMetalPipeline stored{};
		stored.key = key;
		stored.color = (uint32_t)pipeline->format.color;
		stored.depth = (uint32_t)pipeline->format.depth;
		stored.stencil = (uint32_t)pipeline->format.stencil;
		stored.sampleCount = (uint32_t)pipeline->format.sampleCount;
		ok = ok && fwrite(&stored, sizeof(stored), 1, f) == 1;
	});
	return ok;
}

bool DrawEngineMetal::LoadPipelines(FILE *f, int count, bool waitForCompiles) {
	std::vector<MetalPipelineState *> states;
	bool ok = true;
	for (int i = 0; i < count; i++) {
		StoredMetalPipeline stored;
		if (fread(&stored, sizeof(stored), 1, f) != 1) {
			ok = false;
			break;
		}
		MetalRenderTargetFormat format;
		format.color = (MTLPixelFormat)stored.color;
		format.depth = (MTLPixelFormat)stored.depth;
		format.stencil = (MTLPixelFormat)stored.stencil;
		format.sampleCount = (int)stored.sampleCount;
		DecVtxFormat decFmt;
		const bool useHWTransform = stored.key.vtxFmtId != SOFTWARE_TRANSFORM_VTXFMT;
		if (useHWTransform) {
			decFmt.InitializeFromID(stored.key.vtxFmtId);
		}
		Pipeline *pipeline = GetOrCreatePipeline(stored.key, useHWTransform ? &decFmt : nullptr, format);
		states.push_back(pipeline->state);
	}
	if (waitForCompiles) {
		for (MetalPipelineState *state : states) {
			[state wait];
		}
	}
	return ok;
}

void DrawEngineMetal::ConvertStateToKeys(int prim) {
	bool useBufferedRendering = framebufferManager_->UseBufferedRendering();

	if (gstate_c.IsDirty(DIRTY_BLEND_STATE)) {
		if (gstate.isModeClear()) {
			blendKey_.blendEnable = false;
			blendKey_.eqColor = (uint32_t)BlendEq::ADD;
			blendKey_.eqAlpha = (uint32_t)BlendEq::ADD;
			blendKey_.srcColor = (uint32_t)BlendFactor::ONE;
			blendKey_.srcAlpha = (uint32_t)BlendFactor::ONE;
			blendKey_.dstColor = (uint32_t)BlendFactor::ZERO;
			blendKey_.dstAlpha = (uint32_t)BlendFactor::ZERO;
			blendKey_.colorWriteMask = ClearModeChannelMask();
			dynState_.useBlendColor = false;
		} else {
			pipelineState_.Convert(draw_->GetShaderLanguageDesc().bitwiseOps, gstate_c.Use(GPU_USE_SHADER_BLENDING));
			GenericMaskState &maskState = pipelineState_.maskState;
			GenericBlendState &blendState = pipelineState_.blendState;

			if (pipelineState_.FramebufferRead() && useBufferedRendering) {
				ApplyFramebufferRead(&fboTexBindState_);
				// The shader takes over the responsibility for blending, so recompute.
				ApplyStencilReplaceAndLogicOpIgnoreBlend(blendState.replaceAlphaWithStencil, blendState);
				dirtyRequiresRecheck_ |= DIRTY_FRAGMENTSHADER_STATE;
				gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
			} else if (fboTexBound_) {
				boundSecondary_ = nil;
				fboTexBound_ = false;
				dirtyRequiresRecheck_ |= DIRTY_FRAGMENTSHADER_STATE;
				gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
			}

			if (blendState.blendEnabled) {
				blendKey_.blendEnable = true;
				blendKey_.eqColor = (uint32_t)blendState.eqColor;
				blendKey_.eqAlpha = (uint32_t)blendState.eqAlpha;
				blendKey_.srcColor = (uint32_t)blendState.srcColor;
				blendKey_.srcAlpha = (uint32_t)blendState.srcAlpha;
				blendKey_.dstColor = (uint32_t)blendState.dstColor;
				blendKey_.dstAlpha = (uint32_t)blendState.dstAlpha;
				if (blendState.dirtyShaderBlendFixValues) {
					dirtyRequiresRecheck_ |= DIRTY_SHADERBLEND;
					gstate_c.Dirty(DIRTY_SHADERBLEND);
				}
				dynState_.useBlendColor = blendState.useBlendColor;
				if (blendState.useBlendColor) {
					blendState.blendColorToFloat4(dynState_.blendColor);
				}
			} else {
				blendKey_.blendEnable = false;
				blendKey_.eqColor = (uint32_t)BlendEq::ADD;
				blendKey_.eqAlpha = (uint32_t)BlendEq::ADD;
				blendKey_.srcColor = (uint32_t)BlendFactor::ONE;
				blendKey_.srcAlpha = (uint32_t)BlendFactor::ONE;
				blendKey_.dstColor = (uint32_t)BlendFactor::ZERO;
				blendKey_.dstAlpha = (uint32_t)BlendFactor::ZERO;
				dynState_.useBlendColor = false;
			}
			blendKey_.colorWriteMask = maskState.channelMask;
		}
	}

	if (gstate_c.IsDirty(DIRTY_RASTER_STATE)) {
		bool wantCull = !gstate.isModeClear() && prim != GE_PRIM_RECTANGLES && prim > GE_PRIM_LINE_STRIP && gstate.isCullEnabled();
		dynState_.cull = wantCull ? (gstate.getCullMode() ? MTLCullModeFront : MTLCullModeBack) : MTLCullModeNone;
		if (gstate.isModeClear() || gstate.isModeThrough()) {
			dynState_.depthClamp = false;
		} else if (gstate.getDepthRangeMin() == 0 || gstate.getDepthRangeMax() == 65535) {
			dynState_.depthClamp = gstate.isDepthClipEnabled() && gstate_c.Use(GPU_USE_DEPTH_CLAMP);
		} else {
			dynState_.depthClamp = false;
		}
	}

	if (gstate_c.IsDirty(DIRTY_DEPTHSTENCIL_STATE)) {
		GenericStencilFuncState stencilState;
		ConvertStencilFuncState(stencilState);
		MetalDepthStencilKey &key = depthStencilKey_;
		key.value = 0;

		if (gstate.isModeClear()) {
			key.depthTestEnable = true;
			key.depthCompare = GE_COMP_ALWAYS;
			key.depthWriteEnable = gstate.isClearModeDepthMask();
			if (gstate.isClearModeAlphaMask()) {
				key.stencilTestEnable = true;
				key.stencilCompare = GE_COMP_ALWAYS;
				key.stencilPass = GE_STENCILOP_REPLACE;
				key.stencilFail = GE_STENCILOP_REPLACE;
				key.stencilDepthFail = GE_STENCILOP_REPLACE;
				key.writeMask = stencilState.writeMask;
				key.compareMask = 0xFF;
				// Overridden from software transform for clear rectangles.
				dynState_.stencilRef = 0xFF;
			}
		} else {
			if (!IsDepthTestEffectivelyDisabled()) {
				key.depthTestEnable = true;
				key.depthCompare = gstate.getDepthTestFunction();
				key.depthWriteEnable = gstate.isDepthWriteEnabled();
				UpdateEverUsedEqualDepth(gstate.getDepthTestFunction());
			}
			if (stencilState.enabled) {
				key.stencilTestEnable = true;
				key.stencilCompare = stencilState.testFunc;
				key.stencilPass = stencilState.zPass;
				key.stencilFail = stencilState.sFail;
				key.stencilDepthFail = stencilState.zFail;
				key.writeMask = stencilState.writeMask;
				key.compareMask = stencilState.testMask;
				dynState_.stencilRef = stencilState.testRef;

				// Nasty special case for Spongebob and similar, see StateMappingVulkan.cpp.
				if (SpongebobDepthInverseConditions(stencilState)) {
					blendKey_.blendEnable = true;
					blendKey_.eqAlpha = (uint32_t)BlendEq::ADD;
					blendKey_.eqColor = (uint32_t)BlendEq::ADD;
					blendKey_.srcColor = (uint32_t)BlendFactor::ZERO;
					blendKey_.dstColor = (uint32_t)BlendFactor::ZERO;
					blendKey_.srcAlpha = (uint32_t)BlendFactor::ZERO;
					blendKey_.dstAlpha = (uint32_t)BlendFactor::ZERO;
					blendKey_.colorWriteMask = 8;
					key.depthCompare = GE_COMP_LESS;  // Inverse of GREATER_EQUAL
					key.stencilCompare = GE_COMP_ALWAYS;
					key.stencilPass = GE_STENCILOP_ZERO;
					key.stencilFail = GE_STENCILOP_ZERO;
					key.stencilDepthFail = GE_STENCILOP_KEEP;
					dirtyRequiresRecheck_ |= DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE;
					gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE);
				}
			}
		}
	}

	if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
		ViewportAndScissor vpAndScissor;
		ConvertViewportAndScissor(
			framebufferManager_->GetDisplayLayoutConfigCopy(),
			useBufferedRendering,
			framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(),
			framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(),
			vpAndScissor);
		dynState_.viewport = MTLViewport{ vpAndScissor.viewportX, vpAndScissor.viewportY, vpAndScissor.viewportW, vpAndScissor.viewportH, 0.0, 1.0 };
		dynState_.scissor[0] = vpAndScissor.scissorX;
		dynState_.scissor[1] = vpAndScissor.scissorY;
		dynState_.scissor[2] = std::max(0, vpAndScissor.scissorW);
		dynState_.scissor[3] = std::max(0, vpAndScissor.scissorH);
	}
}

void DrawEngineMetal::BindShaderBlendTex() {
	if (!gstate.isModeClear() && fboTexBindState_ == FBO_TEX_COPY_BIND_TEX) {
		VirtualFramebuffer *curRenderVfb = framebufferManager_->GetCurrentRenderVFB();
		bool bindResult = framebufferManager_->BindFramebufferAsColorTexture(1, curRenderVfb, BINDFBCOLOR_MAY_COPY | BINDFBCOLOR_UNCACHED, Draw::ALL_LAYERS);
		_dbg_assert_(bindResult);
		boundSecondary_ = (__bridge id<MTLTexture>)(void *)draw_->GetNativeObject(Draw::NativeObject::BOUND_TEXTURE1_IMAGEVIEW);
		fboTexBound_ = true;
		fboTexBindState_ = FBO_TEX_NONE;
		// Must dirty blend state here so we re-copy next time.  Example: Lunar's spell effects.
		dirtyRequiresRecheck_ |= DIRTY_BLEND_STATE;
	} else if (gstate.isModeClear() || !fboTexBound_) {
		boundSecondary_ = nil;
	}
}

bool DrawEngineMetal::ApplyState(const Pipeline *pipeline, bool overrideStencilRef, uint8_t stencilRef) {
	if (!rm_->EnsureRenderEncoder()) {
		return false;
	}
	rm_->SetPipeline(pipeline->state);
	rm_->SetDepthStencilState(GetDepthStencilState(depthStencilKey_), depthStencilKey_.depthTestEnable || depthStencilKey_.stencilTestEnable);
	if (depthStencilKey_.stencilTestEnable) {
		rm_->SetStencilRef(overrideStencilRef ? stencilRef : dynState_.stencilRef);
	}
	if (dynState_.useBlendColor) {
		rm_->SetBlendColor(dynState_.blendColor[0], dynState_.blendColor[1], dynState_.blendColor[2], dynState_.blendColor[3]);
	}
	rm_->SetCullMode(dynState_.cull);
	rm_->SetFrontFacing(MTLWindingCounterClockwise);
	rm_->SetDepthClip(dynState_.depthClamp ? MTLDepthClipModeClamp : MTLDepthClipModeClip);
	rm_->SetViewport(dynState_.viewport);
	return rm_->SetScissor(dynState_.scissor[0], dynState_.scissor[1], dynState_.scissor[2], dynState_.scissor[3]);
}

static inline void BindTexture(MetalRenderManager *rm, __unsafe_unretained id<MTLTexture> tex, __unsafe_unretained id<MTLSamplerState> sampler, int index, uint32_t arrayMask) {
	if (arrayMask & (1 << index)) {
		tex = rm->ArrayView(tex);
	}
	rm->SetFragmentTexture(tex, index);
	rm->SetFragmentSampler(sampler, index);
}

void DrawEngineMetal::BindResources(const Pipeline *pipeline) {
	uint64_t dirty = shaderManager_->UpdateUniforms(framebufferManager_->UseBufferedRendering(), false);
	if ((dirty & DIRTY_BASE_UNIFORMS) || !baseUBO_.buffer || uboFrame_ != rm_->FrameNumber()) {
		baseUBO_ = rm_->Push().Push(&shaderManager_->BaseUniforms(), sizeof(UB_VS_FS_Base), 256);
	}
	if ((dirty & DIRTY_LIGHT_UNIFORMS) || !lightUBO_.buffer || uboFrame_ != rm_->FrameNumber()) {
		lightUBO_ = rm_->Push().Push(&shaderManager_->LightUniforms(), sizeof(UB_VS_Lights), 256);
	}
	uboFrame_ = rm_->FrameNumber();
	rm_->SetVertexBuffer(baseUBO_.buffer, baseUBO_.offset, METAL_DRAW_BINDING_DYNUBO_BASE);
	rm_->SetFragmentBuffer(baseUBO_.buffer, baseUBO_.offset, METAL_DRAW_BINDING_DYNUBO_BASE);
	rm_->SetVertexBuffer(lightUBO_.buffer, lightUBO_.offset, METAL_DRAW_BINDING_DYNUBO_LIGHT);

	// Only the slots the fragment shader actually uses.
	const uint32_t used = pipeline->fragmentTextureMask;
	const uint32_t arrayMask = pipeline->fragmentArrayTextureMask;
	if (used & (1 << METAL_DRAW_BINDING_TEXTURE)) {
		__unsafe_unretained id<MTLTexture> tex = textureCache_->curTexture;
		__unsafe_unretained id<MTLSamplerState> sampler = textureCache_->curSampler;
		BindTexture(rm_, tex ? tex : nullTexture_, sampler ? sampler : samplerNearest_, METAL_DRAW_BINDING_TEXTURE, arrayMask);
	}
	if (used & (1 << METAL_DRAW_BINDING_2ND_TEXTURE)) {
		BindTexture(rm_, boundSecondary_ ? boundSecondary_ : nullTexture_, samplerNearest_, METAL_DRAW_BINDING_2ND_TEXTURE, arrayMask);
	}
	if (used & (1 << METAL_DRAW_BINDING_DEPAL_TEXTURE)) {
		BindTexture(rm_, boundDepal_ ? boundDepal_ : nullTexture_, boundDepalSmoothed_ ? samplerLinear_ : samplerNearest_, METAL_DRAW_BINDING_DEPAL_TEXTURE, arrayMask);
	}
}

void DrawEngineMetal::DrawPrimitives(MTLPrimitiveType prim, __unsafe_unretained id<MTLBuffer> vbuf, uint32_t vbOffset, int count, const uint16_t *inds) {
	rm_->SetVertexBuffer(vbuf, vbOffset, METAL_VERTEX_BUFFER_INDEX);
	if (inds) {
		MetalPushAlloc ialloc = rm_->Push().Push(inds, count * sizeof(uint16_t), 4);
		rm_->DrawIndexed(prim, count, ialloc.buffer, ialloc.offset);
	} else {
		rm_->Draw(prim, 0, count);
	}
}

void DrawEngineMetal::ResetAfterSkippedDraw() {
	indexGen.Reset();
	numDecodedVerts_ = 0;
	numDrawVerts_ = 0;
	numDrawInds_ = 0;
	vertexCountInDrawCalls_ = 0;
	expandedVertsInDrawCalls_ = 0;
	numVertsToDecode_ = 0;
	decodeIndsCounter_ = 0;
	decodeVertsCounter_ = 0;
	gstate_c.vertexFullAlpha = true;
}

void DrawEngineMetal::Flush() {
	if (!numDrawVerts_) {
		return;
	}
	PROFILE_THIS_SCOPE("Flush");

	GEPrimitiveType prim = prevPrim_;

	// Metal's provoking vertex is the first one, so flat shading goes through software transform.
	bool provokingVertexOk = gstate.getShadeMode() != GE_SHADE_FLAT;
	bool useHWTransform = CanUseHardwareTransform(prim) && provokingVertexOk;
	if (clipInfoFlags_ & ClipInfoFlags::Valid) {
		if (clipInfoFlags_ & ClipInfoFlags::SoftClipCull) {
			useHWTransform = false;
		}
	}
	if (clipInfoFlags_ != lastClipInfoFlags_) {
		ClipInfoFlags changed = (ClipInfoFlags)((u32)clipInfoFlags_ ^ (u32)lastClipInfoFlags_);
		if (changed & (ClipInfoFlags::DepthClampFragment | ClipInfoFlags::MinMaxZDiscard)) {
			gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_RASTER_STATE);
		}
		if (changed & ClipInfoFlags::FlatZ) {
			gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
		}
		lastClipInfoFlags_ = clipInfoFlags_;
	}
	if (useHWTransform != lastUseHwTransform_) {
		gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_RASTER_STATE);
		lastUseHwTransform_ = useHWTransform;
	}

	auto updateVertexFullAlpha = [&]() {
		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}
	};

	auto updatePipeline = [&](bool hwTransform) -> Pipeline * {
		if (!lastPipeline_ || gstate_c.IsDirty(DIRTY_BLEND_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE) || prim != lastPrim_) {
			if (prim != lastPrim_ || gstate_c.IsDirty(DIRTY_BLEND_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE)) {
				ConvertStateToKeys(prim);
			}
			VShaderID vsid;
			FShaderID fsid;
			shaderManager_->GetShaderIDs(prim, dec_->VertexType(), &vsid, &fsid, pipelineState_, hwTransform, clipInfoFlags_);
			// This might copy the framebuffer, which interrupts the render pass. So it goes before the pipeline lookup,
			// which depends on the render target.
			BindShaderBlendTex();
			lastPipeline_ = GetPipeline(vsid, fsid, hwTransform);
			gstate_c.Clean(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE);
			gstate_c.Dirty(dirtyRequiresRecheck_);
			dirtyRequiresRecheck_ = 0;
		}
		lastPrim_ = prim;
		return lastPipeline_;
	};

	if (useHWTransform) {
		MetalPushAlloc valloc;
		if (lastVType_ & GE_VTYPE_WEIGHT_MASK) {
			// If skinning, we're predecoding into "decoded". So make sure we're done, then push that content.
			DecodeVerts(dec_, decoded_);
			valloc = rm_->Push().Push(decoded_, numDecodedVerts_ * dec_->GetDecVtxFmt().stride, 16);
		} else {
			int vertsToDecode = ComputeNumVertsToDecode();
			valloc = rm_->Push().Allocate(vertsToDecode * dec_->GetDecVtxFmt().stride, 16);
			DecodeVerts(dec_, valloc.ptr);
		}

		int vertexCount;
		int maxIndex;
		bool useElements;
		DecodeIndsAndGetData(&prim, &vertexCount, &maxIndex, &useElements, false);
		gpuStats.perFrame.numVertsDrawn += vertexCount;
		updateVertexFullAlpha();

		if (gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS) && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
			gstate_c.Clean(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
			const TextureApplyResult textureResult = textureCache_->ApplyTexture(true);
			textureCache_->ApplySampler(textureResult, clipInfoFlags_ & ClipInfoFlags::FlatZ, false);
		} else if (gstate.getTextureAddress(0) == (gstate.getFrameBufRawAddress() | 0x04000000)) {
			// This catches the case of clearing a texture.
			gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		}

		Pipeline *pipeline = updatePipeline(true);
		if (!pipeline || !ApplyState(pipeline, false, 0)) {
			ResetAfterSkippedDraw();
			return;
		}
		BindResources(pipeline);
		MTLPrimitiveType mtlPrim = prim == GE_PRIM_TRIANGLE_STRIP ? MTLPrimitiveTypeTriangleStrip : MTLPrimitiveTypeTriangle;
		DrawPrimitives(mtlPrim, valloc.buffer, valloc.offset, vertexCount, useElements ? decIndex_ : nullptr);
		if (useDepthRaster_) {
			DepthRasterSubmitRaw(prim, dec_, dec_->VertexType(), vertexCount);
		}
	} else {
		gpuStats.perFrame.numSoftTransformedDraws++;
		PROFILE_THIS_SCOPE("soft");

		DecodeVerts(dec_, decoded_);
		int vertexCount = DecodeInds();
		updateVertexFullAlpha();

		gpuStats.perFrame.numVertsDrawn += vertexCount;
		prim = IndexGenerator::GeneralPrim((GEPrimitiveType)drawInds_[0].prim);

		if (useDepthRaster_) {
			DepthRasterPredecoded(prim, decoded_, numDecodedVerts_, dec_, vertexCount);
		}

		u16 *inds = decIndex_;
		if (gstate.getShadeMode() == GE_SHADE_FLAT) {
			// Rotate the index buffer to simulate a different provoking vertex.
			IndexBufferProvokingLastToFirst(prim, inds, vertexCount);
		}

		bool textureNeedsApply = false;
		TextureApplyResult textureResult;
		if (gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS) && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
			gstate_c.Clean(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
			gstate_c.dstSquared = false;
			textureResult = textureCache_->ApplyTexture(true);
			// NOTE: After this is set, we MUST call ApplySampler before returning.
			textureNeedsApply = true;
		} else if (gstate.getTextureAddress(0) == (gstate.getFrameBufRawAddress() | 0x04000000)) {
			gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
		}

		SoftwareTransformResult result{};
		SoftwareTransformParams params{};
		params.decoded = decoded_;
		params.transformed = transformed_;
		params.transformedExpanded = transformedExpanded_;
		// Clears through the render pass don't respect the scissor, so only allow them when buffered.
		params.allowClear = framebufferManager_->UseBufferedRendering();
		params.allowSeparateAlphaClear = false;
		params.everUsedEqualDepth = everUsedEqualDepth_;
		params.clipInfoFlags = clipInfoFlags_;
		params.trianglesReversed = BatchTrianglesReversed();

		const SoftwareTransformAction action = RunSoftwareTransform(params, prim, dec_->VertexType(), dec_->GetDecVtxFmt(), numDecodedVerts_, VERTEX_BUFFER_MAX, vertexCount, inds, RemainingIndices(inds), &result);
		if (textureNeedsApply) {
			textureCache_->ApplySampler(textureResult, clipInfoFlags_ & ClipInfoFlags::FlatZ, result.pixelMapped);
			if (gstate_c.dstSquared) {
				gstate_c.Dirty(DIRTY_BLEND_STATE);
			}
		}
		if (action == SW_DRAW_INDEXED) {
			Pipeline *pipeline = updatePipeline(false);
			if (!pipeline || !ApplyState(pipeline, result.setStencil, result.stencilValue)) {
				ResetAfterSkippedDraw();
				return;
			}
			// UpdateUniforms in BindResources would otherwise use pixelMapped = false.
			uint64_t dirty = shaderManager_->UpdateUniforms(framebufferManager_->UseBufferedRendering(), result.pixelMapped);
			if (dirty & DIRTY_BASE_UNIFORMS) {
				baseUBO_ = MetalPushAlloc{};
			}
			if (dirty & DIRTY_LIGHT_UNIFORMS) {
				lightUBO_ = MetalPushAlloc{};
			}
			BindResources(pipeline);
			MetalPushAlloc valloc = rm_->Push().Push(result.drawBuffer, result.drawVertexCount * sizeof(TransformedVertex), 16);
			DrawPrimitives(MTLPrimitiveTypeTriangle, valloc.buffer, valloc.offset, result.drawIndexCount, inds);
		} else if (action == SW_CLEAR) {
			// Note: we won't get here if the clear is alpha but not color, or color but not alpha.
			bool clearColor = gstate.isClearModeColorMask();
			bool clearAlpha = gstate.isClearModeAlphaMask();  // and stencil
			bool clearDepth = gstate.isClearModeDepthMask();
			Draw::Aspect mask = Draw::Aspect::NO_BIT;
			if (clearColor || clearAlpha) mask |= Draw::Aspect::COLOR_BIT;
			if (clearDepth) mask |= Draw::Aspect::DEPTH_BIT;
			if (clearAlpha) mask |= Draw::Aspect::STENCIL_BIT;
			// The alpha channel and the stencil channel are shared on the PSP.
			draw_->Clear(mask, result.color, result.depth, result.color >> 24);
			if (gstate_c.Use(GPU_USE_CLEAR_RAM_HACK) && gstate.isClearModeColorMask() && (gstate.isClearModeAlphaMask() || gstate.FrameBufFormat() == GE_FORMAT_565)) {
				int scissorX1 = gstate.getScissorX1();
				int scissorY1 = gstate.getScissorY1();
				int scissorX2 = gstate.getScissorX2() + 1;
				int scissorY2 = gstate.getScissorY2() + 1;
				framebufferManager_->ApplyClearToMemory(scissorX1, scissorY1, scissorX2, scissorY2, result.color);
			}
		}
	}

	ResetAfterDrawInline();
	framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);
	gpuCommon_->NotifyFlush();
}
