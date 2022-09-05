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

#include "Common/Log.h"
#include "Common/Math/expression_parser.h"
#include "Core/Debugger/SymbolMap.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Debugger/GECommandTable.h"
#include "GPU/GPUState.h"

enum class GEReferenceIndex : uint32_t {
	VADDR = 0x100,
	IADDR,
	OFFSET,
	PC,
	STALL,
	BFLAG,

	BONE_MATRIX = 0x200,
	WORLD_MATRIX = 0x260,
	VIEW_MATRIX = 0x26C,
	PROJ_MATRIX = 0x278,
	TGEN_MATRIX = 0x288,
	MATRIX_END = 0x294,
};
ENUM_CLASS_BITOPS(GEReferenceIndex);

struct ReferenceName {
	GEReferenceIndex index;
	const char *name;
};

static constexpr ReferenceName referenceNames[] = {
	{ GEReferenceIndex::VADDR, "vaddr" },
	{ GEReferenceIndex::IADDR, "iaddr" },
	{ GEReferenceIndex::OFFSET, "offset" },
	{ GEReferenceIndex::PC, "pc" },
	{ GEReferenceIndex::STALL, "stall" },
	{ GEReferenceIndex::BFLAG, "bflag" },
	{ GEReferenceIndex::BFLAG, "boundflag" },
};

class GEExpressionFunctions : public IExpressionFunctions {
public:
	GEExpressionFunctions(GPUDebugInterface *gpu) : gpu_(gpu) {}

	bool parseReference(char *str, uint32_t &referenceIndex) override;
	bool parseSymbol(char *str, uint32_t &symbolValue) override;
	uint32_t getReferenceValue(uint32_t referenceIndex) override;
	ExpressionType getReferenceType(uint32_t referenceIndex) override;
	bool getMemoryValue(uint32_t address, int size, uint32_t &dest, char *error) override;

private:
	GPUDebugInterface *gpu_;
};

bool GEExpressionFunctions::parseReference(char *str, uint32_t &referenceIndex) {
	// TODO: Support formats and a form of fields (i.e. vtype.throughmode.)
	// For now, let's just support the register bits directly.
	GECmdInfo info;
	if (GECmdInfoByName(str, info)) {
		referenceIndex = info.reg;
		return true;
	}

	// Also allow non-register references.
	for (const auto &entry : referenceNames) {
		if (strcasecmp(str, entry.name) == 0) {
			referenceIndex = (uint32_t)entry.index;
			return true;
		}
	}

	// And matrix data.  Maybe should allow column/row specification.
	int subindex = -1;
	int len = -1;

	if (sscanf(str, "bone%i%n", &subindex, &len) == 1) {
		if (len == strlen(str) && subindex < 96) {
			referenceIndex = (uint32_t)GEReferenceIndex::BONE_MATRIX + subindex;
			return true;
		}
	}
	if (sscanf(str, "world%i%n", &subindex, &len) == 1) {
		if (len == strlen(str) && subindex < 12) {
			referenceIndex = (uint32_t)GEReferenceIndex::WORLD_MATRIX + subindex;
			return true;
		}
	}
	if (sscanf(str, "view%i%n", &subindex, &len) == 1) {
		if (len == strlen(str) && subindex < 12) {
			referenceIndex = (uint32_t)GEReferenceIndex::VIEW_MATRIX + subindex;
			return true;
		}
	}
	if (sscanf(str, "proj%i%n", &subindex, &len) == 1) {
		if (len == strlen(str) && subindex < 16) {
			referenceIndex = (uint32_t)GEReferenceIndex::PROJ_MATRIX + subindex;
			return true;
		}
	}
	if (sscanf(str, "tgen%i%n", &subindex, &len) == 1 || sscanf(str, "texgen%i%n", &subindex, &len) == 1) {
		if (len == strlen(str) && subindex < 12) {
			referenceIndex = (uint32_t)GEReferenceIndex::TGEN_MATRIX + subindex;
			return true;
		}
	}

	return false;
}

bool GEExpressionFunctions::parseSymbol(char *str, uint32_t &symbolValue) {
	// Mainly useful for checking memory addresses.
	return g_symbolMap->GetLabelValue(str, symbolValue);
}

