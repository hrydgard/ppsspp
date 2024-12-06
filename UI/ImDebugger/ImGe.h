#pragma once

// GE-related windows of the ImDebugger

struct ImConfig;

class FramebufferManagerCommon;
class TextureCacheCommon;
class GPUDebugInterface;

void DrawFramebuffersWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager);
void DrawTexturesWindow(ImConfig &cfg, TextureCacheCommon *textureCache);
void DrawDisplayWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager);
void DrawDebugStatsWindow(ImConfig &cfg);
void DrawGeDebuggerWindow(ImConfig &cfg, GPUDebugInterface *gpuDebug);
void DrawGeStateWindow(ImConfig &cfg, GPUDebugInterface *gpuDebug);

class ImGeDebugger {
public:

};
