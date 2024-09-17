#define _USE_MATH_DEFINES
#include <cmath>

#include "VRBase.h"
#include "VRInput.h"
#include "VRRenderer.h"
#include "OpenXRLoader.h"

#include <cstdlib>
#include <cstring>

XrFovf fov;
XrView* projections;
XrPosef invViewTransform[2];
XrFrameState frameState = {};
bool initialized = false;
bool stageSupported = false;
int vrConfig[VR_CONFIG_MAX] = {};
float vrConfigFloat[VR_CONFIG_FLOAT_MAX] = {};

XrVector3f hmdorientation;

XrPassthroughFB passthrough = XR_NULL_HANDLE;
XrPassthroughLayerFB passthroughLayer = XR_NULL_HANDLE;
bool passthroughRunning = false;
DECL_PFN(xrCreatePassthroughFB);
DECL_PFN(xrDestroyPassthroughFB);
DECL_PFN(xrPassthroughStartFB);
DECL_PFN(xrPassthroughPauseFB);
DECL_PFN(xrCreatePassthroughLayerFB);
DECL_PFN(xrDestroyPassthroughLayerFB);
DECL_PFN(xrPassthroughLayerPauseFB);
DECL_PFN(xrPassthroughLayerResumeFB);

void VR_UpdateStageBounds(ovrApp* pappState) {
	XrExtent2Df stageBounds = {};

	XrResult result;
	OXR(result = xrGetReferenceSpaceBoundsRect(pappState->Session, XR_REFERENCE_SPACE_TYPE_STAGE, &stageBounds));
	if (result != XR_SUCCESS) {
		stageBounds.width = 1.0f;
		stageBounds.height = 1.0f;
		pappState->CurrentSpace = pappState->FakeStageSpace;
	}
}

void VR_GetResolution(engine_t* engine, int *pWidth, int *pHeight) {
	static int width = 0;
	static int height = 0;

	if (engine) {
		// Enumerate the viewport configurations.
		uint32_t viewportConfigTypeCount = 0;
		OXR(xrEnumerateViewConfigurations(
				engine->appState.Instance, engine->appState.SystemId, 0, &viewportConfigTypeCount, NULL));

		XrViewConfigurationType* viewportConfigurationTypes =
				(XrViewConfigurationType*)malloc(viewportConfigTypeCount * sizeof(XrViewConfigurationType));

		OXR(xrEnumerateViewConfigurations(
				engine->appState.Instance,
				engine->appState.SystemId,
				viewportConfigTypeCount,
				&viewportConfigTypeCount,
				viewportConfigurationTypes));

		ALOGV("Available Viewport Configuration Types: %d", viewportConfigTypeCount);

		for (uint32_t i = 0; i < viewportConfigTypeCount; i++) {
			const XrViewConfigurationType viewportConfigType = viewportConfigurationTypes[i];

			ALOGV(
					"Viewport configuration type %d : %s",
					viewportConfigType,
					viewportConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO ? "Selected" : "");

			XrViewConfigurationProperties viewportConfig;
			viewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
			OXR(xrGetViewConfigurationProperties(
					engine->appState.Instance, engine->appState.SystemId, viewportConfigType, &viewportConfig));
			ALOGV(
					"FovMutable=%s ConfigurationType %d",
					viewportConfig.fovMutable ? "true" : "false",
					viewportConfig.viewConfigurationType);

			uint32_t viewCount;
			OXR(xrEnumerateViewConfigurationViews(
					engine->appState.Instance, engine->appState.SystemId, viewportConfigType, 0, &viewCount, NULL));

			if (viewCount > 0) {
				XrViewConfigurationView* elements =
						(XrViewConfigurationView*)malloc(viewCount * sizeof(XrViewConfigurationView));

				for (uint32_t e = 0; e < viewCount; e++) {
					elements[e].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
					elements[e].next = NULL;
				}

				OXR(xrEnumerateViewConfigurationViews(
						engine->appState.Instance,
						engine->appState.SystemId,
						viewportConfigType,
						viewCount,
						&viewCount,
						elements));

				// Cache the view config properties for the selected config type.
				if (viewportConfigType == XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
					assert(viewCount == ovrMaxNumEyes);
					for (uint32_t e = 0; e < viewCount; e++) {
						engine->appState.ViewConfigurationView[e] = elements[e];
					}
				}

				free(elements);
			} else {
				ALOGE("Empty viewport configuration type: %d", viewCount);
			}
		}

		free(viewportConfigurationTypes);

		*pWidth = width = engine->appState.ViewConfigurationView[0].recommendedImageRectWidth;
		*pHeight = height = engine->appState.ViewConfigurationView[0].recommendedImageRectHeight;
	} else {
		//use cached values
		*pWidth = width;
		*pHeight = height;
	}

	*pWidth = (int)(*pWidth * VR_GetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING));
	*pHeight = (int)(*pHeight * VR_GetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING));
}

