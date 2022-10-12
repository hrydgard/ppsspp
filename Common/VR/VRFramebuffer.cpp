#include "VRFramebuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <assert.h>

double FromXrTime(const XrTime time) {
	return (time * 1e-9);
}

/*
================================================================================

ovrFramebuffer

================================================================================
*/


void ovrFramebuffer_Clear(ovrFramebuffer* frameBuffer) {
	frameBuffer->Width = 0;
	frameBuffer->Height = 0;
	frameBuffer->TextureSwapChainLength = 0;
	frameBuffer->TextureSwapChainIndex = 0;
	frameBuffer->ColorSwapChain.Handle = XR_NULL_HANDLE;
	frameBuffer->ColorSwapChain.Width = 0;
	frameBuffer->ColorSwapChain.Height = 0;
	frameBuffer->ColorSwapChainImage = NULL;
	frameBuffer->DepthSwapChain.Handle = XR_NULL_HANDLE;
	frameBuffer->DepthSwapChain.Width = 0;
	frameBuffer->DepthSwapChain.Height = 0;
	frameBuffer->DepthSwapChainImage = NULL;

	frameBuffer->GLFrameBuffers = NULL;
	frameBuffer->Acquired = false;
}

bool ovrFramebuffer_CreateGL(XrSession session, ovrFramebuffer* frameBuffer, int width, int height, bool multiview) {

	frameBuffer->Width = width;
	frameBuffer->Height = height;
	frameBuffer->UseVulkan = false;

	if (strstr((const char*)glGetString(GL_EXTENSIONS), "GL_OVR_multiview2") == nullptr)
	{
		ALOGE("OpenGL implementation does not support GL_OVR_multiview2 extension.\n");
	}

	typedef void (*PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVR)(GLenum, GLenum, GLuint, GLint, GLint, GLsizei);
	auto glFramebufferTextureMultiviewOVR = (PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVR)eglGetProcAddress ("glFramebufferTextureMultiviewOVR");
	if (!glFramebufferTextureMultiviewOVR)
	{
		ALOGE("Can not get proc address for glFramebufferTextureMultiviewOVR.\n");
	}

	XrSwapchainCreateInfo swapChainCreateInfo;
	memset(&swapChainCreateInfo, 0, sizeof(swapChainCreateInfo));
	swapChainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	swapChainCreateInfo.sampleCount = 1;
	swapChainCreateInfo.width = width;
	swapChainCreateInfo.height = height;
	swapChainCreateInfo.faceCount = 1;
	swapChainCreateInfo.mipCount = 1;
	swapChainCreateInfo.arraySize = multiview ? 2 : 1;

	frameBuffer->ColorSwapChain.Width = swapChainCreateInfo.width;
	frameBuffer->ColorSwapChain.Height = swapChainCreateInfo.height;
	frameBuffer->DepthSwapChain.Width = swapChainCreateInfo.width;
	frameBuffer->DepthSwapChain.Height = swapChainCreateInfo.height;

	// Create the color swapchain.
	swapChainCreateInfo.format = GL_SRGB8_ALPHA8;
	swapChainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	OXR(xrCreateSwapchain(session, &swapChainCreateInfo, &frameBuffer->ColorSwapChain.Handle));
	OXR(xrEnumerateSwapchainImages(frameBuffer->ColorSwapChain.Handle, 0, &frameBuffer->TextureSwapChainLength, NULL));
	frameBuffer->ColorSwapChainImage = malloc(frameBuffer->TextureSwapChainLength * sizeof(XrSwapchainImageOpenGLESKHR));

	// Create the depth swapchain.
	swapChainCreateInfo.format = GL_DEPTH24_STENCIL8;
	swapChainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	OXR(xrCreateSwapchain(session, &swapChainCreateInfo, &frameBuffer->DepthSwapChain.Handle));
	frameBuffer->DepthSwapChainImage = malloc(frameBuffer->TextureSwapChainLength * sizeof(XrSwapchainImageOpenGLESKHR));

	// Populate the swapchain image array.
	for (uint32_t i = 0; i < frameBuffer->TextureSwapChainLength; i++) {
		((XrSwapchainImageOpenGLESKHR*)frameBuffer->ColorSwapChainImage)[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
		((XrSwapchainImageOpenGLESKHR*)frameBuffer->ColorSwapChainImage)[i].next = NULL;
		((XrSwapchainImageOpenGLESKHR*)frameBuffer->DepthSwapChainImage)[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
		((XrSwapchainImageOpenGLESKHR*)frameBuffer->DepthSwapChainImage)[i].next = NULL;
	}
	OXR(xrEnumerateSwapchainImages(
			frameBuffer->ColorSwapChain.Handle,
			frameBuffer->TextureSwapChainLength,
			&frameBuffer->TextureSwapChainLength,
			(XrSwapchainImageBaseHeader*)frameBuffer->ColorSwapChainImage));
	OXR(xrEnumerateSwapchainImages(
			frameBuffer->DepthSwapChain.Handle,
			frameBuffer->TextureSwapChainLength,
			&frameBuffer->TextureSwapChainLength,
			(XrSwapchainImageBaseHeader*)frameBuffer->DepthSwapChainImage));

	frameBuffer->GLFrameBuffers = (GLuint*)malloc(frameBuffer->TextureSwapChainLength * sizeof(GLuint));
	for (uint32_t i = 0; i < frameBuffer->TextureSwapChainLength; i++) {
		const GLuint colorTexture = ((XrSwapchainImageOpenGLESKHR*)frameBuffer->ColorSwapChainImage)[i].image;
		const GLuint depthTexture = ((XrSwapchainImageOpenGLESKHR*)frameBuffer->DepthSwapChainImage)[i].image;

		// Create the frame buffer.
		GL(glGenFramebuffers(1, &frameBuffer->GLFrameBuffers[i]));
		GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->GLFrameBuffers[i]));
		if (multiview) {
			GL(glFramebufferTextureMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, depthTexture, 0, 0, 2));
			GL(glFramebufferTextureMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthTexture, 0, 0, 2));
			GL(glFramebufferTextureMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, colorTexture, 0, 0, 2));
		} else {
			GL(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0));
			GL(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0));
			GL(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0));
		}
		GL(GLenum renderFramebufferStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
		GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
		if (renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
			ALOGE("Incomplete frame buffer object: %d", renderFramebufferStatus);
			return false;
		}
	}

	return true;
}

