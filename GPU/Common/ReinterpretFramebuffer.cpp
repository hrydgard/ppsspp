#include <cstdarg>

#include "Common/GPU/Shader.h"
#include "Common/GPU/ShaderWriter.h"
#include "Common/Log.h"
#include "Common/GPU/thin3d.h"
#include "Core/System.h"
#include "GPU/Common/ReinterpretFramebuffer.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/TextureCacheCommon.h"

static const VaryingDef varyings[1] = {
	{ "vec2", "v_texcoord", Draw::SEM_TEXCOORD0, 0, "highp" },
};

static const SamplerDef samplers[1] = {
	{ "tex" }
};

// TODO: We could possibly have an option to preserve any extra color precision? But gonna start without it.
// Requires full size integer math. It would be possible to make a floating point-only version with lots of
// modulo and stuff, might do it one day.
Draw2DPipelineInfo GenerateReinterpretFragmentShader(ShaderWriter &writer, GEBufferFormat from, GEBufferFormat to) {
	writer.HighPrecisionFloat();

	writer.DeclareSamplers(samplers);

	writer.BeginFSMain(Slice<UniformDef>::empty(), varyings, FSFLAG_NONE);

	writer.C("  vec4 val = ").SampleTexture2D("tex", "v_texcoord.xy").C(";\n");

	switch (from) {
	case GE_FORMAT_4444:
		writer.C("  uint color = uint(val.r * 15.99) | (uint(val.g * 15.99) << 4u) | (uint(val.b * 15.99) << 8u) | (uint(val.a * 15.99) << 12u);\n");
		break;
	case GE_FORMAT_5551:
		writer.C("  uint color = uint(val.r * 31.99) | (uint(val.g * 31.99) << 5u) | (uint(val.b * 31.99) << 10u);\n");
		writer.C("  if (val.a >= 0.5) color |= 0x8000U;\n");
		break;
	case GE_FORMAT_565:
		writer.C("  uint color = uint(val.r * 31.99) | (uint(val.g * 63.99) << 5u) | (uint(val.b * 31.99) << 11u);\n");
		break;
	default:
		_assert_(false);
		break;
	}

	switch (to) {
	case GE_FORMAT_4444:
		writer.C("  vec4 outColor = vec4(float(color & 0xFU), float((color >> 4u) & 0xFU), float((color >> 8u) & 0xFU), float((color >> 12u) & 0xFU));\n");
		writer.C("  outColor *= 1.0 / 15.0;\n");
		break;
	case GE_FORMAT_5551:
		writer.C("  vec4 outColor = vec4(float(color & 0x1FU), float((color >> 5u) & 0x1FU), float((color >> 10u) & 0x1FU), 0.0);\n");
		writer.C("  outColor.rgb *= 1.0 / 31.0;\n");
		writer.C("  outColor.a = float(color >> 15);\n");
		break;
	case GE_FORMAT_565:
		writer.C("  vec4 outColor = vec4(float(color & 0x1FU), float((color >> 5u) & 0x3FU), float((color >> 11u) & 0x1FU), 1.0);\n");
		writer.C("  outColor.rb *= 1.0 / 31.0;\n");
		writer.C("  outColor.g *= 1.0 / 63.0;\n");
		break;
	default:
		_assert_(false);
		break;
	}

	writer.EndFSMain("outColor", FSFLAG_NONE);

	return Draw2DPipelineInfo{
		RASTER_COLOR,
		RASTER_COLOR,
	};
}

// Can't easily dynamically create these strings, we just pass along the pointer.
static const char *reinterpretStrings[3][3] = {
	{
		"self_reinterpret_565",
		"reinterpret_565_to_5551",
		"reinterpret_565_to_4444",
	},
	{
		"reinterpret_5551_to_565",
		"self_reinterpret_5551",
		"reinterpret_5551_to_4444",
	},
	{
		"reinterpret_4444_to_565",
		"reinterpret_4444_to_5551",
		"self_reinterpret_4444",
	},
};