void VR_Recenter(engine_t* engine) {

	// Calculate recenter reference
	XrReferenceSpaceCreateInfo spaceCreateInfo = {};
	spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
	if (engine->appState.CurrentSpace != XR_NULL_HANDLE) {
		XrSpaceLocation loc = {};
		loc.type = XR_TYPE_SPACE_LOCATION;
		OXR(xrLocateSpace(engine->appState.HeadSpace, engine->appState.CurrentSpace, engine->predictedDisplayTime, &loc));
		hmdorientation = XrQuaternionf_ToEulerAngles(loc.pose.orientation);

		VR_SetConfigFloat(VR_CONFIG_RECENTER_YAW, VR_GetConfigFloat(VR_CONFIG_RECENTER_YAW) + hmdorientation.y);
		float recenterYaw = ToRadians(VR_GetConfigFloat(VR_CONFIG_RECENTER_YAW));
		spaceCreateInfo.poseInReferenceSpace.orientation.x = 0;
		spaceCreateInfo.poseInReferenceSpace.orientation.y = sinf(recenterYaw / 2);
		spaceCreateInfo.poseInReferenceSpace.orientation.z = 0;
		spaceCreateInfo.poseInReferenceSpace.orientation.w = cosf(recenterYaw / 2);
	}

	// Delete previous space instances
	if (engine->appState.StageSpace != XR_NULL_HANDLE) {
		OXR(xrDestroySpace(engine->appState.StageSpace));
	}
	if (engine->appState.FakeStageSpace != XR_NULL_HANDLE) {
		OXR(xrDestroySpace(engine->appState.FakeStageSpace));
	}

	// Create a default stage space to use if SPACE_TYPE_STAGE is not
	// supported, or calls to xrGetReferenceSpaceBoundsRect fail.
	spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
	spaceCreateInfo.poseInReferenceSpace = {};
	spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0;
	if (VR_GetPlatformFlag(VR_PLATFORM_TRACKING_FLOOR)) {
		spaceCreateInfo.poseInReferenceSpace.position.y = -1.6750f;
	}
	OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.FakeStageSpace));
	ALOGV("Created fake stage space from local space with offset");
	engine->appState.CurrentSpace = engine->appState.FakeStageSpace;

	if (stageSupported) {
		spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
		spaceCreateInfo.poseInReferenceSpace = {};
		spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0;
		OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.StageSpace));
		ALOGV("Created stage space");
		if (VR_GetPlatformFlag(VR_PLATFORM_TRACKING_FLOOR)) {
			engine->appState.CurrentSpace = engine->appState.StageSpace;
		}
	}

	// Update menu orientation
	VR_SetConfigFloat(VR_CONFIG_MENU_PITCH, hmdorientation.x);
	VR_SetConfigFloat(VR_CONFIG_MENU_YAW, 0.0f);
}

