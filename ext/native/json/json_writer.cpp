#include <iomanip>
#include <cstring>
#include "ext/vjson/json.h"
#include "json/json_writer.h"

JsonWriter::JsonWriter(int flags) {
	pretty_ = (flags & PRETTY) != 0;
	str_.imbue(std::locale::classic());
}

JsonWriter::~JsonWriter() {
}

void JsonWriter::begin() {
	str_ << "{";
	stack_.push_back(StackEntry(DICT));
}

void JsonWriter::beginArray() {
	str_ << "[";
	stack_.push_back(StackEntry(ARRAY));
}

void JsonWriter::beginRaw() {
	// For the uncommon case of writing a value directly, to avoid duplicated code.
	stack_.push_back(StackEntry(RAW));
}

void JsonWriter::end() {
	pop();
	if (pretty_)
		str_ << "\n";
}

const char *JsonWriter::indent(int n) const {
	if (!pretty_)
		return "";
	static const char * const whitespace = "                                ";
	if (n > 32) {
		// Avoid crash.
		return whitespace;
	}
	return whitespace + (32 - n);
}

const char *JsonWriter::indent() const {
	if (!pretty_)
		return "";
	int amount = (int)stack_.size() + 1;
	amount *= 2;	// 2-space indent.
	return indent(amount);
}

const char *JsonWriter::arrayIndent() const {
	if (!pretty_)
		return "";
	int amount = (int)stack_.size() + 1;
	amount *= 2;	// 2-space indent.
	return stack_.back().first ? indent(amount) : "";
}

const char *JsonWriter::comma() const {
	if (stack_.back().first) {
		return "";
	} else {
		return pretty_ ? ",\n" : ",";
	}
}

const char *JsonWriter::arrayComma() const {
	if (stack_.back().first) {
		return "\n";
	} else {
		return pretty_ ? ", " : ",";
	}
}

void JsonWriter::pushDict(const char *name) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << "\": {";
	stack_.push_back(StackEntry(DICT));
}

void JsonWriter::pushArray(const char *name) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << "\": [";
	stack_.push_back(StackEntry(ARRAY));
}

void JsonWriter::writeBool(bool value) {
	str_ << arrayComma() << arrayIndent() << (value ? "true" : "false");
	stack_.back().first = false;
}

void JsonWriter::writeBool(const char *name, bool value) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << "\": " << (value ? "true" : "false");
	stack_.back().first = false;
}

void JsonWriter::writeInt(int value) {
	str_ << arrayComma() << arrayIndent() << value;
	stack_.back().first = false;
}

void JsonWriter::writeInt(const char *name, int value) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << "\": " << value;
	stack_.back().first = false;
}

void JsonWriter::writeFloat(double value) {
	str_ << arrayComma() << arrayIndent() << value;
	stack_.back().first = false;
}

void JsonWriter::writeFloat(const char *name, double value) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << "\": " << value;
	stack_.back().first = false;
}

void JsonWriter::writeString(const char *value) {
	str_ << arrayComma() << arrayIndent() << "\"";
	writeEscapedString(value);
	str_ << "\"";
	stack_.back().first = false;
}

void JsonWriter::writeString(const char *name, const char *value) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << "\": \"";
	writeEscapedString(value);
	str_ << "\"";
	stack_.back().first = false;
}

void JsonWriter::writeRaw(const char *value) {
	str_ << arrayComma() << arrayIndent() << value;
	stack_.back().first = false;
}

void JsonWriter::writeRaw(const char *name, const char *value) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << (pretty_ ? "\": " : "\":");
	str_ << value;
	stack_.back().first = false;
}

void JsonWriter::pop() {
	BlockType type = stack_.back().type;
	stack_.pop_back();
	if (pretty_)
		str_ << "\n" << indent();
	switch (type) {
	case ARRAY:
		str_ << "]";
		break;
	case DICT:
		str_ << "}";
		break;
	case RAW:
		break;
	}
	if (stack_.size() > 0)
		stack_.back().first = false;
}

