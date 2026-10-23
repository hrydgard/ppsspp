
#include <cmath>
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#include "libretro/LibretroGLContext.h"
#include "libretro/LibretroVR.h"
#include "Common/Math/fast/fast_matrix.h"
#include "Common/VR/PPSSPPVR.h"
#include "GPU/GPU.h"
#include "GPU/GPUCommon.h"
#include <vector>
#include "Common/VR/VRMath.h"

extern GLuint g_defaultFBO;

namespace Libretro {
static VRMode vrMode = VRMode::Off;
static bool vrActive = false;
static bool vrImmersive = false;
static bool vrReferenceValid = false;
static bool vrReplaying = false;
static bool vrCylinder = false;
static float vrCanvasAspect = 2.0f;
static GLuint vrCanvasProgram = 0, vrCanvasBuffer = 0, vrCanvasVAO = 0;
static unsigned vrGeneration = 0;
static int vrEye = 0;
static unsigned vrWidth = 0, vrHeight = 0;
static GLuint vrFramebuffer[2]{}, vrColor[2]{}, vrDepth[2]{};
static GLuint vrFrontendFramebuffer = 0;
static retro_vr_frame_state vrFrame{};
static XrPosef vrReference = XrPosef_Identity();

static void DeleteVRTargets() {
	glDeleteFramebuffers(2, vrFramebuffer);
	glDeleteTextures(2, vrColor);
	glDeleteRenderbuffers(2, vrDepth);
	memset(vrFramebuffer, 0, sizeof(vrFramebuffer));
	memset(vrColor, 0, sizeof(vrColor));
	memset(vrDepth, 0, sizeof(vrDepth));
	vrWidth = vrHeight = 0;
}

void DestroyVR() {
	if (vrFramebuffer[0]) {
		DeleteVRTargets();
	}
	vrActive = vrImmersive = vrReferenceValid = vrReplaying = false;
	vrCylinder = false;
	if (vrCanvasProgram) {
		glDeleteProgram(vrCanvasProgram);
		glDeleteBuffers(1, &vrCanvasBuffer);
		glDeleteVertexArrays(1, &vrCanvasVAO);
	}
	vrCanvasProgram = vrCanvasBuffer = vrCanvasVAO = 0;
}

void SetVRMode(VRMode mode) {
	if (vrMode != mode) {
		vrReferenceValid = false;
	}
	vrMode = mode;
}

bool VRActive() {
	return vrActive;
}

unsigned VRGeneration() {
	return vrGeneration;
}

static bool CreateVRTargets(unsigned width, unsigned height) {
	if (width == vrWidth && height == vrHeight) {
		return true;
	}
	DeleteVRTargets();
	GLint maxSize = 0;
	glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxSize);
	if (!width || !height || width > (unsigned)maxSize || height > (unsigned)maxSize) {
		return false;
	}
	GLint drawFramebuffer = 0, readFramebuffer = 0, renderbuffer = 0, texture = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
	glGenFramebuffers(2, vrFramebuffer);
	glGenTextures(2, vrColor);
	glGenRenderbuffers(2, vrDepth);
	bool complete = true;
	for (int eye = 0; eye < 2; ++eye) {
		glBindFramebuffer(GL_FRAMEBUFFER, vrFramebuffer[eye]);
		glBindTexture(GL_TEXTURE_2D, vrColor[eye]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, vrColor[eye], 0);
		glBindRenderbuffer(GL_RENDERBUFFER, vrDepth[eye]);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, vrDepth[eye]);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, vrDepth[eye]);
		complete &= glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	}
	glBindTexture(GL_TEXTURE_2D, texture);
	glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
	if (!complete) {
		DeleteVRTargets();
		return false;
	}
	vrWidth = width;
	vrHeight = height;
	return true;
}

static bool ValidEye(const retro_vr_eye_state &eye) {
	float length = 0.0f;
	for (float component : eye.orientation) {
		if (!std::isfinite(component)) {
			return false;
		}
		length += component * component;
	}
	if (fabsf(length - 1.0f) > 0.01f) {
		return false;
	}
	for (float component : eye.position) {
		if (!std::isfinite(component)) {
			return false;
		}
	}
	for (float tangent : eye.fov_tan) {
		if (!std::isfinite(tangent) || tangent <= 0.0f) {
			return false;
		}
	}
	return true;
}

