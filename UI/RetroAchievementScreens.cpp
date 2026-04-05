#include "Common/Data/Text/I18n.h"
#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/TabHolder.h"
#include "Common/UI/Context.h"
#include "Common/UI/IconCache.h"
#include "Common/UI/PopupScreens.h"
#include "Common/UI/Notice.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"
#include "Core/RetroAchievements.h"

#include "UI/RetroAchievementScreens.h"
#include "UI/BackgroundAudio.h"
#include "UI/OnScreenDisplay.h"
#include "UI/MiscViews.h"

static inline std::string_view DeNull(const char *ptr) {
	return std::string_view(ptr ? ptr : "");
}

// Compound view, creating a FileChooserChoice inside.
class AudioFileChooser : public UI::LinearLayout {
public:
	AudioFileChooser(RequesterToken token, std::string *value, std::string_view title, UI::UISound sound, UI::LayoutParams *layoutParams = nullptr);

	UI::UISound sound_;
};

AudioFileChooser::AudioFileChooser(RequesterToken token, std::string *value, std::string_view title, UI::UISound sound, UI::LayoutParams *layoutParams) : UI::LinearLayout(ORIENT_HORIZONTAL, layoutParams), sound_(sound) {
	using namespace UI;
	SetSpacing(2.0f);
	if (!layoutParams) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = ITEM_HEIGHT;
	}
	Add(new Choice(ImageID("I_PLAY"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT)))->OnClick.Add([this](UI::EventParams &) {
		float achievementVolume = Volume100ToMultiplier(g_Config.iAchievementVolume);
		g_BackgroundAudio.SFX().Play(sound_, achievementVolume);
	});
	Add(new FileChooserChoice(token, value, title, BrowseFileType::SOUND_EFFECT, new LinearLayoutParams(1.0f)))->OnChange.Add([sound, value](UI::EventParams &e) {
		std::string path = e.s;
		Sample *sample = Sample::Load(path);
		if (sample) {
			g_BackgroundAudio.SFX().UpdateSample(sound, sample);
		} else {
			auto au = GetI18NCategory(I18NCat::AUDIO);
			g_OSD.Show(OSDType::MESSAGE_ERROR, au->T("Audio file format not supported. Must be WAV or MP3."));
			value->clear();
		}
	});
	Choice *trash = new Choice(ImageID("I_TRASHCAN"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT));
	trash->OnClick.Add([sound, value](UI::EventParams &) {
		g_BackgroundAudio.SFX().UpdateSample(sound, nullptr);
		value->clear();
	});
	Add(trash);
	trash->SetEnabledFunc([value]() {
		return !value->empty();
	});
}

void RetroAchievementsListScreen::CreateTabs() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	AddTab("Achievements", ac->T("Achievements"), ImageID::invalid(), [this](UI::LinearLayout *parent) {
		parent->SetSpacing(5.0f);
		CreateAchievementsTab(parent);
	});

	AddTab("Leaderboards", ac->T("Leaderboards"), ImageID::invalid(), [this](UI::LinearLayout *parent) {
		parent->SetSpacing(5.0f);
		CreateLeaderboardsTab(parent);
	});

#ifdef _DEBUG
	AddTab("AchievementsStatistics", ac->T("Statistics"), ImageID::invalid(), [this](UI::LinearLayout *parent) {
		parent->SetSpacing(5.0f);
		CreateStatisticsTab(parent);
	});
#endif
}

inline const char *AchievementBucketTitle(int bucketType) {
	switch (bucketType) {
	case RC_CLIENT_ACHIEVEMENT_BUCKET_LOCKED:               return "Locked";
	case RC_CLIENT_ACHIEVEMENT_BUCKET_UNLOCKED:             return "Unlocked";
	case RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED:          return "Unsupported";
	case RC_CLIENT_ACHIEVEMENT_BUCKET_UNOFFICIAL:           return "Unofficial";
	case RC_CLIENT_ACHIEVEMENT_BUCKET_RECENTLY_UNLOCKED:    return "Recently unlocked";
	case RC_CLIENT_ACHIEVEMENT_BUCKET_ACTIVE_CHALLENGE:     return "Achievements with active challenges";
	case RC_CLIENT_ACHIEVEMENT_BUCKET_ALMOST_THERE:         return "Almost completed";
	default: return "?";
	}
}

