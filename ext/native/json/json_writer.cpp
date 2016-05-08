#include "json/json_writer.h"

JsonWriter::JsonWriter() {
}

JsonWriter::~JsonWriter() {
}

void JsonWriter::begin() {
	str_ << "{";
	stack_.push_back(StackEntry(DICT));
}

void JsonWriter::end() {
	pop();
	str_ << "\n";
}

const char *JsonWriter::indent(int n) const {
	static const char * const whitespace = "															";
	return whitespace + (32 - n);
}

const char *JsonWriter::indent() const {
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
		return ",";
	}
}

const char *JsonWriter::arrayComma() const {
	if (stack_.back().first) {
		return "\n";
	} else {
		return ", ";
	}
}

void JsonWriter::pushDict(const char *name) {
	str_ << comma() << "\n" << indent() << "\"" << name << "\": {";
	stack_.push_back(StackEntry(DICT));
}

void JsonWriter::pushArray(const char *name) {
	str_ << comma() << "\n" << indent() << "\"" << name << "\": [";
	stack_.push_back(StackEntry(ARRAY));
}

void JsonWriter::writeBool(bool value) {
	str_ << arrayComma() << arrayIndent() << (value ? "true" : "false");
	stack_.back().first = false;
}

void JsonWriter::writeBool(const char *name, bool value) {
	str_ << comma() << "\n" << indent() << "\"" << name << "\": " << (value ? "true" : "false");
	stack_.back().first = false;
}

void JsonWriter::writeInt(int value) {
	str_ << arrayComma() << arrayIndent() << value;
	stack_.back().first = false;
}

void JsonWriter::writeInt(const char *name, int value) {
	str_ << comma() << "\n" << indent() << "\"" << name << "\": " << value;
	stack_.back().first = false;
}

void JsonWriter::writeFloat(double value) {
	str_ << arrayComma() << arrayIndent() << value;
	stack_.back().first = false;
}

void JsonWriter::writeFloat(const char *name, double value) {
	str_ << comma() << "\n" << indent() << "\"" << name << "\": " << value;
	stack_.back().first = false;
}

void JsonWriter::writeString(const char *value) {
	str_ << arrayComma() << arrayIndent() << "\"" << value << "\"";
	stack_.back().first = false;
}

void JsonWriter::writeString(const char *name, const char *value) {
	str_ << comma() << "\n" << indent() << "\"" << name << "\": \"" << value << "\"";
	stack_.back().first = false;
}

void JsonWriter::pop() {
	BlockType type = stack_.back().type;
	stack_.pop_back();
	switch (type) {
	case ARRAY:
		str_ << "\n" << indent() << "]";
		break;
	case DICT:
		str_ << "\n" << indent() << "}";
		break;
	}
	if (stack_.size() > 0)
		stack_.back().first = false;
}
