#include <stdio.h>

#ifndef _WIN32
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
	// This is not really necessary.
	int numFiles = zip_get_num_files(zip_file_);
	for (int i = 0; i < numFiles; i++) {
		const char* name = zip_get_name(zip_file_, i, 0);
		if (name == NULL) {
			ELOG("Error reading zip file name at index %i : %s", i, zip_strerror(zip_file_));
			return;
		}
		// ILOG("File %i : %s\n", i, name);
	}
}

ZipAssetReader::~ZipAssetReader() {
	zip_close(zip_file_);
}

uint8_t *ZipAssetReader::ReadAsset(const char *path, size_t *size) {
	char temp_path[256];
	strcpy(temp_path, in_zip_path_);
	strcat(temp_path, path);
	return ReadFromZip(zip_file_, temp_path, size);
}

#endif

uint8_t *DirectoryAssetReader::ReadAsset(const char *path, size_t *size) {
	char new_path[256] = {0};
	// Check if it already contains the path
	if (strlen(path) > strlen(path_) && 0 == memcmp(path, path_, strlen(path_))) {
	}
	else {
		strcpy(new_path, path_);
	}
	strcat(new_path, path);
	// ILOG("New path: %s", new_path);
	return ReadLocalFile(new_path, size);
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
	int fn_len = strlen(filename);
	for (int i = 0; i < num_entries; i++) {
		int prefix_len = strlen(entries[i].prefix);
		if (prefix_len >= fn_len) continue;
		if (0 == memcmp(filename, entries[i].prefix, prefix_len)) {
			// ILOG("Prefix match: %s (%s) -> %s", entries[i].prefix, filename, filename + prefix_len);
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
