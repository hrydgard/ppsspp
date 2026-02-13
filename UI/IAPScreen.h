#pragma once

// NOTE: This is only used on iOS, to present the availablility of getting PPSSPP Gold through IAP.

#include "ppsspp_config.h"

#include "UI/BaseScreens.h"
#include "UI/SimpleDialogScreen.h"

class IAPScreen : public UIBaseDialogScreen {
public:
	IAPScreen(bool useIAP) : UIBaseDialogScreen(), useIAP_(useIAP) {}
	void CreateViews() override;
	const char *tag() const override { return "IAP"; }
private:
	// This screen can also be used to direct to Play Store purchases, for example.
	bool useIAP_ = false;
};

void LaunchPlayStoreOrWebsiteGold();
