#include "base/basictypes.h"
#include "base/logging.h"
#include "base/error_context.h"
#include <vector>

// TODO: Fix this threadery
#ifndef _WIN32
#undef __THREAD
#define __THREAD
#endif

__THREAD std::vector<const char *> *_error_context_name;
__THREAD std::vector<const char *> *_error_context_data;

_ErrorContext::_ErrorContext(const char *name, const char *data) {
	if (!_error_context_name) {
		_error_context_name = new std::vector<const char *>();
		_error_context_data = new std::vector<const char *>();
		_error_context_name->reserve(16);
		_error_context_data->reserve(16);
	}
	_error_context_name->push_back(name);
	_error_context_data->push_back(data);
}

_ErrorContext::~_ErrorContext() {
	_error_context_name->pop_back();
	_error_context_data->pop_back();
}

void _ErrorContext::Log(const char *message) {
	ILOG("EC: %s", message);
	for (size_t i = 0; i < _error_context_name->size(); i++) {
		if ((*_error_context_data)[i] != 0) {
			ILOG("EC: %s: %s", (*_error_context_name)[i], (*_error_context_data)[i]);
		} else {
			ILOG("EC: %s: %s", (*_error_context_name)[i], (*_error_context_data)[i]);
		}
	}
}
