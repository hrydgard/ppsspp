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
#include <mutex>
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
#include "UI/UnPackScreen.h"

namespace GamePacker {
namespace {

const ImageFormat ALL_FORMATS[] = { ImageFormat::CHD, ImageFormat::CSO, ImageFormat::PBP };

// One library call: compress an ISO, or decompress an image back to one.
struct Step {
	bool compress = true;
	ImageFormat format = ImageFormat::CHD;
	int preset = 0;
	Path input;
	Path output;
};

struct Job {
	Path gamePath;     // As the game list knows it, maybe a PSP/GAME/<folder>.
	Path file;         // The image itself.
	Path finalOutput;  // Where the converted game ends up.
	Path writeOutput;  // What the last step writes: finalOutput, or a temporary name when that's the original's.
	Path tempISO;      // In between the steps of a repack, else empty.
	bool replace = false;
	std::vector<Step> steps;

	// A repack keeps the ISO in memory instead of tempISO, when both libraries can and it fits.
	std::atomic<bool> useMemory{false};
	MemoryImage memory;

	std::atomic<int> step{0};
	std::atomic<float> progress{0.0f};
	std::atomic<double> stepStart{0.0};
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

ImageFormat FormatOf(Container container) {
	switch (container) {
	case Container::CSO: return ImageFormat::CSO;
	case Container::PBP: return ImageFormat::PBP;
	default: return ImageFormat::CHD;
	}
}

Container ContainerOf(ImageFormat format) {
	switch (format) {
	case ImageFormat::CSO: return Container::CSO;
	case ImageFormat::PBP: return Container::PBP;
	default: return Container::CHD;
	}
}

bool HasExtension(const std::vector<std::string> &exts, const std::string &ext) {
	return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

// With memory, a decompressing step writes the ISO there instead of to step.output, and a
// compressing one reads it from there instead of from step.input.
bool RunStep(const Step &step, MemoryImage *memory, const ImageProgressFunc &progress, std::string *error, bool *cancelled) {
	switch (step.format) {
	case ImageFormat::CHD:
	{
		if (!step.compress) {
			return memory ? CHDModern::ExtractToMemory(step.input, memory, progress, error, cancelled) : CHDModern::Extract(step.input, step.output, progress, error, cancelled);
		}
		const uint32_t hunk = step.preset == 0 ? 32768 : 2048;
		const CHDModern::Codecs codecs = step.preset == 2 ? CHDModern::Codecs::Zstd : CHDModern::Codecs::Default;
		return memory ? CHDModern::CreateDVDFromMemory(*memory, step.output, hunk, codecs, progress, error, cancelled) : CHDModern::CreateDVD(step.input, step.output, hunk, codecs, progress, error, cancelled);
	}
	case ImageFormat::PBP:
	{
		if (!step.compress) {
			return memory ? IPBP::PbpToIsoMemory(step.input, memory, progress, error, cancelled) : IPBP::PbpToIso(step.input, step.output, progress, error, cancelled);
		}
		IPBP::Compression compression = IPBP::Compression::LZRC;
		if (step.preset == 0) {
			compression = IPBP::Compression::Max;
		} else if (step.preset == 2) {
			compression = IPBP::Compression::None;
		}
		return memory ? IPBP::IsoToPbpFromMemory(*memory, step.output, compression, progress, error, cancelled) : IPBP::IsoToPbp(step.input, step.output, compression, progress, error, cancelled);
	}
	case ImageFormat::CSO:
	default:
	{
		if (!step.compress) {
			return memory ? MaxCSO::DecompressToMemory(step.input, memory, progress, error, cancelled) : MaxCSO::Decompress(step.input, step.output, progress, error, cancelled);
		}
		const MaxCSO::Preset preset = (MaxCSO::Preset)std::clamp(step.preset, 0, 2);
		return memory ? MaxCSO::CompressFromMemory(*memory, step.output, preset, progress, error, cancelled) : MaxCSO::Compress(step.input, step.output, preset, progress, error, cancelled);
	}
	}
}

void RunJob(Job *job) {
	SetCurrentThreadName("GamePacker");

	bool success = true;
	for (size_t i = 0; i < job->steps.size(); i++) {
		const Step &step = job->steps[i];
		job->step = (int)i;
		job->progress = 0.0f;
		job->stepStart = time_now_d();
		INFO_LOG(Log::Loader, "GamePacker: %s %s -> %s", step.compress ? "Compressing" : "Decompressing", step.input.c_str(), step.output.c_str());

		auto progress = [job](float p) {
			job->progress = p;
			return !job->cancel;
		};
		std::string error;
		bool cancelled = false;
		const bool memory = job->useMemory;
		if (job->cancel) {
			cancelled = true;
		} else if (RunStep(step, memory ? &job->memory : nullptr, progress, &error, &cancelled)) {
			if (memory && step.compress) {
				MemoryImage().swap(job->memory);
			}
			continue;
		}
		if (memory && !step.compress && !cancelled) {
			// Too big for the memory left, most likely: go through tempISO instead.
			WARN_LOG(Log::Loader, "GamePacker: Extracting %s into memory failed (%s), using %s", step.input.c_str(), error.c_str(), job->tempISO.c_str());
			job->useMemory = false;
			i--;
			continue;
		}
		success = false;
		job->cancelled = cancelled;
		job->error = error;
		if (!cancelled) {
			ERROR_LOG(Log::Loader, "GamePacker: Failed on %s: %s", step.input.c_str(), error.c_str());
		}
		break;
	}

	MemoryImage().swap(job->memory);
	if (!job->tempISO.empty() && File::Exists(job->tempISO)) {
		File::Delete(job->tempISO);
	}
	if (!success && File::Exists(job->writeOutput)) {
		File::Delete(job->writeOutput);
	}
	job->success = success;
	job->done = true;
}

// "<name>" for "<name>.<ext>", or for an EBOOT.PBP its disc id (as the iPBP tool names its ISOs).
std::string OutputBaseName(const Path &file, Container container) {
	std::string name = file.GetFilename();
	const std::string ext = file.GetFileExtension();
	if (ext.size() <= name.size()) {
		name.resize(name.size() - ext.size());
	}
	if (container == Container::PBP && equalsNoCase(file.GetFilename(), "EBOOT.PBP")) {
		std::string discId, error;
		if (IPBP::ReadDiscId(file, &discId, &error)) {
			discId = SanitizeString(discId, StringRestriction::FileName);
			if (!discId.empty()) {
				return discId;
			}
		}
	}
	return name;
}

// Where an option puts its result.
Path FinalOutput(const Path &file, Container from, const Option &option) {
	const Container to = option.toISO ? Container::ISO : ContainerOf(option.format);
	if (to == from) {
		return file;
	}
	const char *ext = option.toISO ? ".iso" : ImageCompressors::OutputExtension(option.format);
	return file.NavigateUp() / (OutputBaseName(file, from) + ext);
}

}  // namespace

Container DetectContainer(const Path &gamePath, Path *file) {
	if (gamePath.Type() != PathType::NATIVE) {
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
	} else {
		for (ImageFormat format : ALL_FORMATS) {
			if (HasExtension(ImageCompressors::CompressedExtensions(format), ext)) {
				container = ContainerOf(format);
				break;
			}
		}
	}
	if (container == Container::None) {
		return Container::None;
	}
	if (container == Container::ISO) {
		bool any = false;
		for (ImageFormat format : ALL_FORMATS) {
			any = any || ImageCompressors::IsAvailable(format);
		}
		if (!any) {
			return Container::None;
		}
	} else if (!ImageCompressors::IsAvailable(FormatOf(container))) {
		// Can't read it back.
		return Container::None;
	}
	if (file) {
		*file = image;
	}
	return container;
}

std::vector<Option> ListOptions(Container from) {
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto ga = GetI18NCategory(I18NCat::GAME);

	std::vector<Option> options;
	if (from == Container::None) {
		return options;
	}
	if (from != Container::ISO) {
		Option option;
		option.toISO = true;
		option.title = std::string(ga->T("Extract to ISO"));
		option.description = std::string(ga->T("PackDescISO", "Uncompressed disc image. Largest, reads fastest."));
		options.push_back(option);
	}

	auto add = [&](ImageFormat format, int preset, std::string_view name, std::string_view description) {
		Option option;
		option.format = format;
		option.preset = preset;
		static const char *const formatNames[] = { "CHD", "CSO", "PBP" };  // Indexed by ImageFormat.
		option.title = std::string(formatNames[(int)format]) + " - " + std::string(name);
		option.description = description;
		options.push_back(option);
	};

	// Indexed like iCHDPreset, iCSOPreset and iPBPPreset, in the order the Un/Pack screen lists them.
	if (ImageCompressors::IsAvailable(ImageFormat::CHD)) {
		add(ImageFormat::CHD, 0, sy->T("Large Hunk"), ga->T("PackDescCHDLarge", "32K hunks. Smallest for most games."));
		add(ImageFormat::CHD, 1, sy->T("Small Hunk"), ga->T("PackDescCHDSmall", "2K hunks. Smallest when a game repeats a lot of data."));
		add(ImageFormat::CHD, 2, sy->T("Quick Access"), ga->T("PackDescCHDQuick", "Zstandard, 2K hunks. Bigger, but fastest to read while playing."));
	}
	if (ImageCompressors::IsAvailable(ImageFormat::CSO)) {
		add(ImageFormat::CSO, 0, sy->T("Max"), ga->T("PackDescCSOMax", "Zopfli, 256K blocks. Smallest CSO, slow to compress."));
		add(ImageFormat::CSO, 1, sy->T("Great"), ga->T("PackDescCSOGreat", "libdeflate, 64K blocks. Nearly as small, much faster."));
		add(ImageFormat::CSO, 2, sy->T("PSP Compatible"), ga->T("PackDescCSOPSP", "zlib, 2K blocks. Readable by PSP custom firmware."));
	}
	if (ImageCompressors::IsAvailable(ImageFormat::PBP)) {
		if (IPBP::SupportsMaxCompression()) {
			add(ImageFormat::PBP, 0, sy->T("Max"), ga->T("PackDescPBPMax", "Smallest LZRC EBOOT."));
		}
		add(ImageFormat::PBP, 1, sy->T("PSP Compatible"), ga->T("PackDescPBPPSP", "LZRC like sign_np. What real PSPs are known to read."));
		add(ImageFormat::PBP, 2, sy->T("Uncompressed"), ga->T("PackDescPBPNone", "Blocks stored as they are. Largest, fastest."));
	}
	return options;
}

bool MustReplace(const Path &gamePath, const Option &option) {
	Path file;
	const Container from = DetectContainer(gamePath, &file);
	return from != Container::None && FinalOutput(file, from, option) == file;
}

bool CanStart() {
	return !IsBusy() && !IsUnPackJobRunning() && !PSP_IsInited();
}

bool Start(const Path &gamePath, const Option &option, bool replaceOriginal) {
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	if (!CanStart()) {
		return false;
	}

	auto job = std::make_unique<Job>();
	job->gamePath = gamePath;
	const Container from = DetectContainer(gamePath, &job->file);
	if (from == Container::None || (option.toISO && from == Container::ISO)) {
		return false;
	}

	const Path dir = job->file.NavigateUp();
	job->finalOutput = FinalOutput(job->file, from, option);
	job->replace = replaceOriginal || job->finalOutput == job->file;
	if (job->finalOutput == job->file) {
		const std::string ext = job->file.GetFileExtension();
		std::string name = job->file.GetFilename();
		name.resize(name.size() - ext.size());
		job->writeOutput = dir / (name + ".ppsspp-tmp" + ext);
	} else {
		job->writeOutput = job->finalOutput;
		if (File::Exists(job->finalOutput)) {
			g_OSD.Show(OSDType::MESSAGE_ERROR, std::string(sy->T("UnPackOutputExists", "Output file already exists")) + ": " + job->finalOutput.GetFilename(), 5.0f);
			return false;
		}
	}

	if (option.toISO) {
		job->steps.push_back({ false, FormatOf(from), 0, job->file, job->writeOutput });
	} else if (from == Container::ISO) {
		job->steps.push_back({ true, option.format, option.preset, job->file, job->writeOutput });
	} else {
		job->tempISO = dir / (OutputBaseName(job->file, from) + ".ppsspp-tmp.iso");
		job->steps.push_back({ false, FormatOf(from), 0, job->file, job->tempISO });
		job->steps.push_back({ true, option.format, option.preset, job->tempISO, job->writeOutput });
		job->useMemory = ImageCompressors::SupportsMemory(FormatOf(from)) && ImageCompressors::SupportsMemory(option.format);
	}

	// Our own leftovers, from a crash or a kill. The converters never overwrite.
	for (const Path &temp : { job->tempISO, job->writeOutput }) {
		if (!temp.empty() && temp != job->finalOutput && File::Exists(temp)) {
			File::Delete(temp);
		}
	}

	INFO_LOG(Log::Loader, "GamePacker: %s -> %s%s", job->file.c_str(), job->finalOutput.c_str(), job->replace ? " (replacing it)" : "");
	Job *raw = job.get();
	g_job = std::move(job);
	g_job->stepStart = time_now_d();
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
	const int stepIndex = std::clamp(job.step.load(), 0, (int)job.steps.size() - 1);
	const Step &step = job.steps[stepIndex];

	status->gamePath = job.gamePath;
	status->label = std::string(step.compress ? ga->T("Packing") : ga->T("Extracting")) + " " + job.file.GetFilename();
	if (job.steps.size() > 1) {
		status->label += StringFromFormat(" (%d/%d%s)", stepIndex + 1, (int)job.steps.size(), job.useMemory ? ", RAM" : "");
	}
	status->progress = job.done ? 1.0f : job.progress.load();
	status->aborting = job.cancel;

	const double elapsed = time_now_d() - job.stepStart;
	status->etaSeconds = -1.0;
	// The first moments say little: the libraries scan before they write.
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
		uint64_t after = File::GetFileSize(job->writeOutput);
		std::string problem;

		if (job->replace) {
			// Let go of everything that might hold the original open, or Windows won't delete it.
			g_BackgroundAudio.SetGame(Path());
			g_gameInfoCache->Clear();

			if (File::Delete(job->file)) {
				if (job->writeOutput != job->finalOutput && !File::Rename(job->writeOutput, job->finalOutput)) {
					// The original is gone, so keep the converted game under its temporary name.
					problem = std::string(ga->T("PackRenameFailed", "Could not rename the new file"));
					result.newPath = job->writeOutput;
				} else if (File::IsDirectory(job->gamePath) && job->finalOutput.NavigateUp() == job->gamePath && equalsNoCase(job->finalOutput.GetFilename(), "EBOOT.PBP")) {
					// Repacked a PSP/GAME EBOOT: the folder is still the game.
					result.newPath = job->gamePath;
				} else {
					result.newPath = job->finalOutput;
				}
			} else if (job->writeOutput != job->finalOutput) {
				// Same name as the original, which is still there.
				File::Delete(job->writeOutput);
				problem = std::string(ga->T("PackReplaceFailed", "Could not replace the original"));
				result.success = false;
			} else {
				problem = std::string(sy->T("UnPackDeleteFailed", "Could not delete the original"));
				result.newPath = job->finalOutput;
			}

			if (result.newPath != result.oldPath && g_recentFiles.ContainsFile(result.oldPath.ToString())) {
				g_recentFiles.Remove(result.oldPath.ToString());
				g_recentFiles.Add(result.newPath.ToString());
			}
		}

		if (result.success) {
			std::string text = std::string(sy->T("Done")) + ": " + job->finalOutput.GetFilename() + "\n" + NiceSizeFormat(before) + " -> " + NiceSizeFormat(after);
			if (before > 0) {
				text += StringFromFormat(" (%.2f%%)", 100.0 * (double)after / (double)before);
			}
			g_OSD.Show(problem.empty() ? OSDType::MESSAGE_SUCCESS : OSDType::MESSAGE_WARNING, text, problem, 6.0f);
		} else {
			g_OSD.Show(OSDType::MESSAGE_ERROR, problem, 6.0f);
		}
	} else if (job->cancelled) {
		g_OSD.Show(OSDType::MESSAGE_INFO, ga->T("Packing aborted"), 3.0f);
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

std::string_view PopupTitle(GamePacker::Container from) {
	auto ga = GetI18NCategory(I18NCat::GAME);
	return from == GamePacker::Container::ISO ? ga->T("Pack") : ga->T("Extract / Repack");
}

std::string_view PopupButton(GamePacker::Container from) {
	auto ga = GetI18NCategory(I18NCat::GAME);
	auto di = GetI18NCategory(I18NCat::DIALOG);
	return from == GamePacker::Container::ISO ? ga->T("Pack") : di->T("Start");
}

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
		DrawBG(dc, style);

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

GamePackPopupScreen::GamePackPopupScreen(const Path &gamePath, GamePacker::Container from, std::function<void()> onStarted)
	: UI::PopupScreen(PopupTitle(from), PopupButton(from), GetI18NCategory(I18NCat::DIALOG)->T("Cancel")),
	  gamePath_(gamePath), onStarted_(std::move(onStarted)) {
	options_ = GamePacker::ListOptions(from);

	// Start from what was used last, here or on the Un/Pack screen. Extracting is listed first.
	if (from == GamePacker::Container::ISO) {
		const int formatPresets[] = { g_Config.iCHDPreset, g_Config.iCSOPreset, g_Config.iPBPPreset };  // Indexed by ImageFormat.
		const int format = std::clamp(g_Config.iUnPackFormat, 0, 2);
		for (size_t i = 0; i < options_.size(); i++) {
			if ((int)options_[i].format == format && options_[i].preset == formatPresets[format]) {
				selected_ = (int)i;
				break;
			}
		}
	}
	replaceWanted_ = g_Config.bUnPackDeleteOriginals;
	replace_ = replaceWanted_;
}

void GamePackPopupScreen::CreatePopupContents(UI::ViewGroup *parent) {
	using namespace UI;

	auto ga = GetI18NCategory(I18NCat::GAME);

	choices_.clear();
	ScrollView *scroll = parent->Add(new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FillVertical() ? 0.0f : WRAP_CONTENT, 1.0f)));
	LinearLayout *list = scroll->Add(new LinearLayout(ORIENT_VERTICAL, new LayoutParams(FILL_PARENT, WRAP_CONTENT)));
	list->SetSpacing(0.0f);
	for (size_t i = 0; i < options_.size(); i++) {
		PackOptionView *view = list->Add(new PackOptionView(options_[i].title, options_[i].description));
		view->OnClick.Add([this, i](UI::EventParams &) {
			Select((int)i);
		});
		choices_.push_back(view);
	}

