// Copyright (c) 2013- PPSSPP Project.

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

#include "gfx_es2/gl_state.h"
#include "i18n/i18n.h"
#include "ui/ui_context.h"
#include "ui/view.h"
#include "ui/viewgroup.h"
#include "ui/ui.h"
#include "UI/MiscScreens.h"
#include "UI/DevScreens.h"
#include "UI/GameSettingsScreen.h"
#include "Common/LogManager.h"
#include "Core/Config.h"


void DevMenu::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	parent->Add(new Choice("Log Channels"))->OnClick.Handle(this, &DevMenu::OnLogConfig);
	parent->Add(new Choice("Developer Tools"))->OnClick.Handle(this, &DevMenu::OnDeveloperTools);
}

UI::EventReturn DevMenu::OnLogConfig(UI::EventParams &e) {
	screenManager()->push(new LogConfigScreen());
	return UI::EVENT_DONE;
}

UI::EventReturn DevMenu::OnDeveloperTools(UI::EventParams &e) {
	screenManager()->push(new DeveloperToolsScreen());
	return UI::EVENT_DONE;
}

void DevMenu::dialogFinished(const Screen *dialog, DialogResult result) {
	// Close when a subscreen got closed.
	// TODO: a bug in screenmanager causes this not to work here.
	// screenManager()->finishDialog(this, DR_OK);
}


// It's not so critical to translate everything here, most of this is developers only.

void LogConfigScreen::CreateViews() {
	using namespace UI;

	I18NCategory *d = GetI18NCategory("Dialog");

	root_ = new ScrollView(ORIENT_VERTICAL);

	LinearLayout *vert = root_->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	vert->SetSpacing(0);

	LinearLayout *topbar = new LinearLayout(ORIENT_HORIZONTAL);
	topbar->Add(new Choice("Back"))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	topbar->Add(new Choice("Toggle All"))->OnClick.Handle(this, &LogConfigScreen::OnToggleAll);

	vert->Add(topbar);

	vert->Add(new ItemHeader("Log Channels"));

	static const char *logLevelList[] = {
		"Notice",
		"Error",
		"Warn",
		"Info",
		"Debug",
		"Verb."
	};

	LogManager *logMan = LogManager::GetInstance();

	int cellSize = 400;

	UI::GridLayoutSettings gridsettings(cellSize, 64, 5);
	gridsettings.fillCells = true;
	GridLayout *grid = vert->Add(new GridLayout(gridsettings, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));

	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		LogChannel *chan = logMan->GetLogChannel(type);
		LinearLayout *row = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(cellSize - 50, WRAP_CONTENT));
		row->SetSpacing(0);
		row->Add(new CheckBox(&chan->enable_, "", "", new LinearLayoutParams(50, WRAP_CONTENT)));
		row->Add(new PopupMultiChoice(&chan->level_, chan->GetFullName(), logLevelList, 1, 6, 0, screenManager(), new LinearLayoutParams(1.0)));
		grid->Add(row);
	}
}

UI::EventReturn LogConfigScreen::OnToggleAll(UI::EventParams &e) {
	LogManager *logMan = LogManager::GetInstance();
	
	for (int i = 0; i < LogManager::GetNumChannels(); i++) {
		LogTypes::LOG_TYPE type = (LogTypes::LOG_TYPE)i;
		LogChannel *chan = logMan->GetLogChannel(type);
		chan->enable_ = !chan->enable_;
	}

	return UI::EVENT_DONE;
}

void SystemInfoScreen::CreateViews() {
	// NOTE: Do not translate this section. It will change a lot and will be impossible to keep up.
	I18NCategory *d = GetI18NCategory("Dialog");

	using namespace UI;
	root_ = new ScrollView(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, FILL_PARENT));

	LinearLayout *scroll = new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT));
	root_->Add(scroll);

	scroll->Add(new ItemHeader("System Information"));
	scroll->Add(new InfoItem("System Name", System_GetProperty(SYSPROP_NAME)));
	scroll->Add(new InfoItem("System Lang/Region", System_GetProperty(SYSPROP_LANGREGION)));
	scroll->Add(new InfoItem("GPU Vendor", (char *)glGetString(GL_VENDOR)));
	scroll->Add(new InfoItem("GPU Model", (char *)glGetString(GL_RENDERER)));
	scroll->Add(new InfoItem("OpenGL Version Supported", (char *)glGetString(GL_VERSION)));
	scroll->Add(new Button(d->T("Back"), new LayoutParams(260, 64)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);

#ifdef _WIN32
	scroll->Add(new ItemHeader("OpenGL Extensions"));
#else
	scroll->Add(new ItemHeader("OpenGL ES 2.0 Extensions"));
#endif
	std::vector<std::string> exts;
	SplitString(g_all_gl_extensions, ' ', exts);
	for (size_t i = 0; i < exts.size(); i++) {
		scroll->Add(new TextView(exts[i]));
	}

	scroll->Add(new ItemHeader("EGL Extensions"));
	exts.clear();
	SplitString(g_all_egl_extensions, ' ', exts);
	for (size_t i = 0; i < exts.size(); i++) {
		scroll->Add(new TextView(exts[i]));
	}
}