void RetroAchievementsListScreen::CreateAchievementsTab(UI::ViewGroup *achievements) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;

	int filter = RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE;
	if (Achievements::UnofficialEnabled()) {
		filter = RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL;
	}

	achievements->Add(new GameAchievementSummaryView());

	if (Achievements::EncoreModeActive()) {
		achievements->Add(new NoticeView(NoticeLevel::WARN, ac->T("In Encore mode - unlock state may not be accurate"), ""));
	}

	rc_client_achievement_list_t *list = rc_client_create_achievement_list(Achievements::GetClient(),
		filter, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);

	const rc_client_game_t *client_game = rc_client_get_game_info(Achievements::GetClient());

	for (uint32_t i = 0; i < list->num_buckets; i++) {
		const rc_client_achievement_bucket_t &bucket = list->buckets[i];
		if (!bucket.num_achievements) {
			continue;
		}
		// Populate the subset list as we go.
		const rc_client_subset_t *subset = rc_client_get_subset_info(Achievements::GetClient(), bucket.subset_id);
		std::string title;
		if (!subset || equals(subset->title, client_game->title)) {
			title = StringFromFormat("%s (%d)", ac->T_cstr(AchievementBucketTitle(bucket.bucket_type)), bucket.num_achievements);
		} else {
			title = StringFromFormat("%s - %s (%d)", subset->title, ac->T_cstr(AchievementBucketTitle(bucket.bucket_type)), bucket.num_achievements);
		}

		CollapsibleSection *section = achievements->Add(new CollapsibleSection(title));
		section->SetSpacing(2.0f);
		for (uint32_t j = 0; j < bucket.num_achievements; j++) {
			section->Add(new AchievementView(bucket.achievements[j]));
		}
	}
}

void RetroAchievementsListScreen::CreateLeaderboardsTab(UI::ViewGroup *viewGroup) {
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;

	viewGroup->Add(new GameAchievementSummaryView());

	viewGroup->Add(new ItemHeader(ac->T("Leaderboards")));

	std::vector<const rc_client_leaderboard_t *> leaderboards;
	rc_client_leaderboard_list_t *list = rc_client_create_leaderboard_list(Achievements::GetClient(), RC_CLIENT_LEADERBOARD_LIST_GROUPING_NONE);
	for (uint32_t i = 0; i < list->num_buckets; i++) {
		const rc_client_leaderboard_bucket_t &bucket = list->buckets[i];
		for (uint32_t j = 0; j < bucket.num_leaderboards; j++) {
			leaderboards.push_back(bucket.leaderboards[j]);
		}
	}

	for (auto &leaderboard : leaderboards) {
		int leaderboardID = leaderboard->id;
		viewGroup->Add(new LeaderboardSummaryView(leaderboard))->OnClick.Add([this, leaderboardID](UI::EventParams &e) -> void {
			screenManager()->push(new RetroAchievementsLeaderboardScreen(gamePath_, leaderboardID));
		});
	}
}

void RetroAchievementsListScreen::CreateStatisticsTab(UI::ViewGroup *viewGroup) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	using namespace UI;

	Achievements::Statistics stats = Achievements::GetStatistics();
	viewGroup->Add(new ItemHeader(ac->T("Statistics")));
	viewGroup->Add(new InfoItem(ac->T("Bad memory accesses"), StringFromFormat("%d", stats.badMemoryAccessCount)));
}

RetroAchievementsLeaderboardScreen::~RetroAchievementsLeaderboardScreen() {
	if (pendingAsyncCall_) {
		rc_client_abort_async(Achievements::GetClient(), pendingAsyncCall_);
	}
	Poll();  // Gets rid of pendingEntryList_.
	if (entryList_) {
		rc_client_destroy_leaderboard_entry_list(entryList_);
	}
}

RetroAchievementsLeaderboardScreen::RetroAchievementsLeaderboardScreen(const Path &gamePath, int leaderboardID)
	: UITabbedBaseDialogScreen(gamePath), leaderboardID_(leaderboardID) {
	FetchEntries();
}

void RetroAchievementsLeaderboardScreen::FetchEntries() {
	auto callback = [](int result, const char *error_message, rc_client_leaderboard_entry_list_t *list, rc_client_t *client, void *userdata) {
		if (result != RC_OK) {
			g_OSD.Show(OSDType::MESSAGE_ERROR, error_message, 10.0f);
			return;
		}

		RetroAchievementsLeaderboardScreen *thiz = (RetroAchievementsLeaderboardScreen *)userdata;
		thiz->pendingEntryList_ = list;
		thiz->pendingAsyncCall_ = nullptr;
	};

	if (nearMe_) {
		rc_client_begin_fetch_leaderboard_entries_around_user(Achievements::GetClient(), leaderboardID_, 10, callback, this);
	} else {
		rc_client_begin_fetch_leaderboard_entries(Achievements::GetClient(), leaderboardID_, 0, 25, callback, this);
	}
}

