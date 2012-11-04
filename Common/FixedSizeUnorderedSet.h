#pragma once

// Really stupid but much faster than a vector for keeping breakpoints in
// and looking them up in debug mode. STL is SOOO SLOW in debug mode on Windows.


template<class T, size_t maxCount>
class FixedSizeUnorderedSet
{
public:
  bool insert(T item)
  {
    if (count_ < maxCount - 1)
    {
      data_[count_++] = item;
      return true;
    }
    return false;
  }

  bool remove(T item)
  {
    for (u32 i = 0; i < count_; i++)
    {
      if (data_[i] == item)
      {
        if (i == count_ - 1)
        {
          count_--;
        }
        else
        {
          data_[i] = data_[count_ - 1];
          count_--;
        }
        return true;
      }
    }
    return false;
  }

  size_t size()
  {
    return count_;
  }

  T &operator[](size_t index) {
    return data_[index];
  }

  const T &operator[](size_t index) const {
    return data_[index];
  }

  void clear() {
    count_ = 0;
  }

  bool empty() const {
    return count_ != 0;
  }

private:
  T data_[maxCount];
  u32 count_;
};
