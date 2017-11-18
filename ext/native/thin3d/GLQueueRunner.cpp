#include "GLQueueRunner.h"
#include "GLRenderManager.h"
#include "gfx_es2/gpu_features.h"
#include "math/dataconv.h"

void GLQueueRunner::CreateDeviceObjects() {

}

void GLQueueRunner::DestroyDeviceObjects() {

}

void GLQueueRunner::RunInitSteps(const std::vector<GLRInitStep> &steps) {

}

void GLQueueRunner::RunSteps(const std::vector<GLRStep *> &steps) {
	for (int i = 0; i < steps.size(); i++) {
		const GLRStep &step = *steps[i];
		switch (step.stepType) {
		case GLRStepType::RENDER:
			PerformRenderPass(step);
			break;
		case GLRStepType::COPY:
			PerformCopy(step);
			break;
		case GLRStepType::BLIT:
			PerformBlit(step);
			break;
		case GLRStepType::READBACK:
			PerformReadback(step);
			break;
		case GLRStepType::READBACK_IMAGE:
			PerformReadbackImage(step);
			break;
		}
		delete steps[i];
	}
}

void GLQueueRunner::LogSteps(const std::vector<GLRStep *> &steps) {

}


void GLQueueRunner::PerformBlit(const GLRStep &step) {
}

void GLQueueRunner::PerformRenderPass(const GLRStep &step) {
	// Don't execute empty renderpasses.
	if (step.commands.empty() && step.render.color == GLRRenderPassAction::KEEP && step.render.depthStencil == GLRRenderPassAction::KEEP) {
		// Nothing to do.
		return;
	}

	// This is supposed to bind a vulkan render pass to the command buffer.
	PerformBindFramebufferAsRenderTarget(step);

	int curWidth = step.render.framebuffer ? step.render.framebuffer->width : 0; // vulkan_->GetBackbufferWidth();
	int curHeight = step.render.framebuffer ? step.render.framebuffer->height : 0; // vulkan_->GetBackbufferHeight();
	
	GLRFramebuffer *fb = step.render.framebuffer;

	GLint activeTexture = GL_TEXTURE0;

	auto &commands = step.commands;
	for (const auto &c : commands) {
		switch (c.cmd) {
		case GLRRenderCommand::DEPTH:
			if (c.depth.enabled) {
				glEnable(GL_DEPTH_TEST);
				glDepthMask(c.depth.write);
				glDepthFunc(c.depth.func);
			} else {
				glDisable(GL_DEPTH_TEST);
			}
			break;
		case GLRRenderCommand::BLEND:
			if (c.blend.enabled) {
				glEnable(GL_BLEND);
				glBlendEquationSeparate(c.blend.funcColor, c.blend.funcAlpha);
				glBlendFuncSeparate(c.blend.srcColor, c.blend.dstColor, c.blend.srcAlpha, c.blend.dstAlpha);
			} else {
				glDisable(GL_BLEND);
			}
			break;
		case GLRRenderCommand::CLEAR:
			if (c.clear.clearMask & GLR_ASPECT_COLOR) {
				float color[4];
				Uint8x4ToFloat4(color, c.clear.clearColor);
				glClearColor(color[0], color[1], color[2], color[3]);
			}
			if (c.clear.clearMask & GLR_ASPECT_DEPTH) {
				glClearDepth(c.clear.clearZ);
			}
			if (c.clear.clearMask & GLR_ASPECT_STENCIL) {
				glClearStencil(c.clear.clearStencil);
			}
			break;
		case GLRRenderCommand::BLENDCOLOR:
			glBlendColor(c.blendColor.color[0], c.blendColor.color[1], c.blendColor.color[2], c.blendColor.color[3]);
			break;
		case GLRRenderCommand::VIEWPORT:
			// TODO: Support FP viewports through glViewportArrays
			glViewport((GLint)c.viewport.vp.x, (GLint)c.viewport.vp.y, (GLsizei)c.viewport.vp.w, (GLsizei)c.viewport.vp.h);
			glDepthRange(c.viewport.vp.minZ, c.viewport.vp.maxZ);
			break;
		case GLRRenderCommand::SCISSOR:
			glScissor(c.scissor.rc.x, c.scissor.rc.y, c.scissor.rc.w, c.scissor.rc.h);
			break;
		case GLRRenderCommand::UNIFORM4F:
			switch (c.uniform4.count) {
			case 1:
				glUniform1f(c.uniform4.loc, c.uniform4.v[0]);
				break;
			case 2:
				glUniform2fv(c.uniform4.loc, 1, c.uniform4.v);
				break;
			case 3:
				glUniform3fv(c.uniform4.loc, 1, c.uniform4.v);
				break;
			case 4:
				glUniform4fv(c.uniform4.loc, 1, c.uniform4.v);
				break;
			}
			break;
		case GLRRenderCommand::UNIFORMMATRIX:
			glUniformMatrix4fv(c.uniformMatrix4.loc, 1, false, c.uniformMatrix4.m);
			break;
		case GLRRenderCommand::STENCIL:
			glStencilFunc(c.stencil.stencilFunc, c.stencil.stencilRef, c.stencil.stencilCompareMask);
			glStencilOp(c.stencil.stencilSFail, c.stencil.stencilZFail, c.stencil.stencilPass);
			glStencilMask(c.stencil.stencilWriteMask);
			break;
		case GLRRenderCommand::BINDTEXTURE:
		{
			GLint target = c.texture.slot;
			if (target != activeTexture) {
				glActiveTexture(target);
				activeTexture = target;
			}
			glBindTexture(GL_TEXTURE_2D, c.texture.texture);
			break;
		}
		case GLRRenderCommand::DRAW:
			glDrawArrays(c.draw.mode, c.draw.first, c.draw.count);
			break;
		case GLRRenderCommand::DRAW_INDEXED:
			if (c.drawIndexed.instances == 1) {
				glDrawElements(c.drawIndexed.mode, c.drawIndexed.count, c.drawIndexed.indexType, c.drawIndexed.indices);
			}
			break;
		}
	}
	if (activeTexture != GL_TEXTURE0)
		glActiveTexture(GL_TEXTURE0);
}

