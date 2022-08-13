#pragma once

// For framebuffer copies and similar things that just require passthrough.
struct Draw2DVertex {
	float x;
	float y;
	float u;
	float v;
};
