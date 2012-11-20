#pragma once

// Everything you need to draw a UI collected into a single unit that can be passed around.
// Everything forward declared so this header is safe everywhere.

struct GLSLProgram;
class Texture;
class DrawBuffer;

class UIContext {
public:
	UIContext() : uishader_(0), uitexture_(0), uidrawbuffer_(0), uidrawbufferTop_(0) {}

	void Init(const GLSLProgram *uishader, Texture *uitexture, DrawBuffer *uidrawbuffer, DrawBuffer *uidrawbufferTop) {
		uishader_ = uishader;
		uitexture_ = uitexture;
		uidrawbuffer_ = uidrawbuffer;
		uidrawbufferTop_ = uidrawbufferTop;
	}

	void Begin();
	void End();

private:
	// TODO: Collect these into a UIContext
	const GLSLProgram *uishader_;
	Texture *uitexture_;
	DrawBuffer *uidrawbuffer_;
	DrawBuffer *uidrawbufferTop_;
};
