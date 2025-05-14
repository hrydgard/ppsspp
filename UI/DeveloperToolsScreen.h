#pragma once

#include "ppsspp_config.h"

#include "UI/TabbedDialogScreen.h"

class DeveloperToolsScreen : public TabbedUIDialogScreenWithGameBackground {
public:
	DeveloperToolsScreen(const Path &gamePath) : TabbedUIDialogScreenWithGameBackground(gamePath) {}

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

	UI::EventReturn OnRunCPUTests(UI::EventParams &e);
	UI::EventReturn OnLoggingChanged(UI::EventParams &e);
	UI::EventReturn OnOpenTexturesIniFile(UI::EventParams &e);
	UI::EventReturn OnLogConfig(UI::EventParams &e);
	UI::EventReturn OnJitAffectingSetting(UI::EventParams &e);
	UI::EventReturn OnJitDebugTools(UI::EventParams &e);
	UI::EventReturn OnRemoteDebugger(UI::EventParams &e);
	UI::EventReturn OnMIPSTracerEnabled(UI::EventParams &e);
	UI::EventReturn OnMIPSTracerPathChanged(UI::EventParams &e);
	UI::EventReturn OnMIPSTracerFlushTrace(UI::EventParams &e);
	UI::EventReturn OnMIPSTracerClearJitCache(UI::EventParams &e);
	UI::EventReturn OnMIPSTracerClearTracer(UI::EventParams &e);
	UI::EventReturn OnGPUDriverTest(UI::EventParams &e);
	UI::EventReturn OnMemstickTest(UI::EventParams &e);
	UI::EventReturn OnTouchscreenTest(UI::EventParams &e);
	UI::EventReturn OnCopyStatesToRoot(UI::EventParams &e);

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
};
