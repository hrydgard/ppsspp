#pragma once

#include "Common/Math/CrossSIMD.h"
#include "GPU/Common/VertexDecoderCommon.h"

// Reads decoded vertex formats in a convenient way. For software transform and debugging.
class VertexReader {
public:
	VertexReader(const u8 *base, const DecVtxFormat &decFmt, int vtype) : base_(base), data_(base), decFmt_(decFmt), vtype_(vtype) {}

	Vec4F32 ReadPosF32() const {
		// Only DEC_FLOAT_3 is supported.
		const float *f = (const float *)(data_ + decFmt_.posoff);
		return Vec4F32::Load(f);
	}

	// TODO: Only reason we might want to still keep the separation of ReadPosThrough and ReadPosNonThrough is
	// in case we need to do some rounding for through mode. I doubt it's needed though.
	void ReadPosAuto(float pos[3]) const {
		// Only DEC_FLOAT_3 is supported.
		const float *f = (const float *)(data_ + decFmt_.posoff);
		pos[0] = f[0];
		pos[1] = f[1];
		if (!isThrough()) {
			pos[2] = f[2];
		} else {
			// Integer value passed in a float. Clamped to 0, 65535.
			pos[2] = f[2];
		}
	}

	void ReadPosThrough(float pos[3]) const {
		// Only DEC_FLOAT_3 is supported.
		const float *f = (const float *)(data_ + decFmt_.posoff);
		pos[0] = f[0];
		pos[1] = f[1];
		// Integer value passed in a float. Clamped to 0, 65535 (but where?)
		pos[2] = f[2];
	}

	void ReadPosNonThrough(float pos[3]) const {
		// Only DEC_FLOAT_3 is supported.
		const float *f = (const float *)(data_ + decFmt_.posoff);
		memcpy(pos, f, 12);
	}

	// NOTE: The W component of the return value becomes undefined.
	Vec4F32 ReadNrmF32() const {
		switch (decFmt_.nrmfmt) {
		case DEC_FLOAT_3:
			return Vec4F32::Load((const float *)(data_ + decFmt_.nrmoff));
		case DEC_S16_3:
			return Vec4F32::LoadS16Norm((const s16 *)(data_ + decFmt_.nrmoff));
		case DEC_S8_3:
			return Vec4F32::LoadS8Norm((const s8 *)(data_ + decFmt_.nrmoff));
		default:
			return Vec4F32::Zero();
		}
	}

	void ReadNrm(float nrm[3]) const {
		switch (decFmt_.nrmfmt) {
		case DEC_FLOAT_3:
			//memcpy(nrm, data_ + decFmt_.nrmoff, 12);
		{
			const float *f = (const float *)(data_ + decFmt_.nrmoff);
			for (int i = 0; i < 3; i++)
				nrm[i] = f[i];
		}
		break;
		case DEC_S16_3:
		{
			const s16 *s = (const s16 *)(data_ + decFmt_.nrmoff);
			for (int i = 0; i < 3; i++)
				nrm[i] = s[i] * (1.f / 32768.f);
		}
		break;
		case DEC_S8_3:
		{
			const s8 *b = (const s8 *)(data_ + decFmt_.nrmoff);
			for (int i = 0; i < 3; i++)
				nrm[i] = b[i] * (1.f / 128.f);
		}
		break;
		default:
			memset(nrm, 0, sizeof(float) * 3);
			break;
		}
	}

	void ReadUV(float uv[2]) const {
		// Only DEC_FLOAT_2 is supported.
		const float *f = (const float *)(data_ + decFmt_.uvoff);
		uv[0] = f[0];
		uv[1] = f[1];
	}

	Vec4F32 ReadUVF32() const {
		// Only DEC_FLOAT_2 is supported.
		// The upper two lanes of the return value become undefined.
		const float *f = (const float *)(data_ + decFmt_.uvoff);
		return Vec4F32::Load2(f);
	}

	void ReadColor0(float color[4]) const {
		switch (decFmt_.c0fmt) {
		case DEC_U8_4:
			Vec4F32::LoadU8Norm(data_ + decFmt_.c0off).Store(color);
			break;
		case DEC_FLOAT_4:
			memcpy(color, data_ + decFmt_.c0off, 16);
			break;
		default:
			memset(color, 0, sizeof(float) * 4);
			break;
		}
	}

	Vec4F32 ReadColorF32() const {
		switch (decFmt_.c0fmt) {
		case DEC_U8_4:
			return Vec4F32::LoadU8Norm(data_ + decFmt_.c0off);
		case DEC_FLOAT_4:
			return Vec4F32::Load((const float *)(data_ + decFmt_.c0off));
		default:
			return Vec4F32::Zero();
		}
	}

	u32 ReadColor0_8888() const {
		switch (decFmt_.c0fmt) {
		case DEC_U8_4:
		{
			const u8 *b = (const u8 *)(data_ + decFmt_.c0off);
			u32 value;
			memcpy(&value, b, 4);
			return value;
		}
		break;
		case DEC_FLOAT_4:
		{
			const float *f = (const float *)(data_ + decFmt_.c0off);
			return Float4ToUint8x4_NoClamp(f);
		}
		break;
		default:
			return 0;
		}
	}

