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

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/OSD.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/TimeUtil.h"
#include "Common/UI/Context.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Util/RecentFiles.h"

#include "UI/BackgroundAudio.h"
#include "UI/GameInfoCache.h"
#include "UI/GamePacker.h"

namespace GamePacker {
namespace {

struct Job {
	Path gamePath;     // As the game list knows it, maybe a PSP/GAME/<folder>.
	Path file;         // The image itself.
	Path finalOutput;  // Where the converted game ends up.
	Path writeOutput;  // What gets written: finalOutput, or a temporary name when that's the original's.
	bool replace = false;  // Delete the original when done.
	bool toISO = false;
	CSOCompress::Preset preset = CSOCompress::Preset::Best;

	std::atomic<float> progress{0.0f};
	double startTime = 0.0;
	std::atomic<bool> cancel{false};
	std::atomic<bool> done{false};
	bool success = false;    // Set before done.
	bool cancelled = false;  // Set before done.
	std::string error;       // Set before done.

	std::thread thread;
};

// Only touched on the UI thread. The worker gets the raw pointer, and the job lives until it's joined.
std::unique_ptr<Job> g_job;
int g_generation = 0;
Result g_lastResult;

void RunJob(Job *job) {
	SetCurrentThreadName("GamePacker");
	// Games in Android's scoped storage are read and written through JNI.
	AndroidJNIThreadContext jniContext;
	INFO_LOG(Log::Loader, "GamePacker: %s %s -> %s", job->toISO ? "Extracting" : "Compressing", job->file.c_str(), job->writeOutput.c_str());

	auto progress = [job](float p) {
		job->progress = p;
		return !job->cancel;
	};
	std::string error;
	bool cancelled = false;
	bool success;
	if (job->toISO) {
		success = CSOCompress::Extract(job->file, job->writeOutput, progress, &error, &cancelled);
	} else {
		success = CSOCompress::Compress(job->file, job->writeOutput, job->preset, progress, &error, &cancelled);
	}
	if (!success && !cancelled) {
		ERROR_LOG(Log::Loader, "GamePacker: Failed on %s: %s", job->file.c_str(), error.c_str());
	}
	job->success = success;
	job->cancelled = cancelled;
	job->error = error;
	job->done = true;
}

// "<name>" for "<name>.<ext>", or for an EBOOT.PBP the game's disc id.
std::string OutputBaseName(const Path &gamePath, const Path &file) {
	std::string name = file.GetFilename();
	const std::string ext = file.GetFileExtension();
	if (ext.size() <= name.size()) {
		name.resize(name.size() - ext.size());
	}
	if (equalsNoCase(file.GetFilename(), "EBOOT.PBP")) {
		std::shared_ptr<GameInfo> info = g_gameInfoCache->GetInfo(nullptr, gamePath, GameInfoFlags::PARAM_SFO);
		if (info->Ready(GameInfoFlags::PARAM_SFO)) {
			const std::string id = SanitizeString(info->id, StringRestriction::FileName);
			if (!id.empty()) {
				return id;
			}
		}
	}
	return name;
}

// Where an option puts its result, before looking at what's already there.
Path FinalOutput(const Path &gamePath, const Path &file, Container from, const Option &option) {
	const Container to = option.toISO ? Container::ISO : Container::CSO;
	if (to == from) {
		return file;
	}
	return file.NavigateUp() / (OutputBaseName(gamePath, file) + (option.toISO ? ".iso" : ".cso"));
}

// "<name> (2).<ext>", "<name> (3).<ext>"... for a name that's taken.
Path NumberedName(const Path &path) {
	const std::string ext = path.GetFileExtension();
	std::string name = path.GetFilename();
	name.resize(name.size() - ext.size());
	for (int i = 2; i < 1000; i++) {
		Path candidate = path.NavigateUp() / StringFromFormat("%s (%d)%s", name.c_str(), i, ext.c_str());
		if (!File::Exists(candidate)) {
			return candidate;
		}
	}
	return Path();
}

}  // namespace

Container DetectContainer(const Path &gamePath, Path *file) {
	// Content URIs are fine too (Android's storage), only remote games aren't.
	if (gamePath.Type() == PathType::HTTP) {
		return Container::None;
	}
	Path image = gamePath;
	if (File::IsDirectory(gamePath)) {
		image = gamePath / "EBOOT.PBP";
		if (!File::Exists(image)) {
			return Container::None;
		}
	}
	const std::string ext = image.GetFileExtension();
	Container container = Container::None;
	if (ext == ".iso") {
		container = Container::ISO;
	} else if (ext == ".cso") {
		container = Container::CSO;
	} else if (ext == ".chd") {
		container = Container::CHD;
	} else if (ext == ".pbp") {
		container = Container::PBP;
	}
	if (container != Container::None && file) {
		*file = image;
	}
	return container;
}

std::vector<Option> ListOptions(Container from) {
	auto ga = GetI18NCategory(I18NCat::GAME);

	std::vector<Option> options;
	if (from == Container::None) {
		return options;
	}
	if (from != Container::ISO) {
		Option option;
		option.toISO = true;
		option.title = std::string(ga->T("Extract to ISO"));
		option.description = std::string(ga->T("PackDescISO", "Uncompressed disc image"));
		options.push_back(option);
	}
	auto add = [&](CSOCompress::Preset preset, std::string_view name, std::string_view description) {
		Option option;
		option.preset = preset;
		option.title = "CSO - " + std::string(name);
		option.description = description;
		options.push_back(option);
	};
	add(CSOCompress::Preset::Best, ga->T("Best"), ga->T("PackDescCSOBest", "libdeflate, 64K blocks. Small and fast."));
	add(CSOCompress::Preset::PSPCompatible, ga->T("PSP Compatible"), ga->T("PackDescCSOPSP", "zlib, 2K blocks. Readable by PSP custom firmware."));
	return options;
}

bool CanStart() {
	return !IsBusy() && !PSP_IsInited();
}

bool Start(const Path &gamePath, const Option &option, bool keepOriginal) {
	auto ga = GetI18NCategory(I18NCat::GAME);
	if (!CanStart()) {
		return false;
	}

	auto job = std::make_unique<Job>();
	job->gamePath = gamePath;
	const Container from = DetectContainer(gamePath, &job->file);
	if (from == Container::None || (option.toISO && from == Container::ISO)) {
		return false;
	}

	job->toISO = option.toISO;
	job->preset = option.preset;
	job->replace = !keepOriginal;
	job->finalOutput = FinalOutput(gamePath, job->file, from, option);
	if (job->finalOutput == job->file && job->replace) {
		// Recompressing a CSO in place: write beside it, and swap it in when done.
		const std::string ext = job->file.GetFileExtension();
		std::string name = job->file.GetFilename();
		name.resize(name.size() - ext.size());
		job->writeOutput = job->file.NavigateUp() / (name + ".ppsspp-tmp" + ext);
		// Our own leftover, from a crash or a kill. The compressor never overwrites.
		if (File::Exists(job->writeOutput)) {
			File::Delete(job->writeOutput);
		}
	} else {
		// The kept original's name, or another file that's in the way.
		if (job->finalOutput == job->file || File::Exists(job->finalOutput)) {
			const Path taken = job->finalOutput;
			job->finalOutput = NumberedName(taken);
			if (job->finalOutput.empty()) {
				g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(ga->T("PackOutputExists", "Output file already exists")) + ": " + taken.GetFilename(), 5.0f);
				return false;
			}
		}
		job->writeOutput = job->finalOutput;
	}

