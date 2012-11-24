#include "StateMapping.h"

const GLint aLookup[] = {
	GL_DST_COLOR,
	GL_ONE_MINUS_DST_COLOR,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_DST_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_SRC_ALPHA,	// should be 2x
	GL_ONE_MINUS_SRC_ALPHA,	 // should be 2x
	GL_DST_ALPHA,	 // should be 2x
	GL_ONE_MINUS_DST_ALPHA,	 // should be 2x				 -	and COLOR?
	GL_SRC_ALPHA,	// should be FIXA
};

const GLint bLookup[] = {
	GL_SRC_COLOR,
	GL_ONE_MINUS_SRC_COLOR,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_DST_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_SRC_ALPHA,	// should be 2x
	GL_ONE_MINUS_SRC_ALPHA,	 // should be 2x
	GL_DST_ALPHA,	 // should be 2x
	GL_ONE_MINUS_DST_ALPHA,	 // should be 2x
	GL_SRC_ALPHA,	// should be FIXB
};
const GLint eqLookup[] = {
	GL_FUNC_ADD,
	GL_FUNC_SUBTRACT,
	GL_FUNC_REVERSE_SUBTRACT,
#if defined(ANDROID) || defined(BLACKBERRY)
	GL_FUNC_ADD,
	GL_FUNC_ADD,
#else
	GL_MIN,
	GL_MAX,
#endif
	GL_FUNC_ADD, // should be abs(diff)
};

const GLint cullingMode[] = {
	GL_BACK,
	GL_FRONT,
};

const GLuint ztests[] = 
{
	GL_NEVER, GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL, 
	GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL,
};