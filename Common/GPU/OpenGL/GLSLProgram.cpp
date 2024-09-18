#include <set>

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include "Common/File/VFS/VFS.h"
#include "Common/File/FileUtil.h"
#include "Common/GPU/OpenGL/GLSLProgram.h"

#include "Common/Log.h"

static std::set<GLSLProgram *> active_programs;

bool CompileShader(const char *source, GLuint shader, const char *filename, std::string *error_message) {
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success) {
#define MAX_INFO_LOG_SIZE 2048
		GLchar infoLog[MAX_INFO_LOG_SIZE];
		GLsizei len;
		glGetShaderInfoLog(shader, MAX_INFO_LOG_SIZE, &len, infoLog);
		infoLog[len] = '\0';
		ERROR_LOG(Log::G3D, "Error in shader compilation of %s!\n", filename);
		ERROR_LOG(Log::G3D, "Info log: %s\n", infoLog);
		ERROR_LOG(Log::G3D, "Shader source:\n%s\n", (const char *)source);
		if (error_message)
			*error_message = infoLog;
		return false;
	}
	return true;
}

GLSLProgram *glsl_create_source(const char *vshader_src, const char *fshader_src, std::string *error_message) {
	GLSLProgram *program = new GLSLProgram();
	program->program_ = 0;
	program->vsh_ = 0;
	program->fsh_ = 0;
	program->vshader_source = vshader_src;
	program->fshader_source = fshader_src;
	strcpy(program->name, "[srcshader]");
	strcpy(program->vshader_filename, "");
	strcpy(program->fshader_filename, "");
	if (glsl_recompile(program, error_message)) {
		active_programs.insert(program);
	} else {
		ERROR_LOG(Log::G3D, "Failed compiling GLSL program from source strings");
		delete program;
		return 0;
	}
	return program;
}

// Not wanting to change ReadLocalFile semantics.
// TODO: Use C++11 unique_ptr, remove delete[]
struct AutoCharArrayBuf {
	AutoCharArrayBuf(char *buf = nullptr) : buf_(buf) {
	}
	~AutoCharArrayBuf() {
		delete [] buf_;
		buf_ = nullptr;
	}
	void reset(char *buf) {
		delete[] buf_;
		buf_ = buf;
	}
	operator char *() {
		return buf_;
	}

private:
	char *buf_;
};

