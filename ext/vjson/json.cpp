#include <memory.h>
#include <stdio.h>
#include <string.h>
#include "json.h"
#include "file/easy_file.h"
#include "file/zip_read.h"
#include "file/vfs.h"

// true if character represent a digit
#define IS_DIGIT(c) (c >= '0' && c <= '9')

int json_value::numChildren() const {
	int count = 0;
	const json_value *c = first_child;
	while (c) {
		count++;
    c = c->next_sibling;
	}
	return count;
}

// only works right for first child. includes itself in count.
int json_value::numSiblings() const {
	const json_value *s = next_sibling;
	int count = 1;
	while (s) {
		count++;
    s = s->next_sibling;
	}
	return count;
}

const json_value *json_value::get(const char *child_name) const {
	const json_value *c = first_child;
	while (c) {
		if (!strcmp(c->name, child_name)) {
			return c;
		}
		c = c->next_sibling;
	}
	return 0;
}

const json_value *json_value::get(const char *child_name, json_type type) const {
	const json_value *v = get(child_name);
	if (v && type == v->type)
		return v;
	else
		return 0;
}

const char *json_value::getString(const char *child_name) const {
	const json_value *val = get(child_name, JSON_STRING);
	if (val)
		return val->string_value;
	else
		FLOG("String %s missing from node %s", child_name, this->name);
	return 0;
}

const char *json_value::getString(const char *child_name, const char *default_value) const {
	const json_value *val = get(child_name, JSON_STRING);
	if (!val)
		return default_value;
	return val->string_value;
}

bool json_value::getStringVector(std::vector<std::string> *vec) const {
	vec->clear();
	if (type == JSON_ARRAY) {
		json_value *val = first_child;
		while (val) {
			if (val->type == JSON_STRING) {
				vec->push_back(val->string_value);
			}
		}
		return true;
	} else {
		return false;
	}
}

float json_value::getFloat(const char *child_name) const {
	return get(child_name, JSON_FLOAT)->float_value;
}

float json_value::getFloat(const char *child_name, float default_value) const {
	const json_value *val = get(child_name, JSON_FLOAT);
	if (!val) {
		// Let's try int.
		val = get(child_name, JSON_INT);
		if (!val)
			return default_value;
		return val->int_value;
	}
	return val->float_value;
}

int json_value::getInt(const char *child_name) const {
	return get(child_name, JSON_INT)->int_value;
}

int json_value::getInt(const char *child_name, int default_value) const {
	const json_value *val = get(child_name, JSON_INT);
	if (!val)
		return default_value;
	return val->int_value;
}

bool json_value::getBool(const char *child_name) const {
	return get(child_name, JSON_BOOL)->int_value != 0 ? true : false;
}

bool json_value::getBool(const char *child_name, bool default_value) const {
	const json_value *val = get(child_name, JSON_BOOL);
	if (!val)
		return default_value;
	return val->int_value != 0 ? true : false;
}


// convert string to integer
char *atoi(char *first, char *last, int *out)
{
	int sign = 1;
	if (first != last)
	{
		if (*first == '-')
		{
			sign = -1;
			++first;
		}
		else if (*first == '+')
		{
			++first;
		}
	}

	int result = 0;
	for (; first != last && IS_DIGIT(*first); ++first)
	{
		result = 10 * result + (*first - '0');
	}
	*out = result * sign;

	return first;
}

// convert hexadecimal string to unsigned integer
char *hatoui(char *first, char *last, unsigned int *out)
{
	unsigned int result = 0;
	for (; first != last; ++first)
	{
		int digit;
		if (IS_DIGIT(*first))
		{
			digit = *first - '0';
		}
		else if (*first >= 'a' && *first <= 'f')
		{
			digit = *first - 'a' + 10;
		}
		else if (*first >= 'A' && *first <= 'F')
		{
			digit = *first - 'A' + 10;
		}
		else
		{
			break;
		}
		result = 16 * result + digit;
	}
	*out = result;

	return first;
}

