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

#include <algorithm>

#include "Common/LogReporting.h"
#include "Common/MemoryUtil.h"
#include "Common/TimeUtil.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"

#include "Common/GPU/OpenGL/GLDebugLog.h"
#include "Common/Profiler/Profiler.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/GLES/FragmentTestCacheGLES.h"
#include "GPU/GLES/StateMappingGLES.h"
#include "GPU/GLES/TextureCacheGLES.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/GLES/GPU_GLES.h"

static const GLuint glprim[8] = {
	// Points, which are expanded to triangles.
	GL_TRIANGLES,
	// Lines and line strips, which are also expanded to triangles.
	GL_TRIANGLES,
	GL_TRIANGLES,
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_TRIANGLE_FAN,
	// Rectangles, which are expanded to triangles.
	GL_TRIANGLES,
};

enum {
	TRANSFORMED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * sizeof(TransformedVertex)
};

DrawEngineGLES::DrawEngineGLES(Draw::DrawContext *draw) : inputLayoutMap_(16), draw_(draw) {
	render_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	decOptions_.expandAllWeightsToFloat = false;
	decOptions_.expand8BitNormalsToFloat = false;

	indexGen.Setup(decIndex_);

	InitDeviceObjects();

	tessDataTransferGLES = new TessellationDataTransferGLES(render_);
	tessDataTransfer = tessDataTransferGLES;
}

DrawEngineGLES::~DrawEngineGLES() {
	DestroyDeviceObjects();

	delete tessDataTransferGLES;
}

void DrawEngineGLES::DeviceLost() {
	DestroyDeviceObjects();
	draw_ = nullptr;
	render_ = nullptr;
}

void DrawEngineGLES::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	render_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	InitDeviceObjects();
}

void DrawEngineGLES::InitDeviceObjects() {
	_assert_msg_(render_ != nullptr, "Render manager must be set");

	for (int i = 0; i < GLRenderManager::MAX_INFLIGHT_FRAMES; i++) {
		frameData_[i].pushVertex = render_->CreatePushBuffer(i, GL_ARRAY_BUFFER, 2048 * 1024, "game_vertex");
		frameData_[i].pushIndex = render_->CreatePushBuffer(i, GL_ELEMENT_ARRAY_BUFFER, 256 * 1024, "game_index");
	}

	int stride = sizeof(TransformedVertex);
	std::vector<GLRInputLayout::Entry> entries;
	entries.push_back({ ATTR_POSITION, 4, GL_FLOAT, GL_FALSE, offsetof(TransformedVertex, x) });
	entries.push_back({ ATTR_TEXCOORD, 3, GL_FLOAT, GL_FALSE, offsetof(TransformedVertex, u) });
	entries.push_back({ ATTR_COLOR0, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(TransformedVertex, color0) });
	entries.push_back({ ATTR_COLOR1, 3, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(TransformedVertex, color1) });
	entries.push_back({ ATTR_NORMAL, 1, GL_FLOAT, GL_FALSE, offsetof(TransformedVertex, fog) });
	softwareInputLayout_ = render_->CreateInputLayout(entries, stride);

	draw_->SetInvalidationCallback(std::bind(&DrawEngineGLES::Invalidate, this, std::placeholders::_1));
}

void DrawEngineGLES::DestroyDeviceObjects() {
	if (!draw_) {
		return;
	}
	draw_->SetInvalidationCallback(InvalidationCallback());

	// Beware: this could be called twice in a row, sometimes.
	for (int i = 0; i < GLRenderManager::MAX_INFLIGHT_FRAMES; i++) {
		if (!frameData_[i].pushVertex && !frameData_[i].pushIndex)
			continue;

		if (frameData_[i].pushVertex)
			render_->DeletePushBuffer(frameData_[i].pushVertex);
		if (frameData_[i].pushIndex)
			render_->DeletePushBuffer(frameData_[i].pushIndex);
		frameData_[i].pushVertex = nullptr;
		frameData_[i].pushIndex = nullptr;
	}

	ClearTrackedVertexArrays();

	if (softwareInputLayout_)
		render_->DeleteInputLayout(softwareInputLayout_);
	softwareInputLayout_ = nullptr;

	ClearInputLayoutMap();
}

