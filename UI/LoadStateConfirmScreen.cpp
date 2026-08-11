#include "UI/LoadStateConfirmScreen.h"

#include "Common/UI/AsyncImageFileView.h"
#include "Common/UI/ViewGroup.h"
#include "Common/Data/Text/I18n.h"
#include "Core/SaveState.h"

LoadStateConfirmScreen::LoadStateConfirmScreen(std::string_view saveStatePrefix, int slot, std::function<void(bool)> callback)
	: UI::PopupScreen(GetI18NCategory(I18NCat::PAUSE)->T("Load State"),
	                  GetI18NCategory(I18NCat::PAUSE)->T("Load State"),
	                  GetI18NCategory(I18NCat::DIALOG)->T("Cancel")),
	  saveStatePrefix_(saveStatePrefix), slot_(slot), callback_(callback) {
	screenshotFilename_ = SaveState::GenerateSaveSlotPath(saveStatePrefix_, slot_, SaveState::SCREENSHOT_EXTENSION);
}

void LoadStateConfirmScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	// 480:272 PSP ratio; at default popup width 550, effective area ~510 → height ~289
	parent->Add(new AsyncImageFileView(screenshotFilename_, IS_KEEP_ASPECT,
		new LinearLayoutParams(FILL_PARENT, 289.0f)));
	std::string dateStr = SaveState::GetSlotDateAsString(saveStatePrefix_, slot_);
	if (!dateStr.empty()) {
		parent->Add(new TextView(dateStr, ALIGN_HCENTER, false,
			new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 4))));
	}
	parent->Add(new TextView(di->T("ConfirmLoadState"), ALIGN_HCENTER | FLAG_WRAP_TEXT, false,
		new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(0, 8))));
}

void LoadStateConfirmScreen::TriggerFinish(DialogResult result) {
	if (callback_) {
		callback_(result == DR_OK);
		callback_ = nullptr;
	}
	UI::PopupScreen::TriggerFinish(DR_CANCEL);
}
