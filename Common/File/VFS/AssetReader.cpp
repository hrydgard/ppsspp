#include <algorithm>
#include <ctype.h>
#include <set>
#include <stdio.h>

#ifdef __ANDROID__
#include <zip.h>
#endif

#include "Common/Common.h"
#include "Common/Log.h"
#include "Common/File/VFS/AssetReader.h"

#ifdef __ANDROID__
uint8_t *ReadFromZip(zip *archive, const char* filename, size_t *size) {
	// Figure out the file size first.
	struct zip_stat zstat;
	zip_file *file = zip_fopen(archive, filename, ZIP_FL_NOCASE|ZIP_FL_UNCHANGED);
	if (!file) {
		ERROR_LOG(IO, "Error opening %s from ZIP", filename);
		return 0;
	}
	zip_stat(archive, filename, ZIP_FL_NOCASE|ZIP_FL_UNCHANGED, &zstat);

	uint8_t *contents = new uint8_t[zstat.size + 1];
	zip_fread(file, contents, zstat.size);
	zip_fclose(file);
	contents[zstat.size] = 0;

	*size = zstat.size;
	return contents;
}

#endif

#ifdef __ANDROID__

ZipAssetReader::ZipAssetReader(const char *zip_file, const char *in_zip_path) {
	zip_file_ = zip_open(zip_file, 0, NULL);
	strcpy(in_zip_path_, in_zip_path);
	if (!zip_file_) {
		ERROR_LOG(IO, "Failed to open %s as a zip file", zip_file);
	}

	std::vector<File::FileInfo> info;
	GetFileListing("assets", &info, 0);
	for (size_t i = 0; i < info.size(); i++) {
		if (info[i].isDirectory) {
			DEBUG_LOG(IO, "Directory: %s", info[i].name.c_str());
		} else {
			DEBUG_LOG(IO, "File: %s", info[i].name.c_str());
		}
	}
}

ZipAssetReader::~ZipAssetReader() {
	zip_close(zip_file_);
}

uint8_t *ZipAssetReader::ReadAsset(const char *path, size_t *size) {
	char temp_path[1024];
	strcpy(temp_path, in_zip_path_);
	strcat(temp_path, path);
	return ReadFromZip(zip_file_, temp_path, size);
}

bool ZipAssetReader::GetFileListing(const char *orig_path, std::vector<File::FileInfo> *listing, const char *filter = 0) {
	char path[1024];
	strcpy(path, in_zip_path_);
	strcat(path, orig_path);

	std::set<std::string> filters;
	std::string tmp;
	if (filter) {
		while (*filter) {
			if (*filter == ':') {
				filters.insert("." + tmp);
				tmp.clear();
			} else {
				tmp.push_back(*filter);
			}
			filter++;
		}
	}
	if (tmp.size())
		filters.insert("." + tmp);

	// We just loop through the whole ZIP file and deduce what files are in this directory, and what subdirectories there are.
	std::set<std::string> files;
	std::set<std::string> directories;
	int numFiles = zip_get_num_files(zip_file_);
	size_t pathlen = strlen(path);
	if (path[pathlen-1] == '/')
		pathlen--;
	for (int i = 0; i < numFiles; i++) {
		const char* name = zip_get_name(zip_file_, i, 0);
		if (!name)
			continue;
		if (!memcmp(name, path, pathlen)) {
			// The prefix is right. Let's see if this is a file or path.
			const char *slashPos = strchr(name + pathlen + 1, '/');
			if (slashPos != 0) {
				// A directory.
				std::string dirName = std::string(name + pathlen + 1, slashPos - (name + pathlen + 1));
				directories.insert(dirName);
			} else if (name[pathlen] == '/') {
				const char *fn = name + pathlen + 1;
				files.insert(std::string(fn));
			}  // else, it was a file with the same prefix as the path. like langregion.ini next to lang/.
		}
	}

	for (auto diter = directories.begin(); diter != directories.end(); ++diter) {
		File::FileInfo info;
		info.name = *diter;

		// Remove the "inzip" part of the fullname.
		info.fullName = Path(std::string(path).substr(strlen(in_zip_path_))) / *diter;
		info.exists = true;
		info.isWritable = false;
		info.isDirectory = true;
		listing->push_back(info);
	}

	for (auto fiter = files.begin(); fiter != files.end(); ++fiter) {
		std::string fpath = path;
		File::FileInfo info;
		info.name = *fiter;
		info.fullName = Path(std::string(path).substr(strlen(in_zip_path_))) / *fiter;
		info.exists = true;
		info.isWritable = false;
		info.isDirectory = false;
		std::string ext = info.fullName.GetFileExtension();
		if (filter) {
			if (filters.find(ext) == filters.end()) {
				continue;
			}
		}
		listing->push_back(info);
	}

	std::sort(listing->begin(), listing->end());
	return true;
}

bool ZipAssetReader::GetFileInfo(const char *path, File::FileInfo *info) {
	struct zip_stat zstat;
	char temp_path[1024];
	strcpy(temp_path, in_zip_path_);
	strcat(temp_path, path);
	if (0 != zip_stat(zip_file_, temp_path, ZIP_FL_NOCASE|ZIP_FL_UNCHANGED, &zstat)) {
		// ZIP files do not have real directories, so we'll end up here if we
		// try to stat one. For now that's fine.
		info->exists = false;
		info->size = 0;
		return false;
	}

	info->fullName = Path(path);
	info->exists = true; // TODO
	info->isWritable = false;
	info->isDirectory = false;    // TODO
	info->size = zstat.size;
	return true;
}

#endif

DirectoryAssetReader::DirectoryAssetReader(const Path &path) {
	path_ = path;
}

uint8_t *DirectoryAssetReader::ReadAsset(const char *path, size_t *size) {
	Path new_path = Path(path).StartsWith(path_) ? Path(path) : path_ / path;
	return File::ReadLocalFile(new_path, size);
}

bool DirectoryAssetReader::GetFileListing(const char *path, std::vector<File::FileInfo> *listing, const char *filter = nullptr) {
	Path new_path = Path(path).StartsWith(path_) ? Path(path) : path_ / path;

	File::FileInfo info;
	if (!File::GetFileInfo(new_path, &info))
		return false;

	if (info.isDirectory) {
		File::GetFilesInDir(new_path, listing, filter);
		return true;
	}
	return false;
}

bool DirectoryAssetReader::GetFileInfo(const char *path, File::FileInfo *info) {
	Path new_path = Path(path).StartsWith(path_) ? Path(path) : path_ / path;
	return File::GetFileInfo(new_path, info);
}
