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

#include "ppsspp_config.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/UI.h"

#include "Common/System/Display.h"
#include "Common/System/NativeApp.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/Math/curves.h"
#include "Common/File/VFS/VFS.h"

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Data/Text/I18n.h"
#include "Common/TimeUtil.h"
#include "Common/File/FileUtil.h"
#include "Common/Render/ManagedTexture.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "Core/MIPS/JitCommon/JitCommon.h"
#include "Core/HLE/sceUtility.h"
#include "Core/Util/RecentFiles.h"
#include "GPU/GPUState.h"
#include "GPU/Common/PostShader.h"

#include "UI/ControlMappingScreen.h"
#include "UI/Background.h"
#include "UI/DisplayLayoutScreen.h"
#include "UI/EmuScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/MainScreen.h"
#include "UI/MiscScreens.h"
#include "UI/MemStickScreen.h"
#include "UI/MiscViews.h"

void HandleCommonMessages(UIMessage message, const char *value, ScreenManager *manager, const Screen *activeScreen) {
	bool isActiveScreen = manager->topScreen() == activeScreen;

	if (message == UIMessage::REQUEST_CLEAR_JIT && PSP_IsInited()) {
		// TODO: This seems to clearly be the wrong place to handle this.
		if (MIPSComp::jit) {
			std::lock_guard<std::recursive_mutex> guard(MIPSComp::jitLock);
			if (MIPSComp::jit)
				MIPSComp::jit->ClearCache();
		}
		currentMIPS->UpdateCore((CPUCore)g_Config.iCpuCore);
	} else if (message == UIMessage::SHOW_CONTROL_MAPPING && isActiveScreen && std::string(activeScreen->tag()) != "ControlMapping") {
		UpdateUIState(UISTATE_MENU);
		manager->push(new ControlMappingScreen(Path()));
	} else if (message == UIMessage::SHOW_DISPLAY_LAYOUT_EDITOR && isActiveScreen && std::string(activeScreen->tag()) != "DisplayLayout") {
		UpdateUIState(UISTATE_MENU);
		manager->push(new DisplayLayoutScreen(Path()));
	} else if (message == UIMessage::SHOW_SETTINGS && isActiveScreen && std::string(activeScreen->tag()) != "GameSettings") {
		UpdateUIState(UISTATE_MENU);
		manager->push(new GameSettingsScreen(Path()));
	} else if (message == UIMessage::SHOW_LANGUAGE_SCREEN && isActiveScreen) {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		auto langScreen = new NewLanguageScreen(sy->T("Language"));
		langScreen->OnChoice.Add([](UI::EventParams &) {
			System_PostUIMessage(UIMessage::RECREATE_VIEWS);
			System_Notify(SystemNotification::UI);
		});
		manager->push(langScreen);
	} else if (message == UIMessage::WINDOW_MINIMIZED) {
		if (!strcmp(value, "true")) {
			gstate_c.skipDrawReason |= SKIPDRAW_WINDOW_MINIMIZED;
		} else {
			gstate_c.skipDrawReason &= ~SKIPDRAW_WINDOW_MINIMIZED;
		}
	}
}

ScreenRenderFlags BackgroundScreen::render(ScreenRenderMode mode) {
	UIContext *uiContext = screenManager()->getUIContext();

	uiContext->PushTransform({ translation_, scale_, alpha_ });

	uiContext->Begin();
	Lin::Vec3 focus;
	screenManager()->getFocusPosition(focus.x, focus.y, focus.z);

	if (!gamePath_.empty()) {
		::DrawGameBackground(*uiContext, gamePath_, focus, 1.0f);
	} else {
		::DrawBackground(*uiContext, 1.0f, focus);
	}

	uiContext->Flush();

	uiContext->PopTransform();

	return ScreenRenderFlags::NONE;
}

void BackgroundScreen::sendMessage(UIMessage message, const char *value) {
	switch (message) {
	case UIMessage::GAME_SELECTED:
		if (value && strlen(value)) {
			gamePath_ = Path(value);
		} else {
			gamePath_.clear();
		}
		break;
	default:
		break;
	}
}

