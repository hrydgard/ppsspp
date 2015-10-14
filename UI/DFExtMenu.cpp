// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "input/input_state.h"
#include "ui/ui.h"
#include "util/text/utf8.h"
#include "i18n/i18n.h"
#include "ui/view.h"
#include "ui/viewgroup.h"

#include "Core/Core.h"
#include "Core/Config.h"
#include "Core/MIPS/JitCommon/NativeJit.h"

#include "UI/OnScreenDisplay.h"
#include "UI/ui_atlas.h"
#include "UI/GamepadEmu.h"

#include "UI/MainScreen.h"
#include "UI/EmuScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/MiscScreens.h"
#include "UI/DevScreens.h"
#include "UI/DFExtMenu.h"

static DarkFrostEngine *darkFrostEngine;
int extMenu;
unsigned int exactValue;
bool refreshed;

DFExtMenu::DFExtMenu() {
	extMenu=-1;
	refreshed=false;
}

DFExtMenu::DFExtMenu(int nExtMenu, DarkFrostEngine *nDarkFrostEngine) {
	darkFrostEngine=nDarkFrostEngine;
	extMenu=nExtMenu;
	refreshed=false;
}

void DFExtMenu::CreateViews() {

	using namespace UI;
	I18NCategory *em = GetI18NCategory("extMenu");

	root_ = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));//new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT));

	//Content Holder
	ViewGroup *contentScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));
	LinearLayout *content = new LinearLayout(ORIENT_VERTICAL);
	content->SetSpacing(0);
	contentScroll->Add(content);
	root_->Add(contentScroll);

	auto exactPrompt = new AddressPromptScreen(em->T("Value to Search"));
	char buffer[64];
	//unsigned char asciiVal[5];

	switch(extMenu)
	{
		case 0://CHEATER
			content->Add(new ItemHeader(em->T("Extra Menu")));//Cheat name
			content->Add(new TextView(em->T("Extra Menu #1")));
			break;
		case 1://EXACT SEARCH
			content->Add(new ItemHeader(em->T("Exact Search")));
			//prompt for the value
			if(!refreshed)
			{
				exactPrompt->OnChoice.Handle(this, &DFExtMenu::OnAddressPrompt);
				screenManager()->push(exactPrompt);
			}
			
			//display the hakval
			content->Add(new TextView(em->T("Hex         Decimal     ASCII        Float")));
			//for(int i=0;i<4;i++) asciiVal[i]=darkFrostEngine->getASCII(exactValue, i);
			sprintf(buffer, "0x%08lX  %010u  %c%c%c%c", exactValue, exactValue,
				*((unsigned char*)&exactValue), *((unsigned char*)&exactValue+1),
				*((unsigned char*)&exactValue+2), *((unsigned char*)&exactValue+3));
			content->Add(new TextView(em->T(buffer)));
			break;
		case 2://DIFF SEARCH 8 BIT
			content->Add(new ItemHeader(em->T("Unknown Value 8 Bit")));
			content->Add(new TextView(em->T("Extra Menu #2")));
			break;
		case 3://DIFF SEARCH 16 BIT
			content->Add(new ItemHeader(em->T("Unknown Value 16 Bit")));
			content->Add(new TextView(em->T("Extra Menu #1")));
			break;
		case 4://DIFF SEARCH 32 BIT
			content->Add(new ItemHeader(em->T("Unknown Value 32 Bit")));
			content->Add(new TextView(em->T("Extra Menu #2")));
			break;
		default://invalid
			content->Add(new ItemHeader(em->T("Extra Menu")));
			content->Add(new TextView(em->T("Invalid Menu")));
			break;
	}

	//BACK
	LinearLayout *standardBack = new LinearLayout(ORIENT_HORIZONTAL, new LayoutParams(FILL_PARENT, FILL_PARENT));
	AddStandardBack(standardBack);
	content->Add(standardBack);
}

UI::EventReturn DFExtMenu::OnAddressPrompt(UI::EventParams &params) {
	//nothing yet
	unsigned int oldValue=exactValue;
	exactValue=params.a;
	if(exactValue != oldValue)
	{
		refreshed=true;
		CreateViews();
	}
	return UI::EVENT_DONE;
}