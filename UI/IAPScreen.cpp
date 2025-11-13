// NOTE: This currently only used on iOS, to present the availablility of getting PPSSPP Gold through IAP.

#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/OSD.h"
#include "Common/Render/DrawBuffer.h"
#include "UI/IAPScreen.h"
#include "UI/OnScreenDisplay.h"
#include "UI/MiscViews.h"

void IAPScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	root_ = new LinearLayout(portrait ? ORIENT_VERTICAL : ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	
	const bool bought = System_GetPropertyBool(SYSPROP_APP_GOLD);

	// TODO: Support vertical layout!
	AnchorLayout *leftColumnContainer = new AnchorLayout(new LinearLayoutParams(1.0f, UI::Gravity::G_HCENTER));

	LinearLayout *leftColumnItems = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(600, WRAP_CONTENT, NONE, 105, NONE, 15));
	leftColumnContainer->Add(leftColumnItems);
	root_->Add(leftColumnContainer);

	ViewGroup *appTitle = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	appTitle->Add(new ShinyIcon(ImageID("I_ICON_GOLD"), new LinearLayoutParams(64, 64)));
	appTitle->Add(new TextView("PPSSPP Gold", new LinearLayoutParams(1.0f, Gravity::G_VCENTER)));

	leftColumnItems->Add(appTitle);
	if (!bought) {
		leftColumnItems->Add(new Spacer(20.0f));
		leftColumnItems->Add(new TextView(di->T("GoldOverview1", "Buy PPSSPP Gold to support development!")))->SetAlign(FLAG_WRAP_TEXT);
		leftColumnItems->Add(new Spacer(10.0f));
		leftColumnItems->Add(new TextView(di->T("GoldOverview2", "It helps sustain development!")))->SetAlign(FLAG_WRAP_TEXT);
	} else {
		leftColumnItems->Add(new TextView(di->T("GoldThankYou", "Thank you for supporting the PPSSPP project!")))->SetAlign(FLAG_WRAP_TEXT);
	}

	leftColumnItems->Add(new Spacer(20.0f));
	leftColumnItems->Add(new TextView("Henrik Rydgård"));
	leftColumnItems->Add(new TextView("(hrydgard)"));

	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(300, WRAP_CONTENT, UI::Margins(15,15)));
	root_->Add(rightColumnItems);

	if (!bought) {
		Choice *buyButton = rightColumnItems->Add(new Choice(mm->T("Buy PPSSPP Gold")));
		buyButton->SetIcon(ImageID("I_ICON_GOLD"), 0.5f);
		buyButton->SetShine(true);
		const int requesterToken = GetRequesterToken();
		buyButton->OnClick.Add([this, requesterToken](UI::EventParams &) {
			INFO_LOG(Log::System, "Showing purchase UI...");
			System_IAPMakePurchase(requesterToken, "org.ppsspp.gold", [this](const char *responseString, int intValue) {
				INFO_LOG(Log::System, "PPSSPP Gold purchase successful!");
				auto di = GetI18NCategory(I18NCat::DIALOG);
				g_OSD.Show(OSDType::MESSAGE_SUCCESS, di->T("GoldThankYou", "Thank you for supporting the PPSSPP project!"), 3.0f);
				RecreateViews();
			}, []() {
				WARN_LOG(Log::System, "Purchase failed or cancelled!");
			});
			// TODO: What do we do here?
		});
	}

	Choice *moreInfo = rightColumnItems->Add(new Choice(di->T("More info")));
	moreInfo->OnClick.Add([](UI::EventParams &) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/buygold_ios");
	});

	Choice *backButton = rightColumnItems->Add(new Choice(di->T("Back")));
	backButton->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

	// Put the restore purchases button in the bottom right corner. It's rarely useful, but needed.
	rightColumnItems->Add(new Spacer(new LinearLayoutParams(1.0f)));
	Choice *restorePurchases = new Choice(di->T("Restore purchase"));
	const int requesterToken = GetRequesterToken();
	restorePurchases->OnClick.Add([this, requesterToken, restorePurchases](UI::EventParams &) {
		restorePurchases->SetEnabled(false);
		INFO_LOG(Log::System, "Requesting purchase restore");
		System_IAPRestorePurchases(requesterToken, [this](const char *responseString, int) {
			INFO_LOG(Log::System, "Successfully restored purchases!");
			RecreateViews();
		}, []() {
			WARN_LOG(Log::System, "Failed restoring purchases");
		});
	});
	rightColumnItems->Add(restorePurchases);
}
