#pragma once

#if defined(ANDROID) || defined(BLACKBERRY)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#else
#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#endif
#include <functional>
#include <string.h>


// OpenGL state cache. Should convert all code to use this instead of directly calling glEnable etc,
// as GL state changes can be expensive on some hardware.
class OpenGLState
{
private:
	template<GLenum cap, bool init>
	class BoolState {
		bool _value;
	public:
		BoolState() : _value(init) {}

		inline void set(bool value) {
			if(value && value != _value) {
				_value = value;
				glEnable(cap);
			}
			if(!value && value != _value) {
				_value = value;
				glDisable(cap);
			}
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
			return _value;
		}
		void restore() {
			if(_value)
				glEnable(cap);
			else
				glDisable(cap);
		}
	};

#define STATE1(func, p1type, p1def) \
	class SavedState1_##func { \
		p1type p1; \
	public: \
		SavedState1_##func() : p1(p1def) {}; \
		void set(p1type newp1) { \
			if(newp1 != p1) { \
				p1 = newp1; \
				func(p1); \
			} \
		} \
		void restore() { \
			func(p1); \
		} \
	}

#define STATE2(func, p1type, p2type, p1def, p2def) \
	class SavedState2_##func { \
		p1type p1; \
		p2type p2; \
	public: \
		SavedState2_##func() : p1(p1def), p2(p2def) {}; \
		inline void set(p1type newp1, p2type newp2) { \
			if(newp1 != p1 || newp2 != p2) { \
				p1 = newp1; \
				p2 = newp2; \
				func(p1, p2); \
			} \
		} \
		inline void restore() { \
			func(p1, p2); \
		} \
	}

#define STATEFLOAT4(func, def) \
	class SavedState4_##func { \
		float p[4]; \
	public: \
		SavedState4_##func() { \
			for (int i = 0; i < 4; i++) {p[i] = def;} \
		}; \
		inline void set(const float v[4]) { \
			if(memcmp(p,v,sizeof(float)*4)) { \
				memcpy(p,v,sizeof(float)*4); \
				func(p[0], p[1], p[2], p[3]); \
			} \
		} \
		inline void restore() { \
			func(p[0], p[1], p[2], p[3]); \
		} \
	}

	bool initialized;

public:
	OpenGLState() : initialized(false) {}
	void Initialize();
	void Restore();

	BoolState<GL_BLEND, false> blend;
	STATE2(glBlendFunc, GLenum, GLenum, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) blendFunc;
	STATE1(glBlendEquation, GLenum, GL_FUNC_ADD) blendEquation;
	STATEFLOAT4(glBlendColor, 1.0f) blendColor;

	BoolState<GL_CULL_FACE, false> cullFace;
	STATE1(glCullFace, GLenum, GL_FRONT) cullFaceMode;

	BoolState<GL_DEPTH_TEST, false> depthTest;
	STATE1(glDepthFunc, GLenum, GL_LESS) depthFunc;
	STATE2(glDepthRangef, float, float, 0.f, 1.f) depthRange;

};

#undef STATE1
#undef STATE2

extern OpenGLState glstate;