void JsonWriter::writeEscapedString(const char *str) {
	size_t pos = 0;
	size_t len = strlen(str);

	auto update = [&](size_t current, size_t skip = 0) {
		size_t end = current;
		if (pos < end)
			str_ << std::string(str + pos, end - pos);
		pos = end + skip;
	};

	for (size_t i = 0; i < len; ++i) {
		if (str[i] == '\\' || str[i] == '"' || str[i] == '/') {
			update(i);
			str_ << '\\';
		} else if (str[i] == '\r') {
			update(i, 1);
			str_ << "\\r";
		} else if (str[i] == '\n') {
			update(i, 1);
			str_ << "\\n";
		} else if (str[i] == '\t') {
			update(i, 1);
			str_ << "\\t";
		} else if (str[i] < 32) {
			update(i, 1);
			str_ << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)str[i] << std::dec << std::setw(0);
		}
	}

	if (pos != 0) {
		update(len);
	} else {
		str_ << str;
	}
}

static void json_stringify_object(JsonWriter &writer, const json_value *value);
static void json_stringify_array(JsonWriter &writer, const json_value *value);

std::string json_stringify(const json_value *value) {
	JsonWriter writer;

	// Handle direct values too, not just objects.
	switch (value->type) {
	case JSON_NULL:
	case JSON_STRING:
	case JSON_INT:
	case JSON_FLOAT:
	case JSON_BOOL:
		writer.beginRaw();
		// It's the same as a one entry array without brackets, so reuse.
		json_stringify_array(writer, value);
		break;

	case JSON_OBJECT:
		writer.begin();
		for (const json_value *it = value->first_child; it; it = it->next_sibling) {
			json_stringify_object(writer, it);
		}
		break;
	case JSON_ARRAY:
		writer.beginArray();
		for (const json_value *it = value->first_child; it; it = it->next_sibling) {
			json_stringify_array(writer, it);
		}
		break;
	}

	writer.end();
	return writer.str();
}

static void json_stringify_object(JsonWriter &writer, const json_value *value) {
	switch (value->type) {
	case JSON_NULL:
		writer.writeRaw(value->name, "null");
		break;
	case JSON_STRING:
		writer.writeString(value->name, value->string_value);
		break;
	case JSON_INT:
		writer.writeInt(value->name, value->int_value);
		break;
	case JSON_FLOAT:
		writer.writeFloat(value->name, value->float_value);
		break;
	case JSON_BOOL:
		writer.writeBool(value->name, value->int_value != 0);
		break;

	case JSON_OBJECT:
		writer.pushDict(value->name);
		for (const json_value *it = value->first_child; it; it = it->next_sibling) {
			json_stringify_object(writer, it);
		}
		writer.pop();
		break;
	case JSON_ARRAY:
		writer.pushArray(value->name);
		for (const json_value *it = value->first_child; it; it = it->next_sibling) {
			json_stringify_array(writer, it);
		}
		writer.pop();
		break;
	}
}

static void json_stringify_array(JsonWriter &writer, const json_value *value) {
	switch (value->type) {
	case JSON_NULL:
		writer.writeRaw("null");
		break;
	case JSON_STRING:
		writer.writeString(value->string_value);
		break;
	case JSON_INT:
		writer.writeInt(value->int_value);
		break;
	case JSON_FLOAT:
		writer.writeFloat(value->float_value);
		break;
	case JSON_BOOL:
		writer.writeBool(value->int_value != 0);
		break;

	case JSON_OBJECT:
		writer.begin();
		for (const json_value *it = value->first_child; it; it = it->next_sibling) {
			json_stringify_object(writer, it);
		}
		writer.pop();
		break;
	case JSON_ARRAY:
		writer.beginArray();
		for (const json_value *it = value->first_child; it; it = it->next_sibling) {
			json_stringify_array(writer, it);
		}
		writer.pop();
		break;
	}
}
