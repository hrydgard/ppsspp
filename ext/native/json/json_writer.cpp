#include <iomanip>
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
	int amount = (int)stack_.size() + 1;
	amount *= 2;	// 2-space indent.
	return stack_.back().first ? indent(amount) : "";
}

const char *JsonWriter::comma() const {
	if (stack_.back().first) {
		return "";
	} else {
		return ",\n";
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
