#pragma once

#include <string.h>
#include <string>
#include <vector>
#include "base/logging.h"
#include "block_allocator.h"
#include "file/vfs.h"

enum json_type
{
	JSON_NULL,
	JSON_OBJECT,
	JSON_ARRAY,
	JSON_STRING,
	JSON_INT,
	JSON_FLOAT,
	JSON_BOOL,
};

struct json_value
{
	json_value() {}

	json_value *parent;
	json_value *next_sibling;
	json_value *first_child;
	json_value *last_child;

	char *name;
	union
	{
		char *string_value;
		int int_value;
		float float_value;
	};

	json_type type;

	int numChildren() const;
	int numSiblings() const;  // num siblings *after* this one only
	const json_value *get(const char *child_name) const;
	const json_value *get(const char *child_name, json_type type) const;
	const json_value *getArray(const char *child_name) const {
		return get(child_name, JSON_ARRAY);
	}
	const json_value *getDict(const char *child_name) const {
		return get(child_name, JSON_OBJECT);
	}
	const char *getString(const char *child_name) const;
	const char *getString(const char *child_name, const char *default_value) const;
	bool getStringVector(std::vector<std::string> *vec) const;
	float getFloat(const char *child_name) const;
	float getFloat(const char *child_name, float default_value) const;
	int getInt(const char *child_name) const;
	int getInt(const char *child_name, int default_value) const;
	bool getBool(const char *child_name) const;
	bool getBool(const char *child_name, bool default_value) const;
	
  bool hasChild(const char *child_name, json_type child_type) const {
    return get(child_name, child_type) != 0;
  }

private:
	DISALLOW_COPY_AND_ASSIGN(json_value);
};

// low level interface
json_value *json_parse(char *source, char **error_pos, char **error_desc, int *error_line, block_allocator *allocator);
void json_print(json_value *value, int ident = 0);


// Easy-wrapper
class JsonReader {
public:
	JsonReader(const std::string &filename);
	JsonReader(const char *data, size_t size) : alloc_(1 << 12) {
		buffer_ = (char *)malloc(size + 1);
		memcpy(buffer_, data, size);
		buffer_[size] = 0;
		parse();
	}
	
	~JsonReader() {
		if (buffer_)
			free(buffer_);
	}
	
	
	bool ok() const { return root_ != 0; }

	json_value *root() { return root_; }
	const json_value *root() const { return root_; }

	void print() {
		json_print(root_);
	}

private:
	bool parse() {
		char *error_pos;
		char *error_desc;
		int error_line;
		root_ = json_parse((char *)buffer_, &error_pos, &error_desc, &error_line, &alloc_);
		if (!root_) {
			ELOG("Error at (%i): %s\n%s\n\n", error_line, error_desc, error_pos);
			return false;
		}
		return true;
	}

	char *buffer_;
	block_allocator alloc_;
	json_value *root_;

	DISALLOW_COPY_AND_ASSIGN(JsonReader);
};

// TODO: Make this a push/pop interface similar to JsonWriter. Maybe 
// we can get to the point where reading and writing is near identical or the same code.
class JsonCursor {
public:

};