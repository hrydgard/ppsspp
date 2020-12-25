#define MY_VER_MAJOR 16
#define MY_VER_MINOR 04
#define MY_VER_BUILD 0
#define MY_VERSION_NUMBERS "16.04"
#define MY_VERSION "16.04"
#define MY_DATE "2016-10-04"
#undef MY_COPYRIGHT
#undef MY_VERSION_COPYRIGHT_DATE
#define MY_AUTHOR_NAME "Igor Pavlov"
#define MY_COPYRIGHT_PD "Igor Pavlov : Public domain"
#define MY_COPYRIGHT_CR "Copyright (c) 1999-2016 Igor Pavlov"

#ifdef USE_COPYRIGHT_CR
  #define MY_COPYRIGHT MY_COPYRIGHT_CR
#else
  #define MY_COPYRIGHT MY_COPYRIGHT_PD
#endif

#define MY_VERSION_COPYRIGHT_DATE MY_VERSION " : " MY_COPYRIGHT " : " MY_DATE
