#pragma once

#include <vector>
#include <string>

class Listable
{
public:
  virtual ~Listable() {}
  virtual const char *getItem(size_t i) const = 0;
  virtual size_t numItems() const = 0;
};

class ArrayListable : public Listable
{
public:
  ArrayListable(const char **arr, size_t count) : arr_(arr), count_(count) {}
  virtual ~ArrayListable() {}

  virtual const char *getItem(size_t i) const { return arr_[i]; }
  virtual size_t numItems() const { return count_; }

private:
  const char **arr_;
  size_t count_;
};

class VectorListable : public Listable
{
  VectorListable(const std::vector<std::string> &vec) : vec_(vec) {}
  virtual ~VectorListable() {}

  virtual const char *getItem(size_t i) const { return vec_[i].c_str(); }
  virtual size_t numItems() const { return vec_.size(); }

private:
  const std::vector<std::string> &vec_;
};