#include <cstdarg>

#include "Common/GPU/OpenGL/GLProfiler.h"
#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/GPU/OpenGL/GLDebugLog.h"
#include "Common/Log.h"

// For iOS, define function pointer types and variables locally since gl3stub.h
// content is excluded on iOS. These will remain NULL since iOS doesn't support
// GL_EXT_disjoint_timer_query.
#if PPSSPP_PLATFORM(IOS)
typedef void (*PFNGLQUERYCOUNTERPROC)(GLuint id, GLenum target);
typedef void (*PFNGLGETQUERYOBJECTUI64VPROC)(GLuint id, GLenum pname, GLuint64 *params);
static PFNGLQUERYCOUNTERPROC glQueryCounter = nullptr;
static PFNGLGETQUERYOBJECTUI64VPROC glGetQueryObjectui64v = nullptr;
#endif

// Constants - same values for both EXT and ARB versions
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif

// GLCommon.h provides access to GL functions:
// - On GLES: function pointers from gl3stub.h, loaded via eglGetProcAddress
// - On desktop GL: GLEW provides the functions

void GLProfiler::Init() {
	supported_ = false;
	firstFrame_ = true;
	numQueries_ = 0;
	scopes_.clear();
	scopeStack_.clear();

	// Check for extension support
	// Function pointers are declared in gl3stub.h and loaded appropriately per platform
	if (gl_extensions.EXT_disjoint_timer_query) {
		// GLES path - use function pointers loaded with EXT suffix
		if (glQueryCounter && glGetQueryObjectui64v) {
			supported_ = true;
			INFO_LOG(Log::G3D, "GLProfiler: Using GL_EXT_disjoint_timer_query");
		}
	} else if (gl_extensions.ARB_timer_query) {
		// Desktop GL path - use function pointers (no suffix)
		if (glQueryCounter && glGetQueryObjectui64v) {
			supported_ = true;
			INFO_LOG(Log::G3D, "GLProfiler: Using GL_ARB_timer_query");
		}
	}

	if (supported_) {
		// Pre-allocate query objects
		queries_.resize(MAX_QUERY_COUNT);
		glGenQueries(MAX_QUERY_COUNT, queries_.data());
		CHECK_GL_ERROR_IF_DEBUG();
	}
}

void GLProfiler::Shutdown() {
	if (supported_ && !queries_.empty()) {
		glDeleteQueries((GLsizei)queries_.size(), queries_.data());
		queries_.clear();
	}
	supported_ = false;
	scopes_.clear();
	scopeStack_.clear();
}

void GLProfiler::BeginFrame() {
	if (!supported_) {
		return;
	}

	// Check if profiling is enabled
	if (enabledPtr_ && !*enabledPtr_) {
		scopes_.clear();
		scopeStack_.clear();
		numQueries_ = 0;
		return;
	}

	// Check for disjoint operation (GPU frequency changed) - GLES only
	if (gl_extensions.EXT_disjoint_timer_query) {
		GLint disjoint = 0;
		glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
		if (disjoint) {
			// Results are invalid, just clear and start fresh
			WARN_LOG(Log::G3D, "GLProfiler: GPU disjoint detected, timing results discarded");
			scopes_.clear();
			scopeStack_.clear();
			numQueries_ = 0;
			firstFrame_ = true;
			return;
		}
	}

	// Read results from previous frame (guaranteed complete now)
	if (numQueries_ > 0 && !firstFrame_) {
		static const char * const indent[4] = { "", "  ", "    ", "      " };

		if (!scopes_.empty()) {
			VERBOSE_LOG(Log::G3D, "OpenGL profiling events this frame:");
		}

		// Log results
		for (auto &scope : scopes_) {
			if (scope.endQueryId == -1) {
				WARN_LOG(Log::G3D, "Unclosed scope: %s", scope.name);
				continue;
			}

			GLuint64 startTime = 0, endTime = 0;
			glGetQueryObjectui64v(queries_[scope.startQueryId], GL_QUERY_RESULT, &startTime);
			glGetQueryObjectui64v(queries_[scope.endQueryId], GL_QUERY_RESULT, &endTime);

			// Times are in nanoseconds, convert to milliseconds
			double milliseconds = (double)(endTime - startTime) / 1000000.0;

			VERBOSE_LOG(Log::G3D, "%s%s (%0.3f ms)", indent[scope.level & 3], scope.name, milliseconds);
		}
	}

	firstFrame_ = false;
	scopes_.clear();
	scopeStack_.clear();
	numQueries_ = 0;
}

void GLProfiler::Begin(const char *fmt, ...) {
	if (!supported_ || (enabledPtr_ && !*enabledPtr_) || numQueries_ >= MAX_QUERY_COUNT - 1) {
		return;
	}

	GLProfilerScope scope;
	va_list args;
	va_start(args, fmt);
	vsnprintf(scope.name, sizeof(scope.name), fmt, args);
	va_end(args);
	scope.startQueryId = numQueries_;
	scope.endQueryId = -1;
	scope.level = (int)scopeStack_.size();

	scopeStack_.push_back(scopes_.size());
	scopes_.push_back(scope);

	glQueryCounter(queries_[numQueries_], GL_TIMESTAMP);
	numQueries_++;
}

void GLProfiler::End() {
	if (!supported_ || (enabledPtr_ && !*enabledPtr_) || numQueries_ >= MAX_QUERY_COUNT - 1) {
		return;
	}

	if (scopeStack_.empty()) {
		WARN_LOG(Log::G3D, "GLProfiler::End called without matching Begin");
		return;
	}

	size_t scopeId = scopeStack_.back();
	scopeStack_.pop_back();

	GLProfilerScope &scope = scopes_[scopeId];
	scope.endQueryId = numQueries_;

	glQueryCounter(queries_[numQueries_], GL_TIMESTAMP);
	numQueries_++;
}