void VR_InitRenderer( engine_t* engine ) {
	if (initialized) {
		VR_DestroyRenderer(engine);
	}

	if (VR_GetPlatformFlag(VRPlatformFlag::VR_PLATFORM_EXTENSION_PASSTHROUGH)) {
		INIT_PFN(xrCreatePassthroughFB);
		INIT_PFN(xrDestroyPassthroughFB);
		INIT_PFN(xrPassthroughStartFB);
		INIT_PFN(xrPassthroughPauseFB);
		INIT_PFN(xrCreatePassthroughLayerFB);
		INIT_PFN(xrDestroyPassthroughLayerFB);
		INIT_PFN(xrPassthroughLayerPauseFB);
		INIT_PFN(xrPassthroughLayerResumeFB);
	}

	int eyeW, eyeH;
	VR_GetResolution(engine, &eyeW, &eyeH);
	VR_SetConfig(VR_CONFIG_VIEWPORT_WIDTH, eyeW);
	VR_SetConfig(VR_CONFIG_VIEWPORT_HEIGHT, eyeH);

	// Get the viewport configuration info for the chosen viewport configuration type.
	engine->appState.ViewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;
	OXR(xrGetViewConfigurationProperties(engine->appState.Instance, engine->appState.SystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &engine->appState.ViewportConfig));

	uint32_t numOutputSpaces = 0;
	OXR(xrEnumerateReferenceSpaces(engine->appState.Session, 0, &numOutputSpaces, NULL));
	XrReferenceSpaceType* referenceSpaces = (XrReferenceSpaceType*)malloc(numOutputSpaces * sizeof(XrReferenceSpaceType));
	OXR(xrEnumerateReferenceSpaces(engine->appState.Session, numOutputSpaces, &numOutputSpaces, referenceSpaces));

	for (uint32_t i = 0; i < numOutputSpaces; i++) {
		if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
			stageSupported = true;
			break;
		}
	}

	free(referenceSpaces);

	if (engine->appState.CurrentSpace == XR_NULL_HANDLE) {
		VR_Recenter(engine);
	}

	projections = (XrView*)(malloc(ovrMaxNumEyes * sizeof(XrView)));
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		memset(&projections[eye], 0, sizeof(XrView));
		projections[eye].type = XR_TYPE_VIEW;
	}

	void* vulkanContext = nullptr;
	if (VR_GetPlatformFlag(VR_PLATFORM_RENDERER_VULKAN)) {
		vulkanContext = &engine->graphicsBindingVulkan;
	}
	ovrRenderer_Create(engine->appState.Session, &engine->appState.Renderer, eyeW, eyeH, vulkanContext);

	if (VR_GetPlatformFlag(VRPlatformFlag::VR_PLATFORM_EXTENSION_PASSTHROUGH)) {
		XrPassthroughCreateInfoFB ptci = {XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
		XrResult result;
		OXR(result = xrCreatePassthroughFB(engine->appState.Session, &ptci, &passthrough));

		if (XR_SUCCEEDED(result)) {
			XrPassthroughLayerCreateInfoFB plci = {XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
			plci.passthrough = passthrough;
			plci.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
			OXR(xrCreatePassthroughLayerFB(engine->appState.Session, &plci, &passthroughLayer));
		}

		OXR(xrPassthroughStartFB(passthrough));
	}
	initialized = true;
}

void VR_DestroyRenderer( engine_t* engine ) {
	if (VR_GetPlatformFlag(VRPlatformFlag::VR_PLATFORM_EXTENSION_PASSTHROUGH)) {
		if (passthroughRunning) {
			OXR(xrPassthroughLayerPauseFB(passthroughLayer));
		}
		OXR(xrPassthroughPauseFB(passthrough));
		OXR(xrDestroyPassthroughFB(passthrough));
		passthrough = XR_NULL_HANDLE;
	}
	ovrRenderer_Destroy(&engine->appState.Renderer);
	free(projections);
	initialized = false;
}

bool VR_InitFrame( engine_t* engine ) {
	bool stageBoundsDirty = true;
	if (ovrApp_HandleXrEvents(&engine->appState)) {
		VR_Recenter(engine);
	}
	if (engine->appState.SessionActive == false) {
		return false;
	}

	if (stageBoundsDirty) {
		VR_UpdateStageBounds(&engine->appState);
		stageBoundsDirty = false;
	}

	// Update passthrough
	if (passthroughRunning != (VR_GetConfig(VR_CONFIG_PASSTHROUGH) != 0)) {
		if (VR_GetConfig(VR_CONFIG_PASSTHROUGH)) {
			OXR(xrPassthroughLayerResumeFB(passthroughLayer));
		} else {
			OXR(xrPassthroughLayerPauseFB(passthroughLayer));
		}
		passthroughRunning = (VR_GetConfig(VR_CONFIG_PASSTHROUGH) != 0);
	}

	// NOTE: OpenXR does not use the concept of frame indices. Instead,
	// XrWaitFrame returns the predicted display time.
	XrFrameWaitInfo waitFrameInfo = {};
	waitFrameInfo.type = XR_TYPE_FRAME_WAIT_INFO;
	waitFrameInfo.next = NULL;

	frameState.type = XR_TYPE_FRAME_STATE;
	frameState.next = NULL;

	OXR(xrWaitFrame(engine->appState.Session, &waitFrameInfo, &frameState));
	engine->predictedDisplayTime = frameState.predictedDisplayTime;

	XrViewLocateInfo projectionInfo = {};
	projectionInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
	projectionInfo.next = NULL;
	projectionInfo.viewConfigurationType = engine->appState.ViewportConfig.viewConfigurationType;
	projectionInfo.displayTime = frameState.predictedDisplayTime;
	projectionInfo.space = engine->appState.CurrentSpace;

	XrViewState viewState = {XR_TYPE_VIEW_STATE, NULL};

	uint32_t projectionCapacityInput = ovrMaxNumEyes;
	uint32_t projectionCountOutput = projectionCapacityInput;

	OXR(xrLocateViews(
			engine->appState.Session,
			&projectionInfo,
			&viewState,
			projectionCapacityInput,
			&projectionCountOutput,
			projections));

	// Get the HMD pose, predicted for the middle of the time period during which
	// the new eye images will be displayed. The number of frames predicted ahead
	// depends on the pipeline depth of the engine and the synthesis rate.
	// The better the prediction, the less black will be pulled in at the edges.
	XrFrameBeginInfo beginFrameDesc = {};
	beginFrameDesc.type = XR_TYPE_FRAME_BEGIN_INFO;
	beginFrameDesc.next = NULL;
	OXR(xrBeginFrame(engine->appState.Session, &beginFrameDesc));

	fov = {};
	for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
		fov.angleLeft += projections[eye].fov.angleLeft / 2.0f;
		fov.angleRight += projections[eye].fov.angleRight / 2.0f;
		fov.angleUp += projections[eye].fov.angleUp / 2.0f;
		fov.angleDown += projections[eye].fov.angleDown / 2.0f;
		invViewTransform[eye] = projections[eye].pose;
	}

	// Update HMD and controllers
	hmdorientation = XrQuaternionf_ToEulerAngles(invViewTransform[0].orientation);
	IN_VRInputFrame(engine);

	engine->appState.LayerCount = 0;
	memset(engine->appState.Layers, 0, sizeof(ovrCompositorLayer_Union) * ovrMaxLayerCount);
	return true;
}