void UIBaseDialogScreen::sendMessage(UIMessage message, const char *value) {
	if (message == UIMessage::SHOW_SETTINGS && screenManager()->topScreen() == this) {
		screenManager()->push(new GameSettingsScreen(gamePath_));
	} else {
		HandleCommonMessages(message, value, screenManager(), this);
	}
}

void UIBaseScreen::sendMessage(UIMessage message, const char *value) {
	HandleCommonMessages(message, value, screenManager(), this);
}

void UIBaseDialogScreen::AddStandardBack(UI::ViewGroup *parent) {
	using namespace UI;
	auto di = GetI18NCategory(I18NCat::DIALOG);
	parent->Add(new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK"), new AnchorLayoutParams(190, WRAP_CONTENT, 10, NONE, NONE, 10)))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
}

PromptScreen::PromptScreen(const Path &gamePath, std::string_view message, std::string_view yesButtonText, std::string_view noButtonText, std::function<void(bool)> callback)
	: UIBaseDialogScreen(gamePath), message_(message), callback_(callback) {
	yesButtonText_ = yesButtonText;
	noButtonText_ = noButtonText;
}

void PromptScreen::CreateViews() {
	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	root_ = new AnchorLayout();
	ViewGroup *rightColumnItems;

	if (!portrait) {
		// Horizontal layout.
		root_->Add(new TextView(message_, ALIGN_LEFT | FLAG_WRAP_TEXT, false, new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 15, 105, 330, 10)))->SetClip(false);
		rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(300, WRAP_CONTENT, NONE, 105, 15, NONE));
		root_->Add(rightColumnItems);
	} else {
		// Vertical layout
		root_->Add(new TextView(message_, ALIGN_LEFT | FLAG_WRAP_TEXT, false, new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 15, 15, 55, NONE)))->SetClip(false);
		// Leave space for the version at the bottom.
		rightColumnItems = new LinearLayout(ORIENT_HORIZONTAL, new AnchorLayoutParams(FILL_PARENT, WRAP_CONTENT, 15, NONE, 15, 65));
		root_->Add(rightColumnItems);
	}

	Choice *yesButton = new Choice(yesButtonText_, portrait ? new LinearLayoutParams(1.0f) : nullptr);
	yesButton->SetCentered(portrait);
	yesButton->OnClick.Add([this](UI::EventParams &e) {
		TriggerFinish(DR_OK);
	});
	Choice *noButton = nullptr;
	if (!noButtonText_.empty()) {
		noButton = new Choice(noButtonText_, portrait ? new LinearLayoutParams(1.0f) : nullptr);
		noButton->SetCentered(portrait);
		noButton->OnClick.Add([this](UI::EventParams &e) {
			TriggerFinish(DR_CANCEL);
		});
	}

	// The order of the button depends on the platform, if vertical layout is used.
	// Following UI standards here.
	if (portrait) {
#if PPSSPP_PLATFORM(WINDOWS)
		// On Windows, we put the yes button on the left.
		rightColumnItems->Add(yesButton);
		if (noButton) {
			rightColumnItems->Add(noButton);
		}
#else
		// On other platforms, we put the yes button on the right.
		if (noButton) {
			rightColumnItems->Add(noButton);
		}
		rightColumnItems->Add(yesButton);
#endif
	} else {
		// In horizontal layout, the buttons are placed vertically, we always put the yes button on top.
		rightColumnItems->Add(yesButton);
		if (noButton) {
			rightColumnItems->Add(noButton);
		}
	}

	if (!noButton) {
		// This is an information screen, not a question.
		// Sneak in the version of PPSSPP in the bottom left corner, for debug-reporting user screenshots.
		std::string version = System_GetProperty(SYSPROP_BUILD_VERSION);
		root_->Add(new TextView(version, 0, true, new AnchorLayoutParams(10.0f, NONE, NONE, 10.0f)));
	}
	root_->SetDefaultFocusView(noButton ? noButton : yesButton);
}

void PromptScreen::TriggerFinish(DialogResult result) {
	if (callback_) {
		callback_(result == DR_OK || result == DR_YES);
	}
	UIBaseDialogScreen::TriggerFinish(result);
}

