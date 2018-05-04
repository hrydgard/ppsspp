#pragma once

#include <wiiu/gx2/shaders.h>

#ifdef __cplusplus
extern "C" {
#endif

extern GX2VertexShader defVShaderGX2;
extern GX2PixelShader defPShaderGX2;

extern GX2VertexShader stencilUploadVSshaderGX2;
extern GX2PixelShader stencilUploadPSshaderGX2;

extern GX2VertexShader STVshaderGX2;
extern GX2PixelShader STPshaderGX2;

extern GX2PixelShader PShaderAllGX2;

extern GX2VertexShader VShaderSWGX2;
extern GX2VertexShader VShaderHWNoSkinGX2;
extern GX2VertexShader VShaderHWSkinGX2;

#ifdef __cplusplus
}
#endif
