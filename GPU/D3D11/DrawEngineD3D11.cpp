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

#include "base/logging.h"
#include "base/timeutil.h"

#include "Common/MemoryUtil.h"
#include "Core/MemMap.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/SplineCommon.h"

#include "GPU/Common/TransformCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/DrawEngineD3D11.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/GPU_D3D11.h"

const D3D11_PRIMITIVE_TOPOLOGY d3d11prim[8] = {
	D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
	D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
	D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  // Fans not supported
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  // Need expansion - though we could do it with geom shaders in most cases
};

#define VERTEXCACHE_DECIMATION_INTERVAL 17

enum { VAI_KILL_AGE = 120, VAI_UNRELIABLE_KILL_AGE = 240, VAI_UNRELIABLE_KILL_MAX = 4 };
enum {
	VERTEX_PUSH_SIZE = 1024 * 1024 * 16,
	INDEX_PUSH_SIZE = 1024 * 1024 * 4,
};

static const D3D11_INPUT_ELEMENT_DESC TransformedVertexElements[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

DrawEngineD3D11::DrawEngineD3D11(Draw::DrawContext *draw, ID3D11Device *device, ID3D11DeviceContext *context)
	: draw_(draw),
		device_(device),
		context_(context),
		vai_(256),
		inputLayoutMap_(32),
		blendCache_(32),
		blendCache1_(32),
		depthStencilCache_(64),
		rasterCache_(4) {
	device1_ = (ID3D11Device1 *)draw->GetNativeObject(Draw::NativeObject::DEVICE_EX);
	context1_ = (ID3D11DeviceContext1 *)draw->GetNativeObject(Draw::NativeObject::CONTEXT_EX);
	decOptions_.expandAllWeightsToFloat = true;
	decOptions_.expand8BitNormalsToFloat = true;

	decimationCounter_ = VERTEXCACHE_DECIMATION_INTERVAL;
	// Allocate nicely aligned memory. Maybe graphics drivers will
	// appreciate it.
	// All this is a LOT of memory, need to see if we can cut down somehow.
	decoded = (u8 *)AllocateMemoryPages(DECODED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	decIndex = (u16 *)AllocateMemoryPages(DECODED_INDEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	splineBuffer = (u8 *)AllocateMemoryPages(SPLINE_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);

	indexGen.Setup(decIndex);

	InitDeviceObjects();

	// Vertex pushing buffers. For uniforms we use short DISCARD buffers, but we could use
	// this kind of buffer there as well with D3D11.1. We might be able to use the same buffer
	// for both vertices and indices, and possibly all three data types.
}

DrawEngineD3D11::~DrawEngineD3D11() {
	DestroyDeviceObjects();
	FreeMemoryPages(decoded, DECODED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(decIndex, DECODED_INDEX_BUFFER_SIZE);
	FreeMemoryPages(splineBuffer, SPLINE_BUFFER_SIZE);
}

void DrawEngineD3D11::InitDeviceObjects() {
	pushVerts_ = new PushBufferD3D11(device_, VERTEX_PUSH_SIZE, D3D11_BIND_VERTEX_BUFFER);
	pushInds_ = new PushBufferD3D11(device_, INDEX_PUSH_SIZE, D3D11_BIND_INDEX_BUFFER);

	tessDataTransfer = new TessellationDataTransferD3D11(context_, device_);
}

void DrawEngineD3D11::ClearTrackedVertexArrays() {
	vai_.Iterate([&](uint32_t hash, VertexArrayInfoD3D11 *vai){
		delete vai;
	});
	vai_.Clear();
}

void DrawEngineD3D11::ClearInputLayoutMap() {
	inputLayoutMap_.Iterate([&](const InputLayoutKey &key, ID3D11InputLayout *il) {
		if (il)
			il->Release();
	});
	inputLayoutMap_.Clear();
}

void DrawEngineD3D11::Resized() {
	DrawEngineCommon::Resized();
	ClearInputLayoutMap();
}

void DrawEngineD3D11::DestroyDeviceObjects() {
	ClearTrackedVertexArrays();
	ClearInputLayoutMap();
	delete tessDataTransfer;
	delete pushVerts_;
	delete pushInds_;
	depthStencilCache_.Iterate([&](const uint64_t &key, ID3D11DepthStencilState *ds) {
		ds->Release();
	});
	depthStencilCache_.Clear();
	blendCache_.Iterate([&](const uint64_t &key, ID3D11BlendState *bs) {
		bs->Release();
	});
	blendCache_.Clear();
	blendCache1_.Iterate([&](const uint64_t &key, ID3D11BlendState1 *bs) {
		bs->Release();
	});
	blendCache1_.Clear();
	rasterCache_.Iterate([&](const uint32_t &key, ID3D11RasterizerState *rs) {
		rs->Release();
	});
	rasterCache_.Clear();
}

struct DeclTypeInfo {
	DXGI_FORMAT type;
	const char * name;
};

static const DeclTypeInfo VComp[] = {
	{ DXGI_FORMAT_UNKNOWN, "NULL" }, // DEC_NONE,
	{ DXGI_FORMAT_R32_FLOAT, "D3DDECLTYPE_FLOAT1 " },  // DEC_FLOAT_1,
	{ DXGI_FORMAT_R32G32_FLOAT, "D3DDECLTYPE_FLOAT2 " },  // DEC_FLOAT_2,
	{ DXGI_FORMAT_R32G32B32_FLOAT, "D3DDECLTYPE_FLOAT3 " },  // DEC_FLOAT_3,
	{ DXGI_FORMAT_R32G32B32A32_FLOAT, "D3DDECLTYPE_FLOAT4 " },  // DEC_FLOAT_4,

	{ DXGI_FORMAT_R8G8B8A8_SNORM, "UNUSED" }, // DEC_S8_3,

	{ DXGI_FORMAT_R16G16B16A16_SNORM, "D3DDECLTYPE_SHORT4N	" },	// DEC_S16_3,
	{ DXGI_FORMAT_R8G8B8A8_UNORM, "D3DDECLTYPE_UBYTE4N	" },	// DEC_U8_1,
	{ DXGI_FORMAT_R8G8B8A8_UNORM, "D3DDECLTYPE_UBYTE4N	" },	// DEC_U8_2,
	{ DXGI_FORMAT_R8G8B8A8_UNORM, "D3DDECLTYPE_UBYTE4N	" },	// DEC_U8_3,
	{ DXGI_FORMAT_R8G8B8A8_UNORM, "D3DDECLTYPE_UBYTE4N	" },	// DEC_U8_4,

	{ DXGI_FORMAT_UNKNOWN, "UNUSED_DEC_U16_1" },	// 	DEC_U16_1,
	{ DXGI_FORMAT_UNKNOWN, "UNUSED_DEC_U16_2" },	// 	DEC_U16_2,
	{ DXGI_FORMAT_R16G16B16A16_UNORM	,"D3DDECLTYPE_USHORT4N "}, // DEC_U16_3,
	{ DXGI_FORMAT_R16G16B16A16_UNORM	,"D3DDECLTYPE_USHORT4N "}, // DEC_U16_4,
};

static void VertexAttribSetup(D3D11_INPUT_ELEMENT_DESC * VertexElement, u8 fmt, u8 offset, const char *semantic, u8 semantic_index = 0) {
	memset(VertexElement, 0, sizeof(D3D11_INPUT_ELEMENT_DESC));
	VertexElement->AlignedByteOffset = offset;
	VertexElement->Format = VComp[fmt].type;
	VertexElement->SemanticName = semantic;
	VertexElement->SemanticIndex = semantic_index;
}

ID3D11InputLayout *DrawEngineD3D11::SetupDecFmtForDraw(D3D11VertexShader *vshader, const DecVtxFormat &decFmt, u32 pspFmt) {
	// TODO: Instead of one for each vshader, we can reduce it to one for each type of shader
	// that reads TEXCOORD or not, etc. Not sure if worth it.
	InputLayoutKey key{ vshader, decFmt.id };
	ID3D11InputLayout *inputLayout = inputLayoutMap_.Get(key);
	if (inputLayout) {
		return inputLayout;
	} else {
		D3D11_INPUT_ELEMENT_DESC VertexElements[8];
		D3D11_INPUT_ELEMENT_DESC *VertexElement = &VertexElements[0];

		// Vertices Elements orders
		// WEIGHT
		if (decFmt.w0fmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.w0fmt, decFmt.w0off, "TEXCOORD", 1);
			VertexElement++;
		}

		if (decFmt.w1fmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.w1fmt, decFmt.w1off, "TEXCOORD", 2);
			VertexElement++;
		}

		// TC
		if (decFmt.uvfmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.uvfmt, decFmt.uvoff, "TEXCOORD", 0);
			VertexElement++;
		}

		// COLOR
		if (decFmt.c0fmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.c0fmt, decFmt.c0off, "COLOR", 0);
			VertexElement++;
		}
		// Never used ?
		if (decFmt.c1fmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.c1fmt, decFmt.c1off, "COLOR", 1);
			VertexElement++;
		}

		// NORMAL
		if (decFmt.nrmfmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.nrmfmt, decFmt.nrmoff, "NORMAL", 0);
			VertexElement++;
		}

		// POSITION
		// Always
		VertexAttribSetup(VertexElement, decFmt.posfmt, decFmt.posoff, "POSITION", 0);
		VertexElement++;

		// Create declaration
		HRESULT hr = device_->CreateInputLayout(VertexElements, VertexElement - VertexElements, vshader->bytecode().data(), vshader->bytecode().size(), &inputLayout);
		if (FAILED(hr)) {
			ERROR_LOG(G3D, "Failed to create input layout!");
			inputLayout = nullptr;
		}

		// Add it to map
		inputLayoutMap_.Insert(key, inputLayout);
		return inputLayout;
	}
}