	INFO_LOG(Log::Loader, "GamePacker: %s -> %s%s", job->file.c_str(), job->finalOutput.c_str(), job->replace ? " (replacing it)" : "");
	Job *raw = job.get();
	g_job = std::move(job);
	g_job->startTime = time_now_d();
	g_job->thread = std::thread(RunJob, raw);
	return true;
}

bool IsBusy() {
	return g_job != nullptr;
}

bool IsPacking(const Path &gamePath) {
	return g_job && g_job->gamePath == gamePath;
}

void Abort() {
	if (g_job) {
		g_job->cancel = true;
	}
}

bool GetStatus(Status *status) {
	if (!g_job) {
		return false;
	}
	auto ga = GetI18NCategory(I18NCat::GAME);
	const Job &job = *g_job;

	status->gamePath = job.gamePath;
	status->label = std::string(job.toISO ? ga->T("Extracting") : ga->T("Compressing")) + " " + job.file.GetFilename();
	status->progress = job.done ? 1.0f : job.progress.load();
	status->aborting = job.cancel;

	const double elapsed = time_now_d() - job.startTime;
	status->etaSeconds = -1.0;
	// The first moments say little.
	if (status->progress > 0.01f && elapsed > 2.0) {
		status->etaSeconds = elapsed * (1.0 - status->progress) / status->progress;
	}
	return true;
}