uint32_t GEExpressionFunctions::getReferenceValue(uint32_t referenceIndex) {
	if (referenceIndex < 0x100) {
		uint32_t value = gpu_->GetGState().cmdmem[referenceIndex];
		// TODO: Later, support float values and similar.
		return value & 0x00FFFFFF;
	}

	// We return the matrix value as float bits, which gets interpreted correctly in the parser.
	if (referenceIndex >= (uint32_t)GEReferenceIndex::BONE_MATRIX && referenceIndex < (uint32_t)GEReferenceIndex::MATRIX_END) {
		GPUgstate state = gpu_->GetGState();
		float value;
		if (referenceIndex >= (uint32_t)GEReferenceIndex::TGEN_MATRIX) {
			value = state.tgenMatrix[referenceIndex - (uint32_t)GEReferenceIndex::TGEN_MATRIX];
		} else if (referenceIndex >= (uint32_t)GEReferenceIndex::PROJ_MATRIX) {
			value = state.projMatrix[referenceIndex - (uint32_t)GEReferenceIndex::PROJ_MATRIX];
		} else if (referenceIndex >= (uint32_t)GEReferenceIndex::VIEW_MATRIX) {
			value = state.viewMatrix[referenceIndex - (uint32_t)GEReferenceIndex::VIEW_MATRIX];
		} else if (referenceIndex >= (uint32_t)GEReferenceIndex::WORLD_MATRIX) {
			value = state.worldMatrix[referenceIndex - (uint32_t)GEReferenceIndex::WORLD_MATRIX];
		} else {
			value = state.boneMatrix[referenceIndex - (uint32_t)GEReferenceIndex::BONE_MATRIX];
		}

		uint32_t result;
		memcpy(&result, &value, sizeof(result));
		return result;
	}

	GEReferenceIndex ref = (GEReferenceIndex)referenceIndex;
	DisplayList list;
	switch (ref) {
	case GEReferenceIndex::VADDR:
		return gpu_->GetVertexAddress();
	case GEReferenceIndex::IADDR:
		return gpu_->GetIndexAddress();
	case GEReferenceIndex::OFFSET:
		// TODO: Should use an interface method, probably.
		return gstate_c.offsetAddr;
	case GEReferenceIndex::PC:
		if (gpu_->GetCurrentDisplayList(list)) {
			return list.pc;
		}
		return 0;
	case GEReferenceIndex::STALL:
		if (gpu_->GetCurrentDisplayList(list)) {
			return list.stall;
		}
		return 0;
	case GEReferenceIndex::BFLAG:
		if (gpu_->GetCurrentDisplayList(list)) {
			return list.bboxResult ? 1 : 0;
		}
		return 0;

	case GEReferenceIndex::BONE_MATRIX:
	case GEReferenceIndex::WORLD_MATRIX:
	case GEReferenceIndex::VIEW_MATRIX:
	case GEReferenceIndex::PROJ_MATRIX:
	case GEReferenceIndex::TGEN_MATRIX:
	case GEReferenceIndex::MATRIX_END:
		// Shouldn't have gotten here.
		break;
	}

	_assert_msg_(false, "Invalid reference index");
	return 0;
}

ExpressionType GEExpressionFunctions::getReferenceType(uint32_t referenceIndex) {
	if (referenceIndex < 0x100) {
		// TODO: Later, support float values and similar.
		return EXPR_TYPE_UINT;
	}

	if (referenceIndex >= (uint32_t)GEReferenceIndex::BONE_MATRIX && referenceIndex < (uint32_t)GEReferenceIndex::MATRIX_END)
		return EXPR_TYPE_FLOAT;
	return EXPR_TYPE_UINT;
}

bool GEExpressionFunctions::getMemoryValue(uint32_t address, int size, uint32_t &dest, char *error) {
	if (!Memory::IsValidRange(address, size)) {
		sprintf(error, "Invalid address or size %08x + %d", address, size);
		return false;
	}

	switch (size) {
	case 1:
		dest = Memory::Read_U8(address);
		return true;
	case 2:
		dest = Memory::Read_U16(address);
		return true;
	case 4:
		dest = Memory::Read_U32(address);
		return true;
	}

	sprintf(error, "Unexpected memory access size %d", size);
	return false;
}

