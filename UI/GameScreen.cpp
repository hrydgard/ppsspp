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

#include <algorithm>

#include "ppsspp_config.h"

#include "Common/Render/DrawBuffer.h"
#include "Common/UI/Context.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/Notice.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/System/NativeApp.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "Core/Loaders.h"
#include "Core/HLE/Plugins.h"
#include "Core/Util/GameDB.h"
#include "Core/Util/RecentFiles.h"
#include "Core/Util/PathUtil.h"
#include "UI/OnScreenDisplay.h"
#include "UI/Background.h"
#include "UI/CwCheatScreen.h"
#include "UI/EmuScreen.h"
#include "UI/GameScreen.h"
#include "UI/GameSettingsScreen.h"
#include "UI/GameInfoCache.h"
#include "UI/BaseScreens.h"
#include "UI/MiscScreens.h"
#include "UI/MainScreen.h"
#include "UI/BackgroundAudio.h"
#include "UI/SavedataScreen.h"
#include "UI/MiscViews.h"

constexpr GameInfoFlags g_desiredFlags = GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON | GameInfoFlags::PIC0 | GameInfoFlags::PIC1 | GameInfoFlags::UNCOMPRESSED_SIZE | GameInfoFlags::SIZE;

GameScreen::GameScreen(const Path &gamePath, bool inGame) : UITwoPaneBaseDialogScreen(gamePath, TwoPaneFlags::SettingsToTheRight | TwoPaneFlags::CustomContextMenu), inGame_(inGame) {
	g_BackgroundAudio.SetGame(gamePath);
	System_PostUIMessage(UIMessage::GAME_SELECTED, gamePath.ToString());

	info_ = g_gameInfoCache->GetInfo(NULL, gamePath_, g_desiredFlags, &knownFlags_);
}

GameScreen::~GameScreen() {
	if (CRC32string == "...") {
		Reporting::CancelCRC();
	}
	System_PostUIMessage(UIMessage::GAME_SELECTED, "");
}

template <typename I> std::string int2hexstr(I w, size_t hex_len = sizeof(I) << 1) {
	static const char* digits = "0123456789ABCDEF";
	std::string rc(hex_len, '0');
	for (size_t i = 0, j = (hex_len - 1) * 4; i < hex_len; ++i, j -= 4)
		rc[i] = digits[(w >> j) & 0x0f];
	return rc;
}

void GameScreen::update() {
	UIScreen::update();

	GameInfoFlags hasFlags;
	g_gameInfoCache->GetInfo(NULL, gamePath_, g_desiredFlags, &hasFlags);

	bool recreate = false;

	if (knownFlags_ != hasFlags) {
		knownFlags_ = hasFlags;
		recreate = true;
	}

	// Has the user requested a CRC32?
	if (CRC32string == "...") {
		// Wait until the CRC32 is ready.  It might take time on some devices.
		const bool hasCRC = Reporting::HasCRC(gamePath_);
		if (hasCRC != knownHasCRC_) {
			knownHasCRC_ = hasCRC;
			recreate = true;
		}
	}

	if (recreate) {
		RecreateViews();
	}
}

static bool FileTypeSupportsCRC(IdentifiedFileType fileType) {
	switch (fileType) {
	case IdentifiedFileType::PSP_PBP:
	case IdentifiedFileType::PSP_PBP_DIRECTORY:
	case IdentifiedFileType::PSP_ISO_NP:
	case IdentifiedFileType::PSP_ISO:
		return true;
	default:
		return false;
	}
}

static bool FileTypeHasIcon(IdentifiedFileType fileType) {
	switch (fileType) {
	case IdentifiedFileType::PSP_PBP:
	case IdentifiedFileType::PSP_PBP_DIRECTORY:
	case IdentifiedFileType::PSP_ISO_NP:
	case IdentifiedFileType::PSP_ISO:
	case IdentifiedFileType::PSP_UMD_VIDEO_ISO:
		return true;
	default:
		return false;
	}
}