bool ovrFramebuffer_CreateVK(XrSession session, ovrFramebuffer* frameBuffer, int width, int height,
							 bool multiview, void* context) {

	frameBuffer->Width = width;
	frameBuffer->Height = height;
	frameBuffer->UseVulkan = true;
	frameBuffer->VKContext = (XrGraphicsBindingVulkanKHR*)context;

	XrSwapchainCreateInfo swapChainCreateInfo;
	memset(&swapChainCreateInfo, 0, sizeof(swapChainCreateInfo));
	swapChainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	swapChainCreateInfo.sampleCount = 1;
	swapChainCreateInfo.width = width;
	swapChainCreateInfo.height = height;
	swapChainCreateInfo.faceCount = 1;
	swapChainCreateInfo.mipCount = 1;
	swapChainCreateInfo.arraySize = multiview ? 2 : 1;

	frameBuffer->ColorSwapChain.Width = swapChainCreateInfo.width;
	frameBuffer->ColorSwapChain.Height = swapChainCreateInfo.height;
	frameBuffer->DepthSwapChain.Width = swapChainCreateInfo.width;
	frameBuffer->DepthSwapChain.Height = swapChainCreateInfo.height;

	// Create the color swapchain.
	swapChainCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	swapChainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	OXR(xrCreateSwapchain(session, &swapChainCreateInfo, &frameBuffer->ColorSwapChain.Handle));
	OXR(xrEnumerateSwapchainImages(frameBuffer->ColorSwapChain.Handle, 0, &frameBuffer->TextureSwapChainLength, NULL));
	frameBuffer->ColorSwapChainImage = malloc(frameBuffer->TextureSwapChainLength * sizeof(XrSwapchainImageVulkanKHR));

	// Create the depth swapchain.
	swapChainCreateInfo.format = VK_FORMAT_D24_UNORM_S8_UINT;
	swapChainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	OXR(xrCreateSwapchain(session, &swapChainCreateInfo, &frameBuffer->DepthSwapChain.Handle));
	frameBuffer->DepthSwapChainImage = malloc(frameBuffer->TextureSwapChainLength * sizeof(XrSwapchainImageVulkanKHR));

	// Populate the swapchain image array.
	for (uint32_t i = 0; i < frameBuffer->TextureSwapChainLength; i++) {
		((XrSwapchainImageVulkanKHR*)frameBuffer->ColorSwapChainImage)[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
		((XrSwapchainImageVulkanKHR*)frameBuffer->ColorSwapChainImage)[i].next = NULL;
		((XrSwapchainImageVulkanKHR*)frameBuffer->DepthSwapChainImage)[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
		((XrSwapchainImageVulkanKHR*)frameBuffer->DepthSwapChainImage)[i].next = NULL;
	}
	OXR(xrEnumerateSwapchainImages(
			frameBuffer->ColorSwapChain.Handle,
			frameBuffer->TextureSwapChainLength,
			&frameBuffer->TextureSwapChainLength,
			(XrSwapchainImageBaseHeader*)frameBuffer->ColorSwapChainImage));
	OXR(xrEnumerateSwapchainImages(
			frameBuffer->DepthSwapChain.Handle,
			frameBuffer->TextureSwapChainLength,
			&frameBuffer->TextureSwapChainLength,
			(XrSwapchainImageBaseHeader*)frameBuffer->DepthSwapChainImage));

	frameBuffer->VKFrameBuffers = (VkFramebuffer*)malloc(frameBuffer->TextureSwapChainLength * sizeof(VkFramebuffer));
	for (uint32_t i = 0; i < frameBuffer->TextureSwapChainLength; i++) {
		VkImage colorTexture = ((XrSwapchainImageVulkanKHR*)frameBuffer->ColorSwapChainImage)[i].image;
		VkImage depthTexture = ((XrSwapchainImageVulkanKHR*)frameBuffer->DepthSwapChainImage)[i].image;

		// Create the frame buffer.
		//TODO:implement framebuffer creation
	}

	return true;
}

void ovrFramebuffer_Destroy(ovrFramebuffer* frameBuffer) {
	if (frameBuffer->UseVulkan) {
		for (int i = 0; i < frameBuffer->TextureSwapChainLength; i++) {
			vkDestroyFramebuffer(frameBuffer->VKContext->device, frameBuffer->VKFrameBuffers[i], nullptr);
		}
		free(frameBuffer->VKFrameBuffers);
	} else {
		GL(glDeleteFramebuffers(frameBuffer->TextureSwapChainLength, frameBuffer->GLFrameBuffers));
		free(frameBuffer->GLFrameBuffers);
	}
	OXR(xrDestroySwapchain(frameBuffer->ColorSwapChain.Handle));
	OXR(xrDestroySwapchain(frameBuffer->DepthSwapChain.Handle));
	free(frameBuffer->ColorSwapChainImage);
	free(frameBuffer->DepthSwapChainImage);

	ovrFramebuffer_Clear(frameBuffer);
}

void* ovrFramebuffer_SetCurrent(ovrFramebuffer* frameBuffer) {
	if (frameBuffer->UseVulkan) {
		return frameBuffer->VKFrameBuffers[frameBuffer->TextureSwapChainIndex];
	} else {
		GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->GLFrameBuffers[frameBuffer->TextureSwapChainIndex]));
		return nullptr;
	}
}

