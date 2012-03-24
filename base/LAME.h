#ifndef __LAME_H__
#define __LAME_H__

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/color.h"


#ifndef PI
#define PI 3.141592653589793f
#endif

template <class T>
inline T MIN(T t1, T t2) {
  return t1<t2 ? t1 : t2;
}
template <class T>
inline T MAX(T t1, T t2) {
  return t1>t2 ? t1 : t2;
}

template <class T>
inline T ABS(T &t) {
  if (t<0) return -t; else return t;
}

#endif //__LAME_H__
