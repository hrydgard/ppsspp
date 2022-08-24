#include "VRTweaks.h"
#include <iostream>

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

void VR_TweakMirroring(float* projMatrix) {
	VR_SetConfig(VR_CONFIG_MIRROR_AXIS_X, projMatrix[0] < 0);
	VR_SetConfig(VR_CONFIG_MIRROR_AXIS_Y, projMatrix[5] < 0);
	VR_SetConfig(VR_CONFIG_MIRROR_AXIS_Z, projMatrix[10] > 0);
	if ((projMatrix[0] < 0) && (projMatrix[10] < 0)) { //e.g. Dante's inferno
		VR_SetConfig(VR_CONFIG_MIRROR_PITCH, true);
		VR_SetConfig(VR_CONFIG_MIRROR_YAW, true);
		VR_SetConfig(VR_CONFIG_MIRROR_ROLL, false);
	} else if (projMatrix[10] < 0) { //e.g. GTA - Liberty city
		VR_SetConfig(VR_CONFIG_MIRROR_PITCH, false);
		VR_SetConfig(VR_CONFIG_MIRROR_YAW, false);
		VR_SetConfig(VR_CONFIG_MIRROR_ROLL, false);
	} else if (projMatrix[5] < 0) { //e.g. PES 2014
		VR_SetConfig(VR_CONFIG_MIRROR_PITCH, true);
		VR_SetConfig(VR_CONFIG_MIRROR_YAW, true);
		VR_SetConfig(VR_CONFIG_MIRROR_ROLL, false);
	} else { //e.g. Lego Pirates
		VR_SetConfig(VR_CONFIG_MIRROR_PITCH, false);
		VR_SetConfig(VR_CONFIG_MIRROR_YAW, true);
		VR_SetConfig(VR_CONFIG_MIRROR_ROLL, true);
	}
}

void VR_TweakProjection(float* src, float* dst, VRMatrix matrix) {
	memcpy(dst, src, 16 * sizeof(float));
	ovrMatrix4f hmdProjection = VR_GetMatrix(matrix);
	dst[0] = (dst[0] > 0 ? 1.0f : -1.0f) * hmdProjection.M[0][0];
	dst[5] = (dst[5] > 0 ? 1.0f : -1.0f) * hmdProjection.M[1][1];
}

void VR_TweakView(float* view, float* projMatrix, VRMatrix matrix) {
	// Get view matrix from the game
	ovrMatrix4f gameView;
	memcpy(gameView.M, view, 16 * sizeof(float));

	// Set 6DoF scale
	float scale = pow(fabs(projMatrix[14]), 1.15f);
	VR_SetConfig(VR_CONFIG_6DOF_SCALE, (int)(scale * 1000));

	// Get view matrix from the headset
	ovrMatrix4f hmdView = VR_GetMatrix(matrix);

	// Combine the matrices
	ovrMatrix4f renderView = ovrMatrix4f_Multiply(&hmdView, &gameView);
	memcpy(view, renderView.M, 16 * sizeof(float));
}
