#include "GPU/ge_constants.h"

const char *GeBufferFormatToString(GEBufferFormat fmt) {
	switch (fmt) {
	case GE_FORMAT_4444: return "4444";
	case GE_FORMAT_5551: return "5551";
	case GE_FORMAT_565: return "565";
	case GE_FORMAT_8888: return "8888";
	case GE_FORMAT_DEPTH16: return "DEPTH16";
	default: return "N/A";
	}
}

const char *GEPaletteFormatToString(GEPaletteFormat pfmt) {
	switch (pfmt) {
	case GE_CMODE_16BIT_BGR5650: return "565";
	case GE_CMODE_16BIT_ABGR5551: return "5551";
	case GE_CMODE_16BIT_ABGR4444: return "4444";
	case GE_CMODE_32BIT_ABGR8888: return "8888";
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

const char *GeTextureFormatToString(GETextureFormat tfmt, GEPaletteFormat pfmt) {
	switch (tfmt) {
	case GE_TFMT_CLUT4:
		switch (pfmt) {
		case GE_CMODE_16BIT_BGR5650: return "CLUT4_565";
		case GE_CMODE_16BIT_ABGR5551: return "CLUT4_5551";
		case GE_CMODE_16BIT_ABGR4444: return "CLUT4_4444";
		case GE_CMODE_32BIT_ABGR8888: return "CLUT4_8888";
		default: return "N/A";
		}
	case GE_TFMT_CLUT8:
		switch (pfmt) {
		case GE_CMODE_16BIT_BGR5650: return "CLUT8_565";
		case GE_CMODE_16BIT_ABGR5551: return "CLUT8_5551";
		case GE_CMODE_16BIT_ABGR4444: return "CLUT8_4444";
		case GE_CMODE_32BIT_ABGR8888: return "CLUT8_8888";
		default: return "N/A";
		}
	case GE_TFMT_CLUT16:
		switch (pfmt) {
		case GE_CMODE_16BIT_BGR5650: return "CLUT16_565";
		case GE_CMODE_16BIT_ABGR5551: return "CLUT16_5551";
		case GE_CMODE_16BIT_ABGR4444: return "CLUT16_4444";
		case GE_CMODE_32BIT_ABGR8888: return "CLUT16_8888";
		default: return "N/A";
		}
	case GE_TFMT_CLUT32:
		switch (pfmt) {
		case GE_CMODE_16BIT_BGR5650: return "CLUT32_565";
		case GE_CMODE_16BIT_ABGR5551: return "CLUT32_5551";
		case GE_CMODE_16BIT_ABGR4444: return "CLUT32_4444";
		case GE_CMODE_32BIT_ABGR8888: return "CLUT32_8888";
		default: return "N/A";
		}
	default: return GeTextureFormatToString(tfmt);
	}
}
