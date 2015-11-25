#pragma once

#include <functional>
#include <string.h>
#include <string>

#include "gfx/gl_common.h"
#include "gfx_es2/gpu_features.h"

// OpenGL state cache.
// Probably only really worth it in rendering cores on weak mobile hardware.
class OpenGLState {
private:
	template<GLenum cap, bool init>
	class BoolState {
	private:
		bool v;
	public:
		BoolState() : v(init) {
			OpenGLState::state_count++;
		}

		inline void set(bool value) {
			if (value != v) {
				v = value;
				restore();
			}
		}
		inline void force(bool value) {
			bool old = v;
			set(value);
			v = old;
		}
		inline void enable() {
			set(true);
		}
		inline void disable() {
			set(false);
		}
		operator bool() const {
			return isset();
		}
		inline bool isset() {
			return v;
		}
		void restore() {
			if (v)
				glEnable(cap);
			else
				glDisable(cap);
		}
	};

#define STATE1(func, p1type, p1def) \
	class SavedState1_##func { \
		p1type p1; \
	public: \
		SavedState1_##func() : p1(p1def) { \
			OpenGLState::state_count++; \
		} \
		void set(p1type newp1) { \
			if (newp1 != p1) { \
				p1 = newp1; \
				restore(); \
			} \
		} \
		void force(p1type newp1) { \
			p1type old = p1; \
			set(newp1); \
			p1 = old; \
		} \
		inline void restore() { \
			func(p1); \
		} \
	}

#define STATE2(func, p1type, p2type, p1def, p2def) \
	class SavedState2_##func { \
		p1type p1; \
		p2type p2; \
	public: \
		SavedState2_##func() : p1(p1def), p2(p2def) { \
			OpenGLState::state_count++; \
		} \
		inline void set(p1type newp1, p2type newp2) { \
			if (newp1 != p1 || newp2 != p2) { \
				p1 = newp1; \
				p2 = newp2; \
				restore(); \
			} \
		} \
		void force(p1type newp1, p2type newp2) { \
			p1type old1 = p1; \
			p2type old2 = p2; \
			set(newp1, newp2); \
			p1 = old1; \
			p2 = old2; \
		} \
		inline void restore() { \
			func(p1, p2); \
		} \
	}

#define STATE3(func, p1type, p2type, p3type, p1def, p2def, p3def) \
	class SavedState3_##func { \
		p1type p1; \
		p2type p2; \
		p3type p3; \
	public: \
		SavedState3_##func() : p1(p1def), p2(p2def), p3(p3def) { \
			OpenGLState::state_count++; \
		} \
		inline void set(p1type newp1, p2type newp2, p3type newp3) { \
			if (newp1 != p1 || newp2 != p2 || newp3 != p3) { \
				p1 = newp1; \
				p2 = newp2; \
				p3 = newp3; \
				restore(); \
			} \
		} \
		void force(p1type newp1, p2type newp2, p3type newp3) { \
			p1type old1 = p1; \
			p2type old2 = p2; \
			p3type old3 = p3; \
			set(newp1, newp2, newp3); \
			p1 = old1; \
			p2 = old2; \
			p3 = old3; \
		} \
		inline void restore() { \
			func(p1, p2, p3); \
		} \
	}

	#define STATE4(func, p1type, p2type, p3type, p4type, p1def, p2def, p3def, p4def) \
	class SavedState4_##func { \
		p1type p1; \
		p2type p2; \
		p3type p3; \
		p4type p4; \
	public: \
		SavedState4_##func() : p1(p1def), p2(p2def), p3(p3def), p4(p4def) { \
			OpenGLState::state_count++; \
		} \
		inline void set(p1type newp1, p2type newp2, p3type newp3, p4type newp4) { \
			if (newp1 != p1 || newp2 != p2 || newp3 != p3 || newp4 != p4) { \
				p1 = newp1; \
				p2 = newp2; \
				p3 = newp3; \
				p4 = newp4; \
				restore(); \
			} \
		} \
		void force(p1type newp1, p2type newp2, p3type newp3, p4type newp4) { \
			p1type old1 = p1; \
			p2type old2 = p2; \
			p3type old3 = p3; \
			p4type old4 = p4; \
			set(newp1, newp2, newp3, newp4); \
			p1 = old1; \
			p2 = old2; \
			p3 = old3; \
			p4 = old4; \
		} \
		inline void restore() { \
			func(p1, p2, p3, p4); \
		} \
	}