static bool PublishVRViews(bool tracked) {
	retro_video_view eyes[2]{};
	for (int eye = 0; eye < 2; ++eye) {
		eyes[eye] = {eye * vrWidth, 0, vrWidth, vrHeight, 0,
			(unsigned)(eye == 0 ? RETRO_VIDEO_VIEW_EYE_LEFT : RETRO_VIDEO_VIEW_EYE_RIGHT),
			vrHeight ? (float)vrWidth / vrHeight : 0.0f};
	}
	retro_video_views views{};
	views.views = eyes;
	views.num_views = vrActive ? 2 : 0;
	views.flags = tracked ? RETRO_VIDEO_VIEWS_FLAG_TRACKED : 0;
	views.reference_space = RETRO_VR_REFERENCE_SPACE_LOCAL;
	bool accepted = environ_cb(RETRO_ENVIRONMENT_SET_VIDEO_VIEWS, &views);
	if (views.recommended_view_width && views.recommended_view_height) {
		vrCanvasAspect = 2.0f * views.recommended_view_width / views.recommended_view_height;
	}
	return accepted;
}

void UpdateVR() {
	const bool wasActive = vrActive;
	vrActive = vrImmersive = false;
	vrCylinder = false;
	unsigned status = 0;
	const bool gl = ctx && ctx->GetGPUCore() == GPUCORE_GLES && !g_Config.bSoftwareRendering;
	if (gl && vrMode != VRMode::Off &&
		(gl_extensions.GLES3 || (!gl_extensions.IsGLES && gl_extensions.ARB_framebuffer_object)) &&
		environ_cb(RETRO_ENVIRONMENT_GET_VIDEO_VIEWS_STATUS, &status) &&
		(status & RETRO_VIDEO_VIEWS_STATUS_STEREO) && (status & RETRO_VIDEO_VIEWS_STATUS_PRESENTS)) {
		vrActive = CreateVRTargets(PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		vrFrame = {};
		bool tracked = (status & RETRO_VIDEO_VIEWS_STATUS_HMD) &&
			environ_cb(RETRO_ENVIRONMENT_GET_VR_FRAME_STATE, &vrFrame) &&
			ValidEye(vrFrame.eyes[0]) && ValidEye(vrFrame.eyes[1]);
		vrImmersive = vrActive && tracked && vrMode == VRMode::Immersive;
		if (tracked && (!vrReferenceValid || (vrFrame.flags & RETRO_VR_FRAME_RECENTERED))) {
			const auto &left = vrFrame.eyes[0];
			const auto &right = vrFrame.eyes[1];
			vrReference.orientation = {left.orientation[0], left.orientation[1], left.orientation[2], left.orientation[3]};
			vrReference.position = {(left.position[0] + right.position[0]) * 0.5f,
				(left.position[1] + right.position[1]) * 0.5f, (left.position[2] + right.position[2]) * 0.5f};
			vrReferenceValid = true;
		}
		if (!tracked) {
			vrReferenceValid = false;
		}
	} else {
		vrReferenceValid = false;
	}

	g_Config.bEnableStereo = true;
	// IdentityViewHack uses standalone's projection-layer path, not its immersive cylinder.
	// Keep those games on a stereo screen until that presentation path is adapted too.
	g_Config.bEnableVR = vrImmersive && !PSP_CoreParameter().compat.vrCompat().IdentityViewHack;
	g_Config.bEnableImmersiveVR = g_Config.bEnableVR;
	SetVRAppMode(VR_GAME_MODE);
	if (vrActive) {
		StartVRRender();
		vrCylinder = vrImmersive && !IsFlatVRScene();
	}
	if (!PublishVRViews(vrCylinder)) {
		vrActive = vrImmersive = false;
		PublishVRViews(false);
	}
	if (!vrActive) {
		vrCylinder = false;
	}
	if (wasActive != vrActive) {
		if (gpu) {
			gpu->NotifyConfigChanged();
		}
		if (vrActive) {
			++vrGeneration;
		}
		unsigned width = PSP_CoreParameter().pixelWidth;
		unsigned height = PSP_CoreParameter().pixelHeight;
		retro_game_geometry geometry{width * (vrActive ? 2u : 1u), height,
			width * 2, height, (float)width * (vrActive ? 2.0f : 1.0f) / height};
		environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
		INFO_LOG(Log::G3D, "Libretro VR shared renderer: %s", vrActive ? "stereo views active (immersive cylinder available)" : "flat output");
	}
}

void BeginVREye(int eye) {
	if (eye == 0) {
		vrFrontendFramebuffer = g_defaultFBO;
	}
	vrEye = eye;
	vrReplaying = true;
	SetVRCompat(VR_COMPAT_FBO_CLEAR, false);
	g_defaultFBO = vrFramebuffer[eye];
}

void EndVREyes() {
	g_defaultFBO = vrFrontendFramebuffer;
	vrReplaying = false;
}

bool RetainVRCommands() {
	return vrReplaying && vrEye == 0;
}

bool VRRequested() {
	return vrMode != VRMode::Off;
}

int VREye() {
	return vrEye;
}

void GetVRView(int eye, XrView *view) {
	*view = {};
	view->type = XR_TYPE_VIEW;
	view->pose = XrPosef_Identity();
	const auto &source = vrFrame.eyes[eye];
	view->pose.position.x = eye == 0 ? -0.032f : 0.032f;
	if (vrReferenceValid) {
		view->pose.position = {source.position[0], source.position[1], source.position[2]};
		view->pose.orientation = {source.orientation[0], source.orientation[1], source.orientation[2], source.orientation[3]};
	}
	view->fov = {-atanf(source.fov_tan[0] > 0 ? source.fov_tan[0] : 1.0f),
		atanf(source.fov_tan[1] > 0 ? source.fov_tan[1] : 1.0f),
		atanf(source.fov_tan[2] > 0 ? source.fov_tan[2] : 1.0f),
		-atanf(source.fov_tan[3] > 0 ? source.fov_tan[3] : 1.0f)};
}

// RetroArch owns OpenXR. Render the standalone immersive cylinder into its tracked eyes.
static bool CreateVRCanvas() {
	if (vrCanvasProgram) {
		return true;
	}
	const char *version = gl_extensions.IsGLES ? "#version 300 es\nprecision highp float;\n" : "#version 130\n";
	const char *vertex = "in vec3 position; in vec2 texcoord; uniform mat4 mvp; out vec2 uv;\n"
		"void main() { uv = texcoord; gl_Position = mvp * vec4(position, 1.0); }\n";
	const char *fragment = "uniform sampler2D source; in vec2 uv; out vec4 color;\n"
		"void main() { color = texture(source, uv); }\n";
	GLuint shaders[2] = {glCreateShader(GL_VERTEX_SHADER), glCreateShader(GL_FRAGMENT_SHADER)};
	bool ok = true;
	for (int i = 0; i < 2; ++i) {
		const char *sources[] = {version, i == 0 ? vertex : fragment};
		glShaderSource(shaders[i], 2, sources, nullptr);
		glCompileShader(shaders[i]);
		GLint compiled = 0;
		glGetShaderiv(shaders[i], GL_COMPILE_STATUS, &compiled);
		if (!compiled) {
			char log[2048];
			glGetShaderInfoLog(shaders[i], sizeof(log), nullptr, log);
			ERROR_LOG(Log::G3D, "Libretro VR canvas shader: %s", log);
			ok = false;
		}
	}
	GLuint program = glCreateProgram();
	for (auto shader : shaders) {
		glAttachShader(program, shader);
	}
	glBindAttribLocation(program, 0, "position");
	glBindAttribLocation(program, 1, "texcoord");
	glLinkProgram(program);
	GLint linked = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	for (auto shader : shaders) {
		glDeleteShader(shader);
	}
	if (!ok || !linked) {
		glDeleteProgram(program);
		return false;
	}
	vrCanvasProgram = program;
	glGenBuffers(1, &vrCanvasBuffer);
	glGenVertexArrays(1, &vrCanvasVAO);
	return true;
}

static void DrawVRCanvas() {
	std::vector<float> vertices;
	constexpr int segments = 64;
	constexpr float pi = 3.14159265358979323846f;
	const float height = pi / vrCanvasAspect;
	const float distance = g_Config.fCanvas3DDistance / 4.0f - 1.0f;
	for (int i = 0; i < segments; ++i) {
		const int corners[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
		for (const auto &corner : corners) {
			float u = (float)(i + corner[0]) / segments;
			float angle = (u - 0.5f) * pi;
			vertices.insert(vertices.end(), {2.0f * sinf(angle), (corner[1] * 2 - 1) * height,
				-2.0f * cosf(angle) - distance, u, (float)corner[1]});
		}
	}
	glUseProgram(vrCanvasProgram);
	glBindVertexArray(vrCanvasVAO);
	glBindBuffer(GL_ARRAY_BUFFER, vrCanvasBuffer);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STREAM_DRAW);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 3, GL_FLOAT, false, 5 * sizeof(float), nullptr);
	glVertexAttribPointer(1, 2, GL_FLOAT, false, 5 * sizeof(float), (void *)(3 * sizeof(float)));
	glUniform1i(glGetUniformLocation(vrCanvasProgram, "source"), 0);
	for (int eye = 0; eye < 2; ++eye) {
		const auto &pose = vrFrame.eyes[eye];
		XrQuaternionf referenceInverse = XrQuaternionf_Inverse(vrReference.orientation);
		XrPosef camera = XrPosef_Identity();
		camera.orientation = XrQuaternionf_Multiply(referenceInverse,
			{pose.orientation[0], pose.orientation[1], pose.orientation[2], pose.orientation[3]});
		// Match standalone's default: the cylinder follows translation, never rotation.
		// Keep the eye separation so HUD elements lie on the cylinder, not at infinity.
		XrVector3f origin = vrReference.position;
		if (!g_Config.bEnable6DoF) {
			origin = {(vrFrame.eyes[0].position[0] + vrFrame.eyes[1].position[0]) * 0.5f,
				(vrFrame.eyes[0].position[1] + vrFrame.eyes[1].position[1]) * 0.5f,
				(vrFrame.eyes[0].position[2] + vrFrame.eyes[1].position[2]) * 0.5f};
		}
		camera.position = XrQuaternionf_Rotate(referenceInverse, {
			pose.position[0] - origin.x, pose.position[1] - origin.y, pose.position[2] - origin.z});
		XrPosef view = XrPosef_Inverse(camera);
		float rowMajor[16], matrix[16];
		XrQuaternionf_ToMatrix4f(&view.orientation, rowMajor);
		for (int col = 0; col < 4; ++col) {
			for (int row = 0; row < 4; ++row) {
				matrix[col * 4 + row] = rowMajor[row * 4 + col];
			}
		}
		matrix[12] = view.position.x;
		matrix[13] = view.position.y;
		matrix[14] = view.position.z;
		const float *fov = pose.fov_tan;
		float lens[16]{};
		lens[0] = 2.0f / (fov[0] + fov[1]);
		lens[5] = 2.0f / (fov[2] + fov[3]);
		lens[8] = (fov[1] - fov[0]) / (fov[0] + fov[1]);
		lens[9] = (fov[2] - fov[3]) / (fov[2] + fov[3]);
		lens[10] = -1.0f;
		lens[11] = -1.0f;
		lens[14] = -0.02f;
		float mvp[16];
		fast_matrix_mul_4x4(mvp, lens, matrix);
		glUniformMatrix4fv(glGetUniformLocation(vrCanvasProgram, "mvp"), 1, false, mvp);
		glViewport(eye * vrWidth, 0, vrWidth, vrHeight);
		glBindTexture(GL_TEXTURE_2D, vrColor[eye]);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size() / 5);
	}
}

