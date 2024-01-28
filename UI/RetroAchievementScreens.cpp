#include "Common/System/OSD.h"
#include "Common/System/Request.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/Data/Text/I18n.h"
#include "Common/UI/IconCache.h"

#include "Core/Config.h"
#include "Core/RetroAchievements.h"

#include "UI/RetroAchievementScreens.h"
#include "UI/BackgroundAudio.h"
#include "UI/OnScreenDisplay.h"

static inline const char *DeNull(const char *ptr) {
	return ptr ? ptr : "";
}

// Compound view, creating a FileChooserChoice inside.
class AudioFileChooser : public UI::LinearLayout {
public:
	AudioFileChooser(RequesterToken token, std::string *value, const std::string &title, UI::UISound sound, UI::LayoutParams *layoutParams = nullptr);

	UI::UISound sound_;
};

static constexpr UI::Size ITEM_HEIGHT = 64.f;

AudioFileChooser::AudioFileChooser(RequesterToken token, std::string *value, const std::string &title, UI::UISound sound, UI::LayoutParams *layoutParams) : UI::LinearLayout(UI::ORIENT_HORIZONTAL, layoutParams), sound_(sound) {
	using namespace UI;
	SetSpacing(2.0f);
	if (!layoutParams) {
		layoutParams_->width = FILL_PARENT;
		layoutParams_->height = ITEM_HEIGHT;
	}
	Add(new Choice(ImageID("I_PLAY"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT)))->OnClick.Add([=](UI::EventParams &) {
		float achievementVolume = g_Config.iAchievementSoundVolume * 0.1f;
		g_BackgroundAudio.SFX().Play(sound_, achievementVolume);
		return UI::EVENT_DONE;
	});
	Add(new FileChooserChoice(token, value, title, BrowseFileType::SOUND_EFFECT, new LinearLayoutParams(1.0f)))->OnChange.Add([=](UI::EventParams &e) {
		std::string path = e.s;
		Sample *sample = Sample::Load(path);
		if (sample) {
			g_BackgroundAudio.SFX().UpdateSample(sound, sample);
		} else {
			auto au = GetI18NCategory(I18NCat::AUDIO);
			g_OSD.Show(OSDType::MESSAGE_ERROR, au->T("Audio file format not supported. Must be WAV."));
			value->clear();
		}
		return UI::EVENT_DONE;
	});
	Add(new Choice(ImageID("I_TRASHCAN"), new LinearLayoutParams(ITEM_HEIGHT, ITEM_HEIGHT)))->OnClick.Add([=](UI::EventParams &) {
		g_BackgroundAudio.SFX().UpdateSample(sound, nullptr);
		value->clear();
		return UI::EVENT_DONE;
	});
}

void RetroAchievementsListScreen::CreateTabs() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);

	UI::LinearLayout *achievements = AddTab("Achievements", ac->T("Achievements"));
	achievements->SetSpacing(5.0f);
	CreateAchievementsTab(achievements);

	UI::LinearLayout *leaderboards = AddTab("Leaderboards", ac->T("Leaderboards"));
	leaderboards->SetSpacing(5.0f);
	CreateLeaderboardsTab(leaderboards);

#ifdef _DEBUG
	CreateStatisticsTab(AddTab("AchievementsStatistics", ac->T("Statistics")));
#endif
}

