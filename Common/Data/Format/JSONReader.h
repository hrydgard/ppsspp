#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "ext/gason/gason.h"
#include "Common/Common.h"

namespace json {

struct JsonGet {
	JsonGet(const JsonValue &value) : value_(value) {}

	int numChildren() const;
	const JsonNode *get(const char *child_name) const;
	const JsonNode *get(const char *child_name, JsonTag type) const;
	const JsonNode *getArray(const char *child_name) const {
		return get(child_name, JSON_ARRAY);
	}
	const JsonGet getDict(const char *child_name) const {
		return JsonGet(get(child_name, JSON_OBJECT)->value);
	}
	const char *getStringOrNull(const char *child_name) const;
	const char *getStringOr(const char *child_name, const char *default_value) const;
	bool getString(const char *child_name, std::string *output) const;
	bool getStringVector(std::vector<std::string> *vec) const;
	double getFloat(const char *child_name) const;
	double getFloat(const char *child_name, double default_value) const;
	int getInt(const char *child_name) const;
	int getInt(const char *child_name, int default_value) const;
	bool getBool(const char *child_name) const;
	bool getBool(const char *child_name, bool default_value) const;

	bool hasChild(const char *child_name, JsonTag child_type) const {
		return get(child_name, child_type) != nullptr;
	}

	operator bool() const {
		return value_.getTag() != JSON_NULL;
	}

	JsonValue value_;
};

// Easy-wrapper
class JsonReader {
public:
	JsonReader(const std::string &filename);
	// Makes a copy, after this returns you can free the input buffer. Zero termination is not necessary.
	JsonReader(const char *data, size_t size) {
		buffer_ = (char *)malloc(size + 1);
		if (buffer_) {
			memcpy(buffer_, data, size);
			buffer_[size] = 0;
			parse();
		}
	}
	JsonReader(const JsonNode *node) {
		ok_ = true;
	}

	~JsonReader() {
		free(buffer_);
	}

	bool ok() const { return ok_; }

	JsonGet root() { return root_.getTag() == JSON_OBJECT ? JsonGet(root_) : JsonGet(JSON_NULL); }
	const JsonValue rootArray() const { return root_.getTag() == JSON_ARRAY ? root_ : JSON_NULL; }

	const JsonValue rootValue() const { return root_; }

private:
	bool parse();

	char *buffer_ = nullptr;
	JsonAllocator alloc_;
	JsonValue root_;
	bool ok_ = false;

	DISALLOW_COPY_AND_ASSIGN(JsonReader);
};

}  // namespace json