void GLQueueRunner::PerformCopy(const GLRStep &step) {
	GLuint srcTex = 0;
	GLuint dstTex = 0;
	GLuint target = GL_TEXTURE_2D;

	const GLRect2D &srcRect = step.copy.srcRect;
	const GLOffset2D &dstPos = step.copy.dstPos;

	GLRFramebuffer *src = step.copy.src;
	GLRFramebuffer *dst = step.copy.src;

	int srcLevel = 0;
	int dstLevel = 0;
	int srcZ = 0;
	int dstZ = 0;
	int depth = 1;

	switch (step.copy.aspectMask) {
	case GLR_ASPECT_COLOR:
		srcTex = src->color.texture;
		dstTex = dst->color.texture;
		break;
	case GLR_ASPECT_DEPTH:
		target = GL_RENDERBUFFER;
		srcTex = src->depth.texture;
		dstTex = src->depth.texture;
		break;
	}
#if defined(USING_GLES2)
#ifndef IOS
	glCopyImageSubDataOES(
		srcTex, target, srcLevel, srcRect.x, srcRect.y, srcZ,
		dstTex, target, dstLevel, dstPos.x, dstPos.y, dstZ,
		srcRect.w, srcRect.h, depth);
#endif
#else
	if (gl_extensions.ARB_copy_image) {
		glCopyImageSubData(
			srcTex, target, srcLevel, srcRect.x, srcRect.y, srcZ,
			dstTex, target, dstLevel, dstPos.x, dstPos.y, dstZ,
			srcRect.w, srcRect.h, depth);
	} else if (gl_extensions.NV_copy_image) {
		// Older, pre GL 4.x NVIDIA cards.
		glCopyImageSubDataNV(
			srcTex, target, srcLevel, srcRect.x, srcRect.y, srcZ,
			dstTex, target, dstLevel, dstPos.x, dstPos.y, dstZ,
			srcRect.w, srcRect.h, depth);
	}
#endif
}

void GLQueueRunner::PerformBindFramebufferAsRenderTarget(const GLRStep &pass) {
	
}

void GLQueueRunner::CopyReadbackBuffer(int width, int height, Draw::DataFormat srcFormat, Draw::DataFormat destFormat, int pixelStride, uint8_t *pixels) {

}
