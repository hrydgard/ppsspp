#pragma once

// GE-related windows of the ImDebugger

struct ImConfig;

class FramebufferManagerCommon;
class TextureCacheCommon;

void DrawFramebuffersWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager);
void DrawTexturesWindow(ImConfig &cfg, TextureCacheCommon *textureCache);
void DrawDisplayWindow(ImConfig &cfg, FramebufferManagerCommon *framebufferManager);

class ImGeDebugger {
public:

};
