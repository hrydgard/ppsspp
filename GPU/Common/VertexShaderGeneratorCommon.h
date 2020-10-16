#pragma once

#include "GPU/Common/ShaderId.h"

enum DoLightComputation {
	LIGHT_OFF,
	LIGHT_SHADE,
	LIGHT_FULL,
};

// If we can know from the lights that there's no specular, sets specularIsZero to true.
char *WriteLights(char *p, const VShaderID &id, DoLightComputation doLight[4], bool *specularIsZero);
