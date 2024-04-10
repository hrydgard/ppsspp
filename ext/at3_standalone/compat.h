#pragma once

// Compat hacks

#define av_cold
#define DECLARE_ALIGNED(bits, type, name)  type name
#define LOCAL_ALIGNED(bits, type, name, subscript) type name subscript
#define av_restrict
#define av_always_inline __forceinline
#define av_const
#define av_alias
#define av_unused
#define av_pure
#define av_warn_unused_result
#define av_assert0(cond)
#define av_assert1(cond)
#define av_assert2(cond)
#define av_log(...)
#define attribute_deprecated
#define av_printf_format(a,b)
#define avpriv_report_missing_feature(...)

#include "error.h"
