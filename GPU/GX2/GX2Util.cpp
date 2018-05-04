#include "ppsspp_config.h"

#include <cstdint>
#include <cfloat>
#include <vector>
#include <wiiu/gx2.h>

#include "GX2Util.h"

GX2DepthStencilControlReg StockGX2::depthStencilDisabled;
GX2DepthStencilControlReg StockGX2::depthDisabledStencilWrite;
GX2TargetChannelMaskReg StockGX2::TargetChannelMasks[16];
GX2StencilMaskReg StockGX2::stencilMask;
GX2ColorControlReg StockGX2::blendDisabledColorWrite;
GX2ColorControlReg StockGX2::blendColorDisabled;
GX2Sampler StockGX2::samplerPoint2DWrap;
GX2Sampler StockGX2::samplerLinear2DWrap;
GX2Sampler StockGX2::samplerPoint2DClamp;
GX2Sampler StockGX2::samplerLinear2DClamp;

void StockGX2::Init() {
	GX2InitColorControlReg(&blendDisabledColorWrite, GX2_LOGIC_OP_COPY, 0x00, GX2_DISABLE, GX2_ENABLE);
	GX2InitColorControlReg(&blendColorDisabled, GX2_LOGIC_OP_COPY, 0x00, GX2_DISABLE, GX2_DISABLE);
	for(int i = 0; i < countof(TargetChannelMasks); i++)
		GX2InitTargetChannelMasksReg(TargetChannelMasks + i, (GX2ChannelMask)i, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0);

	GX2InitDepthStencilControlReg(&depthStencilDisabled, GX2_DISABLE, GX2_DISABLE, GX2_COMPARE_FUNC_NEVER, GX2_DISABLE, GX2_DISABLE, GX2_COMPARE_FUNC_NEVER, GX2_STENCIL_FUNCTION_KEEP, GX2_STENCIL_FUNCTION_KEEP, GX2_STENCIL_FUNCTION_KEEP, GX2_COMPARE_FUNC_NEVER, GX2_STENCIL_FUNCTION_KEEP, GX2_STENCIL_FUNCTION_KEEP, GX2_STENCIL_FUNCTION_KEEP);
	GX2InitDepthStencilControlReg(&depthDisabledStencilWrite, GX2_DISABLE, GX2_DISABLE, GX2_COMPARE_FUNC_ALWAYS, GX2_ENABLE, GX2_ENABLE, GX2_COMPARE_FUNC_ALWAYS, GX2_STENCIL_FUNCTION_REPLACE, GX2_STENCIL_FUNCTION_REPLACE, GX2_STENCIL_FUNCTION_REPLACE, GX2_COMPARE_FUNC_ALWAYS, GX2_STENCIL_FUNCTION_REPLACE, GX2_STENCIL_FUNCTION_REPLACE, GX2_STENCIL_FUNCTION_REPLACE);
	GX2InitStencilMaskReg(&stencilMask, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

	GX2InitSampler(&samplerPoint2DWrap, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_POINT);
	GX2InitSampler(&samplerPoint2DClamp, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
	GX2InitSampler(&samplerLinear2DWrap, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_LINEAR);
	GX2InitSampler(&samplerLinear2DClamp, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);

	GX2InitSamplerBorderType(&samplerPoint2DWrap, GX2_TEX_BORDER_TYPE_WHITE);
	GX2InitSamplerBorderType(&samplerPoint2DClamp, GX2_TEX_BORDER_TYPE_WHITE);
	GX2InitSamplerBorderType(&samplerLinear2DWrap, GX2_TEX_BORDER_TYPE_WHITE);
	GX2InitSamplerBorderType(&samplerLinear2DClamp, GX2_TEX_BORDER_TYPE_WHITE);
}
