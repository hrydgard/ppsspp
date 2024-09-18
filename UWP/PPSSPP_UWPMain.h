#pragma once

#include <mutex>

#include "Common/GPU/thin3d.h"
#include "Common/Input/InputState.h"

#include "Common/GraphicsContext.h"
#include "Common/DeviceResources.h"

// Renders Direct2D and 3D content on the screen.
namespace UWP {

ref class App;
enum class HardwareButton;

class UWPGraphicsContext : public GraphicsContext {
public:
	UWPGraphicsContext(std::shared_ptr<DX::DeviceResources> resources);

	void Shutdown() override;
	void Resize() override {}
	Draw::DrawContext * GetDrawContext() override {
		return draw_;
	}

private:
	Draw::DrawContext *draw_;
	std::shared_ptr<DX::DeviceResources> resources_;
};

class PPSSPP_UWPMain : public DX::IDeviceNotify {
public:
	PPSSPP_UWPMain(App ^app, const std::shared_ptr<DX::DeviceResources>& deviceResources);
	~PPSSPP_UWPMain();
	void CreateWindowSizeDependentResources();
	void UpdateScreenState();
	bool Render();

	// IDeviceNotify
	virtual void OnDeviceLost();
	virtual void OnDeviceRestored();

	// Various forwards from App, in simplified format.
	// Not sure whether this abstraction is worth it.
	void OnKeyDown(int scanCode, Windows::System::VirtualKey virtualKey, int repeatCount);
	void OnKeyUp(int scanCode, Windows::System::VirtualKey virtualKey);
	void OnCharacterReceived(int scanCode,unsigned int keyCode);

	void OnTouchEvent(int touchEvent, int touchId, float x, float y, double timestamp);

	void OnMouseWheel(float delta);

	bool OnHardwareButton(HardwareButton button);

	void RotateXYToDisplay(float &x, float &y);

	// Save state fast if we can!
	void OnSuspend();
	void Close();

private:
	App ^app_;

	// Cached pointer to device resources.
	std::shared_ptr<DX::DeviceResources> m_deviceResources;

	std::unique_ptr<UWPGraphicsContext> ctx_;
};

}
