#pragma once
#include "Common/UI/UIScreen.h"
#include "UI/MiscScreens.h"

class JitCompareScreen : public UIDialogScreenWithBackground {
public:
	void CreateViews() override;

	const char *tag() const override { return "JitCompare"; }

private:
	void UpdateDisasm();
	UI::EventReturn OnRandomBlock(UI::EventParams &e);
	UI::EventReturn OnRandomFPUBlock(UI::EventParams &e);
	UI::EventReturn OnRandomVFPUBlock(UI::EventParams &e);
	void OnRandomBlock(int flag);

	UI::EventReturn OnCurrentBlock(UI::EventParams &e);
	UI::EventReturn OnSelectBlock(UI::EventParams &e);
	UI::EventReturn OnPrevBlock(UI::EventParams &e);
	UI::EventReturn OnNextBlock(UI::EventParams &e);
	UI::EventReturn OnBlockAddress(UI::EventParams &e);
	UI::EventReturn OnAddressChange(UI::EventParams &e);
	UI::EventReturn OnShowStats(UI::EventParams &e);

	int currentBlock_ = -1;

	UI::TextView *blockName_;
	UI::TextEdit *blockAddr_;
	UI::TextView *blockStats_;

	UI::LinearLayout *leftDisasm_;
	UI::LinearLayout *rightDisasm_;
};

class AddressPromptScreen : public PopupScreen {
public:
	AddressPromptScreen(std::string_view title) : PopupScreen(title, "OK", "Cancel") {}

	const char *tag() const override { return "AddressPrompt"; }

	bool key(const KeyInput &key) override;

	UI::Event OnChoice;

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void OnCompleted(DialogResult result) override;
	UI::EventReturn OnDigitButton(UI::EventParams &e);
	UI::EventReturn OnBackspace(UI::EventParams &e);

private:
	void AddDigit(int n);
	void BackspaceDigit();
	void UpdatePreviewDigits();

	UI::TextView *addrView_ = nullptr;
	UI::Button *buttons_[16]{};
	unsigned int addr_ = 0;
};
