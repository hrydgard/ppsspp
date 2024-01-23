#include "Common/Data/Format/JSONReader.h"
#include "Common/File/VFS/VFS.h"
#include "Common/File/Path.h"
#include "Common/File/FileUtil.h"
#include "Common/Log.h"

namespace json {

JsonReader::JsonReader(const std::string &filename) {
	size_t buf_size;
	buffer_ = (char *)g_VFS.ReadFile(filename.c_str(), &buf_size);
	if (buffer_) {
		parse();
	} else {
		// Okay, try to read on the local file system
		buffer_ = (char *)File::ReadLocalFile(Path(filename), &buf_size);
		if (buffer_) {
			parse();
		} else {
			ERROR_LOG(IO, "Failed to read json file '%s'", filename.c_str());
		}
	}
}

bool JsonReader::parse() {
	char *error_pos;
	int status = jsonParse(buffer_, &error_pos, &root_, alloc_);
	if (status != JSON_OK) {
		ERROR_LOG(IO, "Error at (%i): %s\n%s\n\n", (int)(error_pos - buffer_), jsonStrError(status), error_pos);
		return false;
	}
	ok_ = true;
	return true;
}

int JsonGet::numChildren() const {
	int count = 0;
	if (value_.getTag() == JSON_OBJECT || value_.getTag() == JSON_ARRAY) {
		for (auto it : value_) {
			(void)it;
			count++;
		}
	}
	return count;
}

const JsonNode *JsonGet::get(const char *child_name) const {
	if (!child_name) {
		ERROR_LOG(IO, "JSON: Cannot get from null child name");
		return nullptr;
	}
	if (value_.getTag() != JSON_OBJECT) {
		return nullptr;
	}
	for (auto it : value_) {
		if (!strcmp(it->key, child_name)) {
			return it;
		}
	}
	return nullptr;
}

const JsonNode *JsonGet::get(const char *child_name, JsonTag type) const {
	const JsonNode *v = get(child_name);
	if (v && type == v->value.getTag())
		return v;
	return nullptr;
}

const char *JsonGet::getStringOrNull(const char *child_name) const {
	const JsonNode *val = get(child_name, JSON_STRING);
	if (val)
		return val->value.toString();
	ERROR_LOG(IO, "String '%s' missing from node", child_name);
	return nullptr;
}

bool JsonGet::getString(const char *child_name, std::string *output) const {
	const JsonNode *val = get(child_name, JSON_STRING);
	if (!val) {
		return false;
	}
	*output = val->value.toString();
	return true;
}

const char *JsonGet::getStringOr(const char *child_name, const char *default_value) const {
	const JsonNode *val = get(child_name, JSON_STRING);
	if (!val)
		return default_value;
	return val->value.toString();
}

bool JsonGet::getStringVector(std::vector<std::string> *vec) const {
	vec->clear();
	if (value_.getTag() == JSON_ARRAY) {
		for (auto it : value_) {
			if (it->value.getTag() == JSON_STRING) {
				vec->push_back(it->value.toString());
			}
		}
		return true;
	} else {
		return false;
	}
}

double JsonGet::getFloat(const char *child_name) const {
	return get(child_name, JSON_NUMBER)->value.toNumber();
}

double JsonGet::getFloat(const char *child_name, double default_value) const {
	const JsonNode *val = get(child_name, JSON_NUMBER);
	if (!val)
		return default_value;
	return val->value.toNumber();
}

int JsonGet::getInt(const char *child_name) const {
	return (int)get(child_name, JSON_NUMBER)->value.toNumber();
}

int JsonGet::getInt(const char *child_name, int default_value) const {
	const JsonNode *val = get(child_name, JSON_NUMBER);
	if (!val)
		return default_value;
	return (int)val->value.toNumber();
}

bool JsonGet::getBool(const char *child_name) const {
	return get(child_name)->value.getTag() == JSON_TRUE;
}

bool JsonGet::getBool(const char *child_name, bool default_value) const {
	const JsonNode *val = get(child_name);
	if (val) {
		JsonTag tag = val->value.getTag();
		if (tag == JSON_TRUE)
			return true;
		if (tag == JSON_FALSE)
			return false;
	}
	return default_value;
}

}  // namespace json
