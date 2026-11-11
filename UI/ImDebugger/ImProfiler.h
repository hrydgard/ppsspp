#pragma once

struct ImConfig;

// The frame profiler's timeline and per-frame totals. Only does anything when built with USE_PROFILER.
void DrawCpuProfiler(ImConfig &cfg);
// The on-screen frame profile overlay's graph, in a window.
void DrawFrameProfile(ImConfig &cfg);
