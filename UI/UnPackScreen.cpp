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
#include <vector>

#include "Common/Common.h"
#include "Common/Data/Text/I18n.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/File/DirListing.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/UI/PopupScreens.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Util/ImageCompressors.h"
#include "Core/Util/PathUtil.h"

#include "UI/UnPackScreen.h"

namespace {

enum class ItemStatus {
	Pending,
	Running,
	Done,
	Skipped,
	Failed,
	Cancelled,
};

struct JobItem {
	Path input;
	Path output;
	ItemStatus status = ItemStatus::Pending;
	std::string message;
	uint64_t inputSize = 0;
	uint64_t outputSize = 0;
};

// What a job does, fixed when it starts.
struct JobSettings {
	ImageFormat format = ImageFormat::CHD;
	bool compress = true;
	MaxCSO::Mode csoMode = MaxCSO::Mode::Standard;
	CHDModern::Codecs chdCodecs = CHDModern::Codecs::Default;
	uint32_t chdHunkBytes = 2048;
	bool recursive = false;
	bool deleteOriginals = false;
};

// Shared between the worker thread and whichever UnPackScreen is showing. Lives in a global so
// that leaving the screen doesn't lose track of a running job - coming back shows it again.
struct JobState {
	JobSettings settings;

	std::atomic<bool> scanning{true};
	std::atomic<bool> done{false};
	std::atomic<bool> cancel{false};
	std::atomic<float> fileProgress{0.0f};