void DrawEngineD3D11::MarkUnreliable(VertexArrayInfoD3D11 *vai) {
	vai->status = VertexArrayInfoD3D11::VAI_UNRELIABLE;
	if (vai->vbo) {
		vai->vbo->Release();
		vai->vbo = nullptr;
	}
	if (vai->ebo) {
		vai->ebo->Release();
		vai->ebo = nullptr;
	}
}

void DrawEngineD3D11::BeginFrame() {
	pushVerts_->Reset();
	pushInds_->Reset();

	if (--decimationCounter_ <= 0) {
		decimationCounter_ = VERTEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	const int threshold = gpuStats.numFlips - VAI_KILL_AGE;
	const int unreliableThreshold = gpuStats.numFlips - VAI_UNRELIABLE_KILL_AGE;
	int unreliableLeft = VAI_UNRELIABLE_KILL_MAX;
	vai_.Iterate([&](uint32_t hash, VertexArrayInfoD3D11 *vai){
		bool kill;
		if (vai->status == VertexArrayInfoD3D11::VAI_UNRELIABLE) {
			// We limit killing unreliable so we don't rehash too often.
			kill = vai->lastFrame < unreliableThreshold && --unreliableLeft >= 0;
		} else {
			kill = vai->lastFrame < threshold;
		}
		if (kill) {
			delete vai;
			vai_.Remove(hash);
		}
	});
	vai_.Maintain();

	// Enable if you want to see vertex decoders in the log output. Need a better way.
#if 0
	char buffer[16384];
	for (std::map<u32, VertexDecoder*>::iterator dec = decoderMap_.begin(); dec != decoderMap_.end(); ++dec) {
		char *ptr = buffer;
		ptr += dec->second->ToString(ptr);
		//		*ptr++ = '\n';
		NOTICE_LOG(G3D, buffer);
	}
#endif
}

VertexArrayInfoD3D11::~VertexArrayInfoD3D11() {
	if (vbo)
		vbo->Release();
	if (ebo)
		ebo->Release();
}

static uint32_t SwapRB(uint32_t c) {
	return (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c << 16) & 0xFF0000);
}

