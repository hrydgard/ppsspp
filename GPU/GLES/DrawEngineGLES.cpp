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

#include "Common/MemoryUtil.h"
#include "Common/TimeUtil.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/Reporting.h"
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

const GLuint glprim[8] = {
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

	// Allocate nicely aligned memory. Maybe graphics drivers will
	// appreciate it.
	// All this is a LOT of memory, need to see if we can cut down somehow.
	decoded = (u8 *)AllocateMemoryPages(DECODED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	decIndex = (u16 *)AllocateMemoryPages(DECODED_INDEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);

	indexGen.Setup(decIndex);

	InitDeviceObjects();

	tessDataTransferGLES = new TessellationDataTransferGLES(render_);
	tessDataTransfer = tessDataTransferGLES;
}

DrawEngineGLES::~DrawEngineGLES() {
	DestroyDeviceObjects();
	FreeMemoryPages(decoded, DECODED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(decIndex, DECODED_INDEX_BUFFER_SIZE);

	delete tessDataTransferGLES;
}

void DrawEngineGLES::DeviceLost() {
	DestroyDeviceObjects();
}

void DrawEngineGLES::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	render_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	InitDeviceObjects();
}

void DrawEngineGLES::InitDeviceObjects() {
	_assert_msg_(render_ != nullptr, "Render manager must be set");

	for (int i = 0; i < GLRenderManager::MAX_INFLIGHT_FRAMES; i++) {
		frameData_[i].pushVertex = render_->CreatePushBuffer(i, GL_ARRAY_BUFFER, 1024 * 1024);
		frameData_[i].pushIndex = render_->CreatePushBuffer(i, GL_ELEMENT_ARRAY_BUFFER, 256 * 1024);
	}

	int vertexSize = sizeof(TransformedVertex);
	std::vector<GLRInputLayout::Entry> entries;
	entries.push_back({ ATTR_POSITION, 4, GL_FLOAT, GL_FALSE, vertexSize, offsetof(TransformedVertex, x) });
	entries.push_back({ ATTR_TEXCOORD, 3, GL_FLOAT, GL_FALSE, vertexSize, offsetof(TransformedVertex, u) });
	entries.push_back({ ATTR_COLOR0, 4, GL_UNSIGNED_BYTE, GL_TRUE, vertexSize, offsetof(TransformedVertex, color0) });
	entries.push_back({ ATTR_COLOR1, 3, GL_UNSIGNED_BYTE, GL_TRUE, vertexSize, offsetof(TransformedVertex, color1) });
	entries.push_back({ ATTR_NORMAL, 1, GL_FLOAT, GL_FALSE, vertexSize, offsetof(TransformedVertex, fog) });
	softwareInputLayout_ = render_->CreateInputLayout(entries);
}

void DrawEngineGLES::DestroyDeviceObjects() {
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
	DecimateTrackedVertexArrays();

	FrameData &frameData = frameData_[render_->GetCurFrame()];
	render_->BeginPushBuffer(frameData.pushIndex);
	render_->BeginPushBuffer(frameData.pushVertex);

	lastRenderStepId_ = -1;
}

void DrawEngineGLES::EndFrame() {
	FrameData &frameData = frameData_[render_->GetCurFrame()];
	render_->EndPushBuffer(frameData.pushIndex);
	render_->EndPushBuffer(frameData.pushVertex);
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

static inline void VertexAttribSetup(int attrib, int fmt, int stride, int offset, std::vector<GLRInputLayout::Entry> &entries) {
	if (fmt) {
		const GlTypeInfo &type = GLComp[fmt];
		GLRInputLayout::Entry entry;
		entry.offset = offset;
		entry.location = attrib;
		entry.normalized = type.normalized;
		entry.type = type.type;
		entry.stride = stride;
		entry.count = type.count;
		entries.push_back(entry);
	}
}

// TODO: Use VBO and get rid of the vertexData pointers - with that, we will supply only offsets
GLRInputLayout *DrawEngineGLES::SetupDecFmtForDraw(LinkedShader *program, const DecVtxFormat &decFmt) {
	uint32_t key = decFmt.id;
	GLRInputLayout *inputLayout = inputLayoutMap_.Get(key);
	if (inputLayout) {
		return inputLayout;
	}

	std::vector<GLRInputLayout::Entry> entries;
	VertexAttribSetup(ATTR_W1, decFmt.w0fmt, decFmt.stride, decFmt.w0off, entries);
	VertexAttribSetup(ATTR_W2, decFmt.w1fmt, decFmt.stride, decFmt.w1off, entries);
	VertexAttribSetup(ATTR_TEXCOORD, decFmt.uvfmt, decFmt.stride, decFmt.uvoff, entries);
	VertexAttribSetup(ATTR_COLOR0, decFmt.c0fmt, decFmt.stride, decFmt.c0off, entries);
	VertexAttribSetup(ATTR_COLOR1, decFmt.c1fmt, decFmt.stride, decFmt.c1off, entries);
	VertexAttribSetup(ATTR_NORMAL, decFmt.nrmfmt, decFmt.stride, decFmt.nrmoff, entries);
	VertexAttribSetup(ATTR_POSITION, decFmt.posfmt, decFmt.stride, decFmt.posoff, entries);

	inputLayout = render_->CreateInputLayout(entries);
	inputLayoutMap_.Insert(key, inputLayout);
	return inputLayout;
}

void *DrawEngineGLES::DecodeVertsToPushBuffer(GLPushBuffer *push, uint32_t *bindOffset, GLRBuffer **buf) {
	u8 *dest = decoded;

	// Figure out how much pushbuffer space we need to allocate.
	if (push) {
		int vertsToDecode = ComputeNumVertsToDecode();
		dest = (u8 *)push->Push(vertsToDecode * dec_->GetDecVtxFmt().stride, bindOffset, buf);
	}
	DecodeVerts(dest);
	return dest;
}

void DrawEngineGLES::DoFlush() {
	PROFILE_THIS_SCOPE("flush");

	FrameData &frameData = frameData_[render_->GetCurFrame()];
	
	gpuStats.numFlushes++;
	gpuStats.numTrackedVertexArrays = 0;

	// A new render step means we need to flush any dynamic state. Really, any state that is reset in
	// GLQueueRunner::PerformRenderPass.
	int curRenderStepId = render_->GetCurrentStepId();
	if (lastRenderStepId_ != curRenderStepId) {
		// Dirty everything that has dynamic state that will need re-recording.
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_BLEND_STATE | DIRTY_RASTER_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
		textureCache_->ForgetLastTexture();
		lastRenderStepId_ = curRenderStepId;
	}

	bool textureNeedsApply = false;
	if (gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS) && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.Clean(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
		textureNeedsApply = true;
	} else if (gstate.getTextureAddress(0) == ((gstate.getFrameBufRawAddress() | 0x04000000) & 0x3FFFFFFF)) {
		// This catches the case of clearing a texture. (#10957)
		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
	}

	GEPrimitiveType prim = prevPrim_;

	VShaderID vsid;
	Shader *vshader = shaderManager_->ApplyVertexShader(CanUseHardwareTransform(prim), useHWTessellation_, lastVType_, decOptions_.expandAllWeightsToFloat, &vsid);

	GLRBuffer *vertexBuffer = nullptr;
	GLRBuffer *indexBuffer = nullptr;
	uint32_t vertexBufferOffset = 0;
	uint32_t indexBufferOffset = 0;

	if (vshader->UseHWTransform()) {
		int vertexCount = 0;
		bool useElements = true;
		bool populateCache = false;

		if (g_Config.bSoftwareSkinning && (lastVType_ & GE_VTYPE_WEIGHT_MASK)) {
			// If software skinning, we've already predecoded into "decoded". So push that content.
			size_t size = decodedVerts_ * dec_->GetDecVtxFmt().stride;
			u8 *dest = (u8 *)frameData.pushVertex->Push(size, &vertexBufferOffset, &vertexBuffer);
			memcpy(dest, decoded, size);
		} else {
			// Decode directly into the pushbuffer
			u8 *dest = (u8 *)DecodeVertsToPushBuffer(frameData.pushVertex, &vertexBufferOffset, &vertexBuffer);
		}

		gpuStats.numUncachedVertsDrawn += indexGen.VertexCount();

		// If there's only been one primitive type, and it's either TRIANGLES, LINES or POINTS,
		// there is no need for the index buffer we built. We can then use glDrawArrays instead
		// for a very minor speed boost.
		useElements = !indexGen.SeenOnlyPurePrims();
		vertexCount = indexGen.VertexCount();
		if (!useElements && indexGen.PureCount()) {
			vertexCount = indexGen.PureCount();
		}
		prim = indexGen.Prim();

		VERBOSE_LOG(G3D, "Flush prim %d! %d verts in one go", prim, vertexCount);
		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		if (textureNeedsApply)
			textureCache_->ApplyTexture();

		// Need to ApplyDrawState after ApplyTexture because depal can launch a render pass and that wrecks the state.
		ApplyDrawState(prim);
		ApplyDrawStateLate(false, 0);
		
		LinkedShader *program = shaderManager_->ApplyFragmentShader(vsid, vshader, lastVType_, framebufferManager_->UseBufferedRendering());
		GLRInputLayout *inputLayout = SetupDecFmtForDraw(program, dec_->GetDecVtxFmt());
		render_->BindVertexBuffer(inputLayout, vertexBuffer, vertexBufferOffset);
		if (useElements) {
			if (!indexBuffer) {
				size_t esz = sizeof(uint16_t) * indexGen.VertexCount();
				void *dest = frameData.pushIndex->Push(esz, &indexBufferOffset, &indexBuffer);
				memcpy(dest, decIndex, esz);
			}
			render_->BindIndexBuffer(indexBuffer);
			render_->DrawIndexed(glprim[prim], vertexCount, GL_UNSIGNED_SHORT, (GLvoid*)(intptr_t)indexBufferOffset);
		} else {
			render_->Draw(glprim[prim], 0, vertexCount);
		}
	} else {
		DecodeVerts(decoded);
		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		gpuStats.numUncachedVertsDrawn += indexGen.VertexCount();
		prim = indexGen.Prim();
		// Undo the strip optimization, not supported by the SW code yet.
		if (prim == GE_PRIM_TRIANGLE_STRIP)
			prim = GE_PRIM_TRIANGLES;

		u16 *inds = decIndex;
		SoftwareTransformResult result{};
		// TODO: Keep this static?  Faster than repopulating?
		SoftwareTransformParams params{};
		params.decoded = decoded;
		params.transformed = transformed;
		params.transformedExpanded = transformedExpanded;
		params.fbman = framebufferManager_;
		params.texCache = textureCache_;
		params.allowClear = true;
		params.allowSeparateAlphaClear = true;
		params.provokeFlatFirst = false;
		params.flippedY = framebufferManager_->UseBufferedRendering();
		params.usesHalfZ = false;

		// We need correct viewport values in gstate_c already.
		if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
			ConvertViewportAndScissor(framebufferManager_->UseBufferedRendering(),
				framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(),
				framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(),
				vpAndScissor);
		}

		int maxIndex = indexGen.MaxIndex();
		int vertexCount = indexGen.VertexCount();

		// TODO: Split up into multiple draw calls for GLES 2.0 where you can't guarantee support for more than 0x10000 verts.
#if defined(MOBILE_DEVICE)
		constexpr int vertexCountLimit = 0x10000 / 3;
		if (vertexCount > vertexCountLimit) {
			WARN_LOG_REPORT_ONCE(manyVerts, G3D, "Truncating vertex count from %d to %d", vertexCount, vertexCountLimit);
			vertexCount = vertexCountLimit;
		}
#endif

		SoftwareTransform swTransform(params);

		const Lin::Vec3 trans(gstate_c.vpXOffset, gstate_c.vpYOffset, gstate_c.vpZOffset);
		const Lin::Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale);
		const bool invertedY = gstate_c.vpHeight * (params.flippedY ? 1.0 : -1.0f) < 0;
		swTransform.SetProjMatrix(gstate.projMatrix, gstate_c.vpWidth < 0, invertedY, trans, scale);

		swTransform.Decode(prim, dec_->VertexType(), dec_->GetDecVtxFmt(), maxIndex, &result);
		if (result.action == SW_NOT_READY)
			swTransform.DetectOffsetTexture(maxIndex);

		if (textureNeedsApply)
			textureCache_->ApplyTexture();

		// Need to ApplyDrawState after ApplyTexture because depal can launch a render pass and that wrecks the state.
		ApplyDrawState(prim);

		if (result.action == SW_NOT_READY)
			swTransform.BuildDrawingParams(prim, vertexCount, dec_->VertexType(), inds, maxIndex, &result);
		if (result.setSafeSize)
			framebufferManager_->SetSafeSize(result.safeWidth, result.safeHeight);

		ApplyDrawStateLate(result.setStencil, result.stencilValue);

		shaderManager_->ApplyFragmentShader(vsid, vshader, lastVType_, framebufferManager_->UseBufferedRendering());

		if (result.action == SW_DRAW_PRIMITIVES) {
			if (result.drawIndexed) {
				vertexBufferOffset = (uint32_t)frameData.pushVertex->Push(result.drawBuffer, maxIndex * sizeof(TransformedVertex), &vertexBuffer);
				indexBufferOffset = (uint32_t)frameData.pushIndex->Push(inds, sizeof(uint16_t) * result.drawNumTrans, &indexBuffer);
				render_->BindVertexBuffer(softwareInputLayout_, vertexBuffer, vertexBufferOffset);
				render_->BindIndexBuffer(indexBuffer);
				render_->DrawIndexed(glprim[prim], result.drawNumTrans, GL_UNSIGNED_SHORT, (void *)(intptr_t)indexBufferOffset);
			} else {
				vertexBufferOffset = (uint32_t)frameData.pushVertex->Push(result.drawBuffer, result.drawNumTrans * sizeof(TransformedVertex), &vertexBuffer);
				render_->BindVertexBuffer(softwareInputLayout_, vertexBuffer, vertexBufferOffset);
				render_->Draw(glprim[prim], 0, result.drawNumTrans);
			}
		} else if (result.action == SW_CLEAR) {
			u32 clearColor = result.color;
			float clearDepth = result.depth;

			bool colorMask = gstate.isClearModeColorMask();
			bool alphaMask = gstate.isClearModeAlphaMask();
			bool depthMask = gstate.isClearModeDepthMask();
			if (depthMask) {
				framebufferManager_->SetDepthUpdated();
			}

			GLbitfield target = 0;
			// Without this, we will clear RGB when clearing stencil, which breaks games.
			uint8_t rgbaMask = (colorMask ? 7 : 0) | (alphaMask ? 8 : 0);
			if (colorMask || alphaMask) target |= GL_COLOR_BUFFER_BIT;
			if (alphaMask) target |= GL_STENCIL_BUFFER_BIT;
			if (depthMask) target |= GL_DEPTH_BUFFER_BIT;

			render_->Clear(clearColor, clearDepth, clearColor >> 24, target, rgbaMask, vpAndScissor.scissorX, vpAndScissor.scissorY, vpAndScissor.scissorW, vpAndScissor.scissorH);
			framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);

			if ((gstate_c.featureFlags & GPU_USE_CLEAR_RAM_HACK) && colorMask && (alphaMask || gstate_c.framebufFormat == GE_FORMAT_565)) {
				int scissorX1 = gstate.getScissorX1();
				int scissorY1 = gstate.getScissorY1();
				int scissorX2 = gstate.getScissorX2() + 1;
				int scissorY2 = gstate.getScissorY2() + 1;
				framebufferManager_->ApplyClearToMemory(scissorX1, scissorY1, scissorX2, scissorY2, clearColor);
			}
			gstate_c.Dirty(DIRTY_BLEND_STATE);  // Make sure the color mask gets re-applied.
		}
	}

	gpuStats.numDrawCalls += numDrawCalls;
	gpuStats.numVertsSubmitted += vertexCountInDrawCalls_;

	indexGen.Reset();
	decodedVerts_ = 0;
	numDrawCalls = 0;
	vertexCountInDrawCalls_ = 0;
	decodeCounter_ = 0;
	dcid_ = 0;
	prevPrim_ = GE_PRIM_INVALID;
	gstate_c.vertexFullAlpha = true;
	framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);

	// Now seems as good a time as any to reset the min/max coords, which we may examine later.
	gstate_c.vertBounds.minU = 512;
	gstate_c.vertBounds.minV = 512;
	gstate_c.vertBounds.maxU = 0;
	gstate_c.vertBounds.maxV = 0;

	GPUDebug::NotifyDraw();
}

