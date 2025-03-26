#pragma once

#include <functional>
#include <vector>
#include <string>
#include <mutex>
#include <thread>

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
	void Add(const std::string &filename);
	void Remove(const std::string &filename);
	void Clean();
	bool HasAny() const;
	void Clear();
	bool ContainsFile(const std::string &filename);

	std::vector<std::string> GetRecentFiles() const;
private:
	void ResetThread();
	void SetThread(std::function<void()> f);
	void RemoveResolved(const std::string &resolvedFilename);
	std::vector<std::string> recentIsos;
	mutable std::mutex recentIsosLock;
	mutable std::mutex recentIsosThreadLock;
	mutable std::thread recentIsosThread;
	bool recentIsosThreadPending = false;
};

// Singleton, don't make more.
extern RecentFilesManager g_recentFiles;
