#include "VRBase.h"
#include "VRRenderer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

XrView* projections;
GLboolean stageSupported = GL_FALSE;
float menuYaw = 0;

void VR_UpdateStageBounds(ovrApp* pappState) {
    XrExtent2Df stageBounds = {};

    XrResult result;
    OXR(result = xrGetReferenceSpaceBoundsRect(
            pappState->Session, XR_REFERENCE_SPACE_TYPE_STAGE, &stageBounds));
    if (result != XR_SUCCESS) {
        ALOGV("Stage bounds query failed: using small defaults");
        stageBounds.width = 1.0f;
        stageBounds.height = 1.0f;

        pappState->CurrentSpace = pappState->FakeStageSpace;
    }

    ALOGV("Stage bounds: width = %f, depth %f", stageBounds.width, stageBounds.height);
}

void VR_GetResolution(engine_t* engine, int *pWidth, int *pHeight)
{
	static int width = 0;
	static int height = 0;
	
	if (engine)
	{
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
	}
	else
	{
		//use cached values
		*pWidth = width;
		*pHeight = height;
	}
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
        //TODO:rewrite
        /*vec3_t rotation = {0, 0, 0};
		QuatToYawPitchRoll(loc.pose.orientation, rotation, vr.hmdorientation);

        vr.recenterYaw += radians(vr.hmdorientation[YAW]);
        spaceCreateInfo.poseInReferenceSpace.orientation.x = 0;
        spaceCreateInfo.poseInReferenceSpace.orientation.y = sin(vr.recenterYaw / 2);
        spaceCreateInfo.poseInReferenceSpace.orientation.z = 0;
        spaceCreateInfo.poseInReferenceSpace.orientation.w = cos(vr.recenterYaw / 2);*/
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
    spaceCreateInfo.poseInReferenceSpace.position.y = -1.6750f;
    OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.FakeStageSpace));
    ALOGV("Created fake stage space from local space with offset");
    engine->appState.CurrentSpace = engine->appState.FakeStageSpace;

    if (stageSupported) {
        spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        spaceCreateInfo.poseInReferenceSpace.position.y = 0.0;
        OXR(xrCreateReferenceSpace(engine->appState.Session, &spaceCreateInfo, &engine->appState.StageSpace));
        ALOGV("Created stage space");
        engine->appState.CurrentSpace = engine->appState.StageSpace;
    }

    // Update menu orientation
    menuYaw = 0;
}

void VR_InitRenderer( engine_t* engine ) {
#if ENABLE_GL_DEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(VR_GLDebugLog, 0);
#endif

	int eyeW, eyeH;
    VR_GetResolution(engine, &eyeW, &eyeH);

    // Get the viewport configuration info for the chosen viewport configuration type.
    engine->appState.ViewportConfig.type = XR_TYPE_VIEW_CONFIGURATION_PROPERTIES;

    OXR(xrGetViewConfigurationProperties(
            engine->appState.Instance, engine->appState.SystemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, &engine->appState.ViewportConfig));

    uint32_t numOutputSpaces = 0;
    OXR(xrEnumerateReferenceSpaces(engine->appState.Session, 0, &numOutputSpaces, NULL));

    XrReferenceSpaceType* referenceSpaces =
            (XrReferenceSpaceType*)malloc(numOutputSpaces * sizeof(XrReferenceSpaceType));

    OXR(xrEnumerateReferenceSpaces(
            engine->appState.Session, numOutputSpaces, &numOutputSpaces, referenceSpaces));

    for (uint32_t i = 0; i < numOutputSpaces; i++) {
        if (referenceSpaces[i] == XR_REFERENCE_SPACE_TYPE_STAGE) {
            stageSupported = GL_TRUE;
            break;
        }
    }

    free(referenceSpaces);

    if (engine->appState.CurrentSpace == XR_NULL_HANDLE) {
        VR_Recenter(engine);
    }

    projections = (XrView*)(malloc(ovrMaxNumEyes * sizeof(XrView)));

    ovrRenderer_Create(
            engine->appState.Session,
            &engine->appState.Renderer,
            engine->appState.ViewConfigurationView[0].recommendedImageRectWidth,
            engine->appState.ViewConfigurationView[0].recommendedImageRectHeight);
}