// convert string to floating point
char *atof(char *first, char *last, float *out)
{
	// sign
	float sign = 1;
	if (first != last)
	{
		if (*first == '-')
		{
			sign = -1;
			++first;
		}
		else if (*first == '+')
		{
			++first;
		}
	}

	// integer part
	float result = 0;
	for (; first != last && IS_DIGIT(*first); ++first)
	{
		result = 10 * result + (*first - '0');
	}

	// fraction part
	if (first != last && *first == '.')
	{
		++first;

		float inv_base = 0.1f;
		for (; first != last && IS_DIGIT(*first); ++first)
		{
			result += (*first - '0') * inv_base;
			inv_base *= 0.1f;
		}
	}

	// result w\o exponent
	result *= sign;

	// exponent
	bool exponent_negative = false;
	int exponent = 0;
	if (first != last && (*first == 'e' || *first == 'E'))
	{
		++first;

		if (*first == '-')
		{
			exponent_negative = true;
			++first;
		}
		else if (*first == '+')
		{
			++first;
		}

		for (; first != last && IS_DIGIT(*first); ++first)
		{
			exponent = 10 * exponent + (*first - '0');
		}
	}

	if (exponent)
	{
		float power_of_ten = 10;
		for (; exponent > 1; exponent--)
		{
			power_of_ten *= 10;
		}

		if (exponent_negative)
		{
			result /= power_of_ten;
		}
		else
		{
			result *= power_of_ten;
		}
	}

	*out = result;

	return first;
}

json_value *json_alloc(block_allocator *allocator)
{
	json_value *value = (json_value *)allocator->malloc(sizeof(json_value));
	memset(value, 0, sizeof(json_value));
	return value;
}

void json_append(json_value *lhs, json_value *rhs)
{
	rhs->parent = lhs;
	if (lhs->last_child)
	{
		lhs->last_child = lhs->last_child->next_sibling = rhs;
	}
	else
	{
		lhs->first_child = lhs->last_child = rhs;
	}
}

#define ERROR(it, desc)\
	*error_pos = it;\
	*error_desc = (char *)desc;\
	*error_line = 1 - escaped_newlines;\
	for (char *c = it; c != source; --c)\
		if (*c == '\n') ++*error_line;\
	return 0

#define CHECK_TOP() if (!top) {ERROR(it, "Unexpected character");}