static bool FileTypeIsPlayable(IdentifiedFileType fileType) {
	switch (fileType) {
	case IdentifiedFileType::ERROR_IDENTIFYING:
	case IdentifiedFileType::UNKNOWN:
	case IdentifiedFileType::PSX_ISO:
	case IdentifiedFileType::PS2_ISO:
	case IdentifiedFileType::PS3_ISO:
	case IdentifiedFileType::UNKNOWN_BIN:
	case IdentifiedFileType::UNKNOWN_ELF:
	case IdentifiedFileType::UNKNOWN_ISO:
	case IdentifiedFileType::NORMAL_DIRECTORY:
	case IdentifiedFileType::PSP_SAVEDATA_DIRECTORY:
	case IdentifiedFileType::PSP_UMD_VIDEO_ISO:
		// Reverse logic.
		return false;
	default:
		return true;
	}
}

void GameScreen::CreateContentViews(UI::ViewGroup *parent) {
	if (!info_) {
		// Shouldn't happen
		return;
	}

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ga = GetI18NCategory(I18NCat::GAME);

	// Information in the top left.
	// Back button to the bottom left.
	// Scrolling action menu to the right.
	using namespace UI;

	Margins actionMenuMargins(0, 15, 15, 0);

	ScrollView *leftScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f, Margins(8)));

	ViewGroup *leftColumn = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT));

	leftScroll->Add(leftColumn);

	parent->Add(leftScroll);

	const bool fileTypeSupportCRC = FileTypeSupportsCRC(info_->fileType);
	const bool fileTypeHasIcon = FileTypeHasIcon(info_->fileType);

	// Need an explicit size here because homebrew uses screenshots as icons.
	LinearLayout *mainGameInfo;
	if (portrait) {
		mainGameInfo = new LinearLayout(ORIENT_VERTICAL);
		leftColumn->Add(new Spacer(8.0f));
		if (fileTypeHasIcon) {
			leftColumn->Add(new GameImageView(gamePath_, GameInfoFlags::ICON, 2.0f, new LinearLayoutParams(UI::Margins(0))));
		}
		leftColumn->Add(mainGameInfo);
	} else {
		mainGameInfo = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
		ViewGroup *badgeHolder = new LinearLayout(ORIENT_HORIZONTAL);
		if (fileTypeHasIcon) {
			badgeHolder->Add(new GameImageView(gamePath_, GameInfoFlags::ICON, 2.0f, new LinearLayoutParams(144 * 2, 80 * 2, UI::Margins(0))));
		}
		badgeHolder->Add(mainGameInfo);
		leftColumn->Add(badgeHolder);
	}
	mainGameInfo->SetSpacing(3.0f);

	GameDBInfo dbInfo;
	std::vector<GameDBInfo> dbInfos;
	const bool inGameDB = g_gameDB.GetGameInfos(info_->id_version, &dbInfos);

	if (knownFlags_ & GameInfoFlags::PARAM_SFO) {
		std::string regionID = ReplaceAll(info_->id_version, "_", " v");
		if (!regionID.empty()) {
			regionID += ": ";

			// Show the game ID title below the icon. The top title will be from the DB.
			std::string title = info_->GetTitle();

			TextView *tvTitle = mainGameInfo->Add(new TextView(title, ALIGN_LEFT | FLAG_WRAP_TEXT, false, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
			tvTitle->SetShadow(true);
		}

		if (info_->region != GameRegion::UNKNOWN) {
			regionID += GameRegionToString(info_->region);
		} else {
			regionID += IdentifiedFileTypeToString(info_->fileType);
		}

		TextView *tvID = mainGameInfo->Add(new TextView(regionID, ALIGN_LEFT | FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		tvID->SetShadow(true);

		if (!info_->errorString.empty()) {
			mainGameInfo->Add(new NoticeView(NoticeLevel::WARN, info_->errorString, ""));
		}
	}

	LinearLayout *infoLayout = new LinearLayout(ORIENT_VERTICAL, new AnchorLayoutParams(10, 200, NONE, NONE));
	leftColumn->Add(infoLayout);

	if (info_->fileType == IdentifiedFileType::PSP_UMD_VIDEO_ISO) {
		auto er = GetI18NCategory(I18NCat::ERRORS);
		leftColumn->Add(new NoticeView(NoticeLevel::INFO, er->T("PPSSPP doesn't support UMD Video."), ""));
		leftColumn->Add(new Choice(di->T("More info"), ImageID("I_LINK_OUT_QUESTION"), new LinearLayoutParams(WRAP_CONTENT, WRAP_CONTENT)))->OnClick.Add([](UI::EventParams &e) {
			System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/docs/reference/umd-video/");
		});
	}

	if ((knownFlags_ & GameInfoFlags::UNCOMPRESSED_SIZE) && (knownFlags_ & GameInfoFlags::SIZE)) {
		auto st = GetI18NCategory(I18NCat::STORE);  // Borrow the size string from here
		char temp[256];
		snprintf(temp, sizeof(temp), "%s: %s", st->T_cstr("Size"), NiceSizeFormat(info_->gameSizeOnDisk).c_str());
		if (info_->gameSizeUncompressed != info_->gameSizeOnDisk) {
			size_t len = strlen(temp);
			snprintf(temp + len, sizeof(temp) - len, " (%s: %s)", ga->T_cstr("Uncompressed"), NiceSizeFormat(info_->gameSizeUncompressed).c_str());
		}
		TextView *tvGameSize = mainGameInfo->Add(new TextView(temp, ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
		tvGameSize->SetShadow(true);

		if (info_->saveDataSize > 0) {
			snprintf(temp, sizeof(temp), "%s: %s", ga->T_cstr("SaveData"), NiceSizeFormat(info_->saveDataSize).c_str());
			TextView *tvSaveDataSize = infoLayout->Add(new TextView(temp, ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
			tvSaveDataSize->SetShadow(true);
		}
		if (info_->installDataSize > 0) {
			snprintf(temp, sizeof(temp), "%s: %1.2f %s", ga->T_cstr("InstallData"), (float)(info_->installDataSize) / 1024.f / 1024.f, ga->T_cstr("MB"));
			TextView *tvInstallDataSize = infoLayout->Add(new TextView(temp, ALIGN_LEFT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
			tvInstallDataSize->SetShadow(true);
		}
	}

	infoLayout->Add(new TextView(GetFriendlyPath(gamePath_), ALIGN_LEFT | FLAG_WRAP_TEXT, true, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)))->SetShadow(true);

	std::string timeStr;
	if (g_Config.TimeTracker().GetPlayedTimeString(info_->id, &timeStr)) {
		LinearLayout *timeHoriz = infoLayout->Add(new LinearLayout(ORIENT_HORIZONTAL));

		TextView *tvPlayTime = timeHoriz->Add(new TextView(timeStr, ALIGN_LEFT, true, new LinearLayoutParams(0.0f, Gravity::G_VCENTER)));
		tvPlayTime->SetShadow(true);
		tvPlayTime->SetText(timeStr);

		auto di = GetI18NCategory(I18NCat::DIALOG);
		Choice *btnResetTime = timeHoriz->Add(new Choice(di->T("Reset"), new LinearLayoutParams(0.0f, Gravity::G_VCENTER)));
		btnResetTime->OnClick.Add([this, ga, timeStr](UI::EventParams &) {
			auto di = GetI18NCategory(I18NCat::DIALOG);
			auto gta = GetI18NCategory(I18NCat::GAME);
			std::string id = info_->id;
			std::string questionText(ga->T("Are you sure you want to reset the played time counter?"));
			questionText += "\n";
			questionText += timeStr;
			screenManager()->push(
				new PromptScreen(gamePath_, questionText, di->T("Reset"), di->T("Cancel"), [id](bool yes) {
				if (yes) {
					g_Config.TimeTracker().Reset(id);
				}
			}));
			RecreateViews();
		});
	}

	LinearLayout *crcHoriz = infoLayout->Add(new LinearLayout(ORIENT_HORIZONTAL));

	if (fileTypeSupportCRC) {
		if (Reporting::HasCRC(gamePath_)) {
			auto rp = GetI18NCategory(I18NCat::REPORTING);
			uint32_t crcVal = Reporting::RetrieveCRC(gamePath_);
			std::string crc = StringFromFormat("%08X", crcVal);

			// CRC button makes sense.
			TextView *tvCRC = crcHoriz->Add(new TextView(ReplaceAll(rp->T("FeedbackCRCValue", "Disc CRC: %1"), "%1", crc), ALIGN_LEFT, true, new LinearLayoutParams(0.0, Gravity::G_VCENTER)));
			tvCRC->SetShadow(true);

			if (System_GetPropertyBool(SYSPROP_HAS_TEXT_CLIPBOARD)) {
				Choice *tvCRCCopy = crcHoriz->Add(new Choice(ImageID("I_FILE_COPY"), new LinearLayoutParams(0.0, Gravity::G_VCENTER)));
				tvCRCCopy->OnClick.Add([this](UI::EventParams &) {
					u32 crc = Reporting::RetrieveCRC(gamePath_);
					char buffer[16];
					snprintf(buffer, sizeof(buffer), "%08X", crc);
					System_CopyStringToClipboard(buffer);
					// Success indication. Not worth a translatable string.
					g_OSD.Show(OSDType::MESSAGE_SUCCESS, buffer, 1.0f);
				});
			}

			// Let's check the CRC in the game database, looking up the ID and also matching the crc.
			std::vector<GameDBInfo> dbInfos;
			if ((knownFlags_ & GameInfoFlags::PARAM_SFO) && g_gameDB.GetGameInfos(info_->id_version, &dbInfos)) {
				bool found = false;
				for (const auto &dbInfo : dbInfos) {
					if (dbInfo.crc == crcVal) {
						found = true;
					}
				}
				if (found) {
					NoticeView *tvVerified = infoLayout->Add(new NoticeView(NoticeLevel::INFO, ga->T("ISO OK according to the ReDump project"), "", new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
					tvVerified->SetLevel(NoticeLevel::SUCCESS);
				}
			}
		} else if (!isHomebrew_) {
			if ((knownFlags_ & GameInfoFlags::PARAM_SFO) && !inGameDB) {
				// tvVerified_->SetText(ga->T("Game ID unknown - not in the ReDump database"));
				// tvVerified_->SetVisibility(UI::V_VISIBLE);
				// tvVerified_->SetLevel(NoticeLevel::WARN);
			} else if ((knownFlags_ & GameInfoFlags::UNCOMPRESSED_SIZE) && info_->gameSizeUncompressed != 0) {  // don't do this check if info_ still pending
				bool found = false;
				for (auto &dbInfo : dbInfos) {
					// TODO: Doesn't take CSO/CHD into account.
					if (info_->gameSizeUncompressed == dbInfo.size) {
						found = true;
					}
				}
				if (!found) {
					// tvVerified_->SetText(ga->T("File size incorrect, bad or modified ISO"));
					// tvVerified_->SetVisibility(UI::V_VISIBLE);
					// tvVerified_->SetLevel(NoticeLevel::ERROR);
					// INFO_LOG(Log::Loader, "File size %d not matching game DB", (int)info_->gameSizeUncompressed);
				}

				NoticeView *tvVerified = infoLayout->Add(new NoticeView(NoticeLevel::INFO, ga->T("Click \"Calculate CRC\" to verify ISO"), "", new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
				tvVerified->SetVisibility(UI::V_VISIBLE);
				tvVerified->SetLevel(NoticeLevel::INFO);
			}
		}
	}

	NoticeView *tvVerified = infoLayout->Add(new NoticeView(NoticeLevel::INFO, ga->T("Click \"Calculate CRC\" to verify ISO"), "", new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	tvVerified->SetVisibility(UI::V_GONE);
	tvVerified->SetSquishy(true);

	// Show plugin info_, if any. Later might add checkboxes.
	auto plugins = HLEPlugins::FindPlugins(info_->id, g_Config.sLanguageIni);
	if (!plugins.empty()) {
		auto sy = GetI18NCategory(I18NCat::SYSTEM);
		infoLayout->Add(new ItemHeader(sy->T("Plugins")));
		for (const auto &plugin : plugins) {
			infoLayout->Add(new TextView(plugin.name, ALIGN_LEFT, true))->SetBullet(true);
		}
	}

	infoLayout->Add(new GameImageView(gamePath_, GameInfoFlags::PIC0, 2.0f, new LinearLayoutParams(UI::Margins(0))));
}

void GameScreen::CreateSettingsViews(UI::ViewGroup *rightColumn) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ga = GetI18NCategory(I18NCat::GAME);

	const bool fileTypeSupportCRC = FileTypeSupportsCRC(info_->fileType);
	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;

	LinearLayout *rightColumnItems = new LinearLayout(ORIENT_VERTICAL);
	rightColumnItems->SetSpacing(0.0f);
	rightColumn->Add(rightColumnItems);

	if (!inGame_ && FileTypeIsPlayable(info_->fileType)) {
		rightColumnItems->Add(new Choice(ga->T("Play"), ImageID("I_PLAY")))->OnClick.Handle(this, &GameScreen::OnPlay);
	}

	if (!info_->id.empty() && !inGame_) {
		if (info_->hasConfig) {
			// Only show the Game Settings button here if the game has a config. Always showing it
			// is confusing since it'll just control the global settings.
			Choice *btnGameSettings = rightColumnItems->Add(new Choice(ga->T("Game Settings"), ImageID("I_GEAR")));
			btnGameSettings->OnClick.Handle(this, &GameScreen::OnGameSettings);

			Choice *btnDeleteGameConfig = rightColumnItems->Add(new Choice(ga->T("Delete Game Config"), ImageID("I_TRASHCAN")));
			btnDeleteGameConfig->OnClick.Handle(this, &GameScreen::OnDeleteConfig);
		} else {
			Choice *btnCreateGameConfig = rightColumnItems->Add(new Choice(ga->T("Create Game Config"), ImageID("I_GEAR_STAR")));
			btnCreateGameConfig->OnClick.Handle(this, &GameScreen::OnCreateConfig);
		}
	}

	if (g_Config.bEnableCheats) {
		auto pa = GetI18NCategory(I18NCat::PAUSE);
		rightColumnItems->Add(new Choice(pa->T("Cheats"), ImageID("I_CHEAT")))->OnClick.Handle(this, &GameScreen::OnCwCheat);
	}

	isHomebrew_ = info_ && info_->region == GameRegion::HOMEBREW;

	if (fileTypeSupportCRC && !isHomebrew_ && !Reporting::HasCRC(gamePath_) ) {
		rightColumnItems->Add(new Choice(ga->T("Calculate CRC"), ImageID("I_CHECKMARK")))->OnClick.Add([this](UI::EventParams &) {
			Reporting::QueueCRC(gamePath_);
			CRC32string = "...";  // signal that we're waiting for it. Kinda ugly.
		});
	}
}

void GameScreen::CreateContextMenu(UI::ViewGroup *parent) {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ga = GetI18NCategory(I18NCat::GAME);

	if (System_GetPropertyBool(SYSPROP_CAN_SHOW_FILE)) {
		parent->Add(new Choice(di->T("Show in folder"), ImageID("I_FOLDER")))->OnClick.Add([this](UI::EventParams &e) {
			System_ShowFileInFolder(gamePath_);
		});
	}

	// TODO: This is synchronous, bad!
	if (!inGame_ && g_recentFiles.ContainsFile(gamePath_.ToString())) {
		Choice *removeButton = parent->Add(new Choice(ga->T("Remove From Recent")));
		removeButton->OnClick.Handle(this, &GameScreen::OnRemoveFromRecent);
	}

	if (info_->saveDataSize) {
		Choice *btnDeleteSaveData = new Choice(ga->T("Delete Save Data"), ImageID("I_TRASHCAN"));
		parent->Add(btnDeleteSaveData)->OnClick.Handle(this, &GameScreen::OnDeleteSaveData);
	}

	if (info_->pic1.texture) {
		Choice *btnSetBackground = parent->Add(new Choice(ga->T("Use background as UI background")));
		btnSetBackground->OnClick.Handle(this, &GameScreen::OnSetBackground);
	}

	if ((knownFlags_ & GameInfoFlags::PARAM_SFO) && System_GetPropertyBool(SYSPROP_CAN_CREATE_SHORTCUT)) {
		parent->Add(new Choice(ga->T("Create Shortcut")))->OnClick.Add([this](UI::EventParams &e) {
			GameInfoFlags hasFlags;
			std::shared_ptr<GameInfo> info_ = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO, &hasFlags);
			if (hasFlags & GameInfoFlags::PARAM_SFO) {
				System_CreateGameShortcut(gamePath_, info_->GetTitle());
				g_OSD.Show(OSDType::MESSAGE_SUCCESS, GetI18NCategory(I18NCat::DIALOG)->T("Desktop shortcut created"), 2.0f);
			}
		});
	}

	// Don't want to be able to delete the game while it's running.
	if (!inGame_) {
		Choice *deleteChoice = parent->Add(new Choice(ga->T("Delete Game"), ImageID("I_WARNING")));
		deleteChoice->OnClick.Handle(this, &GameScreen::OnDeleteGame);
	}
}

void GameScreen::OnCreateConfig(UI::EventParams &e) {
	if (!info_->Ready(GameInfoFlags::PARAM_SFO)) {
		return;
	}
	g_Config.CreateGameConfig(info_->id);
	g_Config.SaveGameConfig(info_->id, info_->GetTitle());
	info_->hasConfig = true;

	screenManager()->topScreen()->RecreateViews();
}

std::string_view GameScreen::GetTitle() const {
	if (knownFlags_ & GameInfoFlags::PARAM_SFO) {
		titleCache_ = info_->GetDBTitle();
	}

	return titleCache_;
}

void GameScreen::OnDeleteConfig(UI::EventParams &e) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	const bool trashAvailable = System_GetPropertyBool(SYSPROP_HAS_TRASH_BIN);
	screenManager()->push(
		new UI::MessagePopupScreen(di->T("Delete"), di->T("DeleteConfirmGameConfig", "Do you really want to delete the settings for this game?"),
			trashAvailable ? di->T("Move to trash") : di->T("Delete"), di->T("Cancel"), [this](bool result) {
		if (!result) {
			return;
		}
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PARAM_SFO);
		if (!info->Ready(GameInfoFlags::PARAM_SFO)) {
			return;
		}
		g_Config.DeleteGameConfig(info->id);
		info->hasConfig = false;
		RecreateViews();
	}));
}

void GameScreen::OnCwCheat(UI::EventParams &e) {
	screenManager()->push(new CwCheatScreen(gamePath_));
}

void GameScreen::OnSwitchBack(UI::EventParams &e) {
	TriggerFinish(DR_OK);
}

void GameScreen::OnPlay(UI::EventParams &e) {
	screenManager()->switchScreen(new EmuScreen(gamePath_));
}

void GameScreen::OnGameSettings(UI::EventParams &e) {
	std::shared_ptr<GameInfo> info_ = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO);
	if (info_ && info_->Ready(GameInfoFlags::PARAM_SFO)) {
		std::string discID = info_->GetParamSFO().GetValueString("DISC_ID");
		if ((discID.empty() || !info_->disc_total) && gamePath_.FilePathContainsNoCase("PSP/GAME/"))
			discID = g_paramSFO.GenerateFakeID(gamePath_);
		screenManager()->push(new GameSettingsScreen(gamePath_, discID, true));
	}
}

void GameScreen::OnDeleteSaveData(UI::EventParams &e) {
	std::shared_ptr<GameInfo> info_ = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO | GameInfoFlags::SIZE);
	if (info_) {
		// Check that there's any savedata to delete
		if (info_->saveDataSize) {
			const bool trashAvailable = System_GetPropertyBool(SYSPROP_HAS_TRASH_BIN);
			auto di = GetI18NCategory(I18NCat::DIALOG);
			Path gamePath = gamePath_;
			screenManager()->push(
				new UI::MessagePopupScreen(di->T("Delete"), di->T("DeleteConfirmAll", "Do you really want to delete all\nyour save data for this game?"), trashAvailable ? di->T("Move to trash") : di->T("Delete"), di->T("Cancel"),
					[gamePath](bool yes) {
				if (yes) {
					std::shared_ptr<GameInfo> info_ = g_gameInfoCache->GetInfo(NULL, gamePath, GameInfoFlags::PARAM_SFO);
					info_->DeleteAllSaveData();
					info_->saveDataSize = 0;
					info_->installDataSize = 0;
				}
			}));
		}
	}
	RecreateViews();
}

void GameScreen::OnDeleteGame(UI::EventParams &e) {
	std::shared_ptr<GameInfo> info_ = g_gameInfoCache->GetInfo(NULL, gamePath_, GameInfoFlags::PARAM_SFO);
	if (info_->Ready(GameInfoFlags::PARAM_SFO)) {
		auto di = GetI18NCategory(I18NCat::DIALOG);
		auto ga = GetI18NCategory(I18NCat::GAME);
		std::string prompt;
		prompt = di->T("DeleteConfirmGame", "Do you really want to delete this game\nfrom your device? You can't undo this.");
		prompt += "\n\n" + gamePath_.ToVisualString(g_Config.memStickDirectory.c_str());
		const bool trashAvailable = System_GetPropertyBool(SYSPROP_HAS_TRASH_BIN);
		Path gamePath = gamePath_;
		ScreenManager *sm = screenManager();
		screenManager()->push(
			new UI::MessagePopupScreen(ga->T("Delete Game"), prompt, trashAvailable ? di->T("Move to trash") : di->T("Delete"), di->T("Cancel"),
				[sm, gamePath](bool yes) {
			if (yes) {
				std::shared_ptr<GameInfo> info_ = g_gameInfoCache->GetInfo(NULL, gamePath, GameInfoFlags::PARAM_SFO);
				info_->Delete();
				g_gameInfoCache->Clear();
				g_recentFiles.Remove(gamePath.c_str());
				sm->switchScreen(new MainScreen());
			}
		}));
	}
}

void GameScreen::OnRemoveFromRecent(UI::EventParams &e) {
	g_recentFiles.Remove(gamePath_.ToString());
	screenManager()->switchScreen(new MainScreen());
}

void GameScreen::OnSetBackground(UI::EventParams &e) {
	auto ga = GetI18NCategory(I18NCat::GAME);
	std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath_, GameInfoFlags::PIC1);
	if (!info->Ready(GameInfoFlags::PIC1)) {
		return;
	}

	GameInfoTex *pic = nullptr;
	if (info->pic1.dataLoaded && info->pic1.data.size()) {
		pic = &info->pic1;
	}

	if (pic) {
		const Path bgPng = GetSysDirectory(DIRECTORY_SYSTEM) / "background.png";
		File::WriteStringToFile(false, pic->data, bgPng);
	}

	// Reinitializes the UI background.
	UIBackgroundShutdown();
}
