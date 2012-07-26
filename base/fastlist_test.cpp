#include "base/fastlist.h"
#include "base/logging.h"

/*
#include <gtest/gtest.h>

TEST(fastlist, AddRemove) {
  InlineFastList<int, 8> list;
  list.Add(8);
  list.Remove(7);
  EXPECT_EQ(1, list.size());
  list.Remove(8);
  EXPECT_EQ(0, list.size());
  list.Add(1);
  list.Add(2);
  list.Add(3);
  EXPECT_EQ(3, list.size());
}
*/