	std::mutex mutex;
	// Guarded by mutex. Filled once scanning ends, after that only statuses change.
	std::vector<JobItem> items;
	// Order in which items finished, to show results as they come in. Guarded by mutex.
	std::vector<size_t> finished;
	int current = -1;
};

std::shared_ptr<JobState> g_job;

bool IsJobRunning() {
	return g_job && !g_job->done;
}

ImageFormat SelectedFormat() {
	return g_Config.iUnPackFormat == (int)ImageFormat::CSO ? ImageFormat::CSO : ImageFormat::CHD;
}

const std::vector<std::string> &InputExtensions(const JobSettings &settings) {
	static const std::vector<std::string> iso = { ".iso" };
	return settings.compress ? iso : ImageCompressors::CompressedExtensions(settings.format);
}

void ScanFolder(const Path &folder, const JobSettings &settings, std::vector<Path> *out) {
	std::string filter;
	for (const std::string &ext : InputExtensions(settings)) {
		if (!filter.empty()) {
			filter += ":";
		}
		filter += ext.substr(1);
	}
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(folder, &files, filter.c_str());
	for (const auto &file : files) {
		if (file.isDirectory) {
			if (settings.recursive) {
				ScanFolder(file.fullName, settings, out);
			}
		} else {
			out->push_back(file.fullName);
		}
	}
}

void RunJob(std::shared_ptr<JobState> job, Path source, bool folder) {
	SetCurrentThreadName("UnPack");
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	const JobSettings &settings = job->settings;
	std::vector<Path> inputs;
	if (folder) {
		ScanFolder(source, settings, &inputs);
		std::sort(inputs.begin(), inputs.end());
	} else {
		inputs.push_back(source);
	}

	const char *outputExt = settings.compress ? ImageCompressors::OutputExtension(settings.format) : ".iso";
	{
		std::lock_guard<std::mutex> guard(job->mutex);
		for (const Path &input : inputs) {
			JobItem item;
			item.input = input;
			item.output = input.WithReplacedExtension(outputExt);
			job->items.push_back(std::move(item));
		}
	}
	job->scanning = false;

	for (size_t i = 0; i < inputs.size(); i++) {
		JobItem item;
		{
			std::lock_guard<std::mutex> guard(job->mutex);
			item = job->items[i];
		}

		auto finish = [&](ItemStatus status, std::string_view message) {
			std::lock_guard<std::mutex> guard(job->mutex);
			job->items[i].status = status;
			job->items[i].message = message;
			job->items[i].inputSize = item.inputSize;
			job->items[i].outputSize = item.outputSize;
			job->finished.push_back(i);
		};

		if (job->cancel) {
			finish(ItemStatus::Cancelled, "");
			continue;
		}

		item.inputSize = File::GetFileSize(item.input);
		const std::vector<std::string> &exts = InputExtensions(settings);
		if (std::find(exts.begin(), exts.end(), item.input.GetFileExtension()) == exts.end()) {
			if (settings.compress) {
				finish(ItemStatus::Failed, sy->T("UnPackNotISO", "Not an ISO file"));
			} else if (settings.format == ImageFormat::CHD) {
				finish(ItemStatus::Failed, sy->T("UnPackNotCHD", "Not a CHD file"));
			} else {
				finish(ItemStatus::Failed, sy->T("UnPackNotCSO", "Not a CSO, ZSO or DAX file"));
			}
			continue;
		}
		if (File::Exists(item.output)) {
			finish(ItemStatus::Skipped, sy->T("UnPackOutputExists", "Output file already exists"));
			continue;
		}

		{
			std::lock_guard<std::mutex> guard(job->mutex);
			job->items[i].status = ItemStatus::Running;
			job->current = (int)i;
		}
		job->fileProgress = 0.0f;

		INFO_LOG(Log::Loader, "UnPack: %s %s -> %s", settings.compress ? "Compressing" : "Decompressing", item.input.c_str(), item.output.c_str());
		auto progress = [job](float p) {
			job->fileProgress = p;
			return !job->cancel;
		};
		std::string error;
		bool cancelled = false;
		bool success;
		if (settings.format == ImageFormat::CHD) {
			if (settings.compress) {
				success = CHDModern::CreateDVD(item.input, item.output, settings.chdHunkBytes, settings.chdCodecs, progress, &error, &cancelled);
			} else {
				success = CHDModern::Extract(item.input, item.output, progress, &error, &cancelled);
			}
		} else {
			success = MaxCSO::Process(item.input, item.output, settings.compress ? settings.csoMode : MaxCSO::Mode::Decompress, progress, &error);
		}

		if (cancelled) {
			finish(ItemStatus::Cancelled, "");
			continue;
		}
		if (!success) {
			ERROR_LOG(Log::Loader, "UnPack: Failed on %s: %s", item.input.c_str(), error.c_str());
			finish(ItemStatus::Failed, error);
			continue;
		}

		item.outputSize = File::GetFileSize(item.output);
		std::string message;
		if (settings.deleteOriginals && item.outputSize > 0) {
			if (!File::Delete(item.input)) {
				message = sy->T("UnPackDeleteFailed", "Could not delete the original");
			}
		}
		finish(ItemStatus::Done, message);
	}

	{
		std::lock_guard<std::mutex> guard(job->mutex);
		job->current = -1;
	}
	job->done = true;
}

void StartJob(const Path &source, bool compress, bool folder) {
	if (IsJobRunning()) {
		return;
	}

	JobSettings settings;
	settings.format = SelectedFormat();
	settings.compress = compress;
	switch (g_Config.iMaxCSOPreset) {
	case 0: settings.csoMode = MaxCSO::Mode::Fast; break;
	case 2: settings.csoMode = MaxCSO::Mode::Maximum; break;
	default: settings.csoMode = MaxCSO::Mode::Standard; break;
	}
	settings.chdCodecs = g_Config.iCHDCompression == 1 ? CHDModern::Codecs::Zstd : CHDModern::Codecs::Default;
	settings.chdHunkBytes = g_Config.iCHDHunkSize == 1 ? 4096 : 2048;
	settings.recursive = g_Config.bUnPackIncludeSubfolders;
	settings.deleteOriginals = g_Config.bUnPackDeleteOriginals;

	auto job = std::make_shared<JobState>();
	job->settings = settings;
	g_job = job;
	std::thread(RunJob, job, source, folder).detach();
}

}  // namespace