void VR_DestroyRenderer( engine_t* engine )
{
    ovrRenderer_Destroy(&engine->appState.Renderer);
    free(projections);
}

void VR_ReInitRenderer()
{
    VR_DestroyRenderer( VR_GetEngine() );
    VR_InitRenderer( VR_GetEngine() );
}

void VR_ClearFrameBuffer( int width, int height)
{
    glEnable( GL_SCISSOR_TEST );
    glViewport( 0, 0, width, height );

    glClearColor( 0.0f, 0.5f, 1.0f, 1.0f );

    glScissor( 0, 0, width, height );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glScissor( 0, 0, 0, 0 );
    glDisable( GL_SCISSOR_TEST );
}

void VR_DrawFrame( engine_t* engine ) {

    GLboolean stageBoundsDirty = GL_TRUE;
    if (ovrApp_HandleXrEvents(&engine->appState)) {
        VR_Recenter(engine);
    }
    if (engine->appState.SessionActive == GL_FALSE) {
        return;
    }

    if (stageBoundsDirty) {
        VR_UpdateStageBounds(&engine->appState);
        stageBoundsDirty = GL_FALSE;
    }

    // NOTE: OpenXR does not use the concept of frame indices. Instead,
    // XrWaitFrame returns the predicted display time.
    XrFrameWaitInfo waitFrameInfo = {};
    waitFrameInfo.type = XR_TYPE_FRAME_WAIT_INFO;
    waitFrameInfo.next = NULL;

    XrFrameState frameState = {};
    frameState.type = XR_TYPE_FRAME_STATE;
    frameState.next = NULL;

    OXR(xrWaitFrame(engine->appState.Session, &waitFrameInfo, &frameState));
    engine->predictedDisplayTime = frameState.predictedDisplayTime;
    if (!frameState.shouldRender) {
        return;
    }

    // Get the HMD pose, predicted for the middle of the time period during which
    // the new eye images will be displayed. The number of frames predicted ahead
    // depends on the pipeline depth of the engine and the synthesis rate.
    // The better the prediction, the less black will be pulled in at the edges.
    XrFrameBeginInfo beginFrameDesc = {};
    beginFrameDesc.type = XR_TYPE_FRAME_BEGIN_INFO;
    beginFrameDesc.next = NULL;
    OXR(xrBeginFrame(engine->appState.Session, &beginFrameDesc));

    XrViewLocateInfo projectionInfo = {};
    projectionInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
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
    //

    XrFovf fov = {};
    XrPosef invViewTransform[2];
    for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
        invViewTransform[eye] = projections[eye].pose;

        fov.angleLeft += projections[eye].fov.angleLeft / 2.0f;
        fov.angleRight += projections[eye].fov.angleRight / 2.0f;
        fov.angleUp += projections[eye].fov.angleUp / 2.0f;
        fov.angleDown += projections[eye].fov.angleDown / 2.0f;
    }

    //TODO: Update HMD and controllers
    /*IN_VRUpdateHMD( invViewTransform[0] );
    IN_VRUpdateControllers( frameState.predictedDisplayTime );
    IN_VRSyncActions();*/

    const ovrMatrix4f projectionMatrix = ovrMatrix4f_CreateProjectionFov(
            fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown, 1.0f, 0.0f );

    engine->appState.LayerCount = 0;
    memset(engine->appState.Layers, 0, sizeof(ovrCompositorLayer_Union) * ovrMaxLayerCount);

	//TODO:
    /*re.SetVRHeadsetParms(projectionMatrix.M,
                         engine->appState.Renderer.FrameBuffer[0].FrameBuffers[engine->appState.Renderer.FrameBuffer[0].TextureSwapChainIndex],
                         engine->appState.Renderer.FrameBuffer[1].FrameBuffers[engine->appState.Renderer.FrameBuffer[1].TextureSwapChainIndex]);*/

    for (int eye = 0; eye < ovrMaxNumEyes; eye++)
    {
        ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer[eye];
        int swapchainIndex = frameBuffer->TextureSwapChainIndex;
        int glFramebuffer = frameBuffer->FrameBuffers[swapchainIndex];

        ovrFramebuffer_Acquire(frameBuffer);
        ovrFramebuffer_SetCurrent(frameBuffer);
        VR_ClearFrameBuffer(frameBuffer->ColorSwapChain.Width, frameBuffer->ColorSwapChain.Height);
    }

    //TODO:Com_Frame();

    for (int eye = 0; eye < ovrMaxNumEyes; eye++)
    {

        // Clear the alpha channel, other way OpenXR would not transfer the framebuffer fully
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        glClearColor(0.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer[eye];
        ovrFramebuffer_Resolve(frameBuffer);
        ovrFramebuffer_Release(frameBuffer);
    }
    ovrFramebuffer_SetNone();

    XrCompositionLayerProjectionView projection_layer_elements[2] = {};
    if (false) {
        //TODO:vr.menuYaw = vr.hmdorientation[YAW];

        for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
            ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer[eye];

            memset(&projection_layer_elements[eye], 0, sizeof(XrCompositionLayerProjectionView));
            projection_layer_elements[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projection_layer_elements[eye].pose = invViewTransform[eye];
            projection_layer_elements[eye].fov = fov;

            memset(&projection_layer_elements[eye].subImage, 0, sizeof(XrSwapchainSubImage));
            projection_layer_elements[eye].subImage.swapchain = frameBuffer->ColorSwapChain.Handle;
            projection_layer_elements[eye].subImage.imageRect.offset.x = 0;
            projection_layer_elements[eye].subImage.imageRect.offset.y = 0;
            projection_layer_elements[eye].subImage.imageRect.extent.width = frameBuffer->ColorSwapChain.Width;
            projection_layer_elements[eye].subImage.imageRect.extent.height = frameBuffer->ColorSwapChain.Height;
            projection_layer_elements[eye].subImage.imageArrayIndex = 0;
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

        // Build the cylinder layer
        int width = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Width;
        int height = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Height;
        XrCompositionLayerCylinderKHR cylinder_layer = {};
        cylinder_layer.type = XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;
        cylinder_layer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        cylinder_layer.space = engine->appState.CurrentSpace;
        cylinder_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        memset(&cylinder_layer.subImage, 0, sizeof(XrSwapchainSubImage));
        cylinder_layer.subImage.swapchain = engine->appState.Renderer.FrameBuffer[0].ColorSwapChain.Handle;
        cylinder_layer.subImage.imageRect.offset.x = 0;
        cylinder_layer.subImage.imageRect.offset.y = 0;
        cylinder_layer.subImage.imageRect.extent.width = width;
        cylinder_layer.subImage.imageRect.extent.height = height;
        cylinder_layer.subImage.imageArrayIndex = 0;
        const XrVector3f axis = {0.0f, 1.0f, 0.0f};
		float yaw = menuYaw * 180.0f / M_PI;
        XrVector3f pos = {
                invViewTransform[0].position.x - sin(yaw) * 4.0f,
                invViewTransform[0].position.y,
                invViewTransform[0].position.z - cos(yaw) * 4.0f
        };
        cylinder_layer.pose.orientation = XrQuaternionf_CreateFromVectorAngle(axis, yaw);
        cylinder_layer.pose.position = pos;
        cylinder_layer.radius = 12.0f;
        cylinder_layer.centralAngle = MATH_PI * 0.5f;
        cylinder_layer.aspectRatio = width / (float)height / 0.75f;

        engine->appState.Layers[engine->appState.LayerCount++].Cylinder = cylinder_layer;
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
    for (int eye = 0; eye < ovrMaxNumEyes; eye++) {
        ovrFramebuffer* frameBuffer = &engine->appState.Renderer.FrameBuffer[eye];
        frameBuffer->TextureSwapChainIndex++;
        frameBuffer->TextureSwapChainIndex %= frameBuffer->TextureSwapChainLength;
    }
}
