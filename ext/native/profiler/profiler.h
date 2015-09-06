#pragma once

#include <inttypes.h>

// #define USE_PROFILER

#ifdef USE_PROFILER

class DrawBuffer;

void internal_profiler_init();
void internal_profiler_end_frame();

int internal_profiler_enter(const char *category_name);  // Returns the category number.
void internal_profiler_leave(int category);


const char *Profiler_GetCategoryName(int i);
int Profiler_GetNumCategories();
int Profiler_GetHistoryLength();
void Profiler_GetHistory(int i, float *data, int count);

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

#else

#define PROFILE_INIT()
#define PROFILE_THIS_SCOPE(cat)
#define PROFILE_END_FRAME()

#endif
