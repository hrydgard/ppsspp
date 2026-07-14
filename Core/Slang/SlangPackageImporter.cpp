// Copyright (c) 2026- PPSSPP Project.

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

#include <zip.h>
#include "Core/Slang/SlangPackageImporter.h"
#include "Core/Slang/SlangPaths.h"
#include "Core/Loaders.h"
#include "Common/File/FileUtil.h"
#include "Common/Data/Format/JSONWriter.h"
#include "Common/Log.h"

static bool WriteZipEntry(zip_t *z, zip_uint64_t index, const Path &out, std::string *error) {
	struct zip_stat st; zip_stat_init(&st);
	if (zip_stat_index(z, index, 0, &st) != 0) { *error = "zip_stat_index failed"; return false; }
	zip_file_t *zf = zip_fopen_index(z, index, 0);
	if (!zf) { *error = "zip_fopen_index failed"; return false; }
	if (!File::CreateFullPath(Path(out.GetDirectory()))) { zip_fclose(zf); *error = "mkdir failed"; return false; }
	FILE *f = File::OpenCFile(out, "wb");
	if (!f) { zip_fclose(zf); *error = "open output failed"; return false; }
	char buf[128 * 1024];
	zip_uint64_t remaining = st.size;
	bool ok = true;
	while (remaining > 0) {
		zip_int64_t n = zip_fread(zf, buf, sizeof(buf));
		if (n <= 0) { ok = false; break; }
		if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) { ok = false; break; }
		remaining -= (zip_uint64_t)n;
	}
	fclose(f);
	zip_fclose(zf);
	if (!ok) *error = "zip read/write failed";
	return ok;
}

bool ExtractSlangPackage(const Path &zipPath, const Path &destRoot,
                         const std::string &sourceUrl, int64_t unixTimestamp,
                         std::string *error) {
	Path tempDir = Path(destRoot.GetDirectory()) / (destRoot.GetFilename() + ".import.tmp");
	File::DeleteDirRecursively(tempDir);          // clear any stale temp
	if (!File::CreateFullPath(tempDir)) { *error = "could not create temp dir"; return false; }

	ZipContainer zipContainer = ZipOpenPath(zipPath);
	zip_t *z = zipContainer;
	if (!z) { *error = "could not open downloaded zip"; File::DeleteDirRecursively(tempDir); return false; }

	int fileCount = 0;
	zip_int64_t numEntries = zip_get_num_entries(z, 0);
	for (zip_int64_t i = 0; i < numEntries; i++) {
		const char *name = zip_get_name(z, (zip_uint64_t)i, 0);
		if (!name) continue;
		std::string entry(name);
		bool isDir = !entry.empty() && (entry.back() == '/' || entry.back() == '\\');
		Path outPath;
		if (!ResolveSafeZipEntryPath(tempDir, entry, isDir, &outPath)) {
			continue;  // silently skip rejected entries (traversal, wrong ext, metadata)
		}
		if (isDir) { File::CreateFullPath(outPath); continue; }
		if (!WriteZipEntry(z, (zip_uint64_t)i, outPath, error)) {
			File::DeleteDirRecursively(tempDir); return false;
		}
		fileCount++;
	}
	ZipClose(zipContainer);

	if (fileCount == 0) { *error = "no valid shader files in archive"; File::DeleteDirRecursively(tempDir); return false; }

	// Write manifest inside the temp dir so it swaps atomically with the content.
	{
		json::JsonWriter w(json::JsonWriter::PRETTY);
		w.begin();
		w.writeString("sourceUrl", sourceUrl);
		w.writeInt("timestamp", (int)unixTimestamp);
		w.writeInt("fileCount", fileCount);
		w.end();
		File::WriteStringToFile(true, w.str(), tempDir / "manifest.json");
	}

	// Swap into place without risking the existing install: move the old install aside to a
	// backup first, put the new tree in place, and only delete the backup once the swap
	// succeeded. If anything fails, restore the backup so a failed import never destroys the
	// user's existing shaders.
	Path backupDir = Path(destRoot.GetDirectory()) / (destRoot.GetFilename() + ".import.bak");
	File::DeleteDirRecursively(backupDir);  // clear any stale backup
	bool hadExisting = File::Exists(destRoot);
	if (hadExisting && !File::Move(destRoot, backupDir)) {
		*error = "could not move existing shader install aside";
		File::DeleteDirRecursively(tempDir);
		return false;
	}
	if (!File::Move(tempDir, destRoot)) {
		*error = "could not move imported shaders into place";
		// Restore the previous install, then drop the temp.
		if (hadExisting) File::Move(backupDir, destRoot);
		File::DeleteDirRecursively(tempDir);
		return false;
	}
	// Swap succeeded; the old install (if any) is no longer needed.
	File::DeleteDirRecursively(backupDir);
	return true;
}