bool GPUDebugInitExpression(GPUDebugInterface *g, const char *str, PostfixExpression &exp) {
	GEExpressionFunctions funcs(g);
	return initPostfixExpression(str, &funcs, exp);
}

bool GPUDebugExecExpression(GPUDebugInterface *g, PostfixExpression &exp, uint32_t &result) {
	GEExpressionFunctions funcs(g);
	return parsePostfixExpression(exp, &funcs, result);
}

bool GPUDebugExecExpression(GPUDebugInterface *g, const char *str, uint32_t &result) {
	GEExpressionFunctions funcs(g);
	return parseExpression(str, &funcs, result);
}

void GPUDebugBuffer::Allocate(u32 stride, u32 height, GEBufferFormat fmt, bool flipped, bool reversed) {
	GPUDebugBufferFormat actualFmt = GPUDebugBufferFormat(fmt);
	if (reversed && actualFmt < GPU_DBG_FORMAT_8888) {
		actualFmt |= GPU_DBG_FORMAT_REVERSE_FLAG;
	}
	Allocate(stride, height, actualFmt, flipped);
}

void GPUDebugBuffer::Allocate(u32 stride, u32 height, GPUDebugBufferFormat fmt, bool flipped) {
	if (alloc_ && stride_ == stride && height_ == height && fmt_ == fmt) {
		// Already allocated the right size.
		flipped_ = flipped;
		return;
	}

	Free();
	alloc_ = true;
	height_ = height;
	stride_ = stride;
	fmt_ = fmt;
	flipped_ = flipped;

	u32 pixelSize = PixelSize();
	data_ = new u8[pixelSize * stride * height];
}

void GPUDebugBuffer::Free() {
	if (alloc_ && data_ != NULL) {
		delete [] data_;
	}
	data_ = NULL;
}

u32 GPUDebugBuffer::PixelSize() const {
	switch (fmt_) {
	case GPU_DBG_FORMAT_8888:
	case GPU_DBG_FORMAT_8888_BGRA:
	case GPU_DBG_FORMAT_FLOAT:
	case GPU_DBG_FORMAT_24BIT_8X:
	case GPU_DBG_FORMAT_24X_8BIT:
	case GPU_DBG_FORMAT_FLOAT_DIV_256:
	case GPU_DBG_FORMAT_24BIT_8X_DIV_256:
		return 4;

	case GPU_DBG_FORMAT_888_RGB:
		return 3;

	case GPU_DBG_FORMAT_8BIT:
		return 1;

	default:
		return 2;
	}
}

u32 GPUDebugBuffer::GetRawPixel(int x, int y) const {
	if (data_ == nullptr) {
		return 0;
	}

	if (flipped_) {
		y = height_ - y - 1;
	}

	u32 pixelSize = PixelSize();
	u32 byteOffset = pixelSize * (stride_ * y + x);
	const u8 *ptr = &data_[byteOffset];

	switch (pixelSize) {
	case 4:
		return *(const u32 *)ptr;
	case 3:
		return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16);
	case 2:
		return *(const u16 *)ptr;
	case 1:
		return *(const u8 *)ptr;
	default:
		return 0;
	}
}

void GPUDebugBuffer::SetRawPixel(int x, int y, u32 c) {
	if (data_ == nullptr) {
		return;
	}

	if (flipped_) {
		y = height_ - y - 1;
	}

	u32 pixelSize = PixelSize();
	u32 byteOffset = pixelSize * (stride_ * y + x);
	u8 *ptr = &data_[byteOffset];

	switch (pixelSize) {
	case 4:
		*(u32 *)ptr = c;
		break;
	case 3:
		ptr[0] = (c >> 0) & 0xFF;
		ptr[1] = (c >> 8) & 0xFF;
		ptr[2] = (c >> 16) & 0xFF;
		break;
	case 2:
		*(u16 *)ptr = (u16)c;
		break;
	case 1:
		*ptr = (u8)c;
		break;
	default:
		break;
	}
}