void DrawEngineGLES::ClearInputLayoutMap() {
	inputLayoutMap_.Iterate([&](const uint32_t &key, GLRInputLayout *il) {
		render_->DeleteInputLayout(il);
	});
	inputLayoutMap_.Clear();
}

void DrawEngineGLES::BeginFrame() {
	FrameData &frameData = frameData_[render_->GetCurFrame()];
	frameData.pushIndex->Begin();
	frameData.pushVertex->Begin();

	lastRenderStepId_ = -1;
}

void DrawEngineGLES::EndFrame() {
	FrameData &frameData = frameData_[render_->GetCurFrame()];
	frameData.pushIndex->End();
	frameData.pushVertex->End();
	tessDataTransferGLES->EndFrame();
}

struct GlTypeInfo {
	u16 type;
	u8 count;
	u8 normalized;
};

static const GlTypeInfo GLComp[] = {
	{0}, // 	DEC_NONE,
	{GL_FLOAT, 1, GL_FALSE}, // 	DEC_FLOAT_1,
	{GL_FLOAT, 2, GL_FALSE}, // 	DEC_FLOAT_2,
	{GL_FLOAT, 3, GL_FALSE}, // 	DEC_FLOAT_3,
	{GL_FLOAT, 4, GL_FALSE}, // 	DEC_FLOAT_4,
	{GL_BYTE, 4, GL_TRUE}, // 	DEC_S8_3,
	{GL_SHORT, 4, GL_TRUE},// 	DEC_S16_3,
	{GL_UNSIGNED_BYTE, 1, GL_TRUE},// 	DEC_U8_1,
	{GL_UNSIGNED_BYTE, 2, GL_TRUE},// 	DEC_U8_2,
	{GL_UNSIGNED_BYTE, 3, GL_TRUE},// 	DEC_U8_3,
	{GL_UNSIGNED_BYTE, 4, GL_TRUE},// 	DEC_U8_4,
	{GL_UNSIGNED_SHORT, 1, GL_TRUE},// 	DEC_U16_1,
	{GL_UNSIGNED_SHORT, 2, GL_TRUE},// 	DEC_U16_2,
	{GL_UNSIGNED_SHORT, 3, GL_TRUE},// 	DEC_U16_3,
	{GL_UNSIGNED_SHORT, 4, GL_TRUE},// 	DEC_U16_4,
};

static inline void VertexAttribSetup(int attrib, int fmt, int offset, std::vector<GLRInputLayout::Entry> &entries) {
	if (fmt) {
		const GlTypeInfo &type = GLComp[fmt];
		GLRInputLayout::Entry entry;
		entry.offset = offset;
		entry.location = attrib;
		entry.normalized = type.normalized;
		entry.type = type.type;
		entry.count = type.count;
		entries.push_back(entry);
	}
}

// TODO: Use VBO and get rid of the vertexData pointers - with that, we will supply only offsets
GLRInputLayout *DrawEngineGLES::SetupDecFmtForDraw(const DecVtxFormat &decFmt) {
	uint32_t key = decFmt.id;
	GLRInputLayout *inputLayout;
	if (inputLayoutMap_.Get(key, &inputLayout)) {
		return inputLayout;
	}

	std::vector<GLRInputLayout::Entry> entries;
	VertexAttribSetup(ATTR_W1, decFmt.w0fmt, decFmt.w0off, entries);
	VertexAttribSetup(ATTR_W2, decFmt.w1fmt, decFmt.w1off, entries);
	VertexAttribSetup(ATTR_TEXCOORD, decFmt.uvfmt, decFmt.uvoff, entries);
	VertexAttribSetup(ATTR_COLOR0, decFmt.c0fmt, decFmt.c0off, entries);
	VertexAttribSetup(ATTR_COLOR1, decFmt.c1fmt, decFmt.c1off, entries);
	VertexAttribSetup(ATTR_NORMAL, decFmt.nrmfmt, decFmt.nrmoff, entries);
	VertexAttribSetup(ATTR_POSITION, DecVtxFormat::PosFmt(), decFmt.posoff, entries);

	int stride = decFmt.stride;
	inputLayout = render_->CreateInputLayout(entries, stride);
	inputLayoutMap_.Insert(key, inputLayout);
	return inputLayout;
}