void RetroAchievementsLeaderboardScreen::CreateTabs() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	const rc_client_leaderboard_t *leaderboard = rc_client_get_leaderboard_info(Achievements::GetClient(), leaderboardID_);

	using namespace UI;
	AddTab("AchievementsLeaderboard", leaderboard->title, [this, leaderboard](UI::LinearLayout *parent) {
		CreateLeaderboardTab(parent, leaderboard);
	});
}

void RetroAchievementsLeaderboardScreen::CreateLeaderboardTab(UI::LinearLayout *layout, const rc_client_leaderboard_t *leaderboard) {
	using namespace UI;
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	layout->Add(new TextView(leaderboard->description, FLAG_WRAP_TEXT, false));
	layout->Add(new ItemHeader(ac->T("Leaderboard")));

	auto strip = layout->Add(new ChoiceStrip(ORIENT_HORIZONTAL));
	strip->AddChoice(ac->T("Top players"));
	strip->AddChoice(ac->T("Around me"));
	strip->OnChoice.Add([this, strip](UI::EventParams &e) {
		strip->SetSelection(e.a, false);
		nearMe_ = e.a != 0;
		FetchEntries();
	});
	strip->SetSelection(nearMe_ ? 1 : 0, false);

	if (entryList_) {
		for (uint32_t i = 0; i < entryList_->num_entries; i++) {
			bool is_self = (i == entryList_->user_index);
			// Should highlight somehow.
			const rc_client_leaderboard_entry_t &entry = entryList_->entries[i];

			char buffer[512];
			rc_client_leaderboard_entry_get_user_image_url(&entry, buffer, sizeof(buffer));
			// Can also show entry.submitted, which is a time_t. And maybe highlight recent ones?
			layout->Add(new LeaderboardEntryView(&entry, is_self));
		}
	}
}

void RetroAchievementsLeaderboardScreen::Poll() {
	if (pendingEntryList_) {
		if (entryList_) {
			rc_client_destroy_leaderboard_entry_list(entryList_);
		}
		entryList_ = pendingEntryList_;
		pendingEntryList_ = nullptr;
		RecreateViews();
	}
}

void RetroAchievementsLeaderboardScreen::update() {
	UITabbedBaseDialogScreen::update();
	Poll();
}

RetroAchievementsSettingsScreen::~RetroAchievementsSettingsScreen() = default;

void RetroAchievementsSettingsScreen::CreateTabs() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	using namespace UI;

	AddTab("AchievementsAccount", ac->T("Account"), [this](UI::LinearLayout *layout) {
		CreateAccountTab(layout);
	});
	// Don't bother creating this tab if we don't have a file browser.
	AddTab("AchievementsCustomize", ac->T("Customize"), [this](UI::LinearLayout *layout) {
		CreateCustomizeTab(layout);
	});
	AddTab("AchievementsDeveloperTools", sy->T("Developer Tools"), [this](UI::LinearLayout *layout) {
		CreateDeveloperToolsTab(layout);
	});
}

void RetroAchievementsSettingsScreen::sendMessage(UIMessage message, const char *value) {
	UITabbedBaseDialogScreen::sendMessage(message, value);

	if (message == UIMessage::ACHIEVEMENT_LOGIN_STATE_CHANGE) {
		RecreateViews();
	}
}