json_value *json_parse(char *source, char **error_pos, char **error_desc, int *error_line, block_allocator *allocator)
{
	json_value *root = 0;
	json_value *top = 0;

	char *name = 0;
	char *it = source;

	int escaped_newlines = 0;

	while (*it)
	{
		switch (*it)
		{
		case '{':
		case '[':
			{
				// create new value
				json_value *object = json_alloc(allocator);

				// name
				object->name = name;
				name = 0;

				// type
				object->type = (*it == '{') ? JSON_OBJECT : JSON_ARRAY;

				// skip open character
				++it;

				// set top and root
				if (top)
				{
					json_append(top, object);
				}
				else if (!root)
				{
					root = object;
				}
				else
				{
					ERROR(it, "Second root. Only one root allowed");
				}
				top = object;
			}
			break;

		case '}':
		case ']':
			{
				if (!top || top->type != ((*it == '}') ? JSON_OBJECT : JSON_ARRAY))
				{
					ERROR(it, "Mismatch closing brace/bracket");
				}

				// skip close character
				++it;

				// set top
				top = top->parent;
			}
			break;

		case ':':
			if (!top || top->type != JSON_OBJECT)
			{
				ERROR(it, "Unexpected character");
			}
			++it;
			break;

		case ',':
			CHECK_TOP();
			++it;
			break;

		case '"':
			{
				CHECK_TOP();

				// skip '"' character
				++it;

				char *first = it;
				char *last = it;
				while (*it)
				{
					if ((unsigned char)*it < '\x20')
					{
						ERROR(first, "Control characters not allowed in strings");
					}
					else if (*it == '\\')
					{
						switch (it[1])
						{
						case '"':
							*last = '"';
							break;
						case '\\':
							*last = '\\';
							break;
						case '/':
							*last = '/';
							break;
						case 'b':
							*last = '\b';
							break;
						case 'f':
							*last = '\f';
							break;
						case 'n':
							*last = '\n';
							++escaped_newlines;
							break;
						case 'r':
							*last = '\r';
							break;
						case 't':
							*last = '\t';
							break;
						case 'u':
							{
								unsigned int codepoint;
								if (hatoui(it + 2, it + 6, &codepoint) != it + 6)
								{
									ERROR(it, "Bad unicode codepoint");
								}

								if (codepoint <= 0x7F)
								{
									*last = (char)codepoint;
								}
								else if (codepoint <= 0x7FF)
								{
									*last++ = (char)(0xC0 | (codepoint >> 6));
									*last = (char)(0x80 | (codepoint & 0x3F));
								}
								else if (codepoint <= 0xFFFF)
								{
									*last++ = (char)(0xE0 | (codepoint >> 12));
									*last++ = (char)(0x80 | ((codepoint >> 6) & 0x3F));
									*last = (char)(0x80 | (codepoint & 0x3F));
								}
							}
							it += 4;
							break;
						default:
							ERROR(first, "Unrecognized escape sequence");
						}

						++last;
						it += 2;
					}
					else if (*it == '"')
					{
						*last = 0;
						++it;
						break;
					}
					else
					{
						*last++ = *it++;
					}
				}

				if (!name && top->type == JSON_OBJECT)
				{
					// field name in object
					name = first;
				}
				else
				{
					// new string value
					json_value *object = json_alloc(allocator);

					object->name = name;
					name = 0;

					object->type = JSON_STRING;
					object->string_value = first;

					json_append(top, object);
				}
			}
			break;

		case 'n':
		case 't':
		case 'f':
			{
				CHECK_TOP();

				// new null/bool value
				json_value *object = json_alloc(allocator);

				object->name = name;
				name = 0;

				// null
				if (it[0] == 'n' && it[1] == 'u' && it[2] == 'l' && it[3] == 'l')
				{
					object->type = JSON_NULL;
					it += 4;
				}
				// true
				else if (it[0] == 't' && it[1] == 'r' && it[2] == 'u' && it[3] == 'e')
				{
					object->type = JSON_BOOL;
					object->int_value = 1;
					it += 4;
				}
				// false
				else if (it[0] == 'f' && it[1] == 'a' && it[2] == 'l' && it[3] == 's' && it[4] == 'e')
				{
					object->type = JSON_BOOL;
					object->int_value = 0;
					it += 5;
				}
				else
				{
					ERROR(it, "Unknown identifier");
				}

				json_append(top, object);
			}
			break;

		case '-':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			{
				CHECK_TOP();

				// new number value
				json_value *object = json_alloc(allocator);

				object->name = name;
				name = 0;

				object->type = JSON_INT;

				char *first = it;
				while (*it != '\x20' && *it != '\x9' && *it != '\xD' && *it != '\xA' && *it != ',' && *it != ']' && *it != '}')
				{
					if (*it == '.' || *it == 'e' || *it == 'E')
					{
						object->type = JSON_FLOAT;
					}
					++it;
				}

				if (object->type == JSON_INT && atoi(first, it, &object->int_value) != it)
				{
					ERROR(first, "Bad integer number");
				}

				if (object->type == JSON_FLOAT && atof(first, it, &object->float_value) != it)
				{
					ERROR(first, "Bad float number");
				}

				json_append(top, object);
			}
			break;

		default:
			ERROR(it, "Unexpected character");
		}

		// skip white space
		while (*it == '\x20' || *it == '\x9' || *it == '\xD' || *it == '\xA')
		{
			++it;
		}
	}

	if (top)
	{
		ERROR(it, "Not all objects/arrays have been properly closed");
	}

	return root;
}

#define IDENT(n) for (int i = 0; i < n; ++i) printf("  ")

void json_print(json_value *value, int ident)
{
	IDENT(ident);
	if (value->name) printf("\"%s\" = ", value->name);
	switch(value->type)
	{
	case JSON_NULL:
		printf("null\n");
		break;
	case JSON_OBJECT:
	case JSON_ARRAY:
		printf(value->type == JSON_OBJECT ? "{\n" : "[\n");
		for (json_value *it = value->first_child; it; it = it->next_sibling)
		{
			json_print(it, ident + 1);
		}
		IDENT(ident);
		printf(value->type == JSON_OBJECT ? "}\n" : "]\n");
		break;
	case JSON_STRING:
		printf("\"%s\"\n", value->string_value);
		break;
	case JSON_INT:
		printf("%d\n", value->int_value);
		break;
	case JSON_FLOAT:
		printf("%f\n", value->float_value);
		break;
	case JSON_BOOL:
		printf(value->int_value ? "true\n" : "false\n");
		break;
	}
}

JsonReader::JsonReader(const std::string &filename) : alloc_(1 << 12), root_(0) {
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
