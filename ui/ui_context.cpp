#include "ui.h"
#include "ui_context.h"
#include "gfx/texture.h"
#include "gfx_es2/draw_buffer.h"
#include "gfx_es2/glsl_program.h"
#include "gfx_es2/gl_state.h"

void UIContext::Begin()
{
	glstate.blend.enable();
	glstate.blendFunc.set(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glstate.cullFace.disable();
	glstate.depthTest.disable();
	if (uishader_)
		glsl_bind(uishader_);
	if (uitexture_)
		uitexture_->Bind(0);

	UIBegin();
	/*
	if (uidrawbuffer_ && uishader_)
		uidrawbuffer_->Begin();
	if (uidrawbufferTop_ && uishader_)
		uidrawbufferTop_->Begin();*/
}

void UIContext::RebindTexture()
{
	if (uitexture_)
		uitexture_->Bind(0);
}

void UIContext::Flush()
{
	if (uidrawbuffer_)
	{
		uidrawbuffer_->End();
		if (uishader_) {
			glsl_bind(uishader_);
			uidrawbuffer_->Flush(uishader_);
		}
	}
	if (uidrawbufferTop_)
	{
		uidrawbufferTop_->End();
		if (uishader_) {
			glsl_bind(uishader_);
			uidrawbufferTop_->Flush(uishader_);
		}
	}
}

void UIContext::FlushNoTex()
{
	if (uidrawbuffer_)
	{
		uidrawbuffer_->End();
		if (uishadernotex_) {
			glsl_bind(uishadernotex_);
			uidrawbuffer_->Flush(uishadernotex_);
		}
	}
	if (uidrawbufferTop_)
	{
		uidrawbufferTop_->End();
		if (uishadernotex_) {
			glsl_bind(uishadernotex_);
			uidrawbufferTop_->Flush(uishadernotex_);
		}
	}
}

void UIContext::End() 
{
	UIEnd();
	Flush();
}