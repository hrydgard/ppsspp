#include <algorithm>

#include "Common/StringUtils.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"
#include "Common/System/Display.h"
#include "UI/TabbedDialogScreen.h"

void TabbedUIDialogScreenWithGameBackground::AddTab(const char *tag, std::string_view title, std::function<void(UI::LinearLayout *)> createCallback, bool isSearch) {
	using namespace UI;

	tabHolder_->AddTabDeferred(title, [this, createCallback = std::move(createCallback), tag, title, isSearch]() -> UI::ViewGroup * {
		ViewGroup *scroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
		scroll->SetTag(tag);
		LinearLayout *contents = new LinearLayoutList(ORIENT_VERTICAL);
		contents->SetSpacing(0);
		scroll->Add(contents);
		createCallback(contents);
		return scroll;
	});
}

void TabbedUIDialogScreenWithGameBackground::CreateViews() {
	PreCreateViews();

	bool vertical = UseVerticalLayout();

	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	auto se = GetI18NCategory(I18NCat::SEARCH);
	filterNotice_ = new TextView("(filter notice, you shouldn't see this text", new LinearLayoutParams(Margins(20, 5)));
	filterNotice_->SetVisibility(V_GONE);

	if (vertical) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		LinearLayout *verticalLayout = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
		tabHolder_ = new TabHolder(ORIENT_HORIZONTAL, 200, filterNotice_, new LinearLayoutParams(1.0f));
		verticalLayout->Add(tabHolder_);
		CreateExtraButtons(verticalLayout, 0);
		verticalLayout->Add(new Choice(di->T("Back"), "", false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, 0.0f, Margins(0))))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
		root_->Add(verticalLayout);
	} else {
		tabHolder_ = new TabHolder(ORIENT_VERTICAL, 200, filterNotice_, new AnchorLayoutParams(10, 0, 10, 0, false));
		CreateExtraButtons(tabHolder_->Container(), 10);
		tabHolder_->AddBack(this);
		root_->Add(tabHolder_);
	}

	tabHolder_->SetTag(tag());  // take the tag from the screen.
	root_->SetDefaultFocusView(tabHolder_);

	float leftSide = 40.0f;
	if (!vertical) {
		leftSide += 200.0f;
	}
	settingInfo_ = new SettingInfoMessage(ALIGN_CENTER | FLAG_WRAP_TEXT, g_display.dp_yres - 200.0f, new AnchorLayoutParams(
		g_display.dp_xres - leftSide - 40.0f, WRAP_CONTENT,
		leftSide, g_display.dp_yres - 80.0f - 40.0f, NONE, NONE));
	root_->Add(settingInfo_);

	// Show it again if we recreated the view
	if (!oldSettingInfo_.empty()) {
		settingInfo_->Show(oldSettingInfo_, nullptr);
	}

	// Let the subclass create its tabs.
	CreateTabs();

	if (System_GetPropertyBool(SYSPROP_HAS_KEYBOARD) || System_GetPropertyBool(SYSPROP_HAS_TEXT_INPUT_DIALOG)) {
		// Hide search if screen is too small.
		int deviceType = System_GetPropertyInt(SYSPROP_DEVICE_TYPE);
		if ((g_display.dp_xres < g_display.dp_yres || g_display.dp_yres >= 500) && (deviceType != DEVICE_TYPE_VR) && ShowSearchControls()) {
			// Search
			auto ms = GetI18NCategory(I18NCat::MAINSETTINGS);
			AddTab("GameSettingsSearch", ms->T("Search"), [this](UI::LinearLayout *searchSettings) {
				auto se = GetI18NCategory(I18NCat::SEARCH);

				searchSettings->Add(new ItemHeader(se->T("Find settings")));
				searchSettings->Add(new PopupTextInputChoice(GetRequesterToken(), &searchFilter_, se->T("Filter"), "", 64, screenManager()))->OnChange.Add([=](UI::EventParams &e) {
					System_PostUIMessage(UIMessage::GAMESETTINGS_SEARCH, StripSpaces(searchFilter_));
					return UI::EVENT_DONE;
				});

				clearSearchChoice_ = searchSettings->Add(new Choice(se->T("Clear filter")));
				clearSearchChoice_->OnClick.Add([=](UI::EventParams &e) {
					System_PostUIMessage(UIMessage::GAMESETTINGS_SEARCH, "");
					return UI::EVENT_DONE;
				});

				noSearchResults_ = searchSettings->Add(new TextView("", new LinearLayoutParams(Margins(20, 5))));
			}, true);

		}
	}
}

