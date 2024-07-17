#include "ppsspp_config.h"

#include <cstdint>
#include <cfloat>
#include <vector>
#include <string>
#include <d3d11.h>
#include <D3Dcompiler.h>

#if PPSSPP_PLATFORM(UWP)
#define ptr_D3DCompile D3DCompile
#else
#include "Common/GPU/D3D11/D3D11Loader.h"
#endif

#include "Common/CommonFuncs.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"

#include "D3D11Util.h"

std::vector<uint8_t> CompileShaderToBytecodeD3D11(const char *code, size_t codeSize, const char *target, UINT flags) {
	ID3DBlob *compiledCode = nullptr;
	ID3DBlob *errorMsgs = nullptr;
	HRESULT result = ptr_D3DCompile(code, codeSize, nullptr, nullptr, nullptr, "main", target, flags, 0, &compiledCode, &errorMsgs);
	std::string errors;
	if (errorMsgs) {
		errors = std::string((const char *)errorMsgs->GetBufferPointer(), errorMsgs->GetBufferSize());
		std::string numberedCode = LineNumberString(code);
		if (SUCCEEDED(result)) {
			std::vector<std::string_view> lines;
			SplitString(errors, '\n', lines);
			for (auto &line : lines) {
				auto trimmed = StripSpaces(line);
				// Ignore the useless warning about taking the power of negative numbers.
				if (trimmed.find("pow(f, e) will not work for negative f") != std::string::npos) {
					continue;
				}
				if (trimmed.size() > 1) {  // ignore single nulls, not sure how they appear.
					WARN_LOG(Log::G3D, "%.*s", (int)trimmed.length(), trimmed.data());
				}
			}
		} else {
			ERROR_LOG(Log::G3D, "%s: %s\n\n%s", "errors", errors.c_str(), numberedCode.c_str());
		}
		OutputDebugStringA(errors.c_str());
		OutputDebugStringA(numberedCode.c_str());
		errorMsgs->Release();
	}
	if (compiledCode) {
		// Success!
		const uint8_t *buf = (const uint8_t *)compiledCode->GetBufferPointer();
		std::vector<uint8_t> compiled = std::vector<uint8_t>(buf, buf +  compiledCode->GetBufferSize());
		_assert_(compiled.size() != 0);
		compiledCode->Release();
		return compiled;
	}
	return std::vector<uint8_t>();
}

ID3D11VertexShader *CreateVertexShaderD3D11(ID3D11Device *device, const char *code, size_t codeSize, std::vector<uint8_t> *byteCodeOut, D3D_FEATURE_LEVEL featureLevel, UINT flags) {
	const char *profile = featureLevel <= D3D_FEATURE_LEVEL_9_3 ? "vs_4_0_level_9_1" : "vs_4_0";
	std::vector<uint8_t> byteCode = CompileShaderToBytecodeD3D11(code, codeSize, profile, flags);
	if (byteCode.empty())
		return nullptr;

	ID3D11VertexShader *vs;
	device->CreateVertexShader(byteCode.data(), byteCode.size(), nullptr, &vs);
	if (byteCodeOut)
		*byteCodeOut = byteCode;
	return vs;
}

ID3D11PixelShader *CreatePixelShaderD3D11(ID3D11Device *device, const char *code, size_t codeSize, D3D_FEATURE_LEVEL featureLevel, UINT flags) {
	const char *profile = featureLevel <= D3D_FEATURE_LEVEL_9_3 ? "ps_4_0_level_9_1" : "ps_4_0";
	std::vector<uint8_t> byteCode = CompileShaderToBytecodeD3D11(code, codeSize, profile, flags);
	if (byteCode.empty())
		return nullptr;

	ID3D11PixelShader *ps;
	device->CreatePixelShader(byteCode.data(), byteCode.size(), nullptr, &ps);
	return ps;
}