std::string_view UnPackScreen::GetTitle() const {
	return "Un/Pack";
}

void UnPackScreen::BeforeCreateViews() {
	// With only one of the two libraries there's nothing to choose.
	const bool hasCHD = ImageCompressors::IsAvailable(ImageFormat::CHD);
	const bool hasCSO = ImageCompressors::IsAvailable(ImageFormat::CSO);
	formatLocked_ = hasCHD != hasCSO;
	if (formatLocked_) {
		g_Config.iUnPackFormat = (int)(hasCHD ? ImageFormat::CHD : ImageFormat::CSO);
	} else if (g_Config.iUnPackFormat != (int)ImageFormat::CSO) {
		g_Config.iUnPackFormat = (int)ImageFormat::CHD;
	}
	g_Config.iCHDCompression = std::clamp(g_Config.iCHDCompression, 0, 1);
	g_Config.iCHDHunkSize = std::clamp(g_Config.iCHDHunkSize, 0, 1);
	g_Config.iMaxCSOPreset = std::clamp(g_Config.iMaxCSOPreset, 0, 2);
}

void UnPackScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	LinearLayout *content = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	for (ImageFormat format : { ImageFormat::CHD, ImageFormat::CSO }) {
		if (!ImageCompressors::IsAvailable(format)) {
			continue;
		}
		std::string name = format == ImageFormat::CHD ? "CHDmodern" : "maxcso";
		std::string version = ImageCompressors::Version(format);
		if (!version.empty()) {
			name += " " + version;
		}
		content->Add(new InfoItem(name, GetFriendlyPath(ImageCompressors::LibraryPath(format))));
	}

	content->Add(new ItemHeader(sy->T("Status")));
	statusText_ = content->Add(new TextView(sy->T("UnPackIdle", "Pick a game or a folder to compress or decompress."), ALIGN_LEFT | FLAG_WRAP_TEXT, false));
	fileProgress_ = content->Add(new ProgressBar());
	totalProgress_ = content->Add(new ProgressBar());
	summary_ = content->Add(new NoticeView(NoticeLevel::SUCCESS, "", ""));
	summary_->SetVisibility(V_GONE);

	content->Add(new ItemHeader(sy->T("Results")));
	results_ = content->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	shownResults_ = 0;

	RefreshStatus();
}