inline const char *AchievementBucketTitle(int bucketType) {
	switch (bucketType) {
	case RC_CLIENT_ACHIEVEMENT_BUCKET_LOCKED:               return "Locked achievements";
	case RC_CLIENT_ACHIEVEMENT_BUCKET_UNLOCKED:             return "Unlocked achievements";
	case RC_CLIENT_ACHIEVEMENT_BUCKET_UNSUPPORTED:          return "Unsupported achievements";
	case RC_CLIENT_ACHIEVEMENT_BUCKET_UNOFFICIAL:           return "Unofficial achievements";
	case RC_CLIENT_ACHIEVEMENT_BUCKET_RECENTLY_UNLOCKED:    return "Recently unlocked achievements";
	case RC_CLIENT_ACHIEVEMENT_BUCKET_ACTIVE_CHALLENGE:     return "Achievements with active challenges";
	case RC_CLIENT_ACHIEVEMENT_BUCKET_ALMOST_THERE:         return "Almost completed achievements";
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

	achievements->Add(new ItemHeader(ac->T("Achievements")));
	achievements->Add(new GameAchievementSummaryView());

	if (Achievements::EncoreModeActive()) {
		achievements->Add(new NoticeView(NoticeLevel::WARN, ac->T("In Encore mode - unlock state may not be accurate"), ""));
	}

	rc_client_achievement_list_t *list = rc_client_create_achievement_list(Achievements::GetClient(),
		filter, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);

	for (uint32_t i = 0; i < list->num_buckets; i++) {
		const rc_client_achievement_bucket_t &bucket = list->buckets[i];
		if (!bucket.num_achievements) {
			continue;
		}
		std::string title = StringFromFormat("%s (%d)", ac->T(AchievementBucketTitle(bucket.bucket_type)), bucket.num_achievements);
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

	std::vector<rc_client_leaderboard_t *> leaderboards;
	rc_client_leaderboard_list_t *list = rc_client_create_leaderboard_list(Achievements::GetClient(), RC_CLIENT_LEADERBOARD_LIST_GROUPING_NONE);
	for (uint32_t i = 0; i < list->num_buckets; i++) {
		const rc_client_leaderboard_bucket_t &bucket = list->buckets[i];
		for (uint32_t j = 0; j < bucket.num_leaderboards; j++) {
			leaderboards.push_back(bucket.leaderboards[j]);
		}
	}

	for (auto &leaderboard : leaderboards) {
		int leaderboardID = leaderboard->id;
		viewGroup->Add(new LeaderboardSummaryView(leaderboard))->OnClick.Add([=](UI::EventParams &e) -> UI::EventReturn {
			screenManager()->push(new RetroAchievementsLeaderboardScreen(gamePath_, leaderboardID));
			return UI::EVENT_DONE;
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
	: TabbedUIDialogScreenWithGameBackground(gamePath), leaderboardID_(leaderboardID) {
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
	UI::LinearLayout *layout = AddTab("AchievementsLeaderboard", leaderboard->title);
	layout->Add(new TextView(leaderboard->description));
	layout->Add(new ItemHeader(ac->T("Leaderboard")));

	auto strip = layout->Add(new ChoiceStrip(ORIENT_HORIZONTAL));
	strip->AddChoice(ac->T("Top players"));
	strip->AddChoice(ac->T("Around me"));
	strip->OnChoice.Add([=](UI::EventParams &e) {
		strip->SetSelection(e.a, false);
		nearMe_ = e.a != 0;
		FetchEntries();
		return UI::EVENT_DONE;
	});
	strip->SetSelection(nearMe_ ? 1 : 0, false);

	if (entryList_) {
		for (uint32_t i = 0; i < entryList_->num_entries; i++) {
			bool is_self = (i == entryList_->user_index);
			// Should highlight somehow.
			const rc_client_leaderboard_entry_t &entry = entryList_->entries[i];

			char buffer[512];
			rc_client_leaderboard_entry_get_user_image_url(&entryList_->entries[i], buffer, sizeof(buffer));
			// Can also show entry.submitted, which is a time_t. And maybe highlight recent ones?
			layout->Add(new LeaderboardEntryView(&entryList_->entries[i], is_self));
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
	TabbedUIDialogScreenWithGameBackground::update();
	Poll();
}

RetroAchievementsSettingsScreen::~RetroAchievementsSettingsScreen() {}

void RetroAchievementsSettingsScreen::CreateTabs() {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	using namespace UI;

	CreateAccountTab(AddTab("AchievementsAccount", ac->T("Account")));
	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		// Don't bother creating this tab if we don't have a file browser.
		CreateCustomizeTab(AddTab("AchievementsCustomize", ac->T("Customize")));
	}
	CreateDeveloperToolsTab(AddTab("AchievementsDeveloperTools", sy->T("Developer Tools")));
}

void RetroAchievementsSettingsScreen::sendMessage(UIMessage message, const char *value) {
	TabbedUIDialogScreenWithGameBackground::sendMessage(message, value);

	if (message == UIMessage::ACHIEVEMENT_LOGIN_STATE_CHANGE) {
		RecreateViews();
	}
}

void RetroAchievementsSettingsScreen::CreateAccountTab(UI::ViewGroup *viewGroup) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	using namespace UI;

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
		viewGroup->Add(new Choice(di->T("Log out")))->OnClick.Add([=](UI::EventParams &) -> UI::EventReturn {
			Achievements::Logout();
			return UI::EVENT_DONE;
		});
	} else {
		std::string errorMessage;
		if (Achievements::LoginProblems(&errorMessage)) {
			viewGroup->Add(new NoticeView(NoticeLevel::WARN, ac->T("Failed logging in to RetroAchievements"), errorMessage));
			viewGroup->Add(new Choice(di->T("Log out")))->OnClick.Add([=](UI::EventParams &) -> UI::EventReturn {
				Achievements::Logout();
				return UI::EVENT_DONE;
			});
		} else if (System_GetPropertyBool(SYSPROP_HAS_LOGIN_DIALOG)) {
			viewGroup->Add(new Choice(di->T("Log in")))->OnClick.Add([=](UI::EventParams &) -> UI::EventReturn {
				System_AskUsernamePassword(GetRequesterToken(), StringFromFormat("RetroAchievements: %s", di->T("Log in")), [](const std::string &value, int) {
					std::vector<std::string> parts;
					SplitString(value, '\n', parts);
					if (parts.size() == 2 && !parts[0].empty() && !parts[1].empty()) {
						Achievements::LoginAsync(parts[0].c_str(), parts[1].c_str());
					}
				});
				return UI::EVENT_DONE;
			});
		} else {
			// Hack up a temporary quick login-form-ish-thing
			viewGroup->Add(new PopupTextInputChoice(GetRequesterToken(), &username_, di->T("Username"), "", 128, screenManager()));
			viewGroup->Add(new PopupTextInputChoice(GetRequesterToken(), &password_, di->T("Password"), "", 128, screenManager()))->SetPasswordDisplay();
			Choice *loginButton = viewGroup->Add(new Choice(di->T("Log in")));
			loginButton->OnClick.Add([=](UI::EventParams &) -> UI::EventReturn {
				if (!username_.empty() && !password_.empty()) {
					Achievements::LoginAsync(username_.c_str(), password_.c_str());
					memset(&password_[0], 0, password_.size());
					password_.clear();
				}
				return UI::EVENT_DONE;
			});
			loginButton->SetEnabledFunc([&]() {
				return !username_.empty() && !password_.empty();
			});
		}
		viewGroup->Add(new Choice(ac->T("Register on www.retroachievements.org")))->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
			System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://retroachievements.org/createaccount.php");
			return UI::EVENT_DONE;
		});
	}

	using namespace UI;
	viewGroup->Add(new ItemHeader(di->T("Settings")));
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsEnable, ac->T("Achievements enabled")))->OnClick.Add([&](UI::EventParams &e) -> UI::EventReturn {
		Achievements::UpdateSettings();
		RecreateViews();
		return UI::EVENT_DONE;
	});
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsChallengeMode, ac->T("Hardcore Mode (no savestates)")))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsSoundEffects, ac->T("Sound Effects")))->SetEnabledPtr(&g_Config.bAchievementsEnable);  // not yet implemented

	viewGroup->Add(new ItemHeader(di->T("Links")));
	viewGroup->Add(new Choice(ac->T("RetroAchievements website")))->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.retroachievements.org/");
		return UI::EVENT_DONE;
	});
	viewGroup->Add(new Choice(ac->T("How to use RetroAchievements")))->OnClick.Add([&](UI::EventParams &) -> UI::EventReturn {
		System_LaunchUrl(LaunchUrlType::BROWSER_URL, "https://www.ppsspp.org/docs/reference/retro-achievements");
		return UI::EVENT_DONE;
	});
}