int Generation() {
	return g_generation;
}

Result LastResult() {
	return g_lastResult;
}

void UpdateOnUIThread() {
	if (!g_job || !g_job->done) {
		return;
	}
	g_job->thread.join();
	std::unique_ptr<Job> job = std::move(g_job);

	auto ga = GetI18NCategory(I18NCat::GAME);
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	Result result;
	result.oldPath = job->gamePath;
	result.newPath = job->gamePath;
	result.success = job->success;

	if (job->success) {
		const uint64_t before = File::GetFileSize(job->file);
		const uint64_t after = File::GetFileSize(job->writeOutput);
		std::string problem;
		Path written = job->finalOutput;  // What the new file is called in the end.

		if (job->replace) {
			// Let go of everything that might hold the original open, or Windows won't delete it.
			g_BackgroundAudio.SetGame(Path());
			g_gameInfoCache->Clear();

			if (File::Delete(job->file)) {
				if (job->writeOutput != job->finalOutput && !File::Rename(job->writeOutput, job->finalOutput)) {
					// The original is gone, so keep the converted game under its temporary name.
					problem = std::string(ga->T("PackRenameFailed", "Could not rename the new file"));
					result.newPath = job->writeOutput;
					written = job->writeOutput;
				} else {
					result.newPath = job->finalOutput;
				}
			} else if (job->writeOutput != job->finalOutput) {
				// Same name as the original, which is still there.
				File::Delete(job->writeOutput);
				problem = std::string(ga->T("PackReplaceFailed", "Could not replace the original"));
				result.success = false;
			} else {
				problem = std::string(ga->T("PackDeleteFailed", "Could not delete the original"));
				result.newPath = job->finalOutput;
			}

			if (result.newPath != result.oldPath && g_recentFiles.ContainsFile(result.oldPath.ToString())) {
				g_recentFiles.Remove(result.oldPath.ToString());
				g_recentFiles.Add(result.newPath.ToString());
			}
		}

		if (result.success) {
			const std::string text = std::string(sy->T("Done")) + ": " + written.GetFilename();
			std::string sizes = NiceSizeFormat(before) + " -> " + NiceSizeFormat(after);
			if (before > 0) {
				sizes += StringFromFormat(" (%.2f%%)", 100.0 * (double)after / (double)before);
			}
			if (!problem.empty()) {
				sizes += "\n" + problem;
			}
			g_OSD.Show(problem.empty() ? OSDType::MESSAGE_SUCCESS : OSDType::MESSAGE_WARNING, text, sizes, 6.0f);
		} else {
			g_OSD.Show(OSDType::MESSAGE_ERROR, problem, 6.0f);
		}
	} else if (job->cancelled) {
		g_OSD.Show(OSDType::MESSAGE_INFO, ga->T("Compression cancelled"), 3.0f);
	} else {
		g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(sy->T("Failed")) + ": " + job->file.GetFilename(), job->error, 8.0f);
	}

	g_lastResult = result;
	g_generation++;
}

void Shutdown() {
	if (!g_job) {
		return;
	}
	g_job->cancel = true;
	if (g_job->thread.joinable()) {
		g_job->thread.join();
	}
	g_job.reset();
}

}  // namespace GamePacker

