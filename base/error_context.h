#pragma once

// do not inherit

// TODO: Have the crash handler investigate the error context.
// Must only be constructed on the stack - DO NOT put these on the heap.
class _ErrorContext
{
public:
	_ErrorContext(const char *name, const char *data = 0);
	~_ErrorContext();

	// Logs the current context stack.
	static void Log(const char *message);
};

#define ErrorContext(...) _ErrorContext __ec(__VA_ARGS__)
#define LogErrorContext(msg) _ErrorContext::Log(msg)
