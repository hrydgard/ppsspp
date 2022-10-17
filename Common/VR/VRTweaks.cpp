#include "VRMath.h"
#include "VRTweaks.h"

bool VR_TweakIsMatrixBigScale(float* matrix) {
	for (int i = 0; i < 2; i++) {
		float value = matrix[i * 4 + i];
		if (fabs(value) < 10.0f) return false;
	}
	return true;
}

bool VR_TweakIsMatrixIdentity(float* matrix) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			float value = matrix[i * 4 + j];

			// Other number than zero on non-diagonale
			if ((i != j) && (fabs(value) > EPSILON)) return false;
			// Other number than one on diagonale
			if ((i == j) && (fabs(value - 1.0f) > EPSILON)) return false;
		}
	}
	return true;
}

bool VR_TweakIsMatrixOneOrtho(float* matrix) {
	float value = matrix[15];
	return fabs(value - 1) < EPSILON;
}

bool VR_TweakIsMatrixOneScale(float* matrix) {
	for (int i = 0; i < 2; i++) {
		float value = matrix[i * 4 + i];
		if (fabs(value - 1) > EPSILON) return false;
	}
	return true;
}

bool VR_TweakIsMatrixOneTransform(float* matrix) {
	for (int j = 0; j < 4; j++) {
		float value = matrix[12 + j];
		if (fabs(fabs(value) - 1.0f) > EPSILON) return false;
	}
	return true;
}