namespace {

// A preset in the list: a radio mark, the name, and a line about it.
class PackOptionView : public UI::ClickableItem {
public:
	PackOptionView(std::string_view title, std::string_view description, UI::LayoutParams *layoutParams = nullptr)
		: UI::ClickableItem(layoutParams), title_(title), description_(description) {}

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override {
		w = 300.0f;
		h = 70.0f;
	}

	void Draw(UIContext &dc) override {
		UI::Style style = dc.GetTheme().itemStyle;
		if (HasFocus()) style = dc.GetTheme().itemFocusedStyle;
		if (down_) style = dc.GetTheme().itemDownStyle;
		if (!IsEnabled()) style = dc.GetTheme().itemDisabledStyle;
		DrawBG(dc, style, dc.GetTheme().choiceRoundRadius);

		const float iconX = bounds_.x + 30.0f;
		dc.Draw()->DrawImage(ImageID(checked_ ? "I_RADIO_SELECTED" : "I_RADIO_EMPTY"), iconX, bounds_.centerY(), 1.0f, style.fgColor, ALIGN_CENTER);

		const float textX = bounds_.x + 60.0f;
		const float textW = bounds_.w - 70.0f;
		dc.SetFontStyle(dc.GetTheme().uiFont);
		dc.DrawTextRectSqueeze(title_, Bounds(textX, bounds_.y + 6.0f, textW, 32.0f), style.fgColor, ALIGN_VCENTER);
		dc.SetFontScale(0.7f, 0.7f);
		dc.DrawTextRectSqueeze(description_, Bounds(textX, bounds_.y + 36.0f, textW, 28.0f), alphaMul(style.fgColor, 0.75f), ALIGN_VCENTER);
		dc.SetFontScale(1.0f, 1.0f);
	}

	std::string DescribeText() const override {
		return title_ + ", " + description_;
	}

	void SetChecked(bool checked) { checked_ = checked; }

private:
	std::string title_;
	std::string description_;
	bool checked_ = false;
};

}  // namespace

GamePackPopupScreen::GamePackPopupScreen(const Path &gamePath, GamePacker::Container from)
	: UI::PopupScreen(GetI18NCategory(I18NCat::GAME)->T("Compress game"), GetI18NCategory(I18NCat::DIALOG)->T("Start"), GetI18NCategory(I18NCat::DIALOG)->T("Cancel")), gamePath_(gamePath) {
	options_ = GamePacker::ListOptions(from);

	// Start from the preset used last. Extracting is listed first.
	if (from == GamePacker::Container::ISO) {
		for (size_t i = 0; i < options_.size(); i++) {
			if (!options_[i].toISO && (int)options_[i].preset == g_Config.iCSOPreset) {
				selected_ = (int)i;
				break;
			}
		}
	}
	keepOriginal_ = g_Config.bPackKeepOriginal;
}

void GamePackPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto ga = GetI18NCategory(I18NCat::GAME);

	choices_.clear();
	LinearLayout *list = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	list->SetSpacing(0.0f);
	for (size_t i = 0; i < options_.size(); i++) {
		PackOptionView *view = list->Add(new PackOptionView(options_[i].title, options_[i].description));
		view->OnClick.Add([this, i](UI::EventParams &) {
			Select((int)i);
		});
		choices_.push_back(view);
	}

	parent->Add(new CheckBox(&keepOriginal_, ga->T("Keep the original file")));

	if (!options_.empty()) {
		Select(std::clamp(selected_, 0, (int)options_.size() - 1));
	}
}

void GamePackPopupScreen::Select(int index) {
	selected_ = index;
	for (size_t i = 0; i < choices_.size(); i++) {
		static_cast<PackOptionView *>(choices_[i])->SetChecked((int)i == index);
	}
}

void GamePackPopupScreen::OnCompleted(DialogResult result) {
	if (result != DR_OK || selected_ < 0 || selected_ >= (int)options_.size()) {
		return;
	}
	const GamePacker::Option &option = options_[selected_];
	if (!option.toISO) {
		g_Config.iCSOPreset = (int)option.preset;
	}
	g_Config.bPackKeepOriginal = keepOriginal_;
	GamePacker::Start(gamePath_, option, keepOriginal_);
}
