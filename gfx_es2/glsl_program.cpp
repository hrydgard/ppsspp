#include <set>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "base/logging.h"
#include "file/vfs.h"
#include "glsl_program.h"

static std::set<GLSLProgram *> active_programs;

bool CompileShader(const char *source, GLuint shader, const char *filename) {
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
		ELOG("Error in shader compilation of %s!\n", filename);
		ELOG("Info log: %s\n", infoLog);
		ELOG("Shader source:\n%s\n", (const char *)source);
#if defined(ARM)
		exit(1);
#endif
		return false;
	}
	return true;
}

GLSLProgram *glsl_create(const char *vshader, const char *fshader) {
	GLSLProgram *program = new GLSLProgram();
	program->program_ = 0;
	program->vsh_ = 0;
	program->fsh_ = 0;
	program->vshader_source = 0;
	program->fshader_source = 0;
	strcpy(program->name, vshader + strlen(vshader) - 15);
	strcpy(program->vshader_filename, vshader);
	strcpy(program->fshader_filename, fshader);
	if (glsl_recompile(program)) {
		active_programs.insert(program);
	}
	else
	{
		FLOG("Failed building GLSL program: %s %s", vshader, fshader);
	}
	register_gl_resource_holder(program);
	return program;
}

GLSLProgram *glsl_create_source(const char *vshader_src, const char *fshader_src) {
	GLSLProgram *program = new GLSLProgram();
	program->program_ = 0;
	program->vsh_ = 0;
	program->fsh_ = 0;
	program->vshader_source = vshader_src;
	program->fshader_source = fshader_src;
	strcpy(program->name, "[srcshader]");
	strcpy(program->vshader_filename, "");
	strcpy(program->fshader_filename, "");
	if (glsl_recompile(program)) {
		active_programs.insert(program);
	}
	register_gl_resource_holder(program);
	return program;
}

bool glsl_up_to_date(GLSLProgram *program) {
	struct stat vs, fs;
	stat(program->vshader_filename, &vs);
	stat(program->fshader_filename, &fs);
	if (vs.st_mtime != program->vshader_mtime ||
			fs.st_mtime != program->fshader_mtime) {
			return false;
	} else {
		return true;
	}
}

void glsl_refresh() {
	ILOG("glsl_refresh()");
	for (std::set<GLSLProgram *>::const_iterator iter = active_programs.begin();
		iter != active_programs.end(); ++iter) {
			if (!glsl_up_to_date(*iter)) {
				glsl_recompile(*iter);
			}
	}
}

bool glsl_recompile(GLSLProgram *program) {
	struct stat vs, fs;
	if (0 == stat(program->vshader_filename, &vs))
		program->vshader_mtime = vs.st_mtime;
	else
		program->vshader_mtime = 0;

	if (0 == stat(program->fshader_filename, &fs))
		program->fshader_mtime = fs.st_mtime;
	else
		program->fshader_mtime = 0;
	
	char *vsh_src = 0, *fsh_src = 0;

	if (!program->vshader_source) 
	{
		size_t sz;
		vsh_src = (char *)VFSReadFile(program->vshader_filename, &sz);
		if (!vsh_src) {
			ELOG("File missing: %s", program->vshader_filename);
			return false;
		}
	}
	if (!program->fshader_source)
	{
		size_t sz;
		fsh_src = (char *)VFSReadFile(program->fshader_filename, &sz);
		if (!fsh_src) {
			ELOG("File missing: %s", program->fshader_filename);
			delete [] vsh_src;
			return false;
		}
	}

	GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
	const GLchar *vsh_str = program->vshader_source ? program->vshader_source : (const GLchar *)(vsh_src);
	if (!CompileShader(vsh_str, vsh, program->vshader_filename)) {
		return false;
	}
	delete [] vsh_src;

	const GLchar *fsh_str = program->fshader_source ? program->fshader_source : (const GLchar *)(fsh_src);
	GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
	if (!CompileShader(fsh_str, fsh, program->fshader_filename)) {
		glDeleteShader(vsh);
		return false;
	}
	delete [] fsh_src;

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vsh);
	glAttachShader(prog, fsh);

	glLinkProgram(prog);

	GLint linkStatus;
	glGetProgramiv(prog, GL_LINK_STATUS, &linkStatus);
	if (linkStatus != GL_TRUE) {
		GLint bufLength = 0;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &bufLength);
		if (bufLength) {
			char* buf = new char[bufLength];
			glGetProgramInfoLog(prog, bufLength, NULL, buf);
			FLOG("Could not link program:\n %s", buf);
			delete [] buf;	// we're dead!
		} else {
			FLOG("Could not link program.");
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
	program->fsh_ = vsh;

	program->sampler0 = glGetUniformLocation(program->program_, "sampler0");
	program->sampler1 = glGetUniformLocation(program->program_, "sampler1");

	program->a_position	= glGetAttribLocation(program->program_, "a_position");
	program->a_color		 = glGetAttribLocation(program->program_, "a_color");
	program->a_normal		= glGetAttribLocation(program->program_, "a_normal");
	program->a_texcoord0 = glGetAttribLocation(program->program_, "a_texcoord0");
	program->a_texcoord1 = glGetAttribLocation(program->program_, "a_texcoord1");

	program->u_worldviewproj = glGetUniformLocation(program->program_, "u_worldviewproj");
	program->u_world = glGetUniformLocation(program->program_, "u_world");
	program->u_viewproj = glGetUniformLocation(program->program_, "u_viewproj");
	program->u_fog = glGetUniformLocation(program->program_, "u_fog");
	program->u_sundir = glGetUniformLocation(program->program_, "u_sundir");
	program->u_camerapos = glGetUniformLocation(program->program_, "u_camerapos");

	//ILOG("Shader compilation success: %s %s",
	//		 program->vshader_filename,
	//		 program->fshader_filename);
	return true;
}

void GLSLProgram::GLLost() {
	// Quoth http://developer.android.com/reference/android/opengl/GLSurfaceView.Renderer.html;
	// "Note that when the EGL context is lost, all OpenGL resources associated with that context will be automatically deleted. 
	// You do not need to call the corresponding "glDelete" methods such as glDeleteTextures to manually delete these lost resources."
	// Hence, we comment out:
	// glDeleteShader(this->vsh_);
	// glDeleteShader(this->fsh_);
	// glDeleteProgram(this->program_);
	ILOG("Restoring GLSL program %s/%s",
		this->vshader_filename ? this->vshader_filename : "(mem)",
		this->fshader_filename ? this->fshader_filename : "(mem)");
	this->program_ = 0;
	this->vsh_ = 0;
	this->fsh_ = 0;
	glsl_recompile(this);
	// Note that uniforms are still lost, hopefully the client sets them every frame at a minimum...
}


int glsl_attrib_loc(const GLSLProgram *program, const char *name) {
	return glGetAttribLocation(program->program_, name);
}

int glsl_uniform_loc(const GLSLProgram *program, const char *name) {
	return glGetUniformLocation(program->program_, name);
}

void glsl_destroy(GLSLProgram *program) {
	unregister_gl_resource_holder(program);
	glDeleteShader(program->vsh_);
	glDeleteShader(program->fsh_);
	glDeleteProgram(program->program_);
	active_programs.erase(program);
	delete program;
}

void glsl_bind(const GLSLProgram *program) {
	glUseProgram(program->program_);
}

void glsl_unbind() {
	glUseProgram(0);
}