void FramebufferManagerCommon::ReinterpretFramebuffer(VirtualFramebuffer *vfb, GEBufferFormat oldFormat, GEBufferFormat newFormat) {
	if (!useBufferedRendering_ || !vfb->fbo) {
		return;
	}

	_assert_(newFormat != oldFormat);
	// The caller is responsible for updating the format.
	_assert_(newFormat == vfb->fb_format);

	ShaderLanguage lang = draw_->GetShaderLanguageDesc().shaderLanguage;

	// Copy image required for now, might get rid of this later.
	bool doReinterpret = PSP_CoreParameter().compat.flags().ReinterpretFramebuffers &&
		(lang == HLSL_D3D11 || lang == GLSL_VULKAN || lang == GLSL_3xx) &&
		draw_->GetDeviceCaps().framebufferCopySupported;

	if (!doReinterpret) {
		// Fake reinterpret - just clear the way we always did on Vulkan. Just clear color and stencil.
		if (oldFormat == GE_FORMAT_565) {
			// We have to bind here instead of clear, since it can be that no framebuffer is bound.
			// The backend can sometimes directly optimize it to a clear.

			// Games that are marked as doing reinterpret just ignore this - better to keep the data than to clear.
			// Fixes #13717.
			if (!PSP_CoreParameter().compat.flags().ReinterpretFramebuffers && !PSP_CoreParameter().compat.flags().BlueToAlpha) {
				draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::KEEP, Draw::RPAction::CLEAR }, "FakeReinterpret");
				// Need to dirty anything that has command buffer dynamic state, in case we started a new pass above.
				// Should find a way to feed that information back, maybe... Or simply correct the issue in the rendermanager.
				gstate_c.Dirty(DIRTY_DEPTHSTENCIL_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE);

				if (currentRenderVfb_ != vfb) {
					// In case ReinterpretFramebuffer was called from the texture manager.
					draw_->BindFramebufferAsRenderTarget(currentRenderVfb_->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "After FakeReinterpret");
				}
			}
		}
		return;
	}

	// We only reinterpret between 16 - bit formats, for now.
	if (!IsGeBufferFormat16BitColor(oldFormat) || !IsGeBufferFormat16BitColor(newFormat)) {
		// 16->32 and 32->16 will require some more specialized shaders.
		return;
	}

	// See if we need to create a new pipeline.

	Draw2DPipeline *pipeline = reinterpretFromTo_[(int)oldFormat][(int)newFormat];
	if (!pipeline) {
		pipeline = draw2D_.Create2DPipeline([=](ShaderWriter &shaderWriter) -> Draw2DPipelineInfo {
			return GenerateReinterpretFragmentShader(shaderWriter, oldFormat, newFormat);
		});

		reinterpretFromTo_[(int)oldFormat][(int)newFormat] = pipeline;
	}

	// Copy to a temp framebuffer.
	Draw::Framebuffer *temp = GetTempFBO(TempFBO::REINTERPRET, vfb->renderWidth, vfb->renderHeight);

	// Ideally on Vulkan this should be using the original framebuffer as an input attachment, allowing it to read from
	// itself while writing.
	draw_->InvalidateCachedState();
	draw_->CopyFramebufferImage(vfb->fbo, 0, 0, 0, 0, temp, 0, 0, 0, 0, vfb->renderWidth, vfb->renderHeight, 1, Draw::FBChannel::FB_COLOR_BIT, "reinterpret_prep");

	BlitUsingRaster(temp, 0.0f, 0.0f, vfb->renderWidth, vfb->renderHeight,
		vfb->fbo, 0.0f, 0.0f, vfb->renderWidth, vfb->renderHeight, false, pipeline, "reinterpret");

	// Unbind.
	draw_->BindTexture(0, nullptr);

	shaderManager_->DirtyLastShader();
	textureCache_->ForgetLastTexture();

	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);

	if (currentRenderVfb_ != vfb) {
		// In case ReinterpretFramebuffer was called from the texture manager.
		draw_->BindFramebufferAsRenderTarget(currentRenderVfb_->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "After reinterpret");
	}
}