// A new render step means we need to flush any dynamic state. Really, any state that is reset in
// GLQueueRunner::PerformRenderPass.
void DrawEngineGLES::Invalidate(InvalidationCallbackFlags flags) {
	if (flags & InvalidationCallbackFlags::RENDER_PASS_STATE) {
		// Dirty everything that has dynamic state that will need re-recording.
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_BLEND_STATE | DIRTY_RASTER_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	}
}

void DrawEngineGLES::DoFlush() {
	PROFILE_THIS_SCOPE("flush");
	FrameData &frameData = frameData_[render_->GetCurFrame()];
	VShaderID vsid;

	if (!render_->IsInRenderPass()) {
		// Something went badly wrong. Try to survive by simply skipping the draw, though.
		_dbg_assert_msg_(false, "Trying to DoFlush while not in a render pass. This is bad.");
		// can't goto bail here, skips too many variable initializations. So let's wipe the most important stuff.
		indexGen.Reset();
		numDecodedVerts_ = 0;
		numDrawVerts_ = 0;
		numDrawInds_ = 0;
		vertexCountInDrawCalls_ = 0;
		decodeVertsCounter_ = 0;
		decodeIndsCounter_ = 0;
		return;
	}

	bool textureNeedsApply = false;
	if (gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS) && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.Clean(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
		textureNeedsApply = true;
	} else if (gstate.getTextureAddress(0) == (gstate.getFrameBufRawAddress() | 0x04000000)) {
		// This catches the case of clearing a texture. (#10957)
		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
	}

	GEPrimitiveType prim = prevPrim_;

	Shader *vshader = shaderManager_->ApplyVertexShader(CanUseHardwareTransform(prim), useHWTessellation_, dec_, decOptions_.expandAllWeightsToFloat, decOptions_.applySkinInDecode || !CanUseHardwareTransform(prim), &vsid);

	GLRBuffer *vertexBuffer = nullptr;
	GLRBuffer *indexBuffer = nullptr;
	uint32_t vertexBufferOffset = 0;
	uint32_t indexBufferOffset = 0;

	if (vshader->UseHWTransform()) {
		if (decOptions_.applySkinInDecode && (lastVType_ & GE_VTYPE_WEIGHT_MASK)) {
			// If software skinning, we're predecoding into "decoded". So make sure we're done, then push that content.
			DecodeVerts(decoded_);
			uint32_t size = numDecodedVerts_ * dec_->GetDecVtxFmt().stride;
			u8 *dest = (u8 *)frameData.pushVertex->Allocate(size, 4, &vertexBuffer, &vertexBufferOffset);
			memcpy(dest, decoded_, size);
		} else {
			// Figure out how much pushbuffer space we need to allocate.
			int vertsToDecode = ComputeNumVertsToDecode();
			u8 *dest = (u8 *)frameData.pushVertex->Allocate(vertsToDecode * dec_->GetDecVtxFmt().stride, 4, &vertexBuffer, &vertexBufferOffset);
			DecodeVerts(dest);
		}

		int vertexCount;
		int maxIndex;
		bool useElements;
		DecodeVerts(decoded_);
		DecodeIndsAndGetData(&prim, &vertexCount, &maxIndex, &useElements, false);

		if (useElements) {
			uint32_t esz = sizeof(uint16_t) * vertexCount;
			void *dest = frameData.pushIndex->Allocate(esz, 2, &indexBuffer, &indexBufferOffset);
			// TODO: When we need to apply an index offset, we can apply it directly when copying the indices here.
			// Of course, minding the maximum value of 65535...
			memcpy(dest, decIndex_, esz);
		}

		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		if (textureNeedsApply) {
			textureCache_->ApplyTexture();
		}

		// Need to ApplyDrawState after ApplyTexture because depal can launch a render pass and that wrecks the state.
		ApplyDrawState(prim);
		ApplyDrawStateLate(false, 0);
		
		LinkedShader *program = shaderManager_->ApplyFragmentShader(vsid, vshader, pipelineState_, framebufferManager_->UseBufferedRendering());
		GLRInputLayout *inputLayout = SetupDecFmtForDraw(dec_->GetDecVtxFmt());
		if (useElements) {
			render_->DrawIndexed(inputLayout,
				vertexBuffer, vertexBufferOffset,
				indexBuffer, indexBufferOffset,
				glprim[prim], vertexCount, GL_UNSIGNED_SHORT);
		} else {
			render_->Draw(
				inputLayout, vertexBuffer, vertexBufferOffset,
				glprim[prim], 0, vertexCount);
		}
	} else {
		PROFILE_THIS_SCOPE("soft");
		if (!decOptions_.applySkinInDecode) {
			decOptions_.applySkinInDecode = true;
			lastVType_ |= (1 << 26);
			dec_ = GetVertexDecoder(lastVType_);
		}
		DecodeVerts(decoded_);
		int vertexCount = DecodeInds();

		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		gpuStats.numUncachedVertsDrawn += vertexCount;
		prim = IndexGenerator::GeneralPrim((GEPrimitiveType)drawInds_[0].prim);

		u16 *inds = decIndex_;
		SoftwareTransformResult result{};
		// TODO: Keep this static?  Faster than repopulating?
		SoftwareTransformParams params{};
		params.decoded = decoded_;
		params.transformed = transformed_;
		params.transformedExpanded = transformedExpanded_;
		params.fbman = framebufferManager_;
		params.texCache = textureCache_;
		params.allowClear = true;  // Clear in OpenGL respects scissor rects, so we'll use it.
		params.allowSeparateAlphaClear = true;
		params.flippedY = framebufferManager_->UseBufferedRendering();
		params.usesHalfZ = false;

		// We need correct viewport values in gstate_c already.
		if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
			ConvertViewportAndScissor(framebufferManager_->UseBufferedRendering(),
				framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(),
				framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(),
				vpAndScissor_);
			UpdateCachedViewportState(vpAndScissor_);
		}

		int maxIndex = numDecodedVerts_;

		// TODO: Split up into multiple draw calls for GLES 2.0 where you can't guarantee support for more than 0x10000 verts.
		if (gl_extensions.IsGLES && !gl_extensions.GLES3) {
			constexpr int vertexCountLimit = 0x10000 / 3;
			if (vertexCount > vertexCountLimit) {
				WARN_LOG_REPORT_ONCE(manyVerts, Log::G3D, "Truncating vertex count from %d to %d", vertexCount, vertexCountLimit);
				vertexCount = vertexCountLimit;
			}
		}

		SoftwareTransform swTransform(params);

		const Lin::Vec3 trans(gstate_c.vpXOffset, gstate_c.vpYOffset, gstate_c.vpZOffset);
		const Lin::Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale);
		const bool invertedY = gstate_c.vpHeight * (params.flippedY ? 1.0 : -1.0f) < 0;
		swTransform.SetProjMatrix(gstate.projMatrix, gstate_c.vpWidth < 0, invertedY, trans, scale);

		swTransform.Transform(prim, dec_->VertexType(), dec_->GetDecVtxFmt(), numDecodedVerts_, &result);
		// Non-zero depth clears are unusual, but some drivers don't match drawn depth values to cleared values.
		// Games sometimes expect exact matches (see #12626, for example) for equal comparisons.
		if (result.action == SW_CLEAR && everUsedEqualDepth_ && gstate.isClearModeDepthMask() && result.depth > 0.0f && result.depth < 1.0f)
			result.action = SW_NOT_READY;

		if (textureNeedsApply) {
			gstate_c.pixelMapped = result.pixelMapped;
			textureCache_->ApplyTexture();
			gstate_c.pixelMapped = false;
		}

		// Need to ApplyDrawState after ApplyTexture because depal can launch a render pass and that wrecks the state.
		ApplyDrawState(prim);

		if (result.action == SW_NOT_READY)
			swTransform.BuildDrawingParams(prim, vertexCount, dec_->VertexType(), inds, RemainingIndices(inds), numDecodedVerts_, VERTEX_BUFFER_MAX, &result);
		if (result.setSafeSize)
			framebufferManager_->SetSafeSize(result.safeWidth, result.safeHeight);

		ApplyDrawStateLate(result.setStencil, result.stencilValue);

		LinkedShader *linked = shaderManager_->ApplyFragmentShader(vsid, vshader, pipelineState_, framebufferManager_->UseBufferedRendering());
		if (!linked) {
			// Not much we can do here. Let's skip drawing.
			goto bail;
		}

		if (result.action == SW_DRAW_INDEXED) {
			vertexBufferOffset = (uint32_t)frameData.pushVertex->Push(result.drawBuffer, numDecodedVerts_ * sizeof(TransformedVertex), 4, &vertexBuffer);
			indexBufferOffset = (uint32_t)frameData.pushIndex->Push(inds, sizeof(uint16_t) * result.drawNumTrans, 2, &indexBuffer);
			render_->DrawIndexed(
				softwareInputLayout_, vertexBuffer, vertexBufferOffset, indexBuffer, indexBufferOffset,
				glprim[prim], result.drawNumTrans, GL_UNSIGNED_SHORT);
		} else if (result.action == SW_CLEAR) {
			u32 clearColor = result.color;
			float clearDepth = result.depth;

			bool colorMask = gstate.isClearModeColorMask();
			bool alphaMask = gstate.isClearModeAlphaMask();
			bool depthMask = gstate.isClearModeDepthMask();

			GLbitfield target = 0;
			// Without this, we will clear RGB when clearing stencil, which breaks games.
			uint8_t rgbaMask = (colorMask ? 7 : 0) | (alphaMask ? 8 : 0);
			if (colorMask || alphaMask) target |= GL_COLOR_BUFFER_BIT;
			if (alphaMask) target |= GL_STENCIL_BUFFER_BIT;
			if (depthMask) target |= GL_DEPTH_BUFFER_BIT;

			render_->Clear(clearColor, clearDepth, clearColor >> 24, target, rgbaMask, vpAndScissor_.scissorX, vpAndScissor_.scissorY, vpAndScissor_.scissorW, vpAndScissor_.scissorH);
			framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);

			if (gstate_c.Use(GPU_USE_CLEAR_RAM_HACK) && colorMask && (alphaMask || gstate_c.framebufFormat == GE_FORMAT_565)) {
				int scissorX1 = gstate.getScissorX1();
				int scissorY1 = gstate.getScissorY1();
				int scissorX2 = gstate.getScissorX2() + 1;
				int scissorY2 = gstate.getScissorY2() + 1;
				framebufferManager_->ApplyClearToMemory(scissorX1, scissorY1, scissorX2, scissorY2, clearColor);
			}
			gstate_c.Dirty(DIRTY_BLEND_STATE);  // Make sure the color mask gets re-applied.
		}
		decOptions_.applySkinInDecode = g_Config.bSoftwareSkinning;
	}

