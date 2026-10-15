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
#include "Core/Util/MaxCSO.h"
#include "Core/Util/PathUtil.h"

#include "UI/MaxCSOScreen.h"

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

// Shared between the worker thread and whichever MaxCSOScreen is showing. Lives in a global so
// that leaving the screen doesn't lose track of a running job - coming back shows it again.
struct JobState {
	bool compress = true;

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

bool HasExtension(const Path &path, const std::vector<std::string> &exts) {
	std::string ext = path.GetFileExtension();
	return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

const std::vector<std::string> &InputExtensions(bool compress) {
	static const std::vector<std::string> compressExts = { ".iso" };
	static const std::vector<std::string> decompressExts = { ".cso", ".zso", ".dax" };
	return compress ? compressExts : decompressExts;
}

void ScanFolder(const Path &folder, bool compress, bool recursive, std::vector<Path> *out) {
	std::vector<File::FileInfo> files;
	File::GetFilesInDir(folder, &files, compress ? "iso" : "cso:zso:dax");
	for (const auto &file : files) {
		if (file.isDirectory) {
			if (recursive) {
				ScanFolder(file.fullName, compress, recursive, out);
			}
		} else {
			out->push_back(file.fullName);
		}
	}
}

void RunJob(std::shared_ptr<JobState> job, Path source, bool folder, MaxCSO::Mode mode, bool recursive, bool deleteOriginals) {
	SetCurrentThreadName("MaxCSO");
	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	const bool compress = job->compress;
	std::vector<Path> inputs;
	if (folder) {
		ScanFolder(source, compress, recursive, &inputs);
		std::sort(inputs.begin(), inputs.end());
	} else {
		inputs.push_back(source);
	}

	{
		std::lock_guard<std::mutex> guard(job->mutex);
		for (const Path &input : inputs) {
			JobItem item;
			item.input = input;
			item.output = input.WithReplacedExtension(compress ? ".cso" : ".iso");
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
		if (!HasExtension(item.input, InputExtensions(compress))) {
			finish(ItemStatus::Failed, compress ? sy->T("MaxCSONotISO", "Not an ISO file") : sy->T("MaxCSONotCompressed", "Not a CSO, ZSO or DAX file"));
			continue;
		}
		if (File::Exists(item.output)) {
			finish(ItemStatus::Skipped, sy->T("MaxCSOOutputExists", "Output file already exists"));
			continue;
		}

		{
			std::lock_guard<std::mutex> guard(job->mutex);
			job->items[i].status = ItemStatus::Running;
			job->current = (int)i;
		}
		job->fileProgress = 0.0f;

		INFO_LOG(Log::Loader, "MaxCSO: %s %s -> %s", compress ? "Compressing" : "Decompressing", item.input.c_str(), item.output.c_str());
		std::string error;
		bool success = MaxCSO::Process(item.input, item.output, mode, [job](float progress) {
			job->fileProgress = progress;
		}, &error);

		if (!success) {
			ERROR_LOG(Log::Loader, "MaxCSO: Failed on %s: %s", item.input.c_str(), error.c_str());
			finish(ItemStatus::Failed, error);
			continue;
		}

		item.outputSize = File::GetFileSize(item.output);
		std::string message;
		if (deleteOriginals && item.outputSize > 0) {
			if (!File::Delete(item.input)) {
				message = sy->T("MaxCSODeleteFailed", "Could not delete the original");
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

	MaxCSO::Mode mode = MaxCSO::Mode::Decompress;
	if (compress) {
		switch (g_Config.iMaxCSOPreset) {
		case 0: mode = MaxCSO::Mode::Fast; break;
		case 2: mode = MaxCSO::Mode::Maximum; break;
		default: mode = MaxCSO::Mode::Standard; break;
		}
	}

	auto job = std::make_shared<JobState>();
	job->compress = compress;
	g_job = job;
	std::thread(RunJob, job, source, folder, mode, g_Config.bMaxCSOIncludeSubfolders, g_Config.bMaxCSODeleteOriginals).detach();
}

}  // namespace

std::string_view MaxCSOScreen::GetTitle() const {
	return "MaxCSO";
}

void MaxCSOScreen::CreateContentViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	LinearLayout *content = parent->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));

	content->Add(new InfoItem(sy->T("MaxCSO version", "maxcso version"), MaxCSO::Version()));
	content->Add(new TextView(GetFriendlyPath(MaxCSO::LibraryPath()), ALIGN_LEFT | FLAG_WRAP_TEXT, true));

	content->Add(new ItemHeader(sy->T("Status")));
	statusText_ = content->Add(new TextView(sy->T("MaxCSOIdle", "Pick a game or a folder to compress or decompress."), ALIGN_LEFT | FLAG_WRAP_TEXT, false));
	fileProgress_ = content->Add(new ProgressBar());
	totalProgress_ = content->Add(new ProgressBar());
	summary_ = content->Add(new NoticeView(NoticeLevel::SUCCESS, "", ""));
	summary_->SetVisibility(V_GONE);

	content->Add(new ItemHeader(sy->T("Results")));
	results_ = content->Add(new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT)));
	shownResults_ = 0;

	RefreshStatus();
}

