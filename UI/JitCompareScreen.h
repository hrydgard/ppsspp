#pragma once
#include "Common/UI/UIScreen.h"
#include "Common/UI/PopupScreens.h"
#include "UI/BaseScreens.h"
#include "UI/TabbedDialogScreen.h"

class JitCompareScreen : public UITabbedBaseDialogScreen {
public:
	JitCompareScreen();
	void CreateTabs() override;

	const char *tag() const override { return "JitCompare"; }

private:
	bool ShowSearchControls() const override { return false; }
	void UpdateDisasm();

	// Uses the current ListType
	void FillBlockList();

	UI::LinearLayout *comparisonView_ = nullptr;
	UI::LinearLayout *leftDisasm_ = nullptr;
	UI::LinearLayout *rightDisasm_ = nullptr;

	UI::LinearLayout *blockListView_ = nullptr;
	UI::LinearLayout *blockListContainer_ = nullptr;

	void OnSelectBlock(UI::EventParams &e);
	void OnBlockAddress(UI::EventParams &e);
	void OnAddressChange(UI::EventParams &e);
	void OnBlockClick(UI::EventParams &e);

	enum class ListType {
		ALL_BLOCKS,
		FPU_BLOCKS,
		VFPU_BLOCKS,
	};
	enum class ListSort {
		BLOCK_NUM,
		BLOCK_LENGTH_DESC,
		BLOCK_LENGTH_ASC,
		TIME_SPENT,
		EXECUTIONS,
		MAX
	};
	ListType listType_ = ListType::ALL_BLOCKS;
	ListSort listSort_ = ListSort::TIME_SPENT;

	int currentBlock_ = -1;  // For DISASM mode
	int64_t sumTotalNanos_ = 0;
	int64_t sumExecutions_ = 0;
	std::vector<int> blockList_;  // for BLOCK_LIST mode

	UI::TextView *blockName_ = nullptr;
	UI::TextEdit *blockAddr_ = nullptr;
	UI::TextView *blockStats_ = nullptr;

	UI::TextView *globalStats_ = nullptr;
};

class AddressPromptScreen : public UI::PopupScreen {
public:
	AddressPromptScreen(std::string_view title) : PopupScreen(title, "OK", "Cancel") {}

	const char *tag() const override { return "AddressPrompt"; }

	bool key(const KeyInput &key) override;

	UI::Event OnChoice;

protected:
	void CreatePopupContents(UI::ViewGroup *parent) override;
	void OnCompleted(DialogResult result) override;
	void OnDigitButton(UI::EventParams &e);
	void OnBackspace(UI::EventParams &e);

private:
	void AddDigit(int n);
	void BackspaceDigit();
	void UpdatePreviewDigits();

	UI::TextView *addrView_ = nullptr;
	UI::Button *buttons_[16]{};
	unsigned int addr_ = 0;
};
