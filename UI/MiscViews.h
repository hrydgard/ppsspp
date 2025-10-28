#pragma once

#include "Common/UI/View.h"
#include "UI/ViewGroup.h"

// Compound view, showing a text with an icon.
class TextWithImage : public UI::LinearLayout {
public:
	TextWithImage(ImageID imageID, std::string_view text, UI::LinearLayoutParams *layoutParams = nullptr);
};

// Compound view, showing a copyable string.
class CopyableText : public UI::LinearLayout {
public:
	CopyableText(ImageID imageID, std::string_view text, UI::LinearLayoutParams *layoutParams = nullptr);
};

class TopBar : public UI::LinearLayout {
public:
	TopBar(std::string_view title, UI::LayoutParams *layoutParams = nullptr);
	UI::View *GetBackButton() const { return backButton_; }
private:
	UI::Choice *backButton_ = nullptr;
};