void RetroAchievementsSettingsScreen::CreateAccountTab(UI::ViewGroup *viewGroup) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	using namespace UI;

	viewGroup->Add(new PaneTitleBar(Path(), sy->T("RetroAchievements"), "/docs/reference/retro-achievements/"));

	if (!g_Config.bAchievementsEnable) {
		viewGroup->Add(new NoticeView(NoticeLevel::INFO, ac->T("Achievements are disabled"), "", new LinearLayoutParams(Margins(5))));
	} else if (Achievements::IsLoggedIn()) {
		const rc_client_user_t *info = rc_client_get_user_info(Achievements::GetClient());

		// In the future, RetroAchievements will support display names. Prepare for that.
		if (strcmp(info->display_name, info->username) != 0) {
			viewGroup->Add(new InfoItem(ac->T("Name"), info->display_name));
		}
		viewGroup->Add(new InfoItem(di->T("Username"), info->username));
		// viewGroup->Add(new InfoItem(ac->T("Unread messages"), info.numUnreadMessages));
		viewGroup->Add(new Choice(di->T("Log out")))->OnClick.Add([](UI::EventParams &) -> void {
			Achievements::Logout();
		});
	} else {
		std::string errorMessage;
		if (Achievements::LoginProblems(&errorMessage)) {
			viewGroup->Add(new NoticeView(NoticeLevel::WARN, ac->T("Failed logging in to RetroAchievements"), errorMessage));
			viewGroup->Add(new Choice(di->T("Log out")))->OnClick.Add([](UI::EventParams &) -> void {
				Achievements::Logout();
			});
		} else if (System_GetPropertyBool(SYSPROP_HAS_LOGIN_DIALOG)) {
			viewGroup->Add(new Choice(di->T("Log in")))->OnClick.Add([this](UI::EventParams &) -> void {
				auto di = GetI18NCategory(I18NCat::DIALOG);
				std::string title = StringFromFormat("RetroAchievements: %s", di->T_cstr("Log in"));
				System_AskUsernamePassword(GetRequesterToken(), title, g_Config.sAchievementsUserName, [](const std::string &value, int) {
					std::vector<std::string> parts;
					SplitString(value, '\n', parts);
					if (parts.size() == 2 && !parts[0].empty() && !parts[1].empty()) {
						Achievements::LoginAsync(parts[0].c_str(), parts[1].c_str());
					}
				});
			});
		} else {
			// Hack up a temporary quick login-form-ish-thing
			viewGroup->Add(new PopupTextInputChoice(GetRequesterToken(), &g_Config.sAchievementsUserName, di->T("Username"), "", 128, screenManager()));
			viewGroup->Add(new PopupTextInputChoice(GetRequesterToken(), &password_, di->T("Password"), "", 128, screenManager()))->SetPasswordDisplay();
			Choice *loginButton = viewGroup->Add(new Choice(di->T("Log in")));
			loginButton->OnClick.Add([this](UI::EventParams &) -> void {
				if (!g_Config.sAchievementsUserName.empty() && !password_.empty()) {
					Achievements::LoginAsync(g_Config.sAchievementsUserName.c_str(), password_.c_str());
					memset(&password_[0], 0, password_.size());
					password_.clear();
				}
			});
			loginButton->SetEnabledFunc([this]() {
				return !g_Config.sAchievementsUserName.empty() && !password_.empty();
			});
		}
		viewGroup->Add(new Choice(ac->T("Register on www.retroachievements.org")))->OnClick.Add([](UI::EventParams &) -> void {
			System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://retroachievements.org/createaccount.php");
		});
	}

	using namespace UI;
	viewGroup->Add(new ItemHeader(di->T("Settings")));
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsEnable, ac->T("Achievements enabled")))->OnClick.Add([this](UI::EventParams &e) -> void {
		Achievements::UpdateSettings();
		RecreateViews();
	});
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsHardcoreMode, ac->T("Hardcore Mode (no savestates)")))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsSoundEffects, ac->T("Sound Effects")))->SetEnabledPtr(&g_Config.bAchievementsEnable);

	viewGroup->Add(new ItemHeader(di->T("Links")));
	viewGroup->Add(new Choice(ac->T("RetroAchievements website"), ImageID("I_LINK_OUT")))->OnClick.Add([](UI::EventParams &) -> void {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.retroachievements.org/");
	});
	viewGroup->Add(new Choice(ac->T("How to use RetroAchievements"), ImageID("I_LINK_OUT")))->OnClick.Add([](UI::EventParams &) -> void {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/docs/reference/retro-achievements");
	});
}

