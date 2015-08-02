#pragma once

namespace HighGpu {

class ShaderManagerGLES {
public:
	void ClearCache(bool);
	void DirtyShader();
	void DirtyLastShader();
};

}  // namespace