	replaceCheckBox_ = parent->Add(new CheckBox(&replace_, ga->T("Replace original when complete")));
	replaceCheckBox_->OnClick.Add([this](UI::EventParams &) {
		replaceWanted_ = replace_;
	});

	if (!options_.empty()) {
		Select(std::clamp(selected_, 0, (int)options_.size() - 1));
	}
}

void GamePackPopupScreen::Select(int index) {
	selected_ = index;
	for (size_t i = 0; i < choices_.size(); i++) {
		static_cast<PackOptionView *>(choices_[i])->SetChecked((int)i == index);
	}
	// Repacking into the same format writes the game's own file name, so the original has to go.
	const bool forced = GamePacker::MustReplace(gamePath_, options_[index]);
	replace_ = forced || replaceWanted_;
	if (replaceCheckBox_) {
		replaceCheckBox_->SetEnabled(!forced);
	}
}

void GamePackPopupScreen::OnCompleted(DialogResult result) {
	if (result != DR_OK || selected_ < 0 || selected_ >= (int)options_.size()) {
		return;
	}
	const GamePacker::Option &option = options_[selected_];
	if (!option.toISO) {
		g_Config.iUnPackFormat = (int)option.format;
		switch (option.format) {
		case ImageFormat::CHD: g_Config.iCHDPreset = option.preset; break;
		case ImageFormat::CSO: g_Config.iCSOPreset = option.preset; break;
		case ImageFormat::PBP: g_Config.iPBPPreset = option.preset; break;
		}
	}
	if (!GamePacker::MustReplace(gamePath_, option)) {
		g_Config.bUnPackDeleteOriginals = replace_;
	}
	if (GamePacker::Start(gamePath_, option, replace_) && onStarted_) {
		onStarted_();
	}
}
