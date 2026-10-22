#pragma once

#include "ppsspp_config.h"

#include "UI/TabbedDialogScreen.h"

class DeveloperToolsScreen : public UITabbedBaseDialogScreen {
public:
	DeveloperToolsScreen(const Path &gamePath);

	void CreateTabs() override;
	void update() override;
	void onFinish(DialogResult result) override;

	const char *tag() const override { return "DeveloperTools"; }

private:
	void PreCreateViews() override;

	void BuildTextureReplacementPage(SettingsPage &page);
	void BuildGeneralPage(SettingsPage &page);
	void BuildDumpFilePage(SettingsPage &page);
	void BuildHLEPage(SettingsPage &page);
	void BuildMIPSTracerPage(SettingsPage &page);
	void BuildTestsPage(SettingsPage &page);
	void BuildAudioPage(SettingsPage &page);
	void BuildGraphicsPage(SettingsPage &page);
	void BuildNetworkPage(SettingsPage &page);
	void BuildUIPage(SettingsPage &page);
	void CreateCrashHistoryTab(UI::LinearLayout *list);

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

	int testSliderValue_ = 0;
	bool pretendIngame_ = false;
};