// The inline wrapper in the header checks for numDrawCalls == 0
void DrawEngineD3D11::DoFlush() {
	gpuStats.numFlushes++;
	gpuStats.numTrackedVertexArrays = (int)vai_.size();

	// This is not done on every drawcall, we should collect vertex data
	// until critical state changes. That's when we draw (flush).

	GEPrimitiveType prim = prevPrim_;
	ApplyDrawState(prim);

	// Always use software for flat shading to fix the provoking index.
	bool useHWTransform = CanUseHardwareTransform(prim) && gstate.getShadeMode() != GE_SHADE_FLAT;

	if (useHWTransform) {
		ID3D11Buffer *vb_ = nullptr;
		ID3D11Buffer *ib_ = nullptr;

		int vertexCount = 0;
		int maxIndex = 0;
		bool useElements = true;

		// Cannot cache vertex data with morph enabled.
		bool useCache = g_Config.bVertexCache && !(lastVType_ & GE_VTYPE_MORPHCOUNT_MASK);
		// Also avoid caching when software skinning.
		if (g_Config.bSoftwareSkinning && (lastVType_ & GE_VTYPE_WEIGHT_MASK))
			useCache = false;

		if (useCache) {
			u32 id = dcid_ ^ gstate.getUVGenMode();  // This can have an effect on which UV decoder we need to use! And hence what the decoded data will look like. See #9263

			VertexArrayInfoD3D11 *vai = vai_.Get(id);
			if (!vai) {
				vai = new VertexArrayInfoD3D11();
				vai_.Insert(id, vai);
			}

			switch (vai->status) {
			case VertexArrayInfoD3D11::VAI_NEW:
				{
					// Haven't seen this one before.
					ReliableHashType dataHash = ComputeHash();
					vai->hash = dataHash;
					vai->minihash = ComputeMiniHash();
					vai->status = VertexArrayInfoD3D11::VAI_HASHING;
					vai->drawsUntilNextFullHash = 0;
					DecodeVerts(decoded); // writes to indexGen
					vai->numVerts = indexGen.VertexCount();
					vai->prim = indexGen.Prim();
					vai->maxIndex = indexGen.MaxIndex();
					vai->flags = gstate_c.vertexFullAlpha ? VAI11_FLAG_VERTEXFULLALPHA : 0;
					goto rotateVBO;
				}

				// Hashing - still gaining confidence about the buffer.
				// But if we get this far it's likely to be worth creating a vertex buffer.
			case VertexArrayInfoD3D11::VAI_HASHING:
				{
					vai->numDraws++;
					if (vai->lastFrame != gpuStats.numFlips) {
						vai->numFrames++;
					}
					if (vai->drawsUntilNextFullHash == 0) {
						// Let's try to skip a full hash if mini would fail.
						const u32 newMiniHash = ComputeMiniHash();
						ReliableHashType newHash = vai->hash;
						if (newMiniHash == vai->minihash) {
							newHash = ComputeHash();
						}
						if (newMiniHash != vai->minihash || newHash != vai->hash) {
							MarkUnreliable(vai);
							DecodeVerts(decoded);
							goto rotateVBO;
						}
						if (vai->numVerts > 64) {
							// exponential backoff up to 16 draws, then every 24
							vai->drawsUntilNextFullHash = std::min(24, vai->numFrames);
						} else {
							// Lower numbers seem much more likely to change.
							vai->drawsUntilNextFullHash = 0;
						}
						// TODO: tweak
						//if (vai->numFrames > 1000) {
						//	vai->status = VertexArrayInfo::VAI_RELIABLE;
						//}
					} else {
						vai->drawsUntilNextFullHash--;
						u32 newMiniHash = ComputeMiniHash();
						if (newMiniHash != vai->minihash) {
							MarkUnreliable(vai);
							DecodeVerts(decoded);
							goto rotateVBO;
						}
					}

					if (vai->vbo == 0) {
						DecodeVerts(decoded);
						vai->numVerts = indexGen.VertexCount();
						vai->prim = indexGen.Prim();
						vai->maxIndex = indexGen.MaxIndex();
						vai->flags = gstate_c.vertexFullAlpha ? VAI11_FLAG_VERTEXFULLALPHA : 0;
						useElements = !indexGen.SeenOnlyPurePrims() || prim == GE_PRIM_TRIANGLE_FAN;
						if (!useElements && indexGen.PureCount()) {
							vai->numVerts = indexGen.PureCount();
						}

						_dbg_assert_msg_(G3D, gstate_c.vertBounds.minV >= gstate_c.vertBounds.maxV, "Should not have checked UVs when caching.");

						// TODO: Combine these two into one buffer?
						u32 size = dec_->GetDecVtxFmt().stride * indexGen.MaxIndex();
						D3D11_BUFFER_DESC desc{ size, D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0 };
						D3D11_SUBRESOURCE_DATA data{ decoded };
						ASSERT_SUCCESS(device_->CreateBuffer(&desc, &data, &vai->vbo));
						if (useElements) {
							u32 size = sizeof(short) * indexGen.VertexCount();
							D3D11_BUFFER_DESC desc{ size, D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0 };
							D3D11_SUBRESOURCE_DATA data{ decIndex };
							ASSERT_SUCCESS(device_->CreateBuffer(&desc, &data, &vai->ebo));
						} else {
							vai->ebo = 0;
						}
					} else {
						gpuStats.numCachedDrawCalls++;
						useElements = vai->ebo ? true : false;
						gpuStats.numCachedVertsDrawn += vai->numVerts;
						gstate_c.vertexFullAlpha = vai->flags & VAI11_FLAG_VERTEXFULLALPHA;
					}
					vb_ = vai->vbo;
					ib_ = vai->ebo;
					vertexCount = vai->numVerts;
					maxIndex = vai->maxIndex;
					prim = static_cast<GEPrimitiveType>(vai->prim);
					break;
				}

				// Reliable - we don't even bother hashing anymore. Right now we don't go here until after a very long time.
			case VertexArrayInfoD3D11::VAI_RELIABLE:
				{
					vai->numDraws++;
					if (vai->lastFrame != gpuStats.numFlips) {
						vai->numFrames++;
					}
					gpuStats.numCachedDrawCalls++;
					gpuStats.numCachedVertsDrawn += vai->numVerts;
					vb_ = vai->vbo;
					ib_ = vai->ebo;

					vertexCount = vai->numVerts;

					maxIndex = vai->maxIndex;
					prim = static_cast<GEPrimitiveType>(vai->prim);

					gstate_c.vertexFullAlpha = vai->flags & VAI11_FLAG_VERTEXFULLALPHA;
					break;
				}

			case VertexArrayInfoD3D11::VAI_UNRELIABLE:
				{
					vai->numDraws++;
					if (vai->lastFrame != gpuStats.numFlips) {
						vai->numFrames++;
					}
					DecodeVerts(decoded);
					goto rotateVBO;
				}
			}

			vai->lastFrame = gpuStats.numFlips;
		} else {
			DecodeVerts(decoded);
rotateVBO:
			gpuStats.numUncachedVertsDrawn += indexGen.VertexCount();
			useElements = !indexGen.SeenOnlyPurePrims() || prim == GE_PRIM_TRIANGLE_FAN;
			vertexCount = indexGen.VertexCount();
			maxIndex = indexGen.MaxIndex();
			if (!useElements && indexGen.PureCount()) {
				vertexCount = indexGen.PureCount();
			}
			prim = indexGen.Prim();
		}

		VERBOSE_LOG(G3D, "Flush prim %i! %i verts in one go", prim, vertexCount);
		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		ApplyDrawStateLate(true, dynState_.stencilRef);

		D3D11VertexShader *vshader;
		D3D11FragmentShader *fshader;
		shaderManager_->GetShaders(prim, lastVType_, &vshader, &fshader, useHWTransform);
		ID3D11InputLayout *inputLayout = SetupDecFmtForDraw(vshader, dec_->GetDecVtxFmt(), dec_->VertexType());
		context_->PSSetShader(fshader->GetShader(), nullptr, 0);
		context_->VSSetShader(vshader->GetShader(), nullptr, 0);
		shaderManager_->UpdateUniforms();
		shaderManager_->BindUniforms();

		context_->IASetInputLayout(inputLayout);
		UINT stride = dec_->GetDecVtxFmt().stride;
		context_->IASetPrimitiveTopology(d3d11prim[prim]);
		if (!vb_) {
			// Push!
			UINT vOffset;
			int vSize = (maxIndex + 1) * dec_->GetDecVtxFmt().stride;
			uint8_t *vptr = pushVerts_->BeginPush(context_, &vOffset, vSize);
			memcpy(vptr, decoded, vSize);
			pushVerts_->EndPush(context_);
			ID3D11Buffer *buf = pushVerts_->Buf();
			context_->IASetVertexBuffers(0, 1, &buf, &stride, &vOffset);
			if (useElements) {
				UINT iOffset;
				int iSize = 2 * indexGen.VertexCount();
				uint8_t *iptr = pushInds_->BeginPush(context_, &iOffset, iSize);
				memcpy(iptr, decIndex, iSize);
				pushInds_->EndPush(context_);
				context_->IASetIndexBuffer(pushInds_->Buf(), DXGI_FORMAT_R16_UINT, iOffset);
				if (gstate_c.bezier || gstate_c.spline)
					context_->DrawIndexedInstanced(vertexCount, numPatches, 0, 0, 0);
				else
					context_->DrawIndexed(vertexCount, 0, 0);
			} else {
				context_->Draw(vertexCount, 0);
			}
		} else {
			UINT offset = 0;
			context_->IASetVertexBuffers(0, 1, &vb_, &stride, &offset);
			if (useElements) {
				context_->IASetIndexBuffer(ib_, DXGI_FORMAT_R16_UINT, 0);
				if (gstate_c.bezier || gstate_c.spline)
					context_->DrawIndexedInstanced(vertexCount, numPatches, 0, 0, 0);
				else
					context_->DrawIndexed(vertexCount, 0, 0);
			} else {
				context_->Draw(vertexCount, 0);
			}
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
		VERBOSE_LOG(G3D, "Flush prim %i SW! %i verts in one go", prim, indexGen.VertexCount());

		int numTrans = 0;
		bool drawIndexed = false;
		u16 *inds = decIndex;
		TransformedVertex *drawBuffer = NULL;
		SoftwareTransformResult result{};
		SoftwareTransformParams params{};
		params.decoded = decoded;
		params.transformed = transformed;
		params.transformedExpanded = transformedExpanded;
		params.fbman = framebufferManager_;
		params.texCache = textureCache_;
		params.allowClear = true;
		params.allowSeparateAlphaClear = false;  // D3D11 doesn't support separate alpha clears
		params.provokeFlatFirst = true;

		int maxIndex = indexGen.MaxIndex();
		SoftwareTransform(
			prim, indexGen.VertexCount(),
			dec_->VertexType(), inds, GE_VTYPE_IDX_16BIT, dec_->GetDecVtxFmt(),
			maxIndex, drawBuffer, numTrans, drawIndexed, &params, &result);

		if (result.action == SW_DRAW_PRIMITIVES) {
			const int vertexSize = sizeof(transformed[0]);

			ApplyDrawStateLate(result.setStencil, result.stencilValue);

			D3D11VertexShader *vshader;
			D3D11FragmentShader *fshader;
			shaderManager_->GetShaders(prim, lastVType_, &vshader, &fshader, false);
			context_->PSSetShader(fshader->GetShader(), nullptr, 0);
			context_->VSSetShader(vshader->GetShader(), nullptr, 0);
			shaderManager_->UpdateUniforms();
			shaderManager_->BindUniforms();

			// We really do need a vertex layout for each vertex shader (or at least check its ID bits for what inputs it uses)!
			// Some vertex shaders ignore one of the inputs, and then the layout created from it will lack it, which will be a problem for others.
			InputLayoutKey key{ vshader, 0xFFFFFFFF };  // Let's use 0xFFFFFFFF to signify TransformedVertex
			ID3D11InputLayout *layout = inputLayoutMap_.Get(key);
			if (!layout) {
				ASSERT_SUCCESS(device_->CreateInputLayout(TransformedVertexElements, ARRAY_SIZE(TransformedVertexElements), vshader->bytecode().data(), vshader->bytecode().size(), &layout));
				inputLayoutMap_.Insert(key, layout);
			}
			context_->IASetInputLayout(layout);
			context_->IASetPrimitiveTopology(d3d11prim[prim]);

			UINT stride = sizeof(TransformedVertex);
			UINT vOffset = 0;
			int vSize = maxIndex * stride;
			uint8_t *vptr = pushVerts_->BeginPush(context_, &vOffset, vSize);
			memcpy(vptr, drawBuffer, vSize);
			pushVerts_->EndPush(context_);
			ID3D11Buffer *buf = pushVerts_->Buf();
			context_->IASetVertexBuffers(0, 1, &buf, &stride, &vOffset);
			if (drawIndexed) {
				UINT iOffset;
				int iSize = sizeof(uint16_t) * numTrans;
				uint8_t *iptr = pushInds_->BeginPush(context_, &iOffset, iSize);
				memcpy(iptr, inds, iSize);
				pushInds_->EndPush(context_);
				context_->IASetIndexBuffer(pushInds_->Buf(), DXGI_FORMAT_R16_UINT, iOffset);
				context_->DrawIndexed(numTrans, 0, 0);
			} else {
				context_->Draw(numTrans, 0);
			}
		} else if (result.action == SW_CLEAR) {
			u32 clearColor = result.color;
			float clearDepth = result.depth;

			uint32_t clearFlag = 0;

			if (gstate.isClearModeColorMask()) clearFlag |= Draw::FBChannel::FB_COLOR_BIT;
			if (gstate.isClearModeAlphaMask()) clearFlag |= Draw::FBChannel::FB_STENCIL_BIT;
			if (gstate.isClearModeDepthMask()) clearFlag |= Draw::FBChannel::FB_DEPTH_BIT;

			if (clearFlag & Draw::FBChannel::FB_DEPTH_BIT) {
				framebufferManager_->SetDepthUpdated();
			}
			if (clearFlag & Draw::FBChannel::FB_COLOR_BIT) {
				framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);
			}

			uint8_t clearStencil = clearColor >> 24;
			draw_->Clear(clearFlag, clearColor, clearDepth, clearStencil);

			int scissorX2 = gstate.getScissorX2() + 1;
			int scissorY2 = gstate.getScissorY2() + 1;
			framebufferManager_->SetSafeSize(scissorX2, scissorY2);
			if (g_Config.bBlockTransferGPU && (gstate_c.featureFlags & GPU_USE_CLEAR_RAM_HACK) && gstate.isClearModeColorMask() && (gstate.isClearModeAlphaMask() || gstate.FrameBufFormat() == GE_FORMAT_565)) {
				int scissorX1 = gstate.getScissorX1();
				int scissorY1 = gstate.getScissorY1();
				framebufferManager_->ApplyClearToMemory(scissorX1, scissorY1, scissorX2, scissorY2, clearColor);
			}
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

#if PPSSPP_PLATFORM(WINDOWS) && !PPSSPP_PLATFORM(UWP)
	// We only support GPU debugging on Windows, and that's the only use case for this.
	host->GPUNotifyDraw();
#endif
}

void DrawEngineD3D11::TessellationDataTransferD3D11::SendDataToShader(const float * pos, const float * tex, const float * col, int size, bool hasColor, bool hasTexCoords) {
	// Position
	if (prevSize < size) {
		prevSize = size;
		if (data_tex[0]) {
			data_tex[0]->Release();
			view[0]->Release();
		}
		desc.Width = size;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		HRESULT hr = device_->CreateTexture1D(&desc, nullptr, &data_tex[0]);
		if (FAILED(hr)) {
			INFO_LOG(G3D, "Failed to create D3D texture for HW tessellation");
			data_tex[0]->Release();
			return; // TODO: Turn off HW tessellation if texture creation error occured.
		}
		hr = device_->CreateShaderResourceView(data_tex[0], nullptr, &view[0]);
		ASSERT_SUCCESS(hr);
		context_->VSSetShaderResources(0, 1, &view[0]);
	}
	dstBox.right = size;
	context_->UpdateSubresource(data_tex[0], 0, &dstBox, pos, 0, 0);

	// Texcoords
	if (hasTexCoords) {
		if (prevSizeTex < size) {
			prevSizeTex = size;
			if (data_tex[1]) {
				data_tex[1]->Release();
				view[1]->Release();
			}
			desc.Width = size;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			HRESULT hr = device_->CreateTexture1D(&desc, nullptr, &data_tex[1]);
			if (FAILED(hr)) {
				INFO_LOG(G3D, "Failed to create D3D texture for HW tessellation");
				data_tex[1]->Release();
				return;
			}
			hr = device_->CreateShaderResourceView(data_tex[1], nullptr, &view[1]);
			context_->VSSetShaderResources(1, 1, &view[1]);
		}
		dstBox.right = size;
		context_->UpdateSubresource(data_tex[1], 0, &dstBox, tex, 0, 0);
	}

	// Color
	int sizeColor = hasColor ? size : 1;
	if (prevSizeCol < sizeColor) {
		prevSizeCol = sizeColor;
		if (data_tex[2]) {
			data_tex[2]->Release();
			view[2]->Release();
		}
		desc.Width = sizeColor;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		HRESULT hr = device_->CreateTexture1D(&desc, nullptr, &data_tex[2]);
		if (FAILED(hr)) {
			INFO_LOG(G3D, "Failed to create D3D texture for HW tessellation");
			data_tex[2]->Release();
			return;
		}
		hr = device_->CreateShaderResourceView(data_tex[2], nullptr, &view[2]);
		context_->VSSetShaderResources(2, 1, &view[2]);
	}
	dstBox.right = sizeColor;
	context_->UpdateSubresource(data_tex[2], 0, &dstBox, col, 0, 0);
}