#define STATEFLOAT4(func, def) \
	class SavedState4_##func { \
		float p[4]; \
	public: \
		SavedState4_##func() { \
			for (int i = 0; i < 4; i++) {p[i] = def;} \
			OpenGLState::state_count++; \
		} \
		inline void set(const float v[4]) { \
			if (memcmp(p, v, sizeof(float) * 4)) { \
				memcpy(p, v, sizeof(float) * 4); \
				restore(); \
			} \
		} \
		void force(const float v[4]) { \
			float old[4]; \
			memcpy(old, p, sizeof(float) * 4); \
			set(v); \
			memcpy(p, old, sizeof(float) * 4); \
		} \
		inline void restore() { \
			func(p[0], p[1], p[2], p[3]); \
		} \
	}

#define STATEBIND(func, target) \
	class SavedBind_##func_##target { \
		GLuint val_; \
	public: \
		SavedBind_##func_##target() { \
			val_ = 0; \
			OpenGLState::state_count++; \
		} \
		inline void bind(GLuint val) { \
			if (val_ != val) { \
				val_ = val; \
				restore(); \
			} \
		} \
		inline void force(GLuint val) { \
			GLuint old = val_; \
			bind(val); \
			val_ = old; \
		} \
		inline void unbind() { \
			bind(0); \
		} \
		inline void restore() { \
			func(target, val_); \
		} \
	}

public:
	static int state_count;
	OpenGLState() {}
	void Restore();

	// When adding a state here, don't forget to add it to OpenGLState::Restore() too

	// Blending
	BoolState<GL_BLEND, false> blend;
	STATE4(glBlendFuncSeparate, GLenum, GLenum, GLenum, GLenum, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) blendFuncSeparate;

	// On OpenGL ES, using minmax blend requires glBlendEquationEXT (in theory at least but I don't think it's true in practice)
	STATE2(glBlendEquationSeparate, GLenum, GLenum, GL_FUNC_ADD, GL_FUNC_ADD) blendEquationSeparate;
	STATEFLOAT4(glBlendColor, 1.0f) blendColor;

	// Logic Ops. Not available on OpenGL ES at all.
#if !defined(USING_GLES2)
	BoolState<GL_COLOR_LOGIC_OP, false> colorLogicOp;
	STATE1(glLogicOp, GLenum, GL_COPY) logicOp;
#endif

	// Dither
	BoolState<GL_DITHER, false> dither;

	// Cull Face
	BoolState<GL_CULL_FACE, false> cullFace;
	STATE1(glCullFace, GLenum, GL_FRONT) cullFaceMode;
	STATE1(glFrontFace, GLenum, GL_CCW) frontFace;

	// Depth Test
	BoolState<GL_DEPTH_TEST, false> depthTest;
#if defined(USING_GLES2)
	STATE2(glDepthRangef, float, float, 0.f, 1.f) depthRange;
#else
	STATE2(glDepthRange, double, double, 0.0, 1.0) depthRange;
#endif
	STATE1(glDepthFunc, GLenum, GL_LESS) depthFunc;
	STATE1(glDepthMask, GLboolean, GL_TRUE) depthWrite;

	// Color Mask
	STATE4(glColorMask, bool, bool, bool, bool, true, true, true, true) colorMask;

	// Viewport
	STATE4(glViewport, GLint, GLint, GLsizei, GLsizei, 0, 0, 128, 128) viewport;

	// Scissor Test
	BoolState<GL_SCISSOR_TEST, false> scissorTest;
	STATE4(glScissor, GLint, GLint, GLsizei, GLsizei, 0, 0, 128, 128) scissorRect;

	// Stencil Test
	BoolState<GL_STENCIL_TEST, false> stencilTest;
	STATE3(glStencilOp, GLenum, GLenum, GLenum, GL_KEEP, GL_KEEP, GL_KEEP) stencilOp;
	STATE3(glStencilFunc, GLenum, GLint, GLuint, GL_ALWAYS, 0, 0xFF) stencilFunc;
	STATE1(glStencilMask, GLuint, 0xFF) stencilMask;

	STATEBIND(glBindBuffer, GL_ARRAY_BUFFER) arrayBuffer;
	STATEBIND(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER) elementArrayBuffer;
};

#undef STATE1
#undef STATE2

extern OpenGLState glstate;