void TabbedUIDialogScreenWithGameBackground::sendMessage(UIMessage message, const char *value) {
	UIDialogScreenWithGameBackground::sendMessage(message, value);
	if (message == UIMessage::GAMESETTINGS_SEARCH) {
		std::string filter = value ? value : "";
		searchFilter_.resize(filter.size());
		std::transform(filter.begin(), filter.end(), searchFilter_.begin(), tolower);

		ApplySearchFilter();
	}
}

void TabbedUIDialogScreenWithGameBackground::RecreateViews() {
	oldSettingInfo_ = settingInfo_ ? settingInfo_->GetText() : "N/A";
	UIScreen::RecreateViews();
}

void TabbedUIDialogScreenWithGameBackground::EnsureTabs() {
	tabHolder_->EnsureAllCreated();
}

void TabbedUIDialogScreenWithGameBackground::ApplySearchFilter() {
	using namespace UI;
	auto se = GetI18NCategory(I18NCat::SEARCH);

	tabHolder_->EnsureAllCreated();

	// Show an indicator that a filter is applied.
	filterNotice_->SetVisibility(searchFilter_.empty() ? UI::V_GONE : UI::V_VISIBLE);
	filterNotice_->SetText(ApplySafeSubstitutions(se->T("Filtering settings by '%1'"), searchFilter_));

	bool matches = searchFilter_.empty();

	const std::vector<ViewGroup *> settingTabs = tabHolder_->GetTabContentViews();

	for (int t = 0; t < (int)settingTabs.size(); ++t) {
		const ViewGroup *tabContents = settingTabs[t];
		std::string_view tag = tabContents->Tag();

		// Dive down to the actual list of settings.
		// TODO: Do this recursively instead.
		while (tabContents->GetNumSubviews() == 1) {
			View *v = tabContents->GetViewByIndex(0);
			if (v->IsViewGroup()) {
				tabContents = (ViewGroup *)v;
			}
		}

		if (tag == "GameSettingsSearch") {
			continue;
		}

		bool tabMatches = searchFilter_.empty();

		UI::View *lastHeading = nullptr;
		for (int i = 1; i < tabContents->GetNumSubviews(); ++i) {
			UI::View *v = tabContents->GetViewByIndex(i);
			if (!v->CanBeFocused()) {
				lastHeading = v;
			}

			std::string label = v->DescribeText();
			std::transform(label.begin(), label.end(), label.begin(), tolower);
			bool match = v->CanBeFocused() && label.find(searchFilter_) != label.npos;
			tabMatches = tabMatches || match;

			if (match && lastHeading)
				lastHeading->SetVisibility(UI::V_VISIBLE);
			v->SetVisibility(searchFilter_.empty() || match ? UI::V_VISIBLE : UI::V_GONE);
		}
		tabHolder_->EnableTab(t, tabMatches);
		matches = matches || tabMatches;
	}

	noSearchResults_->SetText(ApplySafeSubstitutions(se->T("No settings matched '%1'"), searchFilter_));
	noSearchResults_->SetVisibility(matches ? UI::V_GONE : UI::V_VISIBLE);
	clearSearchChoice_->SetVisibility(searchFilter_.empty() ? UI::V_GONE : UI::V_VISIBLE);
}
