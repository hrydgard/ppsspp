#include "Common/UI/View.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/System/Request.h"
#include "UI/MiscViews.h"

TextWithImage::TextWithImage(ImageID imageID, std::string_view text, UI::LinearLayoutParams *layoutParams) : UI::LinearLayout(ORIENT_HORIZONTAL, layoutParams) {
	using namespace UI;
	SetSpacing(8.0f);
	if (!layoutParams) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = ITEM_HEIGHT;
	}
	if (imageID.isValid()) {
		Add(new ImageView(imageID, "", UI::IS_DEFAULT, new LinearLayoutParams(0.0f, UI::Gravity::G_VCENTER)));
	}
	Add(new TextView(text, new LinearLayoutParams(1.0f, UI::Gravity::G_VCENTER)));
}

CopyableText::CopyableText(ImageID imageID, std::string_view text, UI::LinearLayoutParams *layoutParams) : UI::LinearLayout(ORIENT_HORIZONTAL, layoutParams) {
	using namespace UI;
	SetSpacing(8.0f);
	if (!layoutParams) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = ITEM_HEIGHT;
	}
	if (imageID.isValid()) {
		Add(new ImageView(imageID, "", UI::IS_DEFAULT, new LinearLayoutParams(0.0f, UI::Gravity::G_VCENTER)));
	}
	Add(new TextView(text, new LinearLayoutParams(1.0f, UI::Gravity::G_VCENTER)))->SetBig(true);

	std::string textStr(text);  // We need to store the text in the lambda context.
	Add(new Choice(ImageID("I_FILE_COPY"), new LinearLayoutParams()))->OnClick.Add([textStr](UI::EventParams &) {
		System_CopyStringToClipboard(textStr);
	});
}

TopBar::TopBar(std::string_view title, UI::LayoutParams *layoutParams) : UI::LinearLayout(ORIENT_HORIZONTAL, layoutParams) {
	using namespace UI;
	SetSpacing(10.0f);
	if (!layoutParams) {
		layoutParams_->width = UI::FILL_PARENT;
		layoutParams_->height = 64.0f;
	}

	backButton_ = Add(new Choice(ImageID("I_NAVIGATE_BACK"), new LinearLayoutParams()));
	backButton_ ->OnClick.Add([](UI::EventParams &e) {
		e.bubbleResult = DR_BACK;
	});

	if (!title.empty()) {
		Add(new TextView(title, ALIGN_CENTER | FLAG_WRAP_TEXT, false, new LinearLayoutParams(1.0f, G_VCENTER)));
		// To balance the centering, add a spacer on the right.
		Add(new Spacer(50.0f));
	}
}