void PresentVR(unsigned framebuffer) {
	if (!vrActive) {
		return;
	}
	bool cylinder = vrCylinder && CreateVRCanvas();
	PublishVRViews(cylinder);
	GLint program, vao, buffer, active, texture, sampler, viewport[4];
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
	const bool samplers = gl_extensions.GLES3 || gl_extensions.VersionGEThan(3, 3);
	if (samplers) {
		glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
	}
	glGetIntegerv(GL_VIEWPORT, viewport);
	const GLenum states[] = {GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_BLEND, GL_CULL_FACE};
	GLboolean enabled[5], colorMask[4];
	glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
	for (int i = 0; i < 5; ++i) {
		enabled[i] = glIsEnabled(states[i]);
		glDisable(states[i]);
	}
	glColorMask(true, true, true, true);
	if (samplers) {
		glBindSampler(0, 0);
	}
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
	if (cylinder) {
		GLfloat clearColor[4];
		glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor);
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
		DrawVRCanvas();
	} else {
		for (int eye = 0; eye < 2; ++eye) {
			glBindFramebuffer(GL_READ_FRAMEBUFFER, vrFramebuffer[eye]);
			glBlitFramebuffer(0, 0, vrWidth, vrHeight, eye * vrWidth, 0, (eye + 1) * vrWidth, vrHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glBindTexture(GL_TEXTURE_2D, texture);
	if (samplers) {
		glBindSampler(0, sampler);
	}
	glActiveTexture(active);
	glUseProgram(program);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
	glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
	for (int i = 0; i < 5; ++i) {
		if (enabled[i]) {
			glEnable(states[i]);
		}
	}
}

}

bool LibretroGLContext::InitAPI(void *wnd, std::string *deviceName, std::string *error_message) {
	if (!LibretroHWRenderContext::InitHW(false))
		return false;

	g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
	return true;
}

void LibretroGLContext::CreateDrawContext() {

#ifndef USING_GLES2
    // Some core profile drivers elide certain extensions from GL_EXTENSIONS/etc.
    // glewExperimental allows us to force GLEW to search for the pointers anyway.
    if (gl_extensions.IsCoreContext)
        glewExperimental = true;
    if (GLEW_OK != glewInit()) {
        printf("Failed to initialize glew!\n");
    }
    // Unfortunately, glew will generate an invalid enum error, ignore.
    if (gl_extensions.IsCoreContext)
        glGetError();
#endif

    CheckGLExtensions();
    draw_ = Draw::T3DCreateGLContext(false);
    renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
    renderManager_->SetInflightFrames(g_Config.iInflightFrames);
    SetGPUBackend(GPUBackend::OPENGL);
}

void LibretroGLContext::DestroyDrawContext() {
	Libretro::DestroyVR();
	LibretroHWRenderContext::DestroyDrawContext();
	renderManager_ = nullptr;
}

void LibretroGLContext::NotifyEmuThreadExit() {
   renderManager_->NotifyEmuThreadExit();
}