void RetroAchievementsSettingsScreen::CreateCustomizeTab(UI::ViewGroup *viewGroup) {
	auto ac = GetI18NCategory(I18NCat::ACHIEVEMENTS);
	auto a = GetI18NCategory(I18NCat::AUDIO);

	using namespace UI;
	viewGroup->Add(new ItemHeader(ac->T("Sound Effects")));
	viewGroup->Add(new AudioFileChooser(GetRequesterToken(), &g_Config.sAchievementsUnlockAudioFile, ac->T("Achievement unlocked"), UISound::ACHIEVEMENT_UNLOCKED));
	viewGroup->Add(new AudioFileChooser(GetRequesterToken(), &g_Config.sAchievementsLeaderboardSubmitAudioFile, ac->T("Leaderboard score submission"), UISound::LEADERBOARD_SUBMITTED));
	PopupSliderChoice *volume = viewGroup->Add(new PopupSliderChoice(&g_Config.iAchievementSoundVolume, VOLUME_OFF, VOLUME_FULL, VOLUME_FULL, a->T("Achievement sound volume"), screenManager()));
	volume->SetEnabledPtr(&g_Config.bEnableSound);
	volume->SetZeroLabel(a->T("Mute"));

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

	using namespace UI;
	viewGroup->Add(new ItemHeader(di->T("Settings")));
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsEncoreMode, ac->T("Encore Mode")))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsUnofficial, ac->T("Unofficial achievements")))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsLogBadMemReads, ac->T("Log bad memory accesses")))->SetEnabledPtr(&g_Config.bAchievementsEnable);
	viewGroup->Add(new CheckBox(&g_Config.bAchievementsSaveStateInHardcoreMode, ac->T("Allow Save State in Hardcore Mode (but not Load State)")))->SetEnabledPtr(&g_Config.bAchievementsEnable);
}

