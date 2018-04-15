#include "file/zip_read.h"
#include "file/vfs.h"
#include "json/json_reader.h"

JsonReader::JsonReader(const std::string &filename) {
	size_t buf_size;
	buffer_ = (char *)VFSReadFile(filename.c_str(), &buf_size);
	if (buffer_) {
		parse();
	} else {
		// Okay, try to read on the local file system
		buffer_ = (char *)ReadLocalFile(filename.c_str(), &buf_size);
		if (buffer_) {
			parse();
		} else {
			ELOG("Failed to read json %s", filename.c_str());
		}
	}
}

int JsonGet::numChildren() const {
	int count = 0;
	if (value_.getTag() == JSON_OBJECT || value_.getTag() == JSON_ARRAY) {
		for (auto it : value_) {
			count++;
		}
	}
	return count;
}

const JsonNode *JsonGet::get(const char *child_name) const {
	if (!child_name) {
		FLOG("JSON: Cannot get from null child name");
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

const char *JsonGet::getStringOrDie(const char *child_name) const {
	const JsonNode *val = get(child_name, JSON_STRING);
	if (val)
		return val->value.toString();
	FLOG("String '%s' missing from node", child_name);
	return nullptr;
}

const char *JsonGet::getString(const char *child_name, const char *default_value) const {
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
