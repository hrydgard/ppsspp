#include <cstring>
#include <string>

#include "Common/File/FileUtil.h"
#include "Common/GPU/Shader.h"
#include "GPU/Common/ShaderCommon.h"
#include "ext/xxhash.h"

bool AppendShaderCacheChecksum(const Path &filename) {
	std::string data;
	if (!File::ReadBinaryFileToString(filename, &data)) {
		return false;
	}
	const uint64_t checksum = XXH3_64bits(data.data(), data.size());
	FILE *f = File::OpenCFile(filename, "ab");
	if (!f) {
		return false;
	}
	const bool ok = fwrite(&checksum, sizeof(checksum), 1, f) == 1;
	return fclose(f) == 0 && ok;
}

bool VerifyShaderCacheChecksum(const Path &filename) {
	std::string data;
	if (!File::ReadBinaryFileToString(filename, &data) || data.size() < sizeof(uint64_t)) {
		return false;
	}
	const size_t size = data.size() - sizeof(uint64_t);
	uint64_t stored;
	memcpy(&stored, data.data() + size, sizeof(stored));
	return XXH3_64bits(data.data(), size) == stored;
}