bool glsl_recompile(GLSLProgram *program, std::string *error_message) {
	struct stat vs, fs;
	AutoCharArrayBuf vsh_src, fsh_src;

	if (strlen(program->vshader_filename) > 0 && 0 == stat(program->vshader_filename, &vs)) {
		program->vshader_mtime = vs.st_mtime;
		if (!program->vshader_source) {
			size_t sz;
			vsh_src.reset((char *)File::ReadLocalFile(Path(program->vshader_filename), &sz));
		}
	} else {
		program->vshader_mtime = 0;
	}

	if (strlen(program->fshader_filename) > 0 && 0 == stat(program->fshader_filename, &fs)) {
		program->fshader_mtime = fs.st_mtime;
		if (!program->fshader_source) {
			size_t sz;
			fsh_src.reset((char *)File::ReadLocalFile(Path(program->fshader_filename), &sz));
		}
	} else {
		program->fshader_mtime = 0;
	}

	if (!program->vshader_source && !vsh_src) {
		size_t sz;
		vsh_src.reset((char *)g_VFS.ReadFile(program->vshader_filename, &sz));
	}
	if (!program->vshader_source && !vsh_src) {
		ERROR_LOG(Log::G3D, "File missing: %s", program->vshader_filename);
		if (error_message) {
			*error_message = std::string("File missing: ") + program->vshader_filename;
		}
		return false;
	}
	if (!program->fshader_source && !fsh_src) {
		size_t sz;
		fsh_src.reset((char *)g_VFS.ReadFile(program->fshader_filename, &sz));
	}
	if (!program->fshader_source && !fsh_src) {
		ERROR_LOG(Log::G3D, "File missing: %s", program->fshader_filename);
		if (error_message) {
			*error_message = std::string("File missing: ") + program->fshader_filename;
		}
		return false;
	}

	GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
	const GLchar *vsh_str = program->vshader_source ? program->vshader_source : (const GLchar *)(vsh_src);
	if (!CompileShader(vsh_str, vsh, program->vshader_filename, error_message)) {
		return false;
	}

	const GLchar *fsh_str = program->fshader_source ? program->fshader_source : (const GLchar *)(fsh_src);
	GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
	if (!CompileShader(fsh_str, fsh, program->fshader_filename, error_message)) {
		glDeleteShader(vsh);
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
			char* buf = new char[bufLength + 1];  // safety
			glGetProgramInfoLog(prog, bufLength, NULL, buf);
			INFO_LOG(Log::G3D, "vsh: %i   fsh: %i", vsh, fsh);
			ERROR_LOG(Log::G3D, "Could not link shader program (linkstatus=%i):\n %s  \n", linkStatus, buf);
			if (error_message) {
				*error_message = buf;
			}
			delete [] buf;
		} else {
			INFO_LOG(Log::G3D, "vsh: %i   fsh: %i", vsh, fsh);
			ERROR_LOG(Log::G3D, "Could not link shader program (linkstatus=%i). No OpenGL error log was available.", linkStatus);
			if (error_message) {
				*error_message = "(no error message available)";
			}
		}
		glDeleteShader(vsh);
		glDeleteShader(fsh);
		return false;
	}

	// Destroy the old program, if any.
	if (program->program_) {
		glDeleteProgram(program->program_);
	}

	program->program_ = prog;
	program->vsh_ = vsh;
	program->fsh_ = fsh;

	program->sampler0 = glGetUniformLocation(program->program_, "sampler0");
	program->sampler1 = glGetUniformLocation(program->program_, "sampler1");

	program->a_position	= glGetAttribLocation(program->program_, "a_position");
	program->a_color = glGetAttribLocation(program->program_, "a_color");
	program->a_normal = glGetAttribLocation(program->program_, "a_normal");
	program->a_texcoord0 = glGetAttribLocation(program->program_, "a_texcoord0");
	program->a_texcoord1 = glGetAttribLocation(program->program_, "a_texcoord1");

	program->u_worldviewproj = glGetUniformLocation(program->program_, "u_worldviewproj");
	program->u_world = glGetUniformLocation(program->program_, "u_world");
	program->u_viewproj = glGetUniformLocation(program->program_, "u_viewproj");
	program->u_fog = glGetUniformLocation(program->program_, "u_fog");
	program->u_sundir = glGetUniformLocation(program->program_, "u_sundir");
	program->u_camerapos = glGetUniformLocation(program->program_, "u_camerapos");

	//INFO_LOG(Log::G3D, "Shader compilation success: %s %s",
	//		 program->vshader_filename,
	//		 program->fshader_filename);
	return true;
}

int glsl_attrib_loc(const GLSLProgram *program, const char *name) {
	return glGetAttribLocation(program->program_, name);
}

int glsl_uniform_loc(const GLSLProgram *program, const char *name) {
	return glGetUniformLocation(program->program_, name);
}

void glsl_destroy(GLSLProgram *program) {
	if (program) {
		glDeleteShader(program->vsh_);
		glDeleteShader(program->fsh_);
		glDeleteProgram(program->program_);
		active_programs.erase(program);
	} else {
		ERROR_LOG(Log::G3D, "Deleting null GLSL program!");
	}
	delete program;
}

static const GLSLProgram *curProgram;

void glsl_bind(const GLSLProgram *program) {
	if (program)
		glUseProgram(program->program_);
	else
		glUseProgram(0);
	curProgram = program;
}

void glsl_unbind() {
	glUseProgram(0);
	curProgram = nullptr;
}

const GLSLProgram *glsl_get_program() {
	return curProgram;
}