TextureShaderScreen::TextureShaderScreen(std::string_view title) : ListPopupScreen(title) {}

void TextureShaderScreen::CreateViews() {
	auto ps = GetI18NCategory(I18NCat::TEXTURESHADERS);
	ReloadAllPostShaderInfo(screenManager()->getDrawContext());
	shaders_ = GetAllTextureShaderInfo();
	std::vector<std::string> items;
	int selected = -1;
	for (int i = 0; i < (int)shaders_.size(); i++) {
		if (shaders_[i].section == g_Config.sTextureShaderName)
			selected = i;
		items.emplace_back(ps->T(shaders_[i].section, shaders_[i].name));
	}
	adaptor_ = UI::StringVectorListAdaptor(items, selected);

	ListPopupScreen::CreateViews();
}

void TextureShaderScreen::OnCompleted(DialogResult result) {
	if (result != DR_OK)
		return;
	g_Config.sTextureShaderName = shaders_[listView_->GetSelected()].section;
}

NewLanguageScreen::NewLanguageScreen(std::string_view title) : ListPopupScreen(title) {
	// Disable annoying encoding warning
#ifdef _MSC_VER
#pragma warning(disable:4566)
#endif
	auto &langValuesMapping = GetLangValuesMapping();

	std::vector<File::FileInfo> tempLangs;
	g_VFS.GetFileListing("lang", &tempLangs, "ini");
	std::vector<std::string> listing;
	int selected = -1;
	int counter = 0;
	for (size_t i = 0; i < tempLangs.size(); i++) {
		// Skip README
		if (tempLangs[i].name.find("README") != std::string::npos) {
			continue;
		}

		// We only support Arabic on platforms where we have support for the native text rendering
		// APIs, as proper Arabic support is way too difficult to implement ourselves.
#if !(defined(USING_QT_UI) || PPSSPP_PLATFORM(WINDOWS) || PPSSPP_PLATFORM(ANDROID))
		if (tempLangs[i].name.find("ar_AE") != std::string::npos) {
			continue;
		}

		if (tempLangs[i].name.find("fa_IR") != std::string::npos) {
			continue;
		}
#endif

		const File::FileInfo &lang = tempLangs[i];
		langs_.push_back(lang);

		std::string code;
		size_t dot = lang.name.find('.');
		if (dot != std::string::npos)
			code = lang.name.substr(0, dot);

		std::string buttonTitle = lang.name;

		if (!code.empty()) {
			auto iter = langValuesMapping.find(code);
			if (iter == langValuesMapping.end()) {
				// No title found, show locale code
				buttonTitle = code;
			} else {
				buttonTitle = iter->second.first;
			}
		}
		if (g_Config.sLanguageIni == code)
			selected = counter;
		listing.push_back(buttonTitle);
		counter++;
	}

	adaptor_ = UI::StringVectorListAdaptor(listing, selected);
}

void NewLanguageScreen::OnCompleted(DialogResult result) {
	if (result != DR_OK)
		return;
	std::string oldLang = g_Config.sLanguageIni;
	std::string iniFile = langs_[listView_->GetSelected()].name;

	std::string_view code, part2;
	if (!SplitStringOnce(iniFile, &code, &part2, '.')) {
		return;
	}

	g_Config.sLanguageIni = code;

	bool iniLoadedSuccessfully = false;
	// Allow the lang directory to be overridden for testing purposes (e.g. Android, where it's hard to
	// test new languages without recompiling the entire app, which is a hassle).
	const Path langOverridePath = GetSysDirectory(DIRECTORY_SYSTEM) / "lang";

	// If we run into the unlikely case that "lang" is actually a file, just use the built-in translations.
	if (!File::Exists(langOverridePath) || !File::IsDirectory(langOverridePath))
		iniLoadedSuccessfully = g_i18nrepo.LoadIni(g_Config.sLanguageIni);
	else
		iniLoadedSuccessfully = g_i18nrepo.LoadIni(g_Config.sLanguageIni, langOverridePath);

	if (iniLoadedSuccessfully) {
		RecreateViews();
		System_Notify(SystemNotification::UI);
	} else {
		// Failed to load the language ini. Shouldn't really happen, but let's just switch back to the old language.
		g_Config.sLanguageIni = oldLang;
	}
}

