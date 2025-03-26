#include <mutex>
#include <algorithm>
#include <condition_variable>

#include "Common/File/FileUtil.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Core/Loaders.h"
#include "Core/Util/RecentFiles.h"
#include "Core/Config.h"

// Not in Config.h because it's #included a lot.
std::mutex recentIsosLock;
std::mutex recentIsosThreadLock;
std::thread recentIsosThread;
bool recentIsosThreadPending = false;

std::vector<std::string> recentIsos;

std::vector<std::string> GetRecentIsos() {
	std::lock_guard<std::mutex> guard(recentIsosLock);
	return recentIsos;
}

bool HasRecentIsos() {
	std::lock_guard<std::mutex> guard(recentIsosLock);
	return !recentIsos.empty();
}

void ClearRecentIsos() {
	ResetRecentIsosThread();
	std::lock_guard<std::mutex> guard(recentIsosLock);
	recentIsos.clear();
}

void ResetRecentIsosThread() {
	std::lock_guard<std::mutex> guard(recentIsosThreadLock);
	if (recentIsosThreadPending && recentIsosThread.joinable())
		recentIsosThread.join();
}

void SetRecentIsosThread(std::function<void()> f) {
	std::lock_guard<std::mutex> guard(recentIsosThreadLock);
	if (recentIsosThreadPending && recentIsosThread.joinable())
		recentIsosThread.join();
	recentIsosThread = std::thread(f);
	recentIsosThreadPending = true;
}

void LoadRecentIsos(const Section *recent, int maxRecent) {
	ResetRecentIsosThread();

	std::vector<std::string> newRecent;
	for (int i = 0; i < maxRecent; i++) {
		char keyName[64];
		std::string fileName;

		snprintf(keyName, sizeof(keyName), "FileName%d", i);
		if (recent->Get(keyName, &fileName, "") && !fileName.empty()) {
			newRecent.push_back(fileName);
		}
	}

	std::lock_guard<std::mutex> guard(recentIsosLock);
	recentIsos = newRecent;
}

void SaveRecentIsos(Section *recent, int maxRecent) {
	ResetRecentIsosThread();

	std::vector<std::string> recentCopy;
	{
		std::lock_guard<std::mutex> guard(recentIsosLock);
		recentCopy = recentIsos;
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

void RemoveRecentResolved(const std::string &resolvedFilename) {
	ResetRecentIsosThread();

	std::lock_guard<std::mutex> guard(recentIsosLock);
	auto iter = std::remove_if(recentIsos.begin(), recentIsos.end(), [resolvedFilename](const auto &str) {
		return str == resolvedFilename;
	});
	// remove_if is weird.
	recentIsos.erase(iter, recentIsos.end());
}

void AddRecent(const std::string &filename, int maxRecent) {
	std::string resolvedFilename = File::ResolvePath(filename);
	RemoveRecentResolved(resolvedFilename);

	ResetRecentIsosThread();
	std::lock_guard<std::mutex> guard(recentIsosLock);
	recentIsos.insert(recentIsos.begin(), resolvedFilename);
	if ((int)recentIsos.size() > maxRecent)
		recentIsos.resize(maxRecent);
}

void RemoveRecent(const std::string &filename) {
	std::string resolvedFilename = File::ResolvePath(filename);
	RemoveRecentResolved(resolvedFilename);
}

void CleanRecentIsos() {
	SetRecentIsosThread([] {
		SetCurrentThreadName("RecentISOs");

		AndroidJNIThreadContext jniContext;  // destructor detaches

		double startTime = time_now_d();

		std::lock_guard<std::mutex> guard(recentIsosLock);
		std::vector<std::string> cleanedRecent;
		if (recentIsos.empty()) {
			INFO_LOG(Log::Loader, "No recents list found.");
		}

		for (size_t i = 0; i < recentIsos.size(); i++) {
			bool exists = false;
			Path path = Path(recentIsos[i]);
			switch (path.Type()) {
			case PathType::CONTENT_URI:
			case PathType::NATIVE:
				exists = File::Exists(path);
				if (!exists) {
					if (TryUpdateSavedPath(&path)) {
						exists = File::Exists(path);
						INFO_LOG(Log::Loader, "Exists=%d when checking updated path: %s", exists, path.c_str());
					}
				}
				break;
			default:
				FileLoader *loader = ConstructFileLoader(path);
				exists = loader->ExistsFast();
				delete loader;
				break;
			}

			if (exists) {
				std::string pathStr = path.ToString();
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
		recentIsos = cleanedRecent;
	});
}
