#pragma once

#include <string>
#include "Common/Math/expression_parser.h"
#include "Core/MIPS/MIPSDebugInterface.h"

enum class WatchFormat {
	HEX,
	INT,
	FLOAT,
	STR,
};

struct WatchInfo {
	WatchInfo() = default;
	WatchInfo(const std::string &name, const std::string &expr, MIPSDebugInterface *debug, WatchFormat format = WatchFormat::HEX)
		: name(name), originalExpression(expr), format(format) {
		initExpression(debug, expr.c_str(), expression);
	}
	void SetExpression(const std::string &expr, MIPSDebugInterface *debug) {
		originalExpression = expr;
		initExpression(debug, expr.c_str(), expression);
	}
	std::string name;
	std::string originalExpression;
	PostfixExpression expression;
	WatchFormat format = WatchFormat::HEX;
	uint32_t currentValue = 0;
	uint32_t lastValue = 0;
	int steppingCounter = -1;
	bool evaluateFailed = false;
};
