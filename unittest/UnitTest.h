#pragma once

#define EXPECT_TRUE(a) if (!(a)) { printf("%s:%i: Test Fail\n", __FUNCTION__, __LINE__); return false; }
#define EXPECT_FALSE(a) if ((a)) { printf("%s:%i: Test Fail\n", __FUNCTION__, __LINE__); return false; }
#define EXPECT_EQ_FLOAT(a, b) if ((a) != (b)) { printf("%s:" __LINE__ ": Test Fail\n%f\nvs\n%f\n", __FUNCTION__, a, b); return false; }
#define EXPECT_EQ_STR(a, b) if (a != b) { printf("%s: Test Fail\n%s\nvs\n%s\n", __FUNCTION__, a.c_str(), b.c_str()); return false; }

#undef ILOG
#define ILOG(x, ...) printf(x "\n", __VA_ARGS__)
#undef ELOG
#define ELOG(x, ...) printf(x "\n", __VA_ARGS__)
#undef DLOG
#define DLOG(x, ...) printf(x "\n", __VA_ARGS__)


#define RET(a) if (!(a)) { return false; }

bool TestArmEmitter();