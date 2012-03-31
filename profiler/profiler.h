#pragma once

// WIP - very preliminary.
// #define USE_PROFILER

#ifdef USE_PROFILER

class DrawBuffer;

void _profiler_init();
void _profiler_begin_frame();
void _profiler_end_frame();

void _profiler_log();
void _profiler_draw(DrawBuffer *draw2d, int font);

void _profiler_enter(const char *section);
void _profiler_leave(const char *section);

#define PROFILER_INIT() _profiler_init();
#define PROFILER_ENTER(section) _profiler_enter(section);
#define PROFILER_LEAVE(section) _profiler_leave(section);
#define PROFILER_LOG() _profiler_log();
#define PROFILER_DRAW(draw, font) _profiler_draw(draw, font);
#define PROFILER_BEGIN_FRAME() _profiler_begin_frame();
#define PROFILER_END_FRAME() _profiler_end_frame();

#else

#define PROFILER_INIT()
#define PROFILER_ENTER(section)
#define PROFILER_LEAVE(section)
#define PROFILER_LOG()
#define PROFILER_DRAW(draw, font)
#define PROFILER_BEGIN_FRAME()
#define PROFILER_END_FRAME()

#endif
