#pragma once

#define EXPECT_TRUE(a) if (!(a)) { printf("%s:%i: Test Fail\n", __FUNCTION__, __LINE__); return false; }
#define EXPECT_FALSE(a) if ((a)) { printf("%s:%i: Test Fail\n", __FUNCTION__, __LINE__); return false; }
#define EXPECT_EQ_INT(a, b) if ((a) != (b)) { printf("%s:%i: Test Fail\n%d\nvs\n%d\n", __FUNCTION__, __LINE__, a, b); return false; }
#define EXPECT_EQ_FLOAT(a, b) if ((a) != (b)) { printf("%s:%i: Test Fail\n%f\nvs\n%f\n", __FUNCTION__, __LINE__, a, b); return false; }
#define EXPECT_APPROX_EQ_FLOAT(a, b) if (fabsf((a)-(b))>0.00001f) { printf("%s:%i: Test Fail\n%f\nvs\n%f\n", __FUNCTION__, __LINE__, a, b); /*return false;*/ }
#define EXPECT_EQ_STR(a, b) if (a != b) { printf("%s: Test Fail\n%s\nvs\n%s\n", __FUNCTION__, a.c_str(), b.c_str()); return false; }

#define RET(a) if (!(a)) { return false; }
