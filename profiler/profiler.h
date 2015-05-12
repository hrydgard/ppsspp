#pragma once

#define USE_PROFILER

#ifdef USE_PROFILER

class DrawBuffer;

struct Category {
	const char *name;
	uint32_t color;
};

void internal_profiler_init();
void internal_profiler_end_frame();

int internal_profiler_enter(const char *section);  // Returns the category number.
void internal_profiler_leave(int category);

float internal_profiler_gethistory(const char *section, float *data, int count);
const char *GetSectionName(int i);
int GetNumSections();

class ProfileThis {
public:
	ProfileThis(const char *category) {
		cat_ = internal_profiler_enter(category);
	}
	~ProfileThis() {
		internal_profiler_leave(cat_);
	}
private:
	int cat_;
};

#define PROFILE_INIT() internal_profiler_init();
#define PROFILE_THIS_SCOPE(cat) ProfileThis _profile_scoped(cat);
#define PROFILE_END_FRAME() internal_profiler_end_frame();
#define PROFILE_GET_HISTORY(section, data, count) internal_profiler_gethistory(section, data, count);

#else

#define PROFILE_INIT()
#define PROFILE_THIS_SCOPE(cat) ProfileThis _profile_scoped(cat);
#define PROFILE_END_FRAME()
#define PROFILE_GET_HISTORY(section, data, count) internal_profiler_gethistory(section, data, count);

#endif