void ovrFramebuffer_Acquire(ovrFramebuffer* frameBuffer) {
	XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, NULL};
	OXR(xrAcquireSwapchainImage(frameBuffer->ColorSwapChain.Handle, &acquireInfo, &frameBuffer->TextureSwapChainIndex));

	XrSwapchainImageWaitInfo waitInfo;
	waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
	waitInfo.next = NULL;
	waitInfo.timeout = 1000000; /* timeout in nanoseconds */
	XrResult res = xrWaitSwapchainImage(frameBuffer->ColorSwapChain.Handle, &waitInfo);
	int i = 0;
	while ((res != XR_SUCCESS) && (i < 10)) {
		res = xrWaitSwapchainImage(frameBuffer->ColorSwapChain.Handle, &waitInfo);
		i++;
		ALOGV(
				" Retry xrWaitSwapchainImage %d times due to XR_TIMEOUT_EXPIRED (duration %f micro seconds)",
				i,
				waitInfo.timeout * (1E-9));
	}
	frameBuffer->Acquired = res == XR_SUCCESS;

	ovrFramebuffer_SetCurrent(frameBuffer);

	if (frameBuffer->UseVulkan) {
		//TODO:implement
	} else {
		GL(glEnable( GL_SCISSOR_TEST ));
		GL(glViewport( 0, 0, frameBuffer->Width, frameBuffer->Height ));
		GL(glClearColor( 0.0f, 0.0f, 0.0f, 1.0f ));
		GL(glScissor( 0, 0, frameBuffer->Width, frameBuffer->Height ));
		GL(glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT ));
		GL(glScissor( 0, 0, 0, 0 ));
		GL(glDisable( GL_SCISSOR_TEST ));
	}
}

