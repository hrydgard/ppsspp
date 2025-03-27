#include <mutex>
#include <algorithm>
#include <condition_variable>

#include "Common/File/FileUtil.h"
#include "Common/System/System.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Core/Loaders.h"
#include "Core/Util/RecentFiles.h"
#include "Core/Config.h"

RecentFilesManager g_recentFiles;

RecentFilesManager::RecentFilesManager() {}

RecentFilesManager::~RecentFilesManager() {
	if (thread_.joinable()) {
		{
			std::lock_guard<std::mutex> guard(cmdLock_);
			cmds_.push(RecentCommand{ RecentCmd::Exit });
			cmdCondVar_.notify_one();
		}
		thread_.join();
	}
}

void RecentFilesManager::EnsureThread() {
	if (thread_.joinable()) {
		return;
	}
	std::lock_guard<std::mutex> guard(cmdLock_);
	thread_ = std::thread([this] {
		// NOTE: Can't create the thread in the constructor, because at that point,
		// JNI attachment doesn't yet work.
		SetCurrentThreadName("RecentISOThreadFunc");
		AndroidJNIThreadContext jniContext;  // destructor detaches
		ThreadFunc();
	});
}

std::vector<std::string> RecentFilesManager::GetRecentFiles() const {
	std::lock_guard<std::mutex> guard(recentLock_);
	return recentFiles_;
}

bool RecentFilesManager::ContainsFile(std::string_view filename) {
	if (g_Config.iMaxRecent <= 0)
		return false;

	// Unfortunately this resolve needs to be done synchronously.
	std::string resolvedFilename = File::ResolvePath(filename);

	std::lock_guard<std::mutex> guard(recentLock_);
	for (const auto &file : recentFiles_) {
		if (file == resolvedFilename) {
			return true;
		}
	}
	return false;
}

bool RecentFilesManager::HasAny() const {
	std::lock_guard<std::mutex> guard(recentLock_);
	return !recentFiles_.empty();
}

void RecentFilesManager::Clear() {
	std::lock_guard<std::mutex> guard(cmdLock_);
	// Just zapping any pending command, since they won't matter after this.
	WipePendingCommandsUnderLock();
	cmds_.push(RecentCommand{ RecentCmd::Clear });
	cmdCondVar_.notify_one();
}

void RecentFilesManager::WipePendingCommandsUnderLock() {
	// Wipe any queued commands.
	while (!cmds_.empty()) {
		INFO_LOG(Log::System, "Wiped a recent command");
		cmds_.pop();
	}
}

void RecentFilesManager::Load(const Section *recent, int maxRecent) {
	std::vector<std::string> newRecent;
	for (int i = 0; i < maxRecent; i++) {
		char keyName[64];
		std::string fileName;

		snprintf(keyName, sizeof(keyName), "FileName%d", i);
		if (recent->Get(keyName, &fileName, "") && !fileName.empty()) {
			newRecent.push_back(fileName);
		}
	}

	std::lock_guard<std::mutex> guard(cmdLock_);
	// Just zapping any pending command, since they won't matter after this.
	// TODO: Maybe we should let adds through...
	WipePendingCommandsUnderLock();
	cmds_.push(RecentCommand{ RecentCmd::ReplaceAll, std::make_unique<std::vector<std::string>>(newRecent) });
	cmdCondVar_.notify_one();
}

void RecentFilesManager::Save(Section *recent, int maxRecent) {
	// TODO: Should we wait for any commands? Don't want to block...

	std::vector<std::string> recentCopy;
	{
		std::lock_guard<std::mutex> guard(recentLock_);
		recentCopy = recentFiles_;
	}

	for (int i = 0; i < maxRecent; i++) {
		char keyName[64];
		snprintf(keyName, sizeof(keyName), "FileName%d", i);
		if (i < (int)recentCopy.size()) {
			recent->Set(keyName, recentCopy[i]);
		} else {
			recent->Delete(keyName); // delete the nonexisting FileName
		}
	}
}

void RecentFilesManager::Add(std::string_view filename) {
	if (g_Config.iMaxRecent <= 0) {
		return;
	}

	std::lock_guard<std::mutex> guard(cmdLock_);
	cmds_.push(RecentCommand{ RecentCmd::Add, {}, std::make_unique<std::string>(filename) });
	cmdCondVar_.notify_one();
}

void RecentFilesManager::Remove(std::string_view filename) {
	if (g_Config.iMaxRecent <= 0) {
		return;
	}

	std::lock_guard<std::mutex> guard(cmdLock_);
	cmds_.push(RecentCommand{ RecentCmd::Remove, {}, std::make_unique<std::string>(filename) });
	cmdCondVar_.notify_one();
}

