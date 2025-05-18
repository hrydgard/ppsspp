#pragma once

// NOTE: This is only used on iOS, to present the availablility of getting PPSSPP Gold through IAP.

#include "ppsspp_config.h"

#include "UI/MiscScreens.h"

class IAPScreen : public UIDialogScreenWithBackground {
public:
	void CreateViews() override;
	const char *tag() const override { return "IAP"; }
};