ID3D11ComputeShader *CreateComputeShaderD3D11(ID3D11Device *device, const char *code, size_t codeSize, D3D_FEATURE_LEVEL featureLevel, UINT flags) {
	if (featureLevel <= D3D_FEATURE_LEVEL_9_3)
		return nullptr;
	std::vector<uint8_t> byteCode = CompileShaderToBytecodeD3D11(code, codeSize, "cs_4_0", flags);
	if (byteCode.empty())
		return nullptr;

	ID3D11ComputeShader *cs;
	device->CreateComputeShader(byteCode.data(), byteCode.size(), nullptr, &cs);
	return cs;
}

ID3D11GeometryShader *CreateGeometryShaderD3D11(ID3D11Device *device, const char *code, size_t codeSize, D3D_FEATURE_LEVEL featureLevel, UINT flags) {
	if (featureLevel <= D3D_FEATURE_LEVEL_9_3)
		return nullptr;
	std::vector<uint8_t> byteCode = CompileShaderToBytecodeD3D11(code, codeSize, "gs_5_0", flags);
	if (byteCode.empty())
		return nullptr;

	ID3D11GeometryShader *gs;
	device->CreateGeometryShader(byteCode.data(), byteCode.size(), nullptr, &gs);
	return gs;
}

void StockObjectsD3D11::Create(ID3D11Device *device) {
	D3D11_BLEND_DESC blend_desc{};
	blend_desc.RenderTarget[0].BlendEnable = false;
	blend_desc.IndependentBlendEnable = false;
	for (int i = 0; i < 16; i++) {
		blend_desc.RenderTarget[0].RenderTargetWriteMask = i;
		ASSERT_SUCCESS(device->CreateBlendState(&blend_desc, &blendStateDisabledWithColorMask[i]));
	}

	D3D11_DEPTH_STENCIL_DESC depth_desc{};
	depth_desc.DepthEnable = FALSE;
	ASSERT_SUCCESS(device->CreateDepthStencilState(&depth_desc, &depthStencilDisabled));
	depth_desc.StencilEnable = TRUE;
	depth_desc.StencilReadMask = 0xFF;
	depth_desc.StencilWriteMask = 0xFF;
	depth_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
	depth_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_REPLACE;
	depth_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_REPLACE;
	depth_desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	depth_desc.BackFace = depth_desc.FrontFace;
	ASSERT_SUCCESS(device->CreateDepthStencilState(&depth_desc, &depthDisabledStencilWrite));

	D3D11_RASTERIZER_DESC raster_desc{};
	raster_desc.FillMode = D3D11_FILL_SOLID;
	raster_desc.CullMode = D3D11_CULL_NONE;
	raster_desc.ScissorEnable = FALSE;
	raster_desc.DepthClipEnable = TRUE;  // the default! FALSE is unsupported on D3D11 level 9
	ASSERT_SUCCESS(device->CreateRasterizerState(&raster_desc, &rasterStateNoCull));

	D3D11_SAMPLER_DESC sampler_desc{};
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	for (int i = 0; i < 4; i++)
		sampler_desc.BorderColor[i] = 1.0f;
	sampler_desc.MinLOD = -FLT_MAX;
	sampler_desc.MaxLOD = FLT_MAX;
	sampler_desc.MipLODBias = 0.0f;
	sampler_desc.MaxAnisotropy = 1;
	ASSERT_SUCCESS(device->CreateSamplerState(&sampler_desc, &samplerPoint2DWrap));
	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	ASSERT_SUCCESS(device->CreateSamplerState(&sampler_desc, &samplerLinear2DWrap));
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	ASSERT_SUCCESS(device->CreateSamplerState(&sampler_desc, &samplerPoint2DClamp));
	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	ASSERT_SUCCESS(device->CreateSamplerState(&sampler_desc, &samplerLinear2DClamp));
}

void StockObjectsD3D11::Destroy() {
	for (int i = 0; i < 16; i++) {
		blendStateDisabledWithColorMask[i]->Release();
	}
	depthStencilDisabled->Release();
	depthDisabledStencilWrite->Release();
	rasterStateNoCull->Release();
	samplerPoint2DWrap->Release();
	samplerLinear2DWrap->Release();
	samplerPoint2DClamp->Release();
	samplerLinear2DClamp->Release();
}

StockObjectsD3D11 stockD3D11;