	void ReadColor1(float color[3]) const {
		switch (decFmt_.c1fmt) {
		case DEC_U8_4:
		{
			const u8 *b = (const u8 *)(data_ + decFmt_.c1off);
			for (int i = 0; i < 3; i++)
				color[i] = b[i] * (1.f / 255.f);
		}
		break;
		case DEC_FLOAT_4:
			memcpy(color, data_ + decFmt_.c1off, 12);
			break;
		default:
			memset(color, 0, sizeof(float) * 3);
			break;
		}
	}

	void ReadWeights(float weights[8]) const {
		const float *f = (const float *)(data_ + decFmt_.w0off);
		const u8 *b = (const u8 *)(data_ + decFmt_.w0off);
		const u16 *s = (const u16 *)(data_ + decFmt_.w0off);
		switch (decFmt_.w0fmt) {
		case DEC_FLOAT_1:
		case DEC_FLOAT_2:
		case DEC_FLOAT_3:
		case DEC_FLOAT_4:
			for (int i = 0; i <= decFmt_.w0fmt - DEC_FLOAT_1; i++)
				weights[i] = f[i];
			break;
		case DEC_U8_1: weights[0] = b[0] * (1.f / 128.f); break;
		case DEC_U8_2: for (int i = 0; i < 2; i++) weights[i] = b[i] * (1.f / 128.f); break;
		case DEC_U8_3: for (int i = 0; i < 3; i++) weights[i] = b[i] * (1.f / 128.f); break;
		case DEC_U8_4: for (int i = 0; i < 4; i++) weights[i] = b[i] * (1.f / 128.f); break;
		case DEC_U16_1: weights[0] = s[0] * (1.f / 32768.f); break;
		case DEC_U16_2: for (int i = 0; i < 2; i++) weights[i] = s[i] * (1.f / 32768.f); break;
		case DEC_U16_3: for (int i = 0; i < 3; i++) weights[i] = s[i] * (1.f / 32768.f); break;
		case DEC_U16_4: for (int i = 0; i < 4; i++) weights[i] = s[i] * (1.f / 32768.f); break;
		default:
			ERROR_LOG_REPORT_ONCE(fmtw0, Log::G3D, "Reader: Unsupported W0 Format %d", decFmt_.w0fmt);
			memset(weights, 0, sizeof(float) * 8);
			break;
		}

		f = (const float *)(data_ + decFmt_.w1off);
		b = (const u8 *)(data_ + decFmt_.w1off);
		s = (const u16 *)(data_ + decFmt_.w1off);
		switch (decFmt_.w1fmt) {
		case 0:
			// It's fine for there to be w0 weights but not w1.
			break;
		case DEC_FLOAT_1:
		case DEC_FLOAT_2:
		case DEC_FLOAT_3:
		case DEC_FLOAT_4:
			for (int i = 0; i <= decFmt_.w1fmt - DEC_FLOAT_1; i++)
				weights[i + 4] = f[i];
			break;
		case DEC_U8_1: weights[4] = b[0] * (1.f / 128.f); break;
		case DEC_U8_2: for (int i = 0; i < 2; i++) weights[i + 4] = b[i] * (1.f / 128.f); break;
		case DEC_U8_3: for (int i = 0; i < 3; i++) weights[i + 4] = b[i] * (1.f / 128.f); break;
		case DEC_U8_4: for (int i = 0; i < 4; i++) weights[i + 4] = b[i] * (1.f / 128.f); break;
		case DEC_U16_1: weights[4] = s[0] * (1.f / 32768.f); break;
		case DEC_U16_2: for (int i = 0; i < 2; i++) weights[i + 4] = s[i] * (1.f / 32768.f); break;
		case DEC_U16_3: for (int i = 0; i < 3; i++) weights[i + 4] = s[i] * (1.f / 32768.f); break;
		case DEC_U16_4: for (int i = 0; i < 4; i++) weights[i + 4] = s[i] * (1.f / 32768.f); break;
		default:
			memset(weights + 4, 0, sizeof(float) * 4);
			break;
		}
	}

	bool hasColor0() const { return decFmt_.c0fmt != 0; }
	bool hasColor1() const { return decFmt_.c1fmt != 0; }
	bool hasNormal() const { return decFmt_.nrmfmt != 0; }
	bool hasUV() const { return decFmt_.uvfmt != 0; }
	bool isThrough() const { return (vtype_ & GE_VTYPE_THROUGH) != 0; }
	void Goto(int index) {
		data_ = base_ + index * decFmt_.stride;
	}

private:
	const u8 *base_;
	const u8 *data_;
	DecVtxFormat decFmt_;
	int vtype_;
};
// Debugging utilities
void PrintDecodedVertex(const VertexReader &vtx);
