#pragma once

#include "thin3d/thin3d.h"

#include "Common/GraphicsContext.h"
#include "Common/DeviceResources.h"

// Renders Direct2D and 3D content on the screen.
namespace UWP {
	
class UWPGraphicsContext : public GraphicsContext {
public:
	UWPGraphicsContext(std::shared_ptr<DX::DeviceResources> resources);

	void Shutdown() override;
	void SwapInterval(int interval) override;
	void SwapBuffers() override {}
	void Resize() override {}
	Draw::DrawContext * GetDrawContext() override {
		return draw_;
	}

private:
	Draw::DrawContext *draw_;
	std::shared_ptr<DX::DeviceResources> resources_;
};

class PPSSPP_UWPMain : public DX::IDeviceNotify
{
public:
	PPSSPP_UWPMain(const std::shared_ptr<DX::DeviceResources>& deviceResources);
	~PPSSPP_UWPMain();
	void CreateWindowSizeDependentResources();
	bool Render();

	// IDeviceNotify
	virtual void OnDeviceLost();
	virtual void OnDeviceRestored();

	// Various forwards from App, in simplified format.
	// Not sure whether this abstraction is worth it.
	void OnKeyDown(int scanCode, Windows::System::VirtualKey virtualKey, int repeatCount);
	void OnKeyUp(int scanCode, Windows::System::VirtualKey virtualKey);

private:
	// Cached pointer to device resources.
	std::shared_ptr<DX::DeviceResources> m_deviceResources;

	std::unique_ptr<UWPGraphicsContext> ctx_;
};

}