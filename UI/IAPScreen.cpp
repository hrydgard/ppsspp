// NOTE: This is only used on iOS, to present the availablility of getting PPSSPP Gold through IAP.

#include "UI/IAPScreen.h"
#include "Common/System/System.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/OSD.h"

void IAPScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	
	const bool bought = System_GetPropertyBool(SYSPROP_APP_GOLD);

	if (bought) {
		root_->Add(new TextView("You have PPSSPP Gold! Thank you!"));
	} else {
		root_->Add(new TextView("PPSSPP Gold", ALIGN_LEFT | FLAG_WRAP_TEXT, false, new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 15, 105, 330, 10)))->SetClip(false);
	}

	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(300, WRAP_CONTENT, NONE, 105, 15, NONE));
	root_->Add(rightColumnItems);

	if (!bought) {
		Choice *buyButton = rightColumnItems->Add(new Choice(mm->T("Buy PPSSPP Gold")));
		const int requesterToken = GetRequesterToken();
		buyButton->OnClick.Add([this, requesterToken](UI::EventParams &) {
			INFO_LOG(Log::System, "Showing purchase UI...");
			System_IAPMakePurchase(requesterToken, "org.ppsspp.gold", [this](const char *responseString, int intValue) {
				INFO_LOG(Log::System, "Purchase successful!");
				RecreateViews();
			}, []() {
				WARN_LOG(Log::System, "Purchase failed or cancelled!");
			});
			// TODO: What do we do here?
			return UI::EVENT_DONE;
		});
	}

	Choice *moreInfo = rightColumnItems->Add(new Choice(di->T("More info")));
	moreInfo->OnClick.Add([](UI::EventParams &) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/docs/reference/whygold/");
		return UI::EVENT_DONE;
	});

	Choice *backButton = rightColumnItems->Add(new Choice(di->T("Back")));
	backButton->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	// Put the restore purchases button in the bottom right corner. It's rarely useful, but needed.
	rightColumnItems->Add(new Spacer(new LinearLayoutParams(1.0f)));
	Choice *restorePurchases = new Choice(di->T("Restore purchase"));
	const int requesterToken = GetRequesterToken();
	restorePurchases->OnClick.Add([requesterToken, restorePurchases](UI::EventParams &) {
		restorePurchases->SetEnabled(false);
		INFO_LOG(Log::System, "Requesting purchase restore");
		System_IAPRestorePurchases(requesterToken, [restorePurchases](const char *responseString, int) {
			INFO_LOG(Log::System, "Successfully restored purchases!");
		}, []() {
			WARN_LOG(Log::System, "Failed restoring purchases");
		});
		return UI::EVENT_DONE;
	});
	rightColumnItems->Add(restorePurchases);
}