void ovrFramebuffer_Release(ovrFramebuffer* frameBuffer) {
	if (frameBuffer->Acquired) {
		XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, NULL};
		OXR(xrReleaseSwapchainImage(frameBuffer->ColorSwapChain.Handle, &releaseInfo));
		frameBuffer->Acquired = false;

		// Clear the alpha channel, other way OpenXR would not transfer the framebuffer fully
		if (frameBuffer->UseVulkan) {
			//TODO:implement
		} else {
			GL(glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE));
			GL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
			GL(glClear(GL_COLOR_BUFFER_BIT));
			GL(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
		}
	}
}

/*
================================================================================

ovrRenderer

================================================================================
*/

void ovrRenderer_Clear(ovrRenderer* renderer) {
	for (int i = 0; i < ovrMaxNumEyes; i++) {
		ovrFramebuffer_Clear(&renderer->FrameBuffer[i]);
	}
}

void ovrRenderer_Create(XrSession session, ovrRenderer* renderer, int width, int height, bool multiview, void* vulkanContext) {
	renderer->Multiview = multiview;
	int instances = renderer->Multiview ? 1 : ovrMaxNumEyes;
	for (int i = 0; i < instances; i++) {
		if (vulkanContext) {
			ovrFramebuffer_CreateVK(session, &renderer->FrameBuffer[i], width, height, multiview, vulkanContext);
		} else {
			ovrFramebuffer_CreateGL(session, &renderer->FrameBuffer[i], width, height, multiview);
		}
	}
}

void ovrRenderer_Destroy(ovrRenderer* renderer) {
	int instances = renderer->Multiview ? 1 : ovrMaxNumEyes;
	for (int i = 0; i < instances; i++) {
		ovrFramebuffer_Destroy(&renderer->FrameBuffer[i]);
	}
}

void ovrRenderer_MouseCursor(ovrRenderer* renderer, int x, int y, int size) {
	if (renderer->FrameBuffer[0].UseVulkan) {
		//TODO:implement
	} else {
		GL(glEnable(GL_SCISSOR_TEST));
		GL(glScissor(x, y, size, size));
		GL(glViewport(x, y, size, size));
		GL(glClearColor(1.0f, 1.0f, 1.0f, 1.0f));
		GL(glClear(GL_COLOR_BUFFER_BIT));
		GL(glDisable(GL_SCISSOR_TEST));
	}
}

/*
================================================================================

ovrApp

================================================================================
*/

void ovrApp_Clear(ovrApp* app) {
	app->Focused = false;
	app->Instance = XR_NULL_HANDLE;
	app->Session = XR_NULL_HANDLE;
	memset(&app->ViewportConfig, 0, sizeof(XrViewConfigurationProperties));
	memset(&app->ViewConfigurationView, 0, ovrMaxNumEyes * sizeof(XrViewConfigurationView));
	app->SystemId = XR_NULL_SYSTEM_ID;
	app->HeadSpace = XR_NULL_HANDLE;
	app->StageSpace = XR_NULL_HANDLE;
	app->FakeStageSpace = XR_NULL_HANDLE;
	app->CurrentSpace = XR_NULL_HANDLE;
	app->SessionActive = false;
	app->SwapInterval = 1;
	memset(app->Layers, 0, sizeof(ovrCompositorLayer_Union) * ovrMaxLayerCount);
	app->LayerCount = 0;
	app->MainThreadTid = 0;
	app->RenderThreadTid = 0;

	ovrRenderer_Clear(&app->Renderer);
}

void ovrApp_Destroy(ovrApp* app) {
	ovrApp_Clear(app);
}