void RetroAchievementsSettingsScreen::CreateCustomizeTab(UI::ViewGroup *viewGroup) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	auto a = GetI18NCategory(I18NCat::AUDIO);

	viewGroup->Add(new PaneTitleBar(Path(), ac->T("Customize"), "/docs/reference/retro-achievements/"));

	using namespace UI;
	viewGroup->Add(new ItemHeader(ac->T("Sound Effects")));
	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		viewGroup->Add(new AudioFileChooser(GetRequesterToken(), &g_Config.sAchievementsUnlockAudioFile, ac->T("Achievement unlocked"), UISound::ACHIEVEMENT_UNLOCKED));
		viewGroup->Add(new AudioFileChooser(GetRequesterToken(), &g_Config.sAchievementsLeaderboardSubmitAudioFile, ac->T("Leaderboard score submission"), UISound::LEADERBOARD_SUBMITTED));
	}
	PopupSliderChoice *achievementVolume = viewGroup->Add(new PopupSliderChoice(&g_Config.iAchievementVolume, VOLUME_OFF, VOLUMEHI_FULL, MultiplierToVolume100(0.6f), ac->T("Achievement sound volume"), screenManager()));
	achievementVolume->SetFormat("%d%%");
	achievementVolume->SetEnabledPtr(&g_Config.bEnableSound);
	achievementVolume->SetZeroLabel(a->T("Mute"));

	static const char *positions[] = { "None", "Bottom Left", "Bottom Center", "Bottom Right", "Top Left", "Top Center", "Top Right", "Center Left", "Center Right" };

	viewGroup->Add(new ItemHeader(ac->T("Notifications")));
	viewGroup->Add(new PopupMultiChoice(&g_Config.iAchievementsLeaderboardStartedOrFailedPos, ac->T("Leaderboard attempt started or failed"), positions, -1, ARRAY_SIZE(positions), I18NCat::DIALOG, screenManager()))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new PopupMultiChoice(&g_Config.iAchievementsLeaderboardSubmittedPos, ac->T("Leaderboard result submitted"), positions, -1, ARRAY_SIZE(positions), I18NCat::DIALOG, screenManager()))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new PopupMultiChoice(&g_Config.iAchievementsLeaderboardTrackerPos, ac->T("Leaderboard tracker"), positions, -1, ARRAY_SIZE(positions), I18NCat::DIALOG, screenManager()))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new PopupMultiChoice(&g_Config.iAchievementsUnlockedPos, ac->T("Achievement unlocked"), positions, -1, ARRAY_SIZE(positions), I18NCat::DIALOG, screenManager()))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new PopupMultiChoice(&g_Config.iAchievementsChallengePos, ac->T("Challenge indicator"), positions, -1, ARRAY_SIZE(positions), I18NCat::DIALOG, screenManager()))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new PopupMultiChoice(&g_Config.iAchievementsProgressPos, ac->T("Achievement progress"), positions, -1, ARRAY_SIZE(positions), I18NCat::DIALOG, screenManager()))->SetEnabledPtr(&g_Config.bAchievementsEnable);
}

void RetroAchievementsSettingsScreen::CreateDeveloperToolsTab(UI::ViewGroup *viewGroup) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	using namespace UI;
	viewGroup->Add(new PaneTitleBar(Path(), sy->T("Developer Tools"), "/docs/reference/retro-achievements/"));
#ifdef RC_CLIENT_SUPPORTS_RAINTEGRATION
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsEnableRAIntegration, ac->T("Enable RAIntegration (for achievement development)")))->SetEnabledPtr(&g_Config.bAchievementsEnable);
#endif
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsEncoreMode, ac->T("Encore Mode")))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsUnofficial, ac->T("Unofficial achievements")))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsLogBadMemReads, ac->T("Log bad memory accesses")))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsSaveStateInHardcoreMode, ac->T("Allow Save State in Hardcore Mode (but not Load State)")))->SetEnabledPtr(&g_Config.bAchievementsEnable);
}

void MeasureAchievement(const UIContext &dc, const rc_client_achievement_t *achievement, AchievementRenderStyle style, float *w, float *h) {
	*w = 0.0f;
	switch (style) {
	case AchievementRenderStyle::PROGRESS_INDICATOR:
		dc.MeasureText(dc.GetTheme().uiFont, 1.0f, 1.0f, achievement->measured_progress, w, h);
		*w += 44.0f + 4.0f * 3.0f;
		*h = 44.0f;
		break;
	case AchievementRenderStyle::CHALLENGE_INDICATOR:
		// ONLY the icon.
		*w = 60.0f;
		*h = 60.0f;
		break;
	default:
		*h = 72.0f;
		break;
	}
}

static void MeasureLeaderboardSummary(const UIContext &dc, const rc_client_leaderboard_t *leaderboard, float *w, float *h) {
	*w = 0.0f;
	*h = 72.0f;
}

static void MeasureLeaderboardEntry(const UIContext &dc, const rc_client_leaderboard_entry_t *entry, float *w, float *h) {
	*w = 0.0f;
	*h = 72.0f;
}

