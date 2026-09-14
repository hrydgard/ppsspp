#include <cstdio>
#include <cstring>

#include "Common/GPU/OpenGL/GLSLProgram.h"

#include "Common/Log.h"

static bool CompileShader(const char *source, GLuint shader, const char *stageName, std::string *error_message) {
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);
	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
#define MAX_INFO_LOG_SIZE 2048
		GLchar infoLog[MAX_INFO_LOG_SIZE];
		GLsizei len;
		glGetShaderInfoLog(shader, MAX_INFO_LOG_SIZE, &len, infoLog);
		infoLog[len] = '\0';
		ERROR_LOG(Log::G3D, "Error compiling %s shader!\n", stageName);
		ERROR_LOG(Log::G3D, "Info log: %s\n", infoLog);
		ERROR_LOG(Log::G3D, "Shader source:\n%s\n", source);
		if (error_message)
			*error_message = infoLog;
		return false;
	}
	return true;
}

static bool glsl_compile(GLSLProgram *program, const char *vshader_src, const char *fshader_src, std::string *error_message) {
	GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
	if (!CompileShader(vshader_src, vsh, "vertex", error_message)) {
		glDeleteShader(vsh);
		return false;
	}

	GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
	if (!CompileShader(fshader_src, fsh, "fragment", error_message)) {
		glDeleteShader(vsh);
		glDeleteShader(fsh);
		return false;
	}

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vsh);
	glAttachShader(prog, fsh);

	glLinkProgram(prog);

	GLint linkStatus;
	glGetProgramiv(prog, GL_LINK_STATUS, &linkStatus);
	if (linkStatus == GL_FALSE) {
		GLint bufLength = 0;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &bufLength);
		if (bufLength) {
			char *buf = new char[bufLength + 1];  // safety
			glGetProgramInfoLog(prog, bufLength, nullptr, buf);
			ERROR_LOG(Log::G3D, "Could not link shader program (linkstatus=%i):\n %s  \n", linkStatus, buf);
			if (error_message) {
				*error_message = buf;
			}
			delete[] buf;
		} else {
			ERROR_LOG(Log::G3D, "Could not link shader program (linkstatus=%i). No OpenGL error log was available.", linkStatus);
			if (error_message) {
				*error_message = "(no error message available)";
			}
		}
		glDeleteProgram(prog);
		glDeleteShader(vsh);
		glDeleteShader(fsh);
		return false;
	}

	program->program_ = prog;
	program->vsh_ = vsh;
	program->fsh_ = fsh;

	program->sampler0 = glGetUniformLocation(program->program_, "sampler0");
	program->u_viewproj = glGetUniformLocation(program->program_, "u_viewproj");
	program->a_position = glGetAttribLocation(program->program_, "a_position");
	program->a_texcoord0 = glGetAttribLocation(program->program_, "a_texcoord0");
	return true;
}

GLSLProgram *glsl_create_source(const char *vshader_src, const char *fshader_src, std::string *error_message) {
	GLSLProgram *program = new GLSLProgram();
	program->program_ = 0;
	program->vsh_ = 0;
	program->fsh_ = 0;
	if (!glsl_compile(program, vshader_src, fshader_src, error_message)) {
		ERROR_LOG(Log::G3D, "Failed compiling GLSL program from source strings");
		delete program;
		return nullptr;
	}
	return program;
}

void glsl_destroy(GLSLProgram *program) {
	if (program) {
		glDeleteShader(program->vsh_);
		glDeleteShader(program->fsh_);
		glDeleteProgram(program->program_);
	} else {
		ERROR_LOG(Log::G3D, "Deleting null GLSL program!");
	}
	delete program;
}

void glsl_bind(const GLSLProgram *program) {
	if (program)
		glUseProgram(program->program_);
	else
		glUseProgram(0);
}

void glsl_unbind() {
	glUseProgram(0);
}