void VR_BeginFrame( engine_t* engine, int fboIndex ) {
	vrConfig[VR_CONFIG_CURRENT_FBO] = fboIndex;
	ovrFramebuffer_Acquire(&engine->appState.Renderer.FrameBuffer[fboIndex]);
}

void VR_EndFrame( engine_t* engine ) {
	int fboIndex = vrConfig[VR_CONFIG_CURRENT_FBO];
	VR_BindFramebuffer(engine);

	// Show mouse cursor
	int vrMode = vrConfig[VR_CONFIG_MODE];
	bool screenMode = (vrMode == VR_MODE_MONO_SCREEN) || (vrMode == VR_MODE_STEREO_SCREEN);
	if (screenMode && (vrConfig[VR_CONFIG_MOUSE_SIZE] > 0)) {
		int x = vrConfig[VR_CONFIG_MOUSE_X];
		int y = vrConfig[VR_CONFIG_MOUSE_Y];
		int sx = vrConfig[VR_CONFIG_MOUSE_SIZE];
		int sy = (int)((float)sx * VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT));
		ovrRenderer_MouseCursor(&engine->appState.Renderer, x, y, sx, sy);
	}

	ovrFramebuffer_Release(&engine->appState.Renderer.FrameBuffer[fboIndex]);
}