bool DrawEngineGLES::IsCodePtrVertexDecoder(const u8 *ptr) const {
	return decJitCache_->IsInSpace(ptr);
}

bool DrawEngineGLES::SupportsHWTessellation() const {
	bool hasTexelFetch = gl_extensions.GLES3 || (!gl_extensions.IsGLES && gl_extensions.VersionGEThan(3, 3, 0)) || gl_extensions.EXT_gpu_shader4;
	return hasTexelFetch && gstate_c.SupportsAll(GPU_SUPPORTS_VERTEX_TEXTURE_FETCH | GPU_SUPPORTS_TEXTURE_FLOAT | GPU_SUPPORTS_INSTANCE_RENDERING);
}

bool DrawEngineGLES::UpdateUseHWTessellation(bool enable) {
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
		if (!data_tex[0])
			data_tex[0] = renderManager_->CreateTexture(GL_TEXTURE_2D, size_u * 3, size_v, 1);
		renderManager_->TextureImage(data_tex[0], 0, size_u * 3, size_v, Draw::DataFormat::R32G32B32A32_FLOAT, nullptr, GLRAllocType::NONE, false);
		renderManager_->FinalizeTexture(data_tex[0], 0, false);
	}
	renderManager_->BindTexture(TEX_SLOT_SPLINE_POINTS, data_tex[0]);
	// Position
	renderManager_->TextureSubImage(data_tex[0], 0, 0, 0, size_u, size_v, Draw::DataFormat::R32G32B32A32_FLOAT, (u8 *)pos, GLRAllocType::NEW);
	// Texcoord
	if (hasTexCoord)
		renderManager_->TextureSubImage(data_tex[0], 0, size_u, 0, size_u, size_v, Draw::DataFormat::R32G32B32A32_FLOAT, (u8 *)tex, GLRAllocType::NEW);
	// Color
	if (hasColor)
		renderManager_->TextureSubImage(data_tex[0], 0, size_u * 2, 0, size_u, size_v, Draw::DataFormat::R32G32B32A32_FLOAT, (u8 *)col, GLRAllocType::NEW);

	// Weight U
	if (prevSizeWU < weights.size_u) {
		prevSizeWU = weights.size_u;
		if (!data_tex[1])
			data_tex[1] = renderManager_->CreateTexture(GL_TEXTURE_2D, weights.size_u * 2, 1, 1);
		renderManager_->TextureImage(data_tex[1], 0, weights.size_u * 2, 1, Draw::DataFormat::R32G32B32A32_FLOAT, nullptr, GLRAllocType::NONE, false);
		renderManager_->FinalizeTexture(data_tex[1], 0, false);
	}
	renderManager_->BindTexture(TEX_SLOT_SPLINE_WEIGHTS_U, data_tex[1]);
	renderManager_->TextureSubImage(data_tex[1], 0, 0, 0, weights.size_u * 2, 1, Draw::DataFormat::R32G32B32A32_FLOAT, (u8 *)weights.u, GLRAllocType::NONE);

	// Weight V
	if (prevSizeWV < weights.size_v) {
		prevSizeWV = weights.size_v;
		if (!data_tex[2])
			data_tex[2] = renderManager_->CreateTexture(GL_TEXTURE_2D, weights.size_v * 2, 1, 1);
		renderManager_->TextureImage(data_tex[2], 0, weights.size_v * 2, 1, Draw::DataFormat::R32G32B32A32_FLOAT, nullptr, GLRAllocType::NONE, false);
		renderManager_->FinalizeTexture(data_tex[2], 0, false);
	}
	renderManager_->BindTexture(TEX_SLOT_SPLINE_WEIGHTS_V, data_tex[2]);
	renderManager_->TextureSubImage(data_tex[2], 0, 0, 0, weights.size_v * 2, 1, Draw::DataFormat::R32G32B32A32_FLOAT, (u8 *)weights.v, GLRAllocType::NONE);
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
