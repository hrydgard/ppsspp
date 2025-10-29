#pragma once

#include "ppsspp_config.h"

#include "UI/TabbedDialogScreen.h"

class DeveloperToolsScreen : public UITabbedBaseDialogScreen {
public:
	DeveloperToolsScreen(const Path &gamePath) : UITabbedBaseDialogScreen(gamePath) {}

	void CreateTabs() override;
	void update() override;
	void onFinish(DialogResult result) override;

	const char *tag() const override { return "DeveloperTools"; }

private:
	void CreateTextureReplacementTab(UI::LinearLayout *parent);
	void CreateGeneralTab(UI::LinearLayout *parent);
	void CreateDumpFileTab(UI::LinearLayout *parent);
	void CreateHLETab(UI::LinearLayout *parent);
	void CreateMIPSTracerTab(UI::LinearLayout *list);
	void CreateTestsTab(UI::LinearLayout *list);
	void CreateAudioTab(UI::LinearLayout *list);
	void CreateGraphicsTab(UI::LinearLayout *list);
	void CreateNetworkTab(UI::LinearLayout *list);
	void CreateUITab(UI::LinearLayout *list);

	void OnLoggingChanged(UI::EventParams &e);
	void OnOpenTexturesIniFile(UI::EventParams &e);
	void OnJitAffectingSetting(UI::EventParams &e);
	void OnJitDebugTools(UI::EventParams &e);
	void OnRemoteDebugger(UI::EventParams &e);
	void OnMIPSTracerEnabled(UI::EventParams &e);
	void OnMIPSTracerPathChanged(UI::EventParams &e);
	void OnMIPSTracerFlushTrace(UI::EventParams &e);
	void OnMIPSTracerClearJitCache(UI::EventParams &e);
	void OnMIPSTracerClearTracer(UI::EventParams &e);
	void OnGPUDriverTest(UI::EventParams &e);
	void OnMemstickTest(UI::EventParams &e);
	void OnTouchscreenTest(UI::EventParams &e);
	void OnCopyStatesToRoot(UI::EventParams &e);

	void MemoryMapTest();

	bool allowDebugger_ = false;
	bool canAllowDebugger_ = true;
	enum class HasIni {
		NO,
		YES,
		MAYBE,
	};
	HasIni hasTexturesIni_ = HasIni::MAYBE;

	bool MIPSTracerEnabled_ = false;
	std::string MIPSTracerPath_ = "";
	UI::InfoItem* MIPSTracerPath = nullptr;

	int testSliderValue_ = 0;
	bool pretendIngame_ = false;
};