// Graphical
void RenderAchievement(UIContext &dc, const rc_client_achievement_t *achievement, AchievementRenderStyle style, const Bounds &bounds, float alpha, float startTime, float time_s, bool hasFocus) {
	using namespace UI;
	UI::Drawable background = UI::Drawable(dc.GetTheme().backgroundColor);

	if (hasFocus) {
		background = dc.GetTheme().itemFocusedStyle.background;
	}

	_assert_(achievement);

	// Set some alpha, if displayed in list.
	if (style == AchievementRenderStyle::LISTED) {
		background.color = colorAlpha(background.color, 0.6f);
	}

	if (!achievement->unlocked && !hasFocus) {
		// Make the background color gray.
		// TODO: Different colors in hardcore mode, or even in the "re-take achievements" mode when we add that?
		background.color = (background.color & 0xFF000000) | 0x706060;
	}

	int iconState = achievement->state;

	background.color = alphaMul(background.color, alpha);
	uint32_t fgColor = alphaMul(dc.GetTheme().itemStyle.fgColor, alpha);

	if (style == AchievementRenderStyle::UNLOCKED) {
		float mixWhite = pow(Clamp((float)(1.0f - (time_s - startTime)), 0.0f, 1.0f), 3.0f);
		background.color = colorBlend(0xFFE0FFFF, background.color, mixWhite);
	}

	float padding = 4.0f;

	float iconSpace = bounds.h - padding * 2.0f;
	dc.Flush();
	dc.RebindTexture();
	dc.PushScissor(bounds);

	dc.Begin();

	if (style != AchievementRenderStyle::LISTED) {
		dc.DrawRectDropShadow(bounds, 12.0f, 0.7f * alpha);
	}

	dc.FillRect(background, bounds);

	dc.Flush();
	dc.Begin();

	dc.SetFontStyle(*GetTextStyle(dc, UI::TextSize::Normal));

	char temp[512];

	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	switch (style) {
	case AchievementRenderStyle::LISTED:
	case AchievementRenderStyle::UNLOCKED:
	{
		dc.SetFontScale(1.0f, 1.0f);
		std::string title = achievement->title;
		std::string_view badge = "";

		// Add simple display of the achievement types.
		// Needs refinement, but works.
		// See issue #19632
		switch (achievement->type) {
		case RC_CLIENT_ACHIEVEMENT_TYPE_MISSABLE:
			badge = ac->T("Missable");
			break;
		case RC_CLIENT_ACHIEVEMENT_TYPE_PROGRESSION:
			badge = ac->T("Progression");
			break;
		case RC_CLIENT_ACHIEVEMENT_TYPE_WIN:
			badge = ac->T("Win");
			break;
		}

		dc.DrawTextRect(title, bounds.Inset(iconSpace + 12.0f, 2.0f, padding, padding), fgColor, ALIGN_TOPLEFT);

		dc.SetFontStyle(*GetTextStyle(dc, UI::TextSize::Small));
		dc.DrawTextRectSqueeze(DeNull(achievement->description), bounds.Inset(iconSpace + 12.0f, 39.0f, padding, padding), fgColor, ALIGN_TOPLEFT);

		dc.DrawTextRect(badge, bounds, fgColor, ALIGN_TOPRIGHT);

		if (style == AchievementRenderStyle::LISTED && strlen(achievement->measured_progress) > 0) {
			dc.SetFontStyle(*GetTextStyle(dc, UI::TextSize::Normal));
			dc.DrawTextRect(achievement->measured_progress, bounds.Inset(iconSpace + 12.0f, padding, padding + 100.0f, padding), fgColor, ALIGN_VCENTER | ALIGN_RIGHT);
		}

		// TODO: Draw measured_progress / measured_percent in a cute way
		snprintf(temp, sizeof(temp), "%d", achievement->points);

		// The points number to the right.
		dc.SetFontStyle(*GetTextStyle(dc, UI::TextSize::Big));
		dc.DrawTextRect(temp, bounds.Expand(-5.0f, -5.0f), fgColor, ALIGN_RIGHT | (badge.empty() ? ALIGN_VCENTER : ALIGN_BOTTOMRIGHT));

		dc.Flush();
		break;
	}
	case AchievementRenderStyle::PROGRESS_INDICATOR:
		// TODO: Also render a progress bar.
		dc.DrawTextRect(achievement->measured_progress, bounds.Inset(iconSpace + padding * 2.0f, padding, padding, padding), fgColor, ALIGN_LEFT | ALIGN_VCENTER);
		// Show the unlocked icon.
		iconState = RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED;
		break;
	case AchievementRenderStyle::CHALLENGE_INDICATOR:
		// Nothing but the icon, unlocked.
		iconState = RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED;
		break;
	}

	// Download and display the image.
	const char *url = iconState == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED ? achievement->badge_url : achievement->badge_locked_url;
	Achievements::DownloadImageIfMissing(url);
	if (g_iconCache.BindIconTexture(&dc, url)) {
		dc.Draw()->DrawTexRect(Bounds(bounds.x + padding, bounds.y + padding, iconSpace, iconSpace), 0.0f, 0.0f, 1.0f, 1.0f, whiteAlpha(alpha));
	}
	dc.SetFontStyle(*GetTextStyle(dc, UI::TextSize::Normal));
	dc.Flush();
	dc.RebindTexture();
	dc.PopScissor();
}

