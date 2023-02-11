#pragma once

#include <cmath>
#include <algorithm>

inline bool rel_equal(float a, float b, float precision) {
	float diff = fabsf(a - b);
	if (diff == 0.0f) {
		return true;
	}
	float range = std::max(fabsf(a), fabsf(b));
	float quot = diff / range;
	return quot < precision;
}


#define EXPECT_TRUE(a) if (!(a)) { printf("%s:%i: Test Fail\n", __FUNCTION__, __LINE__); return false; }
#define EXPECT_FALSE(a) if ((a)) { printf("%s:%i: Test Fail\n", __FUNCTION__, __LINE__); return false; }
#define EXPECT_EQ_INT(a, b) if ((a) != (b)) { printf("%s:%i: Test Fail\n%d\nvs\n%d\n", __FUNCTION__, __LINE__, (int)(a), (int)(b)); return false; }
#define EXPECT_EQ_HEX(a, b) if ((a) != (b)) { printf("%s:%i: Test Fail\n%x\nvs\n%x\n", __FUNCTION__, __LINE__, a, b); return false; }
#define EXPECT_EQ_FLOAT(a, b) if ((a) != (b)) { printf("%s:%i: Test Fail\n%0.7f\nvs\n%0.7f\n", __FUNCTION__, __LINE__, a, b); return false; }
#define EXPECT_APPROX_EQ_FLOAT(a, b) if (fabsf((a)-(b))>0.00001f) { printf("%s:%i: Test Fail\n%f\nvs\n%f\n", __FUNCTION__, __LINE__, a, b); /*return false;*/ }
#define EXPECT_REL_EQ_FLOAT(a, b, precision) if (!rel_equal(a, b, precision)) { printf("%s:%i: Test Fail\n%0.9f\nvs\n%0.9f\n", __FUNCTION__, __LINE__, a, b); /*return false;*/ }
#define EXPECT_EQ_STR(a, b) if (a != b) { printf("%s: Test Fail\n%s\nvs\n%s\n", __FUNCTION__, a.c_str(), b.c_str()); return false; }

#define RET(a) if (!(a)) { return false; }