void LogoScreen::Next() {
	if (!switched_) {
		switched_ = true;
		Path gamePath = boot_filename;

		switch (afterLogoScreen_) {
		case AfterLogoScreen::TO_GAME_SETTINGS:
			if (!gamePath.empty()) {
				screenManager()->switchScreen(new EmuScreen(gamePath));
			} else {
				screenManager()->switchScreen(new MainScreen());
			}
			screenManager()->push(new GameSettingsScreen(gamePath));
			break;
		case AfterLogoScreen::MEMSTICK_SCREEN_INITIAL_SETUP:
			screenManager()->switchScreen(new MemStickScreen(true));
			break;
		case AfterLogoScreen::DEFAULT:
		default:
			if (boot_filename.size()) {
				screenManager()->switchScreen(new EmuScreen(gamePath));
			} else {
				screenManager()->switchScreen(new MainScreen());
			}
			break;
		}
	}
}

const float logoScreenSeconds = 2.5f;

LogoScreen::LogoScreen(AfterLogoScreen afterLogoScreen)
	: afterLogoScreen_(afterLogoScreen) {
}

void LogoScreen::update() {
	UIScreen::update();
	double rate = std::max(30.0, (double)System_GetPropertyFloat(SYSPROP_DISPLAY_REFRESH_RATE));

	if ((double)frames_ / rate > logoScreenSeconds) {
		Next();
	}
	frames_++;
	sinceStart_ = (double)frames_ / rate;
}

void LogoScreen::sendMessage(UIMessage message, const char *value) {
	if (message == UIMessage::REQUEST_GAME_BOOT && screenManager()->topScreen() == this) {
		screenManager()->switchScreen(new EmuScreen(Path(value)));
	}
}

bool LogoScreen::key(const KeyInput &key) {
	if (key.deviceId != DEVICE_ID_MOUSE && (key.flags & KeyInputFlags::DOWN)) {
		Next();
		return true;
	}
	return false;
}

void LogoScreen::touch(const TouchInput &touch) {
	if (touch.flags & TouchInputFlags::DOWN) {
		Next();
	}
}

void LogoScreen::DrawForeground(UIContext &dc) {
	using namespace Draw;

	const Bounds &bounds = dc.GetLayoutBounds();

	dc.Begin();

	float t = (float)sinceStart_ / (logoScreenSeconds / 3.0f);

	float alpha = t;
	if (t > 1.0f)
		alpha = 1.0f;
	float alphaText = alpha;
	if (t > 2.0f)
		alphaText = 3.0f - t;
	uint32_t textColor = colorAlpha(dc.GetTheme().infoStyle.fgColor, alphaText);

	auto cr = GetI18NCategory(I18NCat::PSPCREDITS);
	auto gr = GetI18NCategory(I18NCat::GRAPHICS);
	char temp[256];

	const float startY = bounds.centerY() - 70;

	// Manually formatting UTF-8 is fun.  \xXX doesn't work everywhere.
	snprintf(temp, sizeof(temp), "%s Henrik Rydg%c%crd", cr->T_cstr("created", "Created by"), 0xC3, 0xA5);
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		UI::DrawIconShine(dc, Bounds::FromCenter(bounds.centerX() - 125, startY, 60.0f), 0.7f, true);
		dc.Draw()->DrawImage(ImageID("I_ICON_GOLD"), bounds.centerX() - 125, startY, 1.2f, 0xFFFFFFFF, ALIGN_CENTER);
	} else {
		dc.Draw()->DrawImage(ImageID("I_ICON"), bounds.centerX() - 125, startY, 1.2f, 0xFFFFFFFF, ALIGN_CENTER);
	}
	dc.Draw()->DrawImage(ImageID("I_LOGO"), bounds.centerX() + 45, startY, 1.5f, 0xFFFFFFFF, ALIGN_CENTER);
	//dc.Draw()->DrawTextShadow(UBUNTU48, "PPSSPP", bounds.w / 2, bounds.h / 2 - 30, textColor, ALIGN_CENTER);
	dc.SetFontScale(1.0f, 1.0f);
	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.DrawText(temp, bounds.centerX(), startY + 70, textColor, ALIGN_CENTER);
	dc.DrawText(cr->T_cstr("license", "Free Software under GPL 2.0+"), bounds.centerX(), startY + 110, textColor, ALIGN_CENTER);

	dc.DrawText("www.ppsspp.org", bounds.centerX(), startY + 160, textColor, ALIGN_CENTER);

