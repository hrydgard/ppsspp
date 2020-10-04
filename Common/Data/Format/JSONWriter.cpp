#include <iomanip>
#include <cmath>
#include <cstring>

#include "Common/Data/Format/JSONReader.h"
#include "Common/Data/Format/JSONWriter.h"

namespace json {

JsonWriter::JsonWriter(int flags) {
	pretty_ = (flags & PRETTY) != 0;
	str_.imbue(std::locale::classic());
	// Let's maximize precision by default.
	str_.precision(53);
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
		return pretty_ ? "\n" : "";
	} else {
		return pretty_ ? ", " : ",";
	}
}

void JsonWriter::pushDict() {
	str_ << arrayComma() << arrayIndent() << "{";
	stack_.back().first = false;
	stack_.push_back(StackEntry(DICT));
}

void JsonWriter::pushDict(const std::string &name) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << (pretty_ ? "\": {" : "\":{");
	stack_.back().first = false;
	stack_.push_back(StackEntry(DICT));
}

void JsonWriter::pushArray() {
	str_ << arrayComma() << arrayIndent() << "[";
	stack_.back().first = false;
	stack_.push_back(StackEntry(ARRAY));
}

void JsonWriter::pushArray(const std::string &name) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << (pretty_ ? "\": [" : "\":[");
	stack_.push_back(StackEntry(ARRAY));
}

void JsonWriter::writeBool(bool value) {
	str_ << arrayComma() << arrayIndent() << (value ? "true" : "false");
	stack_.back().first = false;
}

void JsonWriter::writeBool(const std::string &name, bool value) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << (pretty_ ? "\": " : "\":") << (value ? "true" : "false");
	stack_.back().first = false;
}

void JsonWriter::writeInt(int value) {
	str_ << arrayComma() << arrayIndent() << value;
	stack_.back().first = false;
}

void JsonWriter::writeInt(const std::string &name, int value) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << (pretty_ ? "\": " : "\":") << value;
	stack_.back().first = false;
}

void JsonWriter::writeUint(uint32_t value) {
	str_ << arrayComma() << arrayIndent() << value;
	stack_.back().first = false;
}

void JsonWriter::writeUint(const std::string &name, uint32_t value) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << (pretty_ ? "\": " : "\":") << value;
	stack_.back().first = false;
}

void JsonWriter::writeFloat(double value) {
	str_ << arrayComma() << arrayIndent();
	if (std::isfinite(value))
		str_ << value;
	else
		str_ << "null";
	stack_.back().first = false;
}

void JsonWriter::writeFloat(const std::string &name, double value) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << (pretty_ ? "\": " : "\":");
	if (std::isfinite(value))
		str_ << value;
	else
		str_ << "null";
	stack_.back().first = false;
}

void JsonWriter::writeString(const std::string &value) {
	str_ << arrayComma() << arrayIndent() << "\"";
	writeEscapedString(value);
	str_ << "\"";
	stack_.back().first = false;
}

void JsonWriter::writeString(const std::string &name, const std::string &value) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << (pretty_ ? "\": \"" : "\":\"");
	writeEscapedString(value);
	str_ << "\"";
	stack_.back().first = false;
}

void JsonWriter::writeRaw(const std::string &value) {
	str_ << arrayComma() << arrayIndent() << value;
	stack_.back().first = false;
}

void JsonWriter::writeRaw(const std::string &name, const std::string &value) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << (pretty_ ? "\": " : "\":");
	str_ << value;
	stack_.back().first = false;
}

void JsonWriter::writeNull() {
	str_ << arrayComma() << arrayIndent() << "null";
	stack_.back().first = false;
}

void JsonWriter::writeNull(const std::string &name) {
	str_ << comma() << indent() << "\"";
	writeEscapedString(name);
	str_ << (pretty_ ? "\": " : "\":") << "null";
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

void JsonWriter::writeEscapedString(const std::string &str) {
	size_t pos = 0;
	const size_t len = str.size();

	auto update = [&](size_t current, size_t skip = 0) {
		size_t end = current;
		if (pos < end)
			str_ << str.substr(pos, end - pos);
		pos = end + skip;
	};

	for (size_t i = 0; i < len; ++i) {
		switch (str[i]) {
		case '\\':
		case '"':
		case '/':
			update(i);
			str_ << '\\';
			break;

		case '\r':
			update(i, 1);
			str_ << "\\r";
			break;
			break;

		case '\n':
			update(i, 1);
			str_ << "\\n";
			break;
			break;

		case '\t':
			update(i, 1);
			str_ << "\\t";
			break;

		case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 11:
		case 12: case 14: case 15: case 16: case 17: case 18: case 19: case 20:
		case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28:
		case 29: case 30: case 31:
			update(i, 1);
			str_ << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)str[i] << std::dec << std::setw(0);
			break;

		default:
			break;
		}
	}

	if (pos != 0) {
		update(len);
	} else {
		str_ << str;
	}
}

static void json_stringify_object(JsonWriter &writer, const JsonNode *node);
static void json_stringify_array(JsonWriter &writer, const JsonNode *node);

std::string json_stringify(const JsonNode *node) {
	JsonWriter writer;

	// Handle direct values too, not just objects.
	switch (node->value.getTag()) {
	case JSON_NULL:
	case JSON_STRING:
	case JSON_NUMBER:
	case JSON_TRUE:
	case JSON_FALSE:
		writer.beginRaw();
		// It's the same as a one entry array without brackets, so reuse.
		json_stringify_array(writer, node);
		break;

	case JSON_OBJECT:
		writer.begin();
		for (const JsonNode *it : node->value) {
			json_stringify_object(writer, it);
		}
		break;
	case JSON_ARRAY:
		writer.beginArray();
		for (const JsonNode *it : node->value) {
			json_stringify_array(writer, it);
		}
		break;
	}

	writer.end();
	return writer.str();
}

static void json_stringify_object(JsonWriter &writer, const JsonNode *node) {
	switch (node->value.getTag()) {
	case JSON_NULL:
		writer.writeNull(node->key);
		break;
	case JSON_STRING:
		writer.writeString(node->key, node->value.toString());
		break;
	case JSON_NUMBER:
		writer.writeFloat(node->key, node->value.toNumber());
		break;
	case JSON_TRUE:
		writer.writeBool(node->key, true);
		break;
	case JSON_FALSE:
		writer.writeBool(node->key, false);
		break;

	case JSON_OBJECT:
		writer.pushDict(node->key);
		for (const JsonNode *it : node->value) {
			json_stringify_object(writer, it);
		}
		writer.pop();
		break;
	case JSON_ARRAY:
		writer.pushArray(node->key);
		for (const JsonNode *it : node->value) {
			json_stringify_array(writer, it);
		}
		writer.pop();
		break;
	}
}

static void json_stringify_array(JsonWriter &writer, const JsonNode *node) {
	switch (node->value.getTag()) {
	case JSON_NULL:
		writer.writeRaw("null");
		break;
	case JSON_STRING:
		writer.writeString(node->value.toString());
		break;
	case JSON_NUMBER:
		writer.writeFloat(node->value.toNumber());
		break;
	case JSON_TRUE:
		writer.writeBool(true);
		break;
	case JSON_FALSE:
		writer.writeBool(false);
		break;

	case JSON_OBJECT:
		writer.pushDict();
		for (const JsonNode *it : node->value) {
			json_stringify_object(writer, it);
		}
		writer.pop();
		break;
	case JSON_ARRAY:
		writer.pushArray();
		for (const JsonNode *it : node->value) {
			json_stringify_array(writer, it);
		}
		writer.pop();
		break;
	}
}

}  // namespace json