void ovrApp_HandleSessionStateChanges(ovrApp* app, XrSessionState state) {
	if (state == XR_SESSION_STATE_READY) {
		assert(app->SessionActive == false);

		XrSessionBeginInfo sessionBeginInfo;
		memset(&sessionBeginInfo, 0, sizeof(sessionBeginInfo));
		sessionBeginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
		sessionBeginInfo.next = NULL;
		sessionBeginInfo.primaryViewConfigurationType = app->ViewportConfig.viewConfigurationType;

		XrResult result;
		OXR(result = xrBeginSession(app->Session, &sessionBeginInfo));

		app->SessionActive = (result == XR_SUCCESS);

		// Set session state once we have entered VR mode and have a valid session object.
#ifdef OPENXR_HAS_PERFORMANCE_EXTENSION
		if (app->SessionActive) {
			XrPerfSettingsLevelEXT cpuPerfLevel = XR_PERF_SETTINGS_LEVEL_BOOST_EXT;
			XrPerfSettingsLevelEXT gpuPerfLevel = XR_PERF_SETTINGS_LEVEL_BOOST_EXT;

			PFN_xrPerfSettingsSetPerformanceLevelEXT pfnPerfSettingsSetPerformanceLevelEXT = NULL;
			OXR(xrGetInstanceProcAddr(
					app->Instance,
					"xrPerfSettingsSetPerformanceLevelEXT",
					(PFN_xrVoidFunction*)(&pfnPerfSettingsSetPerformanceLevelEXT)));

			OXR(pfnPerfSettingsSetPerformanceLevelEXT(app->Session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, cpuPerfLevel));
			OXR(pfnPerfSettingsSetPerformanceLevelEXT(app->Session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, gpuPerfLevel));

			PFN_xrSetAndroidApplicationThreadKHR pfnSetAndroidApplicationThreadKHR = NULL;
			OXR(xrGetInstanceProcAddr(
					app->Instance,
					"xrSetAndroidApplicationThreadKHR",
					(PFN_xrVoidFunction*)(&pfnSetAndroidApplicationThreadKHR)));

			OXR(pfnSetAndroidApplicationThreadKHR(app->Session, XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, app->MainThreadTid));
			OXR(pfnSetAndroidApplicationThreadKHR(app->Session, XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR, app->RenderThreadTid));
		}
#endif
	} else if (state == XR_SESSION_STATE_STOPPING) {
		assert(app->SessionActive);

		OXR(xrEndSession(app->Session));
		app->SessionActive = false;
	}
}

int ovrApp_HandleXrEvents(ovrApp* app) {
	XrEventDataBuffer eventDataBuffer = {};
	int recenter = 0;

	// Poll for events
	for (;;) {
		XrEventDataBaseHeader* baseEventHeader = (XrEventDataBaseHeader*)(&eventDataBuffer);
		baseEventHeader->type = XR_TYPE_EVENT_DATA_BUFFER;
		baseEventHeader->next = NULL;
		XrResult r;
		OXR(r = xrPollEvent(app->Instance, &eventDataBuffer));
		if (r != XR_SUCCESS) {
			break;
		}

		switch (baseEventHeader->type) {
			case XR_TYPE_EVENT_DATA_EVENTS_LOST:
				ALOGV("xrPollEvent: received XR_TYPE_EVENT_DATA_EVENTS_LOST event");
				break;
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				const XrEventDataInstanceLossPending* instance_loss_pending_event =
						(XrEventDataInstanceLossPending*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING event: time %f",
						FromXrTime(instance_loss_pending_event->lossTime));
			} break;
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
				ALOGV("xrPollEvent: received XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED event");
				break;
			case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
				const XrEventDataPerfSettingsEXT* perf_settings_event =
						(XrEventDataPerfSettingsEXT*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT event: type %d subdomain %d : level %d -> level %d",
						perf_settings_event->type,
						perf_settings_event->subDomain,
						perf_settings_event->fromLevel,
						perf_settings_event->toLevel);
			} break;
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				XrEventDataReferenceSpaceChangePending* ref_space_change_event =
						(XrEventDataReferenceSpaceChangePending*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING event: changed space: %d for session %p at time %f",
						ref_space_change_event->referenceSpaceType,
						(void*)ref_space_change_event->session,
						FromXrTime(ref_space_change_event->changeTime));
				recenter = 1;
			} break;
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				const XrEventDataSessionStateChanged* session_state_changed_event =
						(XrEventDataSessionStateChanged*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: %d for session %p at time %f",
						session_state_changed_event->state,
						(void*)session_state_changed_event->session,
						FromXrTime(session_state_changed_event->time));

				switch (session_state_changed_event->state) {
					case XR_SESSION_STATE_FOCUSED:
						app->Focused = true;
						break;
					case XR_SESSION_STATE_VISIBLE:
						app->Focused = false;
						break;
					case XR_SESSION_STATE_READY:
					case XR_SESSION_STATE_STOPPING:
						ovrApp_HandleSessionStateChanges(app, session_state_changed_event->state);
						break;
					default:
						break;
				}
			} break;
			default:
				ALOGV("xrPollEvent: Unknown event");
				break;
		}
	}
	return recenter;
}