#if !PPSSPP_PLATFORM(UWP) || defined(_DEBUG)
	// Draw the graphics API, except on UWP where it's always D3D11
	std::string apiName(gr->T(screenManager()->getDrawContext()->GetInfoString(InfoField::APINAME)));
#ifdef _DEBUG
	apiName += ", debug build ";
	// Add some emoji for testing.
	apiName += CodepointToUTF8(0x1F41B) + CodepointToUTF8(0x1F41C) + CodepointToUTF8(0x1F914);
#endif
	dc.DrawText(apiName, bounds.centerX(), startY + 200, textColor, ALIGN_CENTER);
#endif

	dc.Flush();
}

class CreditsScroller : public UI::View {
public:
	CreditsScroller(UI::LayoutParams *layoutParams) : UI::View(layoutParams) {}
	bool Touch(const TouchInput &touch) override {
		if (touch.id != 0)
			return false;
		if (touch.flags & TouchInputFlags::DOWN) {
			dragYStart_ = touch.y;
			dragYOffsetStart_ = dragOffset_;
		}
		if (touch.flags & TouchInputFlags::UP) {
			dragYStart_ = -1.0f;
		}
		if (touch.flags & TouchInputFlags::MOVE) {
			if (dragYStart_ >= 0.0f) {
				dragOffset_ = dragYOffsetStart_ + (touch.y - dragYStart_);
			}
		}
		return true;
	}
	void Draw(UIContext &dc) override;
private:
	Instant startTime_ = Instant::Now();
	double dragYStart_ = -1.0;
	double dragOffset_ = 0.0;
	double dragYOffsetStart_ = 0.0;
};

std::string_view CreditsScreen::GetTitle() const {
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	return mm->T("About PPSSPP");
}

