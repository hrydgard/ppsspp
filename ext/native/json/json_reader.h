#include <string>
#include <vector>
#include "base/basictypes.h"
#include "base/logging.h"
#include "ext/gason/gason.h"

struct JsonGet {
	JsonGet(const JsonValue &value) : value_(value) {
	}

	int numChildren() const;
	const JsonNode *get(const char *child_name) const;
	const JsonNode *get(const char *child_name, JsonTag type) const;
	const JsonNode *getArray(const char *child_name) const {
		return get(child_name, JSON_ARRAY);
	}
	const JsonGet getDict(const char *child_name) const {
		return JsonGet(get(child_name, JSON_OBJECT)->value);
	}
	const char *getStringOrDie(const char *child_name) const;
	const char *getString(const char *child_name, const char *default_value) const;
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
	JsonReader(const void *data, size_t size) {
		buffer_ = (char *)malloc(size + 1);
		memcpy(buffer_, data, size);
		buffer_[size] = 0;
		parse();
	}
	JsonReader(const JsonNode *node) {
		ok_ = true;
	}

	~JsonReader() {
		if (buffer_)
			free(buffer_);
	}

	bool ok() const { return ok_; }

	JsonGet root() { return root_.getTag() == JSON_OBJECT ? JsonGet(root_) : JsonGet(JSON_NULL); }
	const JsonValue rootArray() const { return root_.getTag() == JSON_ARRAY ? root_ : JSON_NULL; }

	const JsonValue rootValue() const { return root_; }

private:
	bool parse() {
		char *error_pos;
		int status = jsonParse(buffer_, &error_pos, &root_, alloc_);
		if (status != JSON_OK) {
			ELOG("Error at (%i): %s\n%s\n\n", (int)(error_pos - buffer_), jsonStrError(status), error_pos);
			return false;
		}
		ok_ = true;
		return true;
	}

	char *buffer_ = nullptr;
	JsonAllocator alloc_;
	JsonValue root_;
	bool ok_ = false;

	DISALLOW_COPY_AND_ASSIGN(JsonReader);
};