void VR_FinishFrame( engine_t* engine ) {
	int vrMode = vrConfig[VR_CONFIG_MODE];
	XrCompositionLayerProjectionView projection_layer_elements[2] = {};
	bool headTracking = (vrMode == VR_MODE_MONO_6DOF) || (vrMode == VR_MODE_SBS_6DOF) || (vrMode == VR_MODE_STEREO_6DOF);
	bool reprojection = vrConfig[VR_CONFIG_REPROJECTION];
	if (headTracking && reprojection) {
		VR_SetConfigFloat(VR_CONFIG_MENU_YAW, hmdorientation.y);

		for (int eye = 0; eye < ovrMaxNumEyes; eye++) {;
			ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer[0];
			XrPosef pose = invViewTransform[0];
			if (vrMode != VR_MODE_MONO_6DOF) {
				pose = invViewTransform[eye];
			}
			if (vrMode == VR_MODE_STEREO_6DOF) {
				frameBuffer = &engine->appState.Renderer.FrameBuffer[eye];
			}

			memset(&projection_layer_elements[eye], 0, sizeof(XrCompositionLayerProjectionView));
			projection_layer_elements[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
			projection_layer_elements[eye].pose = pose;
			projection_layer_elements[eye].fov = fov;

			memset(&projection_layer_elements[eye].subImage, 0, sizeof(XrSwapchainSubImage));
			projection_layer_elements[eye].subImage.swapchain = frameBuffer->ColorSwapChain.Handle;
			projection_layer_elements[eye].subImage.imageRect.offset.x = 0;
			projection_layer_elements[eye].subImage.imageRect.offset.y = 0;
			projection_layer_elements[eye].subImage.imageRect.extent.width = frameBuffer->ColorSwapChain.Width;
			projection_layer_elements[eye].subImage.imageRect.extent.height = frameBuffer->ColorSwapChain.Height;
			projection_layer_elements[eye].subImage.imageArrayIndex = 0;

			if (vrMode == VR_MODE_SBS_6DOF) {
				projection_layer_elements[eye].subImage.imageRect.extent.width /= 2;
				if (eye == 1) {
					projection_layer_elements[eye].subImage.imageRect.offset.x += frameBuffer->ColorSwapChain.Width / 2;
				}
			}
		}

		XrCompositionLayerProjection projection_layer = {};
		projection_layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
		projection_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		projection_layer.layerFlags |= XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
		projection_layer.space = engine->appState.CurrentSpace;
		projection_layer.viewCount = ovrMaxNumEyes;
		projection_layer.views = projection_layer_elements;

		engine->appState.Layers[engine->appState.LayerCount++].Projection = projection_layer;
	} else {

		// Flat screen pose
		float distance = VR_GetConfigFloat(VR_CONFIG_CANVAS_DISTANCE) / 4.0f - 1.0f;
		float menuPitch = ToRadians(VR_GetConfigFloat(VR_CONFIG_MENU_PITCH));
		float menuYaw = ToRadians(VR_GetConfigFloat(VR_CONFIG_MENU_YAW));
		XrVector3f pos = {-sinf(menuYaw) * distance, 0, -cosf(menuYaw) * distance};
		if (!VR_GetConfig(VR_CONFIG_CANVAS_6DOF)) {
			pos.x += invViewTransform[0].position.x;
			pos.y += invViewTransform[0].position.y;
			pos.z += invViewTransform[0].position.z;
		}
		XrQuaternionf pitch = XrQuaternionf_CreateFromVectorAngle({1, 0, 0}, -menuPitch);
		XrQuaternionf yaw = XrQuaternionf_CreateFromVectorAngle({0, 1, 0}, menuYaw);

		// Setup the cylinder layer
		XrCompositionLayerCylinderKHR cylinder_layer = {};
		cylinder_layer.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
		cylinder_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
		cylinder_layer.space = engine->appState.CurrentSpace;
		memset(&cylinder_layer.subImage, 0, sizeof(XrSwapchainSubImage));
		cylinder_layer.subImage.imageRect.offset.x = 0;
		cylinder_layer.subImage.imageRect.offset.y = 0;
		cylinder_layer.subImage.imageRect.extent.width = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Width;
		cylinder_layer.subImage.imageRect.extent.height = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Height;
		cylinder_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Handle;
		cylinder_layer.subImage.imageArrayIndex = 0;
		cylinder_layer.pose.orientation = XrQuaternionf_Multiply(pitch, yaw);
		cylinder_layer.pose.position = pos;
		cylinder_layer.radius = 2.0f;
		cylinder_layer.centralAngle = (float)(M_PI * 0.5);
		cylinder_layer.aspectRatio = VR_GetConfigFloat(VR_CONFIG_CANVAS_ASPECT);
		if (headTracking && !reprojection) {
			float width = (float)engine->appState.ViewConfigurationView[0].recommendedImageRectWidth;
			float height = (float)engine->appState.ViewConfigurationView[0].recommendedImageRectHeight;
			cylinder_layer.aspectRatio = 2.0f * width / height;
			cylinder_layer.centralAngle = (float)(M_PI);
		}

		// Build the cylinder layer
		if ((vrMode == VR_MODE_MONO_SCREEN) || (vrMode == VR_MODE_MONO_6DOF)) {
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
			engine->appState.Layers[engine->appState.LayerCount++].Cylinder = cylinder_layer;
		} else if ((vrMode == VR_MODE_SBS_SCREEN) || (vrMode == VR_MODE_SBS_6DOF)) {
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
			cylinder_layer.subImage.imageRect.extent.width /= 2;
			engine->appState.Layers[engine->appState.LayerCount++].Cylinder = cylinder_layer;
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
			cylinder_layer.subImage.imageRect.offset.x += cylinder_layer.subImage.imageRect.extent.width;
			engine->appState.Layers[engine->appState.LayerCount++].Cylinder = cylinder_layer;
		} else {
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
			engine->appState.Layers[engine->appState.LayerCount++].Cylinder = cylinder_layer;
			cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;
			cylinder_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer[1].ColorSwapChain.Handle;
			engine->appState.Layers[engine->appState.LayerCount++].Cylinder = cylinder_layer;
		}
	}

	// Compose the layers for this frame.
	const XrCompositionLayerBaseHeader* layers[ovrMaxLayerCount] = {};
	for (int i = 0; i < engine->appState.LayerCount; i++) {
		layers[i] = (const XrCompositionLayerBaseHeader*)&engine->appState.Layers[i];
	}

	XrFrameEndInfo endFrameInfo = {};
	endFrameInfo.type = XR_TYPE_FRAME_END_INFO;
	endFrameInfo.displayTime = frameState.predictedDisplayTime;
	endFrameInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endFrameInfo.layerCount = engine->appState.LayerCount;
	endFrameInfo.layers = layers;
	OXR(xrEndFrame(engine->appState.Session, &endFrameInfo));
}

int VR_GetConfig( VRConfig config ) {
	return vrConfig[config];
}

void VR_SetConfig( VRConfig config, int value) {
	vrConfig[config] = value;
}

float VR_GetConfigFloat(VRConfigFloat config) {
	return vrConfigFloat[config];
}

void VR_SetConfigFloat(VRConfigFloat config, float value) {
	vrConfigFloat[config] = value;
}

void* VR_BindFramebuffer(engine_t *engine) {
	if (!initialized) return nullptr;
	int fboIndex = VR_GetConfig(VR_CONFIG_CURRENT_FBO);
	return ovrFramebuffer_SetCurrent(&engine->appState.Renderer.FrameBuffer[fboIndex]);
}

XrView VR_GetView(int eye) {
	return projections[eye];
}

XrVector3f VR_GetHMDAngles() {
	return hmdorientation;
}