void CreditsScreen::CreateDialogViews(UI::ViewGroup *parent) {
	using namespace UI;

	ignoreBottomInset_ = false;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto cr = GetI18NCategory(I18NCat::PSPCREDITS);
	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	const bool gold = System_GetPropertyBool(SYSPROP_APP_GOLD);

	/*
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		root_->Add(new ShinyIcon(ImageID("I_ICON_GOLD"), new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 10, 10, NONE, NONE, false)))->SetScale(1.5f);
	} else {
		root_->Add(new ImageView(ImageID("I_ICON"), "", IS_DEFAULT, new AnchorLayoutParams(WRAP_CONTENT, WRAP_CONTENT, 10, 10, NONE, NONE, false)))->SetScale(1.5f);
	}*/

	constexpr float columnWidth = 265.0f;

	LinearLayout *left;
	LinearLayout *right;
	if (portrait) {
		parent->Add(new CreditsScroller(new LinearLayoutParams(1.0f)));

		LinearLayout *columns = parent->Add(new LinearLayout(ORIENT_HORIZONTAL));

		left = columns->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(columnWidth, WRAP_CONTENT, Margins(10))));
		columns->Add(new Spacer(ORIENT_VERTICAL, new LinearLayoutParams(1.0f)));
		right = columns->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(columnWidth, WRAP_CONTENT, Margins(10))));
	} else {
		LinearLayout *columns = parent->Add(new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(1.0f)));

		left = columns->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(columnWidth, FILL_PARENT, Margins(10))));
		left->Add(new Spacer(0.0f, new LinearLayoutParams(1.0f)));
		columns->Add(new CreditsScroller(new LinearLayoutParams(WRAP_CONTENT, FILL_PARENT, 1.0f)));
		right = columns->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(columnWidth, FILL_PARENT, Margins(10))));
		right->Add(new Spacer(0.0f, new LinearLayoutParams(1.0f)));
	}

	int rightYOffset = 0;
	if (!System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		ScreenManager *sm = screenManager();
		Choice *gold = new Choice(mm->T("Buy PPSSPP Gold"));
		gold->SetIconRight(ImageID("I_ICON_GOLD"), 0.5f);
		gold->SetImageScale(0.6f);  // for the left-icon in case of vertical.
		gold->SetShine(true);

		left->Add(gold)->OnClick.Add([sm](UI::EventParams) {
			LaunchBuyGold(sm);
		});
		rightYOffset = 74;
	}
	left->Add(new Choice(cr->T("PPSSPP Forums"), ImageID("I_LINK_OUT")))->OnClick.Add([](UI::EventParams &e) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://forums.ppsspp.org");
	});
	left->Add(new Choice(cr->T("Discord"), ImageID("I_LOGO_DISCORD")))->OnClick.Add([](UI::EventParams &e) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://discord.gg/5NJB6dD");
	});
	left->Add(new Choice("www.ppsspp.org", ImageID("I_LINK_OUT")))->OnClick.Add([](UI::EventParams &e) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org");
	});
	right->Add(new Choice(cr->T("Privacy Policy"), ImageID("I_LINK_OUT")))->OnClick.Add([](UI::EventParams &e) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/privacy");
	});
	right->Add(new Choice(cr->T("@PPSSPP_emu"), ImageID("I_LOGO_X")))->OnClick.Add([](UI::EventParams &e) {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://x.com/PPSSPP_emu");
	});

	if (System_GetPropertyBool(SYSPROP_SUPPORTS_SHARE_TEXT)) {
		right->Add(new Choice(cr->T("Share PPSSPP"), ImageID("I_SHARE")))->OnClick.Add([](UI::EventParams &e) {
			auto cr = GetI18NCategory(I18NCat::PSPCREDITS);
			System_ShareText(cr->T("CheckOutPPSSPP", "Check out PPSSPP, the awesome PSP emulator: https://www.ppsspp.org/"));
		});
	}
}

void CreditsScreen::update() {
	UIScreen::update();
	UpdateUIState(UISTATE_MENU);
}

