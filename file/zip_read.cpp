#include <stdio.h>
#include <set>
#include <algorithm>

#ifdef ANDROID
#include <zip.h>
#endif

#include "base/basictypes.h"
#include "base/logging.h"
#include "file/zip_read.h"

#ifdef ANDROID
uint8_t *ReadFromZip(zip *archive, const char* filename, size_t *size) {
	// Figure out the file size first.
	struct zip_stat zstat;
	zip_stat(archive, filename, ZIP_FL_NOCASE, &zstat);

	uint8_t *contents = new uint8_t[zstat.size + 1];

	zip_file *file = zip_fopen(archive, filename, 0);
	if (!file) {
		ELOG("Error opening %s from ZIP", filename);
		delete [] contents;
		return 0;
	}
	zip_fread(file, contents, zstat.size);
	zip_fclose(file);
	contents[zstat.size] = 0;

	*size = zstat.size;
	return contents;
}

#endif

// The return is non-const because - why not?
uint8_t *ReadLocalFile(const char *filename, size_t *size) {
	FILE *file = fopen(filename, "rb");
	if (!file) {
		return 0;
	}
	fseek(file, 0, SEEK_END);
	size_t f_size = ftell(file);
	fseek(file, 0, SEEK_SET);
	uint8_t *contents = new uint8_t[f_size+1];
	fread(contents, 1, f_size, file);
	fclose(file);
	contents[f_size] = 0;
	*size = f_size;
	return contents;
}

#ifdef ANDROID

