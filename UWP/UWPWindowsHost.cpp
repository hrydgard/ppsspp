#include "pch.h"

#include "UWPWindowsHost.h"
#include "Windows/XinputDevice.h"
#include "Windows/KeyboardDevice.h"
#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/System.h"
#include "GPU\WindowsEGLContext.h"
#include "XAudioSoundStream.h"
#include "input/input_state.h"

static const int numCPUs = 1;

float mouseDeltaX = 0;
float mouseDeltaY = 0;

UWPWindowsHost::UWPWindowsHost( Windows::UI::Xaml::Controls::SwapChainPanel^ window )
  : window( window )
{
  resizeLock = CreateEvent( nullptr, true, true, L"ResizeLock" );

	mouseDeltaX = 0;
	mouseDeltaY = 0;

	//add first XInput device to respond
#ifndef _M_ARM
	input.push_back(std::shared_ptr<InputDevice>(new XinputDevice()));
#endif
}

UWPWindowsHost::~UWPWindowsHost()
{
  CloseHandle( resizeLock );
}

bool UWPWindowsHost::InitGraphics(std::string *error_message) {
	switch (g_Config.iGPUBackend) {
	case GPU_BACKEND_OPENGL:
		return GL_Init(window, error_message);
	default:
		return false;
	}
}

void UWPWindowsHost::ShutdownGraphics() {
	switch (g_Config.iGPUBackend) {
	case GPU_BACKEND_OPENGL:
		GL_Shutdown();
		break;
	}
}

void UWPWindowsHost::ResizeGraphics()
{
  switch ( g_Config.iGPUBackend ) {
  case GPU_BACKEND_OPENGL:
    GL_Resize( window );
    SetEvent( resizeLock );
    break;
  default:
    break;
  }
}

void UWPWindowsHost::SetWindowTitle(const char *message) {
}

void UWPWindowsHost::InitSound() {
}

// UGLY!
extern WindowsAudioBackend *winAudioBackend;

void UWPWindowsHost::UpdateSound() {
	if (winAudioBackend)
		winAudioBackend->Update();
}

void UWPWindowsHost::ShutdownSound() {
}

void UWPWindowsHost::UpdateUI() {
}

void UWPWindowsHost::UpdateMemView() {
}

void UWPWindowsHost::UpdateDisassembly() {
}

void UWPWindowsHost::SetDebugMode(bool mode) {
}

void UWPWindowsHost::PollControllers(InputState &input_state) {
	bool doPad = true;
	for (auto iter = this->input.begin(); iter != this->input.end(); iter++)
	{
		auto device = *iter;
		if (!doPad && device->IsPad())
			continue;
		if (device->UpdateState(input_state) == InputDevice::UPDATESTATE_SKIP_PAD)
			doPad = false;
	}

	mouseDeltaX *= 0.9f;
	mouseDeltaY *= 0.9f;

	// TODO: Tweak!
	float mx = std::max(-1.0f, std::min(1.0f, mouseDeltaX * 0.01f));
	float my = std::max(-1.0f, std::min(1.0f, mouseDeltaY * 0.01f));
	AxisInput axisX, axisY;
	axisX.axisId = JOYSTICK_AXIS_MOUSE_REL_X;
	axisX.deviceId = DEVICE_ID_MOUSE;
	axisX.value = mx;
	axisY.axisId = JOYSTICK_AXIS_MOUSE_REL_Y;
	axisY.deviceId = DEVICE_ID_MOUSE;
	axisY.value = my;

	// Disabled for now as it makes the mapping dialog unusable!
	//if (fabsf(mx) > 0.1f) NativeAxis(axisX);
	//if (fabsf(my) > 0.1f) NativeAxis(axisY);
}

void UWPWindowsHost::BootDone() {
}

static std::string SymbolMapFilename(const char *currentFilename, char* ext) {
  return std::string();
}

bool UWPWindowsHost::AttemptLoadSymbolMap() {
	return false;
}

void UWPWindowsHost::SaveSymbolMap() {
}

bool UWPWindowsHost::IsDebuggingEnabled() {
	return false;
}

bool UWPWindowsHost::CanCreateShortcut() {
	return false;  // Turn on when below function fixed
}

bool UWPWindowsHost::CreateDesktopShortcut(std::string argumentPath, std::string gameTitle) {
	return false;
}

void UWPWindowsHost::GoFullscreen(bool viewFullscreen) {
}

void UWPWindowsHost::ToggleDebugConsoleVisibility() {
}

bool UWPWindowsHost::GPUDebuggingActive() {
  return false;
}

void UWPWindowsHost::GPUNotifyCommand( u32 pc ) {
}

void UWPWindowsHost::GPUNotifyDisplay( u32 framebuf, u32 stride, int format ) {
}

void UWPWindowsHost::GPUNotifyDraw() {
}

void UWPWindowsHost::GPUNotifyTextureAttachment( u32 addr ) {
}