void MaxCSOScreen::CreateSettingsViews(UI::ViewGroup *parent) {
	using namespace UI;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);
	auto di = GetI18NCategory(I18NCat::DIALOG);

	startChoices_.clear();

	static const char *presets[] = { "Fast compression", "Standard compression", "Maximum compression (very slow)" };
	parent->Add(new PopupMultiChoice(&g_Config.iMaxCSOPreset, sy->T("Compression preset"), presets, 0, ARRAY_SIZE(presets), I18NCat::SYSTEM, screenManager()));
	parent->Add(new CheckBox(&g_Config.bMaxCSOIncludeSubfolders, sy->T("Include subfolders")));
	parent->Add(new CheckBox(&g_Config.bMaxCSODeleteOriginals, sy->T("Delete originals"),
		sy->T("MaxCSODeleteOriginalsDesc", "After a file was converted successfully")));

	const bool hasFileBrowser = System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER);
	const bool hasFolderBrowser = System_GetPropertyBool(SYSPROP_HAS_FOLDER_BROWSER);

	parent->Add(new ItemHeader(sy->T("Compress")));
	if (hasFileBrowser) {
		startChoices_.push_back(parent->Add(new Choice(sy->T("Compress game..."), ImageID("I_FILE"))));
		startChoices_.back()->OnClick.Add([this](UI::EventParams &) { BrowseAndStart(true, false); });
	}
	if (hasFolderBrowser) {
		startChoices_.push_back(parent->Add(new Choice(sy->T("Compress folder..."), ImageID("I_FOLDER"))));
		startChoices_.back()->OnClick.Add([this](UI::EventParams &) { BrowseAndStart(true, true); });
	}

	parent->Add(new ItemHeader(sy->T("Decompress")));
	if (hasFileBrowser) {
		startChoices_.push_back(parent->Add(new Choice(sy->T("Decompress game..."), ImageID("I_FILE"))));
		startChoices_.back()->OnClick.Add([this](UI::EventParams &) { BrowseAndStart(false, false); });
	}
	if (hasFolderBrowser) {
		startChoices_.push_back(parent->Add(new Choice(sy->T("Decompress folder..."), ImageID("I_FOLDER"))));
		startChoices_.back()->OnClick.Add([this](UI::EventParams &) { BrowseAndStart(false, true); });
	}

	cancelChoice_ = parent->Add(new Choice(di->T("Cancel")));
	cancelChoice_->OnClick.Add([](UI::EventParams &) {
		if (g_job) {
			g_job->cancel = true;
		}
	});

	if (!hasFileBrowser && !hasFolderBrowser) {
		parent->Add(new NoticeView(NoticeLevel::WARN, sy->T("MaxCSONoBrowser", "No file browser is available on this platform"), ""));
	}
	if (PSP_IsInited()) {
		parent->Add(new NoticeView(NoticeLevel::INFO, sy->T("MaxCSONeedsNoGame", "Stop the game to compress or decompress games"), ""));
	}

	RefreshStatus();
}

void MaxCSOScreen::BrowseAndStart(bool compress, bool folder) {
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

void MaxCSOScreen::RefreshStatus() {
	using namespace UI;

	auto sy = GetI18NCategory(I18NCat::SYSTEM);

	const bool running = IsJobRunning();
	for (Choice *choice : startChoices_) {
		choice->SetEnabled(!running && !PSP_IsInited());
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
		std::string_view verb = g_job->compress ? sy->T("Compressing") : sy->T("Decompressing");
		std::string text = StringFromFormat("%.*s (%d / %d): %s", STR_VIEW(verb), g_job->current + 1, (int)total, item.input.GetFilename().c_str());
		if (g_job->cancel) {
			text += "\n" + std::string(sy->T("MaxCSOCancelling", "Stopping after the current file..."));
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
		int converted = 0, skipped = 0, failed = 0;
		uint64_t before = 0, after = 0;
		for (const JobItem &item : g_job->items) {
			switch (item.status) {
			case ItemStatus::Done:
				converted++;
				before += item.inputSize;
				after += item.outputSize;
				break;
			case ItemStatus::Skipped: skipped++; break;
			default: failed++; break;
			}
		}
		std::string details = StringFromFormat("%s: %d, %s: %d, %s: %d",
			std::string(sy->T("Converted")).c_str(), converted,
			std::string(sy->T("Skipped")).c_str(), skipped,
			std::string(sy->T("Failed")).c_str(), failed);
		if (converted > 0) {
			details += StringFromFormat("\n%s -> %s", NiceSizeFormat(before).c_str(), NiceSizeFormat(after).c_str());
		}
		if (total == 0) {
			summary_->SetLevelAndText(NoticeLevel::WARN, sy->T("MaxCSONothingFound", "No matching games found"));
		} else {
			summary_->SetLevelAndText(failed > 0 ? NoticeLevel::WARN : NoticeLevel::SUCCESS, sy->T("Done"));
		}
		summary_->SetDetailsText(details);
		summary_->SetVisibility(V_VISIBLE);
	}
}

void MaxCSOScreen::update() {
	UITwoPaneBaseDialogScreen::update();
	RefreshStatus();
}