static void MeasureGameAchievementSummary(const UIContext &dc, std::string_view title, float maxWidth, float *w, float *h) {
	std::string description = Achievements::GetGameAchievementSummary();

	float iconSpace = 64.0f;
	float availableWidth = maxWidth - iconSpace - 5.0f - 5.0f - 8.0f;

	float titleWidth, titleHeight;
	dc.MeasureTextRect(dc.GetTheme().uiFont, 1.0f, 1.0f, title, availableWidth, &titleWidth, &titleHeight, FLAG_ELLIPSIZE_TEXT);
	dc.MeasureTextRect(dc.GetTheme().uiFontSmall, 1.0f, 1.0f, description, availableWidth, w, h, FLAG_WRAP_TEXT);
	*h += 8.0f + titleHeight;
	*w += 8.0f;
}

static void RenderGameAchievementSummary(UIContext &dc, const Bounds &bounds, float alpha, const rc_client_game_t *gameInfo) {
	using namespace UI;
	UI::Drawable background = dc.GetTheme().itemStyle.background;

	background.color = alphaMul(background.color, alpha);
	uint32_t fgColor = colorAlpha(dc.GetTheme().itemStyle.fgColor, alpha);

	float iconSpace = 64.0f;
	dc.Flush();

	dc.Begin();
	dc.FillRect(background, bounds);

	dc.SetFontScale(1.0f, 1.0f);
	dc.SetFontStyle(dc.GetTheme().uiFont);

	dc.DrawTextRect(gameInfo->title, bounds.Inset(iconSpace + 5.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT | FLAG_ELLIPSIZE_TEXT);

	std::string description = Achievements::GetGameAchievementSummary();

	dc.SetFontStyle(dc.GetTheme().uiFontSmall);
	dc.DrawTextRect(description, bounds.Inset(iconSpace + 5.0f, 38.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT | FLAG_WRAP_TEXT);

	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.Flush();

	Achievements::DownloadImageIfMissing(gameInfo->badge_url);
	if (g_iconCache.BindIconTexture(&dc, gameInfo->badge_url)) {
		dc.Draw()->DrawTexRect(Bounds(bounds.x, bounds.y + (bounds.h - iconSpace) * 0.5f, iconSpace, iconSpace), 0.0f, 0.0f, 1.0f, 1.0f, whiteAlpha(alpha));
	}

	dc.Flush();
	dc.RebindTexture();
}

static void RenderLeaderboardSummary(UIContext &dc, const rc_client_leaderboard_t *leaderboard, AchievementRenderStyle style, const Bounds &bounds, float alpha, float startTime, float time_s, bool hasFocus) {
	using namespace UI;
	UI::Drawable background = dc.GetTheme().itemStyle.background;
	if (hasFocus) {
		background = dc.GetTheme().itemFocusedStyle.background;
	}

	background.color = alphaMul(background.color, alpha);
	uint32_t fgColor = alphaMul(dc.GetTheme().itemStyle.fgColor, alpha);

	if (style == AchievementRenderStyle::UNLOCKED) {
		float mixWhite = pow(Clamp((float)(1.0f - (time_s - startTime)), 0.0f, 1.0f), 3.0f);
		background.color = colorBlend(0xFFE0FFFF, background.color, mixWhite);
	}

	dc.Flush();

	dc.Begin();
	dc.FillRect(background, bounds);

	dc.SetFontScale(1.0f, 1.0f);
	dc.SetFontStyle(dc.GetTheme().uiFont);

	dc.DrawTextRect(DeNull(leaderboard->title), bounds.Inset(12.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontStyle(dc.GetTheme().uiFontSmall);
	dc.DrawTextRectSqueeze(DeNull(leaderboard->description), bounds.Inset(12.0f, 39.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	/*
	char temp[64];
	snprintf(temp, sizeof(temp), "%d", leaderboard.points);

	dc.SetFontScale(1.5f, 1.5f);
	dc.DrawTextRect(temp, bounds.Expand(-5.0f, -5.0f), fgColor, ALIGN_RIGHT | ALIGN_VCENTER);

	dc.Flush();
	*/
	dc.SetFontStyle(dc.GetTheme().uiFont);

	dc.Flush();
	dc.RebindTexture();
}

static void RenderLeaderboardEntry(UIContext &dc, const rc_client_leaderboard_entry_t *entry, const Bounds &bounds, float alpha, bool hasFocus, bool isCurrentUser) {
	using namespace UI;
	UI::Drawable background = dc.GetTheme().itemStyle.background;
	if (hasFocus) {
		background = dc.GetTheme().itemFocusedStyle.background;
	}
	if (isCurrentUser) {
		background = dc.GetTheme().itemDownStyle.background;
	}

	background.color = alphaMul(background.color, alpha);
	uint32_t fgColor = alphaMul(dc.GetTheme().itemStyle.fgColor, alpha);

	float iconSize = 64.0f;
	float numberSpace = 128.0f;
	float iconLeft = numberSpace + 5.0f;
	float iconSpace = numberSpace + 5.0f + iconSize;

	// Sanity check
	if (!entry->user) {
		return;
	}

	dc.Flush();

	dc.Begin();
	dc.FillRect(background, bounds);

	dc.SetFontScale(1.0f, 1.0f);
	dc.SetFontStyle(dc.GetTheme().uiFontBig);

	dc.DrawTextRect(StringFromFormat("%d", entry->rank), Bounds(bounds.x + 4.0f, bounds.y + 4.0f, numberSpace - 10.0f, bounds.h - 4.0f * 2.0f), fgColor, ALIGN_TOPRIGHT);

	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.DrawTextRect(entry->user, bounds.Inset(iconSpace + 5.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontStyle(dc.GetTheme().uiFontSmall);
	dc.DrawTextRect(DeNull(entry->display), bounds.Inset(iconSpace + 5.0f, 38.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.Flush();

	// Come up with a unique name for the icon entry.
	char userImageUrl[512];
	if (RC_OK == rc_client_leaderboard_entry_get_user_image_url(entry, userImageUrl, sizeof(userImageUrl))) {
		Achievements::DownloadImageIfMissing(userImageUrl);
		if (g_iconCache.BindIconTexture(&dc, userImageUrl)) {
			dc.Draw()->DrawTexRect(Bounds(bounds.x + iconLeft, bounds.y + 4.0f, 64.0f, 64.0f), 0.0f, 0.0f, 1.0f, 1.0f, whiteAlpha(alpha));
		}
	}

	dc.Flush();
	dc.RebindTexture();
}

void AchievementView::Draw(UIContext &dc) {
	if (!achievement_) {
		return;
	}
	RenderAchievement(dc, achievement_, AchievementRenderStyle::LISTED, bounds_, 1.0f, 0.0f, 0.0f, HasFocus());
}

void AchievementView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	if (!achievement_) {
		return;
	}
	MeasureAchievement(dc, achievement_, AchievementRenderStyle::LISTED, &w, &h);
}

void AchievementView::ClickInternal() {
	if (!achievement_) {
		return;
	}
	// In debug builds, clicking achievements will show them being unlocked (which may be a lie).
#ifdef _DEBUG
	static int type = 0;
	type++;
	type = type % 5;
	switch (type) {
	case 0: g_OSD.ShowAchievementUnlocked((int)achievement_->id); break;
	case 1: g_OSD.ShowAchievementProgress((int)achievement_->id, true); break;
	case 2: g_OSD.ShowAchievementProgress((int)achievement_->id, false); break;
	case 3: g_OSD.ShowChallengeIndicator((int)achievement_->id, true); break;
	case 4: g_OSD.ShowChallengeIndicator((int)achievement_->id, false); break;
	}
#endif
}

void GameAchievementSummaryView::Draw(UIContext &dc) {
	const rc_client_game_t *client_game = rc_client_get_game_info(Achievements::GetClient());
	if (client_game) {
		RenderGameAchievementSummary(dc, bounds_, 1.0f, client_game);
	}
}

void GameAchievementSummaryView::GetContentDimensionsBySpec(const UIContext &dc, UI::MeasureSpec horiz, UI::MeasureSpec vert, float &w, float &h) const {
	const rc_client_game_t *client_game = rc_client_get_game_info(Achievements::GetClient());
	if (!client_game) {
		w = 0;
		h = 0;
		return;
	}
	float layoutWidth = layoutParams_->width;
	if (layoutWidth < 0) {
		// If there's no size, let's grow as big as we want.
		layoutWidth = horiz.size;
	}
	ApplyBoundBySpec(layoutWidth, horiz);
	MeasureGameAchievementSummary(dc, client_game->title, layoutWidth, &w, &h);
}

void LeaderboardSummaryView::Draw(UIContext &dc) {
	RenderLeaderboardSummary(dc, leaderboard_, AchievementRenderStyle::LISTED, bounds_, 1.0f, 0.0f, 0.0f, HasFocus());
}

void LeaderboardSummaryView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MeasureLeaderboardSummary(dc, leaderboard_, &w, &h);
}

void LeaderboardEntryView::Draw(UIContext &dc) {
	RenderLeaderboardEntry(dc, entry_, bounds_, 1.0f, HasFocus(), isCurrentUser_);
}

void LeaderboardEntryView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MeasureLeaderboardEntry(dc, entry_, &w, &h);
}