void MeasureAchievement(const UIContext &dc, const rc_client_achievement_t *achievement, AchievementRenderStyle style, float *w, float *h) {
	*w = 0.0f;
	switch (style) {
	case AchievementRenderStyle::PROGRESS_INDICATOR:
		dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, achievement->measured_progress, w, h);
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

void MeasureGameAchievementSummary(const UIContext &dc, float *w, float *h) {
	std::string description = Achievements::GetGameAchievementSummary();

	float tw, th;
	dc.MeasureText(dc.theme->uiFont, 1.0f, 1.0f, "Wg", &tw, &th);

	dc.MeasureText(dc.theme->uiFont, 0.66f, 0.66f, description.c_str(), w, h);
	*h += 8.0f + th;
	*w += 8.0f;
}

void MeasureLeaderboardSummary(const UIContext &dc, const rc_client_leaderboard_t *leaderboard, float *w, float *h) {
	*w = 0.0f;
	*h = 72.0f;
}

void MeasureLeaderboardEntry(const UIContext &dc, const rc_client_leaderboard_entry_t *entry, float *w, float *h) {
	*w = 0.0f;
	*h = 72.0f;
}

// Graphical
void RenderAchievement(UIContext &dc, const rc_client_achievement_t *achievement, AchievementRenderStyle style, const Bounds &bounds, float alpha, float startTime, float time_s, bool hasFocus) {
	using namespace UI;
	UI::Drawable background = UI::Drawable(dc.theme->backgroundColor);

	if (hasFocus) {
		background = dc.theme->itemFocusedStyle.background;
	}

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
	uint32_t fgColor = alphaMul(dc.theme->itemStyle.fgColor, alpha);

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

	dc.SetFontStyle(dc.theme->uiFont);

	char temp[512];

	switch (style) {
	case AchievementRenderStyle::LISTED:
	case AchievementRenderStyle::UNLOCKED:
	{
		dc.SetFontScale(1.0f, 1.0f);
		dc.DrawTextRect(achievement->title, bounds.Inset(iconSpace + 12.0f, 2.0f, padding, padding), fgColor, ALIGN_TOPLEFT);

		dc.SetFontScale(0.66f, 0.66f);
		dc.DrawTextRectSqueeze(DeNull(achievement->description), bounds.Inset(iconSpace + 12.0f, 39.0f, padding, padding), fgColor, ALIGN_TOPLEFT);

		if (style == AchievementRenderStyle::LISTED && strlen(achievement->measured_progress) > 0) {
			dc.SetFontScale(1.0f, 1.0f);
			dc.DrawTextRect(achievement->measured_progress, bounds.Inset(iconSpace + 12.0f, padding, padding + 100.0f, padding), fgColor, ALIGN_VCENTER | ALIGN_RIGHT);
		}

		// TODO: Draw measured_progress / measured_percent in a cute way
		snprintf(temp, sizeof(temp), "%d", achievement->points);

		dc.SetFontScale(1.5f, 1.5f);
		dc.DrawTextRect(temp, bounds.Expand(-5.0f, -5.0f), fgColor, ALIGN_RIGHT | ALIGN_VCENTER);

		dc.SetFontScale(1.0f, 1.0f);
		dc.Flush();
		break;
	}
	case AchievementRenderStyle::PROGRESS_INDICATOR:
		// TODO: Also render a progress bar.
		dc.SetFontScale(1.0f, 1.0f);
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
	char cacheKey[256];
	snprintf(cacheKey, sizeof(cacheKey), "ai:%s:%s", achievement->badge_name, iconState == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED ? "unlocked" : "locked");
	if (RC_OK == rc_client_achievement_get_image_url(achievement, iconState, temp, sizeof(temp))) {
		Achievements::DownloadImageIfMissing(cacheKey, std::string(temp));
		if (g_iconCache.BindIconTexture(&dc, cacheKey)) {
			dc.Draw()->DrawTexRect(Bounds(bounds.x + padding, bounds.y + padding, iconSpace, iconSpace), 0.0f, 0.0f, 1.0f, 1.0f, whiteAlpha(alpha));
		}
		dc.Flush();
		dc.RebindTexture();
	}

	dc.Flush();
	dc.PopScissor();
}

void RenderGameAchievementSummary(UIContext &dc, const Bounds &bounds, float alpha) {
	using namespace UI;
	UI::Drawable background = dc.theme->itemStyle.background;

	background.color = alphaMul(background.color, alpha);
	uint32_t fgColor = colorAlpha(dc.theme->itemStyle.fgColor, alpha);

	float iconSpace = 64.0f;
	dc.Flush();

	dc.Begin();
	dc.FillRect(background, bounds);

	dc.SetFontStyle(dc.theme->uiFont);

	const rc_client_game_t *gameInfo = rc_client_get_game_info(Achievements::GetClient());

	dc.SetFontScale(1.0f, 1.0f);
	dc.DrawTextRect(gameInfo->title, bounds.Inset(iconSpace + 5.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	std::string description = Achievements::GetGameAchievementSummary();

	dc.SetFontScale(0.66f, 0.66f);
	dc.DrawTextRect(description.c_str(), bounds.Inset(iconSpace + 5.0f, 38.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(1.0f, 1.0f);
	dc.Flush();

	char url[512];
	char cacheKey[256];
	snprintf(cacheKey, sizeof(cacheKey), "gi:%s", gameInfo->badge_name);
	if (RC_OK == rc_client_game_get_image_url(gameInfo, url, sizeof(url))) {
		Achievements::DownloadImageIfMissing(cacheKey, std::string(url));
		if (g_iconCache.BindIconTexture(&dc, cacheKey)) {
			dc.Draw()->DrawTexRect(Bounds(bounds.x, bounds.y, iconSpace, iconSpace), 0.0f, 0.0f, 1.0f, 1.0f, whiteAlpha(alpha));
		}
	}

	dc.Flush();
	dc.RebindTexture();
}

void RenderLeaderboardSummary(UIContext &dc, const rc_client_leaderboard_t *leaderboard, AchievementRenderStyle style, const Bounds &bounds, float alpha, float startTime, float time_s, bool hasFocus) {
	using namespace UI;
	UI::Drawable background = dc.theme->itemStyle.background;
	if (hasFocus) {
		background = dc.theme->itemFocusedStyle.background;
	}

	background.color = alphaMul(background.color, alpha);
	uint32_t fgColor = alphaMul(dc.theme->itemStyle.fgColor, alpha);

	if (style == AchievementRenderStyle::UNLOCKED) {
		float mixWhite = pow(Clamp((float)(1.0f - (time_s - startTime)), 0.0f, 1.0f), 3.0f);
		background.color = colorBlend(0xFFE0FFFF, background.color, mixWhite);
	}

	dc.Flush();

	dc.Begin();
	dc.FillRect(background, bounds);

	dc.SetFontStyle(dc.theme->uiFont);

	dc.SetFontScale(1.0f, 1.0f);
	dc.DrawTextRect(DeNull(leaderboard->title), bounds.Inset(12.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(0.66f, 0.66f);
	dc.DrawTextRectSqueeze(DeNull(leaderboard->description), bounds.Inset(12.0f, 39.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	/*
	char temp[64];
	snprintf(temp, sizeof(temp), "%d", leaderboard.points);

	dc.SetFontScale(1.5f, 1.5f);
	dc.DrawTextRect(temp, bounds.Expand(-5.0f, -5.0f), fgColor, ALIGN_RIGHT | ALIGN_VCENTER);

	dc.Flush();
	*/
	dc.SetFontScale(1.0f, 1.0f);

	dc.Flush();
	dc.RebindTexture();
}

void RenderLeaderboardEntry(UIContext &dc, const rc_client_leaderboard_entry_t *entry, const Bounds &bounds, float alpha, bool hasFocus, bool isCurrentUser) {
	using namespace UI;
	UI::Drawable background = dc.theme->itemStyle.background;
	if (hasFocus) {
		background = dc.theme->itemFocusedStyle.background;
	}
	if (isCurrentUser) {
		background = dc.theme->itemDownStyle.background;
	}

	background.color = alphaMul(background.color, alpha);
	uint32_t fgColor = alphaMul(dc.theme->itemStyle.fgColor, alpha);

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

	dc.SetFontStyle(dc.theme->uiFont);

	dc.SetFontScale(1.5f, 1.5f);
	dc.DrawTextRect(StringFromFormat("%d", entry->rank).c_str(), Bounds(bounds.x + 4.0f, bounds.y + 4.0f, numberSpace - 10.0f, bounds.h - 4.0f * 2.0f), fgColor, ALIGN_TOPRIGHT);

	dc.SetFontScale(1.0f, 1.0f);
	dc.DrawTextRect(entry->user, bounds.Inset(iconSpace + 5.0f, 2.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(0.66f, 0.66f);
	dc.DrawTextRect(DeNull(entry->display), bounds.Inset(iconSpace + 5.0f, 38.0f, 5.0f, 5.0f), fgColor, ALIGN_TOPLEFT);

	dc.SetFontScale(1.0f, 1.0f);
	dc.Flush();

	// Come up with a unique name for the icon entry.
	char cacheKey[256];
	snprintf(cacheKey, sizeof(cacheKey), "lbe:%s", entry->user);
	char temp[512];
	if (RC_OK == rc_client_leaderboard_entry_get_user_image_url(entry, temp, sizeof(temp))) {
		Achievements::DownloadImageIfMissing(cacheKey, std::string(temp));
		if (g_iconCache.BindIconTexture(&dc, cacheKey)) {
			dc.Draw()->DrawTexRect(Bounds(bounds.x + iconLeft, bounds.y + 4.0f, 64.0f, 64.0f), 0.0f, 0.0f, 1.0f, 1.0f, whiteAlpha(alpha));
		}
	}

	dc.Flush();
	dc.RebindTexture();
}

void AchievementView::Draw(UIContext &dc) {
	RenderAchievement(dc, achievement_, AchievementRenderStyle::LISTED, bounds_, 1.0f, 0.0f, 0.0f, HasFocus());
}

void AchievementView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	MeasureAchievement(dc, achievement_, AchievementRenderStyle::LISTED, &w, &h);
}

void AchievementView::Click() {
	// In debug builds, clicking achievements will show them being unlocked (which may be a lie).
#ifdef _DEBUG
	static int type = 0;
	type++;
	type = type % 5;
	switch (type) {
	case 0: g_OSD.ShowAchievementUnlocked(achievement_->id); break;
	case 1: g_OSD.ShowAchievementProgress(achievement_->id, true); break;
	case 2: g_OSD.ShowAchievementProgress(achievement_->id, false); break;
	case 3: g_OSD.ShowChallengeIndicator(achievement_->id, true); break;
	case 4: g_OSD.ShowChallengeIndicator(achievement_->id, false); break;
	}
#endif
}

void GameAchievementSummaryView::Draw(UIContext &dc) {
	RenderGameAchievementSummary(dc, bounds_, 1.0f);
}

void GameAchievementSummaryView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	// Somehow wrong!
	MeasureGameAchievementSummary(dc, &w, &h);
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
