// NOTE: This currently only used on iOS, to present the availablility of getting PPSSPP Gold through IAP.

#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/Data/Text/I18n.h"
#include "Common/System/OSD.h"
#include "Common/Render/DrawBuffer.h"
#include "UI/IAPScreen.h"
#include "UI/OnScreenDisplay.h"
#include "UI/MiscViews.h"

std::string_view IAPScreen::GetTitle() const {
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	return mm->T("Buy PPSSPP Gold");
}

void IAPScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;
	const bool bought = System_GetPropertyBool(SYSPROP_APP_GOLD);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	ViewGroup *leftColumnItems = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(16, 0, 16, 12))));
	leftColumnItems->Add(new Spacer(35.0f));

	ViewGroup *appTitle = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));
	appTitle->Add(new ShinyIcon(ImageID("I_ICON_GOLD"), new LinearLayoutParams(64, 64)));
	appTitle->Add(new TextView("PPSSPP Gold", new LinearLayoutParams(1.0f, Gravity::G_VCENTER)));

	leftColumnItems->Add(appTitle);
	if (!bought) {
		leftColumnItems->Add(new Spacer(30.0f));
		leftColumnItems->Add(new TextView(di->T("GoldOverview1", "Buy PPSSPP Gold to support development!")))->SetAlign(FLAG_WRAP_TEXT);
		leftColumnItems->Add(new Spacer(10.0f));
		leftColumnItems->Add(new TextView(di->T("GoldOverview2", "It helps sustain development!")))->SetAlign(FLAG_WRAP_TEXT);
	} else {
		leftColumnItems->Add(new TextView(di->T("GoldThankYou", "Thank you for supporting the PPSSPP project!")))->SetAlign(FLAG_WRAP_TEXT);
	}

	leftColumnItems->Add(new Spacer(30.0f));
	leftColumnItems->Add(new TextView("Henrik Rydgård"));
	leftColumnItems->Add(new TextView("(hrydgard)"));
}

void IAPScreen::CreateSettingsViews(UI::ViewGroup *rightColumnItems) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	const bool bought = System_GetPropertyBool(SYSPROP_APP_GOLD);
	if (!bought) {
		ImageID image;
#if PPSSPP_PLATFORM(ANDROID)
		image = ImageID("I_LOGO_PLAY_STORE");
#elif PPSSPP_PLATFORM(IOS)
		image = ImageID("I_LOGO_APP_STORE");
#endif
		Choice *buyButton = rightColumnItems->Add(new Choice(mm->T("Buy PPSSPP Gold"), image));
		buyButton->SetIconRight(ImageID("I_ICON_GOLD"), 0.5f);
		buyButton->SetShine(true);
		const int requesterToken = GetRequesterToken();
		buyButton->OnClick.Add([this, requesterToken](UI::EventParams &) {
			INFO_LOG(Log::System, "Showing purchase UI...");
			if (useIAP_) {
				System_IAPMakePurchase(requesterToken, "org.ppsspp.gold", [this](const char *responseString, int intValue) {
					INFO_LOG(Log::System, "PPSSPP Gold purchase successful!");
					auto di = GetI18NCategory(I18NCat::DIALOG);
					g_OSD.Show(OSDType::MESSAGE_SUCCESS, di->T("GoldThankYou", "Thank you for supporting the PPSSPP project!"), 3.0f);
					RecreateViews();
				}, []() {
					WARN_LOG(Log::System, "Purchase failed or cancelled!");
				});
			} else {
				LaunchPlayStoreOrWebsiteGold();
			}
		});
		rightColumnItems->Add(new Spacer(12.0f));
	}

	Choice *moreInfo = rightColumnItems->Add(new Choice(di->T("More info"), ImageID("I_LINK_OUT")));
	moreInfo->OnClick.Add([](UI::EventParams &) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/buygold_ios");
	});

	if (useIAP_) {
		rightColumnItems->Add(new Spacer(12.0f));
		// Put the restore purchases button in the bottom right corner in landscape. It's rarely useful, but needed.
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
}

void LaunchPlayStoreOrWebsiteGold() {
#if PPSSPP_PLATFORM(ANDROID)
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "market://details?id=org.ppsspp.ppssppgold");
#else
	System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/buygold");
#endif
}