void UnPackScreen::CreateSettingsViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	jobLockedViews_.clear();

	static const char *formats[] = { "CHD", "CSO" };
	PopupMultiChoice *formatChoice = parent->Add(new PopupMultiChoice(&g_Config.iUnPackFormat, sy->T("Format"), formats, 0, ARRAY_SIZE(formats), I18NCat::SYSTEM, screenManager()));
	formatChoice->OnChoice.Add([this](UI::EventParams &) {
		// The compression options depend on the format.
		RecreateViews();
	});
	if (formatLocked_) {
		formatChoice->SetEnabled(false);
	} else {
		jobLockedViews_.push_back(formatChoice);
	}

	if (SelectedFormat() == ImageFormat::CHD) {
		static const char *codecs[] = { "Default (LZMA, zlib, Huffman, FLAC)", "Zstandard (faster to load)" };
		jobLockedViews_.push_back(parent->Add(new PopupMultiChoice(&g_Config.iCHDCompression, sy->T("Compression"), codecs, 0, ARRAY_SIZE(codecs), I18NCat::SYSTEM, screenManager())));
		static const char *hunkSizes[] = { "2048 bytes", "4096 bytes" };
		jobLockedViews_.push_back(parent->Add(new PopupMultiChoice(&g_Config.iCHDHunkSize, sy->T("Hunk size"), hunkSizes, 0, ARRAY_SIZE(hunkSizes), I18NCat::SYSTEM, screenManager())));
	} else {
		static const char *presets[] = { "Fast compression", "Standard compression", "Maximum compression (very slow)" };
		jobLockedViews_.push_back(parent->Add(new PopupMultiChoice(&g_Config.iMaxCSOPreset, sy->T("Compression"), presets, 0, ARRAY_SIZE(presets), I18NCat::SYSTEM, screenManager())));
	}

	parent->Add(new CheckBox(&g_Config.bUnPackIncludeSubfolders, sy->T("Include subfolders")));
	parent->Add(new CheckBox(&g_Config.bUnPackDeleteOriginals, sy->T("Delete originals"),
		sy->T("UnPackDeleteOriginalsDesc", "After a file was converted successfully")));

	const bool hasFileBrowser = System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER);
	const bool hasFolderBrowser = System_GetPropertyBool(SYSPROP_HAS_FOLDER_BROWSER);

	auto addAction = [&](std::string_view text, ImageID icon, bool compress, bool folder) {
		Choice *choice = parent->Add(new Choice(text, icon));
		choice->OnClick.Add([this, compress, folder](UI::EventParams &) { BrowseAndStart(compress, folder); });
		jobLockedViews_.push_back(choice);
	};

	parent->Add(new ItemHeader(sy->T("Compress")));
	if (hasFileBrowser) {
		addAction(sy->T("Compress game..."), ImageID("I_FILE"), true, false);
	}
	if (hasFolderBrowser) {
		addAction(sy->T("Compress folder..."), ImageID("I_FOLDER"), true, true);
	}

	parent->Add(new ItemHeader(sy->T("Decompress")));
	if (hasFileBrowser) {
		addAction(sy->T("Decompress game..."), ImageID("I_FILE"), false, false);
	}
	if (hasFolderBrowser) {
		addAction(sy->T("Decompress folder..."), ImageID("I_FOLDER"), false, true);
	}

	cancelChoice_ = parent->Add(new Choice(di->T("Cancel")));
	cancelChoice_->OnClick.Add([](UI::EventParams &) {
		if (g_job) {
			g_job->cancel = true;
		}
	});

	if (!hasFileBrowser && !hasFolderBrowser) {
		parent->Add(new NoticeView(NoticeLevel::WARN, sy->T("UnPackNoBrowser", "No file browser is available on this platform"), ""));
	}
	if (PSP_IsInited()) {
		parent->Add(new NoticeView(NoticeLevel::INFO, sy->T("UnPackNeedsNoGame", "Stop the game to compress or decompress games"), ""));
	}

	RefreshStatus();
}

void UnPackScreen::BrowseAndStart(bool compress, bool folder) {
	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	std::string_view title = compress ? sy->T("Compress") : sy->T("Decompress");
	if (folder) {
		System_BrowseForFolder(GetRequesterToken(), title, g_Config.currentDirectory, [this, compress](std::string_view value, int) {
			StartJob(Path(value), compress, true);
			RefreshStatus();
		});
	} else {
		System_BrowseForFile(GetRequesterToken(), title, BrowseFileType::BOOTABLE, [this, compress](std::string_view value, int) {
			StartJob(Path(value), compress, false);
			RefreshStatus();
		});
	}
}

