#include "Common/GPU/thin3d.h"
#include "GPU/GPUCommonHW.h"


GPUCommonHW::GPUCommonHW(GraphicsContext *gfxCtx, Draw::DrawContext *draw) : GPUCommon(gfxCtx, draw) {

}

GPUCommonHW::~GPUCommonHW() {}

void GPUCommonHW::PreExecuteOp(u32 op, u32 diff) {
	CheckFlushOp(op >> 24, diff);
}