ZipAssetReader::ZipAssetReader(const char *zip_file, const char *in_zip_path) {
	zip_file_ = zip_open(zip_file, 0, NULL);
	strcpy(in_zip_path_, in_zip_path);
	if (!zip_file_) {
		ELOG("Failed to open %s as a zip file", zip_file);
	}

	std::vector<FileInfo> info;
	GetFileListing("assets", &info, 0);
	for (int i = 0; i < info.size(); i++) {
		if (info[i].isDirectory) {
			ILOG("Directory: %s", info[i].name.c_str());
		} else {
			ILOG("File: %s", info[i].name.c_str());
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

bool ZipAssetReader::GetFileListing(const char *path, std::vector<FileInfo> *listing, const char *filter = 0)
{
	ILOG("Zip path: %s", path);
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
		// ILOG("Comparing %s %s %i", name, path, pathlen);
		if (!memcmp(name, path, pathlen)) {
			// The prefix is right. Let's see if this is a file or path.
			char *slashPos = strchr(name + pathlen + 1, '/');
			if (slashPos != 0) {
				// A directory.
				std::string dirName = std::string(name + pathlen + 1, slashPos - (name + pathlen + 1));
				directories.insert(dirName);
			} else {
				files.insert(std::string(name + pathlen + 1));
			}
		}
	}

	for (auto diter = directories.begin(); diter != directories.end(); ++diter) {
		FileInfo info;
		info.name = *diter;
		info.fullName = std::string(path);
		if (info.fullName[info.fullName.size()-1] == '/')
			info.fullName = info.fullName.substr(0, info.fullName.size() - 1);
		info.fullName += "/" + *diter;
		info.exists = true;
		info.isWritable = false;
		info.isDirectory = true;
		listing->push_back(info);
	}

	for (auto fiter = files.begin(); fiter != files.end(); ++fiter) {
		FileInfo info;
		info.name = *fiter;
		info.fullName = std::string(path);
		if (info.fullName[info.fullName.size()-1] == '/')
			info.fullName = info.fullName.substr(0, info.fullName.size() - 1);
		info.fullName += "/" + *fiter;
		info.exists = true;
		info.isWritable = false;
		info.isDirectory = false;
		listing->push_back(info);
	}
	
	std::sort(listing->begin(), listing->end());
	return true;
}

bool ZipAssetReader::GetFileInfo(const char *path, FileInfo *info) {
	struct zip_stat zstat;
	char temp_path[1024];
	strcpy(temp_path, in_zip_path_);
	strcat(temp_path, path);
	if (0 != zip_stat(zip_file_, temp_path, ZIP_FL_NOCASE, &zstat)) {
		// ZIP files do not have real directories, so we'll end up here if we
		// try to stat one. For now that's fine.
		info->exists = false;
		info->size = 0;
		return false;
	}

	info->fullName = path;
	info->exists = true; // TODO
	info->isWritable = false;
	info->isDirectory = false;    // TODO
	info->size = zstat.size;
	return true;
}

#endif

uint8_t *DirectoryAssetReader::ReadAsset(const char *path, size_t *size) {
	char new_path[1024] = {0};
	// Check if it already contains the path
	if (strlen(path) > strlen(path_) && 0 == memcmp(path, path_, strlen(path_))) {
	}
	else {
		strcpy(new_path, path_);
	}
	strcat(new_path, path);
	return ReadLocalFile(new_path, size);
}

bool DirectoryAssetReader::GetFileListing(const char *path, std::vector<FileInfo> *listing, const char *filter = 0)
{
	char new_path[1024] = {0};
	// Check if it already contains the path
	if (strlen(path) > strlen(path_) && 0 == memcmp(path, path_, strlen(path_))) {
	}
	else {
		strcpy(new_path, path_);
	}
	strcat(new_path, path);
	FileInfo info;
	if (!getFileInfo(new_path, &info))
		return false;

	if (info.isDirectory)
	{
		getFilesInDir(new_path, listing, filter);
		return true;
	}
	else
	{
		return false;
	}
}

bool DirectoryAssetReader::GetFileInfo(const char *path, FileInfo *info) 
{
	char new_path[1024] = {0};
	// Check if it already contains the path
	if (strlen(path) > strlen(path_) && 0 == memcmp(path, path_, strlen(path_))) {
	}
	else {
		strcpy(new_path, path_);
	}
	strcat(new_path, path);
	return getFileInfo(new_path, info);	
}

struct VFSEntry {
	const char *prefix;
	AssetReader *reader;
};

static VFSEntry entries[16];
static int num_entries = 0;

void VFSRegister(const char *prefix, AssetReader *reader) {
	entries[num_entries].prefix = prefix;
	entries[num_entries].reader = reader;
	ILOG("Registered VFS for prefix %s: %s", prefix, reader->toString().c_str());
	num_entries++;
}

void VFSShutdown() {
	for (int i = 0; i < num_entries; i++) {
		delete entries[i].reader;
	}
	num_entries = 0;
}

uint8_t *VFSReadFile(const char *filename, size_t *size) {
	int fn_len = (int)strlen(filename);
	for (int i = 0; i < num_entries; i++) {
		int prefix_len = (int)strlen(entries[i].prefix);
		if (prefix_len >= fn_len) continue;
		if (0 == memcmp(filename, entries[i].prefix, prefix_len)) {
			ILOG("Prefix match: %s (%s) -> %s", entries[i].prefix, filename, filename + prefix_len);
			uint8_t *data = entries[i].reader->ReadAsset(filename + prefix_len, size);
			if (data)
				return data;
			else
				continue;
			// Else try the other registered file systems.
		}
	}
	ELOG("Missing filesystem for %s", filename);
	return 0;
}

bool VFSGetFileListing(const char *path, std::vector<FileInfo> *listing, const char *filter)
{
	int fn_len = (int)strlen(path);
	for (int i = 0; i < num_entries; i++) {
		int prefix_len = (int)strlen(entries[i].prefix);
		if (prefix_len >= fn_len) continue;
		if (0 == memcmp(path, entries[i].prefix, prefix_len)) {
			if (entries[i].reader->GetFileListing(path + prefix_len, listing, filter))
			{
				return true;
			}
		}
	}
	ELOG("Missing filesystem for %s", path);
	return false;
}

bool VFSGetFileInfo(const char *path, FileInfo *info)
{
	int fn_len = (int)strlen(path);
	for (int i = 0; i < num_entries; i++) {
		int prefix_len = (int)strlen(entries[i].prefix);
		if (prefix_len >= fn_len) continue;
		if (0 == memcmp(path, entries[i].prefix, prefix_len)) {
			return entries[i].reader->GetFileInfo(path + prefix_len, info);
		}
	}
	ELOG("Missing filesystem for %s", path);
	return false;
}
