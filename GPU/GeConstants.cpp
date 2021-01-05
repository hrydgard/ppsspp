#include "GPU/ge_constants.h"

const char *GeBufferFormatToString(GEBufferFormat fmt) {
	switch (fmt) {
	case GE_FORMAT_4444: return "4444";
	case GE_FORMAT_5551: return "5551";
	case GE_FORMAT_565: return "565";
	case GE_FORMAT_8888: return "8888";
	default: return "N/A";
	}
}

const char *GeTextureFormatToString(GETextureFormat fmt) {
	switch (fmt) {
	case GE_TFMT_5650: return "565";
	case GE_TFMT_5551: return "5551";
	case GE_TFMT_4444: return "4444";
	case GE_TFMT_8888: return "8888";
	case GE_TFMT_CLUT4: return "CLUT4";
	case GE_TFMT_CLUT8: return "CLUT8";
	case GE_TFMT_CLUT16: return "CLUT16";
	case GE_TFMT_CLUT32: return "CLUT32";
	case GE_TFMT_DXT1: return "DXT1";
	case GE_TFMT_DXT3: return "DXT3";
	case GE_TFMT_DXT5: return "DXT5";
	default: return "N/A";
	}
}
