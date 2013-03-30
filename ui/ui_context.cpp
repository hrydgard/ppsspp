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

	UIBegin(uishader_);
	/*
	if (uidrawbuffer_ && uishader_)
		uidrawbuffer_->Begin();
	if (uidrawbufferTop_ && uishader_)
		uidrawbufferTop_->Begin();*/
}

void UIContext::BeginNoTex()
{
	glstate.blend.enable();
	glstate.blendFunc.set(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glstate.cullFace.disable();
	glstate.depthTest.disable();
	if (uishader_)
		glsl_bind(uishader_);
	if (uitexture_)
		uitexture_->Bind(0);

	UIBegin(uishadernotex_);
	/*
	if (uidrawbuffer_ && uishader_)
		uidrawbuffer_->Begin();
	if (uidrawbufferTop_ && uishader_)
		uidrawbufferTop_->Begin();*/
}


void UIContext::RebindTexture() const
{
	if (uitexture_)
		uitexture_->Bind(0);
}

void UIContext::Flush()
{
	if (uidrawbuffer_)
	{
		uidrawbuffer_->End();
		uidrawbuffer_->Flush();
	}
	if (uidrawbufferTop_)
	{
		uidrawbufferTop_->End();
		uidrawbufferTop_->Flush();
	}
}

void UIContext::End() 
{
	UIEnd();
	Flush();
}