void CreditsScroller::Draw(UIContext &dc) {
	auto cr = GetI18NCategory(I18NCat::PSPCREDITS);

	std::string specialthanksMaxim = "Maxim ";
	specialthanksMaxim += cr->T("specialthanksMaxim", "for his amazing Atrac3+ decoder work");

	std::string specialthanksKeithGalocy = "Keith Galocy ";
	specialthanksKeithGalocy += cr->T("specialthanksKeithGalocy", "at NVIDIA (hardware, advice)");

	std::string specialthanksOrphis = "Orphis (";
	specialthanksOrphis += cr->T("build server");
	specialthanksOrphis += ')';

	std::string specialthanksangelxwind = "angelxwind (";
	specialthanksangelxwind += cr->T("iOS builds");
	specialthanksangelxwind += ')';

	std::string specialthanksW_MS = "W.MS (";
	specialthanksW_MS += cr->T("iOS builds");
	specialthanksW_MS += ')';

	std::string specialthankssolarmystic = "solarmystic (";
	specialthankssolarmystic += cr->T("testing");
	specialthankssolarmystic += ')';

	std::string_view credits[] = {
		System_GetPropertyBool(SYSPROP_APP_GOLD) ? "PPSSPP Gold" : "PPSSPP",
		"",
		cr->T("title", "A fast and portable PSP emulator"),
		"",
		"",
		cr->T("created", "Created by"),
		"Henrik Rydg\xc3\xa5rd",
		"",
		"",
		cr->T("contributors", "Contributors:"),
		"unknownbrackets",
		"oioitff",
		"xsacha",
		"raven02",
		"tpunix",
		"orphis",
		"sum2012",
		"mikusp",
		"aquanull",
		"The Dax",
		"bollu",
		"tmaul",
		"artart78",
		"ced2911",
		"soywiz",
		"kovensky",
		"xele",
		"chaserhjk",
		"evilcorn",
		"daniel dressler",
		"makotech222",
		"CPkmn",
		"mgaver",
		"jeid3",
		"cinaera/BeaR",
		"jtraynham",
		"Kingcom",
		"arnastia",
		"lioncash",
		"JulianoAmaralChaves",
		"vnctdj",
		"kaienfr",
		"shenweip",
		"Danyal Zia",
		"Igor Calabria",
		"Coldbird",
		"Kyhel",
		"xebra",
		"LunaMoo",
		"zminhquanz",
		"ANR2ME",
		"adenovan",
		"iota97",
		"Lubos",
		"stenzek",  // For retroachievements integration
		"fp64",
		"",
		cr->T("specialthanks", "Special thanks to:"),
		specialthanksMaxim,
		specialthanksKeithGalocy,
		specialthanksOrphis,
		specialthanksangelxwind,
		specialthanksW_MS,
		specialthankssolarmystic,
		cr->T("all the forum mods"),
		"",
		cr->T("this translation by", ""),   // Empty string as this is the original :)
		cr->T("translators1", ""),
		cr->T("translators2", ""),
		cr->T("translators3", ""),
		cr->T("translators4", ""),
		cr->T("translators5", ""),
		cr->T("translators6", ""),
		"",
		cr->T("written", "Written in C++ for speed and portability"),
		"",
		"",
		cr->T("tools", "Free tools used:"),
#if PPSSPP_PLATFORM(ANDROID)
		"Android SDK + NDK",
#endif
#if defined(USING_QT_UI)
		"Qt",
#endif
#if defined(SDL)
		"SDL",
#endif
		"CMake",
		"freetype2",
		"zlib",
		"rcheevos",
		"SPIRV-Cross",
		"armips",
		"Basis Universal",
		"cityhash",
		"zstd",
		"glew",
		"libchdr",
		"minimp3",
		"xxhash",
		"naett-http",
		"PSP SDK",
		"",
		"",
		cr->T("website", "Check out the website:"),
		"www.ppsspp.org",
		cr->T("list", "compatibility lists, forums, and development info"),
		"",
		"",
		cr->T("check", "Also check out Dolphin, the best Wii/GC emu around:"),
		"https://www.dolphin-emu.org",
		"",
		"",
		cr->T("info1", "PPSSPP is only intended to play games you own."),
		cr->T("info2", "Please make sure that you own the rights to any games"),
		cr->T("info3", "you play by owning the UMD or by buying the digital"),
		cr->T("info4", "download from the PSN store on your real PSP."),
		"",
		"",
		cr->T("info5", "PSP is a trademark by Sony, Inc."),
	};

	// TODO: This is kinda ugly, done on every frame...
	char temp[256];
	if (System_GetPropertyBool(SYSPROP_APP_GOLD)) {
		snprintf(temp, sizeof(temp), "PPSSPP Gold %s", PPSSPP_GIT_VERSION);
	} else {
		snprintf(temp, sizeof(temp), "PPSSPP %s", PPSSPP_GIT_VERSION);
	}
	credits[0] = (const char *)temp;

	dc.Begin();

	const Bounds &bounds = bounds_;
	bounds.Inset(10.f, 10.f);
	const int numItems = ARRAY_SIZE(credits);
	int itemHeight = 36;
	int contentsHeight = numItems * itemHeight + bounds.h + 200;

	const float t = (float)(startTime_.ElapsedSeconds() * 60.0);

	const float yOffset = fmodf(t - dragOffset_, (float)contentsHeight);

	float y = bounds.h - yOffset;
	for (int i = 0; i < numItems; i++, y += itemHeight) {
		if (y + itemHeight < 0.0f)
			continue;
		if (y > bounds.h)
			continue;
		float fadeLength = 64.0f;
		float alpha = linearInOut(y, 64, bounds.h - fadeLength * 2.0f, 64);
		uint32_t textColor = colorAlpha(dc.GetTheme().infoStyle.fgColor, alpha);

		if (alpha > 0.0f) {
			dc.SetFontScale(ease(alpha), ease(alpha));
			dc.DrawText(credits[i], bounds.centerX(), y + bounds.y, textColor, ALIGN_HCENTER);
			dc.SetFontScale(1.0f, 1.0f);
		}
	}

	dc.Flush();
}
