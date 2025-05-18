// NOTE: This currently only used on iOS, to present the availablility of getting PPSSPP Gold through IAP.

#include "UI/IAPScreen.h"
#include "UI/OnScreenDisplay.h"
#include "Common/System/System.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/OSD.h"

void IAPScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));
	
	const bool bought = System_GetPropertyBool(SYSPROP_APP_GOLD);

	// TODO: Support vertical layout!

	ViewGroup *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 15, 15, 330, 10));
	root_->Add(leftColumnItems);

	ViewGroup *appTitle = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	appTitle->Add(new ImageView(ImageID("I_ICONGOLD"), "", IS_DEFAULT, new LinearLayoutParams(64, 64)));
	appTitle->Add(new TextView("PPSSPP Gold", new LinearLayoutParams(1.0f, G_VCENTER)));
	leftColumnItems->Add(appTitle);

	leftColumnItems->Add(new TextView(di->T("GoldOverview", "Buy PPSSPP Gold to support development!")));

	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(300, WRAP_CONTENT, NONE, 105, 15, NONE));
	root_->Add(rightColumnItems);

	if (!bought) {
		Choice *buyButton = rightColumnItems->Add(new Choice(mm->T("Buy PPSSPP Gold")));
		const int requesterToken = GetRequesterToken();
		buyButton->OnClick.Add([this, requesterToken](UI::EventParams &) {
			INFO_LOG(Log::System, "Showing purchase UI...");
			System_IAPMakePurchase(requesterToken, "org.ppsspp.gold", [this](const char *responseString, int intValue) {
				INFO_LOG(Log::System, "Purchase successful!");
				auto di = GetI18NCategory(I18NCat::DIALOG);
				g_OSD.Show(OSDType::MESSAGE_SUCCESS, di->T("Thank you for supporting the PPSSPP project!"), 3.0f);
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
