#include "VRTweaks.h"
#include <iostream>

static bool vrIsHUD = false;

bool VR_TweakIs2D(float* projMatrix) {
	bool ortho = true;
	bool identity = true;
	bool oneTranslation = true;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			float value = projMatrix[i * 4 + j];

			// Other number than zero on non-diagonale
			if ((i != j) && (fabs(value) > EPSILON)) identity = false;
			// Other number than one on diagonale
			if ((i == j) && (fabs(value - 1.0f) > EPSILON)) identity = false;
			// Special case detecting UI in Flatout
			if ((i == j) && (i < 2) && (fabs(value) < 10.0f)) ortho = false;
			// Special case detecting UI in Lego games
			if (((i == 3) && (fabs(fabs(value) - 1.0f) > EPSILON))) oneTranslation = false;
		}
	}
	return identity || oneTranslation || ortho;
}

bool VR_TweakIsHUD(bool is2D, bool isOrtho, bool isProjection) {
	bool flatScreen = VR_GetConfig(VR_CONFIG_MODE) == VR_MODE_FLAT_SCREEN;
	if (isProjection) {
		vrIsHUD = is2D && !flatScreen;
	} else if (isOrtho) {
		vrIsHUD = !flatScreen;
	}
	return vrIsHUD;
}

void VR_TweakMirroring(float* projMatrix) {
	VR_SetConfig(VR_CONFIG_MIRROR_AXIS_X, projMatrix[0] < 0);
	VR_SetConfig(VR_CONFIG_MIRROR_AXIS_Y, projMatrix[5] < 0);
	VR_SetConfig(VR_CONFIG_MIRROR_AXIS_Z, projMatrix[10] > 0);
	if (projMatrix[10] < 0) { //GTA
		VR_SetConfig(VR_CONFIG_MIRROR_PITCH, false);
		VR_SetConfig(VR_CONFIG_MIRROR_YAW, false);
		VR_SetConfig(VR_CONFIG_MIRROR_ROLL, false);
	} else if (projMatrix[5] < 0) { //PES
		VR_SetConfig(VR_CONFIG_MIRROR_PITCH, true);
		VR_SetConfig(VR_CONFIG_MIRROR_YAW, true);
		VR_SetConfig(VR_CONFIG_MIRROR_ROLL, false);
	} else { //Lego
		VR_SetConfig(VR_CONFIG_MIRROR_PITCH, false);
		VR_SetConfig(VR_CONFIG_MIRROR_YAW, true);
		VR_SetConfig(VR_CONFIG_MIRROR_ROLL, true);
	}
}

void VR_TweakProjection(float* src, float* dst, VRMatrix matrix) {
	ovrMatrix4f hmdProjection = VR_GetMatrix(matrix);
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if ((hmdProjection.M[i][j] > 0) != (src[i * 4 + j] > 0)) {
				hmdProjection.M[i][j] *= -1.0f;
			}
		}
	}
	memcpy(dst, hmdProjection.M, 16 * sizeof(float));
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
