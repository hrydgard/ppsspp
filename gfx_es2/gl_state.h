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
                ##func(p1); \
			} \
	    } \
		void restore() { \
            ##func(p1); \
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
                ##func(p1, p2); \
			} \
	    } \
		inline void restore() { \
            ##func(p1, p2); \
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

	BoolState<GL_CULL_FACE, false> cullFace;
	STATE1(glCullFace, GLenum, GL_FRONT) cullFaceMode;

	BoolState<GL_DEPTH_TEST, false> depthTest;
	STATE1(glDepthFunc, GLenum, GL_LESS) depthFunc;
};

#undef STATE1
#undef STATE2

extern OpenGLState glstate;