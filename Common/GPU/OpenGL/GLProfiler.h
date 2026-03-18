#pragma once

#include <vector>
#include <string>
#include <cstdint>

#include "Common/GPU/OpenGL/GLCommon.h"
#include "Common/GPU/OpenGL/gl3stub.h"

// Simple scoped based profiler for OpenGL, similar to VulkanProfiler.
// Uses GL_EXT_disjoint_timer_query (GLES) or GL_ARB_timer_query (desktop GL).
// Put the whole thing in a FrameData to allow for overlap.

struct GLProfilerScope {
	char name[52];  // to make a struct size of 64, just because
	int startQueryId;
	int endQueryId;
	int level;
};

class GLProfiler {
public:
	void Init();
	void Shutdown();

	void BeginFrame();

	void Begin(const char *fmt, ...)
#ifdef __GNUC__
		__attribute__((format(printf, 2, 3)))
#endif
		;
	void End();

	void SetEnabledPtr(bool *enabledPtr) {
		enabledPtr_ = enabledPtr;
	}

	bool IsSupported() const { return supported_; }

private:
	bool supported_ = false;
	bool firstFrame_ = true;
	bool *enabledPtr_ = nullptr;

	std::vector<GLuint> queries_;
	std::vector<GLProfilerScope> scopes_;
	int numQueries_ = 0;

	std::vector<size_t> scopeStack_;

	static const int MAX_QUERY_COUNT = 1024;
};
