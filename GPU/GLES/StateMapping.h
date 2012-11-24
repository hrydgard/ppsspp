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

extern const GLint aLookup[];
extern const GLint bLookup[];
extern const GLint eqLookup[];
extern const GLint cullingMode[];
extern const GLuint ztests[];