void RecentFilesManager::Clean() {
	std::lock_guard<std::mutex> guard(cmdLock_);
	cmds_.push(RecentCommand{ RecentCmd::CleanMissing });
	cmdCondVar_.notify_one();
}

void RecentFilesManager::ThreadFunc() {
	while (true) {
		RecentCommand cmd;
		{
			std::unique_lock<std::mutex> guard(cmdLock_);
			cmdCondVar_.wait(guard, [this]() { return !cmds_.empty(); });
			cmd = std::move(cmds_.front());
			cmds_.pop();
		}

		switch (cmd.cmd) {
		case RecentCmd::Exit:
			// done!
			return;
		case RecentCmd::Clear:
		{
			std::lock_guard<std::mutex> guard(recentLock_);
			recentFiles_.clear();
			System_PostUIMessage(UIMessage::RECENT_FILES_CHANGED);
			break;
		}
		case RecentCmd::Add:
		{
			std::string resolvedFilename = File::ResolvePath(*cmd.sarg);
			std::lock_guard<std::mutex> guard(recentLock_);
			// First, remove the existing one.
			// remove_if is weird.
			if (!recentFiles_.empty()) {
				recentFiles_.erase(std::remove_if(recentFiles_.begin(), recentFiles_.end(), [&resolvedFilename](const auto &str) {
					return str == resolvedFilename;
				}), recentFiles_.end());
			}
			recentFiles_.insert(recentFiles_.begin(), resolvedFilename);
			if ((int)recentFiles_.size() > g_Config.iMaxRecent)
				recentFiles_.resize(g_Config.iMaxRecent);
			System_PostUIMessage(UIMessage::RECENT_FILES_CHANGED);
			break;
		}
		case RecentCmd::Remove:
		{
			std::string resolvedFilename = File::ResolvePath(*cmd.sarg);
			std::lock_guard<std::mutex> guard(recentLock_);
			size_t count = recentFiles_.size();
			// remove_if is weird.
			recentFiles_.erase(std::remove_if(recentFiles_.begin(), recentFiles_.end(), [&resolvedFilename](const auto &str) {
				return str == resolvedFilename;
			}), recentFiles_.end());
			if (recentFiles_.size() != count) {
				System_PostUIMessage(UIMessage::RECENT_FILES_CHANGED);
			}
			break;
		}
		case RecentCmd::ReplaceAll:
		{
			std::lock_guard<std::mutex> guard(recentLock_);
			recentFiles_ = *cmd.varg;
			System_PostUIMessage(UIMessage::RECENT_FILES_CHANGED);
			break;
		}
		case RecentCmd::CleanMissing:
		{
			PerformCleanMissing();
			break;
		}
		}
	}
}

void RecentFilesManager::PerformCleanMissing() {
	std::vector<std::string> initialRecent;

	// Work on a copy, so we don't have to hold the lock.
	{
		std::lock_guard<std::mutex> guard(recentLock_);
		initialRecent = recentFiles_;
	}

	std::vector<std::string> cleanedRecent;
	double startTime = time_now_d();

	for (const auto &filename : initialRecent) {
		bool exists = false;
		Path path(filename);
		switch (path.Type()) {
		case PathType::CONTENT_URI:
		case PathType::NATIVE:
			exists = File::Exists(path);
			if (!exists && TryUpdateSavedPath(&path)) {
				// iOS only stuff
				exists = File::Exists(path);
				INFO_LOG(Log::Loader, "Exists=%d when checking updated path: %s", exists, path.c_str());
			}
			break;
		default:
			FileLoader *loader = ConstructFileLoader(path);
			exists = loader->ExistsFast();
			delete loader;
			break;
		}

		if (exists) {
			const std::string pathStr = path.ToString();
			// Make sure we don't have any redundant items.
			auto duplicate = std::find(cleanedRecent.begin(), cleanedRecent.end(), pathStr);
			if (duplicate == cleanedRecent.end()) {
				cleanedRecent.push_back(pathStr);
			}
		} else {
			DEBUG_LOG(Log::Loader, "Removed %s from recent. errno=%d", path.c_str(), errno);
		}
	}

	double recentTime = time_now_d() - startTime;
	if (recentTime > 0.1) {
		INFO_LOG(Log::System, "CleanRecent took %0.2f", recentTime);
	}

	if (cleanedRecent.size() != initialRecent.size()) {
		std::lock_guard<std::mutex> guard(recentLock_);
		recentFiles_ = cleanedRecent;
		System_PostUIMessage(UIMessage::RECENT_FILES_CHANGED);
	}
}