bail:
	ResetAfterDrawInline();
	framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);
	GPUDebug::NotifyDraw();
}

// TODO: Refactor this to a single USE flag.
bool DrawEngineGLES::SupportsHWTessellation() {
	bool hasTexelFetch = gl_extensions.GLES3 || (!gl_extensions.IsGLES && gl_extensions.VersionGEThan(3, 3, 0)) || gl_extensions.EXT_gpu_shader4;
	return hasTexelFetch && gstate_c.UseAll(GPU_USE_VERTEX_TEXTURE_FETCH | GPU_USE_TEXTURE_FLOAT | GPU_USE_INSTANCE_RENDERING);
}

bool DrawEngineGLES::UpdateUseHWTessellation(bool enable) const {
	return enable && SupportsHWTessellation();
}

void TessellationDataTransferGLES::SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) {
	bool hasColor = (vertType & GE_VTYPE_COL_MASK) != 0;
	bool hasTexCoord = (vertType & GE_VTYPE_TC_MASK) != 0;

	int size = size_u * size_v;
	float *pos = new float[size * 4];
	float *tex = hasTexCoord ? new float[size * 4] : nullptr;
	float *col = hasColor ? new float[size * 4] : nullptr;
	int stride = 4;

	CopyControlPoints(pos, tex, col, stride, stride, stride, points, size, vertType);
	// Removed the 1D texture support, it's unlikely to be relevant for performance.
	// Control Points
	if (prevSizeU < size_u || prevSizeV < size_v) {
		prevSizeU = size_u;
		prevSizeV = size_v;
		if (data_tex[0])
			renderManager_->DeleteTexture(data_tex[0]);
		data_tex[0] = renderManager_->CreateTexture(GL_TEXTURE_2D, size_u * 3, size_v, 1, 1);
		renderManager_->TextureImage(data_tex[0], 0, size_u * 3, size_v, 1, Draw::DataFormat::R32G32B32A32_FLOAT, nullptr, GLRAllocType::NONE, false);
		renderManager_->FinalizeTexture(data_tex[0], 0, false);
	}
	renderManager_->BindTexture(TEX_SLOT_SPLINE_POINTS, data_tex[0]);
	// Position
	renderManager_->TextureSubImage(TEX_SLOT_SPLINE_POINTS, data_tex[0], 0, 0, 0, size_u, size_v, Draw::DataFormat::R32G32B32A32_FLOAT, (u8 *)pos, GLRAllocType::NEW);
	// Texcoord
	if (hasTexCoord)
		renderManager_->TextureSubImage(TEX_SLOT_SPLINE_POINTS, data_tex[0], 0, size_u, 0, size_u, size_v, Draw::DataFormat::R32G32B32A32_FLOAT, (u8 *)tex, GLRAllocType::NEW);
	// Color
	if (hasColor)
		renderManager_->TextureSubImage(TEX_SLOT_SPLINE_POINTS, data_tex[0], 0, size_u * 2, 0, size_u, size_v, Draw::DataFormat::R32G32B32A32_FLOAT, (u8 *)col, GLRAllocType::NEW);

	// Weight U
	if (prevSizeWU < weights.size_u) {
		prevSizeWU = weights.size_u;
		if (data_tex[1])
			renderManager_->DeleteTexture(data_tex[1]);
		data_tex[1] = renderManager_->CreateTexture(GL_TEXTURE_2D, weights.size_u * 2, 1, 1, 1);
		renderManager_->TextureImage(data_tex[1], 0, weights.size_u * 2, 1, 1, Draw::DataFormat::R32G32B32A32_FLOAT, nullptr, GLRAllocType::NONE, false);
		renderManager_->FinalizeTexture(data_tex[1], 0, false);
	}
	renderManager_->BindTexture(TEX_SLOT_SPLINE_WEIGHTS_U, data_tex[1]);
	renderManager_->TextureSubImage(TEX_SLOT_SPLINE_WEIGHTS_U, data_tex[1], 0, 0, 0, weights.size_u * 2, 1, Draw::DataFormat::R32G32B32A32_FLOAT, (u8 *)weights.u, GLRAllocType::NONE);

	// Weight V
	if (prevSizeWV < weights.size_v) {
		prevSizeWV = weights.size_v;
		if (data_tex[2])
			renderManager_->DeleteTexture(data_tex[2]);
		data_tex[2] = renderManager_->CreateTexture(GL_TEXTURE_2D, weights.size_v * 2, 1, 1, 1);
		renderManager_->TextureImage(data_tex[2], 0, weights.size_v * 2, 1, 1, Draw::DataFormat::R32G32B32A32_FLOAT, nullptr, GLRAllocType::NONE, false);
		renderManager_->FinalizeTexture(data_tex[2], 0, false);
	}
	renderManager_->BindTexture(TEX_SLOT_SPLINE_WEIGHTS_V, data_tex[2]);
	renderManager_->TextureSubImage(TEX_SLOT_SPLINE_WEIGHTS_V, data_tex[2], 0, 0, 0, weights.size_v * 2, 1, Draw::DataFormat::R32G32B32A32_FLOAT, (u8 *)weights.v, GLRAllocType::NONE);
}

void TessellationDataTransferGLES::EndFrame() {
	for (int i = 0; i < 3; i++) {
		if (data_tex[i]) {
			renderManager_->DeleteTexture(data_tex[i]);
			data_tex[i] = nullptr;
		}
	}
	prevSizeU = prevSizeV = prevSizeWU = prevSizeWV = 0;
}