void UnPackScreen::RefreshStatus() {
	using namespace UI;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	const bool running = IsJobRunning();
	for (View *view : jobLockedViews_) {
		view->SetEnabled(!running && !PSP_IsInited());
	}
	if (cancelChoice_) {
		cancelChoice_->SetVisibility(running ? V_VISIBLE : V_GONE);
		cancelChoice_->SetEnabled(running && !g_job->cancel);
	}

	if (!statusText_) {
		return;
	}

	if (!g_job) {
		fileProgress_->SetVisibility(V_GONE);
		totalProgress_->SetVisibility(V_GONE);
		return;
	}

	std::lock_guard<std::mutex> guard(g_job->mutex);
	const size_t total = g_job->items.size();
	const size_t finished = g_job->finished.size();

	// New results since the last refresh.
	for (; shownResults_ < finished && results_; shownResults_++) {
		const JobItem &item = g_job->items[g_job->finished[shownResults_]];
		std::string right;
		switch (item.status) {
		case ItemStatus::Done:
			right = StringFromFormat("%s -> %s", NiceSizeFormat(item.inputSize).c_str(), NiceSizeFormat(item.outputSize).c_str());
			break;
		case ItemStatus::Skipped: right = std::string(sy->T("Skipped")); break;
		case ItemStatus::Cancelled: right = std::string(sy->T("Cancelled")); break;
		default: right = std::string(sy->T("Failed")); break;
		}
		results_->Add(new InfoItem(item.input.GetFilename(), right));
		if (!item.message.empty()) {
			results_->Add(new TextView(item.message, ALIGN_LEFT | FLAG_WRAP_TEXT, true));
		}
	}

	if (g_job->scanning) {
		statusText_->SetText(sy->T("Looking for games..."));
	} else if (running && g_job->current >= 0) {
		const JobItem &item = g_job->items[g_job->current];
		std::string_view verb = g_job->settings.compress ? sy->T("Compressing") : sy->T("Decompressing");
		std::string text = StringFromFormat("%.*s (%d / %d): %s", STR_VIEW(verb), g_job->current + 1, (int)total, item.input.GetFilename().c_str());
		if (g_job->cancel) {
			// maxcso can't stop in the middle of a file.
			if (g_job->settings.format == ImageFormat::CSO) {
				text += "\n" + std::string(sy->T("UnPackCancellingAfterFile", "Stopping after the current file..."));
			} else {
				text += "\n" + std::string(sy->T("UnPackCancelling", "Stopping..."));
			}
		}
		statusText_->SetText(text);
	} else if (running) {
		statusText_->SetText(sy->T("Working..."));
	} else {
		statusText_->SetText(sy->T("Done"));
	}

	fileProgress_->SetVisibility(running && !g_job->scanning ? V_VISIBLE : V_GONE);
	fileProgress_->SetProgress(g_job->fileProgress);
	totalProgress_->SetVisibility(running && total > 1 ? V_VISIBLE : V_GONE);
	totalProgress_->SetProgress(total > 0 ? (float)finished / (float)total : 0.0f);

	if (g_job->done && summary_) {
		int converted = 0, skipped = 0, failed = 0, cancelled = 0;
		uint64_t before = 0, after = 0;
		for (const JobItem &item : g_job->items) {
			switch (item.status) {
			case ItemStatus::Done:
				converted++;
				before += item.inputSize;
				after += item.outputSize;
				break;
			case ItemStatus::Skipped: skipped++; break;
			case ItemStatus::Cancelled: cancelled++; break;
			default: failed++; break;
			}
		}
		std::string details = StringFromFormat("%s: %d, %s: %d, %s: %d",
			std::string(sy->T("Converted")).c_str(), converted,
			std::string(sy->T("Skipped")).c_str(), skipped,
			std::string(sy->T("Failed")).c_str(), failed);
		if (cancelled > 0) {
			details += StringFromFormat(", %s: %d", std::string(sy->T("Cancelled")).c_str(), cancelled);
		}
		if (converted > 0) {
			details += StringFromFormat("\n%s -> %s", NiceSizeFormat(before).c_str(), NiceSizeFormat(after).c_str());
		}
		if (total == 0) {
			summary_->SetLevelAndText(NoticeLevel::WARN, sy->T("UnPackNothingFound", "No matching games found"));
		} else {
			summary_->SetLevelAndText(failed > 0 || cancelled > 0 ? NoticeLevel::WARN : NoticeLevel::SUCCESS, sy->T("Done"));
		}
		summary_->SetDetailsText(details);
		summary_->SetVisibility(V_VISIBLE);
	}
}

void UnPackScreen::update() {
	UITwoPaneBaseDialogScreen::update();
	RefreshStatus();
}
