#pragma once

#include <functional>
#include <vector>
#include <queue>
#include <string>
#include <string_view>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>

#include "Common/Data/Format/IniFile.h"

// You'd think this would be simple enough to not require a manager, but we
// want to provide a clean list with non-existent files removed, while never
// blocking the main thread. It does get a bit complex.
class RecentFilesManager {
public:
	RecentFilesManager();
	~RecentFilesManager();

	void Load(const Section *recent, int maxRecent);
	void Save(Section *recent, int maxRecent);
	void Add(std::string_view filename);
	void Remove(std::string_view filename);
	void Clean();
	bool HasAny() const;
	void Clear();
	bool ContainsFile(std::string_view filename);

	void EnsureThread();

	std::vector<std::string> GetRecentFiles() const;
private:

	enum class RecentCmd {
		Exit,
		Clear,
		CleanMissing,
		Add,
		Remove,
		ReplaceAll,
	};

	struct RecentCommand {
		RecentCmd cmd;
		std::unique_ptr<std::vector<std::string>> varg;
		std::unique_ptr<std::string> sarg;
	};

	void PerformCleanMissing();
	void WipePendingCommandsUnderLock();
	void ThreadFunc();

	std::queue<RecentCommand> cmds_;

	mutable std::mutex recentLock_;
	std::vector<std::string> recentFiles_;

	std::thread thread_;
	std::mutex cmdLock_;
	std::condition_variable cmdCondVar_;
};

// Singleton, don't make more.
extern RecentFilesManager g_recentFiles;
